#include "templateeditor.h"
#include "ui_templateeditor.h"
#include "styleselectordialog.h"
#include <QFileDialog>
#include <QTableWidgetItem>
#include <QDialog>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QWidget>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextDocumentFragment>


TemplateEditor::TemplateEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TemplateEditor)
{
    ui->setupUi(this);


    connect(ui->browsePosterButton, &QPushButton::clicked, this, [this](){
        QString path = QFileDialog::getOpenFileName(this, "Выберите файл постера", "", "Изображения (*.png *.jpg *.jpeg *.webp)");
        if (!path.isEmpty()) {
            ui->posterPathEdit->setText(path);
        }
    });
}

TemplateEditor::~TemplateEditor()
{
    delete ui;
}

void TemplateEditor::setTemplate(const ReleaseTemplate &t)
{
    // Вкладка "Основные"
    ui->templateNameEdit->setText(t.templateName);
    ui->seriesTitleEdit->setText(t.seriesTitle);
    ui->rssUrlEdit->setText(t.rssUrl.toString());
    ui->animationStudioEdit->setText(t.animationStudio);
    ui->subAuthorEdit->setText(t.subAuthor);
    ui->originalLanguageEdit->setText(t.originalLanguage);
    ui->endingChapterNameEdit->setText(t.endingChapterName);
    ui->endingStartTimeEdit->setText(t.endingStartTime);
    ui->useManualTimeCheckBox->setChecked(t.useManualTime);
    ui->directorEdit->setText(t.director);
    ui->soundEngineerEdit->setText(t.soundEngineer);
    ui->timingAuthorEdit->setText(t.timingAuthor);
    ui->releaseBuilderEdit->setText(t.releaseBuilder);
    ui->targetAudioFormatComboBox->setCurrentText(t.targetAudioFormat);

    // Вкладка "Каст и стили"
    ui->castEdit->setPlainText(t.cast.join('\n'));
    ui->signStylesEdit->setPlainText(t.signStyles.join('\n'));
    ui->sourceHasSubtitlesCheckBox->setChecked(t.sourceHasSubtitles);
    ui->generateTbCheckBox->setChecked(t.generateTb);
    ui->createSrtMasterCheckBox->setChecked(t.createSrtMaster);

    // Вкладка "Стили ТБ"
    ui->tbStylesTable->setRowCount(0);
    for (const auto& style : t.tbStyles) {
        int row = ui->tbStylesTable->rowCount();
        ui->tbStylesTable->insertRow(row);
        ui->tbStylesTable->setItem(row, 0, new QTableWidgetItem(style.name));
        ui->tbStylesTable->setItem(row, 1, new QTableWidgetItem(QString::number(style.resolutionX)));
        ui->tbStylesTable->setItem(row, 2, new QTableWidgetItem(style.tags));
        ui->tbStylesTable->setItem(row, 3, new QTableWidgetItem(QString::number(style.marginLeft)));
        ui->tbStylesTable->setItem(row, 4, new QTableWidgetItem(QString::number(style.marginRight)));
        ui->tbStylesTable->setItem(row, 5, new QTableWidgetItem(QString::number(style.marginV)));
    }
    ui->defaultTbStyleComboBox->clear();
    for(const auto& style : t.tbStyles) {
        ui->defaultTbStyleComboBox->addItem(style.name);
    }
    ui->defaultTbStyleComboBox->setCurrentText(t.defaultTbStyleName);

    if (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover) {
        ui->voiceoverTypeComboBox->setCurrentText("Закадр");
    } else {
        ui->voiceoverTypeComboBox->setCurrentText("Дубляж");
    }

    // Вкладка "Шаблоны постов"
    ui->postTgMp4Edit->setPlainText(t.postTemplates.value("tg_mp4"));
    ui->postTgMkvEdit->setPlainText(t.postTemplates.value("tg_mkv"));
    ui->postVkEdit->setPlainText(t.postTemplates.value("vk"));
    ui->postVkCommentEdit->setPlainText(t.postTemplates.value("vk_comment"));

    // Вкладка "Публикация"
    ui->seriesTitleForPostEdit->setText(t.seriesTitleForPost);
    ui->totalEpisodesSpinBox->setValue(t.totalEpisodes);
    ui->posterPathEdit->setText(t.posterPath);
    ui->uploadUrlsEdit->setPlainText(t.uploadUrls.join('\n'));
}

