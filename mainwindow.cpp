#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "appsettings.h"
#include "manualassembler.h"
#include "manualrenderer.h"
#include "templateeditor.h"
#include "settingsdialog.h"
#include "postgenerator.h"
#include "processmanager.h"
#include "styleselectordialog.h"
#include "torrentselectordialog.h"
#include "trackselectordialog.h"
#include <QThread>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QFileDialog>
#include <QRegularExpression>
#include <QCloseEvent>
#include <QIcon>


static QString logCategoryToString(LogCategory category)
{
    switch (category) {
    case LogCategory::APP: return "APP";
    case LogCategory::FFMPEG: return "FFMPEG";
    case LogCategory::MKVTOOLNIX: return "MKVTOOLNIX";
    case LogCategory::QBITTORRENT: return "QBITTORRENT";
    case LogCategory::DEBUG: return "DEBUG";
    }
    return "UNKNOWN";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentWorker(nullptr)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/icon.png"));

    m_manualAssemblyWidget = new ManualAssemblyWidget(this);
    ui->manualTabLayout->addWidget(m_manualAssemblyWidget);
    m_manualRenderWidget = new ManualRenderWidget(this);
    ui->renderTabLayout->addWidget(m_manualRenderWidget);
    m_publicationWidget = new PublicationWidget(this);
    ui->mainTabWidget->addTab(m_publicationWidget, "Публикация");
    ui->mainTabWidget->setTabEnabled(ui->mainTabWidget->count() - 1, false);

    QAction *settingsAction = new QAction("Настройки", this);
    ui->menubar->addAction(settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::on_actionSettings_triggered);
    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::templateDataRequested, this, &MainWindow::onRequestTemplateData);

    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::assemblyRequested, this, &MainWindow::startManualAssembly);
    connect(m_manualRenderWidget, &ManualRenderWidget::renderRequested, this, &MainWindow::startManualRender);

    connect(m_publicationWidget, &PublicationWidget::logMessage, this, &MainWindow::logMessage);
    connect(m_publicationWidget, &PublicationWidget::postsUpdateRequest, this, &MainWindow::onPostsUpdateRequest);

    ui->downloadProgressBar->setVisible(false);
    ui->progressLabel->setVisible(false);

    loadTemplates();

    if(ui->templateComboBox->count() == 0) {
        logMessage("Шаблоны не найдены. Создайте новый шаблон.");
    } else {
        logMessage(QString("Загружено %1 шаблонов.").arg(m_templates.count()));
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::logMessage(const QString &message, LogCategory category)
{
    const auto& enabledCategories = AppSettings::instance().enabledLogCategories();
    if (!enabledCategories.contains(category)) {
        return;
    }

    QString categoryName = logCategoryToString(category);
    QString timedMessage = QString("[%1] %2 - %3")
                               .arg(categoryName)
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                               .arg(message.trimmed());
    ui->logOutput->appendPlainText(timedMessage);
}

void MainWindow::loadTemplates()
{
    m_templates.clear();
    ui->templateComboBox->clear();

    QDir templatesDir("templates");
    if (!templatesDir.exists()) {
        templatesDir.mkpath(".");
        logMessage("Создана папка 'templates' для хранения шаблонов.", LogCategory::APP);
    }

    QStringList filter("*.json");
    QFileInfoList fileList = templatesDir.entryInfoList(filter, QDir::Files);

    for (const QFileInfo &fileInfo : fileList) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            logMessage("Ошибка: не удалось открыть файл шаблона " + fileInfo.fileName(), LogCategory::APP);
            continue;
        }

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        ReleaseTemplate t;
        t.read(doc.object());

        if (!t.templateName.isEmpty()) {
            m_templates.insert(t.templateName, t);
        }
    }

    ui->templateComboBox->addItems(m_templates.keys());

    if (m_manualAssemblyWidget) {
        m_manualAssemblyWidget->updateTemplateList(m_templates.keys());
    }
}

