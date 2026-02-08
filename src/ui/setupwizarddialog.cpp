#include "setupwizarddialog.h"

#include "appsettings.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QUrl>
#include <QVBoxLayout>

SetupWizardDialog::SetupWizardDialog(QWidget* parent) : QDialog(parent), m_stack(new QStackedWidget(this))
{
    setWindowTitle("Мастер настройки — DubbingTool");
    setMinimumSize(600, 420);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_stack, 1);

    // Navigation bar
    auto* navLayout = new QHBoxLayout();
    m_backButton = new QPushButton("← Назад", this);
    m_skipButton = new QPushButton("Пропустить", this);
    m_nextButton = new QPushButton("Далее →", this);
    m_finishButton = new QPushButton("Готово", this);

    navLayout->addWidget(m_backButton);
    navLayout->addStretch();
    navLayout->addWidget(m_skipButton);
    navLayout->addWidget(m_nextButton);
    navLayout->addWidget(m_finishButton);
    mainLayout->addLayout(navLayout);

    // Build pages
    buildToolsPage();
    buildQBittorrentPage();
    buildPresetPage();

    // Initial state
    m_backButton->setVisible(false);
    m_skipButton->setVisible(false);
    m_finishButton->setVisible(false);
    m_nextButton->setVisible(true);

    connect(m_backButton, &QPushButton::clicked, this, &SetupWizardDialog::slotPrevPage);
    connect(m_nextButton, &QPushButton::clicked, this, &SetupWizardDialog::slotNextPage);
    connect(m_finishButton, &QPushButton::clicked, this, &SetupWizardDialog::slotFinish);
    connect(m_skipButton, &QPushButton::clicked, this, &SetupWizardDialog::slotSkipQBittorrent);

    detectTools();
    updateNextButtonState();
}

void SetupWizardDialog::buildToolsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* title = new QLabel("Шаг 1 из 3 — Проверка инструментов", page);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* desc = new QLabel("Для работы необходимы FFmpeg и MKVToolNix. "
                            "Программа автоматически ищет их в папке tools/, рядом с exe и в системном PATH. "
                            "Если инструмент не найден — укажите путь вручную или скачайте по ссылкам ниже.",
                            page);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Download links
    auto* linksLayout = new QHBoxLayout();
    auto* ffmpegLink = new QLabel("<a href=\"https://www.gyan.dev/ffmpeg/builds/\">Скачать FFmpeg</a>", page);
    ffmpegLink->setOpenExternalLinks(true);
    auto* mkvtoolnixLink =
        new QLabel("<a href=\"https://mkvtoolnix.download/downloads.html\">Скачать MKVToolNix</a>", page);
    mkvtoolnixLink->setOpenExternalLinks(true);
    linksLayout->addWidget(ffmpegLink);
    linksLayout->addWidget(mkvtoolnixLink);
    linksLayout->addStretch();
    layout->addLayout(linksLayout);

    layout->addSpacing(4);

    m_toolsGrid = new QGridLayout();
    m_toolsGrid->setColumnStretch(2, 1);

    struct ToolDef
    {
        QString exeName;
        QString displayName;
        bool required;
    };
    const QList<ToolDef> kToolDefs = {
        {    "ffmpeg.exe",     "FFmpeg", true},
        {   "ffprobe.exe",    "FFprobe", true},
        {  "mkvmerge.exe",   "MKVmerge", true},
        {"mkvextract.exe", "MKVextract", true},
    };

    for (int32_t i = 0; i < kToolDefs.size(); ++i)
    {
        ToolRow row;
        row.exeName = kToolDefs[i].exeName;
        row.displayName = kToolDefs[i].displayName;
        row.required = kToolDefs[i].required;

        row.statusIcon = new QLabel("⏳", page);
        row.statusIcon->setFixedWidth(24);
        row.statusIcon->setAlignment(Qt::AlignCenter);

        auto* nameLabel = new QLabel(row.displayName + ":", page);
        nameLabel->setFixedWidth(100);

        row.pathEdit = new QLineEdit(page);
        row.pathEdit->setReadOnly(true);
        row.pathEdit->setPlaceholderText("Не найден");

        row.browseButton = new QPushButton("Обзор...", page);
        row.browseButton->setFixedWidth(80);

        m_toolsGrid->addWidget(row.statusIcon, i, 0);
        m_toolsGrid->addWidget(nameLabel, i, 1);
        m_toolsGrid->addWidget(row.pathEdit, i, 2);
        m_toolsGrid->addWidget(row.browseButton, i, 3);

        const int32_t kToolIdx = i;
        connect(row.browseButton, &QPushButton::clicked, this, [this, kToolIdx]() { slotBrowseTool(kToolIdx); });

        m_tools.append(row);
    }

    // Separator before optional tools
    int32_t separatorRow = kToolDefs.size();
    auto* separator = new QFrame(page);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    m_toolsGrid->addWidget(separator, separatorRow, 0, 1, 4);

    auto* optionalLabel = new QLabel("<i>Необязательные:</i>", page);
    m_toolsGrid->addWidget(optionalLabel, separatorRow + 1, 0, 1, 4);

    // NUGEN Audio AMB — optional
    {
        int32_t nugenRow = separatorRow + 2;
        ToolRow row;
        row.exeName = "NUGEN Audio AMB.exe";
        row.displayName = "NUGEN Audio AMB";
        row.required = false;

        row.statusIcon = new QLabel("➖", page);
        row.statusIcon->setFixedWidth(24);
        row.statusIcon->setAlignment(Qt::AlignCenter);

        auto* nameLabel = new QLabel(row.displayName + ":", page);
        nameLabel->setFixedWidth(100);

        row.pathEdit = new QLineEdit(page);
        row.pathEdit->setReadOnly(true);
        row.pathEdit->setPlaceholderText("Не указан (необязательно)");

        row.browseButton = new QPushButton("Обзор...", page);
        row.browseButton->setFixedWidth(80);

        m_toolsGrid->addWidget(row.statusIcon, nugenRow, 0);
        m_toolsGrid->addWidget(nameLabel, nugenRow, 1);
        m_toolsGrid->addWidget(row.pathEdit, nugenRow, 2);
        m_toolsGrid->addWidget(row.browseButton, nugenRow, 3);

        const int32_t kToolIdx = m_tools.size();
        connect(row.browseButton, &QPushButton::clicked, this, [this, kToolIdx]() { slotBrowseTool(kToolIdx); });

        m_tools.append(row);
    }

    layout->addLayout(m_toolsGrid);
    layout->addStretch();

    m_stack->addWidget(page);
}

