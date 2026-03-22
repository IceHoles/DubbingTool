#include "manualrenderer.h"

#include "appsettings.h"
#include "assprocessor.h"
#include "processmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

namespace
{
bool parseFpsRational(const QString& fps, qint64& num, qint64& den)
{
    const QStringList parts = fps.split('/');
    if (parts.size() != 2)
    {
        return false;
    }
    bool numOk = false;
    bool denOk = false;
    const qint64 parsedNum = parts[0].toLongLong(&numOk);
    const qint64 parsedDen = parts[1].toLongLong(&denOk);
    if (!numOk || !denOk || parsedNum <= 0 || parsedDen <= 0)
    {
        return false;
    }
    num = parsedNum;
    den = parsedDen;
    return true;
}

bool isLikelyCfr(const QString& rFrameRate, const QString& avgFrameRate)
{
    qint64 rNum = 0;
    qint64 rDen = 0;
    qint64 avgNum = 0;
    qint64 avgDen = 0;
    if (!parseFpsRational(rFrameRate, rNum, rDen) || !parseFpsRational(avgFrameRate, avgNum, avgDen))
    {
        return false;
    }
    const double r = static_cast<double>(rNum) / static_cast<double>(rDen);
    const double avg = static_cast<double>(avgNum) / static_cast<double>(avgDen);
    return qAbs(r - avg) < 0.001;
}
} // namespace

ManualRenderer::ManualRenderer(const QVariantMap& params, QObject* parent)
    : QObject(parent), m_params(params), m_processManager(new ProcessManager(this))
{
    connect(m_processManager, &ProcessManager::processOutput, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processError, this, &ManualRenderer::onProcessText);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualRenderer::onProcessFinished);
}

ManualRenderer::~ManualRenderer()
{
}

