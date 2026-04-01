#include "mainwindow.h"

#include "appsettings.h"
#include "chaptertimingsdialog.h"
#include "manualassembler.h"
#include "manualextractionwidget.h"
#include "manualrenderer.h"
#include "postgenerator.h"
#include "settingsdialog.h"
#include "setupwizarddialog.h"
#include "styleselectordialog.h"
#include "templateeditor.h"
#include "torrentselectordialog.h"
#include "trackselectordialog.h"
#include "ui_mainwindow.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

static QString logCategoryToString(LogCategory category)
{
    switch (category)
    {
    case LogCategory::APP:
        return "APP";
    case LogCategory::FFMPEG:
        return "FFMPEG";
    case LogCategory::MKVTOOLNIX:
        return "MKVTOOLNIX";
    case LogCategory::QBITTORRENT:
        return "QBITTORRENT";
    case LogCategory::DEBUG:
        return "DEBUG";
    }
    return "UNKNOWN";
}

static QString newTemplateDraftFilePath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir templatesDir(QDir(appDataPath).filePath("templates"));
    if (!templatesDir.exists())
    {
        templatesDir.mkpath(".");
    }
    return templatesDir.filePath(".new_template_draft.json");
}

static QDir templatesStorageDir()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir templatesDir(QDir(appDataPath).filePath("templates"));
    if (!templatesDir.exists())
    {
        templatesDir.mkpath(".");
    }

    // One-time migration from legacy relative storage (e.g. build/templates).
    QDir legacyDir("templates");
    if (legacyDir.exists() && templatesDir.entryList({"*.json"}, QDir::Files).isEmpty())
    {
        const QStringList legacyFiles = legacyDir.entryList({"*.json"}, QDir::Files);
        for (const QString& fileName : legacyFiles)
        {
            QFile::copy(legacyDir.filePath(fileName), templatesDir.filePath(fileName));
        }
    }

    return templatesDir;
}

static bool loadTemplateFromJsonFile(const QString& filePath, ReleaseTemplate& outTemplate)
{
    QFile draftFile(filePath);
    if (!draftFile.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(draftFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }

    outTemplate.read(doc.object());
    return !outTemplate.templateName.isEmpty();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_currentWorker(nullptr)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/icon.png"));
    qRegisterMetaType<ChapterMarker>("ChapterMarker");
    qRegisterMetaType<QList<ChapterMarker>>("QList<ChapterMarker>");

    QSettings settings("MyCompany", "DubbingTool");
    restoreGeometry(settings.value("ui/mainWindowGeometry").toByteArray());
    restoreState(settings.value("ui/mainWindowState").toByteArray());

    m_manualExtractionWidget = ui->extractTab;

    m_manualAssemblyWidget = new ManualAssemblyWidget(this);
    ui->assemblyTabLayout->addWidget(m_manualAssemblyWidget);
    m_manualRenderWidget = new ManualRenderWidget(this);
    ui->renderTabLayout->addWidget(m_manualRenderWidget);
    m_publicationWidget = new PublicationWidget(this);
    ui->mainTabWidget->addTab(m_publicationWidget, "Публикация");
    ui->mainTabWidget->setTabEnabled(ui->mainTabWidget->count() - 1, false);

    QAction* settingsAction = new QAction("Настройки", this);
    ui->menubar->addAction(settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::on_actionSettings_triggered);

    QAction* setupWizardAction = new QAction("Мастер настройки", this);
    ui->menubar->addAction(setupWizardAction);
    connect(setupWizardAction, &QAction::triggered, this,
            [this]()
            {
                SetupWizardDialog wizard(this);
                wizard.exec();
            });

    connect(m_manualExtractionWidget, &ManualExtractionWidget::logMessage, this, &MainWindow::logMessage);
    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::templateDataRequested, this,
            &MainWindow::onRequestTemplateData);

    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::assemblyRequested, this, &MainWindow::startManualAssembly);
    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::chapterTimingsRequested, this,
            &MainWindow::onManualChapterTimingsRequested);
    connect(m_manualRenderWidget, &ManualRenderWidget::renderRequested, this, &MainWindow::startManualRender);
    connect(m_manualRenderWidget, &ManualRenderWidget::chapterTimingsRequested, this,
            &MainWindow::onManualChapterTimingsRequested);

    connect(m_publicationWidget, &PublicationWidget::logMessage, this, &MainWindow::logMessage);
    connect(m_publicationWidget, &PublicationWidget::postsUpdateRequest, this, &MainWindow::onPostsUpdateRequest);

    connect(ui->overrideSubsPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateDecoupleCheckBoxState);
    connect(ui->templateComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateDecoupleCheckBoxState);
    connect(ui->templateComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateChaptersAutoVisibility);

    ui->downloadProgressBar->setVisible(false);
    ui->progressLabel->setVisible(false);

    const bool hasNugenAmb = AppSettings::instance().isNugenAmbAvailable();
    ui->normalizeAudioCheckBox->setEnabled(hasNugenAmb);

    // Open log file for writing (append mode, overwritten each launch)
    m_logFile.setFileName("dubbing_tool.log");
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        qWarning("Failed to open dubbing_tool.log for writing");
    }

    loadTemplates();

    if (ui->templateComboBox->count() == 0)
    {
        logMessage("Шаблоны не найдены. Создайте новый шаблон.");
    }
    else
    {
        logMessage(QString("Загружено %1 шаблонов.").arg(m_templates.count()));
    }

    updateDecoupleCheckBoxState();
    updateChaptersAutoVisibility();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::logMessage(const QString& message, LogCategory category)
{
    QString categoryName = logCategoryToString(category);
    QString timedMessage = QString("[%1] %2 - %3")
                               .arg(categoryName)
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                               .arg(message.trimmed());

    // Always write to log file (all categories)
    if (m_logFile.isOpen())
    {
        m_logFile.write(timedMessage.toUtf8());
        m_logFile.write("\n");
        m_logFile.flush();
    }

    // Filter for UI display
    const auto& enabledCategories = AppSettings::instance().enabledLogCategories();
    if (!enabledCategories.contains(category))
    {
        return;
    }

    ui->logOutput->appendPlainText(timedMessage);
}