void SetupWizardDialog::buildQBittorrentPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* title = new QLabel("Шаг 2 из 3 — qBittorrent Web API", page);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* desc = new QLabel("Настройте подключение к qBittorrent для автоматической загрузки торрентов. "
                            "Если вы не используете qBittorrent — нажмите «Пропустить».",
                            page);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    layout->addSpacing(8);

    const auto& settings = AppSettings::instance();

    auto* group = new QGroupBox("Подключение", page);
    auto* form = new QGridLayout(group);

    form->addWidget(new QLabel("Хост:", group), 0, 0);
    m_qbtHostEdit = new QLineEdit(settings.qbittorrentHost(), group);
    form->addWidget(m_qbtHostEdit, 0, 1);

    form->addWidget(new QLabel("Порт:", group), 1, 0);
    m_qbtPortSpin = new QSpinBox(group);
    m_qbtPortSpin->setRange(1, 65535);
    m_qbtPortSpin->setValue(settings.qbittorrentPort());
    form->addWidget(m_qbtPortSpin, 1, 1);

    form->addWidget(new QLabel("Логин:", group), 2, 0);
    m_qbtUserEdit = new QLineEdit(settings.qbittorrentUser(), group);
    form->addWidget(m_qbtUserEdit, 2, 1);

    form->addWidget(new QLabel("Пароль:", group), 3, 0);
    m_qbtPasswordEdit = new QLineEdit(settings.qbittorrentPassword(), group);
    m_qbtPasswordEdit->setEchoMode(QLineEdit::Password);
    form->addWidget(m_qbtPasswordEdit, 3, 1);

    form->setColumnStretch(1, 1);
    layout->addWidget(group);
    layout->addStretch();

    m_stack->addWidget(page);
}

void SetupWizardDialog::buildPresetPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* title = new QLabel("Шаг 3 из 3 — Пресет рендера", page);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* desc = new QLabel("Выберите пресет рендера, соответствующий вашему оборудованию. "
                            "Его можно изменить позже в настройках или в шаблоне проекта.",
                            page);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    layout->addSpacing(8);

    auto* group = new QGroupBox("Пресет по умолчанию", page);
    auto* groupLayout = new QVBoxLayout(group);

    m_presetCombo = new QComboBox(group);
    const auto kPresets = AppSettings::instance().renderPresets();
    for (const auto& preset : kPresets)
    {
        m_presetCombo->addItem(preset.name);
    }
    groupLayout->addWidget(m_presetCombo);

    auto* hint = new QLabel("• NVIDIA — для видеокарт NVIDIA (быстро, хорошее качество)\n"
                            "• INTEL — для встроенной графики Intel (быстро)\n"
                            "• CPU — универсальный, работает везде (медленнее, но качественнее)",
                            group);
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);

    layout->addWidget(group);
    layout->addStretch();

    m_stack->addWidget(page);
}

void SetupWizardDialog::detectTools()
{
    const auto& settings = AppSettings::instance();

    // ffmpeg
    updateToolStatus(0, settings.ffmpegPath());
    // ffprobe
    updateToolStatus(1, settings.ffprobePath());
    // mkvmerge
    updateToolStatus(2, settings.mkvmergePath());
    // mkvextract
    updateToolStatus(3, settings.mkvextractPath());
    // NUGEN Audio AMB (optional)
    updateToolStatus(4, settings.nugenAmbPath());
}

