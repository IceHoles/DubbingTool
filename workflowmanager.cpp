#include "workflowmanager.h"
#include "mainwindow.h"
#include "fontfinder.h"
#include "assprocessor.h"
#include "processmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QSet>
#include <QHttpMultiPart>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFontDatabase>
#include <QHash>
#include <QFontInfo>
#include <windows.h> // Для API шрифтов


WorkflowManager::WorkflowManager(ReleaseTemplate t, const QString &episode, const QSettings &settings, MainWindow *mainWindow)
    : QObject{mainWindow},
    m_template(t),
    m_episodeNumber(episode),
    m_mainWindow(mainWindow),
    m_wasUserInputRequested(false)
{
    m_webUiHost = settings.value("webUi/host", "http://127.0.0.1").toString();
    m_webUiPort = settings.value("webUi/port", 8080).toInt();
    m_webUiUser = settings.value("webUi/user").toString();
    m_webUiPassword = settings.value("webUi/password").toString();
    m_mkvmergePath = settings.value("paths/mkvmerge", "mkvmerge").toString();
    m_mkvextractPath = settings.value("paths/mkvextract", "mkvextract.exe").toString();
    m_ffmpegPath = settings.value("paths/ffmpeg", "ffmpeg").toString();
    m_renderPreset = settings.value("render/preset", "NVIDIA (hevc_nvenc)").toString();
    m_customRenderArgs = settings.value("render/custom_args", "").toString();
    m_overrideSubsPath = m_mainWindow->getOverrideSubsPath();
    m_overrideSignsPath = m_mainWindow->getOverrideSignsPath();


    m_netManager = new QNetworkAccessManager(this);
    //m_pollingTimer = new QTimer(this);
    m_hashFindTimer = new QTimer(this);
    m_progressTimer = new QTimer(this);
    m_processManager = new ProcessManager(this);
    m_assProcessor = new AssProcessor(this);

    connect(m_assProcessor, &AssProcessor::logMessage, this, &WorkflowManager::logMessage);
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onPollingTimerTimeout);
    connect(m_processManager, &ProcessManager::processOutput, this, &WorkflowManager::onProcessStdOut);
    connect(m_processManager, &ProcessManager::processStdErr, this, &WorkflowManager::onProcessStdErr);
    connect(m_processManager, &ProcessManager::processFinished, this, &WorkflowManager::onProcessFinished);
}

WorkflowManager::~WorkflowManager()
{

}

void WorkflowManager::start()
{
    m_savePath = QString("downloads/%1/Episode %2").arg(m_template.seriesTitle).arg(m_episodeNumber);
    if (checkForExistingFile()) {
        return;
    }

    emit logMessage("Шаг 1: Скачивание RSS-фида...");
    QNetworkRequest request(m_template.rssUrl);
    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onRssDownloaded(reply); });
}

void WorkflowManager::startWithManualFile(const QString &filePath)
{
    m_mkvFilePath = filePath;
    m_savePath = QFileInfo(filePath).path();
    emit logMessage("Работа в ручном режиме с файлом: " + m_mkvFilePath);
    emit logMessage("Рабочая папка: " + m_savePath);
    getMkvInfo();
}

bool WorkflowManager::checkForExistingFile()
{
    QDir dir(m_savePath);
    if (!dir.exists()) {
        return false;
    }

    dir.setNameFilters({"*.mkv"});
    QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name); // Сортируем по имени

    for (const QFileInfo &fileInfo : list) {
        QString fileName = fileInfo.fileName();
        // Пропускаем файлы, которые являются результатом нашей работы
        if (fileName.startsWith("[DUB]", Qt::CaseInsensitive) || fileName.startsWith("[DUB x TVOЁ]", Qt::CaseInsensitive)) {
            continue;
        }

        // Если дошли сюда, значит, это, скорее всего, нужный нам "сырой" файл. Берем первый попавшийся.
        m_mkvFilePath = fileInfo.absoluteFilePath();
        emit logMessage("Обнаружен уже скачанный исходный файл: " + m_mkvFilePath);
        emit logMessage("Пропускаем скачивание и переходим к обработке.");
        getMkvInfo();
        return true;
    }

    return false;
}

void WorkflowManager::onRssDownloaded(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети (RSS): " + reply->errorString());
        emit workflowAborted();
        reply->deleteLater();
        return;
    }
    emit logMessage("RSS-фид успешно скачан.");
    parseRssAndDownload(reply->readAll());
    reply->deleteLater();
}

void WorkflowManager::parseRssAndDownload(const QByteArray &rssData)
{
    emit logMessage("Шаг 2: Поиск нужного торрента в RSS...");

    QXmlStreamReader xml(rssData);
    QString foundMagnetLink;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name().toString() == "item") {
            QString currentTitle, currentLink;
            while (!(xml.isEndElement() && xml.name().toString() == "item")) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name().toString() == "title") currentTitle = xml.readElementText();
                    else if (xml.name().toString() == "link") currentLink = xml.readElementText();
                }
            }

            bool is1080p = currentTitle.contains("1080p", Qt::CaseInsensitive);
            bool isNotHevc = !currentTitle.contains("HEVC", Qt::CaseInsensitive);
            bool hasCorrectEpisode = currentTitle.contains(QString(" - %1 ").arg(m_episodeNumber));

            if (is1080p && isNotHevc && hasCorrectEpisode) {
                emit logMessage("Найден подходящий торрент: " + currentTitle);
                foundMagnetLink = currentLink;
                break;
            }
        }
    }

    if (foundMagnetLink.isEmpty()) {
        emit logMessage("Не удалось найти подходящий торрент для серии " + m_episodeNumber);
        emit workflowAborted();
    } else {
        m_magnetLink = foundMagnetLink;
        login();
    }
}

