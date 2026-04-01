#include "templateeditor.h"

#include "appsettings.h"
#include "postgenerator.h"
#include "styleselectordialog.h"
#include "telegramformatter.h"
#include "telegrampasteedit.h"
#include "ui_templateeditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <qplaintextedit.h>

class ClickableLabel : public QLabel
{
public:
    explicit ClickableLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "QLabel { background-color: #2d2d2d; border: 1px solid #ccc; border-radius: 4px; padding: 2px; }");
        setToolTip("Нажмите, чтобы скопировать");
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QApplication::clipboard()->setText(this->text());

        QPropertyAnimation* animation = new QPropertyAnimation(this, "styleSheet", this);
        animation->setDuration(500);
        animation->setStartValue("QLabel { background-color: #4f4f4f; border: 1px solid #5a9e5a; border-radius: 4px; "
                                 "padding: 2px; }"); // Зеленый
        animation->setEndValue("QLabel { background-color: #2d2d2d; border: 1px solid #ccc; border-radius: 4px; "
                               "padding: 2px; }"); // Исходный

        QLabel::mousePressEvent(event);
    }
};

static constexpr int kPostTemplateTreeKeyRole = Qt::UserRole + 100;

static QString titleFromKey(const QString& key)
{
    if (key == "tg_mp4")
    {
        return "Telegram (Канал)";
    }
    if (key == "tg_mkv")
    {
        return "Telegram (Архив)";
    }
    if (key == "vk")
    {
        return "VK (Пост)";
    }
    if (key == "vk_comment")
    {
        return "VK (Комментарий)";
    }
    return key;
}

static QString platformFromKey(const QString& key)
{
    if (key.startsWith("tg_"))
    {
        return "telegram";
    }
    if (key.startsWith("vk"))
    {
        return "vk";
    }
    return "other";
}

static QString normalizeForParsing(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text.trimmed();
}

static QString parseFieldValue(const PostParseResult& parseResult, const QString& key)
{
    return parseResult.fields.value(key).trimmed();
}

static QString buildVkTemplateFromParseResult(const PostParseResult& parseResult)
{
    const QString parsedTemplateText = parseFieldValue(parseResult, "%PARSED_TEMPLATE_TEXT%").toLower();
    const QString seriesTitle = parseFieldValue(parseResult, "%SERIES_TITLE%");
    const QString totalEpisodes = parseFieldValue(parseResult, "%TOTAL_EPISODES%");
    const QString castList = parseFieldValue(parseResult, "%CAST_LIST%");
    const QString director = parseFieldValue(parseResult, "%DIRECTOR%");
    const QString soundEngineer = parseFieldValue(parseResult, "%SOUND_ENGINEER%");
    const QString episodeEngineer = parseFieldValue(parseResult, "%EPISODE_ENGINEER%");
    const QString recordingEngineer = parseFieldValue(parseResult, "%RECORDING_ENGINEER%");
    const QString subAuthor = parseFieldValue(parseResult, "%SUB_AUTHOR%");
    const QString timingAuthor = parseFieldValue(parseResult, "%TIMING_AUTHOR%");
    const QString signsAuthor = parseFieldValue(parseResult, "%SIGNS_AUTHOR%");
    const QString releaseBuilder = parseFieldValue(parseResult, "%RELEASE_BUILDER%");
    const bool isVoiceover = parsedTemplateText.contains("закадр");

    if (seriesTitle.isEmpty() && castList.isEmpty())
    {
        return QString();
    }

    QStringList lines;
    if (!seriesTitle.isEmpty())
    {
        lines << QString("«%1» в %2 от ТО Дубляжная").arg("%SERIES_TITLE%", isVoiceover ? "закадре" : "дубляже");
        lines << QString();
    }
    lines << "Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%";
    lines << QString();

    if (!castList.isEmpty())
    {
        lines << (isVoiceover ? "Роли озвучивали:" : "Роли дублировали:");
        lines << "%CAST_LIST%";
        lines << QString();
    }

    if (!director.isEmpty())
    {
        lines << (isVoiceover ? "Куратор закадра:" : "Режиссёр дубляжа:");
        lines << "%DIRECTOR%";
        lines << QString();
    }

    if (!episodeEngineer.isEmpty() || !recordingEngineer.isEmpty())
    {
        if (!episodeEngineer.isEmpty())
        {
            lines << "Звукорежиссёр эпизода:";
            lines << "%EPISODE_ENGINEER%";
            lines << QString();
        }
        if (!recordingEngineer.isEmpty())
        {
            lines << "Звукорежиссёр записи:";
            lines << "%RECORDING_ENGINEER%";
            lines << QString();
        }
    }
    else if (!soundEngineer.isEmpty())
    {
        lines << "Звукорежиссёр:";
        lines << "%SOUND_ENGINEER%";
        lines << QString();
    }

    if (!subAuthor.isEmpty())
    {
        lines << "Перевод:";
        lines << "%SUB_AUTHOR%";
        lines << QString();
    }

    if (!timingAuthor.isEmpty())
    {
        lines << "Разметка:";
        lines << "%TIMING_AUTHOR%";
        lines << QString();
    }

    if (!signsAuthor.isEmpty())
    {
        lines << "Локализация надписей:";
        lines << "%SIGNS_AUTHOR%";
        lines << QString();
    }

    if (!releaseBuilder.isEmpty())
    {
        lines << "Сборка релиза:";
        lines << "%RELEASE_BUILDER%";
        lines << QString();
    }

    lines << "#Хештег@dublyajnaya";
    Q_UNUSED(totalEpisodes);
    return lines.join('\n').trimmed();
}

static QString normalizeCastToken(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression("\\s+"), " ");
    return value;
}

static QStringList findDuplicatedCastNamePairs(const QString& castText)
{
    const QRegularExpression splitRx("((\\, )|\\,|\\n)");
    const QStringList entries = castText.split(splitRx, Qt::SkipEmptyParts);
    const QRegularExpression pairRx("^\\s*([A-Za-zА-Яа-яЁё\\-]+)\\s+([A-Za-zА-Яа-яЁё\\-]+)\\b");

    QMap<QString, int> counts;
    QMap<QString, QString> originalCasing;
    for (const QString& rawEntry : entries)
    {
        const QString entry = normalizeCastToken(rawEntry);
        if (entry.isEmpty())
        {
            continue;
        }

        const QRegularExpressionMatch m = pairRx.match(entry);
        if (!m.hasMatch())
        {
            continue;
        }

        const QString pairOriginal = QString("%1 %2").arg(m.captured(1), m.captured(2));
        const QString key = pairOriginal.toLower();
        counts[key] = counts.value(key) + 1;
        if (!originalCasing.contains(key))
        {
            originalCasing.insert(key, pairOriginal);
        }
    }

    QStringList duplicates;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        if (it.value() > 1)
        {
            duplicates.append(QString("%1 (x%2)").arg(originalCasing.value(it.key()), QString::number(it.value())));
        }
    }
    duplicates.sort(Qt::CaseInsensitive);
    return duplicates;
}

static bool isRequiredBuiltinPostKey(const QString& key)
{
    return key == "tg_mp4" || key == "tg_mkv" || key == "vk" || key == "vk_comment";
}