void ManualRenderer::start()
{
    emit logMessage("--- Начало ручного рендера ---", LogCategory::APP);
    emit progressUpdated(-1, "Рендер MP4");

    QString inputMkv = m_params["inputMkv"].toString();
    QString presetName = m_params["renderPresetName"].toString();

    if (inputMkv.isEmpty() || presetName.isEmpty())
    {
        emit logMessage("Ошибка: не указан входной файл или не выбран пресет рендера.", LogCategory::APP);
        emit finished();
        return;
    }

    // Получаем актуальный пресет из настроек
    m_preset = AppSettings::instance().findRenderPreset(presetName);
    if (m_preset.name.isEmpty())
    {
        emit logMessage("Критическая ошибка: пресет рендера '" + presetName + "' не найден в настройках.",
                        LogCategory::APP);
        emit finished();
        return;
    }

    QByteArray jsonData;
    QString videoCodecExtension = "h264";
    int detectedVideoBitrateKbps = -1;
    QString detectedVideoFrameRate;
    QString detectedVideoAvgFrameRate;
    bool detectedVideoIsCfr = false;
    if (m_processManager->executeAndWait(AppSettings::instance().mkvmergePath(), {"-J", inputMkv}, jsonData))
    {
        QJsonObject root = QJsonDocument::fromJson(jsonData).object();
        if (root.contains("container"))
        {
            m_sourceDurationS = static_cast<qint64>(
                root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0);
        }

        QJsonArray tracks = root["tracks"].toArray();
        for (const QJsonValue& val : tracks)
        {
            QJsonObject track = val.toObject();
            if (track["type"].toString() != "video")
            {
                continue;
            }
            QString codec = track["codec"].toString().toLower();
            if (codec.contains("hevc") || codec.contains("h265"))
            {
                videoCodecExtension = "h265";
            }
            else
            {
                videoCodecExtension = "h264";
            }
            break;
        }
    }

    // Match WorkflowManager behavior for bitrate-limited CRF: read bitrate from ffprobe stream metadata.
    const QString ffprobePath = AppSettings::instance().ffprobePath();
    QString bitrateSource = "none";
    if (!ffprobePath.isEmpty() && QFileInfo::exists(ffprobePath))
    {
        QByteArray probeData;
        QStringList probeArgs = {"-v", "quiet", "-print_format", "json", "-show_streams", inputMkv};
        if (m_processManager->executeAndWait(ffprobePath, probeArgs, probeData) && !probeData.isEmpty())
        {
            const QJsonObject probeRoot = QJsonDocument::fromJson(probeData).object();
            const QJsonArray streams = probeRoot.value("streams").toArray();
            for (const auto& streamVal : streams)
            {
                const QJsonObject stream = streamVal.toObject();
                if (stream.value("codec_type").toString() != "video")
                {
                    continue;
                }
                const QString bitRateStr = stream.value("bit_rate").toString();
                if (!bitRateStr.isEmpty())
                {
                    detectedVideoBitrateKbps = static_cast<int>(bitRateStr.toLongLong() / 1000);
                    bitrateSource = "stream.bit_rate";
                }
                detectedVideoFrameRate = stream.value("r_frame_rate").toString();
                detectedVideoAvgFrameRate = stream.value("avg_frame_rate").toString();
                detectedVideoIsCfr = isLikelyCfr(detectedVideoFrameRate, detectedVideoAvgFrameRate);
                break;
            }
        }
    }

    if (m_sourceDurationS == 0)
    {
        emit logMessage("Предупреждение: не удалось определить длительность файла. Прогресс не будет отображаться.",
                        LogCategory::APP);
    }

    const bool useConcatTb = m_params.value("useConcatTb").toBool();
    const bool useHardsub = m_params.value("useHardsub").toBool();
    if (useConcatTb && useHardsub)
    {
        emit logMessage("Включен режим умного рендера ТБ (concat).", LogCategory::APP);
        TbSegment segment;

        const QString hardsubMode = m_params.value("hardsubMode").toString();
        if (hardsubMode == "external")
        {
            const QString subsPath = m_params.value("externalSubsPath").toString();
            segment = AssProcessor::detectTbSegmentFromFile(subsPath);
        }
        else if (hardsubMode == "internal")
        {
            const int subtitleTrackIndex = m_params.value("subtitleTrackIndex").toInt();
            const QString tempAssPath =
                QDir(QFileInfo(inputMkv).absolutePath()).filePath(QString("temp_internal_subs_%1.ass").arg(subtitleTrackIndex));
            QFile::remove(tempAssPath);

            QByteArray extractOutput;
            const QString ffmpegPath = AppSettings::instance().ffmpegPath();
            QStringList extractArgs;
            extractArgs << "-y" << "-i" << inputMkv << "-map" << QString("0:s:%1").arg(subtitleTrackIndex)
                        << "-c:s" << "ass" << tempAssPath;
            if (m_processManager->executeAndWait(ffmpegPath, extractArgs, extractOutput) && QFileInfo::exists(tempAssPath))
            {
                segment = AssProcessor::detectTbSegmentFromFile(tempAssPath);
            }
            else
            {
                emit logMessage("Concat рендер: не удалось извлечь внутреннюю дорожку субтитров в .ass.",
                                LogCategory::APP);
            }
            if (segment.isValid())
            {
                m_tempConcatSubsPath = tempAssPath;
            }
            else
            {
                QFile::remove(tempAssPath);
            }
        }

        if (segment.isValid())
        {
            emit logMessage(QString("Concat рендер: найден сегмент ТБ: %1s - %2s")
                                .arg(segment.startSeconds, 0, 'f', 3)
                                .arg(segment.endSeconds, 0, 'f', 3),
                            LogCategory::APP);
            const QString outputMp4 = QFileInfo(m_params.value("outputMp4").toString()).absoluteFilePath();
            QString hardsubModeForConcat = m_params.value("hardsubMode").toString();
            const int subtitleTrackIndex = m_params.value("subtitleTrackIndex").toInt();
            QString externalSubsPath = m_params.value("externalSubsPath").toString();
            if (!m_tempConcatSubsPath.isEmpty())
            {
                hardsubModeForConcat = "external";
                externalSubsPath = m_tempConcatSubsPath;
            }

            m_concatRenderer = new ConcatTbRenderer(inputMkv, outputMp4, segment, m_sourceDurationS, videoCodecExtension,
                                                    hardsubModeForConcat, subtitleTrackIndex, externalSubsPath,
                                                    detectedVideoBitrateKbps, detectedVideoFrameRate,
                                                    detectedVideoAvgFrameRate, detectedVideoIsCfr,
                                                    m_processManager, this);
            connect(m_concatRenderer, &ConcatTbRenderer::logMessage, this, &ManualRenderer::logMessage);
            connect(m_concatRenderer, &ConcatTbRenderer::progressUpdated, this, &ManualRenderer::progressUpdated);
            connect(m_concatRenderer, &ConcatTbRenderer::finished, this,
                    [this]()
                    {
                        if (!m_tempConcatSubsPath.isEmpty())
                        {
                            QFile::remove(m_tempConcatSubsPath);
                            m_tempConcatSubsPath.clear();
                        }
                        m_concatRenderer = nullptr;
                        emit finished();
                    });
            m_concatRenderer->start();
            return;
        }

        if (!m_tempConcatSubsPath.isEmpty())
        {
            QFile::remove(m_tempConcatSubsPath);
            m_tempConcatSubsPath.clear();
        }
        emit logMessage("Concat рендер: границы ТБ не найдены, используется обычный полный рендер.", LogCategory::APP);
    }

    m_currentStep = Step::Pass1;
    runPass(m_currentStep);
}