void WorkflowManager::login()
{
    emit logMessage("Шаг 3: Аутентификация в qBittorrent Web UI...");

    QUrl url(QString("%1:%2/api/v2/auth/login").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("username", m_webUiUser);
    postData.addQueryItem("password", m_webUiPassword);

    QNetworkReply *reply = m_netManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onLoginFinished(reply); });
}

void WorkflowManager::onLoginFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети (Login): " + reply->errorString());
        emit workflowAborted();
        reply->deleteLater();
        return;
    }

    QVariant cookieVariant = reply->header(QNetworkRequest::SetCookieHeader);
    if (cookieVariant.isValid()) {
        m_cookies = cookieVariant.value<QList<QNetworkCookie>>();
        bool sidFound = false;
        for(const auto& cookie : m_cookies) {
            if(cookie.name() == "SID") {
                sidFound = true;
                break;
            }
        }
        if (sidFound) {
            emit logMessage("Аутентификация успешна.");
            addTorrent(m_magnetLink);
        } else {
            emit logMessage("Ошибка: SID cookie не получен. Проверьте логин/пароль.");
            emit workflowAborted();
        }
    } else {
        emit logMessage("Ошибка: ответ на аутентификацию не содержит cookie.");
        emit workflowAborted();
    }
    reply->deleteLater();
}

void WorkflowManager::addTorrent(const QString &magnetLink)
{
    emit logMessage("Шаг 4: Добавление торрента через Web API...");
    m_currentStep = Step::AddingTorrent;

    QDir(m_savePath).mkpath(".");

    QUrl url(QString("%1:%2/api/v2/torrents/add").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart urlsPart;
    urlsPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"urls\""));
    urlsPart.setBody(magnetLink.toUtf8());

    QHttpPart savePathPart;
    savePathPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"savepath\""));
    savePathPart.setBody(QDir(m_savePath).absolutePath().toUtf8());

    multiPart->append(urlsPart);
    multiPart->append(savePathPart);

    QNetworkReply *reply = m_netManager->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentAdded(reply); });
}

void WorkflowManager::onTorrentAdded(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError && reply->readAll().trimmed() == "Ok.") {
        emit logMessage("Торрент успешно добавлен в qBittorrent. Начинаем поиск хеша...");
        // Вместо немедленного поиска, запускаем таймер с попытками
        findTorrentHashAndStartPolling();
    } else {
        emit logMessage("Ошибка добавления торрента: " + reply->errorString());
        emit workflowAborted();
    }
    reply->deleteLater();
}

void WorkflowManager::findTorrentHashAndStartPolling()
{
    // Сбрасываем счетчик и подключаем таймер к слоту, который будет делать попытки
    m_hashFindAttempts = 0;
    connect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt);

    // Запускаем первую попытку немедленно, а затем по таймеру
    onHashFindAttempt();
}

void WorkflowManager::onHashFindAttempt()
{
    if (m_hashFindAttempts >= MAX_HASH_FIND_ATTEMPTS) {
        emit logMessage("Ошибка: не удалось найти хеш торрента после нескольких попыток.");
        m_hashFindTimer->stop(); // Останавливаем таймер
        disconnect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt); // Отключаем сигнал
        emit workflowAborted();
        return;
    }

    m_hashFindAttempts++;
    emit logMessage(QString("Шаг 5: Поиск хеша торрента (попытка %1)...").arg(m_hashFindAttempts));

    QUrl url(QString("%1:%2/api/v2/torrents/info").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QNetworkReply *reply = m_netManager->get(request);
    // onTorrentListReceived теперь вызывается отсюда
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentListReceived(reply); });

    // Если это не первая попытка, таймер уже запущен. Если первая, запускаем его.
    if (!m_hashFindTimer->isActive()) {
        m_hashFindTimer->start(HASH_FIND_INTERVAL_MS);
    }
}

void WorkflowManager::onTorrentListReceived(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети при получении списка торрентов: " + reply->errorString());
        // Не прерываемся, таймер сделает следующую попытку
        reply->deleteLater();
        return;
    }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    QString absoluteSavePath = QDir(m_savePath).absolutePath();

    for (const QJsonValue &value : torrents) {
        QJsonObject torrent = value.toObject();
        // Сверяем пути, предварительно нормализовав их
        if (QDir(torrent["save_path"].toString()).absolutePath() == absoluteSavePath) {
            m_torrentHash = torrent["hash"].toString();
            emit logMessage("Хеш торрента успешно найден: " + m_torrentHash);

            // Успех! Останавливаем таймер поиска хеша и отключаем его
            m_hashFindTimer->stop();
            disconnect(m_hashFindTimer, &QTimer::timeout, this, &WorkflowManager::onHashFindAttempt);

            startPolling(); // Запускаем основной поллинг прогресса загрузки
            reply->deleteLater();
            return;
        }
    }

    // Если мы дошли сюда, хеш не найден в этом ответе.
    // Ничего не делаем, просто ждем следующего срабатывания таймера.
    emit logMessage("Хеш пока не найден, ждем следующей попытки...");
    reply->deleteLater();
}

void WorkflowManager::startPolling()
{
    m_currentStep = Step::Polling;
    emit logMessage("Начинаем отслеживание прогресса скачивания...");
    emit progressUpdated(0, "Скачивание торрента");
    // Отключаем все предыдущие соединения этого таймера
    m_progressTimer->disconnect();
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onPollingTimerTimeout);
    onPollingTimerTimeout();
    m_progressTimer->start(1000);
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

    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentProgressReceived(reply); });
}

void WorkflowManager::onTorrentProgressReceived(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return; }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    if (torrents.isEmpty()) { reply->deleteLater(); return; }

    QJsonObject torrent = torrents[0].toObject();
    double progress = torrent["progress"].toDouble();
    int percentage = static_cast<int>(progress * 100);

    emit progressUpdated(percentage);

    if (progress >= 1.0) {
        m_progressTimer->stop();
        emit logMessage("Скачивание завершено (100%).");
        getTorrentFiles();
    }
    reply->deleteLater();
}

