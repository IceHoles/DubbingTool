#include "concattbrenderer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace
{
void writeAgentDebugLog(const QString& hypothesisId, const QString& location, const QString& message,
                        const QJsonObject& data = QJsonObject())
{
    // #region agent log
    QFile f("c:/Users/icehole/git/DubbingTool/debug-dd39f3.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        QJsonObject payload;
        payload["sessionId"] = "dd39f3";
        payload["runId"] = "run-manual-vs-workflow-1";
        payload["hypothesisId"] = hypothesisId;
        payload["location"] = location;
        payload["message"] = message;
        payload["data"] = data;
        payload["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        f.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        f.write("\n");
        f.close();
    }
    // #endregion
}
} // namespace

ConcatTbRenderer::ConcatTbRenderer(const QString& inputMkvPath, const QString& outputMp4Path, const TbSegment& segment,
                                   qint64 sourceDurationS, const QString& videoCodecExtension, const QString& hardsubMode,
                                   int subtitleTrackIndex, const QString& externalSubsPath, int videoBitrateKbps,
                                   ProcessManager* processManager,
                                   QObject* parent)
    : QObject(parent), m_inputMkvPath(inputMkvPath), m_outputMp4Path(outputMp4Path), m_segment(segment),
      m_sourceDurationS(sourceDurationS), m_processManager(processManager), m_videoCodecExtension(videoCodecExtension),
      m_hardsubMode(hardsubMode), m_subtitleTrackIndex(subtitleTrackIndex), m_externalSubsPath(externalSubsPath),
      m_videoBitrateKbps(videoBitrateKbps)
{
    m_ffmpegPath = AppSettings::instance().ffmpegPath();
    m_ffprobePath = AppSettings::instance().ffprobePath();
    m_resultPath = QFileInfo(outputMp4Path).absolutePath();
}

void ConcatTbRenderer::start()
{
    if (!m_connectionsInitialized)
    {
        connect(m_processManager, &ProcessManager::processFinished, this, &ConcatTbRenderer::onProcessFinished,
                Qt::UniqueConnection);
        connect(
            m_processManager, &ProcessManager::processError, this,
            [this](const QString& errorText)
            {
                if (!m_isRunningAsyncStep)
                {
                    return;
                }
                failAndFinish("Concat рендер: ошибка запуска/выполнения процесса. " + errorText);
            },
            Qt::UniqueConnection);
        m_connectionsInitialized = true;
    }

    if (!m_segment.isValid())
    {
        emit logMessage("Concat рендер: некорректный сегмент ТБ, отмена concat.", LogCategory::APP);
        emit finished();
        return;
    }
    renderMp4Concat();
}

QString ConcatTbRenderer::concatEncoderForCodec(const QString& extension) const
{
    if (extension == "h264")
    {
        return "libx264";
    }
    if (extension == "h265")
    {
        return "libx265";
    }
    return "";
}

void ConcatTbRenderer::renderMp4Concat()
{
    emit logMessage("Concat рендер: перекодирование только ТБ (ручной режим)...", LogCategory::APP);
    emit progressUpdated(-1, "Concat рендер");

    if (m_outputMp4Path.isEmpty())
    {
        m_outputMp4Path = m_inputMkvPath;
        m_outputMp4Path.replace(".mkv", ".mp4");
    }

    if (m_videoCodecExtension.isEmpty())
    {
        m_videoCodecExtension = "h264";
    }

    QString encoder = concatEncoderForCodec(m_videoCodecExtension);
    if (encoder.isEmpty())
    {
        emit logMessage("Concat рендер: неподдерживаемый кодек исходного видео. Отмена concat.", LogCategory::APP);
        emit finished();
        return;
    }

    m_concatTbStartSeconds = m_segment.startSeconds;
    m_concatTbEndSeconds = m_segment.endSeconds;

    if (m_concatTbEndSeconds >= static_cast<double>(m_sourceDurationS))
    {
        m_concatSegmentCount = 2;
        emit logMessage("Concat рендер: ТБ в конце видео, используем 2 сегмента.", LogCategory::APP);
    }
    else
    {
        m_concatSegmentCount = 3;
        emit logMessage("Concat рендер: после ТБ есть контент, используем 3 сегмента.", LogCategory::APP);
    }

    if (m_concatSegmentCount == 2)
    {
        m_concatKeyframeTime = static_cast<double>(m_sourceDurationS);
    }

    concatFindKeyframe();
}

void ConcatTbRenderer::concatFindKeyframe()
{
    emit logMessage("Concat рендер: поиск keyframe-ов для границ сегментов...", LogCategory::APP);

    if (m_ffprobePath.isEmpty() || !QFileInfo::exists(m_ffprobePath))
    {
        emit logMessage("Concat рендер: ffprobe не найден. Отмена concat.", LogCategory::APP);
        emit finished();
        return;
    }

    {
        double searchStart = m_concatTbStartSeconds > 15.0 ? m_concatTbStartSeconds - 15.0 : 0.0;
        QString readInterval =
            QString("%1%%2").arg(searchStart, 0, 'f', 3).arg(m_concatTbStartSeconds + 0.001, 0, 'f', 3);

        QStringList args;
        args << "-v"
             << "quiet"
             << "-select_streams"
             << "v:0"
             << "-show_entries"
             << "frame=pts_time,key_frame"
             << "-read_intervals" << readInterval << "-of"
             << "json" << m_inputMkvPath;

        QByteArray output;
        bool success = m_processManager->executeAndWait(m_ffprobePath, args, output);

        if (!success || output.isEmpty())
        {
            emit logMessage("Concat рендер: не удалось найти keyframe перед ТБ. Отмена concat.", LogCategory::APP);
            emit finished();
            return;
        }

        m_concatKfBeforeTbStart = 0.0;
        bool foundBefore = false;
        QJsonDocument doc = QJsonDocument::fromJson(output);
        QJsonArray frames = doc.object()["frames"].toArray();
        for (const auto& frameVal : frames)
        {
            QJsonObject frame = frameVal.toObject();
            if (frame["key_frame"].toInt() == 1)
            {
                bool ok = false;
                double kfTime = frame["pts_time"].toString().toDouble(&ok);
                if (ok && kfTime <= m_concatTbStartSeconds)
                {
                    m_concatKfBeforeTbStart = kfTime;
                    foundBefore = true;
                }
            }
        }

        if (!foundBefore)
        {
            m_concatKfBeforeTbStart = 0.0;
            emit logMessage("Concat рендер: keyframe перед ТБ не найден, используем начало видео.", LogCategory::APP);
        }
        else
        {
            emit logMessage(QString("Concat рендер: keyframe перед ТБ на %1с (начало ТБ: %2с, разница: %3с)")
                                .arg(m_concatKfBeforeTbStart, 0, 'f', 3)
                                .arg(m_concatTbStartSeconds, 0, 'f', 3)
                                .arg(m_concatTbStartSeconds - m_concatKfBeforeTbStart, 0, 'f', 3),
                            LogCategory::APP);
        }
    }

    if (m_concatSegmentCount == 3)
    {
        QString readInterval =
            QString("%1%%2").arg(m_concatTbEndSeconds, 0, 'f', 3).arg(m_concatTbEndSeconds + 10.0, 0, 'f', 3);

        QStringList args;
        args << "-v"
             << "quiet"
             << "-select_streams"
             << "v:0"
             << "-show_entries"
             << "frame=pts_time,key_frame"
             << "-read_intervals" << readInterval << "-of"
             << "json" << m_inputMkvPath;

        QByteArray output;
        bool success = m_processManager->executeAndWait(m_ffprobePath, args, output);

        if (!success || output.isEmpty())
        {
            emit logMessage(
                "Concat рендер: не удалось получить данные о keyframe после ТБ. Переходим в 2-сегментный режим.",
                LogCategory::APP);
            m_concatSegmentCount = 2;
            m_concatKeyframeTime = static_cast<double>(m_sourceDurationS);
        }
        else
        {
            bool foundAfter = false;
            QJsonDocument doc = QJsonDocument::fromJson(output);
            QJsonArray frames = doc.object()["frames"].toArray();
            for (const auto& frameVal : frames)
            {
                QJsonObject frame = frameVal.toObject();
                if (frame["key_frame"].toInt() == 1)
                {
                    bool ok = false;
                    double kfTime = frame["pts_time"].toString().toDouble(&ok);
                    if (ok && kfTime >= m_concatTbEndSeconds)
                    {
                        m_concatKeyframeTime = kfTime;
                        foundAfter = true;
                        break;
                    }
                }
            }

            if (!foundAfter)
            {
                emit logMessage("Concat рендер: keyframe после ТБ не найден. Переходим в 2-сегментный режим.",
                                LogCategory::APP);
                m_concatSegmentCount = 2;
                m_concatKeyframeTime = static_cast<double>(m_sourceDurationS);
            }
            else
            {
                emit logMessage(QString("Concat рендер: keyframe после ТБ на %1с (конец ТБ: %2с, разница: %3с)")
                                    .arg(m_concatKeyframeTime, 0, 'f', 3)
                                    .arg(m_concatTbEndSeconds, 0, 'f', 3)
                                    .arg(m_concatKeyframeTime - m_concatTbEndSeconds, 0, 'f', 3),
                                LogCategory::APP);
            }
        }
    }

    concatCutSegment1();
}

void ConcatTbRenderer::concatCutSegment1()
{
    emit logMessage("Concat рендер: вырезка сегмента 1 (до ТБ, копирование)...", LogCategory::APP);
    emit progressUpdated(-1, "Concat: сегмент 1/3");

    QString seg1Path = QDir(m_resultPath).filePath("concat_seg1.ts");
    QString seg1EndStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);
    const QString seg1InputPath = m_inputMkvPath;

    QStringList args;
    args << "-y"
         << "-i" << m_inputMkvPath << "-to" << seg1EndStr << "-map"
         << "0:v:0"
         << "-c:v"
         << "copy"
         << "-an"
         << "-avoid_negative_ts"
         << "make_non_negative"
         << "-muxdelay"
         << "0"
         << "-muxpreload"
         << "0" << seg1Path;
    writeAgentDebugLog("H11", "concattbrenderer.cpp:concatCutSegment1", "manual_seg1_settings",
                       QJsonObject{
                           {"inputPath", seg1InputPath},
                           {"seg1End", seg1EndStr},
                       });

    m_currentStep = Step::CutSegment1;
    runFfmpegAsync(args, "Concat рендер: не удалось вырезать сегмент 1.");
}

