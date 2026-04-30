#include "manualrenderer.h"

#include "appsettings.h"
#include "assprocessor.h"
#include "chapterhelper.h"
#include "processmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

void applyForcedAac256AudioArgs(QStringList& args)
{
    if (args.contains(QLatin1String("-an")))
    {
        return;
    }

    for (int i = 0; i < args.size() - 1; ++i)
    {
        if ((args.at(i) == QLatin1String("-c") || args.at(i) == QLatin1String("-codec")) &&
            args.at(i + 1) == QLatin1String("copy"))
        {
            args[i] = QLatin1String("-c:v");
            break;
        }
    }

    bool codecSet = false;
    bool bitrateSet = false;
    for (int i = 0; i < args.size(); ++i)
    {
        if (args.at(i) != QLatin1String("-c:a") && args.at(i) != QLatin1String("-codec:a"))
        {
            continue;
        }
        if (i + 1 >= args.size())
        {
            return;
        }
        codecSet = true;
        if (AppSettings::instance().hasAacAtCodec())
        {
            args[i + 1] = QLatin1String("aac_at");
        }
        else
        {
            args[i + 1] = QLatin1String("aac");
        }
        const int afterCodec = i + 2;
        if (afterCodec < args.size() &&
            (args.at(afterCodec) == QLatin1String("-b:a") || args.at(afterCodec) == QLatin1String("-audio_bitrate")))
        {
            if (afterCodec + 1 < args.size())
            {
                bitrateSet = true;
                args[afterCodec + 1] = QLatin1String("256k");
            }
            else
            {
                bitrateSet = true;
                args.append(QLatin1String("256k"));
            }
        }
        else
        {
            args.insert(afterCodec, QLatin1String("-b:a"));
            args.insert(afterCodec + 1, QLatin1String("256k"));
            bitrateSet = true;
        }
    }

    const qsizetype outIdx = args.size() - 1;
    if (!codecSet && outIdx >= 0)
    {
        if (AppSettings::instance().hasAacAtCodec())
        {
            args.insert(outIdx, QLatin1String("aac_at"));
        }
        else
        {
            args.insert(outIdx, QLatin1String("aac"));
        }
        args.insert(outIdx, QLatin1String("-c:a"));
    }
    if (!bitrateSet && outIdx >= 0)
    {
        args.insert(outIdx, QLatin1String("256k"));
        args.insert(outIdx, QLatin1String("-b:a"));
    }
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
    cleanupTempFiles();
}

