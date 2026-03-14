#include "appcontroller.h"

#include "appsettings.h"
#include "missingfilescontroller.h"
#include "workflowmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QThread>

AppController::AppController(QObject* parent)
    : QObject(parent), m_missingFilesController(new MissingFilesController(this))
{
    connect(m_missingFilesController, &MissingFilesController::dialogFinished, this,
            &AppController::submitMissingFilesResponse);
}

bool AppController::isBusy() const
{
    return m_isBusy;
}
int AppController::currentProgress() const
{
    return m_currentProgress;
}
QString AppController::currentStage() const
{
    return m_currentStage;
}

void AppController::setIsBusy(bool busy)
{
    if (m_isBusy != busy)
    {
        m_isBusy = busy;
        emit isBusyChanged();
    }
}

void AppController::setProgress(int progress, const QString& stage)
{
    if (m_currentProgress != progress)
    {
        m_currentProgress = progress;
        emit progressChanged();
    }
    if (!stage.isEmpty() && m_currentStage != stage)
    {
        m_currentStage = stage;
        emit stageChanged();
    }
}

void AppController::startAutoWorkflow(const QString& templateName, const QString& episodeNum, const QString& mkvPath,
                                      const QString& audioPath, const QString& subsPath, const QString& signsPath,
                                      bool normalizeAudio, bool decoupleSrt)
{
    if (m_isBusy)
    {
        return;
    }

    if (!m_templates.contains(templateName))
    {
        emit logMessage("[ОШИБКА] Выбранный шаблон не найден в памяти.");
        return;
    }

    setIsBusy(true);
    setProgress(0, "Инициализация...");
    emit logMessage("[APP] Запуск процесса сборки...");

    // Подготавливаем параметры
    WorkflowParams params;
    params.tmpl = m_templates.value(templateName);
    params.episodeNumberForPost = episodeNum;

    // Форматируем номер для поиска (например, "6" -> "06")
    bool ok = false;
    int epInt = episodeNum.toInt(&ok);
    params.episodeNumberForSearch = ok ? QString("%1").arg(epInt, 2, 10, QChar('0')) : episodeNum;

    params.initialAudioPath = audioPath;
    params.overrideSubsPath = subsPath;
    params.overrideSignsPath = signsPath;
    params.isNormalizationEnabled = normalizeAudio;
    params.isSrtMasterDecoupled = decoupleSrt;

    QSettings settings("MyCompany", "DubbingTool");

    // Создаем рабочий поток
    QThread* thread = new QThread();
    WorkflowManager* worker = new WorkflowManager(params, settings);
    m_currentWorker = worker;

    worker->moveToThread(thread);

    // --- ПОДКЛЮЧАЕМ СИГНАЛЫ ---
    // Перенаправляем логи и прогресс из воркера в QML
    connect(
        worker, &WorkflowManager::logMessage, this,
        [this](const QString& msg, LogCategory category)
        {
            Q_UNUSED(category); // Пока игнорируем категорию для простоты
            emit logMessage(msg);
        },
        Qt::QueuedConnection);

    connect(
        worker, &WorkflowManager::progressUpdated, this,
        [this](int percent, const QString& stage) { setProgress(percent, stage); }, Qt::QueuedConnection);

    connect(
        worker, &WorkflowManager::pauseForSubEditRequest, this,
        [this](const QString& path) { emit pauseForSubEditRequested(path); }, Qt::QueuedConnection);

    // --- ОБРАБОТКА ЗАПРОСА СТИЛЕЙ НАДПИСЕЙ ---
    connect(
        worker, &WorkflowManager::signStylesRequest, this,
        [this](const QString& path)
        {
            QSet<QString> foundStyles;
            QSet<QString> foundActors;

            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QTextStream in(&file);
                in.setEncoding(QStringConverter::Utf8);
                bool inEvents = false;

                while (!in.atEnd())
                {
                    QString line = in.readLine().trimmed();
                    if (line.startsWith("[Events]"))
                    {
                        inEvents = true;
                        continue;
                    }
                    if (!inEvents)
                    {
                        continue;
                    }
                    if (line.startsWith("[Fonts]") || line.startsWith("[Graphics]"))
                    {
                        break;
                    }

                    if (line.startsWith("Style:"))
                    {
                        QStringList parts = line.split(QChar(','), Qt::SkipEmptyParts);
                        if (!parts.isEmpty())
                        {
                            foundStyles.insert(parts[0].split(':').last().trimmed());
                        }
                    }
                    else if (line.startsWith("Dialogue:"))
                    {
                        QStringList parts = line.split(QChar(','), Qt::KeepEmptyParts);
                        if (parts.size() > 4)
                        {
                            QString style = parts[3].trimmed();
                            QString actor = parts[4].trimmed();
                            if (!style.isEmpty())
                            {
                                foundStyles.insert(style);
                            }
                            if (!actor.isEmpty())
                            {
                                foundActors.insert(actor);
                            }
                        }
                    }
                }
                file.close();
            }

            // Превращаем QSet в QStringList и сортируем
            QStringList stylesList = foundStyles.values();
            QStringList actorsList = foundActors.values();
            stylesList.sort();
            actorsList.sort();

            emit logMessage(
                QString("[APP] Найдено стилей: %1, актёров: %2").arg(stylesList.size()).arg(actorsList.size()));

            // Отправляем два списка в QML
            emit signStylesRequested(stylesList, actorsList);
        },
        Qt::QueuedConnection);

    // --- ОБРАБОТКА ЗАПРОСА НЕДОСТАЮЩИХ ФАЙЛОВ / ВРЕМЕНИ ТБ ---
    connect(
        worker, &WorkflowManager::userInputRequired, this,
        [this](const UserInputRequest& request)
        {
            emit logMessage("[APP] Процесс приостановлен: требуются данные от пользователя.");

            // Передаем данные в наш встроенный контроллер
            m_missingFilesController->setupRequest(request.audioFileRequired, request.isWavRequired,
                                                   request.missingFonts, request.tbTimeRequired, request.tbTimeReason,
                                                   request.videoFilePath, request.videoDurationS);
        },
        Qt::QueuedConnection);

    // Связываем ответ от диалога с воркером
    connect(this, &AppController::userInputProvided, worker, &WorkflowManager::resumeWithUserInput);

    // Обработка завершения
    connect(
        worker, &WorkflowManager::finished, this,
        [this, thread]()
        {
            emit logMessage("[APP] ============ ПРОЦЕСС ЗАВЕРШЕН ============");
            setIsBusy(false);
            setProgress(100, "Готово");
            thread->quit();
        },
        Qt::QueuedConnection);

    connect(
        worker, &WorkflowManager::workflowAborted, this,
        [this, thread]()
        {
            emit logMessage("[APP] ============ ПРОЦЕСС ПРЕРВАН ============");
            setIsBusy(false);
            setProgress(0, "Прервано");
            thread->quit();
        },
        Qt::QueuedConnection);

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, thread, &QObject::deleteLater);

    // Запуск (с файлом или без)
    if (!mkvPath.isEmpty())
    {
        connect(thread, &QThread::started, worker, [worker, mkvPath]() { worker->startWithManualFile(mkvPath); });
    }
    else
    {
        connect(thread, &QThread::started, worker, &WorkflowManager::start);
    }

    thread->start();
}

