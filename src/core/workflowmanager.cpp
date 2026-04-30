#include "workflowmanager.h"

#include "assprocessor.h"
#include "chapterhelper.h"
#include "fontfinder.h"
#include "mainwindow.h"
#include "manualrenderer.h"
#include "processmanager.h"
#include "trackselectordialog.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <windows.h> // Для API шрифтов

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
    if (rFrameRate.isEmpty() || avgFrameRate.isEmpty())
    {
        return false;
    }

    qint64 rNum = 0;
    qint64 rDen = 0;
    qint64 avgNum = 0;
    qint64 avgDen = 0;
    if (!parseFpsRational(rFrameRate, rNum, rDen) || !parseFpsRational(avgFrameRate, avgNum, avgDen))
    {
        return false;
    }

    // Allow tiny mismatch from probe rounding (e.g. 25 vs 431025/17241 type should fail, near-equal should pass).
    const double r = static_cast<double>(rNum) / static_cast<double>(rDen);
    const double avg = static_cast<double>(avgNum) / static_cast<double>(avgDen);
    return qAbs(r - avg) < 0.001;
}

QString buildSettsExprForFps(const QString& fps)
{
    qint64 num = 0;
    qint64 den = 0;
    if (!parseFpsRational(fps, num, den))
    {
        return "";
    }
    // Generic CFR expression: frame step = den/num seconds.
    // bsf 'setts' evaluates TB as stream time base, so this avoids hardcoding 25fps/3600 ticks.
    return QString("pts=N*%1/(%2*TB):dts=N*%1/(%2*TB)").arg(den).arg(num);
}

double probeTsVideoEndTime(ProcessManager* processManager, const QString& ffprobePath, const QString& tsPath)
{
    QByteArray packetOutput;
    const bool packetsOk = processManager->executeAndWait(ffprobePath,
                                                          {"-v", "quiet", "-select_streams", "v:0", "-show_entries",
                                                           "packet=pts_time,duration_time", "-of", "csv=p=0", tsPath},
                                                          packetOutput);
    if (packetsOk && !packetOutput.isEmpty())
    {
        double lastPts = -1.0;
        double lastDur = 0.0;
        const QStringList lines = QString::fromUtf8(packetOutput).split('\n', Qt::SkipEmptyParts);
        for (const QString& rawLine : lines)
        {
            const QString line = rawLine.trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            const QStringList cols = line.split(',', Qt::KeepEmptyParts);
            if (cols.isEmpty())
            {
                continue;
            }
            bool ptsOk = false;
            const double pts = cols[0].trimmed().toDouble(&ptsOk);
            if (!ptsOk)
            {
                continue;
            }
            double dur = 0.0;
            if (cols.size() > 1)
            {
                bool durOk = false;
                const double parsedDur = cols[1].trimmed().toDouble(&durOk);
                if (durOk && parsedDur > 0.0)
                {
                    dur = parsedDur;
                }
            }
            lastPts = pts;
            lastDur = dur;
        }
        if (lastPts >= 0.0)
        {
            return lastPts + lastDur;
        }
    }

    QByteArray formatOutput;
    const bool formatOk = processManager->executeAndWait(
        ffprobePath, {"-v", "quiet", "-show_entries", "format=duration", "-of", "csv=p=0", tsPath}, formatOutput);
    if (!formatOk || formatOutput.isEmpty())
    {
        return -1.0;
    }
    bool durOk = false;
    const double duration = QString::fromUtf8(formatOutput).trimmed().toDouble(&durOk);
    return durOk ? duration : -1.0;
}

QJsonObject probeTsVideoStats(ProcessManager* processManager, const QString& ffprobePath, const QString& tsPath)
{
    QJsonObject stats{
        {          "path", tsPath},
        {     "packetsOk",  false},
        {      "firstPts",   -1.0},
        {       "lastPts",   -1.0},
        {       "lastDur",    0.0},
        { "endPtsPlusDur",   -1.0},
        {"formatDuration",   -1.0},
    };

    QByteArray packetOutput;
    const bool packetsOk = processManager->executeAndWait(ffprobePath,
                                                          {"-v", "quiet", "-select_streams", "v:0", "-show_entries",
                                                           "packet=pts_time,duration_time", "-of", "csv=p=0", tsPath},
                                                          packetOutput);
    stats["packetsOk"] = packetsOk;
    if (packetsOk && !packetOutput.isEmpty())
    {
        bool firstSet = false;
        double firstPts = -1.0;
        double lastPts = -1.0;
        double lastDur = 0.0;
        int packetCount = 0;
        const QStringList lines = QString::fromUtf8(packetOutput).split('\n', Qt::SkipEmptyParts);
        for (const QString& rawLine : lines)
        {
            const QString line = rawLine.trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            const QStringList cols = line.split(',', Qt::KeepEmptyParts);
            if (cols.isEmpty())
            {
                continue;
            }
            bool ptsOk = false;
            const double pts = cols[0].trimmed().toDouble(&ptsOk);
            if (!ptsOk)
            {
                continue;
            }
            double dur = 0.0;
            if (cols.size() > 1)
            {
                bool durOk = false;
                const double parsedDur = cols[1].trimmed().toDouble(&durOk);
                if (durOk && parsedDur > 0.0)
                {
                    dur = parsedDur;
                }
            }
            if (!firstSet)
            {
                firstPts = pts;
                firstSet = true;
            }
            lastPts = pts;
            lastDur = dur;
            packetCount++;
        }
        stats["packetCount"] = packetCount;
        stats["firstPts"] = firstPts;
        stats["lastPts"] = lastPts;
        stats["lastDur"] = lastDur;
        if (lastPts >= 0.0)
        {
            stats["endPtsPlusDur"] = lastPts + lastDur;
        }
    }

    QByteArray formatOutput;
    const bool formatOk = processManager->executeAndWait(
        ffprobePath, {"-v", "quiet", "-show_entries", "format=duration", "-of", "csv=p=0", tsPath}, formatOutput);
    if (formatOk && !formatOutput.isEmpty())
    {
        bool durOk = false;
        const double duration = QString::fromUtf8(formatOutput).trimmed().toDouble(&durOk);
        if (durOk)
        {
            stats["formatDuration"] = duration;
        }
    }

    return stats;
}

double effectiveTsDuration(const QJsonObject& stats)
{
    const double firstPts = stats.value("firstPts").toDouble(-1.0);
    const double endPtsPlusDur = stats.value("endPtsPlusDur").toDouble(-1.0);
    if (firstPts >= 0.0 && endPtsPlusDur >= 0.0 && endPtsPlusDur >= firstPts)
    {
        return endPtsPlusDur - firstPts;
    }
    return stats.value("formatDuration").toDouble(-1.0);
}

double frameStepSecondsForFps(const QString& fps)
{
    qint64 num = 0;
    qint64 den = 0;
    if (!parseFpsRational(fps, num, den) || num <= 0 || den <= 0)
    {
        return 0.0;
    }
    return static_cast<double>(den) / static_cast<double>(num);
}

QString probeFirstFrameMd5(ProcessManager* processManager, const QString& ffmpegPath, const QString& inputPath,
                           double seekSeconds)
{
    QStringList args;
    args << "-v" << "error";
    if (seekSeconds >= 0.0)
    {
        args << "-ss" << QString::number(seekSeconds, 'f', 3);
    }
    args << "-i" << inputPath << "-frames:v" << "1" << "-an" << "-f" << "framemd5" << "-";

    QByteArray out;
    const bool ok = processManager->executeAndWait(ffmpegPath, args, out);
    if (!ok || out.isEmpty())
    {
        return "";
    }

    const QString text = QString::fromUtf8(out);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (qsizetype idx = lines.size(); idx-- > 0;)
    {
        const QString line = lines[idx].trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }
        const QStringList parts = line.split(',', Qt::SkipEmptyParts);
        if (!parts.isEmpty())
        {
            return parts.last().trimmed();
        }
    }
    return "";
}

QString probeFilteredFirstFrameMd5(ProcessManager* processManager, const QString& ffmpegPath, const QString& inputPath,
                                   double seekSeconds, double probeDuration, const QString& videoFilter)
{
    QStringList args;
    args << "-v" << "error";
    if (seekSeconds >= 0.0)
    {
        args << "-ss" << QString::number(seekSeconds, 'f', 3);
    }
    if (probeDuration > 0.0)
    {
        args << "-t" << QString::number(probeDuration, 'f', 3);
    }
    args << "-i" << inputPath;
    if (!videoFilter.isEmpty())
    {
        args << "-vf" << videoFilter;
    }
    args << "-frames:v" << "1" << "-an" << "-f" << "framemd5" << "-";

    QByteArray out;
    const bool ok = processManager->executeAndWait(ffmpegPath, args, out);
    if (!ok || out.isEmpty())
    {
        return "";
    }

    const QString text = QString::fromUtf8(out);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (qsizetype idx = lines.size(); idx-- > 0;)
    {
        const QString line = lines[idx].trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }
        const QStringList parts = line.split(',', Qt::SkipEmptyParts);
        if (!parts.isEmpty())
        {
            return parts.last().trimmed();
        }
    }
    return "";
}

QString probeLastFrameMd5FromTailWindow(ProcessManager* processManager, const QString& ffmpegPath,
                                        const QString& inputPath, double sseofSeconds)
{
    // Dump framemd5 for a short tail window and return the LAST frame md5.
    // This is more reliable than -frames:v 1 when TS seeking lands after EOF.
    QStringList args;
    args << "-v" << "error" << "-nostats" << "-sseof" << QString::number(sseofSeconds, 'f', 3) << "-i" << inputPath
         << "-an" << "-vsync" << "vfr" << "-f" << "framemd5" << "-";

    QByteArray out;
    const bool ok = processManager->executeAndWait(ffmpegPath, args, out);
    if (!ok || out.isEmpty())
    {
        return "";
    }

    const QString text = QString::fromUtf8(out);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (qsizetype idx = lines.size(); idx-- > 0;)
    {
        const QString line = lines[idx].trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }
        const QStringList parts = line.split(',', Qt::SkipEmptyParts);
        if (!parts.isEmpty())
        {
            return parts.last().trimmed();
        }
    }
    return "";
}

void enforceAacFromWavForPresetArgs(QStringList& args, const QString& wavPath)
{
    if (args.isEmpty() || wavPath.isEmpty())
    {
        return;
    }

    const bool hasAudioOutput = !args.contains(QStringLiteral("-an"));
    if (!hasAudioOutput)
    {
        return;
    }

    // Add WAV as second input right after the first input pair.
    int firstInputIdx = -1;
    for (int i = 0; i < args.size() - 1; ++i)
    {
        if (args.at(i) == QLatin1String("-i"))
        {
            firstInputIdx = i;
            break;
        }
    }
    if (firstInputIdx != -1)
    {
        args.insert(firstInputIdx + 2, wavPath);
        args.insert(firstInputIdx + 2, QStringLiteral("-i"));
    }

    // Redirect audio maps from input 0:* to input 1:*.
    for (int i = 0; i < args.size() - 1; ++i)
    {
        if (args.at(i) != QLatin1String("-map"))
        {
            continue;
        }
        const QString mapValue = args.at(i + 1);
        if (mapValue.startsWith(QLatin1String("0:a")))
        {
            args[i + 1] = QStringLiteral("1:a:0");
        }
    }

    bool hasAudioCodecOption = false;
    bool hasAudioBitrateOption = false;
    for (int i = 0; i < args.size() - 1; ++i)
    {
        const QString key = args.at(i);
        if (key == QLatin1String("-c:a") || key == QLatin1String("-codec:a"))
        {
            hasAudioCodecOption = true;
            if (AppSettings::instance().hasAacAtCodec())
            {
                args[i + 1] = QStringLiteral("aac_at");
            }
            else
            {
                args[i + 1] = QStringLiteral("aac");
            }
        }
        else if (key == QLatin1String("-b:a") || key == QLatin1String("-audio_bitrate"))
        {
            hasAudioBitrateOption = true;
            args[i + 1] = QStringLiteral("256k");
        }
    }

    // Handle preset patterns like "-c copy" where stream-specific codec isn't set.
    for (int i = 0; i < args.size() - 1; ++i)
    {
        if ((args.at(i) == QLatin1String("-c") || args.at(i) == QLatin1String("-codec")) &&
            args.at(i + 1) == QLatin1String("copy"))
        {
            args[i] = QStringLiteral("-c:v");
            hasAudioCodecOption = false;
        }
    }

    const qsizetype outIdx = args.size() - 1;
    if (!hasAudioCodecOption && outIdx >= 0)
    {
        args.insert(outIdx, QStringLiteral("aac_at"));
        args.insert(outIdx, QStringLiteral("-c:a"));
    }
    if (!hasAudioBitrateOption && outIdx >= 0)
    {
        args.insert(outIdx, QStringLiteral("256k"));
        args.insert(outIdx, QStringLiteral("-b:a"));
    }
}
} // namespace

WorkflowManager::WorkflowManager(ReleaseTemplate t, const QString& episodeNumberForPost,
                                 const QString& episodeNumberForSearch, const QSettings& settings,
                                 MainWindow* mainWindow)
    : QObject(nullptr), m_mainWindow(mainWindow), m_template(t), m_episodeNumberForPost(episodeNumberForPost),
      m_episodeNumberForSearch(episodeNumberForSearch), m_wasUserInputRequested(false)
{
    m_webUiHost = settings.value("webUi/host", "http://127.0.0.1").toString();
    m_webUiPort = settings.value("webUi/port", 8080).toInt();
    m_webUiUser = settings.value("webUi/user").toString();
    m_webUiPassword = settings.value("webUi/password").toString();
    m_mkvmergePath = settings.value("paths/mkvmerge", "mkvmerge").toString();
    m_mkvextractPath = settings.value("paths/mkvextract", "mkvextract.exe").toString();
    m_ffmpegPath = settings.value("paths/ffmpeg", "ffmpeg").toString();
    m_renderPreset = AppSettings::instance().findRenderPreset(m_template.renderPresetName);
    m_customRenderArgs = settings.value("render/custom_args", "").toString();
    m_overrideSubsPath = m_mainWindow->getOverrideSubsPath();
    m_overrideSignsPath = m_mainWindow->getOverrideSignsPath();
    m_userChaptersXmlPath = m_mainWindow->getChaptersXmlPath();
    m_isNormalizationEnabled = m_mainWindow->isNormalizationEnabled();
    m_isSrtMasterDecoupled = m_mainWindow->isSrtSubsDecoupled();

    m_netManager = new QNetworkAccessManager(this);
    m_hashFindTimer = new QTimer(this);
    m_progressTimer = new QTimer(this);
    m_processManager = new ProcessManager(this);
    m_assProcessor = new AssProcessor(this);
    m_fontFinder = new FontFinder(this);

    connect(m_fontFinder, &FontFinder::logMessage, this, &WorkflowManager::logMessage);
    connect(m_fontFinder, &FontFinder::finished, this, &WorkflowManager::onFontFinderFinished);
    connect(m_assProcessor, &AssProcessor::logMessage, this, &WorkflowManager::logMessage);
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onPollingTimerTimeout);
    connect(m_processManager, &ProcessManager::processOutput, this, &WorkflowManager::onProcessStdOut);
    connect(m_processManager, &ProcessManager::processStdErr, this, &WorkflowManager::onProcessStdErr);
    connect(m_processManager, &ProcessManager::processFinished, this, &WorkflowManager::onProcessFinished);
}

WorkflowManager::~WorkflowManager()
{
    delete m_paths;
}

void WorkflowManager::start()
{
    m_skipChaptersForWorkflow = false;
    m_chapterContinueKind = ChapterContinueKind::None;
    m_pendingMkvInfoRoot = QJsonObject();

    QString baseDir = AppSettings::instance().effectiveProjectDirectory();
    QString sanitizedTitle = PathManager::sanitizeForPath(m_template.seriesTitle);
    QString baseDownloadPath = QString("%1/%2/Episode %3").arg(baseDir, sanitizedTitle, m_episodeNumberForSearch);

    delete m_paths;
    m_paths = new PathManager(baseDownloadPath);
    m_savePath = m_paths->sourcesPath;
    emit logMessage("Структура папок создана в: " + m_savePath, LogCategory::APP);
    emit logMessage("Шаг 1.0: Аутентификация в qBittorrent...", LogCategory::QBITTORRENT);
    login();
}

void WorkflowManager::downloadRss()
{
    emit logMessage("Шаг 1.2: Скачивание RSS-фида...", LogCategory::APP);
    QNetworkRequest request(m_template.rssUrl);
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onRssDownloaded(reply); });
}

void WorkflowManager::startWithManualFile(const QString& filePath)
{
    m_skipChaptersForWorkflow = false;
    m_chapterContinueKind = ChapterContinueKind::None;
    m_pendingMkvInfoRoot = QJsonObject();

    delete m_paths;

    UserFileAction fileAction = AppSettings::instance().userFileAction();
    bool useOriginal = (fileAction == UserFileAction::UseOriginalPath);
    QString baseDownloadPath;

    if (useOriginal)
    {
        QFileInfo fileInfo(filePath);
        baseDownloadPath = fileInfo.absolutePath();
    }
    else
    {
        QString baseDir = AppSettings::instance().effectiveProjectDirectory();
        QString sanitizedTitle = PathManager::sanitizeForPath(m_template.seriesTitle);
        baseDownloadPath = QString("%1/%2/Episode %3").arg(baseDir, sanitizedTitle, m_episodeNumberForSearch);
    }

    m_paths = new PathManager(baseDownloadPath, useOriginal);
    emit logMessage("Структура папок создана в: " + m_paths->basePath, LogCategory::APP);

    QString newPath = handleUserFile(filePath, m_paths->sourcesPath);
    if (newPath.isEmpty())
    {
        emit logMessage("Критическая ошибка: не удалось переместить указанный видеофайл.", LogCategory::APP,
                        LogLevel::Error);
        emit workflowAborted();
        return;
    }

    m_mkvFilePath = newPath;
    m_sourceFormat = detectSourceFormat(m_mkvFilePath);
    m_mainWindow->findChild<QLineEdit*>("mkvPathLineEdit")->setText(m_mkvFilePath);

    if (m_sourceFormat == SourceFormat::MP4)
    {
        emit logMessage("Работа с MP4 файлом, указанным вручную: " + m_mkvFilePath, LogCategory::APP);
        getMp4Info();
    }
    else
    {
        emit logMessage("Работа с MKV файлом, указанным вручную: " + m_mkvFilePath, LogCategory::APP);
        getMkvInfo();
    }
}

void WorkflowManager::onRssDownloaded(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit logMessage("Ошибка сети (RSS): " + reply->errorString(), LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        reply->deleteLater();
        return;
    }
    emit logMessage("RSS-фид успешно скачан.", LogCategory::APP);
    parseRssAndDownload(reply->readAll());
    reply->deleteLater();
}