void MainWindow::loadTemplates()
{
    m_templates.clear();
    ui->templateComboBox->clear();

    QDir templatesDir = templatesStorageDir();

    QStringList filter("*.json");
    QFileInfoList fileList = templatesDir.entryInfoList(filter, QDir::Files);

    for (const QFileInfo& fileInfo : fileList)
    {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
        {
            logMessage("Ошибка: не удалось открыть файл шаблона " + fileInfo.fileName(), LogCategory::APP);
            continue;
        }

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        ReleaseTemplate t;
        t.read(doc.object());

        if (!t.templateName.isEmpty())
        {
            m_templates.insert(t.templateName, t);
        }
    }

    ui->templateComboBox->addItems(m_templates.keys());

    if (m_manualAssemblyWidget)
    {
        m_manualAssemblyWidget->updateTemplateList(m_templates.keys());
        const QString sel = ui->templateComboBox->currentText();
        if (!sel.isEmpty() && m_templates.contains(sel))
        {
            m_manualAssemblyWidget->onTemplateDataReceived(m_templates.value(sel));
        }
    }
    updateChaptersAutoVisibility();
}

void MainWindow::saveTemplate(const ReleaseTemplate& t)
{
    if (t.templateName.isEmpty())
    {
        logMessage("Ошибка: имя шаблона не может быть пустым.", LogCategory::APP);
        return;
    }

    QString nameToSelect = t.templateName;

    if (!m_editingTemplateFileName.isEmpty() && m_editingTemplateFileName != t.templateName)
    {
        QFile::remove(templatesStorageDir().filePath(m_editingTemplateFileName + ".json"));
    }

    QDir templatesDir = templatesStorageDir();
    QFile file(templatesDir.filePath(t.templateName + ".json"));
    if (!file.open(QIODevice::WriteOnly))
    {
        logMessage("Ошибка: не удалось сохранить файл шаблона " + t.templateName, LogCategory::APP);
        return;
    }

    QJsonObject jsonObj;
    t.write(jsonObj);

    file.write(QJsonDocument(jsonObj).toJson(QJsonDocument::Indented));
    file.close();

    logMessage("Шаблон '" + t.templateName + "' успешно сохранен.", LogCategory::APP);
    loadTemplates();

    int index = ui->templateComboBox->findText(nameToSelect);
    if (index != -1)
    { // -1 означает, что текст не найден
        ui->templateComboBox->setCurrentIndex(index);
    }
}