void WorkflowManager::getTorrentFiles()
{
    emit logMessage("Шаг 6: Получение списка файлов в торренте...");
    QUrl url(QString("%1:%2/api/v2/torrents/files").arg(m_webUiHost).arg(m_webUiPort));
    QUrlQuery query;
    query.addQueryItem("hash", m_torrentHash);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));
    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentFilesReceived(reply); });
}

void WorkflowManager::onTorrentFilesReceived(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return; }

    QJsonArray files = QJsonDocument::fromJson(reply->readAll()).array();
    qint64 maxSize = 0;
    QString mainFileName;

    for (const QJsonValue &val : files) {
        QJsonObject file = val.toObject();
        QString name = file["name"].toString();
        if (name.endsWith(".mkv") && file["size"].toInteger() > maxSize) {
            maxSize = file["size"].toInteger();
            mainFileName = name;
        }
    }

    if (mainFileName.isEmpty()) {
        emit logMessage("Ошибка: не удалось найти .mkv файл в торренте.");
        emit workflowAborted();
    } else {
        m_mkvFilePath = QDir(m_savePath).filePath(mainFileName);
        emit logMessage("Найден основной файл: " + m_mkvFilePath);
        getMkvInfo();
    }
    reply->deleteLater();
}

void WorkflowManager::getMkvInfo()
{
    emit logMessage("Шаг 7: Получение информации о дорожках MKV...");
    m_currentStep = Step::GettingMkvInfo;

    QByteArray jsonData;
    bool success = m_processManager->executeAndWait(m_mkvmergePath, {"-J", m_mkvFilePath}, jsonData);

    if (success) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonObject root = doc.object();
        QJsonArray tracks = root["tracks"].toArray();
        if (root.contains("container")) {
            m_sourceDurationS = root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0;
        }

        if (root.contains("chapters") && !m_template.endingChapterName.isEmpty()) {
            QJsonArray chapters = root["chapters"].toArray();
            bool chapterFound = false;
            for (const QJsonValue &val : chapters) {
                QJsonObject chapter = val.toObject();
                if (chapter["tags"].toObject()["title"].toString() == m_template.endingChapterName) {
                    m_parsedEndingTime = chapter["start_time"].toString();
                    emit logMessage(QString("Найдена глава '%1', время начала: %2").arg(m_template.endingChapterName, m_parsedEndingTime));
                    chapterFound = true;
                    break;
                }
            }
            if (!chapterFound) {
                emit logMessage(QString("ПРЕДУПРЕЖДЕНИЕ: Глава с именем '%1' не найдена в файле.").arg(m_template.endingChapterName));
            }
        }

        for (const QJsonValue &val : tracks) {
            QJsonObject track = val.toObject();
            QJsonObject props = track["properties"].toObject();
            QString codecId = props["codec_id"].toString();

            // Берем ПЕРВОЕ найденное видео
            if (track["type"].toString() == "video" && m_videoTrack.id == -1) {
                m_videoTrack.id = track["id"].toInt();
                m_videoTrack.codecId = codecId;
                m_videoTrack.extension = getExtensionForCodec(codecId);
            }
            // Берем ПЕРВУЮ аудиодорожку с нужным языком
            else if (track["type"].toString() == "audio" && props["language"].toString() == m_template.originalLanguage && m_originalAudioTrack.id == -1) {
                m_originalAudioTrack.id = track["id"].toInt();
                m_originalAudioTrack.codecId = codecId;
                m_originalAudioTrack.extension = getExtensionForCodec(codecId);
            }
            // Берем ПЕРВУЮ дорожку русских субтитров
            else if (track["type"].toString() == "subtitles" && props["language"].toString() == "rus" && m_subtitleTrack.id == -1) {
                m_subtitleTrack.id = track["id"].toInt();
                m_subtitleTrack.codecId = codecId;
                m_subtitleTrack.extension = getExtensionForCodec(codecId);
            }
        }

        if (root.contains("attachments")) {
            extractAttachments(root["attachments"].toArray());
        } else {
            emit logMessage("Вложений в файле не найдено.");
            m_currentStep = Step::ExtractingAttachments; // Имитируем завершение этого шага
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
    } else {
        emit logMessage("Не удалось получить информацию о файле. Процесс остановлен.");
        finishWorkflow();
    }
}

void WorkflowManager::extractTracks()
{
    emit logMessage("Шаг 8: Извлечение дорожек...");
    m_currentStep = Step::ExtractingTracks;

    if (m_videoTrack.id == -1) {
        emit logMessage("Критическая ошибка: в файле отсутствует видеодорожка.");
        finishWorkflow();
        return;
    }
    // Если оригинальная дорожка не найдена - это не критично
    if (m_originalAudioTrack.id == -1) {
        emit logMessage("Предупреждение: оригинальная аудиодорожка не найдена. Сборка будет без нее.");
    }

    QStringList args;
    args << m_mkvFilePath << "tracks";
    QString videoOutPath = QDir(m_savePath).filePath("video." + m_videoTrack.extension);
    QString audioOutPath = QDir(m_savePath).filePath("audio." + m_originalAudioTrack.extension);

    args << QString("%1:%2").arg(m_videoTrack.id).arg(videoOutPath);
    args << QString("%1:%2").arg(m_originalAudioTrack.id).arg(audioOutPath);

    if (m_template.sourceHasSubtitles && m_subtitleTrack.id != -1) {
        args << QString("%1:%2").arg(m_subtitleTrack.id).arg(QDir(m_savePath).filePath("subtitles." + m_subtitleTrack.extension));
    } else if (m_template.sourceHasSubtitles && m_subtitleTrack.id == -1) {
        emit logMessage("Предупреждение: в файле не найдены русские субтитры, хотя в шаблоне они ожидались.");
    }

    emit progressUpdated(-1, "Извлечение дорожек");
    m_processManager->startProcess(m_mkvextractPath, args);
}

void WorkflowManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.");
        emit workflowAborted();
        return;
    }

    switch (m_currentStep) {
    case Step::ExtractingAttachments:
        emit logMessage("Извлечение вложений завершено.");
        extractTracks();
        break;

    case Step::ExtractingTracks:
    {
        emit logMessage("Извлечение дорожек завершено.");
        emit logMessage("Шаг 9: Обработка субтитров...");
        m_currentStep = Step::ProcessingSubs;

        // Определяем, есть ли у нас вообще файл для анализа стилей
        QString subsToAnalyze;
        if (!m_overrideSubsPath.isEmpty()) {
            subsToAnalyze = m_overrideSubsPath;
        } else if (!m_overrideSignsPath.isEmpty()) {
            subsToAnalyze = m_overrideSignsPath;
        } else {
            QString extractedSubs = QDir(m_savePath).filePath("subtitles.ass");
            if (QFileInfo::exists(extractedSubs)) {
                subsToAnalyze = extractedSubs;
            }
        }

        // Главное условие: если в шаблоне есть файл для анализа, то спрашиваем
        if (!subsToAnalyze.isEmpty()) {
            emit logMessage("Запрашиваем у пользователя стили/актеров для отделения надписей...");
            emit signStylesRequest(subsToAnalyze);
            // Прерываемся и ждем. Логика продолжится в resumeWithSignStyles,
            // который в итоге вызовет processSubtitles().
        } else {
            // Если анализировать нечего (нет ни извлеченных, ни указанных субтитров),
            // то и спрашивать не о чем. Просто продолжаем.
            emit logMessage("Файлы субтитров для анализа не найдены. Пропускаем выбор стилей.");
            processSubtitles();
        }
        break; // Этот break очень важен!
    }
    case Step::AssemblingSrtMaster:
    {
        emit logMessage("Мастер-копия с SRT успешно собрана.");
        convertAudioIfNeeded();
        break;
    }

    case Step::ConvertingAudio:
    {
        m_progressTimer->stop();
        QFile::remove(m_ffmpegProgressFile); // Удаляем временный файл
        emit logMessage("Конвертация аудио успешно завершена.");
        emit progressUpdated(100); // Показываем 100% в конце
        assembleMkv(m_finalAudioPath);
        break;
    }

    case Step::AssemblingMkv:
    {
        emit logMessage("Финальный MKV файл успешно собран.");
        renderMp4();
        break;
    }

    case Step::RenderingMp4:
    {
        emit logMessage("Рендер MP4 успешно завершен.");
        emit filesReady(m_finalMkvPath, m_outputMp4Path);
        // На этом рабочий процесс полностью завершен
        finishWorkflow();
        break;
    }

    default:
        break;
    }
}