void MainWindow::saveTemplate(const ReleaseTemplate &t)
{
    if (t.templateName.isEmpty()) {
        logMessage("Ошибка: имя шаблона не может быть пустым.", LogCategory::APP);
        return;
    }

    QString nameToSelect = t.templateName;

    if (!m_editingTemplateFileName.isEmpty() && m_editingTemplateFileName != t.templateName) {
        QFile::remove("templates/" + m_editingTemplateFileName + ".json");
    }

    QDir templatesDir("templates");
    QFile file(templatesDir.filePath(t.templateName + ".json"));
    if (!file.open(QIODevice::WriteOnly)) {
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
    if (index != -1) { // -1 означает, что текст не найден
        ui->templateComboBox->setCurrentIndex(index);
    }
}

void MainWindow::on_createTemplateButton_clicked()
{
    m_editingTemplateFileName.clear();
    TemplateEditor editor(this);

    ReleaseTemplate defaultTemplate;
    defaultTemplate.templateName = "Новый шаблон";
    defaultTemplate.seriesTitle = "Как в архиве mkv";
    defaultTemplate.seriesTitleForPost = "Как в постах";
    defaultTemplate.rssUrl = QUrl("https://example.com/rss.xml");
    defaultTemplate.animationStudio = "STUDIO";
    defaultTemplate.subAuthor = "Crunchyroll";
    defaultTemplate.originalLanguage = "jpn";
    defaultTemplate.endingChapterName = "Ending Start";
    defaultTemplate.totalEpisodes = 12;

    defaultTemplate.director = "Режиссер Дубляжа";
    defaultTemplate.soundEngineer = "Звукорежиссер";
    defaultTemplate.timingAuthor = "Таймингер";
    defaultTemplate.releaseBuilder = "Сборщик Релиза";
    defaultTemplate.cast << "Актер 1" << "Актер 2" << "Актер 3";

    defaultTemplate.postTemplates["tg_mp4"] =
        "▶️Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
        "📌«%SERIES_TITLE%» в дубляже от ТО Дубляжная\n\n"
        "🎁А также вы можете поддержать наш коллектив копеечкой (https://boosty.to/dubl/single-payment/donation/696237/target?share=target_link)\n💙ВК(https://vk.com/dublyajnaya?from=groups&ref=group_widget&w=app6471849_-216649949)\n💰BOOSTY(https://boosty.to/dubl/single-payment/donation/696237/target?share=target_link)\n\n"
        "Anime365 (%LINK_ANIME365%)\n\n"
        "AnimeLib (%LINK_ANILIB%)\n\n"
        "Архив MKV (https://t.me/+CVpSSg33UwI4MzYy)\n\n"
        "🎙Роли дублировали:\n%CAST_LIST%\n\n"
        "📝Режиссёр дубляжа:\n%DIRECTOR%\n\n"
        "🪄Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
        "📚Перевод:\n%SUB_AUTHOR%\n\n"
        "✏️Разметка:\n%TIMING_AUTHOR%\n\n"
        "✨Локализация постера:\nКирилл Хоримиев\n\n"
        "📦Сборка релиза:\n%RELEASE_BUILDER%\n\n"
        "#Дубрелиз@dublyajnaya #Хештег@dublyajnaya #Дубляж@dublyajnaya";
    defaultTemplate.postTemplates["tg_mkv"] =
        "%SERIES_TITLE%\n"
        "Серия %EPISODE_NUMBER%/%TOTAL_EPISODES%\n"
        "#Хештег";
    defaultTemplate.postTemplates["vk"] =
        "%SERIES_TITLE% в дубляже от ТО Дубляжная\n\n"
        "Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
        "Роли дублировали:\n%CAST_LIST%\n\n"
        "Режиссёр дубляжа:\n%DIRECTOR%\n\n"
        "Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
        "Перевод:\n%SUB_AUTHOR%\n\n"
        "️Разметка:\n%TIMING_AUTHOR%\n\n"
        "Локализация постера:\nКирилл Хоримиев\n\n"
        "Сборка релиза:\n%RELEASE_BUILDER%\n\n"
        "#Хештег";
    defaultTemplate.postTemplates["vk_comment"] =
        "А также вы можете поддержать наш коллектив на бусти: https://boosty.to/dubl/single-payment/donation/634652\n\n"
        "ТГ: https://t.me/dublyajnaya\n\n"
        "Anime365: %LINK_ANIME365%\n"
        "AnimeLib: %LINK_ANILIB%\n";
    defaultTemplate.uploadUrls << "https://vk.com/dublyajnaya" << "https://converter.kodik.biz/media-files" << "https://anime-365.ru/" << "https://anilib.me/ru";
    editor.setTemplate(defaultTemplate);

    if (editor.exec() == QDialog::Accepted) {
        ReleaseTemplate newTemplate = editor.getTemplate();
        if (newTemplate.templateName.isEmpty()) {
            QMessageBox::warning(&editor, "Ошибка", "Имя шаблона не может быть пустым.");
            return;
        }
        saveTemplate(newTemplate);
    }
}

void MainWindow::on_editTemplateButton_clicked()
{
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty() || !m_templates.contains(currentName)) {
        logMessage("Ошибка: выберите существующий шаблон для редактирования.", LogCategory::APP);
        return;
    }

    m_editingTemplateFileName = currentName;

    TemplateEditor editor(this);
    editor.setTemplate(m_templates.value(currentName));
    if (editor.exec() == QDialog::Accepted) {
        ReleaseTemplate updatedTemplate = editor.getTemplate();
        saveTemplate(updatedTemplate);
    }
}