void ConcatTbRenderer::concatRenderSegment2()
{
    emit logMessage("Concat рендер: перекодирование сегмента 2 (ТБ с хардсабом)...", LogCategory::APP);
    emit progressUpdated(-1, "Concat: рендер ТБ");
    QString seg2Path = QDir(m_resultPath).filePath("concat_seg2.ts");
    QString encoder = concatEncoderForCodec(m_videoCodecExtension);
    if (encoder.isEmpty())
    {
        emit logMessage("Concat рендер: не удалось определить кодек для сегмента 2.", LogCategory::APP);
        emit finished();
        return;
    }

    // Match WorkflowManager behavior: detect seg1 real duration and trim overlap tail.
    double bframeOverlap = 0.0;
    {
        const QString seg1Path = QDir(m_resultPath).filePath("concat_seg1.ts");
        QStringList probeArgs;
        probeArgs << "-v" << "quiet" << "-show_entries" << "format=duration" << "-of" << "csv=p=0" << seg1Path;

        QByteArray probeOutput;
        if (m_processManager->executeAndWait(m_ffprobePath, probeArgs, probeOutput))
        {
            bool ok = false;
            double seg1Duration = QString::fromUtf8(probeOutput).trimmed().toDouble(&ok);
            if (ok && seg1Duration > m_concatKfBeforeTbStart)
            {
                bframeOverlap = seg1Duration - m_concatKfBeforeTbStart;
            }
        }
    }

    QString seg2StartStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);
    QStringList args;
    bool usesExplicitInputDuration = false;
    args << "-y" << "-ss" << seg2StartStr;

    if (m_concatSegmentCount == 3)
    {
        double segInputDuration = m_concatKeyframeTime - m_concatKfBeforeTbStart;
        args << "-t" << QString::number(segInputDuration, 'f', 3);
        usesExplicitInputDuration = true;
    }
    else
    {
        double segInputDuration = static_cast<double>(m_sourceDurationS) - m_concatKfBeforeTbStart;
        args << "-t" << QString::number(segInputDuration, 'f', 3);
        usesExplicitInputDuration = true;
    }

    args << "-i" << m_inputMkvPath;

    // Prepare subtitle file with a safe name in output directory and use relative path.
    QString subtitleFilter;
    if (m_hardsubMode == "external" && QFileInfo::exists(m_externalSubsPath))
    {
        m_tempFilterSubsPath = QDir(m_resultPath).filePath("concat_filter_subs.ass");
        QFile::remove(m_tempFilterSubsPath);
        if (!QFile::copy(m_externalSubsPath, m_tempFilterSubsPath))
        {
            emit logMessage("Concat рендер: не удалось подготовить временный ASS для фильтра.", LogCategory::APP);
            emit finished();
            return;
        }
        m_processManager->setWorkingDirectory(QFileInfo(m_tempFilterSubsPath).absolutePath());
        subtitleFilter = buildSubtitleFilter();
    }

    if (!subtitleFilter.isEmpty())
    {
        // Keep subtitle timing in original timeline, then reset to segment timeline.
        // This mirrors the WorkflowManager concat pipeline.
    }

    QStringList vfParts;
    if (!subtitleFilter.isEmpty())
    {
        vfParts << QString("setpts=PTS+%1/TB").arg(seg2StartStr);
        vfParts << QString("subtitles=%1").arg(subtitleFilter);
        vfParts << "setpts=PTS-STARTPTS";
    }
    if (bframeOverlap > 0.001)
    {
        vfParts << QString("trim=start=%1").arg(bframeOverlap, 0, 'f', 3);
        vfParts << "setpts=PTS-STARTPTS";
    }
    if (!vfParts.isEmpty())
    {
        args << "-vf" << vfParts.join(",");
    }

    args << "-an";
    args << "-c:v" << encoder;

    bool usesMaxrateBufsize = false;
    if (m_videoBitrateKbps > 0)
    {
        const QString maxrateStr = QString::number(m_videoBitrateKbps) + "k";
        const QString bufsizeStr = QString::number(m_videoBitrateKbps * 2) + "k";
        usesMaxrateBufsize = true;
        if (encoder == "libx264")
        {
            args << "-crf" << "18" << "-maxrate" << maxrateStr << "-bufsize" << bufsizeStr << "-preset" << "medium"
                 << "-profile:v" << "high";
        }
        else
        {
            args << "-crf" << "18" << "-maxrate" << maxrateStr << "-bufsize" << bufsizeStr << "-preset" << "medium"
                 << "-tag:v" << "hvc1";
        }
    }
    else
    {
        if (encoder == "libx264")
        {
            args << "-crf" << "18" << "-preset" << "medium" << "-profile:v" << "high";
        }
        else
        {
            args << "-crf" << "18" << "-preset" << "medium" << "-tag:v" << "hvc1";
        }
    }

    args << "-map" << "0:v:0";
    args << "-muxdelay" << "0" << "-muxpreload" << "0";
    args << seg2Path;
    writeAgentDebugLog("H7", "concattbrenderer.cpp:concatRenderSegment2", "segment2_settings",
                       QJsonObject{
                           {"segmentCount", m_concatSegmentCount},
                           {"seg2Start", m_concatKfBeforeTbStart},
                           {"seg2EndKeyframe", m_concatKeyframeTime},
                           {"usesExplicitInputDuration", usesExplicitInputDuration},
                           {"encoder", encoder},
                           {"usesCrfOnly", !usesMaxrateBufsize},
                           {"usesMaxrateBufsize", usesMaxrateBufsize},
                           {"usesBframeTrim", bframeOverlap > 0.001},
                           {"bframeOverlap", bframeOverlap},
                           {"videoBitrateKbps", m_videoBitrateKbps},
                           {"subtitleFilterApplied", !subtitleFilter.isEmpty()},
                       });

    m_currentStep = Step::RenderSegment2;
    runFfmpegAsync(args, "Concat рендер: не удалось перекодировать сегмент 2.");
}