void SetupWizardDialog::updateToolStatus(int32_t toolIndex, const QString& path)
{
    if (toolIndex < 0 || toolIndex >= m_tools.size())
    {
        return;
    }

    auto& row = m_tools[toolIndex];
    bool found = !path.isEmpty() && QFileInfo::exists(path);

    if (found)
    {
        row.statusIcon->setText("✅");
        row.pathEdit->setText(path);
        row.pathEdit->setToolTip(path);
    }
    else if (row.required)
    {
        row.statusIcon->setText("❌");
        row.pathEdit->setText("");
        row.pathEdit->setToolTip("Не найден");
    }
    else
    {
        row.statusIcon->setText("➖");
        row.pathEdit->setText("");
        row.pathEdit->setToolTip("Не указан (необязательно)");
    }
}

void SetupWizardDialog::updateNextButtonState()
{
    if (m_stack->currentIndex() != 0)
    {
        m_nextButton->setEnabled(true);
        return;
    }

    // Page 1: all required tools must be found
    bool allFound = true;
    for (const auto& row : m_tools)
    {
        if (row.required && !QFileInfo::exists(row.pathEdit->text()))
        {
            allFound = false;
            break;
        }
    }
    m_nextButton->setEnabled(allFound);
}

void SetupWizardDialog::slotBrowseTool(int32_t toolIndex)
{
    if (toolIndex < 0 || toolIndex >= m_tools.size())
    {
        return;
    }

    const auto& row = m_tools[toolIndex];
    QString filter = QString("%1 (%2)").arg(row.displayName, row.exeName);
    QString path = QFileDialog::getOpenFileName(this, "Укажите путь к " + row.displayName, QString(), filter);

    if (!path.isEmpty())
    {
        updateToolStatus(toolIndex, path);
        updateNextButtonState();
    }
}

void SetupWizardDialog::slotNextPage()
{
    int32_t current = m_stack->currentIndex();

    // Save page 1 data before leaving
    if (current == 0)
    {
        auto& settings = AppSettings::instance();
        if (m_tools.size() >= 5)
        {
            settings.setFfmpegPath(m_tools[0].pathEdit->text());
            // ffprobe (index 1) is auto-detected from ffmpeg path
            settings.setMkvmergePath(m_tools[2].pathEdit->text());
            settings.setMkvextractPath(m_tools[3].pathEdit->text());
            settings.setNugenAmbPath(m_tools[4].pathEdit->text());
        }
    }

    int32_t next = current + 1;
    if (next >= m_stack->count())
    {
        return;
    }

    m_stack->setCurrentIndex(next);

    // Update button visibility
    m_backButton->setVisible(next > 0);
    bool isLastPage = (next == m_stack->count() - 1);
    m_nextButton->setVisible(!isLastPage);
    m_finishButton->setVisible(isLastPage);
    m_skipButton->setVisible(next == 1); // Only on qBittorrent page

    updateNextButtonState();
}

void SetupWizardDialog::slotPrevPage()
{
    int32_t current = m_stack->currentIndex();
    if (current <= 0)
    {
        return;
    }

    int32_t prev = current - 1;
    m_stack->setCurrentIndex(prev);

    m_backButton->setVisible(prev > 0);
    m_nextButton->setVisible(true);
    m_finishButton->setVisible(false);
    m_skipButton->setVisible(prev == 1);

    updateNextButtonState();
}

void SetupWizardDialog::slotSkipQBittorrent()
{
    // Jump to preset page without saving qBittorrent settings
    m_stack->setCurrentIndex(2);
    m_backButton->setVisible(true);
    m_nextButton->setVisible(false);
    m_finishButton->setVisible(true);
    m_skipButton->setVisible(false);
}

void SetupWizardDialog::slotFinish()
{
    auto& settings = AppSettings::instance();

    // Save tool paths (page 1)
    if (m_tools.size() >= 5)
    {
        settings.setFfmpegPath(m_tools[0].pathEdit->text());
        // ffprobe (index 1) is auto-detected from ffmpeg path
        settings.setMkvmergePath(m_tools[2].pathEdit->text());
        settings.setMkvextractPath(m_tools[3].pathEdit->text());
        settings.setNugenAmbPath(m_tools[4].pathEdit->text());
    }

    // Save qBittorrent settings (page 2) — only if user didn't skip
    if (m_qbtHostEdit != nullptr)
    {
        settings.setQbittorrentHost(m_qbtHostEdit->text().trimmed());
        settings.setQbittorrentPort(m_qbtPortSpin->value());
        settings.setQbittorrentUser(m_qbtUserEdit->text().trimmed());
        settings.setQbittorrentPassword(m_qbtPasswordEdit->text());
    }

    // Save render preset (page 3) — store it as the first preset
    // The user can change per-template, so this is just informational

    settings.setSetupCompleted(true);
    settings.save();

    accept();
}