void WorkflowManager::parseRssAndDownload(const QByteArray& rssData)
{
    emit logMessage("Шаг 1.3: Поиск подходящих торрентов в RSS...", LogCategory::APP);
    QXmlStreamReader xml(rssData);
    QList<TorrentInfo> candidates;

    while (!xml.atEnd() && !xml.hasError())
    {
        xml.readNext();
        if (xml.isStartElement() && xml.name().toString() == "item")
        {
            QString currentTitle, currentLink;
            while (!(xml.isEndElement() && xml.name().toString() == "item"))
            {
                xml.readNext();
                if (xml.isStartElement())
                {
                    if (xml.name().toString() == "title")
                        currentTitle = xml.readElementText();
                    else if (xml.name().toString() == "link")
                        currentLink = xml.readElementText();
                }
            }

            bool is1080p = currentTitle.contains("1080p", Qt::CaseInsensitive);
            bool isNotHevc = !currentTitle.contains("HEVC", Qt::CaseInsensitive);
            bool hasCorrectEpisode = currentTitle.contains(QString(" - %1 ").arg(m_episodeNumberForSearch));

            // Проверка по тегам из шаблона
            bool hasTags = m_template.releaseTags.isEmpty(); // Если тегов нет, считаем, что проверка пройдена
            if (!hasTags)
            {
                for (const QString& tag : m_template.releaseTags)
                {
                    if (currentTitle.contains(tag, Qt::CaseInsensitive))
                    {
                        hasTags = true;
                        break;
                    }
                }
            }

            if (is1080p && isNotHevc && hasCorrectEpisode && hasTags)
            {
                candidates.append({currentTitle, currentLink});
            }
        }
    }

    if (candidates.isEmpty())
    {
        emit logMessage("Не удалось найти подходящий торрент для серии " + m_episodeNumberForPost, LogCategory::APP);
        emit workflowAborted();
    }
    else if (candidates.size() == 1)
    {
        emit logMessage("Найден один подходящий торрент. Начинаем скачивание...", LogCategory::APP);
        m_magnetLink = candidates.first().magnetLink;
        m_torrentHash = getInfohashFromMagnet(m_magnetLink);
        checkExistingTorrents();
    }
    else
    {
        emit logMessage(
            QString("Найдено %1 подходящих торрентов. Требуется выбор пользователя...").arg(candidates.size()),
            LogCategory::APP);
        emit multipleTorrentsFound(candidates);
    }
}

void WorkflowManager::resumeWithSelectedTorrent(const TorrentInfo& selected)
{
    if (selected.magnetLink.isEmpty())
    {
        emit logMessage("Выбор торрента был отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    emit logMessage("Пользователь выбрал: " + selected.title, LogCategory::APP);
    m_magnetLink = selected.magnetLink;
    m_torrentHash = getInfohashFromMagnet(m_magnetLink);
    checkExistingTorrents();
}

QString WorkflowManager::getInfohashFromMagnet(const QString& magnetLink) const
{
    // Regex для поиска infohash v1 (40 hex-символов) или v2 (64 hex-символа)
    QRegularExpression re("xt=urn:(?:btih|btmh):([a-fA-F0-9]{40,64})");
    QRegularExpressionMatch match = re.match(magnetLink);
    if (match.hasMatch())
    {
        return match.captured(1).toLower();
    }
    return QString();
}

void WorkflowManager::startDownload(const QString& magnetLink)
{
    m_magnetLink = magnetLink;
    addTorrent(m_magnetLink);
}

void WorkflowManager::login()
{
    emit logMessage("Шаг 1.1: Аутентификация в qBittorrent Web UI...", LogCategory::QBITTORRENT);

    QUrl url(QString("%1:%2/api/v2/auth/login").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("username", m_webUiUser);
    postData.addQueryItem("password", m_webUiPassword);

    QNetworkReply* reply = m_netManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onLoginFinished(reply); });
}

void WorkflowManager::onLoginFinished(QNetworkReply* reply)
{
    // --- Блок 1: Попытка автозапуска qBittorrent ---
    // Срабатывает только если:
    // 1. Произошла ошибка отказа в соединении.
    // 2. Мы еще НЕ пытались запустить qBittorrent в этой сессии.
    if (reply->error() == QNetworkReply::ConnectionRefusedError && !m_qbtLaunchAttempted)
    {
        m_qbtLaunchAttempted = true;
        emit logMessage("Не удалось подключиться. Попытка запустить qBittorrent...", LogCategory::QBITTORRENT);

        QString qbtPath = AppSettings::instance().qbittorrentPath();
        if (qbtPath.isEmpty() || !QFileInfo::exists(qbtPath))
        {
            emit logMessage(
                "Путь к qbittorrent.exe не указан или неверен в настройках. Невозможно запустить автоматически.",
                LogCategory::APP);
            emit workflowAborted();
            reply->deleteLater();
            return;
        }

        // Запускаем qBittorrent в отдельном, независимом процессе
        bool started = QProcess::startDetached(qbtPath);

        if (!started)
        {
            emit logMessage("Не удалось запустить процесс qbittorrent.exe. Проверьте путь и права.", LogCategory::APP);
            emit workflowAborted();
            reply->deleteLater();
            return;
        }

        emit logMessage("qBittorrent запущен. Ожидание 5 секунд для инициализации...", LogCategory::QBITTORRENT);

        // Ждем 5 секунд, чтобы qBittorrent успел запуститься и инициализировать веб-сервер,
        // а затем пробуем залогиниться снова.
        QTimer::singleShot(5000, this, &WorkflowManager::login);

        reply->deleteLater();
        return; // Прерываем выполнение текущего слота, ждем ответа от нового вызова login()
    }

    // --- Блок 2: Обработка всех остальных ошибок сети ---
    if (reply->error() != QNetworkReply::NoError)
    {
        QString errorMessage = "Ошибка сети (Login): " + reply->errorString();
        // Добавляем более понятное сообщение, если это все еще отказ в соединении (уже после попытки запуска)
        if (reply->error() == QNetworkReply::ConnectionRefusedError)
        {
            errorMessage = "Не удалось подключиться к qBittorrent. Убедитесь, что программа запущена, веб-интерфейс "
                           "включен и настройки в DubbingTool верны.";
        }
        emit logMessage(errorMessage, LogCategory::QBITTORRENT);
        emit workflowAborted();
        reply->deleteLater();
        return;
    }

    // --- Блок 3: Успешное завершение (код остается без изменений) ---
    QVariant cookieVariant = reply->header(QNetworkRequest::SetCookieHeader);
    if (cookieVariant.isValid())
    {
        m_cookies = cookieVariant.value<QList<QNetworkCookie>>();
        bool sidFound = false;
        for (const auto& cookie : m_cookies)
        {
            if (cookie.name() == "SID")
            {
                sidFound = true;
                break;
            }
        }
        if (sidFound)
        {
            emit logMessage("Аутентификация успешна. Проверка статуса в клиенте...", LogCategory::QBITTORRENT);
            downloadRss();
        }
        else
        {
            emit logMessage("Ошибка: SID cookie не получен. Проверьте логин/пароль в настройках.",
                            LogCategory::QBITTORRENT, LogLevel::Error);
            emit workflowAborted();
        }
    }
    else
    {
        emit logMessage("Ошибка: ответ на аутентификацию не содержит cookie.", LogCategory::QBITTORRENT,
                        LogLevel::Error);
        emit workflowAborted();
    }

    reply->deleteLater();
}

void WorkflowManager::checkExistingTorrents()
{
    QUrl url(QString("%1:%2/api/v2/torrents/info").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentListForHashCheckReceived(reply); });
}

void WorkflowManager::addTorrent(const QString& magnetLink)
{
    emit logMessage("Шаг 1.4: Добавление торрента через Web API...", LogCategory::APP);
    m_currentStep = Step::AddingTorrent;

    QUrl url(QString("%1:%2/api/v2/torrents/add").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart urlsPart;
    urlsPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"urls\""));
    urlsPart.setBody(magnetLink.toUtf8());

    QHttpPart savePathPart;
    savePathPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"savepath\""));
    savePathPart.setBody(QDir(m_savePath).absolutePath().toUtf8());

    multiPart->append(urlsPart);
    multiPart->append(savePathPart);

    QNetworkReply* reply = m_netManager->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentAdded(reply); });
}

void WorkflowManager::onTorrentAdded(QNetworkReply* reply)
{
    if (reply->error() == QNetworkReply::NoError && reply->readAll().trimmed() == "Ok.")
    {
        emit logMessage("Торрент успешно добавлен в qBittorrent. Начинаем поиск хеша...", LogCategory::APP);
        findTorrentHashAndStartPolling();
    }
    else
    {
        emit logMessage("Ошибка добавления торрента: " + reply->errorString(), LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
    }
    reply->deleteLater();
}

void WorkflowManager::findTorrentHashAndStartPolling()
{
    m_hashFindAttempts = 0;
    connect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt);
    onHashFindAttempt();
}

void WorkflowManager::onHashFindAttempt()
{
    if (m_hashFindAttempts >= MAX_HASH_FIND_ATTEMPTS)
    {
        emit logMessage("Ошибка: не удалось найти хеш торрента после нескольких попыток.", LogCategory::APP,
                        LogLevel::Error);
        m_hashFindTimer->stop();
        disconnect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt);
        emit workflowAborted();
        return;
    }

    m_hashFindAttempts++;
    emit logMessage(QString("Шаг 1.5: Поиск хеша торрента (попытка %1)...").arg(m_hashFindAttempts), LogCategory::APP);

    QUrl url(QString("%1:%2/api/v2/torrents/info").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentListReceived(reply); });

    // Если это не первая попытка, таймер уже запущен. Если первая, запускаем его.
    if (!m_hashFindTimer->isActive())
    {
        m_hashFindTimer->start(HASH_FIND_INTERVAL_MS);
    }
}

void WorkflowManager::onTorrentListReceived(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit logMessage("Ошибка сети при получении списка торрентов: " + reply->errorString(), LogCategory::APP,
                        LogLevel::Error);
        // Не прерываемся, таймер сделает следующую попытку
        reply->deleteLater();
        return;
    }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    QString absoluteSavePath = QDir(m_savePath).absolutePath();

    for (const QJsonValue& value : torrents)
    {
        QJsonObject torrent = value.toObject();
        // Сверяем пути, предварительно нормализовав их
        if (QDir(torrent["save_path"].toString()).absolutePath() == absoluteSavePath)
        {
            m_torrentHash = torrent["hash"].toString();
            emit logMessage("Хеш торрента успешно найден: " + m_torrentHash, LogCategory::APP);

            m_hashFindTimer->stop();
            disconnect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt);

            startPolling();
            reply->deleteLater();
            return;
        }
    }
    emit logMessage("Хеш пока не найден, ждем следующей попытки...", LogCategory::APP);
    reply->deleteLater();
}

void WorkflowManager::onTorrentListForHashCheckReceived(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit logMessage("Ошибка сети при проверке торрентов: " + reply->errorString(), LogCategory::QBITTORRENT,
                        LogLevel::Error);
        emit logMessage("Не удалось проверить статус, переход к скачиванию нового.", LogCategory::APP);
        startDownload(m_magnetLink);
        reply->deleteLater();
        return;
    }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    QJsonObject foundTorrent;
    for (const QJsonValue& value : torrents)
    {
        QJsonObject torrent = value.toObject();
        if (torrent["hash"].toString() == m_torrentHash)
        {
            foundTorrent = torrent;
            break;
        }
    }

    // --- Сценарий А: Торрент НАЙДЕН в клиенте по хешу ---
    if (!foundTorrent.isEmpty())
    {
        double progress = foundTorrent["progress"].toDouble();
        QString state = foundTorrent["state"].toString();
        emit logMessage(QString("Торрент уже существует в клиенте (Состояние: %1, Прогресс: %2%).")
                            .arg(state)
                            .arg(progress * 100, 0, 'f', 1),
                        LogCategory::APP);

        if (state == "missingFiles" || state == "error")
        {
            emit logMessage(
                "Состояние торрента проблемное. Удаляем его из клиента (без файлов) и начинаем скачивание заново.",
                LogCategory::APP);
            deleteTorrentAndRedownload();
            reply->deleteLater();
            return;
        }

        if (progress >= 1.0)
        {
            emit logMessage("Торрент уже скачан. Переходим к обработке.", LogCategory::APP);
            m_savePath = foundTorrent["save_path"].toString();
            m_mkvFilePath = findMkvFileInSavePath();
            if (m_mkvFilePath.isEmpty())
            {
                emit logMessage("Ошибка: торрент скачан, но MKV-файл не найден в его папке. Проверьте содержимое: " +
                                    m_savePath,
                                LogCategory::APP, LogLevel::Error);
                emit workflowAborted();
            }
            else
            {
                getMkvInfo();
            }
        }
        else
        {
            emit logMessage("Торрент не докачан. Возобновляем отслеживание.", LogCategory::APP);
            startPolling();
        }

        // --- Сценарий Б: Торрент НЕ НАЙДЕН в клиенте ---
    }
    else
    {
        emit logMessage("Торрент не найден в клиенте. Проверяем диск (в целевой папке)...", LogCategory::APP);
        m_savePath = m_paths->sourcesPath;
        m_mkvFilePath = findMkvFileInSavePath();
        if (!m_mkvFilePath.isEmpty())
        {
            emit logMessage("Найден готовый файл на диске. Переходим к обработке.", LogCategory::APP);
            getMkvInfo();
        }
        else
        {
            emit logMessage("Ничего не найдено. Начинаем скачивание.", LogCategory::APP);
            startDownload(m_magnetLink);
        }
    }

    reply->deleteLater();
}

QString WorkflowManager::findMkvFileInSavePath()
{
    QDir dir(m_savePath);
    if (!dir.exists())
        return QString();

    dir.setNameFilters({"*.mkv"});
    QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo : list)
    {
        if (!fileInfo.fileName().startsWith("[DUB"))
        {
            return fileInfo.absoluteFilePath();
        }
    }
    return QString();
}

void WorkflowManager::deleteTorrentAndRedownload()
{
    emit logMessage("Удаление некорректного торрента из qBittorrent...", LogCategory::QBITTORRENT);

    QUrl url(QString("%1:%2/api/v2/torrents/delete").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("hashes", m_torrentHash);
    postData.addQueryItem("deleteFiles", "false");

    QNetworkReply* reply = m_netManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentDeleted(reply); });
}

void WorkflowManager::onTorrentDeleted(QNetworkReply* reply)
{
    if (reply->error() == QNetworkReply::NoError)
    {
        emit logMessage("Старый торрент успешно удален.", LogCategory::QBITTORRENT);
    }
    else
    {
        emit logMessage("Не удалось удалить старый торрент: " + reply->errorString(), LogCategory::QBITTORRENT);
    }
    startDownload(m_magnetLink);
    reply->deleteLater();
}

void WorkflowManager::startPolling()
{
    m_currentStep = Step::Polling;
    emit logMessage("Начинаем отслеживание прогресса скачивания...", LogCategory::APP);
    emit progressUpdated(0, "Скачивание торрента");
    m_progressTimer->disconnect();
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onPollingTimerTimeout);
    onPollingTimerTimeout();
    m_progressTimer->start(500);
}

void WorkflowManager::onPollingTimerTimeout()
{
    QUrl url(QString("%1:%2/api/v2/torrents/info").arg(m_webUiHost).arg(m_webUiPort));
    QUrlQuery query;
    query.addQueryItem("filter", "all");
    query.addQueryItem("hashes", m_torrentHash);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentProgressReceived(reply); });
}

void WorkflowManager::onTorrentProgressReceived(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        reply->deleteLater();
        return;
    }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    if (torrents.isEmpty())
    {
        reply->deleteLater();
        return;
    }

    QJsonObject torrent = torrents[0].toObject();
    double progress = torrent["progress"].toDouble();
    int percentage = static_cast<int>(progress * 100);

    emit progressUpdated(percentage);

    if (progress >= 1.0)
    {
        m_progressTimer->stop();
        emit logMessage("Скачивание завершено (100%).", LogCategory::APP);
        getTorrentFiles();
    }
    reply->deleteLater();
}

void WorkflowManager::getTorrentFiles()
{
    emit logMessage("Шаг 1.6: Получение списка файлов в торренте...", LogCategory::APP);
    QUrl url(QString("%1:%2/api/v2/torrents/files").arg(m_webUiHost).arg(m_webUiPort));
    QUrlQuery query;
    query.addQueryItem("hash", m_torrentHash);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTorrentFilesReceived(reply); });
}

void WorkflowManager::onTorrentFilesReceived(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        reply->deleteLater();
        return;
    }

    QJsonArray files = QJsonDocument::fromJson(reply->readAll()).array();
    qint64 maxSize = 0;
    QString mainFileName;

    for (const QJsonValue& val : files)
    {
        QJsonObject file = val.toObject();
        QString name = file["name"].toString();
        if (name.endsWith(".mkv") && file["size"].toInteger() > maxSize)
        {
            maxSize = file["size"].toInteger();
            mainFileName = name;
        }
    }

    if (mainFileName.isEmpty())
    {
        emit logMessage("Ошибка: не удалось найти .mkv файл в торренте.", LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
    }
    else
    {
        m_mkvFilePath = m_paths->sourceMkv(mainFileName);
        emit logMessage("Найден основной файл: " + m_mkvFilePath, LogCategory::APP);
        getMkvInfo();
    }
    reply->deleteLater();
}