void MainWindow::on_createTemplateButton_clicked()
{
    m_editingTemplateFileName.clear();
    TemplateEditor editor(this);
    const QString draftPath = newTemplateDraftFilePath();

    ReleaseTemplate defaultTemplate;
    defaultTemplate.templateName = "Новый шаблон";
    defaultTemplate.seriesTitle = "Как в архиве mkv, но без [DUB] и - 00.mkv";
    defaultTemplate.seriesTitleForPost = "Как в постах, без кавычек";
    defaultTemplate.rssUrl = QUrl("https://example.com/rss.xml");
    defaultTemplate.animationStudio = "STUDIO (с шикимори или MAL)";
    defaultTemplate.subAuthor = "Crunchyroll (или Имя Фамилия, если перевод свой + галочка внизу \"Свой перевод\")";
    defaultTemplate.originalLanguage = "ja";
    defaultTemplate.endingChapterName = "Ending Start";
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

    PostTemplateMeta tgMp4Meta;
    tgMp4Meta.title = "Telegram (Канал)";
    tgMp4Meta.platform = "telegram";
    tgMp4Meta.category = "База";
    tgMp4Meta.tags = {"tg", "mp4", "дубляж"};
    tgMp4Meta.sortOrder = 10;
    tgMp4Meta.builtin = true;
    defaultTemplate.postTemplateMeta.insert("tg_mp4", tgMp4Meta);

    PostTemplateMeta tgMkvMeta = tgMp4Meta;
    tgMkvMeta.title = "Telegram (Архив)";
    tgMkvMeta.tags = {"tg", "mkv", "архив"};
    tgMkvMeta.sortOrder = 20;
    defaultTemplate.postTemplateMeta.insert("tg_mkv", tgMkvMeta);

    PostTemplateMeta vkMeta;
    vkMeta.title = "VK (Пост)";
    vkMeta.platform = "vk";
    vkMeta.category = "База";
    vkMeta.tags = {"vk", "пост"};
    vkMeta.sortOrder = 30;
    vkMeta.builtin = true;
    defaultTemplate.postTemplateMeta.insert("vk", vkMeta);

    PostTemplateMeta vkCommentMeta = vkMeta;
    vkCommentMeta.title = "VK (Комментарий)";
    vkCommentMeta.tags = {"vk", "комментарий"};
    vkCommentMeta.sortOrder = 40;
    defaultTemplate.postTemplateMeta.insert("vk_comment", vkCommentMeta);
    defaultTemplate.uploadUrls << "https://vk.com/dublyajnaya" << "https://converter.kodik.biz/media-files"
                               << "https://anime-365.ru/" << "https://v4.anilib.me/ru";

    ReleaseTemplate templateToEdit = defaultTemplate;
    if (QFile::exists(draftPath))
    {
        const auto restoreReply = QMessageBox::question(
            this, "Найден черновик шаблона",
            "Найден несохраненный черновик нового шаблона. Восстановить его и продолжить редактирование?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (restoreReply == QMessageBox::Yes)
        {
            ReleaseTemplate draftTemplate;
            if (loadTemplateFromJsonFile(draftPath, draftTemplate))
            {
                templateToEdit = draftTemplate;
                logMessage("Восстановлен черновик нового шаблона.", LogCategory::APP);
            }
            else
            {
                logMessage("Черновик найден, но не удалось его прочитать. Загружен шаблон по умолчанию.",
                           LogCategory::APP);
                QFile::remove(draftPath);
            }
        }
        else
        {
            QFile::remove(draftPath);
            logMessage("Черновик нового шаблона отклонен и удален.", LogCategory::APP);
        }
    }

    editor.setTemplate(templateToEdit);
    editor.enableDraftAutosave(draftPath);

    if (editor.exec() == QDialog::Accepted)
    {
        ReleaseTemplate newTemplate = editor.getTemplate();
        if (newTemplate.templateName.isEmpty())
        {
            QMessageBox::warning(&editor, "Ошибка", "Имя шаблона не может быть пустым.");
            return;
        }
        saveTemplate(newTemplate);
        const QString savedTemplatePath = templatesStorageDir().filePath(newTemplate.templateName + ".json");
        if (QFile::exists(savedTemplatePath))
        {
            QFile::remove(draftPath);
        }
        else
        {
            logMessage("Сохранение шаблона не подтверждено, черновик оставлен.", LogCategory::APP);
        }
    }
}

void MainWindow::on_editTemplateButton_clicked()
{
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty() || !m_templates.contains(currentName))
    {
        logMessage("Ошибка: выберите существующий шаблон для редактирования.", LogCategory::APP);
        return;
    }

    m_editingTemplateFileName = currentName;

    TemplateEditor editor(this);
    editor.setTemplate(m_templates.value(currentName));
    if (editor.exec() == QDialog::Accepted)
    {
        ReleaseTemplate updatedTemplate = editor.getTemplate();
        saveTemplate(updatedTemplate);
    }
}

void MainWindow::on_deleteTemplateButton_clicked()
{
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty())
    {
        logMessage("Ошибка: нечего удалять.", LogCategory::APP);
        return;
    }

    auto reply = QMessageBox::question(this, "Подтверждение удаления",
                                       "Вы уверены, что хотите удалить шаблон '" + currentName + "'?",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        QFile::remove(templatesStorageDir().filePath(currentName + ".json"));
        logMessage("Шаблон '" + currentName + "' удален.", LogCategory::APP);
        loadTemplates();
    }
}

void MainWindow::on_cancelButton_clicked()
{
    if (m_currentWorker)
    {
        logMessage("Отправка запроса на отмену...", LogCategory::APP);
        QMetaObject::invokeMethod(m_currentWorker, "cancelOperation", Qt::QueuedConnection);
    }
}

