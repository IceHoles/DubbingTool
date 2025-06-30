#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "manualassembler.h"
#include "manualrenderer.h"
#include "templateeditor.h"
#include "settingsdialog.h"
#include "postgenerator.h"
#include "processmanager.h"
#include "styleselectordialog.h"
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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
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

    m_manualAssemblyWidget->updateTemplateList(m_templates.keys());
    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::templateDataRequested, this, &MainWindow::onRequestTemplateData);
    connect(m_manualAssemblyWidget, &ManualAssemblyWidget::assemblyRequested, this, &MainWindow::onManualAssemblyRequested);

    connect(m_manualRenderWidget, &ManualRenderWidget::renderRequested, this, &MainWindow::onManualRenderRequested);

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

void MainWindow::logMessage(const QString &message)
{
    QString timedMessage = QDateTime::currentDateTime().toString("hh:mm:ss") + " - " + message;
    ui->logOutput->appendPlainText(timedMessage);
}

void MainWindow::loadTemplates()
{
    m_templates.clear();
    ui->templateComboBox->clear();

    QDir templatesDir("templates");
    if (!templatesDir.exists()) {
        templatesDir.mkpath(".");
        logMessage("Создана папка 'templates' для хранения шаблонов.");
    }

    QStringList filter("*.json");
    QFileInfoList fileList = templatesDir.entryInfoList(filter, QDir::Files);

    for (const QFileInfo &fileInfo : fileList) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            logMessage("Ошибка: не удалось открыть файл шаблона " + fileInfo.fileName());
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
        logMessage("Ошибка: имя шаблона не может быть пустым.");
        return;
    }

    QString nameToSelect = t.templateName;

    if (!m_editingTemplateFileName.isEmpty() && m_editingTemplateFileName != t.templateName) {
        QFile::remove("templates/" + m_editingTemplateFileName + ".json");
    }

    QDir templatesDir("templates");
    QFile file(templatesDir.filePath(t.templateName + ".json"));
    if (!file.open(QIODevice::WriteOnly)) {
        logMessage("Ошибка: не удалось сохранить файл шаблона " + t.templateName);
        return;
    }

    QJsonObject jsonObj;
    t.write(jsonObj);

    file.write(QJsonDocument(jsonObj).toJson(QJsonDocument::Indented));
    file.close();

    logMessage("Шаблон '" + t.templateName + "' успешно сохранен.");
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
    if (editor.exec() == QDialog::Accepted) {
        ReleaseTemplate newTemplate = editor.getTemplate();
        saveTemplate(newTemplate);
    }
}

void MainWindow::on_editTemplateButton_clicked()
{
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty() || !m_templates.contains(currentName)) {
        logMessage("Ошибка: выберите существующий шаблон для редактирования.");
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
        logMessage("Ошибка: нечего удалять.");
        return;
    }

    auto reply = QMessageBox::question(this, "Подтверждение удаления",
                                       "Вы уверены, что хотите удалить шаблон '" + currentName + "'?",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QFile::remove("templates/" + currentName + ".json");
        logMessage("Шаблон '" + currentName + "' удален.");
        loadTemplates();
    }
}