void AppController::cancelWorkflow()
{
    if (m_currentWorker != nullptr)
    {
        emit logMessage("[APP] Отправка запроса на отмену...");
        QMetaObject::invokeMethod(m_currentWorker, "cancelOperation", Qt::QueuedConnection);
    }
}

QStringList AppController::templateList() const
{
    return m_templateList;
}

void AppController::loadTemplates()
{
    QStringList newList;
    m_templates.clear(); // Очищаем старые шаблоны в памяти

    QDir templatesDir("templates");
    if (!templatesDir.exists())
    {
        templatesDir.mkpath(".");
        emit logMessage("[APP] Создана папка 'templates' для хранения шаблонов.");
    }

    QStringList filter("*.json");
    QFileInfoList fileList = templatesDir.entryInfoList(filter, QDir::Files);

    for (const QFileInfo& fileInfo : fileList)
    {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            ReleaseTemplate t;
            t.read(doc.object());

            if (!t.templateName.isEmpty())
            {
                m_templates.insert(t.templateName, t);
                newList.append(t.templateName);
            }
        }
    }

    if (newList.isEmpty())
    {
        emit logMessage("[APP] Шаблоны не найдены. Создайте новый шаблон.");
    }
    else
    {
        emit logMessage(QString("[APP] Загружено %1 шаблонов.").arg(newList.count()));
    }

    if (m_templateList != newList)
    {
        m_templateList = newList;
        emit templateListChanged();
    }
}