void ManualRenderer::start()
{
    emit logMessage("--- Начало ручного рендера ---", LogCategory::APP);
    emit progressUpdated(-1, "Подготовка");

    m_actualInputMkv = QFileInfo(m_params["inputMkv"].toString()).absoluteFilePath();
    QString presetName = m_params["renderPresetName"].toString();
    m_finalOutputMp4 = QFileInfo(m_params["outputMp4"].toString()).absoluteFilePath();

    if (m_actualInputMkv.isEmpty() || presetName.isEmpty() || m_finalOutputMp4.isEmpty())
    {
        emit logMessage("Ошибка: не указан входной файл, пресет или выходной файл.", LogCategory::APP, LogLevel::Error);
        emit finished();
        return;
    }

    // Инициализируем пути ДО переименования
    QFileInfo outInfo(m_finalOutputMp4);
    QString baseDir = outInfo.absolutePath();
    QString baseName = outInfo.baseName();
    m_tempVideoMp4 = QDir(baseDir).filePath(baseName + "_temp_video.mp4");
    m_tempAudioM4a = QDir(baseDir).filePath(baseName + "_temp_audio.m4a");
    m_tempChaptersTxt = QDir(baseDir).filePath(baseName + "_temp_chapters.txt");

    // Удаляем старые временные файлы напрямую, не вызывая cleanupTempFiles(),
    // чтобы не сбросить переименование m_actualInputMkv
    QFile::remove(m_tempVideoMp4);
    QFile::remove(m_tempAudioM4a);
    QFile::remove(m_tempChaptersTxt);
    QFile::remove(QDir(QFileInfo(m_actualInputMkv).absolutePath()).filePath("manual_chapters_extract.xml"));

    // Переименовываем файл для безопасного хардсаба
    if (m_params.value("useHardsub").toBool() && m_params.value("hardsubMode").toString() == "internal")
    {
        QFileInfo fi(m_actualInputMkv);
        QString tempPath = fi.absolutePath() + QDir::separator() + "temp_name_to_extract_subtitle.mkv";
        if (QFile::rename(m_actualInputMkv, tempPath))
        {
            m_actualInputMkv = tempPath;
            m_processManager->setWorkingDirectory(fi.absolutePath());
            emit logMessage("Входной файл временно переименован для фильтра субтитров.", LogCategory::APP);
        }
    }

    m_preset = AppSettings::instance().findRenderPreset(presetName);
    if (m_preset.name.isEmpty())
    {
        emit logMessage("Критическая ошибка: пресет рендера '" + presetName + "' не найден в настройках.",
                        LogCategory::APP, LogLevel::Error);
        emit finished();
        return;
    }

    QByteArray jsonData;
    QString videoCodecExtension = "h264";
    int detectedVideoBitrateKbps = -1;
    QString detectedVideoFrameRate;
    QString detectedVideoAvgFrameRate;
    bool detectedVideoIsCfr = false;

    if (m_processManager->executeAndWait(AppSettings::instance().mkvmergePath(), {"-J", m_actualInputMkv}, jsonData))
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

    const QString ffprobePath = AppSettings::instance().ffprobePath();
    if (!ffprobePath.isEmpty() && QFileInfo::exists(ffprobePath))
    {
        QByteArray probeData;
        QStringList probeArgs = {"-v", "quiet", "-print_format", "json", "-show_streams", m_actualInputMkv};
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
                        LogCategory::APP, LogLevel::Warning);
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
            const QString tempAssPath = QDir(QFileInfo(m_actualInputMkv).absolutePath())
                                            .filePath(QString("temp_internal_subs_%1.ass").arg(subtitleTrackIndex));
            QFile::remove(tempAssPath);

            QByteArray extractOutput;
            const QString ffmpegPath = AppSettings::instance().ffmpegPath();
            QStringList extractArgs;
            extractArgs << "-y" << "-i" << m_actualInputMkv << "-map" << QString("0:s:%1").arg(subtitleTrackIndex)
                        << "-c:s" << "ass" << tempAssPath;
            if (m_processManager->executeAndWait(ffmpegPath, extractArgs, extractOutput) &&
                QFileInfo::exists(tempAssPath))
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

            const bool reencodeAudioAac = m_params.value(QStringLiteral("reencodeAudioAac256"), true).toBool();
            m_concatRenderer = new ConcatTbRenderer(
                m_actualInputMkv, outputMp4, segment, m_sourceDurationS, videoCodecExtension, hardsubModeForConcat,
                subtitleTrackIndex, externalSubsPath, detectedVideoBitrateKbps, detectedVideoFrameRate,
                detectedVideoAvgFrameRate, detectedVideoIsCfr, reencodeAudioAac, m_processManager, this);
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
                        applyChaptersIfNeeded();
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

    m_currentState = RenderState::VideoPass1;
    runStep();
}

void ManualRenderer::runStep()
{
    QString program = AppSettings::instance().ffmpegPath();
    QStringList args;
    QString stepName;

    switch (m_currentState)
    {
    case RenderState::VideoPass1:
    case RenderState::VideoPass2:
    {
        QString cmdTemplate =
            (m_currentState == RenderState::VideoPass1) ? m_preset.commandPass1 : m_preset.commandPass2;

        if (!parsePreset(cmdTemplate, m_currentVideoArgs, m_currentAudioArgs))
        {
            emit logMessage("Ошибка: не удалось распарсить пресет.", LogCategory::APP, LogLevel::Error);
            emit finished();
            return;
        }

        args = m_currentVideoArgs;
        stepName = (m_currentState == RenderState::VideoPass1) ? "Видео: Проход 1" : "Видео: Проход 2";
        break;
    }
    case RenderState::AudioPass:
    {
        args = m_currentAudioArgs;
        stepName = "Аудио: Извлечение / Кодирование (.m4a)";
        break;
    }
    case RenderState::MuxMP4Box:
    {
        program = AppSettings::instance().mp4boxPath();
        args << "-add" << QString("%1#video").arg(m_tempVideoMp4) << "-add" << QString("%1#audio").arg(m_tempAudioM4a);

        // Обработка глав
        QList<ChapterMarker> markers = prepareChapters();
        if (!markers.isEmpty() && ChapterHelper::writeOgmChapterText(markers, m_tempChaptersTxt))
        {
            args << "-chap" << m_tempChaptersTxt;
            emit logMessage("Главы будут вшиты через MP4Box.", LogCategory::APP);
        }

        args << "-new" << m_finalOutputMp4;
        stepName = "Сборка (Muxing) через MP4Box";
        break;
    }
    default:
        return;
    }

    emit progressUpdated(m_currentState == RenderState::MuxMP4Box ? 95 : -1, stepName);

    m_processManager->startProcess(program, args);
}

