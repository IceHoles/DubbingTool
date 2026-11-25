#include "manualassembler.h"

#include "appsettings.h"
#include "chapterhelper.h"
#include "processmanager.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

ManualAssembler::ManualAssembler(const QVariantMap& params, QObject* parent)
    : QObject(parent), m_params(params), m_processManager(new ProcessManager(this)), m_progressTimer(new QTimer(this))
{
    connect(m_processManager, &ProcessManager::processOutput, this, &ManualAssembler::onProcessText);
    connect(m_processManager, &ProcessManager::processStdErr, this, &ManualAssembler::onProcessText);
    connect(m_processManager, &ProcessManager::processFinished, this, &ManualAssembler::onProcessFinished);
}

ManualAssembler::~ManualAssembler()
{
}

void ManualAssembler::start()
{
    emit logMessage("--- Начало ручной сборки ---", LogCategory::APP);
    m_currentStep = Step::Idle;

    if (m_params["normalizeAudio"].toBool())
    {
        normalizeAudio();
    }
    else if (m_params["convertAudio"].toBool())
    {
        convertAudio();
    }
    else
    {
        processSubtitlesAndAssemble();
    }
}

void ManualAssembler::normalizeAudio()
{
    m_currentStep = Step::NormalizingAudio;
    emit logMessage("Шаг 1: Нормализация аудио...", LogCategory::APP);
    emit progressUpdated(-1, "Нормализация аудио");

    QString nugenPath = AppSettings::instance().nugenAmbPath();
    QString originalAudioPath = m_params["russianAudioPath"].toString();

    if (nugenPath.isEmpty() || !originalAudioPath.endsWith(".wav", Qt::CaseInsensitive))
    {
        emit logMessage("Нормализация пропущена (не указан путь к NUGEN или файл не .wav).", LogCategory::APP);
        if (m_params["convertAudio"].toBool())
        {
            convertAudio();
        }
        else
        {
            processSubtitlesAndAssemble();
        }
        return;
    }

    m_originalAudioPathBeforeNormalization = originalAudioPath;
    QFileInfo originalInfo(m_originalAudioPathBeforeNormalization);
    QString tempInputPath = originalInfo.dir().filePath("temp_audio_for_nugen.wav");

    if (QFile::exists(tempInputPath))
    {
        QFile::remove(tempInputPath);
    }

    if (!QFile::copy(originalAudioPath, tempInputPath))
    {
        emit logMessage("ОШИБКА: Не удалось переименовать аудиофайл. Нормализация отменена.", LogCategory::APP,
                        LogLevel::Error);
        if (m_params["convertAudio"].toBool())
        {
            convertAudio();
        }
        else
        {
            processSubtitlesAndAssemble();
        }
        return;
    }

    emit logMessage("Аудиофайл переименован в temp_audio_for_nugen.wav  для безопасной обработки.", LogCategory::APP);

    QFileInfo nugenInfo(nugenPath);
    QString ambCmdPath = nugenInfo.dir().filePath("AMBCmd.exe");
    emit progressUpdated(-1, "Запуск NUGEN Audio AMB");
    m_didLaunchNugen = true;

    if (!QFileInfo::exists(ambCmdPath))
    {
        emit logMessage("Ошибка: AMBCmd.exe не найден. Нормализация пропущена.", LogCategory::APP, LogLevel::Error);
        if (m_params["convertAudio"].toBool())
        {
            convertAudio();
        }
        else
        {
            processSubtitlesAndAssemble();
        }
        return;
    }

    QProcess::startDetached(nugenPath);
    QTimer::singleShot(3000, this,
                       [this, ambCmdPath, tempInputPath]()
                       {
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
    const bool isAac = (targetFormat == "aac");

    auto alreadyInTargetFormat = [&](const QString& path)
    {
        if (isAac)
            return path.endsWith(".mka", Qt::CaseInsensitive);
        return path.endsWith("." + targetFormat, Qt::CaseInsensitive);
    };

    if (alreadyInTargetFormat(audioPath))
    {
        emit logMessage("Аудио уже в нужном формате, конвертация пропущена.", LogCategory::APP);
        processSubtitlesAndAssemble();
        return;
    }

    emit logMessage("Определение длительности аудиофайла...", LogCategory::APP);
    QByteArray jsonData;
    QString ffprobePath = AppSettings::instance().ffprobePath();
    QStringList ffprobeArgs = {"-v", "error", "-show_format", "-print_format", "json", audioPath};

    m_sourceAudioDurationS = 0.0;
    if (m_processManager->executeAndWait(ffprobePath, ffprobeArgs, jsonData) && !jsonData.isEmpty())
    {
        QJsonObject format = QJsonDocument::fromJson(jsonData).object()["format"].toObject();
        m_sourceAudioDurationS = format["duration"].toString().toDouble();
        emit logMessage(QString("Длительность: %1 секунд.").arg(m_sourceAudioDurationS), LogCategory::APP);
    }
    else
    {
        emit logMessage("Предупреждение: не удалось определить длительность аудио. Прогресс не будет отображаться.",
                        LogCategory::APP, LogLevel::Warning);
    }

    emit progressUpdated(0, "Конвертация аудио");
    // Для ручной сборки MKV при AAC кодируем в Matroska-аудио контейнер (mka).
    const QString outputExtension = isAac ? "mka" : targetFormat;
    QString newAudioPath =
        QFileInfo(audioPath).dir().filePath(QFileInfo(audioPath).baseName() + "_converted." + outputExtension);
    m_params["russianAudioPath"] = newAudioPath;

    m_progressLogPath = QDir::temp().filePath("dt_manual_audio_progress.log");
    QFile::remove(m_progressLogPath);

    QStringList args;
    args << "-y" << "-i" << audioPath;
    if (isAac)
    {
        if (AppSettings::instance().hasAacAtCodec())
        {
            args << "-c:a" << "aac_at" << "-b:a" << "256k";
        }
        else
        {
            args << "-c:a" << "aac" << "-b:a" << "256k";
        }
    }
    else if (targetFormat == "flac")
    {
        args << "-c:a" << "flac";
    }
    args << "-progress" << QDir::toNativeSeparators(m_progressLogPath) << newAudioPath;

    if (m_sourceAudioDurationS > 0)
    {
        connect(m_progressTimer, &QTimer::timeout, this, &ManualAssembler::onConversionProgress);
        m_progressTimer->start(500);
    }

    m_processManager->startProcess(AppSettings::instance().ffmpegPath(), args);
}

void ManualAssembler::processSubtitlesAndAssemble()
{
    assemble();
}

QString ManualAssembler::resolveChaptersPathForMkvMerge()
{
    const bool useCustomChapters = m_params.value(QStringLiteral("useCustomChaptersXml"), false).toBool();
    if (useCustomChapters)
    {
        const QString customPath = m_params.value(QStringLiteral("chaptersXmlPath")).toString().trimmed();
        if (!customPath.isEmpty())
        {
            if (QFileInfo::exists(customPath))
            {
                const QString abs = QFileInfo(customPath).absoluteFilePath();
                emit logMessage(QStringLiteral("Главы: используется указанный XML: %1").arg(abs), LogCategory::APP);
                return abs;
            }
            emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: файл глав не найден: %1").arg(customPath), LogCategory::APP,
                            LogLevel::Warning);
        }
    }

    const QString videoPath = m_params.value(QStringLiteral("videoPath")).toString();
    if (videoPath.isEmpty())
    {
        return {};
    }

    QString baseDir = m_params.value(QStringLiteral("workDir")).toString().trimmed();
    if (baseDir.isEmpty())
    {
        baseDir = QFileInfo(videoPath).absolutePath();
    }

    const QString embPath = QDir(baseDir).filePath(QStringLiteral("chapters_embedded_ma.xml"));
    const QString muxPath = QDir(baseDir).filePath(QStringLiteral("chapters_mux_ma.xml"));

    if (videoPath.endsWith(QStringLiteral(".mkv"), Qt::CaseInsensitive))
    {
        const QString mkvextract = AppSettings::instance().mkvextractPath();
        if (ChapterHelper::extractEmbeddedChaptersToFile(mkvextract, videoPath, embPath, m_processManager))
        {
            const QList<ChapterMarker> markers = ChapterHelper::loadChaptersFromFile(embPath);
            if (!markers.isEmpty())
            {
                if (ChapterHelper::writeMatroskaChapterXml(markers, muxPath))
                {
                    emit logMessage(QStringLiteral("Главы: взяты из встроенных глав MKV."), LogCategory::APP);
                    return muxPath;
                }
                emit logMessage(QStringLiteral("Главы: взяты из встроенных глав MKV."), LogCategory::APP);
                return QFileInfo(embPath).absoluteFilePath();
            }
        }
    }
    else if (videoPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
    {
        const QString ffprobePath = AppSettings::instance().ffprobePath();
        if (!ffprobePath.isEmpty() && QFileInfo::exists(ffprobePath))
        {
            QByteArray json;
            const QStringList args = {QStringLiteral("-v"),
                                      QStringLiteral("quiet"),
                                      QStringLiteral("-show_chapters"),
                                      QStringLiteral("-print_format"),
                                      QStringLiteral("json"),
                                      QStringLiteral("-i"),
                                      videoPath};
            if (m_processManager->executeAndWait(ffprobePath, args, json))
            {
                const QList<ChapterMarker> markers = ChapterHelper::parseFfprobeChaptersJson(json);
                if (!markers.isEmpty() && ChapterHelper::writeMatroskaChapterXml(markers, muxPath))
                {
                    emit logMessage(QStringLiteral("Главы: взяты из встроенных метаданных MP4."), LogCategory::APP);
                    return muxPath;
                }
            }
        }
    }

    return {};
}

void ManualAssembler::assemble()
{
    m_currentStep = Step::AssemblingMkv;
    emit logMessage("Сборка MKV файла...", LogCategory::APP);
    emit progressUpdated(-1, "Сборка MKV");

    QString mkvmergePath = AppSettings::instance().mkvmergePath();
    QString workDir = m_params["workDir"].toString();
    QString outputName = m_params["outputName"].toString();

    if (outputName.isEmpty())
    {
        emit logMessage("Критическая ошибка: не указан выходной файл.", LogCategory::APP, LogLevel::Error);
        emit finished(false);
        return;
    }

    QString fullOutputPath;
    if (!workDir.isEmpty())
    {
        fullOutputPath = QDir(workDir).filePath(outputName);
    }
    else
    {
        QString videoPath = m_params["videoPath"].toString();
        if (!videoPath.isEmpty())
        {
            fullOutputPath = QDir(QFileInfo(videoPath).path()).filePath(outputName);
        }
        else
        {
            fullOutputPath = outputName;
        }
    }

    if (fullOutputPath.isEmpty())
    {
        emit logMessage("Критическая ошибка: не удалось определить путь для сохранения файла.", LogCategory::APP,
                        LogLevel::Error);
        emit finished(false);
        return;
    }

    m_finalMkvPath = fullOutputPath;
    QStringList args;
    args << "-o" << fullOutputPath;
    const QString chaptersMux = resolveChaptersPathForMkvMerge();
    const bool explicitChaptersProvided = (!chaptersMux.isEmpty() && QFileInfo::exists(chaptersMux));

    auto addNoChaptersForContainer = [&](const QString& path)
    {
        if (!explicitChaptersProvided)
        {
            return;
        }
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QStringLiteral("mkv") || ext == QStringLiteral("mka") || ext == QStringLiteral("mp4") ||
            ext == QStringLiteral("m4a"))
        {
            args << "--no-chapters";
        }
    };

    // Шрифты
    QStringList fontPaths = m_params["fontPaths"].toStringList();
    for (const QString& path : fontPaths)
    {
        args << "--attachment-name" << QFileInfo(path).fileName();
        QString mimeType = "application/octet-stream";
        if (path.endsWith(".ttf", Qt::CaseInsensitive))
        {
            mimeType = "application/x-font-ttf";
        }
        else if (path.endsWith(".otf", Qt::CaseInsensitive))
        {
            mimeType = "application/vnd.ms-opentype";
        }
        else if (path.endsWith(".ttc", Qt::CaseInsensitive))
        {
            mimeType = "application/font-collection";
        }
        args << "--attachment-mime-type" << mimeType;
        args << "--attach-file" << path;
    }

    if (!chaptersMux.isEmpty() && QFileInfo::exists(chaptersMux))
    {
        args << "--chapters" << chaptersMux;
        emit logMessage(QStringLiteral("mkvmerge: добавлены главы (%1).").arg(chaptersMux), LogCategory::APP);
    }

    // Дорожки
    QString studio = m_params["studio"].toString();
    QString lang = m_params["language"].toString();
    QString subAuthor = m_params["subAuthor"].toString();
    bool useAudioFromVideo = m_params.value("useAudioFromVideo", false).toBool();
    int audioTrackFromVideoId = m_params.value("audioTrackFromVideoId", -1).toInt();
    int videoTrackId = m_params.value("videoTrackId", 0).toInt();

    QStringList trackOrder;
    int nextFileIndex = 0;

    int videoFileIndex = -1;
    if (m_params.contains("videoPath"))
    {
        const QString videoPath = m_params["videoPath"].toString();
        const QString ext = QFileInfo(videoPath).suffix().toLower();
        const bool isContainer = (ext == "mp4" || ext == "mkv" || ext == "m4v" || ext == "mov" || ext == "ts");

        videoFileIndex = nextFileIndex++;
        trackOrder << QString("%1:%2").arg(videoFileIndex).arg(videoTrackId);

        if (isContainer)
        {
            args << "--video-tracks" << QString::number(videoTrackId);
            if (!lang.isEmpty())
            {
                args << "--language" << QString("%1:%2").arg(videoTrackId).arg(lang);
            }
            if (!studio.isEmpty())
            {
                args << "--track-name" << QString("%1:Видеоряд [%2]").arg(videoTrackId).arg(studio);
            }

            if (useAudioFromVideo && audioTrackFromVideoId >= 0)
            {
                args << "--audio-tracks" << QString::number(audioTrackFromVideoId);
                args << "--default-track-flag" << QString("%1:no").arg(audioTrackFromVideoId);
                if (!lang.isEmpty())
                {
                    args << "--language" << QString("%1:%2").arg(audioTrackFromVideoId).arg(lang);
                }
                if (!studio.isEmpty())
                {
                    args << "--track-name" << QString("%1:Оригинал [%2]").arg(audioTrackFromVideoId).arg(studio);
                }
            }
            else
            {
                args << "--no-audio";
            }

            args << "--no-subtitles" << "--no-attachments";
        }
        else
        {
            if (!lang.isEmpty())
            {
                args << "--language" << "0:" + lang;
            }
            if (!studio.isEmpty())
            {
                args << "--track-name" << QString("0:Видеоряд [%1]").arg(studio);
            }
        }

        addNoChaptersForContainer(videoPath);
        args << videoPath;
    }

    int rusAudioFileIndex = -1;
    if (m_params.contains("russianAudioPath"))
    {
        const QString russianAudioPath = m_params["russianAudioPath"].toString();
        if (!russianAudioPath.isEmpty())
        {
            rusAudioFileIndex = nextFileIndex++;
            args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name"
                 << "0:Русский [Дубляжная]";
            addNoChaptersForContainer(russianAudioPath);
            args << russianAudioPath;
        }
    }

    int origAudioFileIndex = -1;
    if (!useAudioFromVideo && m_params.contains("originalAudioPath"))
    {
        const QString originalAudioPath = m_params["originalAudioPath"].toString();
        if (!originalAudioPath.isEmpty())
        {
            origAudioFileIndex = nextFileIndex++;
            args << "--default-track-flag" << "0:no";
            if (!lang.isEmpty())
            {
                args << "--language" << "0:" + lang;
            }
            if (!studio.isEmpty())
            {
                args << "--track-name" << QString("0:Оригинал [%1]").arg(studio);
            }
            addNoChaptersForContainer(originalAudioPath);
            args << originalAudioPath;
        }
    }

    int signsFileIndex = -1;
    if (m_params.contains("signsPath"))
    {
        const QString signsPath = m_params["signsPath"].toString();
        if (!signsPath.isEmpty())
        {
            signsFileIndex = nextFileIndex++;
            const QString trackName =
                subAuthor.isEmpty() ? QStringLiteral("0:Надписи") : QString("0:Надписи [%1]").arg(subAuthor);
            args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus"
                 << "--track-name" << trackName;
            addNoChaptersForContainer(signsPath);
            args << signsPath;
        }
    }

    int subsFileIndex = -1;
    if (m_params.contains("subtitlesPath"))
    {
        const QString subtitlesPath = m_params["subtitlesPath"].toString();
        if (!subtitlesPath.isEmpty())
        {
            subsFileIndex = nextFileIndex++;
            const QString trackName =
                subAuthor.isEmpty() ? QStringLiteral("0:Субтитры") : QString("0:Субтитры [%1]").arg(subAuthor);
            args << "--default-track-flag" << "0:no" << "--forced-display-flag" << "0:no" << "--language" << "0:rus"
                 << "--track-name" << trackName;
            addNoChaptersForContainer(subtitlesPath);
            args << subtitlesPath;
        }
    }

    // Порядок дорожек: Видео -> Русский звук -> Оригинальный звук -> Надписи -> Полные субтитры
    if (rusAudioFileIndex >= 0)
    {
        trackOrder << QString("%1:0").arg(rusAudioFileIndex);
    }
    if (useAudioFromVideo && audioTrackFromVideoId >= 0 && videoFileIndex >= 0)
    {
        trackOrder << QString("%1:%2").arg(videoFileIndex).arg(audioTrackFromVideoId);
    }
    else if (origAudioFileIndex >= 0)
    {
        trackOrder << QString("%1:0").arg(origAudioFileIndex);
    }
    if (signsFileIndex >= 0)
    {
        trackOrder << QString("%1:0").arg(signsFileIndex);
    }
    if (subsFileIndex >= 0)
    {
        trackOrder << QString("%1:0").arg(subsFileIndex);
    }

    if (trackOrder.size() > 1)
    {
        args << "--track-order" << trackOrder.join(',');
    }

    m_processManager->startProcess(mkvmergePath, args);
}

void ManualAssembler::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if ((m_processManager != nullptr) && m_processManager->wasKilled())
    {
        emit logMessage("Ручная сборка отменена пользователем.", LogCategory::APP);
        emit finished(false);
        return;
    }

    if (m_currentStep == Step::AssemblingMkv && exitCode == 1 && exitStatus == QProcess::NormalExit)
    {
        emit logMessage("mkvmerge завершился с предупреждением (exit code 1). Сборка успешна.", LogCategory::MKVTOOLNIX,
                        LogLevel::Warning);
    }
    else if (exitCode != 0 || exitStatus != QProcess::NormalExit)
    {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.", LogCategory::APP,
                        LogLevel::Error);
        emit finished(false);
        return;
    }

    if (m_currentStep == Step::NormalizingAudio)
    {
        emit logMessage("Нормализация аудио завершена.", LogCategory::APP);

        if (m_didLaunchNugen)
        {
            emit logMessage("Закрытие NUGEN Audio AMB...", LogCategory::APP);
            QProcess::execute("taskkill", {"/F", "/IM", "NUGEN Audio AMB.exe", "/T"});
            m_didLaunchNugen = false;
        }
        QFileInfo originalInfo(m_originalAudioPathBeforeNormalization);
        QString tempPath = originalInfo.dir().filePath("temp_audio_for_nugen.wav");
        QString normalizedTempPath = originalInfo.dir().filePath("temp_audio_for_nugen_corrected.wav");

        if (QFileInfo::exists(normalizedTempPath))
        {
            QString finalBaseName = originalInfo.baseName() + "_corrected.wav";
            QString finalPath = originalInfo.dir().filePath(finalBaseName);

            if (QFile::exists(finalPath))
            {
                QFile::remove(finalPath);
            }
            if (QFile::rename(normalizedTempPath, finalPath))
            {
                emit logMessage("Нормализованный файл сохранен как: " + finalPath, LogCategory::APP);
                m_params["russianAudioPath"] = finalPath;
            }
            else
            {
                emit logMessage("ОШИБКА: Не удалось скопировать нормализованный файл. Будет использован исходный файл.",
                                LogCategory::APP, LogLevel::Error);
                m_params["russianAudioPath"] = m_originalAudioPathBeforeNormalization;
            }
        }
        else
        {
            emit logMessage("ПРЕДУПРЕЖДЕНИЕ: Нормализованный файл не найден. Будет использован исходный.",
                            LogCategory::APP, LogLevel::Warning);
            QFile::rename(tempPath, m_originalAudioPathBeforeNormalization);
            m_params["russianAudioPath"] = m_originalAudioPathBeforeNormalization;
        }
        QFile::remove(tempPath);
        QFile::remove(normalizedTempPath);
        if (m_params["convertAudio"].toBool())
        {
            convertAudio();
        }
        else
        {
            processSubtitlesAndAssemble();
        }
    }
    else if (m_currentStep == Step::ConvertingAudio)
    {
        m_progressTimer->stop();
        disconnect(m_progressTimer, &QTimer::timeout, this, &ManualAssembler::onConversionProgress);
        QFile::remove(m_progressLogPath);
        emit logMessage("Конвертация аудио завершена.", LogCategory::APP);
        processSubtitlesAndAssemble();
    }
    else if (m_currentStep == Step::AssemblingMkv)
    {
        emit logMessage("Ручная сборка MKV успешно завершена.", LogCategory::APP);
        emit finished(true);
    }
}