void AppController::resumeAfterSubEdit()
{
    if (m_currentWorker != nullptr)
    {
        emit logMessage("[APP] Продолжение работы после ручного редактирования...");
        QMetaObject::invokeMethod(m_currentWorker, "resumeAfterSubEdit", Qt::QueuedConnection);
    }
}

void AppController::submitSignStyles(const QStringList& selectedStyles)
{
    if (m_currentWorker != nullptr)
    {
        emit logMessage("[APP] Выбранные стили: " + selectedStyles.join(", "));
        QMetaObject::invokeMethod(m_currentWorker, "resumeWithSignStyles", Qt::QueuedConnection,
                                  Q_ARG(QStringList, selectedStyles));
    }
}

QString AppController::extractEpisodeNumber(const QString& filePath)
{
    QRegularExpression re(" - (\\d{1,3})[ ._]");
    QRegularExpressionMatch match = re.match(filePath);

    if (!match.hasMatch())
    {
        re.setPattern("(\\d+)(?!.*\\d)");
        match = re.match(QFileInfo(filePath).baseName());
    }

    if (match.hasMatch())
    {
        QString episodeNumber = match.captured(1);
        emit logMessage("[APP] Номер серии автоматически извлечен: " + episodeNumber);
        return episodeNumber;
    }

    emit logMessage("[APP] Не удалось автоматически извлечь номер серии из имени файла.");
    return "";
}

bool AppController::canDecoupleSubs(const QString& templateName, const QString& subsPath)
{
    if (subsPath.isEmpty() || !QFileInfo::exists(subsPath))
    {
        return false;
    }
    if (!m_templates.contains(templateName))
    {
        return false;
    }

    return m_templates.value(templateName).createSrtMaster;
}

bool AppController::isNugenAmbAvailable() const
{
    QString path = AppSettings::instance().nugenAmbPath().trimmed();
    if (path.isEmpty())
    {
        return false;
    }
    QFileInfo exeInfo(path);
    if (!exeInfo.exists())
    {
        return false;
    }
    QFileInfo ambCmdInfo(exeInfo.dir().filePath("AMBCmd.exe"));
    return ambCmdInfo.exists();
}

QStringList AppController::autoWorkflowStageNames() const
{
    return {QStringLiteral("Скачивание"),
            QStringLiteral("Извлечение"),
            QStringLiteral("Аудио"),
            QStringLiteral("Субтитры"),
            QStringLiteral("Сборка MKV"),
            QStringLiteral("Рендер")};
}