bool ManualRenderer::parsePreset(const QString& commandTemplate, QStringList& outVideoArgs, QStringList& outAudioArgs)
{
    QString processedTemplate = commandTemplate;

    bool useHardsub = m_params.value("useHardsub").toBool();
    if (useHardsub)
    {
        QString filterValue;

        if (m_params.value("hardsubMode").toString() == "internal")
        {
            QFileInfo fileInfo(m_actualInputMkv);
            m_processManager->setWorkingDirectory(fileInfo.absolutePath());

            QString escapedName = fileInfo.fileName().replace("'", "\\'");
            int trackIndex = m_params.value("subtitleTrackIndex").toInt();
            filterValue = QString("filename='%1':si=%2").arg(escapedName).arg(trackIndex);

            emit logMessage(QString("Hardsub: внутренняя дорожка #%1 (относительный путь).").arg(trackIndex),
                            LogCategory::APP);
        }
        else
        {
            QFileInfo fileInfo(m_params.value("externalSubsPath").toString());
            // ВОЗВРАЩАЕМ: Устанавливаем рабочую папку для внешних сабов
            m_processManager->setWorkingDirectory(fileInfo.absolutePath());

            QString extEscaped = fileInfo.fileName().replace("'", "\\'");
            filterValue = QString("filename='%1'").arg(extEscaped);

            emit logMessage(QString("Hardsub: внешний файл %1 (относительный путь).").arg(fileInfo.fileName()),
                            LogCategory::APP);
        }
        processedTemplate.replace("%SIGNS%", filterValue);
    }
    else
    {
        processedTemplate.replace("subtitles=%SIGNS%", "");
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*,\s*\")"));
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*\")"));
        emit logMessage("Hardsub отключен. Фильтр субтитров удален из команды.", LogCategory::APP);
    }

    // Подставляем абсолютный путь для входа (-i), он двоеточий не боится
    processedTemplate.replace("%INPUT%", m_actualInputMkv);

    QStringList rawArgs = QProcess::splitCommand(processedTemplate);
    if (!rawArgs.isEmpty() && rawArgs.first().contains("ffmpeg"))
    {
        rawArgs.removeFirst();
    }

    outVideoArgs.clear();
    outAudioArgs.clear();

    outAudioArgs << "-y" << "-hide_banner" << "-i" << m_actualInputMkv << "-vn";

    bool isAudioMapFound = false;

    for (int i = 0; i < rawArgs.size(); ++i)
    {
        QString arg = rawArgs.at(i);

        if (arg == "-c:a" || arg == "-codec:a" || arg == "-b:a" || arg == "-audio_bitrate")
        {
            outAudioArgs << arg;
            if (i + 1 < rawArgs.size())
            {
                outAudioArgs << rawArgs.at(i + 1);
                i++;
            }
        }
        else if (arg == "-map" && i + 1 < rawArgs.size() && rawArgs.at(i + 1).contains(":a"))
        {
            outAudioArgs << arg << rawArgs.at(i + 1);
            isAudioMapFound = true;
            i++;
        }
        else if (arg == "-an")
        {
            // Пропускаем -an из пресета
        }
        else if (arg == "-movflags" || arg == "-map_metadata" || arg == "-map_chapters")
        {
            if (i + 1 < rawArgs.size())
            {
                i++;
            }
        }
        else if (arg == "%OUTPUT%")
        {
            outVideoArgs << m_tempVideoMp4;
        }
        else
        {
            outVideoArgs << arg;
        }
    }

    outVideoArgs.insert(outVideoArgs.size() - 1, "-an"); // Отключаем звук в видео

    if (!isAudioMapFound)
    {
        outAudioArgs << "-map" << "0:a:0";
    }

    if (m_params.value("reencodeAudioAac256", true).toBool())
    {
        applyForcedAac256AudioArgs(outAudioArgs);
    }

    outAudioArgs << m_tempAudioM4a;

    return true;
}

