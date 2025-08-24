#include "manualassembler.h"
#include "processmanager.h"
#include "assprocessor.h"
#include "releasetemplate.h"
#include "appsettings.h"
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>


ManualAssembler::ManualAssembler(const QVariantMap &params, QObject *parent)
    : QObject(parent), m_params(params)
{
    m_processManager = new ProcessManager(this);
    m_assProcessor = new AssProcessor(this);
    m_progressTimer = new QTimer(this);

    connect(m_processManager, &ProcessManager::processOutput, this, &ManualAssembler::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualAssembler::onProcessText);
    connect(m_assProcessor, &AssProcessor::logMessage, this, &ManualAssembler::logMessage);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualAssembler::onProcessFinished);
}

ManualAssembler::~ManualAssembler() {}

void ManualAssembler::start()
{
    emit logMessage("--- Начало ручной сборки ---", LogCategory::APP);
    m_currentStep = Step::Idle;

    if (m_params["normalizeAudio"].toBool()) {
        normalizeAudio();
    } else if (m_params["convertAudio"].toBool()) {
        convertAudio();
    } else {
        processSubtitlesAndAssemble();
    }
}

void ManualAssembler::normalizeAudio()
{
    m_currentStep = Step::NormalizingAudio;
    emit logMessage("Шаг 1: Нормализация аудио...", LogCategory::APP);
    emit progressUpdated(-1, "Нормализация аудио");

    QString nugenPath = AppSettings::instance().nugenAmbPath();
    QString originalAudioPath  = m_params["russianAudioPath"].toString();

    if (nugenPath.isEmpty() || !originalAudioPath .endsWith(".wav", Qt::CaseInsensitive)) {
        emit logMessage("Нормализация пропущена (не указан путь к NUGEN или файл не .wav).", LogCategory::APP);
        if (m_params["convertAudio"].toBool()) convertAudio();
        else processSubtitlesAndAssemble();
        return;
    }

    m_originalAudioPathBeforeNormalization = originalAudioPath;
    QFileInfo originalInfo(m_originalAudioPathBeforeNormalization);
    QString tempInputPath = originalInfo.dir().filePath("temp_audio_for_nugen.wav");

    if (QFile::exists(tempInputPath)) {
        QFile::remove(tempInputPath);
    }

    if (!QFile::copy(originalAudioPath, tempInputPath)) {
        emit logMessage("ОШИБКА: Не удалось переименовать аудиофайл. Нормализация отменена.", LogCategory::APP);
        if (m_params["convertAudio"].toBool()) convertAudio();
        else processSubtitlesAndAssemble();
        return;
    }

    emit logMessage("Аудиофайл переименован в temp_audio_for_nugen.wav  для безопасной обработки.", LogCategory::APP);

    QFileInfo nugenInfo(nugenPath);
    QString ambCmdPath = nugenInfo.dir().filePath("AMBCmd.exe");
    emit progressUpdated(-1, "Запуск NUGEN Audio AMB");
    m_didLaunchNugen = true;

    if (!QFileInfo::exists(ambCmdPath)) {
        emit logMessage("Ошибка: AMBCmd.exe не найден. Нормализация пропущена.", LogCategory::APP);
        if (m_params["convertAudio"].toBool()) convertAudio();
        else processSubtitlesAndAssemble();
        return;
    }

    QProcess::startDetached(nugenPath);
    QTimer::singleShot(3000, this, [this, ambCmdPath, tempInputPath](){
        m_processManager->startProcess(ambCmdPath, {"-a", tempInputPath});
        emit progressUpdated(-1, "Нормализация аудиофайла");
    });
}

