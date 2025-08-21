#include "workflowmanager.h"
#include "mainwindow.h"
#include "fontfinder.h"
#include "assprocessor.h"
#include "processmanager.h"
#include "manualrenderer.h"
#include "trackselectordialog.h"
#include "rerenderdialog.h"
#include <QDir>
#include <QFileInfo>
#include <QFontInfo>
#include <QUrlQuery>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QHttpMultiPart>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFontDatabase>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <windows.h> // Для API шрифтов


WorkflowManager::WorkflowManager(ReleaseTemplate t, const QString &episodeNumberForPost, const QString &episodeNumberForSearch, const QSettings &settings, MainWindow *mainWindow)
    : QObject(nullptr),
    m_template(t),
    m_episodeNumberForPost(episodeNumberForPost),
    m_episodeNumberForSearch(episodeNumberForSearch),
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
    m_renderPreset = AppSettings::instance().findRenderPreset(m_template.renderPresetName);
    m_customRenderArgs = settings.value("render/custom_args", "").toString();
    m_overrideSubsPath = m_mainWindow->getOverrideSubsPath();
    m_overrideSignsPath = m_mainWindow->getOverrideSignsPath();
    m_isNormalizationEnabled = m_mainWindow->isNormalizationEnabled();

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
    QString baseDownloadPath = QString("downloads/%1/Episode %2").arg(m_template.seriesTitle).arg(m_episodeNumberForSearch);

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
    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onRssDownloaded(reply); });
}

void WorkflowManager::startWithManualFile(const QString &filePath)
{
    delete m_paths;
    QString baseDownloadPath = QString("downloads/%1/Episode %2").arg(m_template.seriesTitle).arg(m_episodeNumberForSearch);
    m_paths = new PathManager(baseDownloadPath);
    emit logMessage("Структура папок создана в: " + m_paths->basePath, LogCategory::APP);

    QString newMkvPath = handleUserFile(filePath, m_paths->sourcesPath);
    if (newMkvPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не удалось переместить указанный MKV файл.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    m_mkvFilePath = newMkvPath;
    m_mainWindow->findChild<QLineEdit*>("mkvPathLineEdit")->setText(m_mkvFilePath);
    emit logMessage("Работа mkv с файлом, указанным вручную: " + m_mkvFilePath, LogCategory::APP);
    getMkvInfo();
}

void WorkflowManager::onRssDownloaded(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети (RSS): " + reply->errorString(), LogCategory::APP);
        emit workflowAborted();
        reply->deleteLater();
        return;
    }
    emit logMessage("RSS-фид успешно скачан.", LogCategory::APP);
    parseRssAndDownload(reply->readAll());
    reply->deleteLater();
}

void WorkflowManager::parseRssAndDownload(const QByteArray &rssData)
{
    emit logMessage("Шаг 1.3: Поиск подходящих торрентов в RSS...", LogCategory::APP);
    QXmlStreamReader xml(rssData);
    QList<TorrentInfo> candidates;

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
            bool hasCorrectEpisode = currentTitle.contains(QString(" - %1 ").arg(m_episodeNumberForSearch));

            // Проверка по тегам из шаблона
            bool hasTags = m_template.releaseTags.isEmpty(); // Если тегов нет, считаем, что проверка пройдена
            if (!hasTags) {
                for(const QString& tag : m_template.releaseTags) {
                    if (currentTitle.contains(tag, Qt::CaseInsensitive)) {
                        hasTags = true;
                        break;
                    }
                }
            }

            if (is1080p && isNotHevc && hasCorrectEpisode && hasTags) {
                candidates.append({currentTitle, currentLink});
            }
        }
    }

    if (candidates.isEmpty()) {
        emit logMessage("Не удалось найти подходящий торрент для серии " + m_episodeNumberForPost, LogCategory::APP);
        emit workflowAborted();
    } else if (candidates.size() == 1) {
        emit logMessage("Найден один подходящий торрент. Начинаем скачивание...", LogCategory::APP);
        m_magnetLink = candidates.first().magnetLink;
        m_torrentHash = getInfohashFromMagnet(m_magnetLink);
        checkExistingTorrents();
    } else {
        emit logMessage(QString("Найдено %1 подходящих торрентов. Требуется выбор пользователя...").arg(candidates.size()), LogCategory::APP);
        emit multipleTorrentsFound(candidates);
    }
}

void WorkflowManager::resumeWithSelectedTorrent(const TorrentInfo &selected)
{
    if (selected.magnetLink.isEmpty()) {
        emit logMessage("Выбор торрента был отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    emit logMessage("Пользователь выбрал: " + selected.title, LogCategory::APP);
    m_magnetLink = selected.magnetLink;
    m_torrentHash = getInfohashFromMagnet(m_magnetLink);
    checkExistingTorrents();
}

QString WorkflowManager::getInfohashFromMagnet(const QString &magnetLink) const
{
    // Regex для поиска infohash v1 (40 hex-символов) или v2 (64 hex-символа)
    QRegularExpression re("xt=urn:(?:btih|btmh):([a-fA-F0-9]{40,64})");
    QRegularExpressionMatch match = re.match(magnetLink);
    if (match.hasMatch()) {
        return match.captured(1).toLower();
    }
    return QString();
}

void WorkflowManager::startDownload(const QString &magnetLink)
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

    QNetworkReply *reply = m_netManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onLoginFinished(reply); });
}

void WorkflowManager::onLoginFinished(QNetworkReply *reply)
{
    // --- Блок 1: Попытка автозапуска qBittorrent ---
    // Срабатывает только если:
    // 1. Произошла ошибка отказа в соединении.
    // 2. Мы еще НЕ пытались запустить qBittorrent в этой сессии.
    if (reply->error() == QNetworkReply::ConnectionRefusedError && !m_qbtLaunchAttempted) {

        m_qbtLaunchAttempted = true;
        emit logMessage("Не удалось подключиться. Попытка запустить qBittorrent...", LogCategory::QBITTORRENT);

        QString qbtPath = AppSettings::instance().qbittorrentPath();
        if (qbtPath.isEmpty() || !QFileInfo::exists(qbtPath)) {
            emit logMessage("Путь к qbittorrent.exe не указан или неверен в настройках. Невозможно запустить автоматически.", LogCategory::APP);
            emit workflowAborted();
            reply->deleteLater();
            return;
        }

        // Запускаем qBittorrent в отдельном, независимом процессе
        bool started = QProcess::startDetached(qbtPath);

        if (!started) {
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
    if (reply->error() != QNetworkReply::NoError) {
        QString errorMessage = "Ошибка сети (Login): " + reply->errorString();
        // Добавляем более понятное сообщение, если это все еще отказ в соединении (уже после попытки запуска)
        if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            errorMessage = "Не удалось подключиться к qBittorrent. Убедитесь, что программа запущена, веб-интерфейс включен и настройки в DubbingTool верны.";
        }
        emit logMessage(errorMessage, LogCategory::QBITTORRENT);
        emit workflowAborted();
        reply->deleteLater();
        return;
    }

    // --- Блок 3: Успешное завершение (код остается без изменений) ---
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
            emit logMessage("Аутентификация успешна. Проверка статуса в клиенте...", LogCategory::QBITTORRENT);
            downloadRss();
        } else {
            emit logMessage("Ошибка: SID cookie не получен. Проверьте логин/пароль в настройках.", LogCategory::QBITTORRENT);
            emit workflowAborted();
        }
    } else {
        emit logMessage("Ошибка: ответ на аутентификацию не содержит cookie.", LogCategory::QBITTORRENT);
        emit workflowAborted();
    }

    reply->deleteLater();
}

