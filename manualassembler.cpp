#include "manualassembler.h"
#include "processmanager.h"
#include "assprocessor.h"
#include "releasetemplate.h"
#include "workflowmanager.h"
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QCoreApplication>


ManualAssembler::ManualAssembler(const QVariantMap &params, QObject *parent)
    : QObject(parent), m_params(params)
{
    m_processManager = new ProcessManager(this);
    m_assProcessor = new AssProcessor(this);

    connect(m_processManager, &ProcessManager::processOutput, this, &ManualAssembler::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualAssembler::onProcessText);
    connect(m_assProcessor, &AssProcessor::logMessage, this, &ManualAssembler::logMessage);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualAssembler::onProcessFinished);
}

ManualAssembler::~ManualAssembler() {}

void ManualAssembler::start()
{
    emit logMessage("--- Начало ручной сборки ---");

    QString templateName = m_params["templateName"].toString();
    if (templateName.isEmpty()) {
        emit logMessage("Критическая ошибка: базовый шаблон не был выбран.");
        emit finished();
        return;
    }

    QDir templatesDir(QCoreApplication::applicationDirPath());
    templatesDir.cd("../templates"); // Переходим в папку templates
    QString templatePath = templatesDir.filePath(templateName + ".json");

    QFile file(templatePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage("Критическая ошибка: не удалось загрузить шаблон по пути " + templatePath);
        emit finished();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    ReleaseTemplate t;
    t.read(doc.object());

    if (m_params["addTb"].toBool()) {
        emit logMessage("Добавление ТБ в субтитры...");

        ReleaseTemplate tbTemplate = t;
        QString tbStartTime = m_params["tbStartTime"].toString();
        tbTemplate.defaultTbStyleName = m_params["tbStyleName"].toString();

        QString subsPath = m_params.value("subtitlesPath").toString();
        if (!subsPath.isEmpty()) {
            QString newSubsPath = QFileInfo(subsPath).path() + "/" + QFileInfo(subsPath).baseName() + "_with_tb.ass";
            m_assProcessor->processExistingFile(subsPath, newSubsPath, tbTemplate, tbStartTime);
            m_params["subtitlesPath"] = newSubsPath;
        }

        QString signsPath = m_params.value("signsPath").toString();
        if (!signsPath.isEmpty()) {
            QString newSignsPath = QFileInfo(signsPath).path() + "/" + QFileInfo(signsPath).baseName() + "_with_tb.ass";
            m_assProcessor->processExistingFile(signsPath, newSignsPath, tbTemplate, tbStartTime);
            m_params["signsPath"] = newSignsPath;
        }
    }

    assemble(t);
}

void ManualAssembler::assemble(const ReleaseTemplate &t)
{
    m_currentStep = Step::AssemblingMkv;
    emit logMessage("Сборка MKV файла...");
    emit progressUpdated(-1, "Сборка MKV");

    QSettings settings("MyCompany", "DubbingTool");
    QString mkvmergePath = settings.value("paths/mkvmerge", "mkvmerge").toString();
    m_ffmpegPath = settings.value("paths/ffmpeg", "ffmpeg").toString(); // Загружаем путь к ffmpeg

    QString workDir = m_params["workDir"].toString();
    QString outputName = m_params["outputName"].toString();
    m_finalMkvPath = outputName; // Сохраняем для рендера
    QString videoPath = m_params["videoPath"].toString();
    QString originalAudioPath = m_params["originalAudioPath"].toString();
    QString russianAudioPath = m_params["russianAudioPath"].toString();
    QString subtitlesPath = m_params["subtitlesPath"].toString();
    QString signsPath = m_params["signsPath"].toString();
    QStringList fontPaths = m_params["fontPaths"].toStringList();

    // Проверка обязательных полей
    if (outputName.isEmpty()) {
        emit logMessage("Критическая ошибка: не указан выходной файл.");
        emit finished();
        return;
    }

    QString fullOutputPath;
    if (!workDir.isEmpty()) {
        fullOutputPath = QDir(workDir).filePath(outputName);
    } else {
        // Если рабочая папка не указана, сохраняем рядом с видеофайлом
        QString videoPath = m_params["videoPath"].toString();
        if (!videoPath.isEmpty()) {
            fullOutputPath = QDir(QFileInfo(videoPath).path()).filePath(outputName);
        } else {
            // Если и видео нет, то сохраняем в текущую папку (маловероятно)
            fullOutputPath = outputName;
        }
    }

    if (fullOutputPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не удалось определить путь для сохранения файла.");
        emit finished();
        return;
    }

    m_finalMkvPath = fullOutputPath;

    QStringList args;
    args << "-o" << fullOutputPath;

    // Шрифты
    for(const QString& path : fontPaths) {
        args << "--attachment-name" << QFileInfo(path).fileName();
        QString mimeType = "application/octet-stream";
        if (path.endsWith(".ttf", Qt::CaseInsensitive)) mimeType = "application/x-font-ttf";
        else if (path.endsWith(".otf", Qt::CaseInsensitive)) mimeType = "application/vnd.ms-opentype";
        else if (path.endsWith(".ttc", Qt::CaseInsensitive)) mimeType = "application/font-collection";
        args << "--attachment-mime-type" << mimeType;
        args << "--attach-file" << path;
    }

    // Дорожки
    if (m_params.contains("videoPath") && !m_params.value("videoPath").toString().isEmpty()) {
        args << "--language" << "0:" + t.originalLanguage << "--track-name" << QString("0:Видеоряд [%1]").arg(t.animationStudio) << videoPath;
    }
    if (m_params.contains("russianAudioPath") && !m_params.value("russianAudioPath").toString().isEmpty()) {
        args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]" << russianAudioPath;
    }
    if (m_params.contains("originalAudioPath") && !m_params.value("originalAudioPath").toString().isEmpty()) {
        args << "--language" << "0:" + t.originalLanguage << "--track-name" << QString("0:Оригинал [%1]").arg(t.animationStudio) << originalAudioPath;
    }
    if (m_params.contains("signsPath") && !m_params.value("signsPath").toString().isEmpty()) {
        args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << QString("0:Надписи [%1]").arg(t.subAuthor) << signsPath;
    }
    if (m_params.contains("subtitlesPath") && !m_params.value("subtitlesPath").toString().isEmpty()) {
        args << "--language" << "0:rus" << "--track-name" << QString("0:Субтитры [%1]").arg(t.subAuthor) << subtitlesPath;
    }
    m_processManager->startProcess(mkvmergePath, args);
}

void ManualAssembler::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.");
        emit finished();
        return;
    }

    if (m_currentStep == Step::AssemblingMkv) {
        emit logMessage("Ручная сборка MKV успешно завершена.");
        emit finished();
    }
}

void ManualAssembler::onProcessText(const QString &output)
{
    if (!output.trimmed().isEmpty()) {
        emit logMessage(output.trimmed());
    }

    // Парсим прогресс от mkvmerge
    if (m_currentStep == Step::AssemblingMkv) {
        QRegularExpression re("Progress: (\\d+)%");
        auto it = re.globalMatch(output);
        while (it.hasNext()) {
            auto match = it.next();
            int percentage = match.captured(1).toInt();
            emit progressUpdated(percentage, "Сборка MKV");
        }
    }
}

ProcessManager* ManualAssembler::getProcessManager() const {
    return m_processManager;
}