void ManualAssembler::onProcessText(const QString& output)
{
    if (!output.trimmed().isEmpty())
    {
        LogCategory category = (m_currentStep == Step::ConvertingAudio) ? LogCategory::FFMPEG : LogCategory::MKVTOOLNIX;
        emit logMessage(output.trimmed(), category);
    }

    if (m_currentStep == Step::AssemblingMkv)
    {
        QRegularExpression re("Progress: (\\d+)%");
        auto it = re.globalMatch(output);
        while (it.hasNext())
        {
            auto match = it.next();
            int percentage = match.captured(1).toInt();
            emit progressUpdated(percentage, "Сборка MKV");
        }
    }
}

void ManualAssembler::onConversionProgress()
{
    QFile progressFile(m_progressLogPath);
    if (!progressFile.open(QIODevice::ReadOnly))
    {
        return;
    }

    QTextStream in(&progressFile);
    qint64 totalDurationUs = 0;
    qint64 currentTimeUs = 0;

    totalDurationUs = static_cast<qint64>(m_sourceAudioDurationS * 1000000);

    while (!in.atEnd())
    {
        QString line = in.readLine();
        if (line.startsWith("out_time_us="))
        {
            currentTimeUs = line.split('=').last().toLongLong();
        }
    }
    progressFile.close();

    if (totalDurationUs > 0)
    {
        int percentage = static_cast<int>((currentTimeUs * 100) / totalDurationUs);
        emit progressUpdated(percentage, "Конвертация аудио");
    }
}

void ManualAssembler::cancelOperation()
{
    emit logMessage("Получена команда на отмену ручной сборки...", LogCategory::APP);
    if (m_processManager != nullptr)
    {
        m_processManager->killProcess();
    }
}

ProcessManager* ManualAssembler::getProcessManager() const
{
    return m_processManager;
}