ReleaseTemplate TemplateEditor::getTemplate() const
{
    ReleaseTemplate t;
    // Вкладка "Основные"
    t.templateName = ui->templateNameEdit->text().trimmed();
    t.seriesTitle = ui->seriesTitleEdit->text().trimmed();
    t.rssUrl = QUrl(ui->rssUrlEdit->text().trimmed());
    t.animationStudio = ui->animationStudioEdit->text().trimmed();
    t.subAuthor = ui->subAuthorEdit->text().trimmed();
    t.originalLanguage = ui->originalLanguageEdit->text().trimmed();
    t.endingChapterName = ui->endingChapterNameEdit->text().trimmed();
    t.endingStartTime = ui->endingStartTimeEdit->text().trimmed();
    t.useManualTime = ui->useManualTimeCheckBox->isChecked();
    t.director = ui->directorEdit->text().trimmed();
    t.soundEngineer = ui->soundEngineerEdit->text().trimmed();
    t.timingAuthor = ui->timingAuthorEdit->text().trimmed();
    t.releaseBuilder = ui->releaseBuilderEdit->text().trimmed();
    t.targetAudioFormat = ui->targetAudioFormatComboBox->currentText();

    // Вкладка "Каст и стили"
    t.cast = ui->castEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    t.signStyles = ui->signStylesEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    t.sourceHasSubtitles = ui->sourceHasSubtitlesCheckBox->isChecked();
    t.generateTb = ui->generateTbCheckBox->isChecked();
    t.createSrtMaster = ui->createSrtMasterCheckBox->isChecked();

    // Вкладка "Стили ТБ"
    t.tbStyles.clear();
    for(int row = 0; row < ui->tbStylesTable->rowCount(); ++row) {
        TbStyleInfo style;
        style.name = ui->tbStylesTable->item(row, 0)->text();
        style.resolutionX = ui->tbStylesTable->item(row, 1)->text().toInt();
        style.tags = ui->tbStylesTable->item(row, 2)->text();
        style.marginLeft = ui->tbStylesTable->item(row, 3)->text().toInt();
        style.marginRight = ui->tbStylesTable->item(row, 4)->text().toInt();
        style.marginV = ui->tbStylesTable->item(row, 5)->text().toInt();
        t.tbStyles.append(style);
    }
    t.defaultTbStyleName = ui->defaultTbStyleComboBox->currentText();

    if (ui->voiceoverTypeComboBox->currentText() == "Закадр") {
        t.voiceoverType = ReleaseTemplate::VoiceoverType::Voiceover;
    } else {
        t.voiceoverType = ReleaseTemplate::VoiceoverType::Dubbing;
    }

    // Вкладка "Шаблоны постов"
    t.postTemplates.clear();
    t.postTemplates["tg_mp4"] = ui->postTgMp4Edit->toPlainText();
    t.postTemplates["tg_mkv"] = ui->postTgMkvEdit->toPlainText();
    t.postTemplates["vk"] = ui->postVkEdit->toPlainText();
    t.postTemplates["vk_comment"] = ui->postVkCommentEdit->toPlainText();

    // Вкладка "Публикация"
    t.seriesTitleForPost = ui->seriesTitleForPostEdit->text();
    t.totalEpisodes = ui->totalEpisodesSpinBox->value();
    t.posterPath = ui->posterPathEdit->text();
    t.uploadUrls = ui->uploadUrlsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);

    return t;
}

void TemplateEditor::on_selectStylesButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите .ass файл для анализа", "", "ASS Subtitles (*.ass)");
    if (filePath.isEmpty()) {
        return;
    }

    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(filePath);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selectedStyles = dialog.getSelectedStyles();
        ui->signStylesEdit->setPlainText(selectedStyles.join('\n'));
    }
}

void TemplateEditor::on_addTbStyleButton_clicked()
{
    int row = ui->tbStylesTable->rowCount();
    ui->tbStylesTable->insertRow(row);
    ui->tbStylesTable->setItem(row, 0, new QTableWidgetItem("Новый стиль"));
    ui->tbStylesTable->setItem(row, 1, new QTableWidgetItem("1920"));
    ui->tbStylesTable->setItem(row, 2, new QTableWidgetItem("{\\fad(500,500)...}"));
    ui->tbStylesTable->setItem(row, 3, new QTableWidgetItem("10"));
    ui->tbStylesTable->setItem(row, 4, new QTableWidgetItem("30"));
    ui->tbStylesTable->setItem(row, 5, new QTableWidgetItem("10"));
}

void TemplateEditor::on_removeTbStyleButton_clicked()
{
    int currentRow = ui->tbStylesTable->currentRow();
    if (currentRow >= 0) {
        ui->tbStylesTable->removeRow(currentRow);
    }
}

void TemplateEditor::on_tbStylesTable_cellChanged(int row, int column)
{
    if (column == 0) {
        QString currentSelection = ui->defaultTbStyleComboBox->currentText();
        ui->defaultTbStyleComboBox->clear();
        for(int i = 0; i < ui->tbStylesTable->rowCount(); ++i) {
            ui->defaultTbStyleComboBox->addItem(ui->tbStylesTable->item(i, 0)->text());
        }
        int index = ui->defaultTbStyleComboBox->findText(currentSelection);
        if (index != -1) {
            ui->defaultTbStyleComboBox->setCurrentIndex(index);
        }
    }
}