void WorkflowManager::getMkvInfo()
{
    prepareUserFiles();

    m_sourceFormat = SourceFormat::MKV;
    emit logMessage("Шаг 2: Получение информации о файле MKV...", LogCategory::APP);

    m_currentStep = Step::GettingMkvInfo;

    if (!m_template.endingChapterName.isEmpty())
    {
        m_parsedEndingTime = parseChaptersWithMkvExtract();
        if (!m_parsedEndingTime.isEmpty())
        {
            emit logMessage(
                QString("Найдена глава '%1', время начала: %2").arg(m_template.endingChapterName, m_parsedEndingTime),
                LogCategory::APP);
            if (m_parsedEndingTime.contains('.'))
            {
                QStringList parts = m_parsedEndingTime.split('.');
                if (parts.size() == 2 && parts[1].length() > 3)
                {
                    QString original = m_parsedEndingTime;
                    m_parsedEndingTime = parts[0] + "." + parts[1].left(3);
                    emit logMessage(QString("Время '%1' было нормализовано до '%2' для совместимости.")
                                        .arg(original, m_parsedEndingTime),
                                    LogCategory::APP);
                }
            }
        }
        else
        {
            emit logMessage(QString("ПРЕДУПРЕЖДЕНИЕ: Глава с именем '%1' не найдена в файле. Проверьте правильность "
                                    "названия в шаблоне.")
                                .arg(m_template.endingChapterName),
                            LogCategory::APP, LogLevel::Warning);
        }
    }

    QByteArray jsonData;
    bool success = m_processManager->executeAndWait(m_mkvmergePath, {"-J", m_mkvFilePath}, jsonData);

    if (success)
    {
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonObject root = doc.object();
        QJsonArray tracks = root["tracks"].toArray();
        if (root.contains("container"))
        {
            m_sourceDurationS = static_cast<qint64>(
                root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0);
        }

        QMap<QString, QString> languageAliases;
        languageAliases["zho"] = "zh";
        languageAliases["chi"] = "zh";
        languageAliases["cmn"] = "zh";
        languageAliases["kor"] = "ko";
        languageAliases["jpn"] = "ja";
        languageAliases["jp"] = "ja";
        languageAliases["eng"] = "en";

        m_foundAudioTracks.clear();
        for (const QJsonValue& val : tracks)
        {
            QJsonObject track = val.toObject();
            QJsonObject props = track["properties"].toObject();
            QString codecId = props["codec_id"].toString();

            QString trackLanguage = props["language"].toString();
            QString templateLanguage = m_template.originalLanguage;

            bool languageMatch = false;
            if (!trackLanguage.isEmpty() && !templateLanguage.isEmpty())
            {
                // 1. Прямое совпадение (например, "jpn" == "jpn")
                // 2. Проверка по карте псевдонимов (например, trackLanguage="zho", templateLanguage="zh")
                // 3. "Фоллбэк" на startsWith (например, "jpn".startsWith("jp"))
                languageMatch = (trackLanguage == templateLanguage) ||
                                (languageAliases.contains(trackLanguage) &&
                                 languageAliases.value(trackLanguage) == templateLanguage) ||
                                trackLanguage.startsWith(templateLanguage);
            }

            if (track["type"].toString() == "video" && m_videoTrack.id == -1)
            {
                m_videoTrack.id = track["id"].toInt();
                m_videoTrack.codecId = codecId;
                m_videoTrack.extension = getExtensionForCodec(codecId);
            }
            else if (track["type"].toString() == "audio" && languageMatch)
            {
                AudioTrackInfo info;
                info.id = track["id"].toInt();
                info.codec = codecId;
                info.language = trackLanguage;
                info.name = props["track_name"].toString();
                m_foundAudioTracks.append(info);
            }
            else if (track["type"].toString() == "subtitles" && props["language"].toString() == "rus" &&
                     m_subtitleTrack.id == -1)
            {
                m_subtitleTrack.id = track["id"].toInt();
                m_subtitleTrack.codecId = codecId;
                m_subtitleTrack.extension = getExtensionForCodec(codecId);
            }
        }

        if (m_foundAudioTracks.isEmpty())
        {
            emit logMessage("Предупреждение: не найдено ни одной аудиодорожки на языке '" +
                                m_template.originalLanguage + "'. Оригинал не будет добавлен в сборку.",
                            LogCategory::APP, LogLevel::Warning);
        }
        else if (m_foundAudioTracks.size() == 1)
        {
            const auto& track = m_foundAudioTracks.first();
            m_originalAudioTrack.id = track.id;
            m_originalAudioTrack.codecId = track.codec;
            m_originalAudioTrack.extension = getExtensionForCodec(track.codec);
            emit logMessage(QString("Найдена одна оригинальная аудиодорожка (ID: %1).").arg(track.id),
                            LogCategory::APP);
        }
        else
        {
            emit logMessage(QString("Найдено %1 оригинальных аудиодорожек. Требуется выбор пользователя...")
                                .arg(m_foundAudioTracks.size()),
                            LogCategory::APP);
            emit multipleAudioTracksFound(m_foundAudioTracks);
            return;
        }

        loadChaptersForWorkflow();

        if (m_template.chaptersEnabled && m_chapterMarkers.isEmpty() && !m_skipChaptersForWorkflow)
        {
            m_chapterContinueKind = ChapterContinueKind::AfterMkvProbe;
            m_pendingMkvInfoRoot = root;
            m_wasUserInputRequested = true;
            m_lastStepBeforeRequest = Step::GettingMkvInfo;
            UserInputRequest req;
            req.chaptersRequired = true;
            req.chaptersReason =
                QStringLiteral("Для релиза ожидаются главы, но в контейнере не найдено и на главной странице не "
                               "указан файл XML. Укажите путь к файлу глав или выберите сборку без глав.");
            emit userInputRequired(req);
            return;
        }

        if (root.contains("attachments"))
        {
            extractAttachments(root["attachments"].toArray());
        }
        else
        {
            emit logMessage("Вложений в файле не найдено.", LogCategory::APP);
            m_currentStep = Step::ExtractingAttachments; // Имитируем завершение этого шага
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                      Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
    }
    else
    {
        emit logMessage("Не удалось получить информацию о файле. Процесс остановлен.", LogCategory::APP);
        workflowAborted();
    }
}

QString WorkflowManager::parseChaptersWithMkvExtract()
{
    emit logMessage("Запуск mkvextract для извлечения глав...", LogCategory::APP);

    // Мы не знаем, какой формат получим, поэтому используем .txt
    QString chaptersFilePath = QDir(m_paths->sourcesPath).filePath("chapters_temp.txt");

    QStringList args;
    args << m_mkvFilePath << "chapters" << chaptersFilePath;

    QByteArray dummyOutput;
    if (!m_processManager->executeAndWait(m_mkvextractPath, args, dummyOutput))
    {
        emit logMessage("Ошибка при извлечении глав с помощью mkvextract.", LogCategory::APP, LogLevel::Error);
        QFile::remove(chaptersFilePath);
        return QString();
    }

    QFile chaptersFile(chaptersFilePath);
    if (!chaptersFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        emit logMessage("Не удалось открыть временный файл с главами.", LogCategory::APP);
        QFile::remove(chaptersFilePath);
        return QString();
    }

    QString firstLine = chaptersFile.readLine();
    chaptersFile.seek(0);

    QString foundTime;

    if (firstLine.trimmed().startsWith("<?xml"))
    {
        emit logMessage("Обнаружен XML-формат глав. Запуск XML-парсера...", LogCategory::APP);
        QXmlStreamReader xml(&chaptersFile);
        QString currentTimeStart;
        QString currentChapterString;

        while (!xml.atEnd() && !xml.hasError())
        {
            QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement)
            {
                if (xml.name() == QLatin1String("ChapterAtom"))
                {
                    currentTimeStart.clear();
                    currentChapterString.clear();
                }
                else if (xml.name() == QLatin1String("ChapterTimeStart"))
                {
                    currentTimeStart = xml.readElementText();
                }
                else if (xml.name() == QLatin1String("ChapterString"))
                {
                    currentChapterString = xml.readElementText();
                }
            }
            if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("ChapterAtom"))
            {
                if (currentChapterString.trimmed() == m_template.endingChapterName)
                {
                    foundTime = currentTimeStart.trimmed();
                    break;
                }
            }
        }
        if (xml.hasError())
        {
            emit logMessage("Ошибка парсинга XML глав: " + xml.errorString(), LogCategory::APP, LogLevel::Error);
        }
    }
    else
    {
        emit logMessage("Обнаружен простой формат глав. Запуск текстового парсера...", LogCategory::APP);
        QTextStream in(&chaptersFile);
        while (!in.atEnd())
        {
            QString line = in.readLine();
            if (line.contains(m_template.endingChapterName, Qt::CaseInsensitive))
            {
                QRegularExpression nameRegex("CHAPTER(\\d+)NAME");
                auto nameMatch = nameRegex.match(line);
                if (nameMatch.hasMatch())
                {
                    QString chapterNumber = nameMatch.captured(1);
                    in.seek(0);
                    while (!in.atEnd())
                    {
                        QString timeLine = in.readLine();
                        if (timeLine.startsWith(QString("CHAPTER%1=").arg(chapterNumber)))
                        {
                            foundTime = timeLine.split('=').last();
                            break;
                        }
                    }
                    if (!foundTime.isEmpty())
                    {
                        break;
                    }
                }
            }
        }
    }

    chaptersFile.close();
    QFile::remove(chaptersFilePath);

    return foundTime;
}

void WorkflowManager::extractTracks()
{
    emit logMessage("Шаг 4: Извлечение дорожек (ffmpeg)...", LogCategory::APP);
    m_currentStep = Step::ExtractingTracks;

    if (m_videoTrack.id == -1)
    {
        emit logMessage("Критическая ошибка: в файле отсутствует видеодорожка. Извлечение невозможно.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    QStringList args;
    args << "-y" << "-i" << m_mkvFilePath;

    QString videoOutPath = m_paths->extractedVideo(m_videoTrack.extension);
    args << "-map" << QString("0:%1").arg(m_videoTrack.id) << "-c:v" << "copy" << "-map_chapters" << "-1"
         << videoOutPath;

    if (m_originalAudioTrack.id != -1)
    {
        QString audioExtension = (m_sourceFormat == SourceFormat::MP4) ? "m4a" : "mka";
        QString audioOutPath = m_paths->extractedAudio(audioExtension);
        args << "-map" << QString("0:%1").arg(m_originalAudioTrack.id) << "-c:a" << "copy" << "-map_chapters" << "-1"
             << audioOutPath;
    }

    if (m_sourceFormat == SourceFormat::MKV && m_template.sourceHasSubtitles && m_overrideSubsPath.isEmpty() &&
        m_subtitleTrack.id != -1)
    {
        QString subsOutPath = m_paths->extractedSubs(m_subtitleTrack.extension);
        args << "-map" << QString("0:%1").arg(m_subtitleTrack.id) << "-c:s" << "copy" << "-map_chapters" << "-1"
             << subsOutPath;
    }
    else if (!m_overrideSubsPath.isEmpty())
    {
        emit logMessage("Пропускаем извлечение встроенных субтитров, так как указаны внешние файлы.", LogCategory::APP);
    }
    else if (m_sourceFormat == SourceFormat::MP4 && m_template.sourceHasSubtitles)
    {
        emit logMessage(
            "Предупреждение: MP4 файл не содержит субтитров формата ASS. Укажите субтитры через 'Свои субтитры'.",
            LogCategory::APP, LogLevel::Warning);
    }

    emit progressUpdated(-1, "Извлечение дорожек (ffmpeg)");
    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::cancelOperation()
{
    emit logMessage("Получена команда на отмену операции...", LogCategory::APP);

    if (m_processManager != nullptr)
    {
        m_processManager->killProcess();
    }
    switch (m_currentStep)
    {
    case Step::Polling:
    case Step::AddingTorrent:
        // Если мы скачиваем торрент, просто останавливаем таймеры и завершаем работу.
        // Мы не можем "отменить" скачивание в qBittorrent, но мы можем прекратить его отслеживать.
        emit logMessage("Отслеживание торрента прервано пользователем.", LogCategory::APP);
        if ((m_progressTimer != nullptr) && m_progressTimer->isActive())
        {
            m_progressTimer->stop();
        }
        if ((m_hashFindTimer != nullptr) && m_hashFindTimer->isActive())
        {
            m_hashFindTimer->stop();
        }
        emit workflowAborted();
        break;

    default:
        break;
    }
}

void WorkflowManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if ((m_processManager != nullptr) && m_processManager->wasKilled())
    {
        emit logMessage("Операция успешно отменена пользователем.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    // mkvmerge returns exit code 1 for warnings, which is still a success.
    bool isMkvmergeStep = (m_currentStep == Step::AssemblingMkv || m_currentStep == Step::AssemblingSrtMaster);
    bool isMkvmergeWarning = (exitCode == 1 && isMkvmergeStep && exitStatus == QProcess::NormalExit);

    bool isFfmpegStep = (m_currentStep == Step::ConvertingAudio);
    bool isFfmpegExitCrash = (isFfmpegStep && (exitCode == -1073741819 || static_cast<uint>(exitCode) == 0xC0000005));

    if (isFfmpegExitCrash)
    {
        // Дополнительная проверка: если файл существует и не пустой, считаем это успехом
        QFileInfo checkFile(m_audioConversionCurrentOutputPath);
        if (checkFile.exists() && checkFile.size() > 0)
        {
            emit logMessage("FFmpeg завершился с ошибкой доступа при закрытии, но файл корректен. Продолжаем...",
                            LogCategory::APP);
            // Подменяем переменные, чтобы логика ниже пропустила этот шаг как успешный
            exitCode = 0;
            exitStatus = QProcess::NormalExit;
        }
    }

    if ((exitCode != 0 && !isMkvmergeWarning) || exitStatus != QProcess::NormalExit)
    {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.", LogCategory::APP,
                        LogLevel::Error);
        emit workflowAborted();
        return;
    }

    switch (m_currentStep)
    {
    case Step::ExtractingAttachments:
    {
        emit logMessage("Извлечение вложений завершено.", LogCategory::APP);
        if (m_sourceFormat == SourceFormat::MP4)
        {
            extractTracksMp4();
        }
        else
        {
            extractTracks();
        }
        break;
    }
    case Step::ExtractingTracks:
    {
        emit logMessage("Извлечение дорожек завершено.", LogCategory::APP);
        // --- Шаг 4.5: Автозамены и пауза для ручной правки ---
        QString extractedSubsPath = m_paths->extractedSubs("ass");
        if (QFileInfo::exists(extractedSubsPath))
        {
            m_assProcessor->applySubstitutions(extractedSubsPath, m_template.substitutions);
            if (m_template.pauseForSubEdit)
            {
                emit logMessage("Процесс приостановлен. Ожидание ручного редактирования субтитров...",
                                LogCategory::APP);
                emit pauseForSubEditRequest(extractedSubsPath);
                return;
            }
        }
        audioPreparation();
        break;
    }
    case Step::NormalizingAudio:
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
                if (m_lastStepBeforeRequest == Step::AssemblingSrtMaster)
                {
                    m_wavForSrtMasterPath = finalPath;
                }
                else
                {
                    m_mainRuAudioPath = finalPath;
                    m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
                }
                m_wasNormalizationPerformed = true;
            }
            else
            {
                emit logMessage("ОШИБКА: Не удалось переименовать нормализованный файл.", LogCategory::APP,
                                LogLevel::Error);
                emit workflowAborted();
            }
        }
        else
        {
            emit logMessage("ОШИБКА: Нормализованный файл не найден.", LogCategory::APP, LogLevel::Error);
            emit workflowAborted();
        }
        QFile::remove(tempPath);
        QFile::remove(normalizedTempPath);
        processSubtitles();
        break;
    }
    case Step::AssemblingSrtMaster:
    {
        emit logMessage("Мастер-копия с SRT успешно собрана.", LogCategory::APP);
        convertAudioIfNeeded();
        break;
    }
    case Step::ConvertingAudio:
    {
        m_progressTimer->stop();
        QFile::remove(m_ffmpegProgressFile);
        if (m_audioConversionNeedsSecondPass)
        {
            m_audioConversionNeedsSecondPass = false;
            m_audioConversionCurrentOutputPath = m_finalAudioMp4Path;
            emit logMessage("Конвертация аудио для MKV завершена. Запуск отдельной конвертации для MP4...",
                            LogCategory::APP);

            QStringList args;
            args << "-y" << "-i" << m_mainRuAudioPath;
            if (AppSettings::instance().hasAacAtCodec())
            {
                args << "-c:a" << "aac_at" << "-b:a" << "256k";
            }
            else
            {
                args << "-c:a" << "aac" << "-b:a" << "256k";
            }
            args << "-progress" << QDir::toNativeSeparators(m_ffmpegProgressFile) << m_audioConversionCurrentOutputPath;

            m_progressTimer->disconnect();
            connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onAudioConversionProgress);
            m_progressTimer->start(500);
            m_processManager->startProcess(m_ffmpegPath, args);
            break;
        }

        emit logMessage("Конвертация аудио успешно завершена.", LogCategory::APP);
        emit progressUpdated(100);
        m_audioConversionCurrentOutputPath.clear();
        assembleMkv(m_finalAudioPath);
        break;
    }
    case Step::AssemblingMkv:
    {
        emit logMessage("Финальный MKV файл успешно собран.", LogCategory::APP);
        if (AppSettings::instance().deleteTempFiles())
        {
            emit logMessage("Удаление временных файлов...", LogCategory::APP);
            QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
            QString audioPath = m_paths->extractedAudio(m_sourceFormat == SourceFormat::MP4 ? "m4a" : "mka");
            bool videoRemoved = QFile::remove(videoPath);
            bool audioRemoved = QFile::remove(audioPath);
            if (videoRemoved)
            {
                emit logMessage(" -> Временное видео удалено: " + videoPath, LogCategory::APP);
            }
            if (audioRemoved)
            {
                emit logMessage(" -> Временное аудио удалено: " + audioPath, LogCategory::APP);
            }
        }

        emit mkvFileReady(m_finalMkvPath);
        if (m_template.useConcatRender && m_template.generateTb)
        {
            renderMp4Concat();
        }
        else
        {
            renderMp4();
        }
        break;
    }
    case Step::RenderingMp4Pass1:
    {
        if (exitCode != 0)
        {
            emit logMessage("Ошибка рендера. Рабочий процесс остановлен.", LogCategory::APP, LogLevel::Error);
            emit workflowAborted();
            return;
        }

        if (m_renderPreset.isTwoPass())
        {
            emit logMessage("Первый проход рендера успешно завершен. Запуск второго прохода.", LogCategory::APP);
            m_currentStep = Step::RenderingMp4Pass2;
            emit progressUpdated(50, "Рендер MP4 (проход 2/2)");
            runRenderPass(m_currentStep);
        }
        else
        {
            emit logMessage("Рендер MP4 успешно завершен (один проход).", LogCategory::APP);
            RenderHelper* helper = new RenderHelper(m_renderPreset, m_tempVideoMp4Path, m_processManager, this);
            connect(helper, &RenderHelper::logMessage, this, &WorkflowManager::logMessage);
            connect(helper, &RenderHelper::finished, this, &WorkflowManager::onBitrateCheckFinished);
            connect(helper, &RenderHelper::showDialogRequest, this, &WorkflowManager::bitrateCheckRequest);
            helper->startCheck();
        }
        break;
    }
    case Step::RenderingMp4Pass2:
    {
        if (exitCode != 0)
        {
            emit logMessage("Ошибка второго прохода рендера. Рабочий процесс остановлен.", LogCategory::APP,
                            LogLevel::Error);
            emit workflowAborted();
            return;
        }

        emit logMessage("Второй проход и рендер MP4 успешно завершены.", LogCategory::APP);
        RenderHelper* helper = new RenderHelper(m_renderPreset, m_tempVideoMp4Path, m_processManager, this);
        connect(helper, &RenderHelper::logMessage, this, &WorkflowManager::logMessage);
        connect(helper, &RenderHelper::finished, this, &WorkflowManager::onBitrateCheckFinished);
        connect(helper, &RenderHelper::showDialogRequest, this, &WorkflowManager::bitrateCheckRequest);
        helper->startCheck();
        break;
    }
    case Step::RenderingMp4Audio:
    {
        emit logMessage("MP4 mux: аудиопроход завершен.", LogCategory::APP);
        m_mp4AudioReady = true;
        m_currentStep = Step::MuxingMp4;
        if (!runMp4MuxWithMp4Box())
        {
            emit workflowAborted();
        }
        break;
    }
    case Step::MuxingMp4:
    {
        emit logMessage("MP4 mux через MP4Box завершен.", LogCategory::APP);
        QFile::remove(m_tempVideoMp4Path);
        QFile::remove(m_tempAudioForMp4Path);
        QFile::remove(m_tempMp4ChaptersTxtPath);
        finishWorkflow();
        break;
    }
    // ---- Concat render steps ----
    case Step::ConcatCutSegment1:
    {
        emit logMessage("Concat рендер: сегмент 1 готов.", LogCategory::APP);
        concatRenderSegment2();
        break;
    }
    case Step::ConcatRenderSegment2:
    {
        emit logMessage("Concat рендер: сегмент 2 (ТБ) готов.", LogCategory::APP);
        if (m_concatSegmentCount == 3)
        {
            concatCutSegment3();
        }
        else
        {
            concatJoinSegments();
        }
        break;
    }
    case Step::ConcatCutSegment3:
    {
        emit logMessage("Concat рендер: сегмент 3 готов.", LogCategory::APP);
        concatJoinSegments();
        break;
    }
    case Step::ConcatJoin:
    {
        concatCleanup();
        finishWorkflow();
        break;
    }
    default:
        break;
    }
}

