#include "manualrenderer.h"
#include "processmanager.h"
#include "workflowmanager.h"
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

ManualRenderer::ManualRenderer(const QVariantMap &params, QObject *parent)
    : QObject(parent), m_params(params)
{
    m_processManager = new ProcessManager(this);
    connect(m_processManager, &ProcessManager::processOutput, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualRenderer::onProcessFinished);
}

ManualRenderer::~ManualRenderer() {}

void ManualRenderer::start()
{
    emit logMessage("--- Начало ручного рендера ---");

    QString inputMkv = m_params["inputMkv"].toString();
    QString outputMp4 = m_params["outputMp4"].toString();
    QString renderPreset = m_params["renderPreset"].toString();
    QString extraArgs = m_params["extraArgs"].toString();

    if (inputMkv.isEmpty() || outputMp4.isEmpty()) {
        emit logMessage("Ошибка: не указан входной или выходной файл.");
        emit finished();
        return;
    }

    // Получаем длительность входного файла для расчета прогресса
    QSettings settings("MyCompany", "DubbingTool");
    QString mkvmergePath = settings.value("paths/mkvmerge").toString();
    QByteArray jsonData;
    if (m_processManager->executeAndWait(mkvmergePath, {"-J", inputMkv}, jsonData)) {
        QJsonObject root = QJsonDocument::fromJson(jsonData).object();
        if (root.contains("container")) {
            m_sourceDurationS = root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0;
            emit logMessage(QString("Длительность входного файла: %1 секунд.").arg(m_sourceDurationS));
        }
    }

    // Формируем команду
    QString ffmpegPath = settings.value("paths/ffmpeg", "ffmpeg").toString();
    QStringList args;
    args << "-y" << "-hide_banner" << "-i" << inputMkv;
    args << "-map" << "0:v:0" << "-map" << "0:a:m:language:rus";

    if(extraArgs.isEmpty()){
        QStringList vf_options;
        QString escapedInputPath = QDir::toNativeSeparators(inputMkv);
        escapedInputPath.replace(':', "\\:");
        // Выбираем поток субтитров по метаданным: язык 'rus' и флаг 'forced'
        vf_options << QString("subtitles='%1':stream_index=s:m:language:rus:forced=1").arg(escapedInputPath);
        // Рендерим так же, как в WorkflowManager
       if (renderPreset == "NVIDIA (hevc_nvenc)") {
            emit logMessage("Используется пресет NVIDIA (hevc_nvenc).");
            args << "-c:v" << "hevc_nvenc"
                 << "-preset" << "p7"
                 << "-tune" << "hq"
                 << "-profile:v" << "main"
                 << "-rc" << "vbr"
                 << "-b:v" << "4000k"
                 << "-minrate" << "4000k"
                 << "-maxrate" << "8000k"
                 << "-bufsize" << "16000k"
                 << "-rc-lookahead" << "32"
                 << "-spatial-aq" << "1"
                 << "-aq-strength" << "15"
                 << "-multipass" << "fullres"
                 << "-2pass" << "1"
                 << "-tag:v" << "hvc1"; // Тег для лучшей совместимости с Apple устройствами
        }
        else if (renderPreset == "Intel (hevc_qsv)") {
            emit logMessage("Используется пресет Intel (hevc_qsv).");
            // Добавляем опцию format=nv12 в цепочку фильтров
            // Это может быть необходимо для совместимости QSV
            if (!vf_options.isEmpty()) {
                vf_options.last().append(":force_style='PrimaryColour=&H00FFFFFF,BorderStyle=1,Outline=1'");
            }
            vf_options << "format=nv12";

            args << "-c:v" << "hevc_qsv";
            args << "-b:v" << "4000k"
                 << "-minrate" << "4000k"
                 << "-maxrate" << "8000k"
                 << "-bufsize" << "16000k";
            args << "-tag:v" << "hvc1"; // Тег для лучшей совместимости с Apple устройствами
        }
        else { // "CPU (libx265 - медленно)"
            emit logMessage("Используется пресет CPU (libx265).");
            args << "-c:v" << "libx265";
            args << "-preset" << "medium"
                 << "-crf" << "22"
                 << "-b:v" << "4000k"
                 << "-maxrate" << "8000k";
        }
    }

    args << "-c:a" << "aac" << "-b:a" << "256k";
    // Добавляем дополнительные аргументы пользователя
    if (!extraArgs.isEmpty()) {
        args << extraArgs.split(' ', Qt::SkipEmptyParts);
    }

    args << outputMp4;

    emit logMessage("Запуск ffmpeg с командой: " + ffmpegPath + " " + args.join(" "));
    emit progressUpdated(0);
    m_processManager->startProcess(ffmpegPath, args);
}

void ManualRenderer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode == 0) {
        emit logMessage("Ручной рендер успешно завершен.");
        emit progressUpdated(100);
    } else {
        emit logMessage("Ручной рендер завершился с ошибкой.");
    }
    emit finished();
}

void ManualRenderer::onProcessText(const QString &output)
{
    // Логика парсинга прогресса ffmpeg, точно такая же, как в WorkflowManager
    if (!output.trimmed().isEmpty()) {
        emit logMessage(output.trimmed());
    }

    QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch()) {
        int hours = match.captured(1).toInt();
        int minutes = match.captured(2).toInt();
        int seconds = match.captured(3).toInt();
        double currentTimeS = (hours * 3600) + (minutes * 60) + seconds;
        if (m_sourceDurationS > 0) {
            int percentage = static_cast<int>((currentTimeS / m_sourceDurationS) * 100);
            emit progressUpdated(percentage);
        }
    }
}

ProcessManager* ManualRenderer::getProcessManager() const {
    return m_processManager;
}