void MainWindow::on_startButton_clicked()
{
    if (m_currentWorker)
    {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.", LogCategory::APP);
        return;
    }

    m_publicationWidget->clearData();
    m_lastChapterMarkers.clear();
    m_lastChapterDurationNs = 0;
    int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
    if (pubIndex != -1)
    {
        ui->mainTabWidget->setTabEnabled(pubIndex, false);
    }

    QString manualMkvPath = ui->mkvPathLineEdit->text();
    if (manualMkvPath.isEmpty() && ui->episodeNumberLineEdit->text().isEmpty())
    {
        logMessage("Ошибка: укажите номер серии или выберите MKV-файл.", LogCategory::APP);
        return;
    }
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty())
    {
        logMessage("Ошибка: выберите шаблон.", LogCategory::APP);
        return;
    }

    switchToCancelMode();
    setUiEnabled(false);

    ReleaseTemplate currentTemplate = m_templates.value(currentName);
    QString episodeNumberStr = ui->episodeNumberLineEdit->text();
    bool ok;
    int epNum = episodeNumberStr.toInt(&ok);
    if (!ok && manualMkvPath.isEmpty())
    {
        logMessage("Ошибка: введенный номер серии не является числом.", LogCategory::APP);
        return;
    }
    else if (ok)
    {
        episodeNumberStr = QString::number(epNum);
    }
    QString episodeForPost = episodeNumberStr;
    QString episodeForSearch = QString("%1").arg(epNum, 2, 10, QChar('0'));

    QSettings settings("MyCompany", "DubbingTool");

    setUiEnabled(false);

    QThread* thread = new QThread(this);
    WorkflowManager* workflowManager =
        new WorkflowManager(currentTemplate, episodeForPost, episodeForSearch, settings, this);

    m_currentWorker = workflowManager; // Сохраняем указатель на текущего воркера
    m_activeProcessManagers.append(workflowManager->getProcessManager());
    connect(workflowManager, &WorkflowManager::multipleTorrentsFound, this, &MainWindow::onMultipleTorrentsFound,
            Qt::QueuedConnection);
    connect(this, &MainWindow::torrentSelected, workflowManager, &WorkflowManager::resumeWithSelectedTorrent);
    connect(workflowManager, &WorkflowManager::multipleAudioTracksFound, this, &MainWindow::onMultipleAudioTracksFound,
            Qt::QueuedConnection);
    connect(this, &MainWindow::audioTrackSelected, workflowManager, &WorkflowManager::resumeWithSelectedAudioTrack);
    connect(workflowManager, &WorkflowManager::userInputRequired, this, &MainWindow::onUserInputRequired,
            Qt::QueuedConnection);
    connect(this, &MainWindow::userInputProvided, workflowManager, &WorkflowManager::resumeWithUserInput);
    connect(workflowManager, &WorkflowManager::signStylesRequest, this, &MainWindow::onSignStylesRequest,
            Qt::QueuedConnection);
    connect(this, &MainWindow::signStylesProvided, workflowManager, &WorkflowManager::resumeWithSignStyles);
    connect(workflowManager, &WorkflowManager::bitrateCheckRequest, this, &MainWindow::onBitrateCheckRequest,
            Qt::QueuedConnection);

    connect(workflowManager, &WorkflowManager::pauseForSubEditRequest, this, &MainWindow::onPauseForSubEditRequest,
            Qt::QueuedConnection);
    connect(this, &MainWindow::subEditFinished, workflowManager, &WorkflowManager::resumeAfterSubEdit);

    workflowManager->moveToThread(thread);

    if (!manualMkvPath.isEmpty())
    {
        connect(thread, &QThread::started, workflowManager,
                [workflowManager, manualMkvPath]() { workflowManager->startWithManualFile(manualMkvPath); });
    }
    else
    {
        connect(thread, &QThread::started, workflowManager, &WorkflowManager::start);
    }
    connect(workflowManager, &WorkflowManager::finished, this, &MainWindow::finishWorkerProcess);
    connect(workflowManager, &WorkflowManager::workflowAborted, this, &MainWindow::finishWorkerProcess);

    connect(workflowManager, &WorkflowManager::postsReady, this, &MainWindow::onPostsReady);
    connect(workflowManager, &WorkflowManager::chapterMarkersReady, this, &MainWindow::onChapterMarkersReady);
    connect(workflowManager, &WorkflowManager::mkvFileReady, this, &MainWindow::onMkvFileReady);
    connect(workflowManager, &WorkflowManager::filesReady, this, &MainWindow::onFilesReady);
    connect(thread, &QThread::finished, workflowManager, &WorkflowManager::deleteLater);
    connect(workflowManager, &WorkflowManager::destroyed, thread, &QThread::deleteLater);
    connect(workflowManager, &WorkflowManager::logMessage, this, &MainWindow::logMessage);
    connect(workflowManager, &WorkflowManager::progressUpdated, this, &MainWindow::updateProgress);

    thread->start();
}

void MainWindow::on_selectMkvButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->mkvPathLineEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->mkvPathLineEdit->text()).absolutePath();
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Выберите видеофайл", lastDir, "Видеофайлы (*.mkv *.mp4)");
    if (filePath.isEmpty())
    {
        return;
    }
    settings.setValue("ui/lastBrowseDir", QFileInfo(filePath).absolutePath());
    ui->mkvPathLineEdit->setText(filePath);

    if (ui->episodeNumberLineEdit->text().isEmpty())
    {
        // Простая регулярка для поиска числа, окруженного пробелами, тире или в конце строки
        QRegularExpression re(" - (\\d{1,3})[ ._]");
        QRegularExpressionMatch match = re.match(filePath);

        if (!match.hasMatch())
        {
            // Если не нашли, пробуем найти просто последнее число в имени
            re.setPattern("(\\d+)(?!.*\\d)");
            match = re.match(QFileInfo(filePath).baseName());
        }

        if (match.hasMatch())
        {
            QString episodeNumber = match.captured(1);
            ui->episodeNumberLineEdit->setText(episodeNumber);
            logMessage("Номер серии был извлечен из имени файла: " + episodeNumber, LogCategory::APP);
        }
        else
        {
            logMessage("Не удалось автоматически извлечь номер серии из имени файла.", LogCategory::APP);
        }
    }
}

void MainWindow::onMultipleTorrentsFound(const QList<TorrentInfo>& candidates)
{
    TorrentSelectorDialog dialog(candidates, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        int selectedIndex = dialog.getSelectedIndex();
        if (selectedIndex >= 0 && selectedIndex < candidates.size())
        {
            emit torrentSelected(candidates[selectedIndex]);
        }
    }
    else
    {
        // Пользователь нажал "Отмена"
        logMessage("Выбор торрента отменен пользователем. Процесс прерван.", LogCategory::APP);
        // Нам нужно как-то сказать воркеру, чтобы он завершился
        // Самый простой способ - эмитировать сигнал с пустым результатом
        emit torrentSelected({}); // Пустой TorrentInfo будет сигналом к отмене
    }
}