void ConcatTbRenderer::concatCutSegment3()
{
    emit logMessage("Concat рендер: вырезка сегмента 3 (после ТБ, копирование)...", LogCategory::APP);
    emit progressUpdated(-1, "Concat: сегмент 3/3");

    QString seg3Path = QDir(m_resultPath).filePath("concat_seg3.ts");
    QString kfTimeStr = QString::number(m_concatKeyframeTime, 'f', 3);
    const QString seg3InputPath = m_inputMkvPath;

    QStringList args;
    args << "-y" << "-ss" << kfTimeStr << "-i" << m_inputMkvPath << "-map"
         << "0:v:0"
         << "-c:v"
         << "copy"
         << "-an"
         << "-avoid_negative_ts"
         << "make_non_negative"
         << "-muxdelay"
         << "0"
         << "-muxpreload"
         << "0" << seg3Path;
    writeAgentDebugLog("H11", "concattbrenderer.cpp:concatCutSegment3", "manual_seg3_settings",
                       QJsonObject{
                           {"inputPath", seg3InputPath},
                           {"seg3Start", kfTimeStr},
                       });

    m_currentStep = Step::CutSegment3;
    runFfmpegAsync(args, "Concat рендер: не удалось вырезать сегмент 3.");
}

void ConcatTbRenderer::concatJoinSegments()
{
    emit logMessage("Concat рендер: склейка сегментов...", LogCategory::APP);
    emit progressUpdated(-1, "Concat: склейка");

    QString listPath = QDir(m_resultPath).filePath("concat_list.txt");
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit logMessage("Concat рендер: не удалось создать concat_list.txt.", LogCategory::APP);
        if (!m_tempFilterSubsPath.isEmpty())
        {
            QFile::remove(m_tempFilterSubsPath);
            m_tempFilterSubsPath.clear();
        }
        emit finished();
        return;
    }
    QTextStream stream(&listFile);
    stream << "file 'concat_seg1.ts'\n";
    stream << "file 'concat_seg2.ts'\n";
    if (m_concatSegmentCount == 3)
    {
        stream << "file 'concat_seg3.ts'\n";
    }
    listFile.close();

    QStringList args;
    args << "-y"
         << "-f"
         << "concat"
         << "-safe"
         << "0"
         << "-i" << QFileInfo(listPath).absoluteFilePath() << "-i" << m_inputMkvPath << "-map"
         << "0:v:0"
         << "-map"
         << "1:a:0"
         << "-c:v"
         << "copy"
         << "-c:a"
         << "copy"
         << "-movflags"
         << "+faststart"
         << "-shortest" << m_outputMp4Path;
    writeAgentDebugLog("H11", "concattbrenderer.cpp:concatJoinSegments", "manual_join_settings",
                       QJsonObject{
                           {"videoConcatList", QFileInfo(listPath).absoluteFilePath()},
                           {"audioInputPath", m_inputMkvPath},
                           {"outputPath", m_outputMp4Path},
                       });

    m_currentStep = Step::JoinSegments;
    runFfmpegAsync(args, "Concat рендер: не удалось склеить сегменты.");
}