void ManualAssembler::convertAudio()
{
    m_currentStep = Step::ConvertingAudio;
    emit logMessage("Шаг 2: Конвертация аудио...", LogCategory::APP);

    QString audioPath = m_params["russianAudioPath"].toString();
    QString targetFormat = m_params.value("convertAudioFormat", "aac").toString();

    if (audioPath.endsWith("." + targetFormat, Qt::CaseInsensitive)) {
        emit logMessage("Аудио уже в нужном формате, конвертация пропущена.", LogCategory::APP);
        processSubtitlesAndAssemble();
        return;
    }

    emit logMessage("Определение длительности аудиофайла...", LogCategory::APP);
    QByteArray jsonData;
    QFileInfo ffmpegInfo(AppSettings::instance().ffmpegPath());
    QString ffprobePath = QDir(ffmpegInfo.absolutePath()).filePath("ffprobe.exe");
    QStringList ffprobeArgs = {"-v", "error", "-show_format", "-print_format", "json", audioPath};

    m_sourceAudioDurationS = 0.0;
    if (m_processManager->executeAndWait(ffprobePath, ffprobeArgs, jsonData) && !jsonData.isEmpty()) {
        QJsonObject format = QJsonDocument::fromJson(jsonData).object()["format"].toObject();
        m_sourceAudioDurationS = format["duration"].toString().toDouble();
        emit logMessage(QString("Длительность: %1 секунд.").arg(m_sourceAudioDurationS), LogCategory::APP);
    } else {
        emit logMessage("Предупреждение: не удалось определить длительность аудио. Прогресс не будет отображаться.", LogCategory::APP);
    }

    emit progressUpdated(0, "Конвертация аудио");
    QString newAudioPath = QFileInfo(audioPath).dir().filePath(QFileInfo(audioPath).baseName() + "_converted." + targetFormat);
    m_params["russianAudioPath"] = newAudioPath;

    m_progressLogPath = QDir::temp().filePath("dt_manual_audio_progress.log");
    QFile::remove(m_progressLogPath);

    QStringList args;
    args << "-y" << "-i" << audioPath;
    if (targetFormat == "aac") args << "-c:a" << "aac" << "-b:a" << "256k";
    else if (targetFormat == "flac") args << "-c:a" << "flac";
    args << "-progress" << QDir::toNativeSeparators(m_progressLogPath) << newAudioPath;

    if (m_sourceAudioDurationS > 0) {
        connect(m_progressTimer, &QTimer::timeout, this, &ManualAssembler::onConversionProgress);
        m_progressTimer->start(500);
    }

    m_processManager->startProcess(AppSettings::instance().ffmpegPath(), args);
}

void ManualAssembler::processSubtitlesAndAssemble()
{
    if (!m_params["isManualMode"].toBool() && m_params["addTb"].toBool()) {
        emit logMessage("Добавление ТБ в субтитры...", LogCategory::APP);

        ReleaseTemplate t;
        QFile file("templates/" + m_params["templateName"].toString() + ".json");
        if (file.open(QIODevice::ReadOnly)) {
            t.read(QJsonDocument::fromJson(file.readAll()).object());
        } else {
            emit logMessage("Ошибка: не удалось загрузить шаблон для генерации ТБ.", LogCategory::APP);
            assemble();
            return;
        }

        QString tbStartTime = m_params["tbStartTime"].toString();
        t.defaultTbStyleName = m_params["tbStyleName"].toString();

        QString subsPath = m_params.value("subtitlesPath").toString();
        if (!subsPath.isEmpty()) {
            QString newSubsPathBase = QFileInfo(subsPath).dir().filePath(QFileInfo(subsPath).baseName() + "_with_tb");
            m_assProcessor->processExistingFile(subsPath, newSubsPathBase, t, tbStartTime);
            m_params["subtitlesPath"] = newSubsPathBase + "_full.ass";
        }

        QString signsPath = m_params.value("signsPath").toString();
        if (!signsPath.isEmpty()) {
            QString newSignsPathBase = QFileInfo(signsPath).dir().filePath(QFileInfo(signsPath).baseName() + "_with_tb");
            m_assProcessor->processExistingFile(signsPath, newSignsPathBase, t, tbStartTime);
            m_params["signsPath"] = newSignsPathBase + "_signs.ass";
        }
    }

    assemble();
}