void WorkflowManager::checkExistingTorrents()
{
    QUrl url(QString("%1:%2/api/v2/torrents/info").arg(m_webUiHost).arg(m_webUiPort));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(m_cookies));

    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentListForHashCheckReceived(reply); });
}

void WorkflowManager::addTorrent(const QString &magnetLink)
{
    emit logMessage("Шаг 1.4: Добавление торрента через Web API...", LogCategory::APP);
    m_currentStep = Step::AddingTorrent;

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
        emit logMessage("Торрент успешно добавлен в qBittorrent. Начинаем поиск хеша...", LogCategory::APP);
        findTorrentHashAndStartPolling();
    } else {
        emit logMessage("Ошибка добавления торрента: " + reply->errorString(), LogCategory::APP);
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
    if (m_hashFindAttempts >= MAX_HASH_FIND_ATTEMPTS) {
        emit logMessage("Ошибка: не удалось найти хеш торрента после нескольких попыток.", LogCategory::APP);
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

    QNetworkReply *reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentListReceived(reply); });

    // Если это не первая попытка, таймер уже запущен. Если первая, запускаем его.
    if (!m_hashFindTimer->isActive()) {
        m_hashFindTimer->start(HASH_FIND_INTERVAL_MS);
    }
}

void WorkflowManager::onTorrentListReceived(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети при получении списка торрентов: " + reply->errorString(), LogCategory::APP);
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

void WorkflowManager::onTorrentListForHashCheckReceived(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage("Ошибка сети при проверке торрентов: " + reply->errorString(), LogCategory::QBITTORRENT);
        emit logMessage("Не удалось проверить статус, переход к скачиванию нового.", LogCategory::APP);
        startDownload(m_magnetLink);
        reply->deleteLater();
        return;
    }

    QJsonArray torrents = QJsonDocument::fromJson(reply->readAll()).array();
    QJsonObject foundTorrent;
    for (const QJsonValue &value : torrents) {
        QJsonObject torrent = value.toObject();
        if (torrent["hash"].toString() == m_torrentHash) {
            foundTorrent = torrent;
            break;
        }
    }

    // --- Сценарий А: Торрент НАЙДЕН в клиенте по хешу ---
    if (!foundTorrent.isEmpty()) {
        double progress = foundTorrent["progress"].toDouble();
        QString state = foundTorrent["state"].toString();
        emit logMessage(QString("Торрент уже существует в клиенте (Состояние: %1, Прогресс: %2%).")
                            .arg(state).arg(progress * 100, 0, 'f', 1), LogCategory::APP);

        if (state == "missingFiles" || state == "error") {
            emit logMessage("Состояние торрента проблемное. Удаляем его из клиента (без файлов) и начинаем скачивание заново.", LogCategory::APP);
            deleteTorrentAndRedownload();
            reply->deleteLater();
            return;
        }

        if (progress >= 1.0) {
            emit logMessage("Торрент уже скачан. Переходим к обработке.", LogCategory::APP);
            m_savePath = foundTorrent["save_path"].toString();
            m_mkvFilePath = findMkvFileInSavePath();
            if (m_mkvFilePath.isEmpty()) {
                emit logMessage("Ошибка: торрент скачан, но MKV-файл не найден в его папке. Проверьте содержимое: " + m_savePath, LogCategory::APP);
                emit workflowAborted();
            } else {
                getMkvInfo();
            }
        } else {
            emit logMessage("Торрент не докачан. Возобновляем отслеживание.", LogCategory::APP);
            startPolling();
        }

        // --- Сценарий Б: Торрент НЕ НАЙДЕН в клиенте ---
    } else {
        emit logMessage("Торрент не найден в клиенте. Проверяем диск (в целевой папке)...", LogCategory::APP);
        m_savePath = m_paths->sourcesPath;
        m_mkvFilePath = findMkvFileInSavePath();
        if (!m_mkvFilePath.isEmpty()) {
            emit logMessage("Найден готовый файл на диске. Переходим к обработке.", LogCategory::APP);
            getMkvInfo();
        } else {
            emit logMessage("Ничего не найдено. Начинаем скачивание.", LogCategory::APP);
            startDownload(m_magnetLink);
        }
    }

    reply->deleteLater();
}

QString WorkflowManager::findMkvFileInSavePath()
{
    QDir dir(m_savePath);
    if (!dir.exists()) return QString();

    dir.setNameFilters({"*.mkv"});
    QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Name);

    for (const QFileInfo &fileInfo : list) {
        if (!fileInfo.fileName().startsWith("[DUB")) {
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

    QNetworkReply *reply = m_netManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply](){ onTorrentDeleted(reply); });
}

void WorkflowManager::onTorrentDeleted(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        emit logMessage("Старый торрент успешно удален.", LogCategory::QBITTORRENT);
    } else {
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
        emit logMessage("Ошибка: не удалось найти .mkv файл в торренте.", LogCategory::APP);
        emit workflowAborted();
    } else {
        m_mkvFilePath = m_paths->sourceMkv(mainFileName);
        emit logMessage("Найден основной файл: " + m_mkvFilePath, LogCategory::APP);
        getMkvInfo();
    }
    reply->deleteLater();
}