void MainWindow::on_startButton_clicked()
{
    if (!m_activeProcessManagers.isEmpty()) {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.");
        return;
    }

    m_publicationWidget->clearData();
    int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
    if (pubIndex != -1) {
        ui->mainTabWidget->setTabEnabled(pubIndex, false);
    }

    QString manualMkvPath = ui->mkvPathLineEdit->text();
    if (manualMkvPath.isEmpty() && ui->episodeNumberLineEdit->text().isEmpty()) {
        logMessage("Ошибка: укажите номер серии для автоматического режима или выберите MKV-файл для ручного.");
        return;
    }
    QString currentName = ui->templateComboBox->currentText();
    if (currentName.isEmpty()) {
        logMessage("Ошибка: выберите шаблон.");
        return;
    }
    ReleaseTemplate currentTemplate = m_templates.value(currentName);
    QString episodeNumber = ui->episodeNumberLineEdit->text();
    QSettings settings("MyCompany", "DubbingTool");

    QThread* thread = new QThread(this);
    WorkflowManager* workflowManager = new WorkflowManager(currentTemplate, episodeNumber, settings, this);

    connect(workflowManager, &WorkflowManager::missingFilesRequest, this, &MainWindow::onMissingFilesRequest, Qt::QueuedConnection);
    connect(this, &MainWindow::missingFilesProvided, workflowManager, [workflowManager](const QString &audioPath, const QMap<QString, QString> &resolvedFonts){
        workflowManager->resumeWithMissingFiles(audioPath, resolvedFonts);
    });
    connect(workflowManager, &WorkflowManager::signStylesRequest, this, &MainWindow::onSignStylesRequest, Qt::QueuedConnection);
    connect(this, &MainWindow::signStylesProvided, workflowManager, [workflowManager](const QStringList &styles){
        workflowManager->resumeWithSignStyles(styles);
    });
    m_activeProcessManagers.append(workflowManager->getProcessManager());

    workflowManager->moveToThread(thread);

    if (!manualMkvPath.isEmpty()) {
        connect(thread, &QThread::started, workflowManager, [workflowManager, manualMkvPath](){
            workflowManager->startWithManualFile(manualMkvPath);
        });
    } else {
        connect(thread, &QThread::started, workflowManager, &WorkflowManager::start);
    }

    connect(workflowManager, &WorkflowManager::postsReady, this, &MainWindow::onPostsReady);
    connect(workflowManager, &WorkflowManager::filesReady, this, &MainWindow::onFilesReady);
    connect(workflowManager, &WorkflowManager::workflowAborted, this, &MainWindow::onWorkflowAborted);

    connect(workflowManager, &WorkflowManager::filesReady, thread, &QThread::quit);
    connect(workflowManager, &WorkflowManager::workflowAborted, thread, &QThread::quit);

    connect(thread, &QThread::finished, this, [this, workflowManager](){
        m_activeProcessManagers.removeOne(workflowManager->getProcessManager());
        logMessage("============ ПРОЦЕСС ЗАВЕРШЕН ============");
        ui->startButton->setEnabled(true);
        ui->selectMkvButton->setEnabled(true);
        ui->selectAudioButton->setEnabled(true);
        ui->downloadProgressBar->setVisible(false);
        ui->progressLabel->setVisible(false);
    });
    connect(thread, &QThread::finished, workflowManager, &WorkflowManager::deleteLater);
    connect(workflowManager, &WorkflowManager::destroyed, thread, &QThread::deleteLater);

    connect(workflowManager, &WorkflowManager::logMessage, this, &MainWindow::logMessage);
    connect(workflowManager, &WorkflowManager::progressUpdated, this, &MainWindow::updateProgress);

    ui->startButton->setEnabled(false);
    ui->selectMkvButton->setEnabled(false);
    ui->selectAudioButton->setEnabled(false);
    thread->start();
}

void MainWindow::on_selectMkvButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите MKV файл", "", "MKV файлы (*.mkv)");
    if (filePath.isEmpty()) {
        return;
    }

    // Заполняем поле пути
    ui->mkvPathLineEdit->setText(filePath);

    if (ui->episodeNumberLineEdit->text().isEmpty()) {
        // Простая регулярка для поиска числа, окруженного пробелами, тире или в конце строки
        // Это более надежно, чем просто последнее число
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
            logMessage("Номер серии был извлечен из имени файла: " + episodeNumber);
        } else {
            logMessage("Не удалось автоматически извлечь номер серии из имени файла.");
        }
    }
}

// void MainWindow::onWorkflowFinished(const ReleaseTemplate &t, const EpisodeData &data, const QString &mkvPath, const QString &mp4Path)
// {
//     logMessage("============ ПРОЦЕСС УСПЕШНО ЗАВЕРШЕН ============");

//     // Сначала показываем панель публикации с полученными данными
//     showPublicationPanel(t, data, mkvPath, mp4Path);

//     // Затем сбрасываем UI в исходное состояние
//     ui->startButton->setEnabled(true);
//     ui->selectMkvButton->setEnabled(true);
//     ui->selectAudioButton->setEnabled(true);

//     ui->downloadProgressBar->setVisible(false);
//     ui->progressLabel->setVisible(false);
//     ui->downloadProgressBar->setValue(0);

//     m_workflowManager = nullptr; // Указатель обнуляется
// }