void MainWindow::onWorkflowAborted()
{
    logMessage("============ ПРОЦЕСС ПРЕРВАН С ОШИБКОЙ ============", LogCategory::APP);
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        // Пользователь нажал "ОК", и данные уже сохранены на диск.
        // Теперь нужно обновить данные в работающем приложении.

        // 1. Заново загружаем все настройки из файла в память.
        emit logMessage("Настройки обновлены. Перезагрузка конфигурации...", LogCategory::APP);
        AppSettings::instance().load();

        // 2. Обновляем все виджеты, которые зависят от настроек.
        m_manualRenderWidget->updateRenderPresets();

        const bool hasNugenAmb = AppSettings::instance().isNugenAmbAvailable();
        ui->normalizeAudioCheckBox->setEnabled(hasNugenAmb);
        if (m_manualAssemblyWidget)
        {
            if (auto checkBox = m_manualAssemblyWidget->findChild<QCheckBox*>("normalizeAudioCheckBox"))
            {
                checkBox->setEnabled(hasNugenAmb);
            }
        }

        // Также нужно обновить список пресетов в редакторе шаблонов,
        // но он и так обновляется каждый раз при открытии, так что это не обязательно.
        emit logMessage("Конфигурация успешно перезагружена.", LogCategory::APP);
    }
}

void MainWindow::updateProgress(int percentage, const QString& stageName)
{
    if (!ui->downloadProgressBar->isVisible())
    {
        ui->downloadProgressBar->setVisible(true);
        ui->progressLabel->setVisible(true);
    }

    if (!stageName.isEmpty())
    {
        ui->progressLabel->setText(QString("Текущий этап: %1").arg(stageName));
    }

    if (percentage < 0)
    {
        // Отрицательное значение будет сигналом для включения "бегающего" прогресс-бара
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(0);
        ui->downloadProgressBar->setValue(-1);
    }
    else
    {
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(100);
        ui->downloadProgressBar->setValue(percentage);
    }
}

void MainWindow::on_selectAudioButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->audioPathLineEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->audioPathLineEdit->text()).absolutePath();
    }

    QString filePath =
        QFileDialog::getOpenFileName(this, "Выберите аудиофайл", lastDir, "Аудиофайлы (*.wav *.flac *.aac)");
    if (!filePath.isEmpty())
    {
        ui->audioPathLineEdit->setText(filePath);
        settings.setValue("ui/lastBrowseDir", QFileInfo(filePath).absolutePath());
    }
}

QString MainWindow::getAudioPath() const
{
    return ui->audioPathLineEdit->text();
}

QString MainWindow::getManualMkvPath() const
{
    return ui->mkvPathLineEdit->text();
}

void MainWindow::onRequestTemplateData(const QString& templateName)
{
    if (m_templates.contains(templateName))
    {
        m_manualAssemblyWidget->onTemplateDataReceived(m_templates.value(templateName));
    }
}

void MainWindow::startManualAssembly()
{
    if (m_currentWorker)
    {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.", LogCategory::APP);
        return;
    }
    logMessage("Запрос на ручную сборку...", LogCategory::APP);

    switchToCancelMode();
    setUiEnabled(false);
    ui->mainTabWidget->setCurrentIndex(0); // Переходим на главный таб

    QThread* thread = new QThread(this);
    ManualAssembler* worker = new ManualAssembler(m_manualAssemblyWidget->getParameters(), nullptr);

    m_currentWorker = worker;
    m_activeProcessManagers.append(worker->getProcessManager());
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &ManualAssembler::start);
    connect(worker, &ManualAssembler::finished, this,
            [this](bool success)
            {
                finishWorkerProcess();
                if (success)
                {
                    m_manualAssemblyWidget->clearFontsList();
                }
            });
    connect(worker, &ManualAssembler::logMessage, this, &MainWindow::logMessage);
    connect(worker, &ManualAssembler::progressUpdated, this, &MainWindow::updateProgress);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::startManualRender()
{
    if (m_currentWorker)
    {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.", LogCategory::APP);
        return;
    }
    logMessage("Запрос на ручной рендер...", LogCategory::APP);

    switchToCancelMode();
    setUiEnabled(false);
    ui->mainTabWidget->setCurrentIndex(0);

    QThread* thread = new QThread(this);
    ManualRenderer* worker = new ManualRenderer(m_manualRenderWidget->getParameters(), nullptr);

    m_currentWorker = worker;
    m_activeProcessManagers.append(worker->getProcessManager());
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &ManualRenderer::start);
    connect(worker, &ManualRenderer::finished, this, &MainWindow::finishWorkerProcess);
    connect(worker, &ManualRenderer::logMessage, this, &MainWindow::logMessage);
    connect(worker, &ManualRenderer::progressUpdated, this, &MainWindow::updateProgress);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, thread, &QObject::deleteLater);
    connect(worker, &ManualRenderer::bitrateCheckRequest, this, &MainWindow::onBitrateCheckRequest,
            Qt::QueuedConnection);

    thread->start();
}