void WorkflowManager::getMkvInfo()
{
    prepareUserFiles();

    emit logMessage("Шаг 2: Получение информации о файле MKV...", LogCategory::APP);

    m_currentStep = Step::GettingMkvInfo;

    if (!m_template.endingChapterName.isEmpty()) {
        m_parsedEndingTime = parseChaptersWithMkvExtract();
        if (!m_parsedEndingTime.isEmpty()) {
            emit logMessage(QString("Найдена глава '%1', время начала: %2").arg(m_template.endingChapterName, m_parsedEndingTime), LogCategory::APP);
        } else {
            emit logMessage(QString("ПРЕДУПРЕЖДЕНИЕ: Глава с именем '%1' не найдена в файле. Проверьте правильность названия в шаблоне.").arg(m_template.endingChapterName), LogCategory::APP);
        }
    }

    QByteArray jsonData;
    bool success = m_processManager->executeAndWait(m_mkvmergePath, {"-J", m_mkvFilePath}, jsonData);

    if (success) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonObject root = doc.object();
        QJsonArray tracks = root["tracks"].toArray();
        if (root.contains("container")) {
            m_sourceDurationS = root["container"].toObject()["properties"].toObject()["duration"].toDouble() / 1000000000.0;
        }

        QMap<QString, QString> languageAliases;
        languageAliases["zho"] = "zh";
        languageAliases["chi"] = "zh";
        languageAliases["cmn"] = "zh";
        languageAliases["jpn"] = "ja";
        languageAliases["jp"]  = "ja";
        languageAliases["eng"] = "en";

        m_foundAudioTracks.clear();
        for (const QJsonValue &val : tracks) {
            QJsonObject track = val.toObject();
            QJsonObject props = track["properties"].toObject();
            QString codecId = props["codec_id"].toString();

            QString trackLanguage = props["language"].toString();
            QString templateLanguage = m_template.originalLanguage;

            bool languageMatch = false;
            if (!trackLanguage.isEmpty() && !templateLanguage.isEmpty()) {
                // 1. Прямое совпадение (например, "jpn" == "jpn")
                if (trackLanguage == templateLanguage) {
                    languageMatch = true;
                }
                // 2. Проверка по карте псевдонимов (например, trackLanguage="zho", templateLanguage="zh")
                else if (languageAliases.contains(trackLanguage) && languageAliases.value(trackLanguage) == templateLanguage) {
                    languageMatch = true;
                }
                // 3. "Фоллбэк" на startsWith (например, "jpn".startsWith("jp"))
                else if (trackLanguage.startsWith(templateLanguage)) {
                    languageMatch = true;
                }
            }

            if (track["type"].toString() == "video" && m_videoTrack.id == -1) {
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
            else if (track["type"].toString() == "subtitles" && props["language"].toString() == "rus" && m_subtitleTrack.id == -1)
            {
                m_subtitleTrack.id = track["id"].toInt();
                m_subtitleTrack.codecId = codecId;
                m_subtitleTrack.extension = getExtensionForCodec(codecId);
            }
        }

        if (m_foundAudioTracks.isEmpty()) {
            emit logMessage("Предупреждение: не найдено ни одной аудиодорожки на языке '" + m_template.originalLanguage + "'. Оригинал не будет добавлен в сборку.", LogCategory::APP);
        } else if (m_foundAudioTracks.size() == 1) {
            const auto& track = m_foundAudioTracks.first();
            m_originalAudioTrack.id = track.id;
            m_originalAudioTrack.codecId = track.codec;
            m_originalAudioTrack.extension = getExtensionForCodec(track.codec);
            emit logMessage(QString("Найдена одна оригинальная аудиодорожка (ID: %1).").arg(track.id), LogCategory::APP);
        } else {
            emit logMessage(QString("Найдено %1 оригинальных аудиодорожек. Требуется выбор пользователя...").arg(m_foundAudioTracks.size()), LogCategory::APP);
            emit multipleAudioTracksFound(m_foundAudioTracks);
            return;
        }

        if (root.contains("attachments")) {
            extractAttachments(root["attachments"].toArray());
        } else {
            emit logMessage("Вложений в файле не найдено.", LogCategory::APP);
            m_currentStep = Step::ExtractingAttachments; // Имитируем завершение этого шага
            QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
        }
    } else {
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
    if (!m_processManager->executeAndWait(m_mkvextractPath, args, dummyOutput)) {
        emit logMessage("Ошибка при извлечении глав с помощью mkvextract.", LogCategory::APP);
        QFile::remove(chaptersFilePath);
        return QString();
    }

    QFile chaptersFile(chaptersFilePath);
    if (!chaptersFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage("Не удалось открыть временный файл с главами.", LogCategory::APP);
        QFile::remove(chaptersFilePath);
        return QString();
    }

    QString firstLine = chaptersFile.readLine();
    chaptersFile.seek(0);

    QString foundTime;

    if (firstLine.trimmed().startsWith("<?xml")) {
        emit logMessage("Обнаружен XML-формат глав. Запуск XML-парсера...", LogCategory::APP);
        QXmlStreamReader xml(&chaptersFile);
        QString currentTimeStart, currentChapterString;

        while (!xml.atEnd() && !xml.hasError()) {
            QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (xml.name() == QLatin1String("ChapterAtom")) {
                    currentTimeStart.clear();
                    currentChapterString.clear();
                } else if (xml.name() == QLatin1String("ChapterTimeStart")) {
                    currentTimeStart = xml.readElementText();
                } else if (xml.name() == QLatin1String("ChapterString")) {
                    currentChapterString = xml.readElementText();
                }
            }
            if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("ChapterAtom")) {
                if (currentChapterString.trimmed() == m_template.endingChapterName) {
                    foundTime = currentTimeStart.trimmed();
                    break;
                }
            }
        }
        if (xml.hasError()) {
            emit logMessage("Ошибка парсинга XML глав: " + xml.errorString(), LogCategory::APP);
        }

    } else {
        emit logMessage("Обнаружен простой формат глав. Запуск текстового парсера...", LogCategory::APP);
        QTextStream in(&chaptersFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.contains(m_template.endingChapterName, Qt::CaseInsensitive)) {
                QRegularExpression nameRegex("CHAPTER(\\d+)NAME");
                auto nameMatch = nameRegex.match(line);
                if (nameMatch.hasMatch()) {
                    QString chapterNumber = nameMatch.captured(1);
                    in.seek(0);
                    while(!in.atEnd()) {
                        QString timeLine = in.readLine();
                        if (timeLine.startsWith(QString("CHAPTER%1=").arg(chapterNumber))) {
                            foundTime = timeLine.split('=').last();
                            break;
                        }
                    }
                    if (!foundTime.isEmpty()) break;
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
    emit logMessage("Шаг 4: Извлечение дорожек...", LogCategory::APP);
    m_currentStep = Step::ExtractingTracks;

    if (m_videoTrack.id == -1) {
        emit logMessage("Критическая ошибка: в файле отсутствует видеодорожка. Извлечение невозможно.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    QStringList args;
    args << m_mkvFilePath << "tracks";

    QString videoOutPath = m_paths->extractedVideo(m_videoTrack.extension);
    args << QString("%1:%2").arg(m_videoTrack.id).arg(videoOutPath);

    if (m_originalAudioTrack.id != -1) {
        QString audioOutPath = m_paths->extractedAudio(m_originalAudioTrack.extension);
        args << QString("%1:%2").arg(m_originalAudioTrack.id).arg(audioOutPath);
    }

    if (m_template.sourceHasSubtitles && m_overrideSubsPath.isEmpty()) {
        if (m_subtitleTrack.id != -1) {
            QString subsOutPath = m_paths->extractedSubs(m_subtitleTrack.extension);
            args << QString("%1:%2").arg(m_subtitleTrack.id).arg(subsOutPath);
        } else {
            emit logMessage("Предупреждение: в файле не найдены русские субтитры, хотя в шаблоне они ожидались.", LogCategory::APP);
        }
    } else if (!m_overrideSubsPath.isEmpty()) {
        emit logMessage("Пропускаем извлечение встроенных субтитров, так как указаны внешние файлы.", LogCategory::APP);
    }

    emit progressUpdated(-1, "Извлечение дорожек");
    m_processManager->startProcess(m_mkvextractPath, args);
}

void WorkflowManager::cancelOperation()
{
    emit logMessage("Получена команда на отмену операции...", LogCategory::APP);

    if (m_processManager) {
        m_processManager->killProcess();
    }
    switch (m_currentStep) {
    case Step::Polling:
    case Step::AddingTorrent:
        // Если мы скачиваем торрент, просто останавливаем таймеры и завершаем работу.
        // Мы не можем "отменить" скачивание в qBittorrent, но мы можем прекратить его отслеживать.
        emit logMessage("Отслеживание торрента прервано пользователем.", LogCategory::APP);
        if (m_progressTimer && m_progressTimer->isActive()) {
            m_progressTimer->stop();
        }
        if (m_hashFindTimer && m_hashFindTimer->isActive()) {
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
    if (m_processManager && m_processManager->wasKilled()) {
        emit logMessage("Операция успешно отменена пользователем.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit logMessage("Ошибка выполнения дочернего процесса. Рабочий процесс остановлен.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    switch (m_currentStep)
    {
        case Step::ExtractingAttachments:
        {
            emit logMessage("Извлечение вложений завершено.", LogCategory::APP);
            extractTracks();
            break;
        }
        case Step::ExtractingTracks:
        {
            emit logMessage("Извлечение дорожек завершено.", LogCategory::APP);
            // --- Шаг 4.5: Автозамены и пауза для ручной правки ---
            QString extractedSubsPath = m_paths->extractedSubs("ass");
            if (QFileInfo::exists(extractedSubsPath)) {
                m_assProcessor->applySubstitutions(extractedSubsPath, m_template.substitutions);
                if (m_template.pauseForSubEdit) {
                    emit logMessage("Процесс приостановлен. Ожидание ручного редактирования субтитров...", LogCategory::APP);
                    emit pauseForSubEditRequest(extractedSubsPath);
                    return;
                }
            }
            audioPreparation();
            break;
        }
        case Step::_NormalizingAudio:
        {
            emit logMessage("Нормализация аудио завершена.", LogCategory::APP);
            if (m_didLaunchNugen) {
                emit logMessage("Закрытие NUGEN Audio AMB...", LogCategory::APP);
                QProcess::execute("taskkill", {"/F", "/IM", "NUGEN Audio AMB.exe", "/T"});
                m_didLaunchNugen = false;
            }

            QString originalPath = (m_lastStepBeforeRequest == Step::AssemblingSrtMaster) ? m_wavForSrtMasterPath : m_mainRuAudioPath;
            QFileInfo originalInfo(originalPath);
            QString normalizedPath = originalInfo.dir().filePath(originalInfo.baseName() + "_corrected.wav");

            if (QFileInfo::exists(normalizedPath)) {
                emit logMessage("Найден нормализованный файл: " + normalizedPath, LogCategory::APP);
                if(m_lastStepBeforeRequest == Step::AssemblingSrtMaster) {
                    m_wavForSrtMasterPath = normalizedPath;
                } else {
                    m_mainRuAudioPath = normalizedPath;
                    m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
                }
                m_wasNormalizationPerformed = true;
            } else {
                emit logMessage("ОШИБКА: Нормализованный файл не найден.", LogCategory::APP);
                emit workflowAborted();
            }
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
            emit logMessage("Конвертация аудио успешно завершена.", LogCategory::APP);
            emit progressUpdated(100);
            assembleMkv(m_finalAudioPath);
            break;
        }
        case Step::AssemblingMkv:
        {
            emit logMessage("Финальный MKV файл успешно собран.", LogCategory::APP);
            if (AppSettings::instance().deleteTempFiles()) {
                emit logMessage("Удаление временных файлов...", LogCategory::APP);
                QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
                QString audioPath = m_paths->extractedAudio(m_originalAudioTrack.extension);
                bool videoRemoved = QFile::remove(videoPath);
                bool audioRemoved = QFile::remove(audioPath);
                if(videoRemoved) emit logMessage(" -> Временное видео удалено: " + videoPath, LogCategory::APP);
                if(audioRemoved) emit logMessage(" -> Временное аудио удалено: " + audioPath, LogCategory::APP);
            }

            emit mkvFileReady(m_finalMkvPath);
            renderMp4();
            break;
        }
        case Step::RenderingMp4Pass1:
        {
            if (exitCode != 0) {
                emit logMessage("Ошибка рендера. Рабочий процесс остановлен.", LogCategory::APP);
                emit workflowAborted();
                return;
            }

            if (m_renderPreset.isTwoPass()) {
                emit logMessage("Первый проход рендера успешно завершен. Запуск второго прохода.", LogCategory::APP);
                m_currentStep = Step::RenderingMp4Pass2;
                emit progressUpdated(50, "Рендер MP4 (проход 2/2)");
                runRenderPass(m_currentStep);
            } else {
                emit logMessage("Рендер MP4 успешно завершен (один проход).", LogCategory::APP);
                RenderHelper* helper = new RenderHelper(m_renderPreset, m_outputMp4Path, m_processManager, this);
                connect(helper, &RenderHelper::logMessage, this, &WorkflowManager::logMessage);
                connect(helper, &RenderHelper::finished, this, &WorkflowManager::onBitrateCheckFinished);
                connect(helper, &RenderHelper::showDialogRequest, this, &WorkflowManager::bitrateCheckRequest);
                helper->startCheck();
            }
            break;
        }
        case Step::RenderingMp4Pass2:
        {
            if (exitCode != 0) {
                emit logMessage("Ошибка второго прохода рендера. Рабочий процесс остановлен.", LogCategory::APP);
                emit workflowAborted();
                return;
            }

            emit logMessage("Второй проход и рендер MP4 успешно завершены.", LogCategory::APP);
            RenderHelper* helper = new RenderHelper(m_renderPreset, m_outputMp4Path, m_processManager, this);
            connect(helper, &RenderHelper::logMessage, this, &WorkflowManager::logMessage);
            connect(helper, &RenderHelper::finished, this, &WorkflowManager::onBitrateCheckFinished);
            connect(helper, &RenderHelper::showDialogRequest, this, &WorkflowManager::bitrateCheckRequest);
            helper->startCheck();
            break;
        }
        default:
            break;
    }
}

void WorkflowManager::finishWorkflow()
{
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

    UserInputRequest request;
    if (m_mainRuAudioPath.isEmpty()) {
        request.audioFileRequired = true;
    }

    bool mainAudioIsWav = m_mainRuAudioPath.endsWith(".wav", Qt::CaseInsensitive);
    if (m_template.createSrtMaster && !mainAudioIsWav && m_wavForSrtMasterPath.isEmpty()) {
        request.audioFileRequired = true;
        request.isWavRequired = true;
    }

    QString timeForTb;
    if (m_template.useManualTime) {
        timeForTb = m_template.endingStartTime;
        emit logMessage("Используется время для ТБ, указанное вручную: " + timeForTb, LogCategory::APP);
    } else if (!m_parsedEndingTime.isEmpty()) {
        timeForTb = m_parsedEndingTime;
        emit logMessage("Используется время для ТБ: " + timeForTb, LogCategory::APP);
    }

    if (timeForTb.isEmpty()) {
        request.tbTimeRequired = true;
        request.tbTimeReason = "Время эндинга не определено. Укажите его вручную.";
    } else {
        QTime tbStartTime = QTime::fromString(timeForTb, "H:mm:ss.zzz");
        if (tbStartTime.isValid() && m_sourceDurationS > 0) {
            double tbStartTimeS = tbStartTime.hour() * 3600 + tbStartTime.minute() * 60 + tbStartTime.second() + tbStartTime.msec() / 1000.0;
            double remainingTimeS = m_sourceDurationS - tbStartTimeS;
            int tbLines = AssProcessor::calculateTbLineCount(m_template);
            double requiredTimeS = tbLines * 3.0;

            if (remainingTimeS < requiredTimeS) {
                request.tbTimeRequired = true;
                request.tbTimeReason = QString("Недостаточно времени для ТБ! Требуется: ~%1 сек. Осталось: ~%2 сек.").arg(qRound(requiredTimeS)).arg(qRound(remainingTimeS));
            } else {
                emit logMessage(QString("Проверка времени для ТБ пройдена. Требуется: ~%1 сек. Осталось: ~%2 сек.").arg(qRound(requiredTimeS)).arg(qRound(remainingTimeS)), LogCategory::APP);
            }
        }
    }

    if (request.isValid()) {
        emit logMessage("Недостаточно аудиоданных. Запрос у пользователя...", LogCategory::APP);
        m_lastStepBeforeRequest = Step::AudioPreparation;
        emit userInputRequired(request);
        return;
    }

    if (!m_isNormalizationEnabled || m_wasNormalizationPerformed) {
        processSubtitles();
        return;
    }

    QString nugenPath = AppSettings::instance().nugenAmbPath();
    if (nugenPath.isEmpty()) {
        processSubtitles();
        return;
    }

    QString targetWavPath;
    if (!m_wavForSrtMasterPath.isEmpty()) {
        targetWavPath = m_wavForSrtMasterPath;
        m_lastStepBeforeRequest = Step::AssemblingSrtMaster;
    } else if (mainAudioIsWav) {
        targetWavPath = m_mainRuAudioPath;
        m_lastStepBeforeRequest = Step::AudioPreparation;
    }

    if (targetWavPath.isEmpty()) {
        processSubtitles();
        return;
    }
    emit logMessage("Шаг 5: Подготовка аудио...", LogCategory::APP);

    m_currentStep = Step::_NormalizingAudio;
    QFileInfo nugenInfo(nugenPath);
    QString ambCmdPath = nugenInfo.dir().filePath("AMBCmd.exe");

    if (!QFileInfo::exists(ambCmdPath)) {
        emit logMessage("Ошибка: AMBCmd.exe не найден. Шаг нормализации пропущен.", LogCategory::APP);
        processSubtitles();
        return;
    }

    emit logMessage("Запуск GUI NUGEN AMB в фоновом режиме...", LogCategory::APP);
    emit progressUpdated(-1, "Запуск NUGEN Audio AMB");
    QProcess::startDetached(nugenPath);
    m_didLaunchNugen = true;

    QTimer::singleShot(3000, this, [this, ambCmdPath, targetWavPath](){
        emit logMessage("Запуск AMBCmd для обработки файла: " + targetWavPath, LogCategory::APP);
        m_processManager->startProcess(ambCmdPath, {"-a", targetWavPath});
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

    if (QFileInfo::exists(fullSubsPath)) {
        subFilesToCheck << fullSubsPath;
    } else if (QFileInfo::exists(signsSubsPath)) {
        subFilesToCheck << signsSubsPath;
    }

    if (subFilesToCheck.isEmpty()) {
        emit logMessage("Файлы субтитров для анализа не найдены. Пропускаем поиск шрифтов.", LogCategory::APP);
        QMetaObject::invokeMethod(this, "onFontFinderFinished", Qt::QueuedConnection, Q_ARG(FontFinderResult, FontFinderResult()));
    } else {
        m_fontFinder->findFontsInSubs(subFilesToCheck);
    }
}

void WorkflowManager::convertAudioIfNeeded()
{
    emit logMessage("Шаг 8: Проверка и конвертация аудио...", LogCategory::APP);
    
    if (m_mainRuAudioPath.isEmpty()) {
        emit logMessage("КРИТИЧЕСКАЯ ОШИБКА: Аудиофайл отсутствует на шаге конвертации.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    QString targetFormat = m_template.targetAudioFormat;
    emit logMessage(QString("Целевой формат аудио: %1").arg(targetFormat), LogCategory::APP);

    if (m_mainRuAudioPath.endsWith("." + targetFormat, Qt::CaseInsensitive)) {
        emit logMessage("Аудиофайл уже в целевом формате. Конвертация не требуется.", LogCategory::APP);
        m_finalAudioPath = m_mainRuAudioPath;
        assembleMkv(m_finalAudioPath);
        return;
    }

    m_currentStep = Step::ConvertingAudio;
    m_finalAudioPath = m_paths->convertedRuAudio(targetFormat);
    emit logMessage(QString("Запуск конвертации в %1...").arg(targetFormat.toUpper()), LogCategory::APP);

    m_ffmpegProgressFile = QDir(m_paths->ruAudioPath).filePath("ffmpeg_progress.log");

    QStringList args;
    args << "-y" << "-i" << m_mainRuAudioPath;

    if (targetFormat == "aac") {
        args << "-c:a" << "aac" << "-b:a" << "256k";
    } else if (targetFormat == "flac") {
        args << "-c:a" << "flac";
    } else {
        emit logMessage(QString("Ошибка: неизвестный целевой формат аудио '%1'").arg(targetFormat), LogCategory::APP);
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
    emit logMessage("Шаг 9: Проверка компонентов для сборки MKV...", LogCategory::APP);
    m_currentStep = Step::AssemblingMkv;

    QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
    QString originalAudioPath = m_paths->extractedAudio(m_originalAudioTrack.extension);
    QString fullSubsPath = m_paths->processedFullSubs();
    QString signsPath = m_paths->processedSignsSubs();


    UserInputRequest request;
    if (!m_fontResult.notFoundFontNames.isEmpty()) {
        request.missingFonts = m_fontResult.notFoundFontNames;
    }
    if (m_mainRuAudioPath.isEmpty()) {
        request.audioFileRequired = true;
    }

    if (request.isValid()) {
        emit logMessage("Недостаточно файлов для сборки MKV. Запрос у пользователя...", LogCategory::APP);
        m_lastStepBeforeRequest = Step::AssemblingMkv;
        emit userInputRequired(request);
        return;
    }

    if (m_mainRuAudioPath.isEmpty()) {
        emit logMessage("Критическая ошибка: не указан путь к русской аудиодорожке. Сборка невозможна.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    emit logMessage("Все необходимые файлы на месте. Начинаем сборку mkvmerge...", LogCategory::APP);

    QStringList fontPaths;
    for (const FoundFontInfo &fontInfo : m_fontResult.foundFonts) {
        fontPaths.append(fontInfo.path);
    }
    fontPaths.removeDuplicates();

    if (!m_fontResult.notFoundFontNames.isEmpty()) {
        emit logMessage("ПРЕДУПРЕЖДЕНИЕ: Не все шрифты были предоставлены. Субтитры могут отображаться некорректно.", LogCategory::APP);
        emit logMessage("Пропущены: " + m_fontResult.notFoundFontNames.join(", "), LogCategory::APP);
    }

    QString outputFileName = QString("[DUB] %1 - %2.mkv").arg(m_template.seriesTitle).arg(m_episodeNumberForSearch);
    m_finalMkvPath  = QDir(m_paths->resultPath).absoluteFilePath(outputFileName);

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

    // args << "--chapters" << m_mkvFilePath;

    // Названия из шаблона
    QString animStudio = m_template.animationStudio;
    QString subAuthor = m_template.subAuthor;

    // Дорожка видео
    args << "--language" << "0:" + m_template.originalLanguage << "--track-name" << QString("0:Видеоряд [%1]").arg(animStudio) << videoPath;

    // Дорожка русского аудио (с флагом по умолчанию)
    args << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << "0:Русский [Дубляжная]" << russianAudioPath;

    // Дорожка оригинального аудио
    args << "--default-track-flag" << "0:no" << "--language" << "0:" + m_template.originalLanguage << "--track-name" << QString("0:Оригинал [%1]").arg(animStudio) << originalAudioPath;

    QString subTrackAuthorName = m_template.isCustomTranslation ? "Дубляжная" : subAuthor;

    // Дорожки субтитров (если они существуют)
    if (QFileInfo::exists(signsPath)) {
        args << "--forced-display-flag" << "0:yes" << "--default-track-flag" << "0:yes" << "--language" << "0:rus" << "--track-name" << QString("0:Надписи [%1]").arg(subTrackAuthorName) << signsPath;
    }
    if (QFileInfo::exists(fullSubsPath)) {
        args << "--default-track-flag" << "0:no" << "--language" << "0:rus" << "--track-name" << QString("0:Субтитры [%1]").arg(subTrackAuthorName) << fullSubsPath;
    }

    emit progressUpdated(-1, "Сборка MKV");
    m_processManager->startProcess(m_mkvmergePath, args);
}

void WorkflowManager::renderMp4()
{
    emit logMessage("Шаг 10: Рендер финального MP4 файла...", LogCategory::APP);
    emit progressUpdated(-1, "Рендер MP4");
    m_renderPreset = AppSettings::instance().findRenderPreset(m_template.renderPresetName);
    if (m_renderPreset.name.isEmpty()) {
        emit logMessage("Критическая ошибка: пресет рендера '" + m_template.renderPresetName + "' не найден. Процесс остановлен.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    m_outputMp4Path = m_finalMkvPath;
    m_outputMp4Path.replace(".mkv", ".mp4");

    m_currentStep = Step::RenderingMp4Pass1;
    runRenderPass(m_currentStep);
}

void WorkflowManager::runRenderPass(Step pass)
{
    QString commandTemplate = (pass == Step::RenderingMp4Pass1) ? m_renderPreset .commandPass1 : m_renderPreset .commandPass2;

    QStringList args = prepareCommandArguments(commandTemplate);
    if (args.isEmpty()) {
        emit logMessage("Ошибка: не удалось подготовить команду для рендера.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    QString program = args.takeFirst();

    emit logMessage(QString("Запуск прохода: ") + program + " " + args.join(" "), LogCategory::APP);
    m_processManager->startProcess(program, args);
}

QStringList WorkflowManager::prepareCommandArguments(const QString& commandTemplate)
{
    QString processedTemplate = commandTemplate;

    // Плейсхолдеры для путей
    QString inputMkv = m_finalMkvPath;
    QString outputMp4 = m_outputMp4Path;

    processedTemplate.replace("%INPUT%", inputMkv);
    processedTemplate.replace("%OUTPUT%", outputMp4);

    // Плейсхолдер для фильтра
    bool useHardsub = QFileInfo::exists(m_paths->processedSignsSubs());

    if (useHardsub) {
        QString signsPath = m_paths->processedSignsSubs();
        processedTemplate.replace("%SIGNS%", "'" + escapePathForFfmpegFilter(signsPath) + "'");
    } else {
        // Если hardsub не нужен, надо аккуратно удалить фильтр
        QRegularExpression filter_regex(R"(-vf\s+\"[^\"]*subtitles=%SIGNS%[^\"]*\")");
        processedTemplate.remove(filter_regex);
    }

    // Используем QProcess для корректного разбора строки
    return QProcess::splitCommand(processedTemplate);
}

void WorkflowManager::extractAttachments(const QJsonArray &attachments)
{
    emit logMessage("Шаг 3: Извлечение вложенных шрифтов...", LogCategory::APP);
    m_currentStep = Step::ExtractingAttachments;

    m_tempFontPaths.clear();

    QDir attachmentsDir(m_paths->attachedFontsDir());
    if (!attachmentsDir.exists()) attachmentsDir.mkpath(".");

    QStringList args;
    args << m_mkvFilePath << "attachments";
    QStringList logFontNames;

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
                logFontNames.append(fileName);
                foundAnyFonts = true;
            }
        }
    }

    if (foundAnyFonts) {
        emit logMessage("Найдены шрифты для извлечения: " + logFontNames.join(", "), LogCategory::APP);
        emit progressUpdated(-1, "Извлечение вложений");
        m_processManager->startProcess(m_mkvextractPath, args);
    } else {
        emit logMessage("Шрифтов среди вложений не найдено. Пропускаем шаг.", LogCategory::APP);
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
    }
}

void WorkflowManager::onProcessStdOut(const QString &output)
{
    if (!output.trimmed().isEmpty()) {
        LogCategory category = LogCategory::DEBUG;
        switch (m_currentStep) {
        case Step::AssemblingMkv:
        case Step::ExtractingTracks:
        case Step::ExtractingAttachments:
        case Step::AssemblingSrtMaster:
        case Step::GettingMkvInfo:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ConvertingAudio:
        case Step::RenderingMp4Pass1:
        case Step::RenderingMp4Pass2:
            category = LogCategory::FFMPEG;
            break;

        default:
            break;
        }
        emit logMessage(output, category);
    }

    if (m_currentStep == Step::ExtractingTracks || m_currentStep == Step::ExtractingAttachments || m_currentStep == Step::AssemblingMkv || m_currentStep == Step::AssemblingSrtMaster)
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
    if (!output.trimmed().isEmpty()) {
        LogCategory category = LogCategory::DEBUG;
        switch (m_currentStep) {
        case Step::AssemblingMkv:
        case Step::ExtractingTracks:
        case Step::ExtractingAttachments:
        case Step::AssemblingSrtMaster:
        case Step::GettingMkvInfo:
            category = LogCategory::MKVTOOLNIX;
            break;

        case Step::ConvertingAudio:
        case Step::RenderingMp4Pass1:
        case Step::RenderingMp4Pass2:
            category = LogCategory::FFMPEG;
            break;

        default:
            break;
        }
        emit logMessage("STDERR: " + output, category);
    }

    // И парсим прогресс
    if (m_currentStep == Step::RenderingMp4Pass1 || m_currentStep == Step::RenderingMp4Pass2)
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
    if (!m_template.createSrtMaster) {
        emit logMessage("Создание мастер-копии с SRT пропущено (отключено в шаблоне).", LogCategory::APP);
        convertAudioIfNeeded();
        return;
    }

    emit logMessage("--- Начало сборки мастер-копии с SRT ---", LogCategory::APP);
    m_currentStep = Step::AssemblingSrtMaster;

    // 1. Определяем исходный ASS для конвертации (полные субтитры)
    QString sourceAss = m_paths->processedFullSubs();
    if (!QFileInfo::exists(sourceAss)) {
        emit logMessage("Ошибка: не найдены обработанные .ass файлы для создания SRT. Сборка мастер-копии отменена.", LogCategory::APP);
        convertAudioIfNeeded();
        return;
    }

    // 2. Конвертируем в SRT
    QString outputSrtPath = m_paths->masterSrt();
    m_assProcessor->convertToSrt(sourceAss, outputSrtPath, m_template.signStyles);

    // 3. Собираем MKV
    QStringList args;
    QString outputMkvPath = m_paths->masterMkv(QString("[DUB x TVOЁ] %1 - %2.mkv").arg(m_template.seriesTitle, m_episodeNumberForSearch));
    args << "-o" << outputMkvPath;

    QString videoPath = m_paths->extractedVideo(m_videoTrack.extension);
    args << videoPath;

    QString wavPath = m_wavForSrtMasterPath.isEmpty() ? m_mainRuAudioPath : m_wavForSrtMasterPath;
    args << "--language" << "0:rus" << wavPath;
    args << "--language" << "0:rus" << outputSrtPath;

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
    emit logMessage(QString("Предупреждение: неизвестный CodecID '%1', будет использовано расширение .bin").arg(codecId), LogCategory::APP);
    return "bin";
}

void WorkflowManager::resumeWithUserInput(const UserInputResponse &response)
{
    if (!response.isValid()) {
        emit logMessage("Процесс прерван пользователем в диалоге выбора файлов.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    m_wasUserInputRequested = false;
    emit logMessage("Данные от пользователя получены. Возобновление работы...", LogCategory::APP);

    if (!response.audioPath.isEmpty()) {
        emit logMessage("Пользователь предоставил аудиофайл. Обработка...", LogCategory::APP);
        QString newAudioPath = handleUserFile(response.audioPath, m_paths->ruAudioPath);
        if (!newAudioPath.isEmpty()) {
            if (m_mainRuAudioPath.isEmpty()) {
                m_mainRuAudioPath = newAudioPath;
                m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
            } else {
                m_wavForSrtMasterPath = newAudioPath;
            }
        }
    } else if (m_lastStepBeforeRequest == Step::AudioPreparation && m_template.createSrtMaster && !m_mainRuAudioPath.endsWith(".wav")) {
        emit logMessage("WAV для мастер-копии не предоставлен. Сборка мастер-копии отменена.", LogCategory::APP);
        m_template.createSrtMaster = false;
    }

    if (!response.time.isEmpty() && response.time != "0:00:00.000") {
        m_parsedEndingTime = response.time;
    }

    if (!response.resolvedFonts.isEmpty()) {
        for (auto it = response.resolvedFonts.constBegin(); it != response.resolvedFonts.constEnd(); ++it) {
            m_fontResult.foundFonts.append({it.value(), it.key()});
            m_fontResult.notFoundFontNames.removeOne(it.key());
        }
    }

    if        (m_lastStepBeforeRequest == Step::AudioPreparation) {
        audioPreparation();
    } else if (m_lastStepBeforeRequest == Step::ProcessingSubs) {
        processSubtitles();
    } else if (m_lastStepBeforeRequest == Step::AssemblingMkv) {
        assembleMkv(m_mainRuAudioPath);
    }
}

void WorkflowManager::resumeWithSignStyles(const QStringList &styles)
{
    if (styles.isEmpty()) {
        emit logMessage("Не выбрано ни одного стиля для надписей. Процесс остановлен.", LogCategory::APP);
        emit workflowAborted();
        return;
    }
    m_template.signStyles = styles;
    emit logMessage("Стили для надписей получены. Продолжаем...", LogCategory::APP);
    m_wasUserInputRequested = false;
    processSubtitles();
}

void WorkflowManager::resumeWithSelectedAudioTrack(int trackId)
{
    if (trackId < 0) {
        emit logMessage("Выбор аудиодорожки отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    bool found = false;
    for (const auto& track : m_foundAudioTracks) {
        if (track.id == trackId) {
            m_originalAudioTrack.id = track.id;
            m_originalAudioTrack.codecId = track.codec;
            m_originalAudioTrack.extension = getExtensionForCodec(track.codec);
            found = true;
            break;
        }
    }

    if (!found) {
        emit logMessage("Критическая ошибка: ID выбранной дорожки не найден. Процесс прерван.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    emit logMessage("Пользователь выбрал аудиодорожку с ID: " + QString::number(trackId) + ". Продолжаем...", LogCategory::APP);

    QByteArray jsonData;
    m_processManager->executeAndWait(m_mkvmergePath, {"-J", m_mkvFilePath}, jsonData);
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.object().contains("attachments")) {
        extractAttachments(doc.object()["attachments"].toArray());
    } else {
        emit logMessage("Вложений в файле не найдено.", LogCategory::APP);
        m_currentStep = Step::ExtractingAttachments;
        QMetaObject::invokeMethod(this, "onProcessFinished", Qt::QueuedConnection, Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
    }
}

void WorkflowManager::processSubtitles()
{
    emit logMessage("Шаг 6: Обработка субтитров...", LogCategory::APP);
    m_currentStep = Step::ProcessingSubs;

    QString subsToAnalyze;
    if ((m_template.signStyles.isEmpty() || m_template.forceSignStyleRequest) && !m_wereStylesRequested) {
        if (!m_overrideSubsPath.isEmpty()) subsToAnalyze = m_overrideSubsPath;
        else {
            QString extractedSubs = m_paths->extractedSubs("ass");
            if (QFileInfo::exists(extractedSubs)) subsToAnalyze = extractedSubs;
        }
        if (!subsToAnalyze.isEmpty()) {
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
    QString extractedSubs = m_paths->extractedSubs("ass");
    QString overrideSubs = m_overrideSubsPath;
    QString overrideSigns = m_overrideSignsPath;

    bool hasExtracted = QFileInfo::exists(extractedSubs);
    bool hasOverrideSubs = !overrideSubs.isEmpty() && QFileInfo::exists(overrideSubs);
    bool hasOverrideSigns = !overrideSigns.isEmpty() && QFileInfo::exists(overrideSigns);

    QString outputFileBase = m_paths->processedFullSubs().left(m_paths->processedFullSubs().lastIndexOf('_'));
    bool success = false;
    if (hasOverrideSubs && hasOverrideSigns) {
        emit logMessage("Сценарий: используются свои субтитры для диалогов и свои надписи для надписей.", LogCategory::APP);
        success = m_assProcessor->processFromTwoSources(overrideSubs, overrideSigns, outputFileBase, m_template, m_parsedEndingTime);
    } else if (hasOverrideSubs) {
        emit logMessage("Сценарий: используются свои субтитры. Файл будет разделен на диалоги и надписи.", LogCategory::APP);
        success = m_assProcessor->processExistingFile(overrideSubs, outputFileBase, m_template, m_parsedEndingTime);
    } else if (hasOverrideSigns) {
        if (hasExtracted) {
            emit logMessage("Сценарий: используются извлеченные субтитры для диалогов и свои надписи.", LogCategory::APP);
            success = m_assProcessor->processFromTwoSources(extractedSubs, overrideSigns, outputFileBase, m_template, m_parsedEndingTime);
        } else {
            emit logMessage("Сценарий: извлеченных субтитров нет, используются только свои надписи.", LogCategory::APP);
            success = m_assProcessor->processExistingFile(overrideSigns, outputFileBase, m_template, m_parsedEndingTime);
        }
    } else {
        if (hasExtracted) {
            emit logMessage("Сценарий: используются извлеченные субтитры. Файл будет разделен.", LogCategory::APP);
            success = m_assProcessor->processExistingFile(extractedSubs, outputFileBase, m_template, m_parsedEndingTime);
        } else {
            emit logMessage("Сценарий: субтитры не найдены и не указаны. Генерируем только ТБ.", LogCategory::APP);
            QString outputPath = m_paths->processedSignsSubs();
            success = m_assProcessor->generateTbOnlyFile(outputPath, m_template, m_parsedEndingTime);
        }
    }

    if (!success) {
        emit logMessage("Во время обработки субтитров произошла ошибка.", LogCategory::APP);
        emit workflowAborted();
        return;
    }

    emit logMessage("Обработка субтитров успешно завершена.", LogCategory::APP);
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
    if (m_processManager && m_processManager->wasKilled()) return;

    emit logMessage("Поиск шрифтов завершен.", LogCategory::APP);
    m_fontResult = result;
    convertToSrtAndAssembleMaster();
}

QString WorkflowManager::handleUserFile(const QString& sourcePath, const QString& destDir, const QString& newName)
{
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
        return QString();
    }

    UserFileAction action = AppSettings::instance().userFileAction();

    if (action == UserFileAction::UseOriginalPath) {
        return sourcePath;
    }

    QFileInfo sourceInfo(sourcePath);
    QString finalName = newName.isEmpty() ? sourceInfo.fileName() : newName;
    QFileInfo destInfo(QDir(destDir), finalName);

    if (sourceInfo.absoluteFilePath() == destInfo.absoluteFilePath()) {
        return sourceInfo.absoluteFilePath();
    }

    if (destInfo.exists()) {
        QFile::remove(destInfo.absoluteFilePath());
    }

    bool success = false;
    if (action == UserFileAction::Move) {
        success = QFile::rename(sourceInfo.absoluteFilePath(), destInfo.absoluteFilePath());
        if (success) {
            return destInfo.absoluteFilePath();
        }
        // Если rename не удался (например, разные диски), пробуем скопировать
    }

    // Действие Copy или фоллбэк для Move
    success = QFile::copy(sourceInfo.absoluteFilePath(), destInfo.absoluteFilePath());

    if (success) {
        if (action == UserFileAction::Move) {
            // Если это был фоллбэк для Move, удаляем исходник
            QFile::remove(sourceInfo.absoluteFilePath());
        }
        return destInfo.absoluteFilePath();
    }

    // Если ничего не получилось, возвращаем исходный путь и логируем предупреждение
    emit logMessage("ПРЕДУПРЕЖДЕНИЕ: не удалось переместить/скопировать файл. Будет использован оригинальный путь: " + sourcePath, LogCategory::APP);
    return sourceInfo.absoluteFilePath();
}

void WorkflowManager::prepareUserFiles()
{
    emit logMessage("Перемещение пользовательских файлов в структуру проекта...", LogCategory::APP);

    // 1. Русская аудиодорожка
    QString oldAudioPath = m_mainWindow->getAudioPath();
    QString newAudioPath = handleUserFile(oldAudioPath, m_paths->ruAudioPath);
    
    if (newAudioPath != oldAudioPath && !newAudioPath.isEmpty()) {
        m_mainRuAudioPath = newAudioPath;
        m_mainWindow->findChild<QLineEdit*>("audioPathLineEdit")->setText(m_mainRuAudioPath);
        emit logMessage("Путь к аудиодорожке обновлен: " + m_mainRuAudioPath, LogCategory::APP);
    } else if (!newAudioPath.isEmpty()) {
        // Путь не изменился, но файл существует
        m_mainRuAudioPath = newAudioPath;
    }

    // 2. Свои субтитры
    QString newSubsPath = handleUserFile(m_mainWindow->getOverrideSubsPath(), m_paths->sourcesPath, "override_subs.ass");
    if (!newSubsPath.isEmpty() && newSubsPath != m_overrideSubsPath) {
        m_overrideSubsPath = newSubsPath;
        m_mainWindow->findChild<QLineEdit*>("overrideSubsPathEdit")->setText(m_overrideSubsPath);
        emit logMessage("Путь к файлу субтитров обновлен: " + m_overrideSubsPath, LogCategory::APP);
    } else if (!newSubsPath.isEmpty()) {
        m_overrideSubsPath = newSubsPath;
    }

    // 3. Свои надписи
    QString newSignsPath = handleUserFile(m_mainWindow->getOverrideSignsPath(), m_paths->sourcesPath, "override_signs.ass");
    if (!newSignsPath.isEmpty() && newSignsPath != m_overrideSignsPath) {
        m_overrideSignsPath = newSignsPath;
        m_mainWindow->findChild<QLineEdit*>("overrideSignsPathEdit")->setText(m_overrideSignsPath);
        emit logMessage("Путь к файлу надписей обновлен: " + m_overrideSignsPath, LogCategory::APP);
    } else if (!newSignsPath.isEmpty()) {
        m_overrideSignsPath = newSignsPath;
    }
}

void WorkflowManager::onBitrateCheckFinished(RerenderDecision decision, const RenderPreset &newPreset)
{
    if (decision == RerenderDecision::Rerender) {
        emit logMessage("Получено решение о перерендере.", LogCategory::APP);
        m_renderPreset = newPreset;
        m_currentStep = Step::RenderingMp4Pass1;
        runRenderPass(m_currentStep);
    } else {
        finishWorkflow();
    }
}

void WorkflowManager::resumeAfterSubEdit()
{
    emit logMessage("Редактирование завершено, процесс возобновлен.", LogCategory::APP);
    processSubtitles();
}