void ManualAssembler::assemble()
{
    m_currentStep = Step::AssemblingMkv;
    emit logMessage("Сборка MKV файла...", LogCategory::APP);
    emit progressUpdated(-1, "Сборка MKV");

    QString mkvmergePath = AppSettings::instance().mkvmergePath();
    QString workDir = m_params["workDir"].toString();
    QString outputName = m_params["outputName"].toString();

    if (outputName.isEmpty()) {
        emit logMessage("Критическая ошибка: не указан выходной файл.", LogCategory::APP);
        emit finished();
        return;
    }

    QString fullOutputPath;
    if (!workDir.isEmpty()) {
        fullOutputPath = QDir(workDir).filePath(outputName);
    } else {
        QString videoPath = m_params["videoPath"].toString();
        if (!videoPath.isEmpty()) {
            fullOutputPath = QDir(QFileInfo(videoPath).path()).filePath(outputName);
        } else {
            fullOutputPath = outputName;
        }
    }

    if (fullOutputPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не удалось определить путь для сохранения файла.", LogCategory::APP);
        emit finished();
        return;
    }

    m_finalMkvPath = fullOutputPath;
    QStringList args;
    args << "-o" << fullOutputPath;

    // Шрифты
    QStringList fontPaths = m_params["fontPaths"].toStringList();
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
    if (m_params["isManualMode"].toBool()) {
        QString studio = m_params["studio"].toString();
        QString lang = m_params["language"].toString();
        QString subAuthor = m_params["subAuthor"].toString();
        if (m_params.contains("videoPath")) args << "--language" << "0:" + lang << "--track-name" << QString("0:Видеоряд [%1]").arg(studio) << m_params["videoPath"].toString();
        if (m_params.contains("russianAudioPath")) args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]" << m_params["russianAudioPath"].toString();
        if (m_params.contains("originalAudioPath")) args << "--language" << "0:" + lang << "--track-name" << QString("0:Оригинал [%1]").arg(studio) << m_params["originalAudioPath"].toString();
        if (m_params.contains("signsPath")) args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << QString("0:Надписи [%1]").arg(subAuthor) << m_params["signsPath"].toString();
        if (m_params.contains("subtitlesPath")) args << "--language" << "0:rus" << "--track-name" << QString("0:Субтитры [%1]").arg(subAuthor) << m_params["subtitlesPath"].toString();
    } else {
        ReleaseTemplate t;
        QFile file("templates/" + m_params["templateName"].toString() + ".json");
        if (file.open(QIODevice::ReadOnly)) t.read(QJsonDocument::fromJson(file.readAll()).object());
        if (m_params.contains("videoPath")) args << "--language" << "0:" + t.originalLanguage << "--track-name" << QString("0:Видеоряд [%1]").arg(t.animationStudio) << m_params["videoPath"].toString();
        if (m_params.contains("russianAudioPath")) args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]" << m_params["russianAudioPath"].toString();
        if (m_params.contains("originalAudioPath")) args << "--language" << "0:" + t.originalLanguage << "--track-name" << QString("0:Оригинал [%1]").arg(t.animationStudio) << m_params["originalAudioPath"].toString();
        if (m_params.contains("signsPath")) args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << QString("0:Надписи [%1]").arg(t.subAuthor) << m_params["signsPath"].toString();
        if (m_params.contains("subtitlesPath")) args << "--language" << "0:rus" << "--track-name" << QString("0:Субтитры [%1]").arg(t.subAuthor) << m_params["subtitlesPath"].toString();
    }

    m_processManager->startProcess(mkvmergePath, args);
}