void MainWindow::on_deleteTemplateButton_clicked()
{
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty()) {
        logMessage("Ошибка: нечего удалять.", LogCategory::APP);
        return;
    }

    auto reply = QMessageBox::question(this, "Подтверждение удаления",
                                       "Вы уверены, что хотите удалить шаблон '" + currentName + "'?",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QFile::remove("templates/" + currentName + ".json");
        logMessage("Шаблон '" + currentName + "' удален.", LogCategory::APP);
        loadTemplates();
    }
}

void MainWindow::on_cancelButton_clicked()
{
    if (m_currentWorker) {
        logMessage("Отправка запроса на отмену...", LogCategory::APP);
        QMetaObject::invokeMethod(m_currentWorker, "cancelOperation", Qt::QueuedConnection);
    }
}

void MainWindow::on_startButton_clicked()
{
    if (m_currentWorker) {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.", LogCategory::APP);
        return;
    }

    m_publicationWidget->clearData();
    int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
    if (pubIndex != -1) {
        ui->mainTabWidget->setTabEnabled(pubIndex, false);
    }

    QString manualMkvPath = ui->mkvPathLineEdit->text();
    if (manualMkvPath.isEmpty() && ui->episodeNumberLineEdit->text().isEmpty()) {
        logMessage("Ошибка: укажите номер серии или выберите MKV-файл.", LogCategory::APP);
        return;
    }
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty()) {
        logMessage("Ошибка: выберите шаблон.", LogCategory::APP);
        return;
    }

    switchToCancelMode();
    setUiEnabled(false);

    ReleaseTemplate currentTemplate = m_templates.value(currentName);
    QString episodeNumberStr = ui->episodeNumberLineEdit->text();
    bool ok;
    int epNum = episodeNumberStr.toInt(&ok);
    if (!ok && manualMkvPath.isEmpty()) {
        logMessage("Ошибка: введенный номер серии не является числом.", LogCategory::APP);
        return;
    } else if (ok) {
        episodeNumberStr = QString::number(epNum);
    }
    QString episodeForPost = episodeNumberStr;
    QString episodeForSearch = QString("%1").arg(epNum, 2, 10, QChar('0'));

    QSettings settings("MyCompany", "DubbingTool");

    setUiEnabled(false);

    QThread* thread = new QThread(this);
    WorkflowManager* workflowManager = new WorkflowManager(currentTemplate, episodeForPost, episodeForSearch, settings, this);

    m_currentWorker = workflowManager; // Сохраняем указатель на текущего воркера
    m_activeProcessManagers.append(workflowManager->getProcessManager());
    connect(workflowManager, &WorkflowManager::multipleTorrentsFound, this, &MainWindow::onMultipleTorrentsFound, Qt::QueuedConnection);
    connect(this, &MainWindow::torrentSelected, workflowManager, &WorkflowManager::resumeWithSelectedTorrent);
    connect(workflowManager, &WorkflowManager::multipleAudioTracksFound, this, &MainWindow::onMultipleAudioTracksFound, Qt::QueuedConnection);
    connect(this, &MainWindow::audioTrackSelected, workflowManager, &WorkflowManager::resumeWithSelectedAudioTrack);
    connect(workflowManager, &WorkflowManager::missingFilesRequest, this, &MainWindow::onMissingFilesRequest, Qt::QueuedConnection);
    connect(this, &MainWindow::missingFilesProvided, workflowManager, &WorkflowManager::resumeWithMissingFiles);
    connect(workflowManager, &WorkflowManager::signStylesRequest, this, &MainWindow::onSignStylesRequest, Qt::QueuedConnection);
    connect(this, &MainWindow::signStylesProvided, workflowManager, &WorkflowManager::resumeWithSignStyles);
    connect(workflowManager, &WorkflowManager::bitrateCheckRequest, this, &MainWindow::onBitrateCheckRequest, Qt::QueuedConnection);

    connect(workflowManager, &WorkflowManager::pauseForSubEditRequest, this, &MainWindow::onPauseForSubEditRequest, Qt::QueuedConnection);
    connect(this, &MainWindow::subEditFinished, workflowManager, &WorkflowManager::resumeAfterSubEdit);

    workflowManager->moveToThread(thread);

    if (!manualMkvPath.isEmpty()) {
        connect(thread, &QThread::started, workflowManager, [workflowManager, manualMkvPath](){
            workflowManager->startWithManualFile(manualMkvPath);
        });
    } else {
        connect(thread, &QThread::started, workflowManager, &WorkflowManager::start);
    }
    connect(workflowManager, &WorkflowManager::finished, this, &MainWindow::finishWorkerProcess);
    connect(workflowManager, &WorkflowManager::workflowAborted, this, &MainWindow::finishWorkerProcess);

    connect(workflowManager, &WorkflowManager::postsReady, this, &MainWindow::onPostsReady);
    connect(workflowManager, &WorkflowManager::filesReady, this, &MainWindow::onFilesReady);
    connect(workflowManager, &WorkflowManager::filesReady, thread, &QThread::quit);
    connect(thread, &QThread::finished, workflowManager, &WorkflowManager::deleteLater);
    connect(workflowManager, &WorkflowManager::destroyed, thread, &QThread::deleteLater);
    connect(workflowManager, &WorkflowManager::logMessage, this, &MainWindow::logMessage);
    connect(workflowManager, &WorkflowManager::progressUpdated, this, &MainWindow::updateProgress);

    thread->start();
}

