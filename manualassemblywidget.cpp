#include "manualassemblywidget.h"
#include "ui_manualassemblywidget.h"
#include "fontfinder.h"
#include "appsettings.h"
#include <QListWidgetItem>
#include <QColor>
#include <QFileDialog>
#include <QMessageBox>
#include <QVariantMap>
#include <QStyle>


ManualAssemblyWidget::ManualAssemblyWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ManualAssemblyWidget)
{
    ui->setupUi(this);

    m_fontFinder = new FontFinder(this);
    connect(m_fontFinder, &FontFinder::finished, this, &ManualAssemblyWidget::onFontFinderFinished);

    m_templateModeIcon = this->style()->standardIcon(QStyle::SP_FileDialogListView);
    m_manualModeIcon = this->style()->standardIcon(QStyle::SP_FileDialogDetailedView);

    connect(ui->modeSwitchButton, &QToolButton::toggled, this, &ManualAssemblyWidget::onModeSwitched);
    connect(ui->convertAudioCheckBox, &QCheckBox::toggled, ui->convertAudioFormatComboBox, &QComboBox::setVisible);
    ui->modeSwitchButton->setChecked(false);
    onModeSwitched(false);
    ui->convertAudioFormatComboBox->setVisible(ui->convertAudioCheckBox->isChecked());
}

ManualAssemblyWidget::~ManualAssemblyWidget()
{
    delete ui;
}

void ManualAssemblyWidget::onModeSwitched(bool isManualMode)
{
    updateUiState(isManualMode);
}

void ManualAssemblyWidget::updateUiState(bool isManualMode)
{
    ui->templateLabel->setVisible(!isManualMode);
    ui->templateComboBox->setVisible(!isManualMode);
    ui->modeSwitchSpacer->changeSize(isManualMode ? 40 : 0, 20, isManualMode ? QSizePolicy::Expanding : QSizePolicy::Fixed);

    ui->pagesWidget->setCurrentIndex(isManualMode ? 1 : 0); // 0 = pageTemplate, 1 = pageManual

    if (isManualMode) {
        ui->modeSwitchButton->setIcon(m_manualModeIcon);
        ui->modeSwitchButton->setText("Ручной режим");
        ui->modeSwitchButton->setToolTip("Переключить в режим по шаблону");
    } else {
        ui->modeSwitchButton->setIcon(m_templateModeIcon);
        ui->modeSwitchButton->setText("Режим по шаблону");
        ui->modeSwitchButton->setToolTip("Переключить в ручной режим");
    }
}

void ManualAssemblyWidget::updateTemplateList(const QStringList &templateNames)
{
    QString current = ui->templateComboBox->currentText();

    // Блокируем сигналы, чтобы избежать лишнего вызова on_templateComboBox_currentIndexChanged
    ui->templateComboBox->blockSignals(true);
    ui->templateComboBox->clear();
    ui->templateComboBox->addItems(templateNames);
    ui->templateComboBox->blockSignals(false);

    // Пытаемся восстановить выбор
    int index = templateNames.indexOf(current);
    if (index != -1) {
        ui->templateComboBox->setCurrentIndex(index);
    } else {
        // Если старого выбора нет, эмулируем выбор первого элемента, чтобы поля заполнились
        if (ui->templateComboBox->count() > 0) {
            ui->templateComboBox->setCurrentIndex(0);
            on_templateComboBox_currentIndexChanged(0);
        }
    }
}

void ManualAssemblyWidget::on_templateComboBox_currentIndexChanged(int index)
{
    if (index == -1) return;
    QString templateName = ui->templateComboBox->currentText();
    emit templateDataRequested(templateName);
}