static QString sectionCodeFromKey(const QString& key)
{
    if (key == "tg_mp4")
    {
        return "tg_post";
    }
    if (key == "tg_mkv")
    {
        return "tg_archive";
    }
    if (key == "vk")
    {
        return "vk_post";
    }
    if (key == "vk_comment")
    {
        return "vk_comment";
    }
    return "tg_post";
}

static QString sectionLabel(const QString& sectionCode)
{
    if (sectionCode == "tg_post")
    {
        return "Telegram пост";
    }
    if (sectionCode == "tg_archive")
    {
        return "Telegram (Архив)";
    }
    if (sectionCode == "vk_post")
    {
        return "ВК пост";
    }
    if (sectionCode == "vk_comment")
    {
        return "ВК комментарий";
    }
    return "Telegram пост";
}

static QString sectionCodeFromMeta(const QString& key, const PostTemplateMeta& meta)
{
    if (isRequiredBuiltinPostKey(key))
    {
        return sectionCodeFromKey(key);
    }
    const QString code = meta.category.trimmed();
    if (code == "tg_post" || code == "tg_archive" || code == "vk_post" || code == "vk_comment")
    {
        return code;
    }
    if (code == "База")
    {
        return sectionCodeFromKey(key);
    }
    if (meta.platform.trimmed().toLower() == "vk")
    {
        return "vk_post";
    }
    return "tg_post";
}

/// Previous releases stored the catalog under AppData/post_templates/.
static QString legacyPostTemplateCatalogFilePath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(appDataPath).filePath(QStringLiteral("post_templates/post_template_catalog.json"));
}

static QString postTemplateCatalogFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("post_template_catalog.json"));
}

TemplateEditor::TemplateEditor(QWidget* parent) : QDialog(parent), ui(new Ui::TemplateEditor)
{
    ui->setupUi(this);
    m_draftAutosaveTimer.setInterval(2000);
    m_draftAutosaveTimer.setSingleShot(false);
    connect(&m_draftAutosaveTimer, &QTimer::timeout, this, &TemplateEditor::saveDraftNow);

    // Перехватываем вставку в полях Telegram-постов, чтобы уметь понимать
    // специальный формат буфера обмена Telegram (application/x-td-field-*).
    auto replaceEditor = [this](QPlainTextEdit* original) -> TelegramPasteEdit*
    {
        TelegramPasteEdit* newEdit = new TelegramPasteEdit(this);

        // Копируем свойства из старого виджета
        newEdit->setObjectName(original->objectName());
        newEdit->setGeometry(original->geometry());
        newEdit->setSizePolicy(original->sizePolicy());
        newEdit->setFont(original->font());
        // Добавьте другие свойства, если они важны (placeholderText и т.д.)

        // Вставляем новый виджет в layout на место старого
        if (original->parentWidget() && original->parentWidget()->layout())
        {
            QLayout* layout = original->parentWidget()->layout();
            layout->replaceWidget(original, newEdit);
        }

        // Удаляем старый
        delete original;
        return newEdit;
    };
    auto* mp4Edit = replaceEditor(ui->postTgMp4Edit);
    ui->postTgMp4Edit = mp4Edit; // Обновляем указатель в ui, чтобы остальной код работал

    auto* mkvEdit = replaceEditor(ui->postTgMkvEdit);
    ui->postTgMkvEdit = mkvEdit;

    setupPostTemplateCatalogUi();

    connect(ui->addSubstitutionButton, &QPushButton::clicked, this,
            [this]()
            {
                int row = ui->substitutionsTableWidget->rowCount();
                ui->substitutionsTableWidget->insertRow(row);
                ui->substitutionsTableWidget->setItem(row, 0, new QTableWidgetItem("Текст для поиска"));
                ui->substitutionsTableWidget->setItem(row, 1, new QTableWidgetItem("Текст для замены"));
            });

    connect(ui->removeSubstitutionButton, &QPushButton::clicked, this,
            [this]()
            {
                int currentRow = ui->substitutionsTableWidget->currentRow();
                if (currentRow >= 0)
                {
                    ui->substitutionsTableWidget->removeRow(currentRow);
                }
            });

    connect(ui->browsePosterButton, &QPushButton::clicked, this,
            [this]()
            {
                QString path = QFileDialog::getOpenFileName(this, "Выберите файл постера", "",
                                                            "Изображения (*.png *.jpg *.jpeg *.webp)");
                if (!path.isEmpty())
                {
                    ui->posterPathEdit->setText(path);
                }
            });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &TemplateEditor::slotValidateAndAccept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &TemplateEditor::reject);
}

TemplateEditor::~TemplateEditor()
{
    saveDraftNow();
    delete ui;
}