void ManualRenderer::runPass(Step pass)
{
    QString commandTemplate = (pass == Step::Pass1) ? m_preset.commandPass1 : m_preset.commandPass2;

    QStringList args = prepareCommandArguments(commandTemplate);
    if (args.isEmpty())
    {
        emit logMessage("Ошибка: не удалось подготовить команду для рендера.", LogCategory::APP);
        emit finished();
        return;
    }

    QString program = args.takeFirst();

    emit logMessage(QString("Запуск прохода: ") + program + " " + args.join(" "), LogCategory::APP);
    m_processManager->startProcess(program, args);
}

QStringList ManualRenderer::prepareCommandArguments(const QString& commandTemplate)
{
    QString processedTemplate = commandTemplate;

    // 1. Подставляем пути
    QString inputMkv = QFileInfo(m_params.value("inputMkv").toString()).absoluteFilePath();
    QString outputMp4 = QFileInfo(m_params.value("outputMp4").toString()).absoluteFilePath();

    processedTemplate.replace("%OUTPUT%", outputMp4);

    // 2. Обрабатываем фильтр субтитров
    bool useHardsub = m_params.value("useHardsub").toBool();

    if (useHardsub)
    {
        QString hardsubMode = m_params.value("hardsubMode").toString();
        QString filterValue;

        if (hardsubMode == "internal")
        {
            QFileInfo fileInfo(inputMkv);
            m_processManager->setWorkingDirectory(fileInfo.absolutePath());

            QFile::rename(fileInfo.absoluteFilePath(),
                          fileInfo.absolutePath() + QDir::separator() + "temp_name_to_extract_subtitle.mkv");
            QFileInfo tempFileInfo(fileInfo.absolutePath() + QDir::separator() + "temp_name_to_extract_subtitle.mkv");
            processedTemplate.replace("%INPUT%", tempFileInfo.absoluteFilePath());

            int trackIndex = m_params.value("subtitleTrackIndex").toInt();
            QString escapedName = tempFileInfo.fileName().replace("'", "\\'");
            // Формируем specifier 's:<index>', например 's:0' для первой дорожки субтитров
            filterValue = QString("filename='%1':si=%2").arg(escapedName).arg(trackIndex);

            emit logMessage(QString("Hardsub: внутренняя дорожка #%1 (относительный путь).").arg(trackIndex),
                            LogCategory::APP);
        }
        else
        { // "external"
            QFileInfo fileInfo(m_params.value("externalSubsPath").toString());
            m_processManager->setWorkingDirectory(fileInfo.absolutePath());
            QString escapedName = fileInfo.fileName().replace("'", "\\'");
            filterValue = QString("filename='%1'").arg(escapedName);

            emit logMessage(QString("Hardsub: внешний файл %1 (относительный путь).").arg(fileInfo.fileName()),
                            LogCategory::APP);
        }

        // Подставляем готовый фильтр в плейсхолдер
        processedTemplate.replace("%SIGNS%", filterValue);
    }
    else
    {
        // Если hardsub отключен, нужно аккуратно удалить сам плейсхолдер и опцию -vf, если она относится только к нему.
        // Простой вариант - заменить плейсхолдер на пустую строку, но это может оставить -vf "" в команде.
        // Надежный вариант:
        // QRegularExpression filterRegex(R"(-vf\s+\"[^\"]*%SIGNS%[^\"]*\")");
        // processedTemplate.remove(filterRegex);

        processedTemplate.replace("subtitles=%SIGNS%", "");
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*,\s*\")"));
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*\")"));

        emit logMessage("Hardsub отключен. Фильтр субтитров удален из команды.", LogCategory::APP);
    }
    processedTemplate.replace("%INPUT%", inputMkv);

    // 3. Используем QProcess для безопасного разбора готовой строки в список аргументов
    return QProcess::splitCommand(processedTemplate);
}