QString ConcatTbRenderer::buildSubtitleFilter() const
{
    // We run ffmpeg in subtitle file directory and pass a safe relative filename.
    // This avoids repeated escaping issues with spaces, brackets and drive letters.
    if (!m_tempFilterSubsPath.isEmpty())
    {
        return QString("filename='%1'").arg(QFileInfo(m_tempFilterSubsPath).fileName());
    }
    return "";
}

void ConcatTbRenderer::runFfmpegAsync(const QStringList& args, const QString& errorMessageForStep)
{
    m_pendingStepErrorMessage = errorMessageForStep;
    m_isRunningAsyncStep = true;
    m_processManager->setWorkingDirectory(m_resultPath);
    writeAgentDebugLog("H9", "concattbrenderer.cpp:runFfmpegAsync", "start_async_ffmpeg_step",
                       QJsonObject{
                           {"step", static_cast<int>(m_currentStep)},
                           {"workingDir", m_resultPath},
                           {"argCount", static_cast<int>(args.size())},
                       });
    m_processManager->startProcess(m_ffmpegPath, args);
}

void ConcatTbRenderer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_isRunningAsyncStep)
    {
        return;
    }

    m_isRunningAsyncStep = false;
    writeAgentDebugLog("H9", "concattbrenderer.cpp:onProcessFinished", "finish_async_ffmpeg_step",
                       QJsonObject{
                           {"step", static_cast<int>(m_currentStep)},
                           {"exitCode", exitCode},
                           {"exitStatus", static_cast<int>(exitStatus)},
                       });
    if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        failAndFinish(m_pendingStepErrorMessage);
        return;
    }

    switch (m_currentStep)
    {
    case Step::CutSegment1:
        emit logMessage("Concat рендер: сегмент 1 готов.", LogCategory::APP);
        concatRenderSegment2();
        return;
    case Step::RenderSegment2:
        emit logMessage("Concat рендер: сегмент 2 (ТБ) готов.", LogCategory::APP);
        if (m_concatSegmentCount == 3)
        {
            concatCutSegment3();
        }
        else
        {
            concatJoinSegments();
        }
        return;
    case Step::CutSegment3:
        emit logMessage("Concat рендер: сегмент 3 готов.", LogCategory::APP);
        concatJoinSegments();
        return;
    case Step::JoinSegments:
        cleanupTempFiles(true);
        m_currentStep = Step::Idle;
        m_pendingStepErrorMessage.clear();
        emit logMessage("Concat рендер MP4 успешно завершен.", LogCategory::APP);
        emit progressUpdated(100, "Concat: готово");
        emit finished();
        return;
    case Step::Idle:
        return;
    }
}

void ConcatTbRenderer::failAndFinish(const QString& message)
{
    m_isRunningAsyncStep = false;
    if (!message.isEmpty())
    {
        emit logMessage(message, LogCategory::APP);
    }
    cleanupTempFiles(true);
    m_currentStep = Step::Idle;
    m_pendingStepErrorMessage.clear();
    emit finished();
}

void ConcatTbRenderer::cleanupTempFiles(bool removeSegments)
{
    if (removeSegments)
    {
        QFile::remove(QDir(m_resultPath).filePath("concat_seg1.ts"));
        QFile::remove(QDir(m_resultPath).filePath("concat_seg2.ts"));
        QFile::remove(QDir(m_resultPath).filePath("concat_seg3.ts"));
        QFile::remove(QDir(m_resultPath).filePath("concat_list.txt"));
    }
    if (!m_tempFilterSubsPath.isEmpty())
    {
        QFile::remove(m_tempFilterSubsPath);
        m_tempFilterSubsPath.clear();
    }
}