QList<ChapterMarker> ManualRenderer::prepareChapters()
{
    const QString kExt = m_params.value(QStringLiteral("chaptersExternalPath")).toString().trimmed();
    const bool kTransferEmbedded = m_params.value(QStringLiteral("transferEmbeddedChapters"), true).toBool();
    QList<ChapterMarker> markers;

    if (!kExt.isEmpty() && QFileInfo::exists(kExt))
    {
        markers = ChapterHelper::loadChaptersFromFile(kExt);
    }
    else if (kTransferEmbedded && !m_actualInputMkv.isEmpty())
    {
        const QString kEmbPath =
            QDir(QFileInfo(m_actualInputMkv).absolutePath()).filePath("manual_chapters_extract.xml");
        if (ChapterHelper::extractEmbeddedChaptersToFile(AppSettings::instance().mkvextractPath(), m_actualInputMkv,
                                                         kEmbPath, m_processManager))
        {
            markers = ChapterHelper::loadChaptersFromFile(kEmbPath);
        }
    }
    return markers;
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
        return;

    if (m_processManager != nullptr && m_processManager->wasKilled())
    {
        emit logMessage("Ручной рендер отменен пользователем.", LogCategory::APP);
        emit finished();
        return;
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        emit logMessage(QString("Ошибка процесса на этапе. Код: %1").arg(exitCode), LogCategory::APP, LogLevel::Error);
        emit finished();
        return;
    }

    switch (m_currentState)
    {
    case RenderState::VideoPass1:
        if (m_preset.isTwoPass())
        {
            m_currentState = RenderState::VideoPass2;
            runStep();
        }
        else
        {
            auto* helper = new RenderHelper(m_preset, m_tempVideoMp4, m_processManager, this);
            connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
            helper->startCheck();
        }
        break;

    case RenderState::VideoPass2:
    {
        auto* helper = new RenderHelper(m_preset, m_tempVideoMp4, m_processManager, this);
        connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
        helper->startCheck();
        break;
    }

    case RenderState::AudioPass:
        m_currentState = RenderState::MuxMP4Box;
        runStep();
        break;

    case RenderState::MuxMP4Box:
        cleanupTempFiles();
        emit progressUpdated(100, "Готово");
        emit logMessage("Рендер успешно завершен.", LogCategory::APP);
        emit finished();
        break;

    default:
        break;
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
                basePercentage = (m_currentState == RenderState::VideoPass1) ? 0 : 50;
            }
            int percentage = basePercentage + static_cast<int>((currentTimeS / static_cast<double>(m_sourceDurationS)) *
                                                               (m_preset.isTwoPass() ? 50 : 100));
            emit progressUpdated(qMin(100, percentage), "Рендер MP4");
        }
    }
}

void ManualRenderer::cleanupTempFiles()
{
    QString originalMkv = QFileInfo(m_params["inputMkv"].toString()).absoluteFilePath();
    if (!originalMkv.isEmpty() && m_actualInputMkv != originalMkv && QFile::exists(m_actualInputMkv))
    {
        QFile::rename(m_actualInputMkv, originalMkv);
        m_actualInputMkv = originalMkv;
    }

    QFile::remove(m_tempVideoMp4);
    QFile::remove(m_tempAudioM4a);
    QFile::remove(m_tempChaptersTxt);
    QFile::remove(QDir(QFileInfo(originalMkv).absolutePath()).filePath("manual_chapters_extract.xml"));
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
        m_currentState = RenderState::VideoPass1;
        runStep();
    }
    else
    {
        m_currentState = RenderState::AudioPass;
        runStep();
    }
}

void ManualRenderer::applyChaptersIfNeeded()
{
    // Метод больше не вызывается из основного пайплайна (MP4Box вшивает главы на лету),
    // но оставлен для ConcatTbRenderer, если вы используете его.
    const QString ext = m_params.value(QStringLiteral("chaptersExternalPath")).toString().trimmed();
    const QString inputMkv = m_params.value(QStringLiteral("inputMkv")).toString();
    const bool transferEmbedded = m_params.value(QStringLiteral("transferEmbeddedChapters"), true).toBool();
    const QString outMp4 = m_params.value(QStringLiteral("outputMp4")).toString();
    if (outMp4.isEmpty())
    {
        return;
    }

    QList<ChapterMarker> markers;
    if (!ext.isEmpty() && QFileInfo::exists(ext))
    {
        markers = ChapterHelper::loadChaptersFromFile(ext);
    }
    else if (transferEmbedded && !inputMkv.isEmpty())
    {
        const QString embPath =
            QDir(QFileInfo(inputMkv).absolutePath()).filePath(QStringLiteral("manual_chapters_extract.xml"));
        if (ChapterHelper::extractEmbeddedChaptersToFile(AppSettings::instance().mkvextractPath(), inputMkv, embPath,
                                                         m_processManager))
        {
            markers = ChapterHelper::loadChaptersFromFile(embPath);
        }
    }

    if (markers.isEmpty() || !QFileInfo::exists(outMp4))
    {
        return;
    }

    const qint64 durNs = m_sourceDurationS > 0 ? static_cast<qint64>(static_cast<double>(m_sourceDurationS) * 1e9) : 0;
    QString err;
    emit logMessage(QStringLiteral("Запись глав в MP4..."), LogCategory::APP);
    if (ChapterHelper::applyChaptersToMp4(outMp4, markers, durNs, AppSettings::instance().ffmpegPath(),
                                          m_processManager, &err))
    {
        emit logMessage(QStringLiteral("Главы записаны в MP4."), LogCategory::APP);
    }
    else
    {
        emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: не удалось записать главы в MP4: %1").arg(err),
                        LogCategory::APP, LogLevel::Warning);
    }
}