int AppController::currentPipelineStageIndex() const
{
    const QString stageName = m_currentStage.trimmed();
    if (stageName.isEmpty() || stageName == QStringLiteral("Ожидание"))
    {
        return -1;
    }
    if (stageName.contains(QStringLiteral("Готово")) || stageName.contains(QStringLiteral("Прервано")))
    {
        return 5; // last stage
    }
    if (stageName.contains(QStringLiteral("Скачивание")) || stageName.contains(QStringLiteral("торрент")) ||
        stageName.contains(QStringLiteral("RSS")) || stageName.contains(QStringLiteral("Инициализация")))
    {
        return 0;
    }
    if (stageName.contains(QStringLiteral("Извлечение")) || stageName.contains(QStringLiteral("дорожек")) ||
        stageName.contains(QStringLiteral("вложен")))
    {
        return 1;
    }
    if (stageName.contains(QStringLiteral("NUGEN")) || stageName.contains(QStringLiteral("Нормализация")) ||
        stageName.contains(QStringLiteral("Конвертация аудио")) || stageName.contains(QStringLiteral("аудио")))
    {
        return 2;
    }
    if (stageName.contains(QStringLiteral("шрифт")) || stageName.contains(QStringLiteral("стил")) ||
        stageName.contains(QStringLiteral("субтитр")) || stageName.contains(QStringLiteral("Подготовка данных")) ||
        stageName.contains(QStringLiteral("ASS")) || stageName.contains(QStringLiteral("SRT-копи")))
    {
        return 3;
    }
    if (stageName.contains(QStringLiteral("Сборка MKV")) || stageName.contains(QStringLiteral("MKV")))
    {
        return 4;
    }
    if (stageName.contains(QStringLiteral("Рендер")) || stageName.contains(QStringLiteral("MP4")) ||
        stageName.contains(QStringLiteral("Concat")))
    {
        return 5;
    }
    return -1;
}

void AppController::deleteTemplate(const QString& templateName)
{
    QFile::remove("templates/" + templateName + ".json");
    emit logMessage("[APP] Шаблон '" + templateName + "' успешно удален.");
    loadTemplates();
}

QJsonObject AppController::getTemplateJson(const QString& name)
{
    QJsonObject json;
    if (m_templates.contains(name))
    {
        m_templates.value(name).write(json);
    }
    return json;
}

QJsonObject AppController::getDefaultTemplateJson()
{
    ReleaseTemplate defaultTemplate;
    defaultTemplate.templateName = "Новый шаблон";
    defaultTemplate.seriesTitle = "Как в архиве mkv, но без [DUB] и - 00.mkv";
    defaultTemplate.seriesTitleForPost = "Как в постах, без кавычек";
    defaultTemplate.rssUrl = QUrl("https://example.com/rss.xml");
    defaultTemplate.animationStudio = "STUDIO (с шикимори или MAL)";
    defaultTemplate.subAuthor = "Crunchyroll (или Имя Фамилия, если перевод свой + галочка внизу \"Свой перевод\")";
    defaultTemplate.originalLanguage = "ja";
    defaultTemplate.endingChapterName = "Ending Start";
    defaultTemplate.renderPresetName = "NVIDIA (HEVC NVENC)";
    defaultTemplate.totalEpisodes = 12;
    defaultTemplate.director = "Режиссер Дубляжа";
    defaultTemplate.soundEngineer = "Звукорежиссер";
    defaultTemplate.timingAuthor = "Разметка";
    defaultTemplate.releaseBuilder = "Сборщик Релиза";
    defaultTemplate.cast << "Актер 1" << "Актер 2" << "Актер 3";

    defaultTemplate.postTemplates["tg_mp4"] =
        "▶️Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
        "📌«%SERIES_TITLE%» в дубляже от ТО Дубляжная\n\n"
        "🎁Сериал озвучен при поддержке [онлайн-кинотеатра TVOЁ](https://tvoe.live/), если вы хотите поддержать "
        "Дубляжную, то смотрите нашу озвучку именно там, ведь TVOЁ дарит скидку нашим подписчикам по промокоду - "
        "`Dublyazhnaya`, где 1 месяц 99 рублей вместо 299 руб\n\n"
        "Помимо ТГ сериал можно посмотреть здесь:\n\n"
        "[TVOЁ](https://tvoe.live/p/) (~~299~~ 99 руб. по промокоду: `Dublyazhnaya`)\n\n"
        "[Архив MKV](https://t.me/+CVpSSg33UwI4MzYy)\n\n"
        "🎙Роли дублировали:\n%CAST_LIST%\n\n"
        "📝Режиссёр дубляжа:\n%DIRECTOR%\n\n"
        "🪄Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
        "📚Перевод:\n%SUB_AUTHOR%\n\n"
        "✏️Разметка:\n%TIMING_AUTHOR%\n\n"
        "✨Локализация постера:\nКирилл Хоримиев\n\n"
        "📦Сборка релиза:\n%RELEASE_BUILDER%\n\n"
        "#Дубрелиз@dublyajnaya #Хештег@dublyajnaya #Дубляж@dublyajnaya";
    defaultTemplate.postTemplates["tg_mkv"] = "%SERIES_TITLE%\n"
                                              "Серия %EPISODE_NUMBER%/%TOTAL_EPISODES%\n"
                                              "#Хештег";
    defaultTemplate.postTemplates["vk"] =
        "«%SERIES_TITLE%» в дубляже от ТО Дубляжная\n\n"
        "Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
        "🎁Сериал озвучен при поддержке онлайн-кинотеатра TVOЁ, если вы хотите поддержать Дубляжную, то смотрите нашу "
        "озвучку именно там, ведь TVOЁ дарит скидку нашим подписчикам по промокоду - Dublyazhnaya, где 1 месяц 99 "
        "рублей вместо 299 руб tvoe.cc/inby\n\n"
        "Роли дублировали:\n%CAST_LIST%\n\n"
        "Режиссёр дубляжа:\n%DIRECTOR%\n\n"
        "Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
        "Перевод:\n%SUB_AUTHOR%\n\n"
        "️Разметка:\n%TIMING_AUTHOR%\n\n"
        "Локализация постера:\nКирилл Хоримиев\n\n"
        "Сборка релиза:\n%RELEASE_BUILDER%\n\n"
        "#Хештег@dublyajnaya";
    defaultTemplate.postTemplates["vk_comment"] =
        "А также вы можете поддержать наш коллектив на бусти: https://boosty.to/dubl/single-payment/donation/634652\n\n"
        "ТГ: https://t.me/dublyajnaya\n\n"
        "TVOЁ (99 руб. по промокоду: Dublyazhnaya): https://tvoe.live/p/";
    defaultTemplate.uploadUrls << "https://vk.com/dublyajnaya" << "https://converter.kodik.biz/media-files"
                               << "https://smotret-anime.app/" << "https://v4.anilib.me/ru";

    QJsonObject json;
    defaultTemplate.write(json);
    return json;
}