void MainWindow::onPostsReady(const ReleaseTemplate& t, const EpisodeData& data)
{
    logMessage("Данные для постов готовы, открываю панель 'Публикация'.", LogCategory::APP);

    m_lastTemplate = t;
    m_lastEpisodeData = data;
    m_lastMkvPath = "";
    m_lastMp4Path = "";
    m_lastChapterMarkers.clear();
    m_lastChapterDurationNs = 0;

    PostGenerator generator;
    QMap<QString, PostVersions> postTexts = generator.generate(m_lastTemplate, m_lastEpisodeData);

    m_publicationWidget->updateData(m_lastTemplate, m_lastEpisodeData, postTexts, m_lastMkvPath, m_lastMp4Path);
    m_publicationWidget->setChapterTimings(m_lastChapterMarkers, m_lastChapterDurationNs);

    int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
    if (pubIndex != -1)
    {
        ui->mainTabWidget->setTabEnabled(pubIndex, true);
    }
}

void MainWindow::onMkvFileReady(const QString& mkvPath)
{
    logMessage("MKV файл готов, обновляю панель 'Публикация'.", LogCategory::APP);
    m_lastMkvPath = mkvPath;
    m_publicationWidget->setFilePaths(mkvPath, m_lastMp4Path); // m_lastMp4Path пока пуст
}

void MainWindow::onFilesReady(const QString& mkvPath, const QString& mp4Path)
{
    logMessage("Файлы готовы, обновляю панель 'Публикация'.", LogCategory::APP);
    m_lastMkvPath = mkvPath;
    m_lastMp4Path = mp4Path;

    m_publicationWidget->setFilePaths(mkvPath, mp4Path);
}

void MainWindow::onChapterMarkersReady(const QList<ChapterMarker>& chapters, qint64 durationNs)
{
    m_lastChapterMarkers = chapters;
    m_lastChapterDurationNs = durationNs;
    m_publicationWidget->setChapterTimings(m_lastChapterMarkers, m_lastChapterDurationNs);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("ui/mainWindowGeometry", saveGeometry());
    settings.setValue("ui/mainWindowState", saveState());

    AppSettings::instance().save();
    settings.setValue("manualRender/lastUsedPreset", m_manualRenderWidget->getCurrentPresetName());
    if (m_currentWorker)
    {
        logMessage("Запрошена отмена операции перед закрытием...", LogCategory::APP);
        QMetaObject::invokeMethod(m_currentWorker, "cancelOperation", Qt::QueuedConnection);

        if (m_currentWorker && m_currentWorker->thread())
        {
            if (!m_currentWorker->thread()->wait(5000))
            {
                logMessage("Поток не завершился штатно, будет прерван принудительно.", LogCategory::APP);
                m_currentWorker->thread()->terminate();
            }
        }

        logMessage("Фоновый процесс остановлен.", LogCategory::APP);
    }
    event->accept();
}

void MainWindow::on_browseOverrideSubsButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->overrideSubsPathEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->overrideSubsPathEdit->text()).absolutePath();
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ASS файл", lastDir, "ASS Subtitles (*.ass)");
    if (!filePath.isEmpty())
    {
        ui->overrideSubsPathEdit->setText(filePath);
        settings.setValue("ui/lastBrowseDir", QFileInfo(filePath).absolutePath());
    }
}

void MainWindow::on_browseOverrideSignsButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->overrideSignsPathEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->overrideSignsPathEdit->text()).absolutePath();
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ASS файл", lastDir, "ASS Subtitles (*.ass)");
    if (!filePath.isEmpty())
    {
        ui->overrideSignsPathEdit->setText(filePath);
        settings.setValue("ui/lastBrowseDir", QFileInfo(filePath).absolutePath());
    }
}

void MainWindow::on_browseChaptersXmlButton_clicked()
{
    QSettings settings("MyCompany", "DubbingTool");
    QString lastDir = settings.value("ui/lastBrowseDir").toString();
    if (lastDir.isEmpty() && !ui->chaptersXmlPathLineEdit->text().isEmpty())
    {
        lastDir = QFileInfo(ui->chaptersXmlPathLineEdit->text()).absolutePath();
    }

    QString filePath =
        QFileDialog::getOpenFileName(this, "Файл глав Matroska (XML)", lastDir, "XML (*.xml);;Все файлы (*)");
    if (!filePath.isEmpty())
    {
        ui->chaptersXmlPathLineEdit->setText(filePath);
        settings.setValue("ui/lastBrowseDir", QFileInfo(filePath).absolutePath());
    }
}

QString MainWindow::getOverrideSubsPath() const
{
    return ui->overrideSubsPathEdit->text();
}

QString MainWindow::getOverrideSignsPath() const
{
    return ui->overrideSignsPathEdit->text();
}

QString MainWindow::getChaptersXmlPath() const
{
    return ui->chaptersXmlPathLineEdit->text().trimmed();
}

bool MainWindow::isNormalizationEnabled() const
{
    return ui->normalizeAudioCheckBox->isChecked();
}

void MainWindow::onPostsUpdateRequest(const QMap<QString, QString>& viewLinks)
{
    logMessage("Обновление постов с новыми ссылками...", LogCategory::APP);
    m_lastEpisodeData.viewLinks = viewLinks;

    PostGenerator generator;
    QMap<QString, PostVersions> postTexts = generator.generate(m_lastTemplate, m_lastEpisodeData);
    m_publicationWidget->updateData(m_lastTemplate, m_lastEpisodeData, postTexts, m_lastMkvPath, m_lastMp4Path);
    m_publicationWidget->setChapterTimings(m_lastChapterMarkers, m_lastChapterDurationNs);

    QMessageBox::information(m_publicationWidget, "Успех", "Тексты постов обновлены.");
}