void ManualAssemblyWidget::onTemplateDataReceived(const ReleaseTemplate &t)
{
    ui->tbStartTimeEdit->setTime(QTime::fromString(t.endingStartTime, "H:mm:ss.zzz"));

    ui->tbStyleComboBox->clear();
    for(const auto& style : AppSettings::instance().tbStyles()) {
        ui->tbStyleComboBox->addItem(style.name);
    }
    ui->tbStyleComboBox->setCurrentText(t.defaultTbStyleName);

    ui->outputFileNameEdit->setText(QString("[DUB] %1 - 00.mkv").arg(t.seriesTitle));
}

void ManualAssemblyWidget::browseForFile(QLineEdit *lineEdit, const QString &caption, const QString &filter)
{
    QString path = QFileDialog::getOpenFileName(this, caption, "", filter);
    if (!path.isEmpty()) {
        lineEdit->setText(path);
    }
}

void ManualAssemblyWidget::on_browseVideo_clicked()
{
    browseForFile(ui->videoPathEdit, "Выберите видеофайл", "Видеофайлы (*.h264 *.hevc *.mkv *.mp4 *.avc);;Все файлы (*)");
}

void ManualAssemblyWidget::on_browseOriginalAudio_clicked()
{
    browseForFile(ui->originalAudioPathEdit, "Выберите оригинальную аудиодорожку", "Аудиофайлы (*.aac *.ac3 *.eac3 *.flac *.wav *.opus);;Все файлы (*)");
}

void ManualAssemblyWidget::on_browseRussianAudio_clicked()
{
    browseForFile(ui->russianAudioPathEdit, "Выберите русскую аудиодорожку", "Аудиофайлы (*.wav *.aac *.flac);;Все файлы (*)");
}

void ManualAssemblyWidget::on_browseSubtitles_clicked()
{
    browseForFile(ui->subtitlesPathEdit, "Выберите файл полных субтитров", "ASS Subtitles (*.ass)");
}

void ManualAssemblyWidget::on_browseSigns_clicked()
{
    browseForFile(ui->signsPathEdit, "Выберите файл надписей", "ASS Subtitles (*.ass)");
}

void ManualAssemblyWidget::on_analyzeSubsButton_clicked()
{
    QStringList filesToAnalyze;
    if (!ui->subtitlesPathEdit->text().isEmpty()) {
        filesToAnalyze << ui->subtitlesPathEdit->text();
    }
    if (!ui->signsPathEdit->text().isEmpty()) {
        filesToAnalyze << ui->signsPathEdit->text();
    }

    if (filesToAnalyze.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Выберите хотя бы один файл субтитров для анализа.");
        return;
    }

    // Блокируем кнопку, чтобы не запускать анализ дважды
    ui->analyzeSubsButton->setEnabled(false);
    ui->analyzeSubsButton->setText("Анализ...");
    m_fontFinder->findFontsInSubs(filesToAnalyze);
}

void ManualAssemblyWidget::on_addFontsButton_clicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, "Выберите файлы шрифтов", "",
                                                      "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");

    for(const QString& path : paths) {
        QListWidgetItem* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setForeground(Qt::magenta);
        item->setText(item->text() + " - добавлен вручную");
        item->setData(Qt::UserRole, path);
        ui->fontsListWidget->addItem(item);
    }
}