void TemplateEditor::setTemplate(const ReleaseTemplate& t)
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
    for (const auto& preset : AppSettings::instance().renderPresets())
    {
        ui->renderPresetComboBox->addItem(preset.name);
    }
    ui->renderPresetComboBox->setCurrentText(t.renderPresetName);

    // Вкладка "Создание ТБ"
    ui->generateTbCheckBox->setChecked(t.generateTb);
    ui->chaptersEnabledCheckBox->setChecked(t.chaptersEnabled);
    ui->endingChapterNameEdit->setText(t.endingChapterName);
    ui->endingStartTimeEdit->setTime(QTime::fromString(t.endingStartTime, "H:mm:ss.zzz"));
    ui->useManualTimeCheckBox->setChecked(t.useManualTime);
    ui->useOriginalAudioCheckBox->setChecked(t.useOriginalAudio);
    ui->defaultTbStyleComboBox->clear();
    for (const auto& style : AppSettings::instance().tbStyles())
    {
        ui->defaultTbStyleComboBox->addItem(style.name);
    }
    ui->defaultTbStyleComboBox->setCurrentText(t.defaultTbStyleName);
    if (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover)
    {
        ui->voiceoverTypeComboBox->setCurrentText("Закадр");
    }
    else
    {
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
    for (auto it = t.substitutions.constBegin(); it != t.substitutions.constEnd(); ++it)
    {
        int row = ui->substitutionsTableWidget->rowCount();
        ui->substitutionsTableWidget->insertRow(row);
        ui->substitutionsTableWidget->setItem(row, 0, new QTableWidgetItem(it.key()));
        ui->substitutionsTableWidget->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    // Вкладка "Шаблоны постов"
    QMap<QString, QString> legacyPosts = t.postTemplates;
    ensureBuiltinLegacyPostTemplates(legacyPosts);
    ui->postTgMp4Edit->setPlainText(legacyPosts.value("tg_mp4"));
    ui->postTgMkvEdit->setPlainText(legacyPosts.value("tg_mkv"));
    ui->postVkEdit->setPlainText(legacyPosts.value("vk"));
    ui->postVkCommentEdit->setPlainText(legacyPosts.value("vk_comment"));
    loadPostTemplateCatalogFromDisk(t);

    // Вкладка "Публикация"
    ui->seriesTitleForPostEdit->setText(t.seriesTitleForPost);
    ui->totalEpisodesSpinBox->setValue(t.totalEpisodes);
    ui->posterPathEdit->setText(t.posterPath);
    ui->uploadUrlsEdit->setPlainText(t.uploadUrls.join('\n'));
}

ReleaseTemplate TemplateEditor::getTemplate()
{
    saveCurrentPostTemplateEdits();
    ReleaseTemplate t;

    // Вкладка "Основные"
    t.templateName = ui->templateNameEdit->text().trimmed();
    t.seriesTitle = ui->seriesTitleEdit->text().trimmed();
    QStringList tags = ui->releaseTagsEdit->text().split(',', Qt::SkipEmptyParts);
    for (QString& tag : tags)
    {
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
    t.chaptersEnabled = ui->chaptersEnabledCheckBox->isChecked();
    t.endingChapterName = ui->endingChapterNameEdit->text().trimmed();
    t.endingStartTime = ui->endingStartTimeEdit->time().toString("H:mm:ss.zzz");
    t.useManualTime = ui->useManualTimeCheckBox->isChecked();
    t.useOriginalAudio = ui->useOriginalAudioCheckBox->isChecked();
    t.defaultTbStyleName = ui->defaultTbStyleComboBox->currentText();
    if (ui->voiceoverTypeComboBox->currentText() == "Закадр")
    {
        t.voiceoverType = ReleaseTemplate::VoiceoverType::Voiceover;
    }
    else
    {
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
    for (int row = 0; row < ui->substitutionsTableWidget->rowCount(); ++row)
    {
        QString findText = ui->substitutionsTableWidget->item(row, 0)->text();
        QString replaceText = ui->substitutionsTableWidget->item(row, 1)->text();
        if (!findText.isEmpty())
        {
            t.substitutions.insert(findText, replaceText);
        }
    }

    // Вкладка "Шаблоны постов"
    syncPostTemplateCatalogToTemplate(t);

    // Вкладка "Публикация"
    t.seriesTitleForPost = ui->seriesTitleForPostEdit->text();
    t.totalEpisodes = ui->totalEpisodesSpinBox->value();
    t.posterPath = ui->posterPathEdit->text();
    t.uploadUrls = ui->uploadUrlsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);

    return t;
}

void TemplateEditor::setupPostTemplateCatalogUi()
{
    m_openCatalogButton = new QPushButton("Каталог шаблонов...", this);
    m_openCatalogButton->setToolTip("Открывает отдельное окно с поиском, группировкой и управлением шаблонами постов.");

    m_parseSourceComboBox = new QComboBox(this);
    m_parseSourceComboBox->addItem("TG (Канал)", "tg_mp4");
    m_parseSourceComboBox->addItem("VK (Пост)", "vk");
    m_parseSourceComboBox->setToolTip("Тип поста в буфере обмена (поддерживаются Telegram и VK посты).");

    m_parsePostButton = new QPushButton("Вставить пост и извлечь поля", this);
    m_parsePostButton->setToolTip("Читает текст из буфера обмена и заполняет поля шаблона.");

    if (ui->horizontalLayout != nullptr)
    {
        ui->horizontalLayout->insertWidget(0, m_openCatalogButton);
        ui->horizontalLayout->insertWidget(1, m_parseSourceComboBox);
        ui->horizontalLayout->insertWidget(2, m_parsePostButton);
    }

    connect(m_openCatalogButton, &QPushButton::clicked, this, &TemplateEditor::onOpenPostTemplateCatalogClicked);
    connect(m_parsePostButton, &QPushButton::clicked, this, &TemplateEditor::onParsePostToTemplateClicked);
}

void TemplateEditor::buildPostTemplateCatalogDialogUi(QDialog* dialog)
{
    auto* rootLayout = new QVBoxLayout(dialog);
    auto* splitter = new QSplitter(Qt::Horizontal, dialog);
    splitter->setObjectName("postTemplateCatalogSplitter");
    rootLayout->addWidget(splitter);

    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* searchLabel = new QLabel("Поиск шаблонов:", leftPane);
    leftLayout->addWidget(searchLabel);

    m_postTemplateSearchEdit = new QLineEdit(leftPane);
    m_postTemplateSearchEdit->setPlaceholderText("Название, категория, теги...");
    leftLayout->addWidget(m_postTemplateSearchEdit);

    m_postTemplateTree = new QTreeWidget(leftPane);
    m_postTemplateTree->setObjectName("postTemplateCatalogTree");
    m_postTemplateTree->setHeaderLabels({"Шаблон"});
    m_postTemplateTree->setRootIsDecorated(true);
    m_postTemplateTree->setAlternatingRowColors(true);
    m_postTemplateTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_postTemplateTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_postTemplateTree->header()->setStretchLastSection(true);
    leftLayout->addWidget(m_postTemplateTree, 1);

    auto* leftButtons = new QHBoxLayout();
    auto* addButton = new QPushButton("Добавить", leftPane);
    m_postTemplateDeleteButton = new QPushButton("Удалить", leftPane);
    auto* restoreBuiltinButton = new QPushButton("Базовые", leftPane);
    leftButtons->addWidget(addButton);
    leftButtons->addWidget(m_postTemplateDeleteButton);
    leftButtons->addWidget(restoreBuiltinButton);
    leftLayout->addLayout(leftButtons);

    auto* rightPane = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto* metaForm = new QFormLayout();
    m_postTemplateTitleEdit = new QLineEdit(rightPane);
    m_postTemplateTypeEdit = new QComboBox(rightPane);
    m_postTemplateTypeEdit->addItem(sectionLabel("tg_post"), "tg_post");
    m_postTemplateTypeEdit->addItem(sectionLabel("tg_archive"), "tg_archive");
    m_postTemplateTypeEdit->addItem(sectionLabel("vk_post"), "vk_post");
    m_postTemplateTypeEdit->addItem(sectionLabel("vk_comment"), "vk_comment");

    metaForm->addRow("Название:", m_postTemplateTitleEdit);
    metaForm->addRow("Раздел:", m_postTemplateTypeEdit);
    rightLayout->addLayout(metaForm);

    auto* editorLabel = new QLabel("Текст шаблона:", rightPane);
    rightLayout->addWidget(editorLabel);

    m_postTemplateBodyEdit = new TelegramPasteEdit(rightPane);
    rightLayout->addWidget(m_postTemplateBodyEdit, 1);

    m_postTemplateSaveButton = new QPushButton("Сохранить шаблон в каталог", rightPane);
    rightLayout->addWidget(m_postTemplateSaveButton);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    QSettings settings("MyCompany", "DubbingTool");
    const QByteArray splitterState = settings.value("ui/templateEditor/postCatalogSplitter").toByteArray();
    if (!splitterState.isEmpty())
    {
        splitter->restoreState(splitterState);
    }
    else
    {
        splitter->setSizes({420, 780});
    }
    const QByteArray treeHeaderState = settings.value("ui/templateEditor/postCatalogTreeHeader").toByteArray();
    if (!treeHeaderState.isEmpty())
    {
        m_postTemplateTree->header()->restoreState(treeHeaderState);
    }

    connect(m_postTemplateTree, &QTreeWidget::itemSelectionChanged, this,
            &TemplateEditor::onPostTemplateSelectionChanged);
    connect(m_postTemplateSearchEdit, &QLineEdit::textChanged, this, &TemplateEditor::onPostTemplateFilterChanged);
    connect(addButton, &QPushButton::clicked, this, &TemplateEditor::onPostTemplateAddClicked);
    connect(m_postTemplateDeleteButton, &QPushButton::clicked, this, &TemplateEditor::onPostTemplateDeleteClicked);
    connect(restoreBuiltinButton, &QPushButton::clicked, this, &TemplateEditor::onPostTemplateRestoreBuiltinClicked);
    connect(m_postTemplateTitleEdit, &QLineEdit::editingFinished, this, &TemplateEditor::onPostTemplateMetaEdited);
    connect(m_postTemplateTypeEdit, &QComboBox::currentIndexChanged, this, [this](int) { onPostTemplateMetaEdited(); });
    connect(m_postTemplateBodyEdit, &QPlainTextEdit::textChanged, this, &TemplateEditor::onPostTemplateBodyEdited);
    connect(m_postTemplateSaveButton, &QPushButton::clicked, this, &TemplateEditor::onPostTemplateSaveClicked);
}

void TemplateEditor::onOpenPostTemplateCatalogClicked()
{
    if (m_postTemplateDialog && m_postTemplateDialog->isVisible())
    {
        m_postTemplateDialog->raise();
        m_postTemplateDialog->activateWindow();
        return;
    }

    m_postTemplateDialog = new QDialog(this);
    m_postTemplateDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_postTemplateDialog->setWindowTitle("Каталог шаблонов постов");
    QSettings settings("MyCompany", "DubbingTool");
    const QByteArray geometry = settings.value("ui/templateEditor/postCatalogGeometry").toByteArray();
    if (!geometry.isEmpty())
    {
        m_postTemplateDialog->restoreGeometry(geometry);
    }
    else
    {
        m_postTemplateDialog->resize(1240, 820);
    }
    buildPostTemplateCatalogDialogUi(m_postTemplateDialog);
    rebuildPostTemplateTree(QString());
    m_postTemplateDialog->show();

    connect(m_postTemplateDialog, &QDialog::finished, this,
            [this](int)
            {
                if (!m_postTemplateDialog)
                {
                    return;
                }
                QSettings settings("MyCompany", "DubbingTool");
                settings.setValue("ui/templateEditor/postCatalogGeometry", m_postTemplateDialog->saveGeometry());
                if (auto* splitter = m_postTemplateDialog->findChild<QSplitter*>("postTemplateCatalogSplitter"))
                {
                    settings.setValue("ui/templateEditor/postCatalogSplitter", splitter->saveState());
                }
                if (m_postTemplateTree != nullptr)
                {
                    settings.setValue("ui/templateEditor/postCatalogTreeHeader",
                                      m_postTemplateTree->header()->saveState());
                }
            });

    connect(m_postTemplateDialog, &QDialog::destroyed, this,
            [this]()
            {
                m_postTemplateSearchEdit = nullptr;
                m_postTemplateTree = nullptr;
                m_postTemplateTitleEdit = nullptr;
                m_postTemplateTypeEdit = nullptr;
                m_postTemplateBodyEdit = nullptr;
                m_postTemplateDeleteButton = nullptr;
                m_postTemplateSaveButton = nullptr;
                m_postTemplateDialog = nullptr;
            });
}

void TemplateEditor::syncPostTemplateEditorsFromTemplate()
{
    sanitizePostTemplateCatalog();

    const auto builtinTemplatesMap = builtinPostTemplates();
    const auto builtinMetaMap = builtinPostTemplateMeta();
    for (auto it = builtinTemplatesMap.constBegin(); it != builtinTemplatesMap.constEnd(); ++it)
    {
        if (!m_postTemplates.contains(it.key()))
        {
            m_postTemplates.insert(it.key(), it.value());
        }
        if (!m_postTemplateMeta.contains(it.key()))
        {
            m_postTemplateMeta.insert(it.key(), builtinMetaMap.value(it.key()));
        }
    }

    for (auto it = m_postTemplates.constBegin(); it != m_postTemplates.constEnd(); ++it)
    {
        if (!m_postTemplateMeta.contains(it.key()))
        {
            PostTemplateMeta meta;
            meta.title = displayNameForPostKey(it.key());
            meta.platform = platformForPostKey(it.key());
            meta.category = sectionCodeFromKey(it.key());
            m_postTemplateMeta.insert(it.key(), meta);
        }
        else
        {
            PostTemplateMeta meta = m_postTemplateMeta.value(it.key());
            if (meta.title.trimmed().isEmpty())
            {
                meta.title = displayNameForPostKey(it.key());
            }
            if (meta.platform.trimmed().isEmpty())
            {
                meta.platform = platformForPostKey(it.key());
            }
            meta.category = sectionCodeFromMeta(it.key(), meta);
            m_postTemplateMeta.insert(it.key(), meta);
        }
    }

    rebuildPostTemplateTree(m_postTemplateSearchEdit ? m_postTemplateSearchEdit->text() : QString());
}

void TemplateEditor::loadPostTemplateCatalogFromDisk(const ReleaseTemplate& t)
{
    m_postTemplates.clear();
    m_postTemplateMeta.clear();

    const QString catalogPath = postTemplateCatalogFilePath();
    if (!QFile::exists(catalogPath))
    {
        const QString legacy = legacyPostTemplateCatalogFilePath();
        if (QFile::exists(legacy))
        {
            QFile::copy(legacy, catalogPath);
        }
    }

    QFile catalogFile(catalogPath);
    if (catalogFile.exists() && catalogFile.open(QIODevice::ReadOnly))
    {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(catalogFile.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
        {
            const QJsonObject root = doc.object();
            const QJsonObject templatesObj = root.value("postTemplates").toObject();
            for (auto it = templatesObj.constBegin(); it != templatesObj.constEnd(); ++it)
            {
                m_postTemplates.insert(it.key(), it.value().toString());
            }

            const QJsonObject metaObj = root.value("postTemplateMeta").toObject();
            for (auto it = metaObj.constBegin(); it != metaObj.constEnd(); ++it)
            {
                PostTemplateMeta meta;
                meta.read(it.value().toObject());
                m_postTemplateMeta.insert(it.key(), meta);
            }
        }
    }

    // Migration path: take non-core catalog entries from release template if catalog file is empty/new.
    if (m_postTemplates.isEmpty())
    {
        for (auto it = t.postTemplates.constBegin(); it != t.postTemplates.constEnd(); ++it)
        {
            if (!isRequiredBuiltinPostKey(it.key()))
            {
                m_postTemplates.insert(it.key(), it.value());
            }
        }
        for (auto it = t.postTemplateMeta.constBegin(); it != t.postTemplateMeta.constEnd(); ++it)
        {
            if (!isRequiredBuiltinPostKey(it.key()))
            {
                m_postTemplateMeta.insert(it.key(), it.value());
            }
        }
    }

    syncPostTemplateEditorsFromTemplate();
    savePostTemplateCatalogToDisk();
}

void TemplateEditor::savePostTemplateCatalogToDisk() const
{
    QJsonObject root;
    QJsonObject templatesObj;
    QJsonObject metaObj;

    for (auto it = m_postTemplates.constBegin(); it != m_postTemplates.constEnd(); ++it)
    {
        // Keep catalog global-only; core legacy templates are edited in series template.
        if (isRequiredBuiltinPostKey(it.key()))
        {
            continue;
        }
        templatesObj.insert(it.key(), it.value());
    }

    for (auto it = m_postTemplateMeta.constBegin(); it != m_postTemplateMeta.constEnd(); ++it)
    {
        if (isRequiredBuiltinPostKey(it.key()))
        {
            continue;
        }
        QJsonObject metaJson;
        it.value().write(metaJson);
        metaObj.insert(it.key(), metaJson);
    }

    root.insert("postTemplates", templatesObj);
    root.insert("postTemplateMeta", metaObj);

    QSaveFile out(postTemplateCatalogFilePath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.commit();
}

void TemplateEditor::syncPostTemplateCatalogToTemplate(ReleaseTemplate& t) const
{
    t.postTemplates.clear();
    t.postTemplates.insert("tg_mp4", ui->postTgMp4Edit->toPlainText());
    t.postTemplates.insert("tg_mkv", ui->postTgMkvEdit->toPlainText());
    t.postTemplates.insert("vk", ui->postVkEdit->toPlainText());
    t.postTemplates.insert("vk_comment", ui->postVkCommentEdit->toPlainText());
    ensureBuiltinLegacyPostTemplates(t.postTemplates);

    QMap<QString, QString> sanitized = m_postTemplates;
    QMap<QString, PostTemplateMeta> sanitizedMeta = m_postTemplateMeta;
    {
        static const QRegularExpression garbageKeyRx("^_*[0-9]+$");
        QStringList keysToDrop;
        for (auto it = sanitized.constBegin(); it != sanitized.constEnd(); ++it)
        {
            const QString key = it.key();
            if (isRequiredBuiltinPostKey(key))
            {
                continue;
            }
            const QString body = it.value().trimmed();
            const PostTemplateMeta meta = sanitizedMeta.value(key);
            const bool looksGarbageByKey = garbageKeyRx.match(key).hasMatch();
            const bool looksGarbageByMeta = body.isEmpty() && meta.title.trimmed() == key &&
                                            meta.platform.trimmed().toLower() == "other" &&
                                            meta.category.trimmed() == "Общие" && meta.tags.isEmpty();
            if (looksGarbageByKey || looksGarbageByMeta)
            {
                keysToDrop.append(key);
            }
        }
        for (const QString& key : keysToDrop)
        {
            sanitized.remove(key);
            sanitizedMeta.remove(key);
        }
    }

    for (auto it = sanitized.constBegin(); it != sanitized.constEnd(); ++it)
    {
        if (!t.postTemplates.contains(it.key()))
        {
            t.postTemplates.insert(it.key(), it.value());
        }
    }
    t.postTemplateMeta = sanitizedMeta;
}

void TemplateEditor::rebuildPostTemplateTree(const QString& filterText)
{
    if (!m_postTemplateTree)
    {
        return;
    }

    const QString filter = filterText.trimmed().toLower();
    const QString previousKey =
        !m_selectedPostTemplateKey.isEmpty() ? m_selectedPostTemplateKey : selectedPostTemplateKey();
    QTreeWidgetItem* itemToSelect = nullptr;

    m_postTemplateTree->clear();

    // Fixed group order requested by user.
    const QStringList sectionOrder = {"tg_post", "vk_post", "vk_comment", "tg_archive"};
    QMap<QString, QMultiMap<QString, QString>> sectionItems; // sectionCode -> (title -> key)

    for (auto it = m_postTemplates.constBegin(); it != m_postTemplates.constEnd(); ++it)
    {
        const QString key = it.key();
        const PostTemplateMeta meta = m_postTemplateMeta.value(key);
        const QString title = meta.title.isEmpty() ? displayNameForPostKey(key) : meta.title;
        const QString sectionCode = sectionCodeFromMeta(key, meta);
        const QString sectionName = sectionLabel(sectionCode);

        const QString haystack = QString("%1 %2 %3").arg(key, title, sectionName).toLower();
        if (!filter.isEmpty() && !haystack.contains(filter))
        {
            continue;
        }

        sectionItems[sectionCode].insert(title.toLower() + "|" + key.toLower(), key);
    }

    for (const QString& sectionCode : sectionOrder)
    {
        if (!sectionItems.contains(sectionCode) || sectionItems.value(sectionCode).isEmpty())
        {
            continue;
        }

        auto* groupItem = new QTreeWidgetItem(m_postTemplateTree);
        groupItem->setText(0, sectionLabel(sectionCode));
        groupItem->setFirstColumnSpanned(true);
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);

        const auto items = sectionItems.value(sectionCode);
        for (auto itemIt = items.constBegin(); itemIt != items.constEnd(); ++itemIt)
        {
            const QString key = itemIt.value();
            const PostTemplateMeta meta = m_postTemplateMeta.value(key);
            const QString title = meta.title.isEmpty() ? displayNameForPostKey(key) : meta.title;

            auto* item = new QTreeWidgetItem(groupItem);
            item->setText(0, title);
            item->setData(0, kPostTemplateTreeKeyRole, key);
            item->setToolTip(0, key);
            if (!previousKey.isEmpty() && key == previousKey)
            {
                itemToSelect = item;
            }
        }
    }

    m_postTemplateTree->expandAll();
    if (itemToSelect != nullptr)
    {
        m_postTemplateTree->setCurrentItem(itemToSelect);
    }
    else if (m_postTemplateTree->topLevelItemCount() > 0)
    {
        auto* firstGroup = m_postTemplateTree->topLevelItem(0);
        if (firstGroup && firstGroup->childCount() > 0)
        {
            m_postTemplateTree->setCurrentItem(firstGroup->child(0));
        }
    }
    else
    {
        loadSelectedPostTemplateToEditor();
    }
}

void TemplateEditor::saveCurrentPostTemplateEdits(const QString& key)
{
    const QString targetKey = key.isEmpty() ? selectedPostTemplateKey() : key;
    if (targetKey.isEmpty() || !m_postTemplates.contains(targetKey))
    {
        return;
    }

    PostTemplateMeta meta = m_postTemplateMeta.value(targetKey);
    meta.title = m_postTemplateTitleEdit->text().trimmed();
    const QString sectionCode = m_postTemplateTypeEdit ? m_postTemplateTypeEdit->currentData().toString() : "tg_post";
    meta.category = sectionCode;
    meta.platform = (sectionCode.startsWith("vk")) ? "vk" : "telegram";
    meta.tags.clear();
    m_postTemplateMeta.insert(targetKey, meta);
    m_postTemplates.insert(targetKey, m_postTemplateBodyEdit->toPlainText());
}

void TemplateEditor::loadSelectedPostTemplateToEditor()
{
    const QString key = selectedPostTemplateKey();
    const bool hasSelection = !key.isEmpty() && m_postTemplates.contains(key);

    m_postTemplateDeleteButton->setEnabled(hasSelection);
    m_postTemplateTitleEdit->setEnabled(hasSelection);
    m_postTemplateTypeEdit->setEnabled(hasSelection);
    m_postTemplateBodyEdit->setEnabled(hasSelection);
    m_postTemplateSaveButton->setEnabled(hasSelection);

    if (!hasSelection)
    {
        // Ignore non-template rows (group headers) without clearing editor values.
        return;
    }

    m_selectedPostTemplateKey = key;

    const PostTemplateMeta meta = m_postTemplateMeta.value(key);
    const QSignalBlocker b1(m_postTemplateTitleEdit);
    const QSignalBlocker b2(m_postTemplateTypeEdit);
    const QSignalBlocker b3(m_postTemplateBodyEdit);
    m_postTemplateTitleEdit->setText(meta.title.isEmpty() ? displayNameForPostKey(key) : meta.title);
    const QString sectionCode = sectionCodeFromMeta(key, meta);
    const int typeIndex = m_postTemplateTypeEdit->findData(sectionCode);
    if (typeIndex >= 0)
    {
        m_postTemplateTypeEdit->setCurrentIndex(typeIndex);
    }
    m_postTemplateBodyEdit->setPlainText(m_postTemplates.value(key));

    const bool protectedBuiltin = isRequiredBuiltinPostKey(key);
    m_postTemplateDeleteButton->setEnabled(!protectedBuiltin);
    m_postTemplateTitleEdit->setEnabled(!protectedBuiltin);
    m_postTemplateTypeEdit->setEnabled(!protectedBuiltin);
    if (protectedBuiltin)
    {
        m_postTemplateDeleteButton->setToolTip("Базовые шаблоны tg_mp4, tg_mkv, vk и vk_comment удалять нельзя.");
        m_postTemplateTitleEdit->setToolTip("Для базовых шаблонов метаданные фиксированы.");
        m_postTemplateTypeEdit->setToolTip("Для базовых шаблонов раздел фиксирован.");
    }
    else
    {
        m_postTemplateDeleteButton->setToolTip("Удалить выбранный шаблон.");
        m_postTemplateTitleEdit->setToolTip("");
        m_postTemplateTypeEdit->setToolTip("");
    }
}

QMap<QString, QString> TemplateEditor::builtinPostTemplates()
{
    QMap<QString, QString> result;
    result["tg_mp4"] =
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
    result["tg_mkv"] = "%SERIES_TITLE%\n"
                       "Серия %EPISODE_NUMBER%/%TOTAL_EPISODES%\n"
                       "#Хештег";
    result["vk"] =
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
    result["vk_comment"] =
        "А также вы можете поддержать наш коллектив на бусти: https://boosty.to/dubl/single-payment/donation/634652\n\n"
        "ТГ: https://t.me/dublyajnaya\n\n"
        "TVOЁ (99 руб. по промокоду: Dublyazhnaya): https://tvoe.live/p/";
    return result;
}

void TemplateEditor::ensureBuiltinLegacyPostTemplates(QMap<QString, QString>& templates)
{
    const auto builtin = builtinPostTemplates();
    const QStringList requiredKeys = {"tg_mp4", "tg_mkv", "vk", "vk_comment"};
    for (const QString& key : requiredKeys)
    {
        if (!templates.contains(key) || templates.value(key).trimmed().isEmpty())
        {
            templates.insert(key, builtin.value(key));
        }
    }
}

void TemplateEditor::sanitizePostTemplateCatalog()
{
    static const QRegularExpression garbageKeyRx("^_*[0-9]+$");
    QStringList keysToDrop;

    // Remove meta entries without template payload.
    for (auto it = m_postTemplateMeta.constBegin(); it != m_postTemplateMeta.constEnd(); ++it)
    {
        if (!m_postTemplates.contains(it.key()))
        {
            keysToDrop.append(it.key());
        }
    }

    // Remove auto-garbage empty templates that were accidentally generated.
    for (auto it = m_postTemplates.constBegin(); it != m_postTemplates.constEnd(); ++it)
    {
        const QString key = it.key();
        if (isRequiredBuiltinPostKey(key))
        {
            continue;
        }

        const QString body = it.value().trimmed();
        const PostTemplateMeta meta = m_postTemplateMeta.value(key);
        const bool looksGarbageByKey = garbageKeyRx.match(key).hasMatch();
        const bool looksGarbageByMeta = body.isEmpty() && meta.title.trimmed() == key &&
                                        meta.platform.trimmed().toLower() == "other" &&
                                        meta.category.trimmed() == "Общие" && meta.tags.isEmpty();

        if (looksGarbageByKey || looksGarbageByMeta)
        {
            keysToDrop.append(key);
        }
    }

    keysToDrop.removeDuplicates();
    for (const QString& key : keysToDrop)
    {
        m_postTemplates.remove(key);
        m_postTemplateMeta.remove(key);
    }
}

QMap<QString, PostTemplateMeta> TemplateEditor::builtinPostTemplateMeta()
{
    QMap<QString, PostTemplateMeta> result;

    PostTemplateMeta tgMp4;
    tgMp4.title = "Telegram (Канал)";
    tgMp4.platform = "telegram";
    tgMp4.category = "tg_post";
    tgMp4.tags = {"tg", "mp4", "дубляж"};
    tgMp4.sortOrder = 10;
    tgMp4.builtin = true;
    result.insert("tg_mp4", tgMp4);

    PostTemplateMeta tgMkv = tgMp4;
    tgMkv.title = "Telegram (Архив)";
    tgMkv.tags = {"tg", "mkv", "архив"};
    tgMkv.sortOrder = 20;
    tgMkv.category = "tg_archive";
    result.insert("tg_mkv", tgMkv);

    PostTemplateMeta vk;
    vk.title = "VK (Пост)";
    vk.platform = "vk";
    vk.category = "vk_post";
    vk.tags = {"vk", "пост"};
    vk.sortOrder = 30;
    vk.builtin = true;
    result.insert("vk", vk);

    PostTemplateMeta vkComment = vk;
    vkComment.title = "VK (Комментарий)";
    vkComment.tags = {"vk", "комментарий"};
    vkComment.sortOrder = 40;
    vkComment.category = "vk_comment";
    result.insert("vk_comment", vkComment);

    return result;
}

QString TemplateEditor::displayNameForPostKey(const QString& key)
{
    return titleFromKey(key);
}

QString TemplateEditor::platformForPostKey(const QString& key)
{
    return ::platformFromKey(key);
}

bool TemplateEditor::isTelegramPlatform(const QString& platform)
{
    return platform == "telegram" || platform == "tg";
}

QString TemplateEditor::selectedPostTemplateKey() const
{
    auto* item = m_postTemplateTree ? m_postTemplateTree->currentItem() : nullptr;
    if (!item)
    {
        return QString();
    }
    return item->data(0, kPostTemplateTreeKeyRole).toString().trimmed();
}

QString TemplateEditor::createUniquePostTemplateKey(const QString& title) const
{
    QString base = title.toLower().trimmed();
    base.replace(QRegularExpression("\\s+"), "_");
    base.replace(QRegularExpression("[^a-z0-9_]+"), "");
    base.replace(QRegularExpression("_+"), "_");
    base = base.trimmed();
    while (base.startsWith('_'))
    {
        base.remove(0, 1);
    }
    while (base.endsWith('_'))
    {
        base.chop(1);
    }

    if (base.isEmpty())
    {
        base = "custom_post_template";
    }
    if (!base.isEmpty() && base.at(0).isDigit())
    {
        base.prepend("custom_");
    }
    if (!m_postTemplates.contains(base))
    {
        return base;
    }

    int index = 2;
    QString candidate = QString("%1_%2").arg(base).arg(index);
    while (m_postTemplates.contains(candidate))
    {
        ++index;
        candidate = QString("%1_%2").arg(base).arg(index);
    }
    return candidate;
}

QPlainTextEdit* TemplateEditor::legacyEditorForPostKey(const QString& key) const
{
    if (key == "tg_mp4")
    {
        return ui->postTgMp4Edit;
    }
    if (key == "tg_mkv")
    {
        return ui->postTgMkvEdit;
    }
    if (key == "vk")
    {
        return ui->postVkEdit;
    }
    if (key == "vk_comment")
    {
        return ui->postVkCommentEdit;
    }
    return nullptr;
}

void TemplateEditor::onPostTemplateSelectionChanged()
{
    const QString newKey = selectedPostTemplateKey();
    if (newKey.isEmpty())
    {
        return;
    }

    if (!m_selectedPostTemplateKey.isEmpty() && m_selectedPostTemplateKey != newKey)
    {
        saveCurrentPostTemplateEdits(m_selectedPostTemplateKey);
    }
    m_selectedPostTemplateKey = newKey;
    loadSelectedPostTemplateToEditor();
}

void TemplateEditor::onPostTemplateFilterChanged(const QString& filterText)
{
    saveCurrentPostTemplateEdits();
    rebuildPostTemplateTree(filterText);
}

void TemplateEditor::onPostTemplateAddClicked()
{
    saveCurrentPostTemplateEdits();
    const QString title = QString("Новый шаблон %1").arg(m_postTemplates.size() + 1);
    const QString key = createUniquePostTemplateKey(title);

    PostTemplateMeta meta;
    meta.title = title;
    meta.platform = "telegram";
    meta.category = "tg_post";
    meta.tags = {};
    meta.sortOrder = 1000 + static_cast<int>(m_postTemplates.size());
    meta.builtin = false;

    m_postTemplates.insert(key, "");
    m_postTemplateMeta.insert(key, meta);
    rebuildPostTemplateTree(m_postTemplateSearchEdit->text());
    savePostTemplateCatalogToDisk();

    QList<QTreeWidgetItem*> items = m_postTemplateTree->findItems(meta.title, Qt::MatchExactly | Qt::MatchRecursive, 0);
    if (!items.isEmpty())
    {
        m_postTemplateTree->setCurrentItem(items.first());
    }
}

void TemplateEditor::onPostTemplateDeleteClicked()
{
    auto* currentItem = m_postTemplateTree ? m_postTemplateTree->currentItem() : nullptr;
    QString key = selectedPostTemplateKey();
    if (key.isEmpty())
    {
        return;
    }

    if (isRequiredBuiltinPostKey(key))
    {
        QMessageBox::information(this, "Удаление недоступно",
                                 "Базовые ключи tg_mp4, tg_mkv, vk и vk_comment обязательны для публикации.");
        return;
    }

    const auto reply = QMessageBox::question(this, "Удалить шаблон", "Удалить выбранный пост-шаблон из каталога?");
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    qsizetype removedTemplates = m_postTemplates.remove(key);
    qsizetype removedMeta = m_postTemplateMeta.remove(key);
    if (removedTemplates == 0 && currentItem != nullptr)
    {
        const QString fallbackTitle = currentItem->text(0).trimmed();
        if (!fallbackTitle.isEmpty())
        {
            key = fallbackTitle;
            removedTemplates = m_postTemplates.remove(fallbackTitle);
            removedMeta = m_postTemplateMeta.remove(fallbackTitle);
        }
    }
    if (removedTemplates == 0 && removedMeta == 0)
    {
        QMessageBox::warning(this, "Не удалось удалить",
                             "Выбранный шаблон не найден в каталоге. Попробуйте обновить список.");
        return;
    }
    rebuildPostTemplateTree(m_postTemplateSearchEdit->text());
    savePostTemplateCatalogToDisk();
}

void TemplateEditor::onPostTemplateRestoreBuiltinClicked()
{
    saveCurrentPostTemplateEdits();
    const auto templatesMap = builtinPostTemplates();
    const auto metaMap = builtinPostTemplateMeta();

    for (auto it = templatesMap.constBegin(); it != templatesMap.constEnd(); ++it)
    {
        if (!m_postTemplates.contains(it.key()))
        {
            m_postTemplates.insert(it.key(), it.value());
        }
    }

    for (auto it = metaMap.constBegin(); it != metaMap.constEnd(); ++it)
    {
        m_postTemplateMeta.insert(it.key(), it.value());
    }

    rebuildPostTemplateTree(m_postTemplateSearchEdit->text());
    savePostTemplateCatalogToDisk();
}

void TemplateEditor::onPostTemplateMetaEdited()
{
    const QString key = selectedPostTemplateKey();
    if (key.isEmpty())
    {
        return;
    }

    saveCurrentPostTemplateEdits();
    rebuildPostTemplateTree(m_postTemplateSearchEdit->text());
}

void TemplateEditor::onPostTemplateBodyEdited()
{
    const QString key = selectedPostTemplateKey();
    if (key.isEmpty())
    {
        return;
    }
    m_postTemplates.insert(key, m_postTemplateBodyEdit->toPlainText());
}

void TemplateEditor::onPostTemplateSaveClicked()
{
    saveCurrentPostTemplateEdits();
    savePostTemplateCatalogToDisk();
    QMessageBox::information(this, "Каталог сохранен", "Шаблон сохранен в отдельный каталог постов.");
}

void TemplateEditor::onParsePostToTemplateClicked()
{
    QString parseKey = "tg_mp4";
    if (m_parseSourceComboBox != nullptr)
    {
        parseKey = m_parseSourceComboBox->currentData().toString();
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    QString postText;
    if (mimeData)
    {
        const QString platform = platformForPostKey(parseKey).toLower();
        if (isTelegramPlatform(platform))
        {
            postText = TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mimeData);
        }
        if (postText.trimmed().isEmpty())
        {
            postText = mimeData->text();
        }
    }
    postText = normalizeForParsing(postText);

    if (postText.isEmpty())
    {
        QMessageBox::warning(this, "Буфер обмена пуст", "Скопируйте текст поста и повторите импорт.");
        return;
    }

    const QString sourceType = platformForPostKey(parseKey).toLower();
    const PostParseResult parseResult = PostGenerator::parsePostToFields(postText, sourceType);
    if (!parseResult.success)
    {
        QMessageBox::warning(this, "Парсинг не выполнен", parseResult.errors.join("\n"));
        return;
    }

    if (QPlainTextEdit* templateEditor = legacyEditorForPostKey(parseKey))
    {
        const QString parsedTemplateText = parseResult.fields.value("%PARSED_TEMPLATE_TEXT%").trimmed();
        if (!parsedTemplateText.isEmpty())
        {
            const QSignalBlocker blocker(templateEditor);
            templateEditor->setPlainText(parsedTemplateText);
        }
    }

    if (sourceType == "telegram")
    {
        const QString vkTemplate = buildVkTemplateFromParseResult(parseResult);
        if (!vkTemplate.isEmpty())
        {
            const QSignalBlocker blocker(ui->postVkEdit);
            ui->postVkEdit->setPlainText(vkTemplate);
        }
    }

    applyParsedFieldsToTemplate(parseResult);
    const qsizetype extractedFieldsCount =
        parseResult.fields.size() - (parseResult.fields.contains("%PARSED_TEMPLATE_TEXT%") ? 1 : 0);
    QMessageBox::information(this, "Готово",
                             QString("Извлечено полей: %1.\nПроверьте вкладки \"Создание ТБ\" и \"Публикация\".")
                                 .arg(static_cast<int>(qMax(qsizetype(0), extractedFieldsCount))));
}

void TemplateEditor::applyParsedFieldsToTemplate(const PostParseResult& parseResult)
{
    auto value = [&parseResult](const QString& placeholder) -> QString
    {
        return parseResult.fields.value(placeholder).trimmed();
    };

    if (!value("%SERIES_TITLE%").isEmpty())
    {
        ui->seriesTitleForPostEdit->setText(value("%SERIES_TITLE%"));
    }
    if (!value("%TOTAL_EPISODES%").isEmpty())
    {
        bool ok = false;
        const int total = value("%TOTAL_EPISODES%").toInt(&ok);
        if (ok)
        {
            ui->totalEpisodesSpinBox->setValue(total);
        }
    }
    if (!value("%CAST_LIST%").isEmpty())
    {
        const QString castValue = value("%CAST_LIST%");
        QRegularExpression rx("((\\, )|\\,|\\n)");
        const QStringList castList = castValue.split(rx, Qt::SkipEmptyParts);
        ui->castEdit->setPlainText(castList.join(", "));
    }

    if (!value("%DIRECTOR%").isEmpty())
        ui->directorEdit->setText(value("%DIRECTOR%"));
    if (!value("%SOUND_ENGINEER%").isEmpty())
        ui->soundEngineerEdit->setText(value("%SOUND_ENGINEER%"));
    if (!value("%SONG_ENGINEER%").isEmpty())
        ui->songsSoundEngineerEdit->setText(value("%SONG_ENGINEER%"));
    if (!value("%EPISODE_ENGINEER%").isEmpty())
        ui->episodeSoundEngineerEdit->setText(value("%EPISODE_ENGINEER%"));
    if (!value("%RECORDING_ENGINEER%").isEmpty())
        ui->recordingSoundEngineerEdit->setText(value("%RECORDING_ENGINEER%"));
    if (!value("%SUB_AUTHOR%").isEmpty())
        ui->subAuthorEdit->setText(value("%SUB_AUTHOR%"));
    if (!value("%TIMING_AUTHOR%").isEmpty())
        ui->timingAuthorEdit->setText(value("%TIMING_AUTHOR%"));
    if (!value("%SIGNS_AUTHOR%").isEmpty())
        ui->signsAuthorEdit->setText(value("%SIGNS_AUTHOR%"));
    if (!value("%TRANSLATION_EDITOR%").isEmpty())
        ui->translationEditorEdit->setText(value("%TRANSLATION_EDITOR%"));
    if (!value("%RELEASE_BUILDER%").isEmpty())
        ui->releaseBuilderEdit->setText(value("%RELEASE_BUILDER%"));
}

void TemplateEditor::on_selectStylesButton_clicked()
{
    QString filePath =
        QFileDialog::getOpenFileName(this, "Выберите .ass файл для анализа", "", "ASS Subtitles (*.ass)");
    if (filePath.isEmpty())
    {
        return;
    }

    StyleSelectorDialog dialog(this);
    dialog.analyzeFile(filePath);

    if (dialog.exec() == QDialog::Accepted)
    {
        QStringList selectedStyles = dialog.getSelectedStyles();
        ui->signStylesEdit->setPlainText(selectedStyles.join('\n'));
    }
}

void TemplateEditor::on_helpButton_clicked()
{
    if (m_helpDialog)
    {
        if (m_helpDialog->isVisible())
        {
            m_helpDialog->activateWindow();
            return;
        }
    }

    m_helpDialog = new QDialog(this);
    m_helpDialog->setAttribute(Qt::WA_DeleteOnClose);

    m_helpDialog->setWindowTitle("Справка по шаблонам и форматированию");
    m_helpDialog->setMinimumSize(500, 800);

    QScrollArea* scrollArea = new QScrollArea(m_helpDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    QWidget* scrollWidget = new QWidget();
    scrollArea->setWidget(scrollWidget);
    QVBoxLayout* mainLayout = new QVBoxLayout(scrollWidget);

    auto addRow = [&](QGridLayout* layout, int row, const QString& code, const QString& description)
    {
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
    QWidget* vkWidget = new QWidget();
    QGridLayout* vkLayout = new QGridLayout(vkWidget);
    addRow(vkLayout, 0, "[https://vk.com/dublyajnaya|ТО Дубляжная]", "Ссылка на внешний ресурс");
    vkLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(vkWidget);

    mainLayout->addStretch(1);

    QVBoxLayout* dialogLayout = new QVBoxLayout(m_helpDialog);
    dialogLayout->addWidget(scrollArea);
    m_helpDialog->show();
}

void TemplateEditor::slotValidateAndAccept()
{
    // Validate fields that will be used in file/directory names
    QString seriesTitle = ui->seriesTitleEdit->text().trimmed();

    if (containsForbiddenChars(seriesTitle))
    {
        QString found = forbiddenCharsFound(seriesTitle);
        QMessageBox::warning(this, "Недопустимые символы",
                             QString("Название серии содержит символы, запрещённые в именах файлов Windows:\n\n"
                                     "  %1\n\n"
                                     "Запрещённые символы:  : \" < > | ? *\n\n"
                                     "Пожалуйста, уберите их из названия.")
                                 .arg(found));
        ui->seriesTitleEdit->setFocus();
        return;
    }

    const QStringList duplicateCastPairs = findDuplicatedCastNamePairs(ui->castEdit->toPlainText());
    if (!duplicateCastPairs.isEmpty())
    {
        QMessageBox::warning(this, "Повторы в касте",
                             QString("В списке каста найдены повторяющиеся ФИО (Имя Фамилия):\n\n%1\n\n"
                                     "Проверьте поле \"Каст\" и удалите дубли.")
                                 .arg(duplicateCastPairs.join("\n")));
        ui->castEdit->setFocus();
        return;
    }

    accept();
}

void TemplateEditor::enableDraftAutosave(const QString& draftFilePath)
{
    m_draftFilePath = draftFilePath;
    m_lastDraftPayload.clear();
    m_draftAutosaveTimer.start();
    saveDraftNow();
}

void TemplateEditor::saveDraftNow()
{
    if (m_draftFilePath.isEmpty())
    {
        return;
    }

    ReleaseTemplate draftTemplate = getTemplate();
    QJsonObject json;
    draftTemplate.write(json);
    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (payload == m_lastDraftPayload)
    {
        return;
    }

    QSaveFile draftFile(m_draftFilePath);
    if (!draftFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }

    draftFile.write(payload);
    if (!draftFile.commit())
    {
        return;
    }

    m_lastDraftPayload = payload;
}

bool TemplateEditor::containsForbiddenChars(const QString& text)
{
    static const QString kForbidden = ":\"<>|?*";
    for (const QChar& ch : text)
    {
        if (kForbidden.contains(ch))
        {
            return true;
        }
    }
    return false;
}

QString TemplateEditor::forbiddenCharsFound(const QString& text)
{
    static const QString kForbidden = ":\"<>|?*";
    QStringList found;
    for (const QChar& ch : text)
    {
        if (kForbidden.contains(ch))
        {
            QString repr = (ch == '"') ? "\"" : QString(ch);
            if (!found.contains(repr))
            {
                found.append(repr);
            }
        }
    }
    return found.join("  ");
}