void WorkflowManager::finishWorkflow()
{
    const qint64 durationNs =
        m_sourceDurationS > 0 ? static_cast<qint64>(static_cast<double>(m_sourceDurationS) * 1e9) : 0;
    emit chapterMarkersReady(m_chapterMarkers, durationNs);
    maybeApplyChaptersToFinalMp4();
    emit logMessage("Все шаги автоматического процесса выполнены.", LogCategory::APP);
    emit filesReady(m_finalMkvPath, m_outputMp4Path);
    // Сигнал workflowAborted теперь используется только для ошибок или принудительной отмены.
    // Для штатного завершения используем сигнал finished.
    // В MainWindow слот finishWorkerProcess обрабатывает оба.
    emit finished({}, {}, "", "");
}

void WorkflowManager::audioPreparation()
{
    m_currentStep = Step::AudioPreparation;
    emit progressUpdated(-1, "Подготовка данных");

    UserInputRequest request;
    if (m_mainRuAudioPath.isEmpty())
    {
        request.audioFileRequired = true;
    }

    bool mainAudioIsWav = m_mainRuAudioPath.endsWith(".wav", Qt::CaseInsensitive);
    if (m_template.createSrtMaster && !mainAudioIsWav && m_wavForSrtMasterPath.isEmpty())
    {
        request.audioFileRequired = true;
        request.isWavRequired = true;
    }

    QString timeForTb;
    if (m_template.useManualTime)
    {
        timeForTb = m_template.endingStartTime;
        emit logMessage("Используется время для ТБ, указанное вручную: " + timeForTb, LogCategory::APP);
    }
    else if (!m_parsedEndingTime.isEmpty())
    {
        timeForTb = m_parsedEndingTime;
        emit logMessage("Используется время для ТБ: " + timeForTb, LogCategory::APP);
    }

    if (timeForTb.isEmpty())
    {
        request.tbTimeRequired = true;
        request.tbTimeReason = "Время эндинга не определено. Укажите его вручную.";
    }
    else
    {
        QTime tbStartTime = QTime::fromString(timeForTb, "H:mm:ss.zzz");
        if (tbStartTime.isValid() && m_sourceDurationS > 0)
        {
            double tbStartTimeS = tbStartTime.hour() * 3600 + tbStartTime.minute() * 60 + tbStartTime.second() +
                                  tbStartTime.msec() / 1000.0;
            double remainingTimeS = static_cast<double>(m_sourceDurationS) - tbStartTimeS;
            int tbLines = AssProcessor::calculateTbLineCount(m_template);
            double requiredTimeS = tbLines * 3.0;

            if (remainingTimeS < requiredTimeS)
            {
                request.tbTimeRequired = true;
                request.tbTimeReason = QString("Недостаточно времени для ТБ! Требуется: ~%1 сек. Осталось: ~%2 сек.")
                                           .arg(qRound(requiredTimeS))
                                           .arg(qRound(remainingTimeS));
            }
            else
            {
                emit logMessage(QString("Проверка времени для ТБ пройдена. Требуется: ~%1 сек. Осталось: ~%2 сек.")
                                    .arg(qRound(requiredTimeS))
                                    .arg(qRound(remainingTimeS)),
                                LogCategory::APP);
            }
        }
    }

    if (request.isValid())
    {
        request.videoFilePath = m_mkvFilePath;
        request.videoDurationS = static_cast<double>(m_sourceDurationS);
        emit logMessage("Недостаточно данных. Запрос у пользователя...", LogCategory::APP);
        m_lastStepBeforeRequest = Step::AudioPreparation;
        emit progressUpdated(-1, "Запрос данных у пользователя");
        emit userInputRequired(request);
        return;
    }

    if (!m_isNormalizationEnabled || m_wasNormalizationPerformed)
    {
        processSubtitles();
        return;
    }

    QString nugenPath = AppSettings::instance().nugenAmbPath();
    if (nugenPath.isEmpty())
    {
        processSubtitles();
        return;
    }

    QString targetWavPath;
    if (!m_wavForSrtMasterPath.isEmpty())
    {
        targetWavPath = m_wavForSrtMasterPath;
        m_lastStepBeforeRequest = Step::AssemblingSrtMaster;
    }
    else if (mainAudioIsWav)
    {
        targetWavPath = m_mainRuAudioPath;
        m_lastStepBeforeRequest = Step::AudioPreparation;
    }

    if (targetWavPath.isEmpty())
    {
        processSubtitles();
        return;
    }
    emit logMessage("Шаг 5: Подготовка аудио...", LogCategory::APP);

    m_currentStep = Step::NormalizingAudio;
    QFileInfo nugenInfo(nugenPath);
    QString ambCmdPath = nugenInfo.dir().filePath("AMBCmd.exe");

    if (!QFileInfo::exists(ambCmdPath))
    {
        emit logMessage("Ошибка: AMBCmd.exe не найден. Шаг нормализации пропущен.", LogCategory::APP, LogLevel::Error);
        processSubtitles();
        return;
    }

    emit logMessage("Запуск GUI NUGEN AMB в фоновом режиме...", LogCategory::APP);
    emit progressUpdated(-1, "Запуск NUGEN Audio AMB");
    QProcess::startDetached(nugenPath);
    m_didLaunchNugen = true;

    m_originalAudioPathBeforeNormalization = targetWavPath;
    QFileInfo originalInfo(m_originalAudioPathBeforeNormalization);
    QString tempInputPath = originalInfo.dir().filePath("temp_audio_for_nugen.wav");

    if (QFile::exists(tempInputPath))
    {
        QFile::remove(tempInputPath);
    }

    if (!QFile::copy(targetWavPath, tempInputPath))
    {
        emit logMessage("ОШИБКА: Не удалось скопировать временный аудиофайл.", LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    emit logMessage("Аудиофайл переименован в temp_audio_for_nugen.wav для безопасной обработки.", LogCategory::APP);

    QTimer::singleShot(3000, this,
                       [this, ambCmdPath, tempInputPath]()
                       {
                           emit logMessage("Запуск AMBCmd для обработки файла: " + tempInputPath, LogCategory::APP);
                           m_processManager->startProcess(ambCmdPath, {"-a", tempInputPath});
                           emit progressUpdated(-1, "Нормализация аудиофайла");
                       });
}

void WorkflowManager::findFontsInProcessedSubs()
{
    m_currentStep = Step::FindingFonts;
    emit logMessage("Шаг 7: Поиск шрифтов в обработанных субтитрах...", LogCategory::APP);
    emit progressUpdated(-1, "Поиск шрифтов");

    QStringList subFilesToCheck;
    QString fullSubsPath = m_paths->processedFullSubs();
    QString signsSubsPath = m_paths->processedSignsSubs();

    if (QFileInfo::exists(fullSubsPath))
    {
        subFilesToCheck << fullSubsPath;
    }
    else if (QFileInfo::exists(signsSubsPath))
    {
        subFilesToCheck << signsSubsPath;
    }

    if (subFilesToCheck.isEmpty())
    {
        emit logMessage("Файлы субтитров для анализа не найдены. Пропускаем поиск шрифтов.", LogCategory::APP);
        QMetaObject::invokeMethod(this, "onFontFinderFinished", Qt::QueuedConnection,
                                  Q_ARG(FontFinderResult, FontFinderResult()));
    }
    else
    {
        m_fontFinder->findFontsInSubs(subFilesToCheck);
    }
}

void WorkflowManager::convertAudioIfNeeded()
{
    emit logMessage("Шаг 8: Проверка и конвертация аудио...", LogCategory::APP);

    if (m_mainRuAudioPath.isEmpty())
    {
        emit logMessage("КРИТИЧЕСКАЯ ОШИБКА: Аудиофайл отсутствует на шаге конвертации.", LogCategory::APP,
                        LogLevel::Error);
        emit workflowAborted();
        return;
    }

    QString targetFormat = m_template.targetAudioFormat;
    emit logMessage(QString("Целевой формат аудио: %1").arg(targetFormat), LogCategory::APP);

    const bool isAac = (targetFormat == "aac");

    auto alreadyInTargetFormat = [&](const QString& path)
    {
        if (isAac)
            return path.endsWith(".mka", Qt::CaseInsensitive);
        return path.endsWith("." + targetFormat, Qt::CaseInsensitive);
    };

    if (!isAac && alreadyInTargetFormat(m_mainRuAudioPath))
    {
        emit logMessage("Аудиофайл уже в целевом формате. Конвертация не требуется.", LogCategory::APP);
        m_finalAudioPath = m_mainRuAudioPath;
        m_finalAudioMp4Path.clear();
        m_audioConversionNeedsSecondPass = false;
        m_audioConversionCurrentOutputPath.clear();
        assembleMkv(m_finalAudioPath);
        return;
    }

    m_currentStep = Step::ConvertingAudio;
    // Для AAC кодируем отдельно в два контейнера:
    // - mka для mkvmerge (финальный MKV)
    // - m4a для MP4Box (финальный MP4)
    const QString outputExtension = isAac ? "mka" : targetFormat;
    m_finalAudioPath = m_paths->convertedRuAudio(outputExtension);
    m_finalAudioMp4Path = isAac ? m_paths->convertedRuAudio("m4a") : QString();
    m_audioConversionNeedsSecondPass = isAac;
    m_audioConversionCurrentOutputPath = m_finalAudioPath;
    emit logMessage(QString("Запуск конвертации в %1...").arg(targetFormat.toUpper()), LogCategory::APP);

    m_ffmpegProgressFile = QDir(m_paths->sourcesPath).filePath("ffmpeg_progress.log");

    QStringList args;
    args << "-y" << "-i" << m_mainRuAudioPath;

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
    else
    {
        emit logMessage(QString("Ошибка: неизвестный целевой формат аудио '%1'").arg(targetFormat), LogCategory::APP,
                        LogLevel::Error);
        emit workflowAborted();
        return;
    }

    args << "-progress" << QDir::toNativeSeparators(m_ffmpegProgressFile);

    args << m_audioConversionCurrentOutputPath;

    m_progressTimer->disconnect();
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onAudioConversionProgress);
    m_progressTimer->start(500);
    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::onAudioConversionProgress()
{
    QFile progressFile(m_ffmpegProgressFile);
    if (!progressFile.open(QIODevice::ReadOnly))
    {
        return;
    }

    QTextStream in(&progressFile);
    qint64 totalDurationUs = 0;
    qint64 currentTimeUs = 0;

    // Узнаем общую длительность из mkvmerge -J
    totalDurationUs = m_sourceDurationS * 1000000;

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

void WorkflowManager::assembleMkv(const QString& russianAudioPath)
{
    emit logMessage("Шаг 9: Проверка компонентов для сборки MKV...", LogCategory::APP);
    m_currentStep = Step::AssemblingMkv;

    QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
    QString originalAudioPath = m_paths->extractedAudio(m_sourceFormat == SourceFormat::MP4 ? "m4a" : "mka");
    QString fullSubsPath = m_paths->processedFullSubs();
    QString signsPath = m_paths->processedSignsSubs();

    UserInputRequest request;
    if (!m_fontResult.notFoundFontNames.isEmpty())
    {
        request.missingFonts = m_fontResult.notFoundFontNames;
    }
    if (m_mainRuAudioPath.isEmpty())
    {
        request.audioFileRequired = true;
    }

    if (request.isValid() && !m_wereFontsRequested)
    {
        emit logMessage("Недостаточно файлов для сборки MKV. Запрос у пользователя...", LogCategory::APP);
        m_lastStepBeforeRequest = Step::AssemblingMkv;
        emit progressUpdated(-1, "Запрос файлов у пользователя");
        emit userInputRequired(request);
        m_wereFontsRequested = true;
        return;
    }

    if (m_mainRuAudioPath.isEmpty())
    {
        emit logMessage("Критическая ошибка: не указан путь к русской аудиодорожке. Сборка невозможна.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    emit logMessage("Все необходимые файлы на месте. Начинаем сборку mkvmerge...", LogCategory::APP);

    QStringList fontPaths;
    for (const FoundFontInfo& fontInfo : m_fontResult.foundFonts)
    {
        fontPaths.append(fontInfo.path);
    }
    fontPaths.removeDuplicates();

    if (!m_fontResult.notFoundFontNames.isEmpty())
    {
        emit logMessage("ПРЕДУПРЕЖДЕНИЕ: Не все шрифты были предоставлены. Субтитры могут отображаться некорректно.",
                        LogCategory::APP, LogLevel::Warning);
        emit logMessage("Пропущены: " + m_fontResult.notFoundFontNames.join(", "), LogCategory::APP);
    }

    m_wereFontsRequested = false;

    QString outputFileName = PathManager::sanitizeForPath(
        QString("[DUB] %1 - %2.mkv").arg(m_template.seriesTitle).arg(m_episodeNumberForSearch));
    m_finalMkvPath = QDir(m_paths->resultPath).absoluteFilePath(outputFileName);

    QStringList args;
    args << "-o" << m_finalMkvPath;

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

    if (!m_chaptersMuxPathForMkv.isEmpty() && QFileInfo::exists(m_chaptersMuxPathForMkv))
    {
        args << "--chapters" << m_chaptersMuxPathForMkv;
        emit logMessage(QStringLiteral("mkvmerge: добавлены главы (%1).").arg(m_chaptersMuxPathForMkv),
                        LogCategory::APP);
    }

    // Названия из шаблона
    QString animStudio = m_template.animationStudio;
    QString subAuthor = m_template.subAuthor;

    // Дорожка видео
    args << "--language" << "0:" + m_template.originalLanguage << "--track-name"
         << QString("0:Видеоряд [%1]").arg(animStudio) << videoPath;

    // Дорожка русского аудио (с флагом по умолчанию)
    args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]"
         << russianAudioPath;

    // Дорожка оригинального аудио
    if (QFileInfo::exists(originalAudioPath))
    {
        args << "--default-track-flag" << "0:no" << "--language" << "0:" + m_template.originalLanguage << "--track-name"
             << QString("0:Оригинал [%1]").arg(animStudio) << originalAudioPath;
    }
    else if (m_template.useOriginalAudio)
    {
        emit logMessage("Критическая ошибка: Отсутствует оригинальная аудиодорожка, проверьте язык оригинального аудио "
                        "в шаблоне. Сборка остановлена.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    QString subTrackAuthorName = m_template.isCustomTranslation ? "Дубляжная" : subAuthor;

    // Дорожки субтитров (если они существуют)
    if (QFileInfo::exists(signsPath))
    {
        args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus"
             << "--track-name" << QString("0:Надписи [%1]").arg(subTrackAuthorName) << signsPath;
    }
    if (QFileInfo::exists(fullSubsPath))
    {
        args << "--default-track-flag" << "0:no" << "--forced-display-flag" << "0:no" << "--language" << "0:rus"
             << "--track-name" << QString("0:Субтитры [%1]").arg(subTrackAuthorName) << fullSubsPath;
    }

    emit progressUpdated(-1, "Сборка MKV");
    m_processManager->startProcess(m_mkvmergePath, args);
}

void WorkflowManager::renderMp4()
{
    emit logMessage("Шаг 10: Рендер финального MP4 файла...", LogCategory::APP);
    emit progressUpdated(-1, "Рендер MP4");
    m_renderPreset = AppSettings::instance().findRenderPreset(m_template.renderPresetName);
    if (m_renderPreset.name.isEmpty())
    {
        emit logMessage("Критическая ошибка: пресет рендера '" + m_template.renderPresetName +
                            "' не найден. Процесс остановлен.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    m_outputMp4Path = m_finalMkvPath;
    m_outputMp4Path.replace(".mkv", ".mp4");
    m_tempVideoMp4Path = QDir(QFileInfo(m_outputMp4Path).absolutePath())
                             .filePath(QFileInfo(m_outputMp4Path).completeBaseName() + "_temp_video.mp4");
    m_tempAudioForMp4Path = QDir(QFileInfo(m_outputMp4Path).absolutePath())
                                .filePath(QFileInfo(m_outputMp4Path).completeBaseName() + "_temp_audio.m4a");
    m_tempMp4ChaptersTxtPath = QDir(QFileInfo(m_outputMp4Path).absolutePath())
                                   .filePath(QFileInfo(m_outputMp4Path).completeBaseName() + "_auto_chapters.txt");

    QFile::remove(m_tempVideoMp4Path);
    QFile::remove(m_tempAudioForMp4Path);
    QFile::remove(m_tempMp4ChaptersTxtPath);
    m_useExternalAudioForMp4Mux = false;
    m_mp4ChaptersEmbeddedInMux = false;
    m_mp4VideoReady = false;
    m_mp4AudioReady = false;
    m_renderAudioArgs.clear();

    m_currentStep = Step::RenderingMp4Pass1;
    runRenderPass(m_currentStep);
}

void WorkflowManager::runRenderPass(Step pass)
{
    QString commandTemplate =
        (pass == Step::RenderingMp4Pass1) ? m_renderPreset.commandPass1 : m_renderPreset.commandPass2;
    QStringList videoArgs;
    QStringList audioArgs;
    if (!prepareSplitRenderArgs(commandTemplate, m_tempVideoMp4Path, videoArgs, audioArgs))
    {
        emit logMessage("Ошибка: не удалось подготовить команду для рендера.", LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    const bool isFinalVideoPass = (!m_renderPreset.isTwoPass() && pass == Step::RenderingMp4Pass1) ||
                                  (m_renderPreset.isTwoPass() && pass == Step::RenderingMp4Pass2);
    if (isFinalVideoPass)
    {
        m_renderAudioArgs = audioArgs;
    }

    m_processManager->startProcess(m_ffmpegPath, videoArgs);
}

bool WorkflowManager::prepareSplitRenderArgs(const QString& commandTemplate, const QString& outputVideoPath,
                                             QStringList& outVideoArgs, QStringList& outAudioArgs)
{
    QString processedTemplate = commandTemplate;
    const QString inputMkv = QFileInfo(m_finalMkvPath).absoluteFilePath();
    const QString outputMp4 = QFileInfo(m_outputMp4Path).absoluteFilePath();

    processedTemplate.replace("%INPUT%", inputMkv);
    processedTemplate.replace("%OUTPUT%", outputMp4);

    const bool useHardsub = QFileInfo::exists(m_paths->processedSignsSubs());
    if (useHardsub)
    {
        QFileInfo subsInfo(m_paths->processedSignsSubs());
        m_processManager->setWorkingDirectory(subsInfo.absolutePath());
        const QString escapedFileName = subsInfo.fileName().replace("'", "\\'");
        processedTemplate.replace("%SIGNS%", QString("filename='%1'").arg(escapedFileName));
    }
    else
    {
        processedTemplate.replace("subtitles=%SIGNS%", "");
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"[^\"]*,\s*\")"));
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*,\s*[^\"]*\")"));
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*\")"));
        emit logMessage("Hardsub отключен. Фильтр субтитров удален из команды.", LogCategory::APP);
    }

    QStringList rawArgs = QProcess::splitCommand(processedTemplate);
    if (!rawArgs.isEmpty() && rawArgs.first().contains("ffmpeg"))
    {
        rawArgs.removeFirst();
    }
    if (rawArgs.isEmpty())
    {
        return false;
    }

    outVideoArgs.clear();
    outAudioArgs.clear();
    outAudioArgs << "-y" << "-hide_banner" << "-i" << inputMkv << "-vn";

    bool hasAudioMap = false;
    for (int i = 0; i < rawArgs.size(); ++i)
    {
        const QString arg = rawArgs.at(i);

        if (arg == "-map" && i + 1 < rawArgs.size())
        {
            const QString mapVal = rawArgs.at(i + 1);
            if (mapVal.contains(":a"))
            {
                outAudioArgs << arg << mapVal;
                hasAudioMap = true;
            }
            else
            {
                outVideoArgs << arg << mapVal;
            }
            ++i;
            continue;
        }

        if (arg == "-c:a" || arg == "-codec:a" || arg == "-b:a" || arg == "-audio_bitrate" || arg == "-ac" ||
            arg == "-ar" || arg == "-af")
        {
            outAudioArgs << arg;
            if (i + 1 < rawArgs.size())
            {
                outAudioArgs << rawArgs.at(i + 1);
                ++i;
            }
            continue;
        }

        if (arg == "-map_metadata" || arg == "-map_chapters")
        {
            if (i + 1 < rawArgs.size())
            {
                ++i;
            }
            continue;
        }

        if (arg == outputMp4)
        {
            outVideoArgs << outputVideoPath;
            continue;
        }

        if (arg == "-an")
        {
            continue;
        }

        outVideoArgs << arg;
    }

    if (outVideoArgs.isEmpty())
    {
        return false;
    }

    const qsizetype videoOutIdx = outVideoArgs.size() - 1;
    if (!outVideoArgs.contains("-an") && videoOutIdx >= 0)
    {
        outVideoArgs.insert(videoOutIdx, "-an");
    }

    if (!hasAudioMap)
    {
        outAudioArgs << "-map" << "0:a:m:language:rus";
    }

    outAudioArgs << m_tempAudioForMp4Path;
    return true;
}

void WorkflowManager::startMp4MuxPipeline()
{
    const QString finalAudio = QFileInfo(m_finalAudioMp4Path).absoluteFilePath();
    const bool canUseExternalAudio =
        QFileInfo::exists(finalAudio) &&
        (finalAudio.endsWith(".m4a", Qt::CaseInsensitive) || finalAudio.endsWith(".aac", Qt::CaseInsensitive));

    if (m_template.targetAudioFormat == "aac" && canUseExternalAudio)
    {
        m_useExternalAudioForMp4Mux = true;
        m_mp4AudioReady = true;
        emit logMessage("MP4 mux: используем отдельно подготовленное m4a-аудио.", LogCategory::APP);
    }
    else
    {
        m_useExternalAudioForMp4Mux = false;
        m_mp4AudioReady = false;
    }

    if (!m_mp4AudioReady)
    {
        if (m_renderAudioArgs.isEmpty())
        {
            emit logMessage("MP4 mux: не удалось подготовить аудиопроход из пресета.", LogCategory::APP,
                            LogLevel::Error);
            emit workflowAborted();
            return;
        }

        m_currentStep = Step::RenderingMp4Audio;
        emit progressUpdated(-1, "MP4: подготовка аудио");
        emit logMessage("MP4 mux: запуск отдельного аудиопрохода для .m4a.", LogCategory::APP);
        m_processManager->startProcess(m_ffmpegPath, m_renderAudioArgs);
        return;
    }

    m_currentStep = Step::MuxingMp4;
    if (!runMp4MuxWithMp4Box())
    {
        emit workflowAborted();
    }
}

bool WorkflowManager::runMp4MuxWithMp4Box()
{
    const QString mp4boxPath = AppSettings::instance().mp4boxPath();
    if (mp4boxPath.isEmpty() || !QFileInfo::exists(mp4boxPath))
    {
        emit logMessage("MP4Box не найден. Проверьте путь в настройках.", LogCategory::APP, LogLevel::Error);
        return false;
    }
    if (!QFileInfo::exists(m_tempVideoMp4Path))
    {
        emit logMessage("MP4 mux: временный видеофайл не найден.", LogCategory::APP, LogLevel::Error);
        return false;
    }

    const QString audioPath = m_useExternalAudioForMp4Mux ? QFileInfo(m_finalAudioMp4Path).absoluteFilePath()
                                                          : QFileInfo(m_tempAudioForMp4Path).absoluteFilePath();
    if (!QFileInfo::exists(audioPath))
    {
        emit logMessage("MP4 mux: аудиофайл для мукса не найден.", LogCategory::APP, LogLevel::Error);
        return false;
    }

    QStringList args;
    args << "-add" << QString("%1#video").arg(QDir::toNativeSeparators(m_tempVideoMp4Path)) << "-add"
         << QString("%1#audio").arg(QDir::toNativeSeparators(audioPath));

    m_mp4ChaptersEmbeddedInMux = false;
    if (!m_chapterMarkers.isEmpty() && ChapterHelper::writeOgmChapterText(m_chapterMarkers, m_tempMp4ChaptersTxtPath))
    {
        args << "-chap" << QDir::toNativeSeparators(m_tempMp4ChaptersTxtPath);
        m_mp4ChaptersEmbeddedInMux = true;
        emit logMessage("MP4 mux: главы будут вшиты через MP4Box.", LogCategory::APP);
    }
    args << "-new" << QDir::toNativeSeparators(m_outputMp4Path);

    emit progressUpdated(95, "MP4: mux через MP4Box");
    emit logMessage("MP4 mux: сборка финального MP4 через MP4Box...", LogCategory::APP);
    m_processManager->startProcess(mp4boxPath, args);
    return true;
}

QStringList WorkflowManager::prepareCommandArguments(const QString& commandTemplate)
{
    QString processedTemplate = commandTemplate;
    QString inputMkv = QFileInfo(m_finalMkvPath).absoluteFilePath();
    QString outputMp4 = QFileInfo(m_outputMp4Path).absoluteFilePath();

    processedTemplate.replace("%INPUT%", inputMkv);
    processedTemplate.replace("%OUTPUT%", outputMp4);

    bool useHardsub = QFileInfo::exists(m_paths->processedSignsSubs());
    if (useHardsub)
    {
        QFileInfo subsInfo(m_paths->processedSignsSubs());
        m_processManager->setWorkingDirectory(subsInfo.absolutePath());
        QString escapedFileName = subsInfo.fileName().replace("'", "\\'");
        processedTemplate.replace("%SIGNS%", QString("filename='%1'").arg(escapedFileName));
    }
    else
    {
        // Если hardsub не нужен, надо аккуратно удалить фильтр
        processedTemplate.replace("subtitles=%SIGNS%", "");
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"[^\"]*,\s*\")"));    // запятая после
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*,\s*[^\"]*\")")); // запятая перед
        processedTemplate.remove(QRegularExpression(R"(-vf\s+\"\s*\")"));           // пустые кавычки
        emit logMessage("Hardsub отключен. Фильтр субтитров удален из команды.", LogCategory::APP);
    }

    QStringList args = QProcess::splitCommand(processedTemplate);
    // Do not copy chapters from source MKV into the intermediate MP4; chapters are written later via ffmetadata
    // (otherwise MediaInfo shows two chapter sets with conflicting times).
    if (!m_chapterMarkers.isEmpty() && !m_skipChaptersForWorkflow)
    {
        const qsizetype outIdx = args.size() - 1;
        if (outIdx >= 0)
        {
            args.insert(outIdx, QStringLiteral("-1"));
            args.insert(outIdx, QStringLiteral("-map_chapters"));
        }
    }

    // Preset commands may contain "-c:a copy" (or "-c copy"), which breaks AAC
    // priming metadata when remuxing to MP4. For AAC target we force encoding
    // from the original WAV directly in the final MP4 pass.
    if (m_template.targetAudioFormat == QLatin1String("aac") &&
        m_mainRuAudioPath.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive))
    {
        enforceAacFromWavForPresetArgs(args, QFileInfo(m_mainRuAudioPath).absoluteFilePath());
    }
    return args;
}

// ==================== Smart Concat Render ====================

QString WorkflowManager::concatEncoderForCodec(const QString& codecExtension)
{
    if (codecExtension == "h264")
    {
        return "libx264";
    }
    if (codecExtension == "h265")
    {
        return "libx265";
    }
    return "";
}

void WorkflowManager::renderMp4Concat()
{
    emit logMessage("Шаг 10: Умный рендер MP4 (concat) — перекодирование только ТБ...", LogCategory::APP);
    emit progressUpdated(-1, "Concat рендер");

    // Determine output path
    m_outputMp4Path = m_finalMkvPath;
    m_outputMp4Path.replace(".mkv", ".mp4");

    // Validate that we have a supported video codec for re-encoding
    QString encoder = concatEncoderForCodec(m_videoTrack.extension);
    if (encoder.isEmpty())
    {
        emit logMessage("Concat рендер: неподдерживаемый кодек '" + m_videoTrack.extension +
                            "'. Переключение на полный рендер.",
                        LogCategory::APP);
        renderMp4();
        return;
    }

    // Calculate TB start/end in seconds
    QString timeForTb = m_parsedEndingTime;
    if (m_template.useManualTime && !m_template.endingStartTime.isEmpty())
    {
        timeForTb = m_template.endingStartTime;
    }

    QTime tbStartTime = QTime::fromString(timeForTb, "H:mm:ss.zzz");
    if (!tbStartTime.isValid())
    {
        emit logMessage("Concat рендер: некорректное время начала ТБ '" + timeForTb +
                            "'. Переключение на полный рендер.",
                        LogCategory::APP);
        renderMp4();
        return;
    }

    m_concatTbStartSeconds = (tbStartTime.hour() * 3600.0) + (tbStartTime.minute() * 60.0) + tbStartTime.second() +
                             (tbStartTime.msec() / 1000.0);

    int tbLines = AssProcessor::calculateTbLineCount(m_template);
    double tbDurationSeconds = tbLines * 3.0;
    m_concatTbEndSeconds = m_concatTbStartSeconds + tbDurationSeconds;

    emit logMessage(QString("Concat рендер: ТБ начало=%1с, длительность=%2с (%3 строк), конец=%4с, видео=%5с")
                        .arg(m_concatTbStartSeconds, 0, 'f', 3)
                        .arg(tbDurationSeconds, 0, 'f', 1)
                        .arg(tbLines)
                        .arg(m_concatTbEndSeconds, 0, 'f', 3)
                        .arg(m_sourceDurationS),
                    LogCategory::APP);

    // Check if there is content after TB
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

    // Always search for keyframes: we need the keyframe before TB start
    // for a clean segment 1→2 boundary, and (in 3-segment mode) the keyframe
    // after TB end for the segment 2→3 boundary.
    if (m_concatSegmentCount == 2)
    {
        m_concatKeyframeTime = static_cast<double>(m_sourceDurationS);
    }
    concatFindKeyframe();
}

void WorkflowManager::concatFindKeyframe()
{
    emit logMessage("Concat рендер: поиск keyframe-ов для границ сегментов...", LogCategory::APP);
    m_currentStep = Step::ConcatFindKeyframe;

    QString ffprobePath = AppSettings::instance().ffprobePath();
    if (ffprobePath.isEmpty() || !QFileInfo::exists(ffprobePath))
    {
        emit logMessage("Concat рендер: ffprobe не найден. Переключение на полный рендер.", LogCategory::APP);
        renderMp4();
        return;
    }

    // --- 1. Find the last keyframe AT or BEFORE TB start (for seg1→seg2 boundary) ---
    {
        double searchStart = m_concatTbStartSeconds > 15.0 ? m_concatTbStartSeconds - 15.0 : 0.0;
        QString readInterval =
            QString("%1%%2").arg(searchStart, 0, 'f', 3).arg(m_concatTbStartSeconds + 0.001, 0, 'f', 3);

        QStringList args;
        args << "-v" << "quiet"
             << "-select_streams" << "v:0"
             << "-show_entries" << "frame=pts_time,key_frame"
             << "-read_intervals" << readInterval << "-of" << "json" << m_finalMkvPath;

        QByteArray output;
        bool success = m_processManager->executeAndWait(ffprobePath, args, output);

        if (!success || output.isEmpty())
        {
            emit logMessage("Concat рендер: не удалось найти keyframe перед ТБ. Переключение на полный рендер.",
                            LogCategory::APP);
            renderMp4();
            return;
        }

        // Pick the LAST keyframe with pts_time <= tbStart
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
                    // Don't break — keep iterating to find the LAST one
                }
            }
        }

        if (!foundBefore)
        {
            // Fallback: use 0 (very beginning) — practically unreachable
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

    // --- 2. Find the first keyframe AT or AFTER TB end (for seg2→seg3 boundary) ---
    if (m_concatSegmentCount == 3)
    {
        QString readInterval =
            QString("%1%%2").arg(m_concatTbEndSeconds, 0, 'f', 3).arg(m_concatTbEndSeconds + 10.0, 0, 'f', 3);

        QStringList args;
        args << "-v" << "quiet"
             << "-select_streams" << "v:0"
             << "-show_entries" << "frame=pts_time,key_frame"
             << "-read_intervals" << readInterval << "-of" << "json" << m_finalMkvPath;

        QByteArray output;
        bool success = m_processManager->executeAndWait(ffprobePath, args, output);

        if (!success || output.isEmpty())
        {
            emit logMessage(
                "Concat рендер: не удалось получить данные о keyframe после ТБ. Переключение на полный рендер.",
                LogCategory::APP);
            renderMp4();
            return;
        }

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
            emit logMessage("Concat рендер: keyframe после ТБ не найден. Переключение в 2-сегментный режим.",
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

    concatCutSegment1();
}

void WorkflowManager::concatCutSegment1()
{
    emit logMessage("Concat рендер: вырезка сегмента 1 (до ТБ, копирование)...", LogCategory::APP);
    m_currentStep = Step::ConcatCutSegment1;
    emit progressUpdated(-1, "Concat: сегмент 1/3");

    // Cut segment 1 at the keyframe BEFORE TB start so that -c copy produces
    // a clean cut without B-frame overlap at the seg1→seg2 boundary.
    QString seg1Path = QDir(m_paths->resultPath).filePath("concat_seg1.ts");

    QString seg1EndStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);

    QStringList args;
    args << "-y";

    // Video-only segment: audio will be taken from the continuous MKV track
    // in the final join step, eliminating AAC splicing artefacts entirely.
    const bool sourceIsMp4 = (m_sourceFormat == SourceFormat::MP4);
    if (sourceIsMp4)
    {
        args << "-i" << m_mkvFilePath // original MP4 — video with correct DTS
             << "-to" << seg1EndStr << "-map" << "0:v:0"
             << "-c:v" << "copy"
             << "-an"
             << "-avoid_negative_ts" << "make_non_negative";
    }
    else
    {
        args << "-i" << m_finalMkvPath << "-to" << seg1EndStr << "-map" << "0:v:0"
             << "-c:v" << "copy"
             << "-an"
             << "-avoid_negative_ts" << "make_non_negative";
    }

    // Prevent TS muxer from adding initial buffering delays
    args << "-muxdelay" << "0" << "-muxpreload" << "0";
    args << seg1Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::concatRenderSegment2()
{
    emit logMessage("Concat рендер: перекодирование сегмента 2 (ТБ с хардсабом)...", LogCategory::APP);
    m_currentStep = Step::ConcatRenderSegment2;
    emit progressUpdated(-1, "Concat: рендер ТБ");

    QString seg2Path = QDir(m_paths->resultPath).filePath("concat_seg2.ts");
    QString encoder = concatEncoderForCodec(m_videoTrack.extension);

    // --- Probe segment 1 to find its actual end time (B-frame tail) ---
    // With -c copy, B-frame packets whose DTS <= keyframe but PTS > keyframe
    // end up in segment 1, creating a content overlap with segment 2.
    // We probe the actual duration and skip those frames via trim filter.
    double bframeOverlap = 0.0;
    double overlapUsed = 0.0;
    {
        QString seg1Path = QDir(m_paths->resultPath).filePath("concat_seg1.ts");
        QString ffprobePath = AppSettings::instance().ffprobePath();
        const QJsonObject seg1Stats = probeTsVideoStats(m_processManager, ffprobePath, seg1Path);
        const double seg1EndTime = effectiveTsDuration(seg1Stats);
        if (seg1EndTime > m_concatKfBeforeTbStart)
        {
            bframeOverlap = seg1EndTime - m_concatKfBeforeTbStart;
            emit logMessage(QString("Concat рендер: сег.1 фактический конец %1с, B-frame хвост %2с — "
                                    "корректируем старт сег.2")
                                .arg(seg1EndTime, 0, 'f', 3)
                                .arg(bframeOverlap, 0, 'f', 3),
                            LogCategory::APP);
        }
    }
    overlapUsed = bframeOverlap;

    if (m_sourceFormat == SourceFormat::MP4)
    {
        const QString probeVf = QString("trim=start=%1,setpts=PTS-STARTPTS").arg(bframeOverlap, 0, 'f', 3);
        const QString seg2HeadBaseMd5 = probeFilteredFirstFrameMd5(m_processManager, m_ffmpegPath, m_finalMkvPath,
                                                                   m_concatKfBeforeTbStart, 1.000, probeVf);
        const QList<double> seamOffsets = {-0.20, -0.16, -0.12, -0.08, -0.04, 0.00, 0.04, 0.08, 0.12, 0.16, 0.20};
        double seg2HeadSourceTs = -1.0;
        for (const double offset : seamOffsets)
        {
            const double ts = qMax(0.0, m_concatTbStartSeconds + offset);
            const QString md5 = probeFirstFrameMd5(m_processManager, m_ffmpegPath, m_mkvFilePath, ts);
            const bool matches = (!seg2HeadBaseMd5.isEmpty() && md5 == seg2HeadBaseMd5);
            if (matches && seg2HeadSourceTs < 0.0)
            {
                seg2HeadSourceTs = ts;
            }
        }

        const QString seg1Path = QDir(m_paths->resultPath).filePath("concat_seg1.ts");
        const QString seg1TailLastMd5_1s =
            probeLastFrameMd5FromTailWindow(m_processManager, m_ffmpegPath, seg1Path, -1.000);
        double seg1TailSourceTs = -1.0;
        if (!seg1TailLastMd5_1s.isEmpty())
        {
            for (const double offset : seamOffsets)
            {
                const double ts = qMax(0.0, m_concatTbStartSeconds + offset);
                const QString md5 = probeFirstFrameMd5(m_processManager, m_ffmpegPath, m_mkvFilePath, ts);
                const bool matches = (md5 == seg1TailLastMd5_1s);
                if (matches && seg1TailSourceTs < 0.0)
                {
                    seg1TailSourceTs = ts;
                }
            }
        }

        const double frameStep = frameStepSecondsForFps(m_videoTrack.frameRate);
        const double tailToHead =
            (seg1TailSourceTs >= 0.0 && seg2HeadSourceTs >= 0.0) ? (seg2HeadSourceTs - seg1TailSourceTs) : -1.0;
        if (frameStep > 0.0 && tailToHead > 0.0)
        {
            const double oneFrameDelta = tailToHead - frameStep;
            if (qAbs(oneFrameDelta) <= 0.012)
            {
                overlapUsed = qMax(0.0, overlapUsed - frameStep);
            }
        }
    }

    // Fast seek to the keyframe before TB start.
    // -t before -i limits input duration (avoids reading entire file).
    QString seg2StartStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);

    QStringList args;
    args << "-y"
         << "-ss" << seg2StartStr;

    // Input duration: exact distance from keyframe-before-TB to keyframe-after-TB.
    // No margin — the encoder produces the exact number of frames needed, and
    // -shortest (below) ensures audio matches video duration precisely.
    if (m_concatSegmentCount == 3)
    {
        double segInputDuration = m_concatKeyframeTime - m_concatKfBeforeTbStart;
        args << "-t" << QString::number(segInputDuration, 'f', 3);
        emit logMessage(QString("Concat рендер: input duration сег.2 = %1с (keyframe %2 - start %3)")
                            .arg(segInputDuration, 0, 'f', 3)
                            .arg(m_concatKeyframeTime, 0, 'f', 3)
                            .arg(m_concatKfBeforeTbStart, 0, 'f', 3),
                        LogCategory::APP);
    }

    args << "-i" << m_finalMkvPath;

    // Build the video filter chain:
    // 1. trim overlap first (if needed), then reset PTS
    // 2. shift to subtitle timeline anchor, burn subtitles, reset PTS
    // Keeping trim before subtitles prevents cutting the initial fade-in.
    bool useHardsub = QFileInfo::exists(m_paths->processedSignsSubs());
    QStringList vfParts;
    if (overlapUsed > 0.001)
    {
        vfParts << QString("trim=start=%1").arg(overlapUsed, 0, 'f', 3);
        vfParts << "setpts=PTS-STARTPTS";
    }
    if (useHardsub)
    {
        const QString signsPath = "'" + escapePathForFfmpegFilter(m_paths->processedSignsSubs()) + "'";
        // Keep subtitle timeline anchored to TB start when overlap trim would
        // otherwise cut into the first subtitle fade window.
        const double subtitleTimelineStart = qMin(m_concatKfBeforeTbStart + overlapUsed, m_concatTbStartSeconds);
        vfParts << QString("setpts=PTS+%1/TB").arg(QString::number(subtitleTimelineStart, 'f', 3));
        vfParts << QString("subtitles=%1").arg(signsPath);
        vfParts << "setpts=PTS-STARTPTS";
    }
    if (!vfParts.isEmpty())
    {
        args << "-vf" << vfParts.join(",");
    }

    // Video-only segment: audio will be taken from the continuous MKV track
    // in the final join step, eliminating AAC splicing artefacts entirely.
    args << "-an";

    args << "-c:v" << encoder;

    // Quality settings: use CRF for consistent quality on short segments.
    // Pure ABR (-b:v) without VBV causes catastrophic rate control failure
    // on 30-second segments: I-frames consume the entire bit budget, forcing
    // QP to maximum (51) for all remaining frames, resulting in ~150 kbps
    // instead of the target bitrate.
    // CRF with maxrate/bufsize gives consistent quality AND prevents
    // the segment from exceeding the source bitrate.
    if (m_videoTrack.bitrateKbps > 0)
    {
        QString maxrateStr = QString::number(m_videoTrack.bitrateKbps) + "k";
        QString bufsizeStr = QString::number(m_videoTrack.bitrateKbps * 2) + "k";

        if (encoder == "libx264")
        {
            args << "-crf" << "18" << "-maxrate" << maxrateStr << "-bufsize" << bufsizeStr << "-preset" << "medium"
                 << "-profile:v" << "high";
        }
        else if (encoder == "libx265")
        {
            args << "-crf" << "18" << "-maxrate" << maxrateStr << "-bufsize" << bufsizeStr << "-preset" << "medium"
                 << "-tag:v" << "hvc1";
        }
        emit logMessage(QString("Concat рендер: кодируем ТБ с CRF + maxrate оригинала: %1").arg(maxrateStr),
                        LogCategory::APP);
    }
    else
    {
        emit logMessage("Concat рендер: битрейт оригинала неизвестен, используем CRF.", LogCategory::APP);
        if (encoder == "libx264")
        {
            args << "-crf" << "18" << "-preset" << "medium" << "-profile:v" << "high";
        }
        else if (encoder == "libx265")
        {
            args << "-crf" << "18" << "-preset" << "medium" << "-tag:v" << "hvc1";
        }
    }

    args << "-map" << "0:v:0";

    // Prevent TS muxer from adding initial buffering delays
    args << "-muxdelay" << "0" << "-muxpreload" << "0";
    args << seg2Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::concatCutSegment3()
{
    emit logMessage("Concat рендер: вырезка сегмента 3 (после ТБ, копирование)...", LogCategory::APP);
    m_currentStep = Step::ConcatCutSegment3;
    emit progressUpdated(-1, "Concat: сегмент 3/3");

    bool sourceIsMp4 = (m_sourceFormat == SourceFormat::MP4);
    QString seg3Path = QDir(m_paths->resultPath).filePath("concat_seg3.ts");
    double seg3Start = m_concatKeyframeTime;

    // Symmetric seam correction for seg2->seg3:
    // if cumulative seg1+seg2 duration exceeds keyframe boundary due timestamp rounding
    // or reordering tail, shift seg3 start forward to avoid backward visual jump.
    const QString ffprobePath = AppSettings::instance().ffprobePath();
    if (!ffprobePath.isEmpty() && QFileInfo::exists(ffprobePath))
    {
        const QString seg1Path = QDir(m_paths->resultPath).filePath("concat_seg1.ts");
        const QString seg2Path = QDir(m_paths->resultPath).filePath("concat_seg2.ts");
        const double seg1EndTime = probeTsVideoEndTime(m_processManager, ffprobePath, seg1Path);
        const double seg2EndTime = probeTsVideoEndTime(m_processManager, ffprobePath, seg2Path);
        if (seg1EndTime > 0.0 && seg2EndTime > 0.0)
        {
            const double cumulativeSeam = seg1EndTime + seg2EndTime;
            const double delta = cumulativeSeam - m_concatKeyframeTime;
            if (delta > 0.001 && delta < 1.0)
            {
                seg3Start = cumulativeSeam;
                emit logMessage(
                    QString(
                        "Concat рендер: корректируем старт сег.3 по длительности seg1+seg2: %1с -> %2с (дельта %3с)")
                        .arg(m_concatKeyframeTime, 0, 'f', 3)
                        .arg(seg3Start, 0, 'f', 3)
                        .arg(delta, 0, 'f', 3),
                    LogCategory::APP);
            }
        }
    }

    QString kfTimeStr = QString::number(seg3Start, 'f', 3);

    QStringList args;
    args << "-y";

    // Video-only segment: audio will be taken from the continuous MKV track
    // in the final join step, eliminating AAC splicing artefacts entirely.
    if (sourceIsMp4)
    {
        args << "-ss" << kfTimeStr << "-i" << m_mkvFilePath // original MP4 — video only
             << "-map" << "0:v:0"
             << "-c:v" << "copy"
             << "-an"
             << "-avoid_negative_ts" << "make_non_negative";
    }
    else
    {
        args << "-ss" << kfTimeStr << "-i" << m_finalMkvPath << "-map" << "0:v:0"
             << "-c:v" << "copy"
             << "-an"
             << "-avoid_negative_ts" << "make_non_negative";
    }

    // Prevent TS muxer from adding initial buffering delays
    args << "-muxdelay" << "0" << "-muxpreload" << "0";
    args << seg3Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::concatJoinSegments()
{
    emit logMessage("Concat рендер: склейка сегментов...", LogCategory::APP);
    m_currentStep = Step::ConcatJoin;
    emit progressUpdated(-1, "Concat: склейка");

    // Write concat list file for the video-only TS segments.
    QString listPath = QDir(m_paths->resultPath).filePath("concat_list.txt");
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit logMessage("Concat рендер: не удалось создать файл списка. Переключение на полный рендер.",
                        LogCategory::APP);
        renderMp4();
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

    // Concat demuxer provides concatenated video from segments (input 0).
    // Audio source depends on codec: for AAC we encode from the original WAV
    // (m_mainRuAudioPath) directly into the MP4 so that ffmpeg creates a
    // correct Edit List (edts) with the encoder delay.  Copying AAC from
    // the MKV resets media_time to 0 and introduces ~44 ms lip-sync drift.
    // For non-AAC codecs (FLAC, etc.) we stream-copy from the assembled MKV
    // because there is no encoder delay to preserve.
    const bool audioIsAac = m_template.targetAudioFormat == "aac";
    const QString audioInputPath = audioIsAac ? m_mainRuAudioPath : m_finalMkvPath;

    QStringList args;
    args << "-y"
         << "-f" << "concat"
         << "-safe" << "0"
         << "-i" << QFileInfo(listPath).absoluteFilePath() << "-i" << audioInputPath << "-map" << "0:v:0"
         << "-map" << "1:a:0"
         << "-c:v" << "copy";

    if (audioIsAac)
    {
        args << "-c:a" << "aac_at" << "-b:a" << "256k";
    }
    else
    {
        args << "-c:a" << "copy";
    }

    // Full PTS+DTS normalization (same as concattbrenderer): MediaInfo reports CFR;
    // dts-only setts was tried (H28) but restored VFR-like metadata / worse UX.
    const QString settsExpr = m_videoTrack.isCfr ? buildSettsExprForFps(m_videoTrack.frameRate) : "";
    const bool applySetts = !settsExpr.isEmpty();
    if (applySetts)
    {
        args << "-bsf:v" << QString("setts=%1").arg(settsExpr);
        emit logMessage(QString("Concat рендер: включен setts для CFR (%1, avg %2).")
                            .arg(m_videoTrack.frameRate, m_videoTrack.avgFrameRate),
                        LogCategory::APP);
    }
    else
    {
        emit logMessage(QString("Concat рендер: setts отключен (fps=%1, avg=%2, CFR=%3).")
                            .arg(m_videoTrack.frameRate.isEmpty() ? "n/a" : m_videoTrack.frameRate)
                            .arg(m_videoTrack.avgFrameRate.isEmpty() ? "n/a" : m_videoTrack.avgFrameRate)
                            .arg(m_videoTrack.isCfr ? "yes" : "no"),
                        LogCategory::APP);
    }

    args << "-movflags" << "+faststart"
         << "-shortest" << m_outputMp4Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::concatExtractH264()
{
    // Use mkvmerge to force CFR instead of raw H.264 extraction.
    // mkvmerge preserves B-frame PTS ordering and fixes Non-monotonic DTS
    // errors at segment boundaries by forcing each frame to exactly 1/fps duration.
    emit logMessage("Concat рендер: принудительное CFR через mkvmerge...", LogCategory::APP);
    m_currentStep = Step::ConcatExtract;
    emit progressUpdated(-1, "Concat: CFR ремукс");

    QString tempMp4Path = QDir(m_paths->resultPath).filePath("concat_temp.mp4");
    QString tempMkvPath = QDir(m_paths->resultPath).filePath("concat_cfr.mkv");

    QString fps = m_videoTrack.frameRate.isEmpty() ? "25" : m_videoTrack.frameRate;

    QStringList args;
    args << "-o" << tempMkvPath << "--default-duration" << QString("0:%1p").arg(fps) << tempMp4Path;

    m_processManager->startProcess(m_mkvmergePath, args);
}

void WorkflowManager::concatRemux()
{
    emit logMessage("Concat рендер: конвертация MKV → MP4...", LogCategory::APP);
    m_currentStep = Step::ConcatRemux;
    emit progressUpdated(-1, "Concat: финальный MP4");

    // Convert the CFR MKV (from mkvmerge) to MP4 with faststart for streaming.
    // For AAC audio we re-encode from the original WAV to create a correct
    // Edit List (edts) — stream-copying AAC from MKV resets media_time to 0.
    QString tempMkvPath = QDir(m_paths->resultPath).filePath("concat_cfr.mkv");
    const bool audioIsAac = m_template.targetAudioFormat == "aac";

    QStringList args;
    args << "-y"
         << "-i" << tempMkvPath;

    if (audioIsAac)
    {
        args << "-i" << m_mainRuAudioPath << "-map" << "0:v:0" << "-map" << "1:a:0"
             << "-c:v" << "copy"
             << "-c:a" << "aac_at" << "-b:a" << "256k";
    }
    else
    {
        args << "-c" << "copy";
    }

    args << "-movflags" << "+faststart" << m_outputMp4Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::concatCleanup()
{
    emit logMessage("Concat рендер: очистка временных файлов...", LogCategory::APP);

    QFile::remove(QDir(m_paths->resultPath).filePath("concat_seg1.ts"));
    QFile::remove(QDir(m_paths->resultPath).filePath("concat_seg2.ts"));
    QFile::remove(QDir(m_paths->resultPath).filePath("concat_seg3.ts"));
    QFile::remove(QDir(m_paths->resultPath).filePath("concat_list.txt"));
    emit logMessage("Concat рендер MP4 успешно завершен.", LogCategory::APP);
}

// ==================== End Smart Concat Render ====================

void WorkflowManager::extractAttachments(const QJsonArray& attachments)
{
    emit logMessage("Шаг 3: Извлечение вложенных шрифтов...", LogCategory::APP);
    m_currentStep = Step::ExtractingAttachments;

    m_tempFontPaths.clear();

    QDir attachmentsDir(m_paths->attachedFontsDir());
    if (!attachmentsDir.exists())
        attachmentsDir.mkpath(".");

    QStringList args;
    args << m_mkvFilePath << "attachments";
    QStringList logFontNames;

    bool foundAnyFonts = false;
    for (const QJsonValue& val : attachments)
    {
        QJsonObject att = val.toObject();
        QString contentType = att["content_type"].toString();
        // Ищем шрифты по MIME-типу
        if (contentType.contains("font", Qt::CaseInsensitive) ||
            contentType.contains("octet-stream", Qt::CaseInsensitive))
        {
            QString id = QString::number(att["id"].toInt());
            QString fileName = att["file_name"].toString();
            // Проверяем расширение, чтобы не извлекать картинки и прочее
            if (fileName.toLower().endsWith(".ttf") || fileName.toLower().endsWith(".otf") ||
                fileName.toLower().endsWith(".ttc"))
            {
                QString outputPath = attachmentsDir.filePath(fileName);
                args << QString("%1:%2").arg(id, outputPath);

                m_tempFontPaths.append(outputPath);
                logFontNames.append(fileName);
                foundAnyFonts = true;
            }
        }
    }

    if (foundAnyFonts)
    {
        emit logMessage("Найдены шрифты для извлечения: " + logFontNames.join(", "), LogCategory::APP);
        emit progressUpdated(-1, "Извлечение вложений");
        m_processManager->startProcess(m_mkvextractPath, args);
    }
    else
    {
        emit logMessage("Шрифтов среди вложений не найдено. Пропускаем шаг.", LogCategory::APP);
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                  Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
    }
}

void WorkflowManager::onProcessStdOut(const QString& output)
{
    if (!output.trimmed().isEmpty())
    {
        LogCategory category = LogCategory::DEBUG;
        switch (m_currentStep)
        {
        case Step::AssemblingMkv:
        case Step::AssemblingSrtMaster:
        case Step::GettingMkvInfo:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ExtractingTracks:
        case Step::ExtractingAttachments:
            category = (m_sourceFormat == SourceFormat::MP4) ? LogCategory::FFMPEG : LogCategory::MKVTOOLNIX;
            break;

        case Step::ConcatExtract:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ConvertingAudio:
        case Step::RenderingMp4Pass1:
        case Step::RenderingMp4Pass2:
        case Step::RenderingMp4Audio:
        case Step::ConcatCutSegment1:
        case Step::ConcatRenderSegment2:
        case Step::ConcatCutSegment3:
        case Step::ConcatJoin:
        case Step::ConcatRemux:
            category = LogCategory::FFMPEG;
            break;
        case Step::MuxingMp4:
            category = LogCategory::APP;
            break;

        default:
            break;
        }
        emit logMessage(output, category);
    }

    if (m_currentStep == Step::ConcatExtract || m_currentStep == Step::ExtractingTracks ||
        m_currentStep == Step::ExtractingAttachments || m_currentStep == Step::AssemblingMkv ||
        m_currentStep == Step::AssemblingSrtMaster)
    {
        QRegularExpression re("Progress: (\\d+)%");
        auto it = re.globalMatch(output);
        while (it.hasNext())
        {
            auto match = it.next();
            int percentage = match.captured(1).toInt();
            emit progressUpdated(percentage);
        }
    }
}

void WorkflowManager::onProcessStdErr(const QString& output)
{
    if (!output.trimmed().isEmpty())
    {
        LogCategory category = LogCategory::DEBUG;
        switch (m_currentStep)
        {
        case Step::AssemblingMkv:
        case Step::AssemblingSrtMaster:
        case Step::GettingMkvInfo:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ExtractingTracks:
        case Step::ExtractingAttachments:
            category = (m_sourceFormat == SourceFormat::MP4) ? LogCategory::FFMPEG : LogCategory::MKVTOOLNIX;
            break;

        case Step::ConcatExtract:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ConvertingAudio:
        case Step::RenderingMp4Pass1:
        case Step::RenderingMp4Pass2:
        case Step::RenderingMp4Audio:
        case Step::ConcatCutSegment1:
        case Step::ConcatRenderSegment2:
        case Step::ConcatCutSegment3:
        case Step::ConcatJoin:
        case Step::ConcatRemux:
            category = LogCategory::FFMPEG;
            break;
        case Step::MuxingMp4:
            category = LogCategory::APP;
            break;

        default:
            break;
        }
        emit logMessage(output, category);
    }

    // И парсим прогресс
    if (m_currentStep == Step::RenderingMp4Pass1 || m_currentStep == Step::RenderingMp4Pass2 ||
        m_currentStep == Step::ConcatRenderSegment2)
    {
        QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
        QRegularExpressionMatch match = re.match(output); // Используем match, т.к. ffmpeg пишет в stderr порциями
        if (match.hasMatch())
        {
            int hours = match.captured(1).toInt();
            int minutes = match.captured(2).toInt();
            int seconds = match.captured(3).toInt();
            double currentTimeS = (hours * 3600) + (minutes * 60) + seconds;
            if (m_sourceDurationS > 0)
            {
                int percentage = static_cast<int>((currentTimeS / static_cast<double>(m_sourceDurationS)) * 100);
                emit progressUpdated(percentage);
            }
        }
    }
}

void WorkflowManager::killChildProcesses()
{
    if (m_processManager)
    {
        m_processManager->killProcess();
    }
}

ProcessManager* WorkflowManager::getProcessManager() const
{
    return m_processManager;
}

void WorkflowManager::convertToSrtAndAssembleMaster()
{
    if (!m_template.createSrtMaster)
    {
        emit logMessage("Создание мастер-копии с SRT пропущено (отключено в шаблоне).", LogCategory::APP);
        convertAudioIfNeeded();
        return;
    }

    emit logMessage("--- Начало сборки мастер-копии с SRT ---", LogCategory::APP);
    m_currentStep = Step::AssemblingSrtMaster;

    QStringList args;
    QString outputMkvPath =
        m_paths->masterMkv(QString("[DUB x TVOЁ] %1 - %2.mkv").arg(m_template.seriesTitle, m_episodeNumberForSearch));
    args << "-o" << outputMkvPath;

    QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
    args << videoPath;

    QString wavPath = m_wavForSrtMasterPath.isEmpty() ? m_mainRuAudioPath : m_wavForSrtMasterPath;
    args << "--language" << "0:rus" << wavPath;
    args << "--language" << "0:rus" << m_paths->masterSrt();

    emit progressUpdated(0, "Сборка SRT-копии");
    m_processManager->startProcess(m_mkvmergePath, args);
}

QString WorkflowManager::getExtensionForCodec(const QString& codecId)
{
    // Видео
    if (codecId == "V_MPEG4/ISO/AVC")
        return "h264";
    if (codecId == "V_MPEGH/ISO/HEVC")
        return "h265"; // или .hevc
    if (codecId == "V_AV1")
        return "ivf";
    if (codecId == "V_VP8")
        return "ivf";
    if (codecId == "V_VP9")
        return "ivf";

    // Аудио
    if (codecId == "A_AAC")
        return "aac";
    if (codecId == "A_AC3")
        return "ac3";
    if (codecId == "A_EAC3")
        return "eac3";
    if (codecId == "A_DTS")
        return "dts";
    if (codecId == "A_FLAC")
        return "flac";
    if (codecId == "A_OPUS")
        return "opus";
    if (codecId == "A_VORBIS")
        return "ogg";
    if (codecId == "A_MP3")
        return "mp3";

    // Субтитры
    if (codecId == "S_TEXT/ASS")
        return "ass";
    if (codecId == "S_TEXT/SSA")
        return "ssa";
    if (codecId == "S_TEXT/UTF8")
        return "srt";
    if (codecId == "S_VOBSUB")
        return "sub";
    if (codecId == "S_HDMV/PGS")
        return "sup";

    // Фоллбэк для неизвестных форматов
    emit logMessage(
        QString("Предупреждение: неизвестный CodecID '%1', будет использовано расширение .bin").arg(codecId),
        LogCategory::APP, LogLevel::Warning);
    return "bin";
}

QString WorkflowManager::getExtensionForFfprobeCodec(const QString& codecName)
{
    // Видео
    if (codecName == "h264")
        return "h264";
    if (codecName == "hevc")
        return "h265";
    if (codecName == "av1")
        return "ivf";
    if (codecName == "vp8")
        return "ivf";
    if (codecName == "vp9")
        return "ivf";

    // Аудио
    if (codecName == "aac")
        return "aac";
    if (codecName == "ac3")
        return "ac3";
    if (codecName == "eac3")
        return "eac3";
    if (codecName == "dts")
        return "dts";
    if (codecName == "flac")
        return "flac";
    if (codecName == "opus")
        return "opus";
    if (codecName == "vorbis")
        return "ogg";
    if (codecName == "mp3")
        return "mp3";

    // Фоллбэк
    emit logMessage(
        QString("Предупреждение: неизвестный кодек ffprobe '%1', будет использовано расширение .bin").arg(codecName),
        LogCategory::APP, LogLevel::Warning);
    return "bin";
}

SourceFormat WorkflowManager::detectSourceFormat(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "mkv")
        return SourceFormat::MKV;
    if (ext == "mp4")
        return SourceFormat::MP4;
    return SourceFormat::Unknown;
}

void WorkflowManager::getMp4Info()
{
    prepareUserFiles();

    emit logMessage("Шаг 2: Получение информации о файле MP4 (ffprobe)...", LogCategory::APP);
    m_currentStep = Step::GettingMkvInfo;

    QString ffprobePath = AppSettings::instance().ffprobePath();

    if (!QFileInfo::exists(ffprobePath))
    {
        emit logMessage("Критическая ошибка: ffprobe.exe не найден. Проверьте настройки пути к ffmpeg.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    QByteArray jsonData;
    QStringList args = {"-v", "quiet", "-print_format", "json", "-show_streams", "-show_format", m_mkvFilePath};
    bool success = m_processManager->executeAndWait(ffprobePath, args, jsonData);

    if (!success || jsonData.isEmpty())
    {
        emit logMessage("Не удалось получить информацию о MP4 файле через ffprobe.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QJsonObject root = doc.object();
    QJsonArray streams = root["streams"].toArray();

    // Получаем длительность из format
    if (root.contains("format"))
    {
        m_sourceDurationS = static_cast<qint64>(root["format"].toObject()["duration"].toString().toDouble());
    }

    m_foundAudioTracks.clear();
    for (const auto& val : streams)
    {
        QJsonObject stream = val.toObject();
        QString codecType = stream["codec_type"].toString();
        QString codecName = stream["codec_name"].toString();
        int streamIndex = stream["index"].toInt();

        if (codecType == "video" && m_videoTrack.id == -1)
        {
            m_videoTrack.id = streamIndex;
            m_videoTrack.codecId = codecName;
            m_videoTrack.extension = getExtensionForFfprobeCodec(codecName);

            // Parse video bitrate (ffprobe reports in bps as string)
            QString bitRateStr = stream["bit_rate"].toString();
            if (!bitRateStr.isEmpty())
            {
                m_videoTrack.bitrateKbps = static_cast<int>(bitRateStr.toLongLong() / 1000);
            }

            // Parse frame rate (e.g. "25/1", "24000/1001")
            m_videoTrack.frameRate = stream["r_frame_rate"].toString();
            m_videoTrack.avgFrameRate = stream["avg_frame_rate"].toString();
            m_videoTrack.isCfr = isLikelyCfr(m_videoTrack.frameRate, m_videoTrack.avgFrameRate);

            emit logMessage(
                QString("Видеодорожка найдена: индекс %1, кодек %2, битрейт %3 kbps, fps %4, avg %5, CFR=%6")
                    .arg(streamIndex)
                    .arg(codecName)
                    .arg(m_videoTrack.bitrateKbps)
                    .arg(m_videoTrack.frameRate)
                    .arg(m_videoTrack.avgFrameRate)
                    .arg(m_videoTrack.isCfr ? "yes" : "no"),
                LogCategory::APP);
        }
        else if (codecType == "audio")
        {
            // Для MP4 от режиссёра берём все аудиодорожки
            QString language = "und";
            if (stream.contains("tags"))
            {
                language = stream["tags"].toObject()["language"].toString("und");
            }

            AudioTrackInfo info;
            info.id = streamIndex;
            info.codec = codecName;
            info.language = language;
            info.name = stream.contains("tags") ? stream["tags"].toObject()["title"].toString() : "";
            m_foundAudioTracks.append(info);
        }
    }

    if (m_videoTrack.id == -1)
    {
        emit logMessage("Критическая ошибка: в MP4 файле не найдена видеодорожка.", LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    // Обработка аудиодорожек
    if (m_foundAudioTracks.isEmpty())
    {
        emit logMessage("Предупреждение: в MP4 файле не найдено аудиодорожек. Оригинал не будет добавлен в сборку.",
                        LogCategory::APP, LogLevel::Warning);
    }
    else if (m_foundAudioTracks.size() == 1)
    {
        const auto& track = m_foundAudioTracks.first();
        m_originalAudioTrack.id = track.id;
        m_originalAudioTrack.codecId = track.codec;
        m_originalAudioTrack.extension = getExtensionForFfprobeCodec(track.codec);
        emit logMessage(QString("Найдена одна аудиодорожка (индекс: %1, кодек: %2).").arg(track.id).arg(track.codec),
                        LogCategory::APP);
    }
    else
    {
        emit logMessage(
            QString("Найдено %1 аудиодорожек. Требуется выбор пользователя...").arg(m_foundAudioTracks.size()),
            LogCategory::APP);
        emit multipleAudioTracksFound(m_foundAudioTracks);
        return;
    }

    loadChaptersForWorkflow();

    if (m_template.chaptersEnabled && m_chapterMarkers.isEmpty() && !m_skipChaptersForWorkflow)
    {
        m_chapterContinueKind = ChapterContinueKind::AfterMp4Probe;
        m_wasUserInputRequested = true;
        m_lastStepBeforeRequest = Step::GettingMkvInfo;
        UserInputRequest req;
        req.chaptersRequired = true;
        req.chaptersReason =
            QStringLiteral("Для релиза ожидаются главы, но в контейнере не найдено и на главной странице не "
                           "указан файл XML. Укажите путь к файлу глав или выберите сборку без глав.");
        emit userInputRequired(req);
        return;
    }

    // MP4 не содержит attachments (шрифтов) - пропускаем этот шаг
    emit logMessage("MP4 файл не содержит вложенных шрифтов. Пропускаем извлечение вложений.", LogCategory::APP);
    m_currentStep = Step::ExtractingAttachments;
    QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                              Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
}

void WorkflowManager::extractTracksMp4()
{
    emit logMessage("Шаг 4: Извлечение дорожек из MP4 (ffmpeg)...", LogCategory::APP);
    m_currentStep = Step::ExtractingTracks;

    if (m_videoTrack.id == -1)
    {
        emit logMessage("Критическая ошибка: в файле отсутствует видеодорожка. Извлечение невозможно.",
                        LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    // Извлекаем видео и аудио одной командой ffmpeg
    QStringList args;
    args << "-y" << "-i" << m_mkvFilePath;

    // Видео
    QString videoOutPath = m_paths->extractedVideo(m_videoTrack.extension);
    args << "-map" << QString("0:%1").arg(m_videoTrack.id) << "-c" << "copy" << "-map_chapters" << "-1" << videoOutPath;

    // Аудио
    if (m_originalAudioTrack.id != -1)
    {
        QString audioOutPath = m_paths->extractedAudio("m4a");
        args << "-map" << QString("0:%1").arg(m_originalAudioTrack.id) << "-c" << "copy" << "-map_chapters" << "-1"
             << audioOutPath;
    }

    // Субтитры в MP4 не извлекаем — используем override subs
    if (!m_overrideSubsPath.isEmpty())
    {
        emit logMessage("Используются внешние субтитры: " + m_overrideSubsPath, LogCategory::APP);
    }
    else if (m_template.sourceHasSubtitles)
    {
        emit logMessage(
            "Предупреждение: MP4 файл не содержит субтитров формата ASS. Укажите субтитры через 'Свои субтитры'.",
            LogCategory::APP, LogLevel::Warning);
    }

    emit progressUpdated(-1, "Извлечение дорожек (ffmpeg)");
    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::resumeWithUserInput(const UserInputResponse& response)
{
    if (!response.isValid())
    {
        emit logMessage("Процесс прерван пользователем в диалоге выбора файлов.", LogCategory::APP);
        m_chapterContinueKind = ChapterContinueKind::None;
        m_pendingMkvInfoRoot = QJsonObject();
        emit workflowAborted();
        return;
    }

    const bool chapterResume = (m_chapterContinueKind != ChapterContinueKind::None);
    if (chapterResume)
    {
        if (response.buildWithoutChapters)
        {
            m_skipChaptersForWorkflow = true;
            emit logMessage(QStringLiteral("Главы: сборка без глав в контейнере (по выбору пользователя)."),
                            LogCategory::APP);
        }
        else if (!response.chaptersXmlPath.isEmpty())
        {
            m_userChaptersXmlPath = response.chaptersXmlPath.trimmed();
            loadChaptersForWorkflow();
            if (m_template.chaptersEnabled && m_chapterMarkers.isEmpty() && !m_skipChaptersForWorkflow)
            {
                warnIfExpectedChaptersMissing();
            }
        }
        m_wasUserInputRequested = false;
        continueWorkflowAfterChaptersResolved();
        return;
    }

    m_wasUserInputRequested = false;
    emit logMessage("Данные от пользователя получены. Возобновление работы...", LogCategory::APP);

    if (!response.audioPath.isEmpty())
    {
        emit logMessage("Пользователь предоставил аудиофайл. Обработка...", LogCategory::APP);
        QString newAudioPath = handleUserFile(response.audioPath, m_paths->sourcesPath);
        if (!newAudioPath.isEmpty())
        {
            if (m_mainRuAudioPath.isEmpty())
            {
                m_mainRuAudioPath = newAudioPath;
                m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
            }
            else
            {
                m_wavForSrtMasterPath = newAudioPath;
            }
        }
    }
    else if (m_lastStepBeforeRequest == Step::AudioPreparation && m_template.createSrtMaster &&
             !m_mainRuAudioPath.endsWith(".wav"))
    {
        emit logMessage("WAV для мастер-копии не предоставлен. Сборка мастер-копии отменена.", LogCategory::APP);
        m_template.createSrtMaster = false;
    }

    if (!response.time.isEmpty() && response.time != "0:00:00.000")
    {
        m_parsedEndingTime = response.time;
    }

    if (!response.resolvedFonts.isEmpty())
    {
        for (auto it = response.resolvedFonts.constBegin(); it != response.resolvedFonts.constEnd(); ++it)
        {
            m_fontResult.foundFonts.append({it.value(), it.key()});
            m_fontResult.notFoundFontNames.removeOne(it.key());
        }
    }

    if (m_lastStepBeforeRequest == Step::AudioPreparation)
    {
        audioPreparation();
    }
    else if (m_lastStepBeforeRequest == Step::ProcessingSubs)
    {
        processSubtitles();
    }
    else if (m_lastStepBeforeRequest == Step::AssemblingMkv)
    {
        assembleMkv(m_finalAudioPath);
    }
}

void WorkflowManager::resumeWithSignStyles(const QStringList& styles)
{
    // if (styles.isEmpty()) {
    //     emit logMessage("Не выбрано ни одного стиля для надписей. Процесс остановлен.", LogCategory::APP);
    //     emit workflowAborted();
    //     return;
    // }
    m_template.signStyles = styles;
    emit logMessage("Стили для надписей получены. Продолжаем...", LogCategory::APP);
    m_wasUserInputRequested = false;
    processSubtitles();
}

void WorkflowManager::resumeWithSelectedAudioTrack(int trackId)
{
    if (trackId < 0)
    {
        emit logMessage("Выбор аудиодорожки отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    bool found = false;
    for (const auto& track : m_foundAudioTracks)
    {
        if (track.id == trackId)
        {
            m_originalAudioTrack.id = track.id;
            m_originalAudioTrack.codecId = track.codec;
            m_originalAudioTrack.extension = (m_sourceFormat == SourceFormat::MP4)
                                                 ? getExtensionForFfprobeCodec(track.codec)
                                                 : getExtensionForCodec(track.codec);
            found = true;
            break;
        }
    }

    if (!found)
    {
        emit logMessage("Критическая ошибка: ID выбранной дорожки не найден. Процесс прерван.", LogCategory::APP,
                        LogLevel::Error);
        emit workflowAborted();
        return;
    }

    emit logMessage("Пользователь выбрал аудиодорожку с ID: " + QString::number(trackId) + ". Продолжаем...",
                    LogCategory::APP);

    loadChaptersForWorkflow();

    if (m_template.chaptersEnabled && m_chapterMarkers.isEmpty() && !m_skipChaptersForWorkflow)
    {
        m_chapterContinueKind = ChapterContinueKind::AfterAudioTrack;
        m_wasUserInputRequested = true;
        m_lastStepBeforeRequest = Step::GettingMkvInfo;
        UserInputRequest req;
        req.chaptersRequired = true;
        req.chaptersReason =
            QStringLiteral("Для релиза ожидаются главы, но в контейнере не найдено и на главной странице не "
                           "указан файл XML. Укажите путь к файлу глав или выберите сборку без глав.");
        emit userInputRequired(req);
        return;
    }

    if (m_sourceFormat == SourceFormat::MP4)
    {
        // MP4 не содержит attachments — сразу переходим к извлечению треков
        emit logMessage("MP4 файл не содержит вложенных шрифтов. Пропускаем извлечение вложений.", LogCategory::APP);
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                  Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
    }
    else
    {
        QByteArray jsonData;
        m_processManager->executeAndWait(m_mkvmergePath, {"-J", m_mkvFilePath}, jsonData);
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.object().contains("attachments"))
        {
            extractAttachments(doc.object()["attachments"].toArray());
        }
        else
        {
            emit logMessage("Вложений в файле не найдено.", LogCategory::APP);
            m_currentStep = Step::ExtractingAttachments;
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                      Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
    }
}

void WorkflowManager::processSubtitles()
{
    emit logMessage("Шаг 6: Обработка субтитров...", LogCategory::APP);
    m_currentStep = Step::ProcessingSubs;

    QString subsToAnalyze;
    if ((m_template.signStyles.isEmpty() || m_template.forceSignStyleRequest) && !m_wereStylesRequested)
    {
        if (!m_overrideSubsPath.isEmpty())
            subsToAnalyze = m_overrideSubsPath;
        else
        {
            QString extractedSubs = m_paths->extractedSubs("ass");
            if (QFileInfo::exists(extractedSubs))
                subsToAnalyze = extractedSubs;
        }
        if (!subsToAnalyze.isEmpty())
        {
            emit logMessage("Шаг 6.2: Запрос стилей и актёров для надписей...", LogCategory::APP);
            emit progressUpdated(-1, "Запрос стилей и актёров для разделения субтитров от надписей");
            m_lastStepBeforeRequest = Step::ProcessingSubs;
            m_wereStylesRequested = true;
            emit signStylesRequest(subsToAnalyze);
            return;
        }
    }

    runAssProcessing();
}

void WorkflowManager::runAssProcessing()
{
    QFile::remove(m_paths->processedFullSubs());
    QFile::remove(m_paths->processedSignsSubs());
    QFile::remove(m_paths->masterSrt());

    QString extractedSubs = m_paths->extractedSubs("ass");
    QString overrideSubs = m_overrideSubsPath;
    QString overrideSigns = m_overrideSignsPath;

    bool hasExtracted = QFileInfo::exists(extractedSubs);
    bool hasOverrideSubs = !overrideSubs.isEmpty() && QFileInfo::exists(overrideSubs);
    bool hasOverrideSigns = !overrideSigns.isEmpty() && QFileInfo::exists(overrideSigns);

    QString outputFileBase = m_paths->processedFullSubs().left(m_paths->processedFullSubs().lastIndexOf('_'));
    if (m_isSrtMasterDecoupled && hasOverrideSubs && m_template.createSrtMaster)
    {
        emit logMessage("АКТИВИРОВАН РАЗДЕЛЕННЫЙ РЕЖИМ ОБРАБОТКИ СУБТИТРОВ.", LogCategory::APP);
        bool srtSuccess = false;
        bool releaseSuccess = false;

        // 1. Поток для SRT-мастера: override_subs -> master.srt
        emit logMessage("Создание SRT из файла 'Свои субтитры'...", LogCategory::APP);
        srtSuccess = m_assProcessor->convertToSrt(overrideSubs, m_paths->masterSrt(), m_template.signStyles);
        if (hasOverrideSigns)
        {
            // 2. Поток для релиза: override_signs -> _signs.ass
            emit logMessage("Обработка файла 'Свои надписи' для релиза...", LogCategory::APP);
            releaseSuccess = m_assProcessor->addTbToFile(overrideSigns, m_paths->processedSignsSubs(), m_template,
                                                         m_parsedEndingTime);
        }
        else
        {
            emit logMessage("Свои надписи не указаны. Вырезаем надписи из основного файла субтитров для видео...",
                            LogCategory::APP);
            releaseSuccess =
                m_assProcessor->processExistingFile(overrideSubs, outputFileBase, m_template, m_parsedEndingTime);
            if (releaseSuccess)
            {
                QFile::remove(m_paths->processedFullSubs());
            }
        }
        if (!srtSuccess || !releaseSuccess)
        {
            emit logMessage("Ошибка во время раздельной обработки субтитров.", LogCategory::APP, LogLevel::Error);
            emit workflowAborted();
            return;
        }

        emit logMessage("Генерация текстов постов...", LogCategory::APP);
        EpisodeData data;
        data.episodeNumber = m_episodeNumberForPost;
        data.cast = m_template.cast;
        emit postsReady(m_template, data);
        m_wereStylesRequested = false;

        findFontsInProcessedSubs();
        return;
    }
    bool success = false;
    if (hasOverrideSubs && hasOverrideSigns)
    {
        emit logMessage("Сценарий: используются свои субтитры для диалогов и свои надписи для надписей.",
                        LogCategory::APP);
        success = m_assProcessor->processFromTwoSources(overrideSubs, overrideSigns, outputFileBase, m_template,
                                                        m_parsedEndingTime);
    }
    else if (hasOverrideSubs)
    {
        emit logMessage("Сценарий: используются свои субтитры. Файл будет разделен на диалоги и надписи.",
                        LogCategory::APP);
        success = m_assProcessor->processExistingFile(overrideSubs, outputFileBase, m_template, m_parsedEndingTime);
    }
    else if (hasOverrideSigns)
    {
        if (hasExtracted)
        {
            emit logMessage("Сценарий: используются извлеченные субтитры для диалогов и свои надписи.",
                            LogCategory::APP);
            success = m_assProcessor->processFromTwoSources(extractedSubs, overrideSigns, outputFileBase, m_template,
                                                            m_parsedEndingTime);
        }
        else
        {
            emit logMessage("Сценарий: извлеченных субтитров нет, используются только свои надписи.", LogCategory::APP);
            QString outputPath = m_paths->processedSignsSubs();
            success = m_assProcessor->addTbToFile(overrideSigns, outputPath, m_template, m_parsedEndingTime);
        }
    }
    else
    {
        if (hasExtracted)
        {
            emit logMessage("Сценарий: используются извлеченные субтитры. Файл будет разделен.", LogCategory::APP);
            success =
                m_assProcessor->processExistingFile(extractedSubs, outputFileBase, m_template, m_parsedEndingTime);
        }
        else if (m_template.generateTb)
        {
            emit logMessage("Сценарий: субтитры не найдены и не указаны. Генерируем только ТБ.", LogCategory::APP);
            QString outputPath = m_paths->processedSignsSubs();
            success = m_assProcessor->generateTbOnlyFile(outputPath, m_template, m_parsedEndingTime);
        }
        else
        {
            emit logMessage("Сценарий: субтитры не найдены и не указаны. Генерация ТБ отключена. Выходной файл будет "
                            "без субтитров и надписей",
                            LogCategory::APP);
        }
    }

    if (!success)
    {
        emit logMessage("Во время обработки субтитров произошла ошибка.", LogCategory::APP, LogLevel::Error);
        emit workflowAborted();
        return;
    }

    emit logMessage("Обработка субтитров успешно завершена.", LogCategory::APP);

    if (m_template.createSrtMaster)
    {
        m_assProcessor->convertToSrt(m_paths->processedFullSubs(), m_paths->masterSrt(), m_template.signStyles);
    }

    emit logMessage("Генерация текстов постов...", LogCategory::APP);
    EpisodeData data;
    data.episodeNumber = m_episodeNumberForPost;
    data.cast = m_template.cast;
    emit postsReady(m_template, data);
    m_wereStylesRequested = false;

    findFontsInProcessedSubs();
}

void WorkflowManager::onFontFinderFinished(const FontFinderResult& result)
{
    if ((m_processManager != nullptr) && m_processManager->wasKilled())
    {
        return;
    }

    emit logMessage("Поиск шрифтов завершен.", LogCategory::APP);
    m_fontResult = result;
    convertToSrtAndAssembleMaster();
}

QString WorkflowManager::handleUserFile(const QString& sourcePath, const QString& destDir, const QString& newName)
{
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath))
    {
        return QString();
    }

    UserFileAction action = AppSettings::instance().userFileAction();

    if (action == UserFileAction::UseOriginalPath)
    {
        return sourcePath;
    }

    QFileInfo sourceInfo(sourcePath);
    QString finalName = newName.isEmpty() ? sourceInfo.fileName() : newName;
    QFileInfo destInfo(QDir(destDir), finalName);

    if (sourceInfo.absoluteFilePath() == destInfo.absoluteFilePath())
    {
        return sourceInfo.absoluteFilePath();
    }

    if (destInfo.exists())
    {
        QFile::remove(destInfo.absoluteFilePath());
    }

    bool success = false;
    if (action == UserFileAction::Move)
    {
        success = QFile::rename(sourceInfo.absoluteFilePath(), destInfo.absoluteFilePath());
        if (success)
        {
            return destInfo.absoluteFilePath();
        }
        // Если rename не удался (например, разные диски), пробуем скопировать
    }

    // Действие Copy или фоллбэк для Move
    success = QFile::copy(sourceInfo.absoluteFilePath(), destInfo.absoluteFilePath());

    if (success)
    {
        if (action == UserFileAction::Move)
        {
            // Если это был фоллбэк для Move, удаляем исходник
            QFile::remove(sourceInfo.absoluteFilePath());
        }
        return destInfo.absoluteFilePath();
    }

    // Если ничего не получилось, возвращаем исходный путь и логируем предупреждение
    emit logMessage("ПРЕДУПРЕЖДЕНИЕ: не удалось переместить/скопировать файл. Будет использован оригинальный путь: " +
                        sourcePath,
                    LogCategory::APP, LogLevel::Warning);
    return sourceInfo.absoluteFilePath();
}

void WorkflowManager::warnIfExpectedChaptersMissing()
{
    if (m_template.chaptersEnabled && m_chapterMarkers.isEmpty())
    {
        emit logMessage(QStringLiteral("ВНИМАНИЕ: для релиза ожидались главы, но ни встроенных, ни валидного "
                                       "внешнего файла не найдено. Возможно, не приложен XML глав."),
                        LogCategory::APP);
    }
}

void WorkflowManager::loadChaptersForWorkflow()
{
    m_chaptersMuxPathForMkv.clear();
    m_chapterMarkers.clear();

    if (!m_template.chaptersEnabled || m_skipChaptersForWorkflow)
    {
        return;
    }

    const QString extPath = m_userChaptersXmlPath.trimmed();
    if (!extPath.isEmpty() && QFileInfo::exists(extPath))
    {
        m_chapterMarkers = ChapterHelper::loadChaptersFromFile(extPath);
        if (!m_chapterMarkers.isEmpty())
        {
            m_chaptersMuxPathForMkv = QFileInfo(extPath).absoluteFilePath();
            emit logMessage(QStringLiteral("Главы: используется внешний файл: %1").arg(m_chaptersMuxPathForMkv),
                            LogCategory::APP);
        }
        else
        {
            emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: не удалось разобрать файл глав: %1").arg(extPath),
                            LogCategory::APP, LogLevel::Warning);
        }
        warnIfExpectedChaptersMissing();
        return;
    }
    if (!extPath.isEmpty() && !QFileInfo::exists(extPath))
    {
        emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: файл глав не найден: %1").arg(extPath), LogCategory::APP,
                        LogLevel::Warning);
    }

    if (m_sourceFormat == SourceFormat::MKV && QFileInfo::exists(m_mkvFilePath))
    {
        const QString embPath = QDir(m_paths->sourcesPath).filePath(QStringLiteral("chapters_embedded.xml"));
        if (ChapterHelper::extractEmbeddedChaptersToFile(m_mkvextractPath, m_mkvFilePath, embPath, m_processManager))
        {
            m_chapterMarkers = ChapterHelper::loadChaptersFromFile(embPath);
            if (!m_chapterMarkers.isEmpty())
            {
                const QString muxCopy = QDir(m_paths->sourcesPath).filePath(QStringLiteral("chapters_mux.xml"));
                if (ChapterHelper::writeMatroskaChapterXml(m_chapterMarkers, muxCopy))
                {
                    m_chaptersMuxPathForMkv = muxCopy;
                }
                else
                {
                    m_chaptersMuxPathForMkv = QFileInfo(embPath).absoluteFilePath();
                }
                emit logMessage(QStringLiteral("Главы: взяты из встроенных глав MKV."), LogCategory::APP);
            }
        }
    }
    else if (m_sourceFormat == SourceFormat::MP4 && QFileInfo::exists(m_mkvFilePath))
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
                                      m_mkvFilePath};
            if (m_processManager->executeAndWait(ffprobePath, args, json))
            {
                m_chapterMarkers = ChapterHelper::parseFfprobeChaptersJson(json);
                if (!m_chapterMarkers.isEmpty())
                {
                    const QString muxCopy = QDir(m_paths->sourcesPath).filePath(QStringLiteral("chapters_mux.xml"));
                    if (ChapterHelper::writeMatroskaChapterXml(m_chapterMarkers, muxCopy))
                    {
                        m_chaptersMuxPathForMkv = muxCopy;
                    }
                    emit logMessage(QStringLiteral("Главы: взяты из встроенных метаданных MP4."), LogCategory::APP);
                }
            }
        }
    }
}

void WorkflowManager::continueWorkflowAfterChaptersResolved()
{
    switch (m_chapterContinueKind)
    {
    case ChapterContinueKind::None:
        return;
    case ChapterContinueKind::AfterMkvProbe:
    {
        QJsonObject root = m_pendingMkvInfoRoot;
        m_pendingMkvInfoRoot = QJsonObject();
        m_chapterContinueKind = ChapterContinueKind::None;
        if (root.contains(QStringLiteral("attachments")))
        {
            extractAttachments(root[QStringLiteral("attachments")].toArray());
        }
        else
        {
            emit logMessage(QStringLiteral("Вложений в файле не найдено."), LogCategory::APP);
            m_currentStep = Step::ExtractingAttachments;
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                      Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
        break;
    }
    case ChapterContinueKind::AfterMp4Probe:
        m_chapterContinueKind = ChapterContinueKind::None;
        emit logMessage(QStringLiteral("MP4 файл не содержит вложенных шрифтов. Пропускаем извлечение вложений."),
                        LogCategory::APP);
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                  Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        break;
    case ChapterContinueKind::AfterAudioTrack:
        m_chapterContinueKind = ChapterContinueKind::None;
        if (m_sourceFormat == SourceFormat::MP4)
        {
            emit logMessage(QStringLiteral("MP4 файл не содержит вложенных шрифтов. Пропускаем извлечение вложений."),
                            LogCategory::APP);
            m_currentStep = Step::ExtractingAttachments;
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                      Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
        else
        {
            QByteArray jsonData;
            m_processManager->executeAndWait(m_mkvmergePath, {QStringLiteral("-J"), m_mkvFilePath}, jsonData);
            QJsonDocument doc = QJsonDocument::fromJson(jsonData);
            const QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("attachments")))
            {
                extractAttachments(obj[QStringLiteral("attachments")].toArray());
            }
            else
            {
                emit logMessage(QStringLiteral("Вложений в файле не найдено."), LogCategory::APP);
                m_currentStep = Step::ExtractingAttachments;
                QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0),
                                          Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
            }
        }
        break;
    }
}

void WorkflowManager::maybeApplyChaptersToFinalMp4()
{
    if (m_mp4ChaptersEmbeddedInMux)
    {
        emit logMessage(QStringLiteral("Главы уже добавлены в MP4 на этапе mux через MP4Box."), LogCategory::APP);
        return;
    }
    if (m_chapterMarkers.isEmpty() || m_outputMp4Path.isEmpty() || !QFileInfo::exists(m_outputMp4Path))
    {
        return;
    }
    emit logMessage(QStringLiteral("Запись глав в финальный MP4 через MP4Box..."), LogCategory::APP);

    const QString mp4boxPath = AppSettings::instance().mp4boxPath();
    const QString chaptersTxt = QDir(QFileInfo(m_outputMp4Path).absolutePath()).filePath("auto_mp4_chapters.txt");
    if (!mp4boxPath.isEmpty() && QFileInfo::exists(mp4boxPath) &&
        ChapterHelper::writeOgmChapterText(m_chapterMarkers, chaptersTxt))
    {
        QByteArray out;
        const QStringList args = {QStringLiteral("-chap"), QDir::toNativeSeparators(chaptersTxt),
                                  QDir::toNativeSeparators(m_outputMp4Path)};
        const bool ok = m_processManager->executeAndWait(mp4boxPath, args, out);
        QFile::remove(chaptersTxt);
        if (ok)
        {
            emit logMessage(QStringLiteral("Главы записаны в MP4 через MP4Box."), LogCategory::APP);
            return;
        }
        emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: MP4Box не смог добавить главы, используем ffmpeg fallback."),
                        LogCategory::APP, LogLevel::Warning);
    }

    const qint64 durNs = m_sourceDurationS > 0 ? static_cast<qint64>(static_cast<double>(m_sourceDurationS) * 1e9) : 0;
    QString err;
    if (ChapterHelper::applyChaptersToMp4(m_outputMp4Path, m_chapterMarkers, durNs, m_ffmpegPath, m_processManager,
                                          &err))
    {
        emit logMessage(QStringLiteral("Главы записаны в MP4 через ffmpeg fallback."), LogCategory::APP);
    }
    else
    {
        emit logMessage(QStringLiteral("ПРЕДУПРЕЖДЕНИЕ: не удалось записать главы в MP4: %1").arg(err),
                        LogCategory::APP, LogLevel::Warning);
    }
}

void WorkflowManager::prepareUserFiles()
{
    emit logMessage("Перемещение пользовательских файлов в структуру проекта...", LogCategory::APP);

    // 1. Русская аудиодорожка
    QString oldAudioPath = m_mainWindow->getAudioPath();
    QString newAudioPath = handleUserFile(oldAudioPath, m_paths->sourcesPath);

    if (newAudioPath != oldAudioPath && !newAudioPath.isEmpty())
    {
        m_mainRuAudioPath = newAudioPath;
        m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
        emit logMessage("Путь к аудиодорожке обновлен: " + m_mainRuAudioPath, LogCategory::APP);
    }
    else if (!newAudioPath.isEmpty())
    {
        // Путь не изменился, но файл существует
        m_mainRuAudioPath = newAudioPath;
    }

    // 2. Свои субтитры
    QString newSubsPath =
        handleUserFile(m_mainWindow->getOverrideSubsPath(), m_paths->sourcesPath, "override_subs.ass");
    if (!newSubsPath.isEmpty() && newSubsPath != m_overrideSubsPath)
    {
        m_overrideSubsPath = newSubsPath;
        m_mainWindow->findChild<QLineEdit*>("overrideSubsPathEdit")->setText(m_overrideSubsPath);
        emit logMessage("Путь к файлу субтитров обновлен: " + m_overrideSubsPath, LogCategory::APP);
    }
    else if (!newSubsPath.isEmpty())
    {
        m_overrideSubsPath = newSubsPath;
    }

    // 3. Свои надписи
    QString newSignsPath =
        handleUserFile(m_mainWindow->getOverrideSignsPath(), m_paths->sourcesPath, "override_signs.ass");
    if (!newSignsPath.isEmpty() && newSignsPath != m_overrideSignsPath)
    {
        m_overrideSignsPath = newSignsPath;
        m_mainWindow->findChild<QLineEdit*>("overrideSignsPathEdit")->setText(m_overrideSignsPath);
        emit logMessage("Путь к файлу надписей обновлен: " + m_overrideSignsPath, LogCategory::APP);
    }
    else if (!newSignsPath.isEmpty())
    {
        m_overrideSignsPath = newSignsPath;
    }
}

void WorkflowManager::onBitrateCheckFinished(RerenderDecision decision, const RenderPreset& newPreset)
{
    if (decision == RerenderDecision::Rerender)
    {
        emit logMessage("Получено решение о перерендере.", LogCategory::APP);
        m_renderPreset = newPreset;
        m_currentStep = Step::RenderingMp4Pass1;
        runRenderPass(m_currentStep);
    }
    else
    {
        m_mp4VideoReady = true;
        startMp4MuxPipeline();
    }
}

void WorkflowManager::resumeAfterSubEdit()
{
    emit logMessage("Редактирование завершено, процесс возобновлен.", LogCategory::APP);
    audioPreparation();
}