QVariantMap ManualAssemblyWidget::getParameters() const
{
    QVariantMap params;
    bool isManualMode = ui->modeSwitchButton->isChecked();
    params["isManualMode"] = isManualMode;

    if (ui->includeVideoCheckBox->isChecked()) params["videoPath"] = ui->videoPathEdit->text();
    if (ui->includeOriginalAudioCheckBox->isChecked()) params["originalAudioPath"] = ui->originalAudioPathEdit->text();
    if (ui->includeRussianAudioCheckBox->isChecked()) params["russianAudioPath"] = ui->russianAudioPathEdit->text();
    if (ui->includeSubtitlesCheckBox->isChecked()) params["subtitlesPath"] = ui->subtitlesPathEdit->text();
    if (ui->includeSignsCheckBox->isChecked()) params["signsPath"] = ui->signsPathEdit->text();

    params["normalizeAudio"] = ui->normalizeAudioCheckBox->isChecked();
    params["convertAudio"] = ui->convertAudioCheckBox->isChecked();

    if (params["convertAudio"].toBool()) {
        if (ui->convertAudioFormatComboBox->currentText() == "в AAC") {
            params["convertAudioFormat"] = "aac";
        } else {
            params["convertAudioFormat"] = "flac";
        }
    }

    params["workDir"] = ui->workDirEdit->text();
    params["outputName"] = ui->outputFileNameEdit->text();

    QStringList fontPaths;
    for(int i = 0; i < ui->fontsListWidget->count(); ++i) {
        QString path = ui->fontsListWidget->item(i)->data(Qt::UserRole).toString();
        if (!path.isEmpty()) fontPaths.append(path);
    }
    params["fontPaths"] = fontPaths;

    if (isManualMode) {
        params["studio"] = ui->studioEdit->text();
        params["language"] = ui->languageEdit->text();
        params["subAuthor"] = ui->subAuthorEdit->text();
    } else {
        params["templateName"] = ui->templateComboBox->currentText();
        params["addTb"] = ui->tbGroupBox->isChecked();
        if (ui->tbGroupBox->isChecked()) {
            params["tbStartTime"] = ui->tbStartTimeEdit->time().toString("H:mm:ss.zzz");
            params["tbStyleName"] = ui->tbStyleComboBox->currentText();
        }
    }

    return params;
}

void ManualAssemblyWidget::on_assembleButton_clicked()
{
    if (ui->templateComboBox->currentIndex() == -1) {
        QMessageBox::warning(this, "Ошибка", "Выберите базовый шаблон для использования метаданных.");
        return;
    }

    bool fontsMissing = false;
    for(int i = 0; i < ui->fontsListWidget->count(); ++i) {
        QListWidgetItem *item = ui->fontsListWidget->item(i);
        // Проверяем, что у элемента красный цвет, который мы задаем для ненайденных шрифтов
        if (item->foreground().color() == Qt::red) {
            fontsMissing = true;
            break;
        }
    }

    if (fontsMissing) {
        auto reply = QMessageBox::question(this, "Отсутствуют шрифты",
                                           "Некоторые шрифты не были найдены в системе (отмечены красным). Субтитры могут отображаться некорректно.\n\n"
                                           "Вы уверены, что хотите продолжить сборку?",
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return; // Пользователь отменил сборку
        }
    }

    emit assemblyRequested();
}

void ManualAssemblyWidget::on_browseWorkDirButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выберите рабочую папку");
    if (!dir.isEmpty()) {
        ui->workDirEdit->setText(dir);
    }
}

void ManualAssemblyWidget::onFontFinderFinished(const FontFinderResult &result)
{
    // Разблокируем кнопку
    ui->analyzeSubsButton->setEnabled(true);
    ui->analyzeSubsButton->setText("Анализировать субтитры");

    ui->fontsListWidget->clear();

    for (const FoundFontInfo& fontInfo : result.foundFonts) {
        QListWidgetItem* item = new QListWidgetItem(QString("%1 - НАЙДЕН -> %2").arg(fontInfo.familyName).arg(fontInfo.path));
        item->setForeground(Qt::darkGreen);
        item->setData(Qt::UserRole, fontInfo.path);
        item->setToolTip(fontInfo.path);
        ui->fontsListWidget->addItem(item);
    }

    for (const QString& fontName : result.notFoundFontNames) {
        QListWidgetItem* item = new QListWidgetItem(QString("%1 - НЕ НАЙДЕН В СИСТЕМЕ").arg(fontName));
        item->setForeground(Qt::red);
        ui->fontsListWidget->addItem(item);
    }

    if (ui->fontsListWidget->count() == 0) {
        QMessageBox::warning(this, "Анализ завершен", "Не удалось найти информацию о шрифтах в указанных файлах.");
    }
}

void ManualAssemblyWidget::setAssembling(bool assembling)
{
    ui->assembleButton->setDisabled(assembling);
}