void AppController::saveTemplateJson(const QJsonObject& json)
{
    ReleaseTemplate t;
    t.read(json);

    if (t.templateName.isEmpty())
    {
        emit logMessage("[ОШИБКА] Имя шаблона не может быть пустым.");
        return;
    }

    // Сохраняем в файл (твоя логика из старого MainWindow)
    QDir templatesDir("templates");
    QFile file(templatesDir.filePath(t.templateName + ".json"));
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonObject outJson;
        t.write(outJson);
        file.write(QJsonDocument(outJson).toJson(QJsonDocument::Indented));
        file.close();

        emit logMessage("[APP] Шаблон '" + t.templateName + "' успешно сохранен.");
        loadTemplates(); // Обновляем список в UI
    }
    else
    {
        emit logMessage("[ОШИБКА] Не удалось сохранить файл шаблона " + t.templateName);
    }
}

QStringList AppController::getRenderPresets()
{
    QStringList list;
    for (const auto& preset : AppSettings::instance().renderPresets())
    {
        list.append(preset.name);
    }
    return list;
}

QStringList AppController::getTbStyles()
{
    QStringList list;
    for (const auto& style : AppSettings::instance().tbStyles())
    {
        list.append(style.name);
    }
    return list;
}

void AppController::submitMissingFilesResponse(bool accepted, const QString& audioPath,
                                               const QMap<QString, QString>& fonts, const QString& time)
{
    if (accepted)
    {
        UserInputResponse response;
        response.audioPath = audioPath;
        response.resolvedFonts = fonts;
        response.time = time;
        emit logMessage("[APP] Данные от пользователя получены, возобновление процесса...");
        emit userInputProvided(response);
    }
    else
    {
        emit logMessage("[APP] Пользователь отменил ввод данных. Процесс прерван.");
        emit userInputProvided({});
    }
}

MissingFilesController* AppController::missingFiles() const
{
    return m_missingFilesController;
}