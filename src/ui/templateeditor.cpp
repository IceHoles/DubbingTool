#include "templateeditor.h"
#include "ui_templateeditor.h"
#include "styleselectordialog.h"
#include "appsettings.h"
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
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QApplication>
#include <QClipboard>
#include <QPropertyAnimation>


class ClickableLabel : public QLabel {
public:
    explicit ClickableLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #ccc; border-radius: 4px; padding: 2px; }");
        setToolTip("Нажмите, чтобы скопировать");
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        QApplication::clipboard()->setText(this->text());

        QPropertyAnimation *animation = new QPropertyAnimation(this, "styleSheet", this);
        animation->setDuration(500);
        animation->setStartValue("QLabel { background-color: #4f4f4f; border: 1px solid #5a9e5a; border-radius: 4px; padding: 2px; }"); // Зеленый
        animation->setEndValue("QLabel { background-color: #2d2d2d; border: 1px solid #ccc; border-radius: 4px; padding: 2px; }");   // Исходный

        QLabel::mousePressEvent(event);
    }
};

TemplateEditor::TemplateEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TemplateEditor)
{
    ui->setupUi(this);

    connect(ui->addSubstitutionButton, &QPushButton::clicked, this, [this](){
        int row = ui->substitutionsTableWidget->rowCount();
        ui->substitutionsTableWidget->insertRow(row);
        ui->substitutionsTableWidget->setItem(row, 0, new QTableWidgetItem("Текст для поиска"));
        ui->substitutionsTableWidget->setItem(row, 1, new QTableWidgetItem("Текст для замены"));
    });

    connect(ui->removeSubstitutionButton, &QPushButton::clicked, this, [this](){
        int currentRow = ui->substitutionsTableWidget->currentRow();
        if (currentRow >= 0) {
            ui->substitutionsTableWidget->removeRow(currentRow);
        }
    });

    connect(ui->browsePosterButton, &QPushButton::clicked, this, [this](){
        QString path = QFileDialog::getOpenFileName(this, "Выберите файл постера", "", "Изображения (*.png *.jpg *.jpeg *.webp)");
        if (!path.isEmpty()) {
            ui->posterPathEdit->setText(path);
        }
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &TemplateEditor::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &TemplateEditor::reject);
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
    ui->releaseTagsEdit->setText(t.releaseTags.join(", "));
    ui->rssUrlEdit->setText(t.rssUrl.toString());
    ui->animationStudioEdit->setText(t.animationStudio);
    ui->subAuthorEdit->setText(t.subAuthor);
    ui->originalLanguageEdit->setText(t.originalLanguage);
    ui->targetAudioFormatComboBox->setCurrentText(t.targetAudioFormat);
    ui->createSrtMasterCheckBox->setChecked(t.createSrtMaster);
    ui->isCustomTranslationCheckBox->setChecked(t.isCustomTranslation);
    ui->useConcatRenderCheckBox->setChecked(t.useConcatRender);
    ui->renderPresetComboBox->clear();
    for(const auto& preset : AppSettings::instance().renderPresets()) {
        ui->renderPresetComboBox->addItem(preset.name);
    }
    ui->renderPresetComboBox->setCurrentText(t.renderPresetName);

    // Вкладка "Создание ТБ"
    ui->generateTbCheckBox->setChecked(t.generateTb);
    ui->endingChapterNameEdit->setText(t.endingChapterName);
    ui->endingStartTimeEdit->setTime(QTime::fromString(t.endingStartTime, "H:mm:ss.zzz"));
    ui->useManualTimeCheckBox->setChecked(t.useManualTime);
    ui->defaultTbStyleComboBox->clear();
    for(const auto& style : AppSettings::instance().tbStyles()) {
        ui->defaultTbStyleComboBox->addItem(style.name);
    }
    ui->defaultTbStyleComboBox->setCurrentText(t.defaultTbStyleName);
    if (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover) {
        ui->voiceoverTypeComboBox->setCurrentText("Закадр");
    } else {
        ui->voiceoverTypeComboBox->setCurrentText("Дубляж");
    }
    ui->castEdit->setPlainText(t.cast.join(", "));
    ui->directorEdit->setText(t.director);
    ui->assistantDirectorEdit->setText(t.assistantDirector);
    ui->soundEngineerEdit->setText(t.soundEngineer);
    ui->songsSoundEngineerEdit->setText(t.songsSoundEngineer);
    ui->episodeSoundEngineerEdit->setText(t.episodeSoundEngineer);
    ui->recordingSoundEngineerEdit->setText(t.recordingSoundEngineer);
    ui->videoLocalizationAuthorEdit->setText(t.videoLocalizationAuthor);
    ui->timingAuthorEdit->setText(t.timingAuthor);
    ui->signsAuthorEdit->setText(t.signsAuthor);
    ui->translationEditorEdit->setText(t.translationEditor);
    ui->releaseBuilderEdit->setText(t.releaseBuilder);

    // Вкладка "Субтитры"
    ui->sourceHasSubtitlesCheckBox->setChecked(t.sourceHasSubtitles);
    ui->forceSignStyleRequestCheckBox->setChecked(t.forceSignStyleRequest);
    ui->signStylesEdit->setPlainText(t.signStyles.join('\n'));
    ui->pauseForSubEditCheckBox->setChecked(t.pauseForSubEdit);
    ui->substitutionsTableWidget->setRowCount(0);
    for (auto it = t.substitutions.constBegin(); it != t.substitutions.constEnd(); ++it) {
        int row = ui->substitutionsTableWidget->rowCount();
        ui->substitutionsTableWidget->insertRow(row);
        ui->substitutionsTableWidget->setItem(row, 0, new QTableWidgetItem(it.key()));
        ui->substitutionsTableWidget->setItem(row, 1, new QTableWidgetItem(it.value()));
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
    QStringList tags = ui->releaseTagsEdit->text().split(',', Qt::SkipEmptyParts);
    for(QString& tag : tags) {
        t.releaseTags.append(tag.trimmed());
    }
    t.rssUrl = QUrl(ui->rssUrlEdit->text().trimmed());
    t.animationStudio = ui->animationStudioEdit->text().trimmed();
    t.subAuthor = ui->subAuthorEdit->text().trimmed();
    t.originalLanguage = ui->originalLanguageEdit->text().trimmed();
    t.targetAudioFormat = ui->targetAudioFormatComboBox->currentText();
    t.createSrtMaster = ui->createSrtMasterCheckBox->isChecked();
    t.isCustomTranslation = ui->isCustomTranslationCheckBox->isChecked();
    t.useConcatRender = ui->useConcatRenderCheckBox->isChecked();
    t.renderPresetName = ui->renderPresetComboBox->currentText();

    // Вкладка "Создание ТБ"
    t.generateTb = ui->generateTbCheckBox->isChecked();
    t.endingChapterName = ui->endingChapterNameEdit->text().trimmed();
    t.endingStartTime = ui->endingStartTimeEdit->time().toString("H:mm:ss.zzz");
    t.useManualTime = ui->useManualTimeCheckBox->isChecked();
    t.defaultTbStyleName = ui->defaultTbStyleComboBox->currentText();
    if (ui->voiceoverTypeComboBox->currentText() == "Закадр") {
        t.voiceoverType = ReleaseTemplate::VoiceoverType::Voiceover;
    } else {
        t.voiceoverType = ReleaseTemplate::VoiceoverType::Dubbing;
    }
    QRegularExpression rx("((\\, )|\\,|\\n)");
    t.cast = ui->castEdit->toPlainText().split(rx, Qt::SkipEmptyParts);
    t.director = ui->directorEdit->text().trimmed();
    t.assistantDirector = ui->assistantDirectorEdit->text().trimmed();
    t.soundEngineer = ui->soundEngineerEdit->text().trimmed();
    t.songsSoundEngineer = ui->songsSoundEngineerEdit->text().trimmed();
    t.episodeSoundEngineer = ui->episodeSoundEngineerEdit->text().trimmed();
    t.recordingSoundEngineer = ui->recordingSoundEngineerEdit->text().trimmed();
    t.videoLocalizationAuthor = ui->videoLocalizationAuthorEdit->text().trimmed();
    t.timingAuthor = ui->timingAuthorEdit->text().trimmed();
    t.signsAuthor = ui->signsAuthorEdit->text().trimmed();
    t.translationEditor = ui->translationEditorEdit->text().trimmed();
    t.releaseBuilder = ui->releaseBuilderEdit->text().trimmed();

    // Вкладка "Субтитры"
    t.sourceHasSubtitles = ui->sourceHasSubtitlesCheckBox->isChecked();
    t.forceSignStyleRequest = ui->forceSignStyleRequestCheckBox->isChecked();
    t.signStyles = ui->signStylesEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    t.pauseForSubEdit = ui->pauseForSubEditCheckBox->isChecked();
    t.substitutions.clear();
    for (int row = 0; row < ui->substitutionsTableWidget->rowCount(); ++row) {
        QString findText = ui->substitutionsTableWidget->item(row, 0)->text();
        QString replaceText = ui->substitutionsTableWidget->item(row, 1)->text();
        if (!findText.isEmpty()) {
            t.substitutions.insert(findText, replaceText);
        }
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
    if (filePath.isEmpty()) return;

    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(filePath);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList selectedStyles = dialog.getSelectedStyles();
        ui->signStylesEdit->setPlainText(selectedStyles.join('\n'));
    }
}

void TemplateEditor::on_helpButton_clicked()
{
    static QDialog* helpDialog = nullptr;
    if (helpDialog && helpDialog->isVisible()) {
        helpDialog->activateWindow();
        return;
    }

    helpDialog = new QDialog(this);
    helpDialog->setAttribute(Qt::WA_DeleteOnClose);
    helpDialog->setWindowTitle("Справка по шаблонам и форматированию");
    helpDialog->setMinimumSize(500, 800);

    QScrollArea *scrollArea = new QScrollArea(helpDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    QWidget *scrollWidget = new QWidget();
    scrollArea->setWidget(scrollWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(scrollWidget);

    auto addRow = [&](QGridLayout* layout, int row, const QString& code, const QString& description) {
        layout->addWidget(new ClickableLabel(code), row, 0);
        layout->addWidget(new QLabel(description), row, 1);
    };

    // --- Блок 1: Плейсхолдеры ---
    mainLayout->addWidget(new QLabel("<h3>Плейсхолдеры (нажмите, чтобы скопировать):</h3>"));
    QWidget* placeholdersWidget = new QWidget();
    QGridLayout* placeholdersLayout = new QGridLayout(placeholdersWidget);
    addRow(placeholdersLayout, 0, "%SERIES_TITLE%", "Название сериала для постов");
    addRow(placeholdersLayout, 1, "%EPISODE_NUMBER%", "Номер текущей серии");
    addRow(placeholdersLayout, 2, "%TOTAL_EPISODES%", "Общее количество серий");
    addRow(placeholdersLayout, 3, "%CAST_LIST%", "Список актеров через запятую");
    addRow(placeholdersLayout, 4, "%DIRECTOR%", "Режиссер дубляжа");
    addRow(placeholdersLayout, 5, "%SOUND_ENGINEER%", "Звукорежиссер");
    addRow(placeholdersLayout, 6, "%SONG_ENGINEER%", "Звукорежиссер песен");
    addRow(placeholdersLayout, 7, "%EPISODE_ENGINEER%", "Звукорежиссер эпизода");
    addRow(placeholdersLayout, 8, "%RECORDING_ENGINEER%", "Звукорежиссер записи");
    addRow(placeholdersLayout, 9, "%SUB_AUTHOR%", "Автор перевода");
    addRow(placeholdersLayout, 10, "%TIMING_AUTHOR%", "Разметка (тайминг)");
    addRow(placeholdersLayout, 11, "%SIGNS_AUTHOR%", "Локализация надписей");
    addRow(placeholdersLayout, 12, "%TRANSLATION_EDITOR%", "Редактор перевода");
    addRow(placeholdersLayout, 13, "%RELEASE_BUILDER%", "Сборка релиза");
    addRow(placeholdersLayout, 14, "%LINK_ANILIB%", "Ссылка на Anilib (из панели 'Публикация')");
    addRow(placeholdersLayout, 15, "%LINK_ANIME365%", "Ссылка на Anime365 (из панели 'Публикация')");
    placeholdersLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(placeholdersWidget);

    mainLayout->addWidget(new QFrame);

    // --- Блок 2: Форматирование Telegram ---
    mainLayout->addWidget(new QLabel("<h3>Форматирование Telegram (псевдо-Markdown):</h3>"));
    QTextBrowser* tgHelpBrowser = new QTextBrowser();
    tgHelpBrowser->setOpenExternalLinks(true);
    tgHelpBrowser->setHtml(R"(
        <p>Для форматирования текста используйте специальные символы. При копировании текста для Telegram, он будет автоматически преобразован в нужный формат.</p>
        <h4>Базовые стили</h4>
        <table border="1" cellspacing="0" cellpadding="5">
            <tr><th>Стиль</th><th>Синтаксис</th></tr>
            <tr><td><b>Жирный</b></td><td><code>**Жирный текст**</code></td></tr>
            <tr><td><i>Курсив</i></td><td><code>__Курсивный текст__</code></td></tr>
            <tr><td><u>Подчеркнутый</u></td><td><code>^^Подчеркнутый текст^^</code></td></tr>
            <tr><td><s>Зачеркнутый</s></td><td><code>~~Зачеркнутый текст~~</code></td></tr>
            <tr><td><span style='background-color: #555; color: #555;'>Спойлер</span></td><td><code>||Скрытый текст||</code></td></tr>
            <tr><td><code>Моноширинный</code></td><td><code>`моноширинный текст`</code></td></tr>
        </table>
        <h4>Ссылки и кастомные эмодзи</h4>
        <ul>
            <li><b>Обычная ссылка:</b> <code>[видимый текст](URL-адрес)</code><br><i>Пример:</i> <code>[Сайт Qt](https://qt.io/)</code></li>
            <li><b>Кастомный эмодзи:</b> <code>[эмодзи](emoji:ID)</code><br><i>Пример:</i> <code>[💙](emoji:5278229754099540071)</code></li>
        </ul>
        <h4>Блочные элементы</h4>
        <ul>
            <li><b>Цитата:</b> Текст заключается в <code>&gt;</code> и <code>&lt;</code>.<br><i>Пример:</i> <code>&gt;Это цитата.&lt;</code></li>
            <li><b>Сворачиваемая цитата:</b> Текст заключается в <code>&gt;^</code> и <code>&lt;^</code>.</li>
            <li><b>Блок кода:</b> Текст заключается в тройные обратные кавычки <code>```</code>. Можно указать язык.<br><i>Пример:</i> <code>```cpp<br>#include &lt;iostream&gt;<br>```</code></li>
        </ul>
        <p><b>Правила вложенности:</b> Стили можно комбинировать (<code>**__жирный курсив__**</code>). Блок кода имеет наивысший приоритет и отменяет любое форматирование внутри. Внутри цитаты нельзя использовать моноширинный стиль.</p>
    )");
    mainLayout->addWidget(tgHelpBrowser);

    mainLayout->addWidget(new QFrame);

    // --- Блок 3: Форматирование VK ---
    mainLayout->addWidget(new QLabel("<h3>Форматирование VK (для ссылок):</h3>"));
    QWidget *vkWidget = new QWidget();
    QGridLayout *vkLayout = new QGridLayout(vkWidget);
    addRow(vkLayout, 0, "[https://vk.com/dublyajnaya|ТО Дубляжная]", "Ссылка на внешний ресурс");
    vkLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(vkWidget);

    mainLayout->addStretch(1);

    QVBoxLayout *dialogLayout = new QVBoxLayout(helpDialog);
    dialogLayout->addWidget(scrollArea);
    helpDialog->setLayout(dialogLayout);
    helpDialog->show();
}