void MainWindow::onUserInputRequired(const UserInputRequest& request)
{
    logMessage("Процесс приостановлен: требуются данные от пользователя.", LogCategory::APP);

    for (;;)
    {
        MissingFilesDialog dialog(this);

        dialog.setAudioPathVisible(request.audioFileRequired);
        if (ui->audioPathLineEdit->text().isEmpty())
        {
            dialog.setAudioPrompt("Не удалось найти русскую аудиодорожку. Укажите путь к ней:");
        }
        else if (request.isWavRequired)
        {
            dialog.setAudioPrompt("Для SRT-мастера требуется несжатый WAV-файл:");
        }

        dialog.setMissingFonts(request.missingFonts);

        dialog.setTimeInputVisible(request.tbTimeRequired);
        if (request.tbTimeRequired)
        {
            dialog.setTimePrompt(request.tbTimeReason);
            dialog.setVideoFile(request.videoFilePath, request.videoDurationS);
        }

        dialog.setChaptersInputVisible(request.chaptersRequired);
        if (request.chaptersRequired && !request.chaptersReason.isEmpty())
        {
            dialog.setChaptersPrompt(request.chaptersReason);
        }

        if (dialog.exec() != QDialog::Accepted)
        {
            logMessage("Пользователь отменил ввод данных. Процесс прерван.", LogCategory::APP);
            emit userInputProvided({});
            return;
        }

        UserInputResponse response;
        response.audioPath = dialog.getAudioPath();
        response.resolvedFonts = dialog.getResolvedFonts();
        response.time = dialog.getTime();
        response.chaptersXmlPath = dialog.getChaptersXmlPath();
        response.buildWithoutChapters = dialog.getBuildWithoutChapters();

        if (request.chaptersRequired && !response.buildWithoutChapters && response.chaptersXmlPath.isEmpty())
        {
            QMessageBox::warning(this, "Нужны данные по главам",
                                 "Укажите путь к XML глав или отметьте сборку без глав.");
            continue;
        }

        if (request.audioFileRequired && response.audioPath.isEmpty())
        {
            QMessageBox::critical(this, "Ошибка", "Аудиодорожка является обязательной. Сборка прервана.");
            emit userInputProvided({});
            return;
        }
        if (request.tbTimeRequired && response.time == "0:00:00.000")
        {
            QMessageBox::critical(this, "Ошибка", "Время эндинга является обязательным. Сборка прервана.");
            emit userInputProvided({});
            return;
        }

        logMessage("Данные от пользователя получены, возобновление процесса...", LogCategory::APP);
        emit userInputProvided(response);
        return;
    }
}

void MainWindow::onSignStylesRequest(const QString& subFilePath)
{
    logMessage("Процесс приостановлен: требуется выбрать стили для надписей.", LogCategory::APP);
    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(subFilePath);

    if (dialog.exec() == QDialog::Accepted)
    {
        QStringList selected = dialog.getSelectedStyles();
        logMessage("Выбранные стили для надписей: " + selected.join(", "), LogCategory::APP);
        emit signStylesProvided(selected);
    }
    else
    {
        logMessage("Выбор стилей отменен. Процесс прерван.", LogCategory::APP);
        emit signStylesProvided({}); // Пустой список
    }
}

void MainWindow::onMultipleAudioTracksFound(const QList<AudioTrackInfo>& candidates)
{
    TrackSelectorDialog dialog(candidates, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        emit audioTrackSelected(dialog.getSelectedTrackId());
    }
    else
    {
        logMessage("Выбор аудиодорожки отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit audioTrackSelected(-1); // -1 будет сигналом к отмене
    }
}

void MainWindow::setUiEnabled(bool enabled)
{
    ui->createTemplateButton->setEnabled(enabled);
    ui->editTemplateButton->setEnabled(enabled);
    ui->deleteTemplateButton->setEnabled(enabled);

    if (m_manualAssemblyWidget)
        m_manualAssemblyWidget->findChild<QPushButton*>("assembleButton")->setEnabled(enabled);
    if (m_manualRenderWidget)
        m_manualRenderWidget->findChild<QPushButton*>("renderButton")->setEnabled(enabled);
}

void MainWindow::finishWorkerProcess()
{
    logMessage("============ ПРОЦЕСС ЗАВЕРШЕН ============", LogCategory::APP);

    if (m_currentWorker)
    {
        if (auto ptr = qobject_cast<WorkflowManager*>(m_currentWorker.data()))
            m_activeProcessManagers.removeOne(ptr->getProcessManager());
        else if (auto ptr = qobject_cast<ManualAssembler*>(m_currentWorker.data()))
            m_activeProcessManagers.removeOne(ptr->getProcessManager());
        else if (auto ptr = qobject_cast<ManualRenderer*>(m_currentWorker.data()))
            m_activeProcessManagers.removeOne(ptr->getProcessManager());

        if (m_currentWorker->thread())
            m_currentWorker->thread()->quit();
        m_currentWorker = nullptr;
    }

    restoreUiAfterFinish();
}

void MainWindow::switchToCancelMode()
{
    ui->startButton->setText("ОТМЕНА");
    ui->startButton->setStyleSheet("background-color: #d9534f; color: white;");
    disconnect(ui->startButton, &QPushButton::clicked, this, &MainWindow::on_startButton_clicked);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::on_cancelButton_clicked);
}