void MainWindow::on_selectMkvButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите MKV файл", "", "MKV файлы (*.mkv)");
    if (filePath.isEmpty()) {
        return;
    }
    ui->mkvPathLineEdit->setText(filePath);

    if (ui->episodeNumberLineEdit->text().isEmpty()) {
        // Простая регулярка для поиска числа, окруженного пробелами, тире или в конце строки
        QRegularExpression re(" - (\\d{1,3})[ ._]");
        QRegularExpressionMatch match = re.match(filePath);

        if (!match.hasMatch()) {
            // Если не нашли, пробуем найти просто последнее число в имени
            re.setPattern("(\\d+)(?!.*\\d)");
            match = re.match(QFileInfo(filePath).baseName());
        }

        if (match.hasMatch()) {
            QString episodeNumber = match.captured(1);
            ui->episodeNumberLineEdit->setText(episodeNumber);
            logMessage("Номер серии был извлечен из имени файла: " + episodeNumber, LogCategory::APP);
        } else {
            logMessage("Не удалось автоматически извлечь номер серии из имени файла.", LogCategory::APP);
        }
    }
}

void MainWindow::onMultipleTorrentsFound(const QList<TorrentInfo> &candidates)
{
    TorrentSelectorDialog dialog(candidates, this);
    if (dialog.exec() == QDialog::Accepted) {
        int selectedIndex = dialog.getSelectedIndex();
        if (selectedIndex >= 0 && selectedIndex < candidates.size()) {
            emit torrentSelected(candidates[selectedIndex]);
        }
    } else {
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
    if (dialog.exec() == QDialog::Accepted) {
        // Пользователь нажал "ОК", и данные уже сохранены на диск.
        // Теперь нужно обновить данные в работающем приложении.

        // 1. Заново загружаем все настройки из файла в память.
        emit logMessage("Настройки обновлены. Перезагрузка конфигурации...", LogCategory::APP);
        AppSettings::instance().load();

        // 2. Обновляем все виджеты, которые зависят от настроек.
        // Сейчас это только список пресетов в ручном рендере.
        // В будущем здесь могут быть и другие виджеты.
        m_manualRenderWidget->updateRenderPresets();

        // Также нужно обновить список пресетов в редакторе шаблонов,
        // но он и так обновляется каждый раз при открытии, так что это не обязательно.
        emit logMessage("Конфигурация успешно перезагружена.", LogCategory::APP);
    }
}

void MainWindow::updateProgress(int percentage, const QString &stageName)
{
    if (!ui->downloadProgressBar->isVisible()) {
        ui->downloadProgressBar->setVisible(true);
        ui->progressLabel->setVisible(true);
    }

    if (!stageName.isEmpty()) {
        ui->progressLabel->setText(QString("Текущий этап: %1").arg(stageName));
    }

    if (percentage < 0) {
        // Отрицательное значение будет сигналом для включения "бегающего" прогресс-бара
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(0);
        ui->downloadProgressBar->setValue(-1);
    } else {
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(100);
        ui->downloadProgressBar->setValue(percentage);
    }
}

void MainWindow::on_selectAudioButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите аудиофайл", "", "Аудиофайлы (*.wav *.flac *.aac)");
    if (!filePath.isEmpty()) {
        ui->audioPathLineEdit->setText(filePath);
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

void MainWindow::onRequestTemplateData(const QString &templateName)
{
    if (m_templates.contains(templateName)) {
        m_manualAssemblyWidget->onTemplateDataReceived(m_templates.value(templateName));
    }
}

void MainWindow::startManualAssembly()
{
    if (m_currentWorker) {
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
    connect(worker, &ManualAssembler::finished, this, &MainWindow::finishWorkerProcess);
    connect(worker, &ManualAssembler::logMessage, this, &MainWindow::logMessage);
    connect(worker, &ManualAssembler::progressUpdated, this, &MainWindow::updateProgress);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::startManualRender()
{
    if (m_currentWorker) {
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
    connect(worker, &ManualRenderer::progressUpdated, this, [this](int p){ updateProgress(p); });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, thread, &QObject::deleteLater);
    connect(worker, &ManualRenderer::bitrateCheckRequest, this, &MainWindow::onBitrateCheckRequest, Qt::QueuedConnection);

    thread->start();
}

void MainWindow::onPostsReady(const ReleaseTemplate &t, const EpisodeData &data)
{
    logMessage("Данные для постов готовы, открываю панель 'Публикация'.", LogCategory::APP);

    m_lastTemplate = t;
    m_lastEpisodeData = data;
    m_lastMkvPath = "";
    m_lastMp4Path = "";

    PostGenerator generator;
    QMap<QString, PostVersions> postTexts = generator.generate(m_lastTemplate, m_lastEpisodeData);

    m_publicationWidget->updateData(m_lastTemplate, m_lastEpisodeData, postTexts, m_lastMkvPath, m_lastMp4Path);

    int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
    if (pubIndex != -1) {
        ui->mainTabWidget->setTabEnabled(pubIndex, true);
    }
}


void MainWindow::onFilesReady(const QString &mkvPath, const QString &mp4Path)
{
    logMessage("Файлы готовы, обновляю панель 'Публикация'.", LogCategory::APP);
    m_lastMkvPath = mkvPath;
    m_lastMp4Path = mp4Path;

    m_publicationWidget->setFilePaths(mkvPath, mp4Path);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    AppSettings::instance().save();
    if (m_currentWorker) {
        logMessage("Запрошена отмена операции перед закрытием...", LogCategory::APP);
        QMetaObject::invokeMethod(m_currentWorker, "cancelOperation", Qt::QueuedConnection);

        if (m_currentWorker && m_currentWorker->thread()) {
            if (!m_currentWorker->thread()->wait(5000)) {
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
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ASS файл", "", "ASS Subtitles (*.ass)");
    if (!filePath.isEmpty()) {
        ui->overrideSubsPathEdit->setText(filePath);
    }
}

void MainWindow::on_browseOverrideSignsButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ASS файл", "", "ASS Subtitles (*.ass)");
    if (!filePath.isEmpty()) {
        ui->overrideSignsPathEdit->setText(filePath);
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

void MainWindow::onPostsUpdateRequest(const QMap<QString, QString> &viewLinks)
{
    logMessage("Обновление постов с новыми ссылками...", LogCategory::APP);
    m_lastEpisodeData.viewLinks = viewLinks;

    PostGenerator generator;
    QMap<QString, PostVersions> postTexts = generator.generate(m_lastTemplate, m_lastEpisodeData);
    m_publicationWidget->updateData(m_lastTemplate, m_lastEpisodeData, postTexts, m_lastMkvPath, m_lastMp4Path);

    QMessageBox::information(m_publicationWidget, "Успех", "Тексты постов обновлены.");
}

void MainWindow::onMissingFilesRequest(const QStringList &missingFonts, bool requireWav, bool requireTime)
{
    logMessage("Процесс приостановлен: требуются данные от пользователя.", LogCategory::APP);
    MissingFilesDialog dialog(this);

    bool audioNeeded = ui->audioPathLineEdit->text().isEmpty();
    if (requireWav) {
        audioNeeded = true;
        dialog.setAudioPrompt("Для SRT-мастера требуется несжатый WAV-файл:");
    } else {
        dialog.setAudioPrompt("Не удалось найти русскую аудиодорожку. Укажите путь к ней:");
    }

    dialog.setAudioPathVisible(audioNeeded);
    dialog.setMissingFonts(missingFonts);
    dialog.setTimeInputVisible(requireTime);

    if (requireTime && !requireWav && missingFonts.isEmpty()) {
        dialog.setTimePrompt("Времени для ТБ недостаточно. Укажите более раннее время начала эндинга:");
    }

    if (dialog.exec() == QDialog::Accepted) {
        QString audioPath = dialog.getAudioPath();
        QString time = dialog.getTime();
        ui->audioPathLineEdit->setText(audioPath);
        if (audioNeeded && audioPath.isEmpty()) {
            QMessageBox::critical(this, "Ошибка", "Аудиодорожка является обязательной. Сборка прервана.");
            emit missingFilesProvided("", {}, "");
            return;
        }
        if (requireTime && time.isEmpty()) {
            QMessageBox::critical(this, "Ошибка", "Время эндинга является обязательным. Сборка прервана.");
            emit missingFilesProvided("", {}, "");
            return;
        }

        logMessage("Данные от пользователя получены, возобновление процесса...", LogCategory::APP);
        emit missingFilesProvided(audioPath, dialog.getResolvedFonts(), time);

    } else {
        logMessage("Пользователь отменил ввод данных. Процесс прерван.", LogCategory::APP);
        emit missingFilesProvided("", {}, "");
    }
}

void MainWindow::onSignStylesRequest(const QString &subFilePath)
{
    logMessage("Процесс приостановлен: требуется выбрать стили для надписей.", LogCategory::APP);
    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(subFilePath);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selected = dialog.getSelectedStyles();
        logMessage("Выбранные стили для надписей: " + selected.join(", "), LogCategory::APP);
        emit signStylesProvided(selected);
    } else {
        logMessage("Выбор стилей отменен. Процесс прерван.", LogCategory::APP);
        emit signStylesProvided({}); // Пустой список
    }
}

void MainWindow::onMultipleAudioTracksFound(const QList<AudioTrackInfo> &candidates)
{
    TrackSelectorDialog dialog(candidates, this);
    if (dialog.exec() == QDialog::Accepted) {
        emit audioTrackSelected(dialog.getSelectedTrackId());
    } else {
        logMessage("Выбор аудиодорожки отменен пользователем. Процесс прерван.", LogCategory::APP);
        emit audioTrackSelected(-1); // -1 будет сигналом к отмене
    }
}

void MainWindow::setUiEnabled(bool enabled)
{
    ui->createTemplateButton->setEnabled(enabled);
    ui->editTemplateButton->setEnabled(enabled);
    ui->deleteTemplateButton->setEnabled(enabled);

    if (m_manualAssemblyWidget) m_manualAssemblyWidget->findChild<QPushButton*>("assembleButton")->setEnabled(enabled);
    if (m_manualRenderWidget) m_manualRenderWidget->findChild<QPushButton*>("renderButton")->setEnabled(enabled);
}

void MainWindow::finishWorkerProcess()
{
    logMessage("============ ПРОЦЕСС ЗАВЕРШЕН ============", LogCategory::APP);

    if (m_currentWorker) {
        if(auto ptr = qobject_cast<WorkflowManager*>(m_currentWorker.data())) m_activeProcessManagers.removeOne(ptr->getProcessManager());
        else if(auto ptr = qobject_cast<ManualAssembler*>(m_currentWorker.data())) m_activeProcessManagers.removeOne(ptr->getProcessManager());
        else if(auto ptr = qobject_cast<ManualRenderer*>(m_currentWorker.data())) m_activeProcessManagers.removeOne(ptr->getProcessManager());

        if(m_currentWorker->thread()) m_currentWorker->thread()->quit();
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

void MainWindow::onBitrateCheckRequest(const RenderPreset &preset, double actualBitrate)
{
    RerenderDialog dialog(preset, actualBitrate, this);
    bool accepted = (dialog.exec() == QDialog::Accepted);

    // Ответ нужно передать обратно в рабочий поток, где живет воркер
    if (m_currentWorker) {
        QString pass1 = accepted ? dialog.getCommandPass1() : "";
        QString pass2 = accepted ? dialog.getCommandPass2() : "";

        // Находим дочерний RenderHelper и вызываем его слот
        RenderHelper* helper = m_currentWorker->findChild<RenderHelper*>();
        if(helper) {
            QMetaObject::invokeMethod(helper, "onDialogFinished", Qt::QueuedConnection,
                                      Q_ARG(bool, accepted),
                                      Q_ARG(QString, pass1),
                                      Q_ARG(QString, pass2));
        }
    }
}

void MainWindow::onPauseForSubEditRequest(const QString &subFilePath)
{
    logMessage("Процесс приостановлен для ручного редактирования субтитров.", LogCategory::APP);
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("Процесс приостановлен");
    msgBox.setInformativeText(QString("Вы можете отредактировать файл субтитров:\n%1\n\nНажмите 'OK' для продолжения сборки.").arg(QDir::toNativeSeparators(subFilePath)));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();

    emit subEditFinished();
}