void ManualRenderer::cancelOperation()
{
    emit logMessage("Получена команда на отмену ручного рендера...", LogCategory::APP);
    if (m_processManager != nullptr)
    {
        m_processManager->killProcess();
    }
}

void ManualRenderer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_concatRenderer != nullptr)
    {
        return;
    }

    QFileInfo fileInfo = QFileInfo(m_params.value("inputMkv").toString());
    QFile::rename(fileInfo.absolutePath() + QDir::separator() + "temp_name_to_extract_subtitle.mkv",
                  fileInfo.absoluteFilePath());

    if ((m_processManager != nullptr) && m_processManager->wasKilled())
    {
        emit logMessage("Ручной рендер отменен пользователем.", LogCategory::APP);
        emit finished();
        return;
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        emit logMessage("Ошибка выполнения дочернего процесса. Рендер остановлен.", LogCategory::APP);
        emit finished();
        return;
    }

    if (m_currentStep == Step::Pass1)
    {
        if (m_preset.isTwoPass())
        {
            emit logMessage("Первый проход успешно завершен.", LogCategory::APP);
            m_currentStep = Step::Pass2;
            runPass(m_currentStep);
        }
        else
        {
            emit logMessage("Рендер успешно завершен (один проход).", LogCategory::APP);
            RenderHelper* helper = new RenderHelper(m_preset, m_params["outputMp4"].toString(), m_processManager, this);
            connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
            connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
            connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
            helper->startCheck();
        }
    }
    else
    { // Step::Pass2
        emit logMessage("Второй проход и весь рендер успешно завершены.", LogCategory::APP);
        RenderHelper* helper = new RenderHelper(m_preset, m_params["outputMp4"].toString(), m_processManager, this);
        connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
        connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
        connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
        helper->startCheck();
    }
}

void ManualRenderer::onProcessText(const QString& output)
{
    if (!output.trimmed().isEmpty())
    {
        emit logMessage(output.trimmed(), LogCategory::FFMPEG);
    }

    QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
    QRegularExpressionMatch match = re.match(output);
    if (match.hasMatch())
    {
        int hours = match.captured(1).toInt();
        int minutes = match.captured(2).toInt();
        int seconds = match.captured(3).toInt();
        double currentTimeS = (hours * 3600) + (minutes * 60) + seconds;
        if (m_sourceDurationS > 0)
        {
            int basePercentage = 0;
            if (m_preset.isTwoPass())
            {
                basePercentage = (m_currentStep == Step::Pass1) ? 0 : 50;
            }
            int percentage = basePercentage + static_cast<int>((currentTimeS / static_cast<double>(m_sourceDurationS)) *
                                                               (m_preset.isTwoPass() ? 50 : 100));
            emit progressUpdated(qMin(100, percentage), "Рендер MP4");
        }
    }
}

ProcessManager* ManualRenderer::getProcessManager() const
{
    return m_processManager;
}

void ManualRenderer::onBitrateCheckFinished(RerenderDecision decision, const RenderPreset& newPreset)
{
    if (decision == RerenderDecision::Rerender)
    {
        emit logMessage("Получено решение о перерендере.", LogCategory::APP);
        m_preset = newPreset;
        m_currentStep = Step::Pass1;
        runPass(m_currentStep);
    }
    else
    {
        emit progressUpdated(100, "Рендер MP4");
        emit finished();
    }
}