void MainWindow::restoreUiAfterFinish()
{
    ui->startButton->setText("СТАРТ");
    ui->startButton->setStyleSheet("");
    disconnect(ui->startButton, &QPushButton::clicked, this, &MainWindow::on_cancelButton_clicked);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::on_startButton_clicked);

    setUiEnabled(true);

    ui->downloadProgressBar->setVisible(false);
    ui->progressLabel->setVisible(false);
}

void MainWindow::onBitrateCheckRequest(const RenderPreset& preset, double actualBitrate)
{
    RerenderDialog dialog(preset, actualBitrate, this);
    bool accepted = (dialog.exec() == QDialog::Accepted);

    // Ответ нужно передать обратно в рабочий поток, где живет воркер
    if (m_currentWorker)
    {
        QString pass1 = accepted ? dialog.getCommandPass1() : "";
        QString pass2 = accepted ? dialog.getCommandPass2() : "";

        // Находим дочерний RenderHelper и вызываем его слот
        RenderHelper* helper = m_currentWorker->findChild<RenderHelper*>();
        if (helper)
        {
            QMetaObject::invokeMethod(helper, "onDialogFinished", Qt::QueuedConnection, Q_ARG(bool, accepted),
                                      Q_ARG(QString, pass1), Q_ARG(QString, pass2));
        }
    }
}

void MainWindow::onPauseForSubEditRequest(const QString& subFilePath)
{
    logMessage("Процесс приостановлен для ручного редактирования субтитров.", LogCategory::APP);
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("Файл готов к редактированию: <a href=\"file:///" + subFilePath + "\">Открыть в редакторе</a>");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setInformativeText(
        QString("Вы можете отредактировать файл субтитров:\n%1\n\nНажмите 'OK' для продолжения сборки.")
            .arg(QDir::toNativeSeparators(subFilePath)));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();

    emit subEditFinished();
}

bool MainWindow::isSrtSubsDecoupled() const
{
    // Чекбокс может быть скрыт, но его состояние isChecked() все равно будет верным
    return ui->decoupleSrtSubsCheckBox->isChecked();
}

void MainWindow::updateDecoupleCheckBoxState()
{
    QString currentTemplateName = ui->templateComboBox->currentText();
    if (currentTemplateName.isEmpty() || !m_templates.contains(currentTemplateName))
    {
        ui->decoupleContainerWidget->setVisible(false);
        return;
    }

    const ReleaseTemplate& currentTemplate = m_templates.value(currentTemplateName);

    bool canBeVisible = !ui->overrideSubsPathEdit->text().isEmpty() &&
                        QFileInfo::exists(ui->overrideSubsPathEdit->text()) && currentTemplate.createSrtMaster;

    ui->decoupleContainerWidget->setVisible(canBeVisible);
    if (!canBeVisible)
    {
        ui->decoupleSrtSubsCheckBox->setChecked(false);
    }
}

void MainWindow::updateChaptersAutoVisibility()
{
    const QString name = ui->templateComboBox->currentText();
    if (name.isEmpty() || !m_templates.contains(name))
    {
        ui->chaptersAutoContainerWidget->setVisible(false);
        return;
    }
    ui->chaptersAutoContainerWidget->setVisible(m_templates.value(name).chaptersEnabled);
}

QList<ChapterMarker> MainWindow::loadChaptersFromSourcePath(const QString& sourcePath, qint64* durationNs) const
{
    if (durationNs)
    {
        *durationNs = 0;
    }
    const QString path = sourcePath.trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path))
    {
        return {};
    }

    if (path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
    {
        return ChapterHelper::loadChaptersFromFile(path);
    }

    const QString ffprobePath = AppSettings::instance().ffprobePath();
    if (ffprobePath.isEmpty() || !QFileInfo::exists(ffprobePath))
    {
        return {};
    }

    QProcess process;
    process.start(ffprobePath,
                  {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-show_chapters"),
                   QStringLiteral("-show_format"), QStringLiteral("-print_format"), QStringLiteral("json"),
                   QStringLiteral("-i"), path});
    if (!process.waitForFinished(10000) || process.exitCode() != 0)
    {
        return {};
    }

    const QByteArray json = process.readAllStandardOutput();
    if (durationNs)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        const QJsonObject formatObj = doc.object().value(QStringLiteral("format")).toObject();
        bool ok = false;
        const double durationSec = formatObj.value(QStringLiteral("duration")).toString().toDouble(&ok);
        if (ok && durationSec > 0.0)
        {
            *durationNs = static_cast<qint64>(durationSec * 1e9);
        }
    }
    return ChapterHelper::parseFfprobeChaptersJson(json);
}

void MainWindow::onManualChapterTimingsRequested(const QString& sourcePath)
{
    if (sourcePath.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("Тайминги глав"),
                                 QStringLiteral("Укажите источник глав (MKV/MP4 или XML), чтобы показать тайминги."));
        return;
    }

    qint64 durationNs = 0;
    const QList<ChapterMarker> chapters = loadChaptersFromSourcePath(sourcePath, &durationNs);
    if (chapters.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("Тайминги глав"),
                             QStringLiteral("Не удалось получить главы из выбранного источника."));
        return;
    }

    ChapterTimingsDialog dialog(ChapterHelper::buildChapterTimingSeconds(chapters, durationNs), this);
    dialog.exec();
}