void WorkflowManager::convertAudioIfNeeded()
{
    emit logMessage("Шаг 10: Проверка и конвертация аудио...");

    m_userAudioPath = m_mainWindow->getAudioPath();
    if (m_userAudioPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не указан путь к русской аудиодорожке.");
        emit workflowAborted();
        return;
    }

    QString targetFormat = m_template.targetAudioFormat;
    emit logMessage(QString("Целевой формат аудио: %1").arg(targetFormat));

    // Если формат файла уже соответствует целевому, ничего не делаем
    if (m_userAudioPath.endsWith("." + targetFormat, Qt::CaseInsensitive)) {
        emit logMessage("Аудиофайл уже в целевом формате. Конвертация не требуется.");
        m_finalAudioPath = m_userAudioPath;
        assembleMkv(m_finalAudioPath);
        return;
    }

    m_currentStep = Step::ConvertingAudio;
    m_finalAudioPath = QDir(m_savePath).filePath("russian_audio." + targetFormat);
    emit logMessage(QString("Запуск конвертации в %1...").arg(targetFormat.toUpper()));

    m_ffmpegProgressFile = QDir(m_savePath).filePath("ffmpeg_progress.log");

    QStringList args;
    args << "-y" << "-i" << m_userAudioPath;

    if (targetFormat == "aac") {
        args << "-c:a" << "aac" << "-b:a" << "256k";
    } else if (targetFormat == "flac") {
        args << "-c:a" << "flac";
    } else {
        emit logMessage(QString("Ошибка: неизвестный целевой формат аудио '%1'").arg(targetFormat));
        emit workflowAborted();
        return;
    }

    args << "-progress" << QDir::toNativeSeparators(m_ffmpegProgressFile);

    args << m_finalAudioPath;

    m_progressTimer->disconnect();
    connect(m_progressTimer, &QTimer::timeout, this, &WorkflowManager::onAudioConversionProgress);
    m_progressTimer->start(500);
    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::onAudioConversionProgress()
{
    QFile progressFile(m_ffmpegProgressFile);
    if (!progressFile.open(QIODevice::ReadOnly)) return;

    QTextStream in(&progressFile);
    qint64 totalDurationUs = 0;
    qint64 currentTimeUs = 0;

    // Узнаем общую длительность из mkvmerge -J
    totalDurationUs = m_sourceDurationS * 1000000;

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

void WorkflowManager::assembleMkv(const QString &russianAudioPath)
{
    m_userAudioPath = russianAudioPath; // Обновляем путь к аудио

    emit logMessage("Шаг 11: Проверка компонентов для сборки MKV...");
    m_currentStep = Step::AssemblingMkv;

    QString videoPath = QDir(m_savePath).filePath("video." + m_videoTrack.extension);
    QString originalAudioPath = QDir(m_savePath).filePath("audio." + m_originalAudioTrack.extension);
    QString fullSubsPath = QDir(m_savePath).filePath("subtitles_processed_full.ass");
    QString signsPath = QDir(m_savePath).filePath("subtitles_processed_signs.ass");


    // Этот блок поиска шрифтов остается без изменений
    QStringList subFilesToCheck;
    if (QFileInfo::exists(QDir(m_savePath).filePath("subtitles_processed_full.ass"))) {
        subFilesToCheck << QDir(m_savePath).filePath("subtitles_processed_full.ass");
    } else {
        subFilesToCheck << QDir(m_savePath).filePath("subtitles_processed_signs.ass");
    }

    // Если шрифты еще не искали, ищем их
    if (m_fontResult.foundFonts.isEmpty() && m_fontResult.notFoundFontNames.isEmpty()) {
        FontFinder finder;
        connect(&finder, &FontFinder::logMessage, this, &WorkflowManager::logMessage);
        QEventLoop loop;
        connect(&finder, &FontFinder::finished, &loop, &QEventLoop::quit);
        connect(&finder, &FontFinder::finished, this, [&](const FontFinderResult& result){
            m_fontResult = result;
        });
        finder.findFontsInSubs(subFilesToCheck);
        loop.exec();
    }

    // --- НОВАЯ ЛОГИКА ПРОВЕРКИ И ЗАПРОСА ---

    // Проверяем, нужны ли данные, И не спрашивали ли мы уже пользователя
    if (!m_wasUserInputRequested) {
        bool audioNeeded = m_userAudioPath.isEmpty();
        bool fontsNeeded = !m_fontResult.notFoundFontNames.isEmpty();

        if (audioNeeded || fontsNeeded) {
            emit logMessage("Обнаружены недостающие файлы, запрашиваем у пользователя...");
            m_wasUserInputRequested = true; // Устанавливаем флаг, что мы спросили!
            m_lastStepBeforeRequest = m_currentStep;
            emit missingFilesRequest(m_fontResult.notFoundFontNames);
            return; // Прерываемся и ждем ответа
        }
    }
    // --- КОНЕЦ НОВОЙ ЛОГИКИ ---

    // Если мы здесь, значит, либо все данные были на месте, либо пользователь уже ответил.
    // Проверяем наличие обязательного аудио. Если его нет даже после запроса - это критическая ошибка.
    if (m_userAudioPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не указан путь к русской аудиодорожке. Сборка невозможна.");
        emit workflowAborted();
        return;
    }

    emit logMessage("Все необходимые файлы на месте. Начинаем сборку mkvmerge...");

    // Собираем пути к шрифтам
    QStringList fontPaths;
    for (const FoundFontInfo &fontInfo : m_fontResult.foundFonts) {
        fontPaths.append(fontInfo.path);
    }
    fontPaths.removeDuplicates();

    // Если остались ненайденные шрифты (пользователь нажал ОК, не выбрав их)
    if (!m_fontResult.notFoundFontNames.isEmpty()) {
        emit logMessage("ПРЕДУПРЕЖДЕНИЕ: Не все шрифты были предоставлены. Субтитры могут отображаться некорректно.");
        emit logMessage("Пропущены: " + m_fontResult.notFoundFontNames.join(", "));
    }

    // 5. Собираем команду для mkvmerge
    QString outputFileName = QString("[DUB] %1 - %2.mkv").arg(m_template.seriesTitle).arg(m_episodeNumber);
    m_finalMkvPath  = QDir(m_savePath).filePath(outputFileName);

    QStringList args;
    args << "-o" << m_finalMkvPath ;

    for(const QString& path : fontPaths) {
        args << "--attachment-name" << QFileInfo(path).fileName();
        QString mimeType = "application/octet-stream";
        if (path.endsWith(".ttf", Qt::CaseInsensitive)) mimeType = "application/x-font-ttf";
        else if (path.endsWith(".otf", Qt::CaseInsensitive)) mimeType = "application/vnd.ms-opentype";
        else if (path.endsWith(".ttc", Qt::CaseInsensitive)) mimeType = "application/font-collection";
        args << "--attachment-mime-type" << mimeType;
        args << "--attach-file" << path;
    }

    // Названия из шаблона
    QString animStudio = m_template.animationStudio;
    QString subAuthor = m_template.subAuthor;

    // Дорожка видео
    args << "--language" << "0:" + m_template.originalLanguage << "--track-name" << QString("0:Видеоряд [%1]").arg(animStudio) << videoPath;

    // Дорожка русского аудио (с флагом по умолчанию)
    args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]" << russianAudioPath;

    // Дорожка оригинального аудио
    args << "--default-track-flag" << "0:no" << "--language" << "0:" + m_template.originalLanguage << "--track-name" << QString("0:Оригинал [%1]").arg(animStudio) << originalAudioPath;

    // Дорожки субтитров (если они существуют)
    if (QFileInfo::exists(signsPath)) {
        args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << QString("0:Надписи [%1]").arg(subAuthor) << signsPath;
    }
    if (QFileInfo::exists(fullSubsPath)) {
        args << "--default-track-flag" << "0:no" << "--language" << "0:rus" << "--track-name" << QString("0:Субтитры [%1]").arg(subAuthor) << fullSubsPath;
    }

    emit progressUpdated(-1, "Сборка MKV");
    m_processManager->startProcess(m_mkvmergePath, args);
}

void WorkflowManager::renderMp4()
{
    emit logMessage("Шаг 12: Рендер финального MP4 файла...");
    emit progressUpdated(0, "Рендер MP4");
    m_currentStep = Step::RenderingMp4;

    QString signsPath = QDir(m_savePath).filePath("subtitles_processed_signs.ass");
    if (!QFileInfo::exists(signsPath)) {
        emit logMessage("Предупреждение: файл надписей не найден, MP4 будет без hardsub.");
        signsPath.clear();
    }

    // Экранируем путь для фильтра ffmpeg
    QString escapedSubsPath = signsPath;
    if (!escapedSubsPath.isEmpty()) {
        escapedSubsPath.replace('\\', "/");
        escapedSubsPath.replace(':', "\\:");
    }

    m_outputMp4Path = m_finalMkvPath;
    m_outputMp4Path.replace(".mkv", ".mp4");

    QStringList args;
    args << "-y"           // Перезаписывать без вопроса
         << "-hide_banner"; // Скрыть информацию о сборке ffmpeg

    // Входной файл
    args << "-i" << m_finalMkvPath;

    // --- НАЧАЛО ЛОГИКИ ВЫБОРА КОДИРОВЩИКА И ПАРАМЕТРОВ ---

    // 1. Фильтры (-vf)
    QStringList vf_options;
    if (!signsPath.isEmpty()) {
        vf_options << QString("subtitles='%1'").arg(escapedSubsPath);
    }

    // 2. Параметры видеокодека (-c:v и сопутствующие)
    if (!m_customRenderArgs.isEmpty()) {
        emit logMessage("Используются кастомные аргументы для видео: " + m_customRenderArgs);
        args.append(m_customRenderArgs.split(' ', Qt::SkipEmptyParts));
    } else {
        if (m_renderPreset == "NVIDIA (hevc_nvenc)") {
            emit logMessage("Используется пресет NVIDIA (hevc_nvenc).");
            args << "-c:v" << "hevc_nvenc";
            args << "-preset" << "p6"
                 << "-tune" << "hq"
                 << "-rc" << "vbr"
                 << "-b:v" << "4000k"
                 << "-maxrate" << "8000k" // Добавим для контроля
                 << "-bufsize" << "16000k";
        }
        else if (m_renderPreset == "Intel (hevc_qsv)") {
            emit logMessage("Используется пресет Intel (hevc_qsv).");
            // Добавляем опцию format=nv12 в цепочку фильтров, как в вашем пресете
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
    // Применяем собранные видео-фильтры
    if (!vf_options.isEmpty()) {
        args << "-vf" << vf_options.join(",");
    }

    // 3. Параметры аудиокодека (-c:a)
    // Мы уже имеем готовую дорожку, поэтому всегда копируем
    args << "-c:a" << "copy";

    // 4. Маппинг дорожек
    // Предполагаем, что русская дорожка вторая по счету аудиодорожка (индекс 1)
    // 0:v:0 - первая видеодорожка, 0:a:1 - вторая аудиодорожка
    args << "-map" << "0:v:0" << "-map" << "0:a:m:language:rus";

    // 5. Прочие флаги
    args << "-map_metadata" << "-1"        // Не копировать глобальные метаданные
         << "-movflags" << "+faststart"; // Для быстрой загрузки в вебе
    args << m_outputMp4Path;

    m_processManager->startProcess(m_ffmpegPath, args);
}

void WorkflowManager::finishWorkflow()
{
    emit workflowAborted();
}

void WorkflowManager::extractAttachments(const QJsonArray &attachments)
{
    emit logMessage("Шаг 7.5: Извлечение вложенных шрифтов...");
    m_currentStep = Step::ExtractingAttachments;

    m_tempFontPaths.clear();

    QDir attachmentsDir(QDir(m_savePath).filePath("attached_fonts"));
    if (!attachmentsDir.exists()) attachmentsDir.mkpath(".");

    QStringList args;
    args << m_mkvFilePath << "attachments";

    bool foundAnyFonts = false;
    for (const QJsonValue &val : attachments) {
        QJsonObject att = val.toObject();
        QString contentType = att["content_type"].toString();
        // Ищем шрифты по MIME-типу
        if (contentType.contains("font", Qt::CaseInsensitive) || contentType.contains("octet-stream", Qt::CaseInsensitive)) {
            QString id = QString::number(att["id"].toInt());
            QString fileName = att["file_name"].toString();
            // Проверяем расширение, чтобы не извлекать картинки и прочее
            if (fileName.toLower().endsWith(".ttf") || fileName.toLower().endsWith(".otf") || fileName.toLower().endsWith(".ttc")) {
                QString outputPath = attachmentsDir.filePath(fileName);
                args << QString("%1:%2").arg(id, outputPath);

                m_tempFontPaths.append(outputPath);

                foundAnyFonts = true;
            }
        }
    }

    if (foundAnyFonts) {
        emit logMessage("Найдены шрифты для извлечения: " + m_tempFontPaths.join(", "));
        emit progressUpdated(-1, "Извлечение вложений");
        m_processManager->startProcess(m_mkvextractPath, args);
    } else {
        emit logMessage("Шрифтов среди вложений не найдено. Пропускаем шаг.");
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
    }
}

void WorkflowManager::onProcessStdOut(const QString &output)
{
    // Логируем
    if (!output.trimmed().isEmpty()) {
        emit logMessage(output.trimmed());
    }

    // И парсим прогресс
    if (m_currentStep == Step::ExtractingTracks || m_currentStep == Step::ExtractingAttachments || m_currentStep == Step::AssemblingMkv)
    {
        QRegularExpression re("Progress: (\\d+)%");
        auto it = re.globalMatch(output);
        while (it.hasNext()) {
            auto match = it.next();
            int percentage = match.captured(1).toInt();
            emit progressUpdated(percentage);
        }
    }
}

void WorkflowManager::onProcessStdErr(const QString &output)
{
    // Логируем как ошибку
    if (!output.trimmed().isEmpty()) {
        emit logMessage("ЛОГ ОШИБОК ПРОЦЕССА: " + output.trimmed());
    }

    // И парсим прогресс
    if (m_currentStep == Step::RenderingMp4)
    {
        QRegularExpression re("time=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{2})");
        QRegularExpressionMatch match = re.match(output); // Используем match, т.к. ffmpeg пишет в stderr порциями
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
}

void WorkflowManager::killChildProcesses() {
    if (m_processManager) {
        m_processManager->killProcess();
    }
}

ProcessManager* WorkflowManager::getProcessManager() const {
    return m_processManager;
}

void WorkflowManager::convertToSrtAndAssembleMaster()
{
    // Проверяем, нужно ли вообще выполнять этот блок
    if (!m_template.createSrtMaster) {
        emit logMessage("Создание мастер-копии с SRT пропущено (отключено в шаблоне).");
        // Сразу переходим к основному процессу
        convertAudioIfNeeded();
        return;
    }

    emit logMessage("--- Начало сборки мастер-копии с SRT ---");
    m_currentStep = Step::AssemblingSrtMaster;

    // 1. Определяем исходный ASS для конвертации (полные субтитры без надписей)
    QString sourceAss = QDir(m_savePath).filePath("subtitles_processed_full.ass");
    if (!QFileInfo::exists(sourceAss)) {
        // Если его нет (например, были только надписи), используем файл надписей
        sourceAss = QDir(m_savePath).filePath("subtitles_processed_signs.ass");
    }
    if (!QFileInfo::exists(sourceAss)) {
        emit logMessage("Ошибка: не найдены обработанные .ass файлы для создания SRT. Сборка мастер-копии отменена.");
        // Все равно продолжаем основной процесс
        convertAudioIfNeeded();
        return;
    }

    // 2. Конвертируем в SRT
    QString outputSrtPath = QDir(m_savePath).filePath("master_subtitles.srt");
    m_assProcessor->convertToSrt(sourceAss, outputSrtPath, m_template.signStyles);

    // 3. Проверяем наличие несжатого WAV
    m_userAudioPath = m_mainWindow->getAudioPath();
    if (!m_userAudioPath.endsWith(".wav", Qt::CaseInsensitive)) {
        emit logMessage("Предупреждение: для мастер-копии не был предоставлен WAV файл. Сборка мастер-копии отменена.");
        // Продолжаем основной процесс
        convertAudioIfNeeded();
        return;
    }

    // 4. Собираем MKV
    QStringList args;
    QString outputMkvPath = QDir(m_savePath).filePath(
        QString("[DUB x TVOЁ] %1 - %2.mkv").arg(m_template.seriesTitle, m_episodeNumber)
        );
    args << "-o" << outputMkvPath;

    // Проверяем, в каком режиме мы работаем, по наличию пути к исходному MKV файлу
    if (!m_mainWindow->getManualMkvPath().isEmpty()) {
        emit logMessage("Сборка SRT-мастера из исходного MKV файла (ручной режим).");
        // Берем ТОЛЬКО видео из исходного MKV
        args << "--no-audio" << "--no-subtitles" << "--no-attachments" << m_mainWindow->getManualMkvPath();
    } else {
        emit logMessage("Сборка SRT-мастера из извлеченных дорожек (автоматический режим).");
        // Используем извлеченную видеодорожку
        QString videoPath = QDir(m_savePath).filePath("video." + m_videoTrack.extension);
        args << "--track-name" << "0:Видео" << videoPath;
    }

    // Добавляем несжатый WAV и новый SRT (это общее для обоих режимов)
    args << "--track-name" << "0:Русский (несжатый)" << m_userAudioPath;
    args << "--language" << "0:rus" << "--track-name" << "0:Русский (SRT)" << outputSrtPath;

    emit progressUpdated(0, "Сборка SRT-копии");
    m_processManager->startProcess(m_mkvmergePath, args);
}

QString WorkflowManager::getExtensionForCodec(const QString &codecId)
{
    // Видео
    if (codecId == "V_MPEG4/ISO/AVC") return "h264";
    if (codecId == "V_MPEGH/ISO/HEVC") return "h265"; // или .hevc
    if (codecId == "V_AV1") return "ivf";
    if (codecId == "V_VP8") return "ivf";
    if (codecId == "V_VP9") return "ivf";

    // Аудио
    if (codecId == "A_AAC") return "aac";
    if (codecId == "A_AC3") return "ac3";
    if (codecId == "A_EAC3") return "eac3";
    if (codecId == "A_DTS") return "dts";
    if (codecId == "A_FLAC") return "flac";
    if (codecId == "A_OPUS") return "opus";
    if (codecId == "A_VORBIS") return "ogg";
    if (codecId == "A_MP3") return "mp3";

    // Субтитры
    if (codecId == "S_TEXT/ASS") return "ass";
    if (codecId == "S_TEXT/SSA") return "ssa";
    if (codecId == "S_TEXT/UTF8") return "srt";
    if (codecId == "S_VOBSUB") return "sub";
    if (codecId == "S_HDMV/PGS") return "sup";

    // Фоллбэк для неизвестных форматов
    emit logMessage(QString("Предупреждение: неизвестный CodecID '%1', будет использовано расширение .bin").arg(codecId));
    return "bin";
}

void WorkflowManager::resumeWithMissingFiles(const QString &audioPath, const QMap<QString, QString> &resolvedFonts)
{
    // Флаг отмены. Если audioPath пуст И до этого он был пуст, значит отмена.
    if (audioPath.isEmpty() && m_userAudioPath.isEmpty()) {
        emit logMessage("Процесс прерван пользователем.");
        emit workflowAborted();
        return;
    }

    // Обновляем данные
    if (!audioPath.isEmpty()) m_userAudioPath = audioPath;

    for (auto it = resolvedFonts.constBegin(); it != resolvedFonts.constEnd(); ++it) {
        m_fontResult.foundFonts.append({it.value(), it.key()});
        m_fontResult.notFoundFontNames.removeOne(it.key());
    }

    // После получения данных от пользователя мы не знаем, на каком этапе остановились.
    // Поэтому просто вызываем следующую "большую" функцию в конвейере.
    // Мы только что закончили processSubtitles, значит, следующая - convertToSrtAndAssembleMaster.

    // ВАЖНО: Мы получили ответ на запрос, который мог быть как для WAV, так и для шрифтов.
    // Проще всего просто перезапустить логику с того шага, где мы остановились.
    // А остановились мы в processSubtitles.

    // Тогда resumeWithMissingFiles должен вызывать convertToSrtAndAssembleMaster.

    if (m_currentStep == Step::ProcessingSubs) {
        // Мы были на этапе обработки субтитров, значит, следующий шаг - мастер
        convertToSrtAndAssembleMaster();
    } else {
        // Мы были на этапе сборки, значит, продолжаем сборку
        assembleMkv(m_userAudioPath);
    }
}

void WorkflowManager::resumeWithSignStyles(const QStringList &styles)
{
    if (styles.isEmpty()) {
        emit logMessage("Не выбрано ни одного стиля для надписей. Процесс остановлен.");
        emit workflowAborted();
        return;
    }

    // Сохраняем выбранные стили в наш экземпляр шаблона
    m_template.signStyles = styles;
    emit logMessage("Стили для надписей получены. Продолжаем обработку субтитров...");

    // Теперь, когда стили есть, вызываем основной метод обработки
    processSubtitles();
}

void WorkflowManager::processSubtitles()
{

    // 1. Определяем, какие файлы субтитров у нас есть
    QString extractedSubs = QDir(m_savePath).filePath("subtitles.ass");
    QString overrideSubs = m_overrideSubsPath;
    QString overrideSigns = m_overrideSignsPath;

    bool hasExtracted = QFileInfo::exists(extractedSubs);
    bool hasOverrideSubs = !overrideSubs.isEmpty() && QFileInfo::exists(overrideSubs);
    bool hasOverrideSigns = !overrideSigns.isEmpty() && QFileInfo::exists(overrideSigns);

    // 2. Определяем время для ТБ
    QString timeForTb;
    if (m_template.useManualTime) {
        timeForTb = m_template.endingStartTime;
        emit logMessage("Используется время для ТБ, указанное вручную: " + timeForTb);
    } else {
        timeForTb = m_parsedEndingTime;
        emit logMessage("Используется время для ТБ, полученное из главы: " + timeForTb);
    }

    if (timeForTb.isEmpty()) {
        emit logMessage("КРИТИЧЕСКАЯ ОШИБКА: Время для ТБ не определено. Проверьте имя главы или укажите время вручную в шаблоне.");
        finishWorkflow();
        return;
    }

    // 3. Вызываем AssProcessor в зависимости от наличия файлов
    QString outputFileBase = QDir(m_savePath).filePath("subtitles_processed");
    bool success = false;

    if (hasOverrideSubs && hasOverrideSigns) {
        emit logMessage("Сценарий: используются свои субтитры для диалогов и свои надписи для надписей.");
        success = m_assProcessor->processFromTwoSources(overrideSubs, overrideSigns, outputFileBase, m_template, timeForTb);
    } else if (hasOverrideSubs) {
        emit logMessage("Сценарий: используются свои субтитры. Файл будет разделен на диалоги и надписи.");
        success = m_assProcessor->processExistingFile(overrideSubs, outputFileBase, m_template, timeForTb);
    } else if (hasOverrideSigns) {
        if (hasExtracted) {
            emit logMessage("Сценарий: используются извлеченные субтитры для диалогов и свои надписи.");
            success = m_assProcessor->processFromTwoSources(extractedSubs, overrideSigns, outputFileBase, m_template, timeForTb);
        } else {
            emit logMessage("Сценарий: извлеченных субтитров нет, используются только свои надписи.");
            success = m_assProcessor->processExistingFile(overrideSigns, outputFileBase, m_template, timeForTb);
        }
    } else { // Ничего не указано
        if (hasExtracted) {
            emit logMessage("Сценарий: используются извлеченные субтитры. Файл будет разделен.");
            success = m_assProcessor->processExistingFile(extractedSubs, outputFileBase, m_template, timeForTb);
        } else {
            emit logMessage("Сценарий: субтитры не найдены и не указаны. Генерируем только ТБ.");
            QString outputPath = QDir(m_savePath).filePath("subtitles_processed_signs.ass");
            success = m_assProcessor->generateTbOnlyFile(outputPath, m_template, timeForTb);
        }
    }

    // 4. Проверяем результат и двигаемся дальше по конвейеру
    if (success) {
        emit logMessage("Обработка субтитров успешно завершена.");
        emit logMessage("Генерация текстов постов...");
        EpisodeData data;
        data.episodeNumber = m_episodeNumber;
        data.cast = m_template.cast;
        emit postsReady(m_template, data);
        m_userAudioPath = m_mainWindow->getAudioPath();

        // Проверяем, нужен ли нам WAV для SRT-мастера и не был ли он уже предоставлен
        bool srtMasterWanted = m_template.createSrtMaster;
        bool wavIsMissing = m_userAudioPath.isEmpty() || !m_userAudioPath.endsWith(".wav", Qt::CaseInsensitive);

        if (srtMasterWanted && wavIsMissing && !m_wasUserInputRequested) {
            // Если хотим мастер, но нет WAV, и мы еще не спрашивали - ЗАПРАШИВАЕМ.
            // Запрашиваем только аудио, список шрифтов будет пустым.
            emit logMessage("Для сборки SRT-мастера требуется WAV файл. Запрашиваем у пользователя...");
            m_wasUserInputRequested = true; // Устанавливаем флаг, что спросили
            m_lastStepBeforeRequest = m_currentStep;
            emit missingFilesRequest({}); // Отправляем запрос без шрифтов
            return; // Прерываемся и ждем ответа от пользователя
        }

        convertToSrtAndAssembleMaster();
    } else {
        emit logMessage("Во время обработки субтитров произошла ошибка. Процесс остановлен.");
        finishWorkflow();
    }
}