void TemplateEditor::on_helpButton_clicked()
{
    QDialog *helpDialog = new QDialog(this);
    helpDialog->setWindowTitle("Справка по шаблонам");
    helpDialog->setMinimumSize(700, 800); // Увеличим высоту для нового контента

    QScrollArea *scrollArea = new QScrollArea(helpDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");

    QWidget *scrollWidget = new QWidget();
    scrollArea->setWidget(scrollWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(scrollWidget);
    scrollWidget->setLayout(mainLayout);

    // --- Функция для создания строк с копируемым кодом ---
    auto addRow = [&](QGridLayout* layout, int row, const QString& code, const QString& description) {
        QLineEdit *codeEdit = new QLineEdit(code);
        codeEdit->setReadOnly(true);
        codeEdit->setFont(QFont("Courier New", 10));
        QLabel *descLabel = new QLabel(description);
        descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(codeEdit, row, 0);
        layout->addWidget(descLabel, row, 1);
    };

    // --- 1. Секция плейсхолдеров (теперь копируемая) ---
    mainLayout->addWidget(new QLabel("<h3>Доступные плейсхолдеры (можно копировать):</h3>"));
    QWidget* placeholdersWidget = new QWidget();
    QGridLayout* placeholdersLayout = new QGridLayout(placeholdersWidget);
    placeholdersLayout->setContentsMargins(0,0,0,0);
    addRow(placeholdersLayout, 0, "%SERIES_TITLE%", "Название сериала");
    addRow(placeholdersLayout, 1, "%EPISODE_NUMBER%", "Номер серии");
    addRow(placeholdersLayout, 2, "%TOTAL_EPISODES%", "Всего серий");
    addRow(placeholdersLayout, 3, "%CAST_LIST%", "Список актёров");
    addRow(placeholdersLayout, 4, "%DIRECTOR%", "Режиссёр дубляжа");
    addRow(placeholdersLayout, 5, "%SOUND_ENGINEER%", "Звукорежиссёр");
    addRow(placeholdersLayout, 6, "%SUB_AUTHOR%", "Автор перевода");
    addRow(placeholdersLayout, 7, "%TIMING_AUTHOR%", "Разметка (тайминг)");
    addRow(placeholdersLayout, 8, "%RELEASE_BUILDER%", "Сборка релиза");
    addRow(placeholdersLayout, 9, "%LINK_ANILIB%", "Ссылка на Anilib");
    addRow(placeholdersLayout, 10, "%LINK_ANIME365%", "Ссылка на Anime365");
    placeholdersLayout->setColumnStretch(0, 1);
    placeholdersLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(placeholdersWidget);

    // --- 2. Секция форматирования Telegram (ИСПРАВЛЕННАЯ) ---
    mainLayout->addWidget(new QLabel("<h3>Форматирование Telegram (стиль MarkdownV2):</h3>"));
    QWidget *formatWidget = new QWidget();
    QGridLayout *gridLayout = new QGridLayout(formatWidget);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    addRow(gridLayout, 0, "**жирный**", "<b>жирный</b>");
    addRow(gridLayout, 1, "__курсив__", "<i>курсив</i>");
    //addRow(gridLayout, 2, "__подчеркнутый__", "<u>подчеркнутый</u>");
    addRow(gridLayout, 3, "~~зачеркнутый~~", "<s>зачеркнутый</s>");
    addRow(gridLayout, 4, "||скрытый текст||", "<span style='background-color: #555; color: #555; padding: 1px 3px; border-radius: 3px;'>скрытый текст</span>");
    addRow(gridLayout, 5, "`моноширинный`", "<code>моноширинный</code>");
    addRow(gridLayout, 6, "[текст ссылки](https://t.me/dublyajnaya)", "<a href=\"https://t.me/dublyajnaya\">текст ссылки</a>");
    //addRow(gridLayout, 7, ">Цитата", "<blockquote style='border-left: 2px solid #ccc; padding-left: 5px; margin-left: 0; color: #666;'>Цитата</blockquote>");
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(formatWidget);


    // --- 4. Секция VK ---
    QLabel *vkLabel = new QLabel();
    vkLabel->setWordWrap(true);
    vkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    vkLabel->setOpenExternalLinks(true);
    vkLabel->setText(R"(
        <h3>Форматирование VK:</h3>
        <p>VK не поддерживает форматирование при вставке из буфера. Для создания ссылок используйте специальный синтаксис:</p>
        <ul><li>Пример кода: <code>[%LINK_ANILIB%|Смотреть на Anilib]</code></li></ul>
        <p>Этот синтаксис преобразуется в кликабельную ссылку при публикации.</p>
    )");
    mainLayout->addWidget(vkLabel);

    mainLayout->addStretch(1);

    // --- Кнопка OK ---
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, helpDialog);
    connect(buttonBox, &QDialogButtonBox::accepted, helpDialog, &QDialog::accept);

    // --- Финальная компоновка диалога ---
    QVBoxLayout *dialogLayout = new QVBoxLayout(helpDialog);
    dialogLayout->addWidget(scrollArea);
    dialogLayout->addWidget(buttonBox);
    helpDialog->setLayout(dialogLayout);

    helpDialog->exec();
    delete helpDialog;
}