void ManualAssembler::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_processManager && m_processManager->wasKilled()) {
        emit logMessage("Ручная сборка отменена пользователем.", LogCategory::APP);
        emit finished();
        return;
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.", LogCategory::APP);
        emit finished();
        return;
    }

    if (m_currentStep == Step::NormalizingAudio) {
        emit logMessage("Нормализация аудио завершена.", LogCategory::APP);

        if (m_didLaunchNugen) {
            emit logMessage("Закрытие NUGEN Audio AMB...", LogCategory::APP);
            QProcess::execute("taskkill", {"/F", "/IM", "NUGEN Audio AMB.exe", "/T"});
            m_didLaunchNugen = false;
        }
        QFileInfo originalInfo(m_originalAudioPathBeforeNormalization);
        QString tempPath = originalInfo.dir().filePath("temp_audio_for_nugen.wav");
        QString normalizedTempPath = originalInfo.dir().filePath("temp_audio_for_nugen_corrected.wav");

        if (QFileInfo::exists(normalizedTempPath)) {
            QString finalBaseName = originalInfo.baseName() + "_corrected.wav";
            QString finalPath = originalInfo.dir().filePath(finalBaseName);

            if (QFile::exists(finalPath)) {
                QFile::remove(finalPath);
            }
            if (QFile::rename(normalizedTempPath, finalPath)) {
                emit logMessage("Нормализованный файл сохранен как: " + finalPath, LogCategory::APP);
                m_params["russianAudioPath"] = finalPath;
            } else {
                emit logMessage("ОШИБКА: Не удалось скопировать нормализованный файл. Будет использован исходный файл.", LogCategory::APP);
                m_params["russianAudioPath"] = m_originalAudioPathBeforeNormalization;
            }
        } else {
            emit logMessage("ПРЕДУПРЕЖДЕНИЕ: Нормализованный файл не найден. Будет использован исходный.", LogCategory::APP);
            QFile::rename(tempPath, m_originalAudioPathBeforeNormalization);
            m_params["russianAudioPath"] = m_originalAudioPathBeforeNormalization;
        }
        QFile::remove(tempPath);
        QFile::remove(normalizedTempPath);
        if (m_params["convertAudio"].toBool()) {
            convertAudio();
        } else {
            processSubtitlesAndAssemble();
        }
    }
    else if (m_currentStep == Step::ConvertingAudio) {
        m_progressTimer->stop();
        disconnect(m_progressTimer, &QTimer::timeout, this, &ManualAssembler::onConversionProgress);
        QFile::remove(m_progressLogPath);
        emit logMessage("Конвертация аудио завершена.", LogCategory::APP);
        processSubtitlesAndAssemble();
    }
    else if (m_currentStep == Step::AssemblingMkv) {
        emit logMessage("Ручная сборка MKV успешно завершена.", LogCategory::APP);
        emit finished();
    }
}

void ManualAssembler::onProcessText(const QString &output)
{
    if (!output.trimmed().isEmpty()) {
        LogCategory category = (m_currentStep == Step::ConvertingAudio) ? LogCategory::FFMPEG : LogCategory::MKVTOOLNIX;
        emit logMessage(output.trimmed(), category);
    }

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

// void ManualAssembler::onConversionProgress()
// {
//     QFile progressFile(m_progressLogPath);
//     if (!progressFile.open(QIODevice::ReadOnly)) return;

//     QTextStream in(&progressFile);
//     QString logContent = in.readAll();
//     progressFile.close();

//     int lastPos = logContent.lastIndexOf("out_time_us=");
//     if (lastPos == -1) return;

//     QString timeUsStr = logContent.mid(lastPos + 12);
//     qint64 currentTimeUs = timeUsStr.toLongLong();

//     if (m_sourceAudioDurationS > 0) {
//         double totalUs = m_sourceAudioDurationS * 1000000.0;
//         int percentage = (currentTimeUs * 100)/ totalUs;
//         emit progressUpdated(percentage, "Конвертация аудио");
//     }
// }
void ManualAssembler::onConversionProgress()
{
    QFile progressFile(m_progressLogPath);
    if (!progressFile.open(QIODevice::ReadOnly)) return;

    QTextStream in(&progressFile);
    qint64 totalDurationUs = 0;
    qint64 currentTimeUs = 0;

    totalDurationUs = m_sourceAudioDurationS * 1000000;

    while(!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("out_time_us=")) {
            currentTimeUs = line.split('=').last().toLongLong();
        }
    }
    progressFile.close();

    if (totalDurationUs > 0) {
        int percentage = (currentTimeUs * 100) / totalDurationUs;
        emit progressUpdated(percentage, "Конвертация аудио");
    }
}

void ManualAssembler::cancelOperation()
{
    emit logMessage("Получена команда на отмену ручной сборки...", LogCategory::APP);
    if (m_processManager) {
        m_processManager->killProcess();
    }
}

ProcessManager* ManualAssembler::getProcessManager() const {
    return m_processManager;
}