void MainWindow::onWorkflowAborted()
{
    logMessage("============ ПРОЦЕСС ПРЕРВАН С ОШИБКОЙ ============");

    // // Просто сбрасываем UI в исходное состояние
    // ui->startButton->setEnabled(true);
    // ui->selectMkvButton->setEnabled(true);
    // ui->selectAudioButton->setEnabled(true);

    // ui->downloadProgressBar->setVisible(false);
    // ui->progressLabel->setVisible(false);
    // ui->downloadProgressBar->setValue(0);

    // m_workflowManager = nullptr; // Указатель обнуляется
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::updateProgress(int percentage, const QString &stageName)
{
    // Показываем элементы, если они скрыты
    if (!ui->downloadProgressBar->isVisible()) {
        ui->downloadProgressBar->setVisible(true);
        ui->progressLabel->setVisible(true);
    }

    // Обновляем текст этапа, если он передан
    if (!stageName.isEmpty()) {
        ui->progressLabel->setText(QString("Текущий этап: %1").arg(stageName));
    }

    if (percentage < 0) {
        // Отрицательное значение будет сигналом для включения "бегающего" прогресс-бара
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(0);
        ui->downloadProgressBar->setValue(-1); // Для некоторых стилей это включает анимацию
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

void MainWindow::onManualAssemblyRequested(const QVariantMap &parameters)
{
    if (!m_activeProcessManagers.isEmpty()) {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.");
        return;
    }
    logMessage("Запрос на ручную сборку получен...");
    ui->mainTabWidget->setCurrentIndex(0);

    ManualAssembler *assembler = new ManualAssembler(parameters, nullptr);
    m_activeProcessManagers.append(assembler->getProcessManager());

    QThread* thread = new QThread(this);
    assembler->moveToThread(thread);

    connect(thread, &QThread::started, assembler, &ManualAssembler::start);
    connect(assembler, &ManualAssembler::finished, thread, &QThread::quit);

    connect(thread, &QThread::finished, this, [this, assembler](){
        m_activeProcessManagers.removeOne(assembler->getProcessManager());
        logMessage("Ручная сборка завершена.");
        ui->mainTabWidget->setEnabled(true);
    });
    connect(thread, &QThread::finished, assembler, &ManualAssembler::deleteLater);
    connect(assembler, &ManualAssembler::destroyed, thread, &QThread::deleteLater);

    connect(assembler, &ManualAssembler::logMessage, this, &MainWindow::logMessage);
    connect(assembler, &ManualAssembler::progressUpdated, this, &MainWindow::updateProgress);

    thread->start();
}

void MainWindow::onManualRenderRequested(const QVariantMap &parameters)
{
    if (!m_activeProcessManagers.isEmpty()) {
        logMessage("Другой процесс уже запущен. Дождитесь его завершения.");
        return;
    }
    logMessage("Запрос на ручной рендер получен...");
    ui->mainTabWidget->setCurrentIndex(0);

    ManualRenderer *renderer = new ManualRenderer(parameters, nullptr);
    m_activeProcessManagers.append(renderer->getProcessManager());

    QThread* thread = new QThread(this);
    renderer->moveToThread(thread);

    connect(thread, &QThread::started, renderer, &ManualRenderer::start);
    connect(renderer, &ManualRenderer::finished, thread, &QThread::quit);

    connect(thread, &QThread::finished, this, [this, renderer](){
        m_activeProcessManagers.removeOne(renderer->getProcessManager());
        logMessage("Ручной рендер завершен.");
        ui->mainTabWidget->setEnabled(true);
        updateProgress(0, "");
        ui->downloadProgressBar->setVisible(false);
        ui->progressLabel->setVisible(false);
    });
    connect(thread, &QThread::finished, renderer, &ManualRenderer::deleteLater);
    connect(renderer, &ManualRenderer::destroyed, thread, &QThread::deleteLater);

    connect(renderer, &ManualRenderer::logMessage, this, &MainWindow::logMessage);
    connect(renderer, &ManualRenderer::progressUpdated, this, [this](int percentage){
        updateProgress(percentage, "Ручной рендер");
    });

    thread->start();
}

void MainWindow::onPostsReady(const ReleaseTemplate &t, const EpisodeData &data)
{
    logMessage("Данные для постов готовы, открываю панель 'Публикация'.");

    m_lastTemplate = t;
    m_lastEpisodeData = data;
    m_lastMkvPath = ""; // Файлы еще не готовы
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
    logMessage("Файлы готовы, обновляю панель 'Публикация'.");
    m_lastMkvPath = mkvPath;
    m_lastMp4Path = mp4Path;

    // Обновляем только пути к файлам на уже открытой панели
    m_publicationWidget->setFilePaths(mkvPath, mp4Path);
}


// void MainWindow::showPublicationPanel(const ReleaseTemplate &t, const EpisodeData &data, const QString &mkvPath, const QString &mp4Path)
// {
//     m_publicationWidget->setData(t, data, mkvPath, mp4Path);

//     int pubIndex = ui->mainTabWidget->indexOf(m_publicationWidget);
//     ui->mainTabWidget->setTabEnabled(pubIndex, true);
//     ui->mainTabWidget->setCurrentIndex(pubIndex);
// }

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_activeProcessManagers.isEmpty()) {
        logMessage("Закрытие окна, принудительное завершение активных процессов...");
        // Создаем копию списка, так как он может изменяться во время итерации
        QList<ProcessManager*> managersToKill = m_activeProcessManagers;
        for (ProcessManager* manager : managersToKill) {
            manager->killProcess();
        }
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
    logMessage("Обновление постов с новыми ссылками...");
    m_lastEpisodeData.viewLinks = viewLinks;

    // Перегенерируем посты
    PostGenerator generator;
    QMap<QString, PostVersions> postTexts = generator.generate(m_lastTemplate, m_lastEpisodeData);

    // Вызываем тот же метод, чтобы обновить все данные на панели
    m_publicationWidget->updateData(m_lastTemplate, m_lastEpisodeData, postTexts, m_lastMkvPath, m_lastMp4Path);

    QMessageBox::information(m_publicationWidget, "Успех", "Тексты постов обновлены.");
}

void MainWindow::onMissingFilesRequest(const QStringList &missingFonts)
{
    logMessage("Процесс приостановлен: требуются данные от пользователя.");
    MissingFilesDialog dialog(this);

    bool audioNeeded = ui->audioPathLineEdit->text().isEmpty();
    dialog.setAudioPathVisible(audioNeeded);
    dialog.setMissingFonts(missingFonts);

    if (dialog.exec() == QDialog::Accepted) {
        // --- Пользователь нажал "ОК" ---
        QString audioPath = dialog.getAudioPath();

        if (audioNeeded) {
            if (audioPath.isEmpty()) {
                // Пользователь не указал обязательное аудио, но нажал ОК.
                // Это критическая ошибка, прерываем процесс.
                QMessageBox::critical(this, "Ошибка", "Аудиодорожка является обязательной для продолжения. Сборка прервана.");
                logMessage("Критическая ошибка: аудиодорожка не была предоставлена. Процесс прерван.");
                // Отправляем специальный сигнал об отмене.
                // Путь к аудио "" будет индикатором отмены для WorkflowManager.
                emit missingFilesProvided("", {});
                return;
            }
            // Сохраняем путь в главном окне
            ui->audioPathLineEdit->setText(audioPath);
        }

        // Если дошли сюда, то либо аудио не требовалось, либо оно было предоставлено.
        logMessage("Данные от пользователя получены, возобновление процесса...");
        emit missingFilesProvided(audioPath, dialog.getResolvedFonts());

    } else {
        // --- Пользователь нажал "Отмена" ---
        logMessage("Пользователь отменил ввод данных. Процесс прерван.");
        // Отправляем специальный сигнал об отмене.
        // Пустой путь к аудио "" будет индикатором отмены.
        emit missingFilesProvided("", {});
    }
}

void MainWindow::onSignStylesRequest(const QString &subFilePath)
{
    logMessage("Процесс приостановлен: требуется выбрать стили для надписей.");
    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(subFilePath);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selected = dialog.getSelectedStyles();
        logMessage("Выбранные стили для надписей: " + selected.join(", "));
        emit signStylesProvided(selected);
    } else {
        logMessage("Выбор стилей отменен. Процесс прерван.");
        emit signStylesProvided({}); // Пустой список
    }
}
