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
void applyForcedAacAudioArgs(QStringList& args, const QString& bitrate)
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
                args[afterCodec + 1] = bitrate;
            }
            else
            {
                bitrateSet = true;
                args.append(bitrate);
            }
        }
        else
        {
            args.insert(afterCodec, QLatin1String("-b:a"));
            args.insert(afterCodec + 1, bitrate);
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
        args.insert(outIdx, bitrate);
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

    if (QFileInfo(m_actualInputMkv) == QFileInfo(m_finalOutputMp4))
    {
        emit logMessage("Ошибка: выходной файл совпадает с входным. Перезапись исходного файла запрещена.",
                        LogCategory::APP, LogLevel::Error);
        emit finished();
        return;
    }

    if (AppSettings::instance().mp4boxPath().isEmpty() || !QFileInfo::exists(AppSettings::instance().mp4boxPath()))
    {
        emit logMessage("Ошибка: mp4box.exe не найден в настройках. Рендер невозможен.", LogCategory::APP,
                        LogLevel::Error);
        emit finished();
        return;
    }

    QFileInfo outInfo(m_finalOutputMp4);
    QString baseDir = outInfo.absolutePath();
    QString baseName = outInfo.baseName();
    m_tempVideoMp4 = QDir(baseDir).filePath(baseName + "_temp_video.mp4");
    m_tempAudioM4a = QDir(baseDir).filePath(baseName + "_temp_audio.m4a");
    m_tempChaptersTxt = QDir(baseDir).filePath(baseName + "_temp_chapters.txt");
    m_tempExtractedSubsPath.clear();

    QFile::remove(m_tempVideoMp4);
    QFile::remove(m_tempAudioM4a);
    QFile::remove(m_tempChaptersTxt);
    QFile::remove(QDir(QFileInfo(m_actualInputMkv).absolutePath()).filePath("manual_chapters_extract.xml"));

    // Если запрошен хардсаб из внутренней дорожки — извлекаем её во временный файл через FFmpeg
    if (m_params.value("useHardsub").toBool() && m_params.value("hardsubMode").toString() == "internal")
    {
        int trackIndex = m_params.value("subtitleTrackIndex").toInt();
        m_tempExtractedSubsPath = QDir(baseDir).filePath("dt_temp_render_subs.ass");
        QFile::remove(m_tempExtractedSubsPath);

        emit logMessage(QString("Извлечение внутренней дорожки субтитров 0:s:%1 во временный файл...").arg(trackIndex),
                        LogCategory::APP);

        QStringList ffmpegArgs = {"-y",   "-i",  m_actualInputMkv,       "-map", QString("0:s:%1").arg(trackIndex),
                                  "-c:s", "ass", m_tempExtractedSubsPath};
        QByteArray dummy;
        if (!m_processManager->executeAndWait(AppSettings::instance().ffmpegPath(), ffmpegArgs, dummy, 120000) ||
            !QFileInfo::exists(m_tempExtractedSubsPath))
        {
            emit logMessage("Ошибка: не удалось извлечь субтитры для хардсаба через ffmpeg.", LogCategory::APP,
                            LogLevel::Error);
            emit finished();
            return;
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
    if (m_processManager->executeAndWait(AppSettings::instance().mkvmergePath(), {"-J", m_actualInputMkv}, jsonData))
    {
        QJsonObject root = QJsonDocument::fromJson(jsonData).object();
        if (root.contains("container"))
        {
            m_sourceDurationS = static_cast<qint64>(
                root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0);
        }
    }

    if (m_sourceDurationS == 0)
    {
        emit logMessage("Предупреждение: не удалось определить длительность файла. Прогресс не будет отображаться.",
                        LogCategory::APP, LogLevel::Warning);
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
        QString subFilePath;
        if (m_params.value("hardsubMode").toString() == "internal")
        {
            subFilePath = m_tempExtractedSubsPath;
        }
        else
        {
            subFilePath = m_params.value("externalSubsPath").toString();
        }

        if (!subFilePath.isEmpty() && QFileInfo::exists(subFilePath))
        {
            QFileInfo fileInfo(subFilePath);
            m_processManager->setWorkingDirectory(fileInfo.absolutePath());

            QString extEscaped = fileInfo.fileName().replace("'", "\\'");
            QString filterValue = QString("filename='%1'").arg(extEscaped);
            processedTemplate.replace("%SIGNS%", filterValue);

            emit logMessage(QString("Hardsub: файл субтитров %1.").arg(fileInfo.fileName()), LogCategory::APP);
        }
        else
        {
            emit logMessage("Ошибка: файл субтитров для хардсаба не найден.", LogCategory::APP, LogLevel::Error);
            return false;
        }
    }
    else
    {
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

        if (arg == "-c:a" || arg == "-codec:a" || arg == "-b:a" || arg == "-audio_bitrate" || arg == "-ac" ||
            arg == "-ar" || arg == "-af" || arg == "-profile:a")
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
        else if (arg == "-vf" || arg == "-filter:v")
        {
            if (i + 1 < rawArgs.size())
            {
                QString vfValue = rawArgs.at(i + 1);
                if (!useHardsub)
                {
                    const QStringList filters = vfValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    QStringList cleanedFilters;
                    for (const QString& f : filters)
                    {
                        const QString trimmed = f.trimmed();
                        if (!trimmed.isEmpty() && trimmed != QLatin1String("subtitles=%SIGNS%") &&
                            !trimmed.startsWith(QLatin1String("subtitles=%SIGNS%")))
                        {
                            cleanedFilters.append(trimmed);
                        }
                    }
                    if (cleanedFilters.isEmpty())
                    {
                        i++; // Пропускаем -vf и его очищенное значение целиком
                        continue;
                    }
                    vfValue = cleanedFilters.join(QLatin1Char(','));
                }
                outVideoArgs << arg << vfValue;
                i++;
            }
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

    if (m_params.value(QStringLiteral("reencodeAudioAac"), true).toBool())
    {
        applyForcedAacAudioArgs(outAudioArgs, AppSettings::defaultAudioBitrateArg());
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
            connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
            connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
            connect(helper, &RenderHelper::finished, this, &ManualRenderer::onBitrateCheckFinished);
            helper->startCheck();
        }
        break;

    case RenderState::VideoPass2:
    {
        auto* helper = new RenderHelper(m_preset, m_tempVideoMp4, m_processManager, this);
        connect(helper, &RenderHelper::logMessage, this, &ManualRenderer::logMessage);
        connect(helper, &RenderHelper::showDialogRequest, this, &ManualRenderer::bitrateCheckRequest);
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
    QFile::remove(m_tempVideoMp4);
    QFile::remove(m_tempAudioM4a);
    QFile::remove(m_tempChaptersTxt);
    if (!m_tempExtractedSubsPath.isEmpty())
    {
        QFile::remove(m_tempExtractedSubsPath);
        m_tempExtractedSubsPath.clear();
    }
    QFile::remove(QDir(QFileInfo(m_actualInputMkv).absolutePath()).filePath("manual_chapters_extract.xml"));
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
