#include "missingfilesdialog.h"
#include "ui_missingfilesdialog.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QProcess>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

#include "appsettings.h"


MissingFilesDialog::MissingFilesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MissingFilesDialog)
{
    ui->setupUi(this);
    ui->audioGroupBox->setVisible(false);
    ui->fontsGroupBox->setVisible(false);
    ui->timeGroupBox->setVisible(false);

    // Prepare the container layout for the video widget (widget added later in setVideoFile)
    auto *containerLayout = new QVBoxLayout(ui->videoContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    // Viewfinder connections
    connect(ui->openInPlayerButton, &QPushButton::clicked,
            this, &MissingFilesDialog::slotOpenInPlayer);
    connect(ui->timeSlider, &QSlider::valueChanged,
            this, &MissingFilesDialog::slotSliderValueChanged);
    connect(ui->timeSlider, &QSlider::sliderPressed,
            this, &MissingFilesDialog::slotSliderPressed);
    connect(ui->timeEdit, &QTimeEdit::timeChanged,
            this, &MissingFilesDialog::slotTimeEditChanged);

    // Transport controls
    connect(ui->prevFrameButton, &QPushButton::clicked,
            this, &MissingFilesDialog::slotPrevFrame);
    connect(ui->playPauseButton, &QPushButton::clicked,
            this, &MissingFilesDialog::slotPlayPause);
    connect(ui->nextFrameButton, &QPushButton::clicked,
            this, &MissingFilesDialog::slotNextFrame);
}

MissingFilesDialog::~MissingFilesDialog()
{
    if (m_mediaPlayer != nullptr)
    {
        m_mediaPlayer->stop();
    }
    delete ui;
}

void MissingFilesDialog::setAudioPathVisible(bool visible)
{
    ui->audioGroupBox->setVisible(visible);
}

void MissingFilesDialog::setMissingFonts(const QStringList &fontNames)
{
    if (fontNames.isEmpty())
    {
        ui->fontsGroupBox->setVisible(false);
        return;
    }

    ui->fontsGroupBox->setVisible(true);
    ui->fontsListWidget->clear();
    for (const QString &name : fontNames)
    {
        auto *item = new QListWidgetItem(name);
        item->setForeground(Qt::red);
        ui->fontsListWidget->addItem(item);
    }
}

QString MissingFilesDialog::getAudioPath() const
{
    return ui->audioPathEdit->text();
}

QMap<QString, QString> MissingFilesDialog::getResolvedFonts() const
{
    return m_resolvedFonts;
}

void MissingFilesDialog::setTimeInputVisible(bool visible)
{
    ui->timeGroupBox->setVisible(visible);
}

QString MissingFilesDialog::getTime() const
{
    return ui->timeEdit->time().toString("H:mm:ss.zzz");
}

void MissingFilesDialog::setVideoFile(const QString &videoPath, double durationS)
{
    m_videoFilePath = videoPath;
    m_videoDurationS = durationS;

    bool hasVideo = !m_videoFilePath.isEmpty() && QFileInfo::exists(m_videoFilePath);

    // Detect actual frame rate from video (fallback to 25fps)
    if (hasVideo)
    {
        m_fps = detectFps();
        m_frameStepS = 1.0 / m_fps;
    }

    if (m_videoDurationS > 0.0)
    {
        // Slider range: 1 tick = 1 frame at the detected fps
        int maxSlider = static_cast<int>(m_videoDurationS * m_fps);
        ui->timeSlider->setMaximum(maxSlider);
        ui->timeSlider->setSingleStep(1);    // 1 frame
        ui->timeSlider->setPageStep(static_cast<int>(m_fps * 10.0)); // 10 seconds

        // Set initial position to ~last 3 minutes (most likely TB location)
        double initialTimeS = m_videoDurationS - 180.0;
        if (initialTimeS < 0.0)
        {
            initialTimeS = 0.0;
        }
        m_syncInProgress = true;
        int initialSlider = secondsToSliderValue(initialTimeS);
        ui->timeSlider->setValue(initialSlider);
        syncTimeEditFromSlider(initialSlider);
        m_syncInProgress = false;
    }

    // Enable/disable viewfinder controls
    setViewfinderEnabled(hasVideo);

    // Create and initialize the media player lazily — the video container
    // must be visible before QVideoWidget can initialize its rendering surface
    if (hasVideo && m_mediaPlayer == nullptr)
    {
        m_audioOutput = new QAudioOutput(this);
        m_mediaPlayer = new QMediaPlayer(this);
        m_videoWidget = new QVideoWidget();
        m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);

        m_mediaPlayer->setAudioOutput(m_audioOutput);
        m_mediaPlayer->setVideoOutput(m_videoWidget);

        // Add QVideoWidget to the container (which is now visible)
        ui->videoContainer->layout()->addWidget(m_videoWidget);

        connect(m_mediaPlayer, &QMediaPlayer::positionChanged,
                this, &MissingFilesDialog::slotPlayerPositionChanged);
        connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged,
                this, &MissingFilesDialog::slotMediaStatusChanged);
    }

    // Load video into QMediaPlayer
    if (hasVideo && m_mediaPlayer != nullptr)
    {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_videoFilePath));
    }
}

void MissingFilesDialog::setViewfinderEnabled(bool enabled)
{
    ui->openInPlayerButton->setEnabled(enabled);
    ui->prevFrameButton->setEnabled(enabled);
    ui->playPauseButton->setEnabled(enabled);
    ui->nextFrameButton->setEnabled(enabled);
    ui->timeSlider->setEnabled(enabled && m_videoDurationS > 0.0);
}

void MissingFilesDialog::on_browseAudioButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "Выберите аудиофайл", "",
        "Аудиофайлы (*.wav *.flac *.aac *.eac3)");
    if (!filePath.isEmpty())
    {
        ui->audioPathEdit->setText(filePath);
    }
}

void MissingFilesDialog::on_fontsListWidget_itemDoubleClicked(QListWidgetItem *item)
{
    QString fontName = item->text();
    QString path = QFileDialog::getOpenFileName(
        this, "Выберите файл для шрифта '" + fontName + "'", "",
        "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");
    if (!path.isEmpty())
    {
        item->setText(fontName + " -> " + path);
        item->setForeground(Qt::darkGreen);
        m_resolvedFonts[fontName] = path;
    }
}

void MissingFilesDialog::setAudioPrompt(const QString &text)
{
    ui->audioLabel->setText(text);
}

void MissingFilesDialog::setTimePrompt(const QString &text)
{
    ui->timeLabel->setText(text);
}

// =============================================================================
// Viewfinder slots
// =============================================================================

void MissingFilesDialog::slotOpenInPlayer()
{
    if (!m_videoFilePath.isEmpty())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_videoFilePath));
    }
}

void MissingFilesDialog::slotSliderPressed()
{
    // User started dragging — pause playback
    if (m_mediaPlayer != nullptr
        && m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        stopPlayback();
    }
}

void MissingFilesDialog::slotSliderValueChanged(int value)
{
    if (m_syncInProgress)
    {
        return;
    }
    m_syncInProgress = true;
    syncTimeEditFromSlider(value);
    m_syncInProgress = false;

    // Seek the player to the new position
    if (m_mediaPlayer != nullptr)
    {
        double timeS = sliderValueToSeconds(value);
        qint64 positionMs = static_cast<qint64>(timeS * 1000.0);
        m_mediaPlayer->setPosition(positionMs);
    }
}

void MissingFilesDialog::slotTimeEditChanged()
{
    if (m_syncInProgress)
    {
        return;
    }
    m_syncInProgress = true;
    syncSliderFromTimeEdit();
    m_syncInProgress = false;

    // Seek the player to the new time
    if (m_mediaPlayer != nullptr)
    {
        QTime t = ui->timeEdit->time();
        double timeS = t.hour() * 3600.0 + t.minute() * 60.0 + t.second() + t.msec() / 1000.0;
        qint64 positionMs = static_cast<qint64>(timeS * 1000.0);
        m_mediaPlayer->setPosition(positionMs);
    }
}

// =============================================================================
// Transport controls
// =============================================================================

void MissingFilesDialog::slotPrevFrame()
{
    if (m_mediaPlayer == nullptr)
    {
        return;
    }

    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        stopPlayback();
    }

    qint64 currentMs = m_mediaPlayer->position();
    qint64 frameDurationMs = static_cast<qint64>(m_frameStepS * 1000.0);
    qint64 newMs = currentMs - frameDurationMs;
    if (newMs < 0)
    {
        newMs = 0;
    }

    m_mediaPlayer->setPosition(newMs);
}

void MissingFilesDialog::slotNextFrame()
{
    if (m_mediaPlayer == nullptr)
    {
        return;
    }

    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        stopPlayback();
    }

    qint64 currentMs = m_mediaPlayer->position();
    qint64 frameDurationMs = static_cast<qint64>(m_frameStepS * 1000.0);
    qint64 newMs = currentMs + frameDurationMs;
    qint64 durationMs = static_cast<qint64>(m_videoDurationS * 1000.0);
    if (newMs > durationMs)
    {
        newMs = durationMs;
    }

    m_mediaPlayer->setPosition(newMs);
}

void MissingFilesDialog::slotPlayPause()
{
    if (m_mediaPlayer == nullptr)
    {
        return;
    }

    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        stopPlayback();
    }
    else
    {
        m_mediaPlayer->play();
        ui->playPauseButton->setText(QString::fromUtf8("\u23F8"));
        ui->playPauseButton->setToolTip("Пауза");
    }
}

void MissingFilesDialog::stopPlayback()
{
    if (m_mediaPlayer != nullptr)
    {
        m_mediaPlayer->pause();
    }
    ui->playPauseButton->setText(QString::fromUtf8("\u25B6"));
    ui->playPauseButton->setToolTip("Воспроизведение");
}

// =============================================================================
// Player feedback
// =============================================================================

void MissingFilesDialog::slotPlayerPositionChanged(qint64 position)
{
    if (m_syncInProgress)
    {
        return;
    }

    double timeS = static_cast<double>(position) / 1000.0;
    int sliderValue = secondsToSliderValue(timeS);

    m_syncInProgress = true;
    ui->timeSlider->setValue(sliderValue);
    syncTimeEditFromSlider(sliderValue);
    m_syncInProgress = false;
}

void MissingFilesDialog::slotMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (!m_initialSeekDone
        && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia))
    {
        m_initialSeekDone = true;

        // Media is ready — seek to initial position and pause
        double initialTimeS = sliderValueToSeconds(ui->timeSlider->value());
        qint64 positionMs = static_cast<qint64>(initialTimeS * 1000.0);
        m_mediaPlayer->pause();
        m_mediaPlayer->setPosition(positionMs);
    }
}

// =============================================================================
// Helpers
// =============================================================================

double MissingFilesDialog::detectFps() const
{
    QString ffprobePath = AppSettings::instance().ffprobePath();
    if (ffprobePath.isEmpty() || !QFileInfo::exists(ffprobePath))
    {
        return kDefaultFps;
    }

    // ffprobe -v quiet -select_streams v:0 -show_entries stream=r_frame_rate
    //         -of default=noprint_wrappers=1:nokey=1 <video>
    // Output example: "24000/1001" or "25/1" or "30/1"
    QStringList args;
    args << "-v" << "quiet"
         << "-select_streams" << "v:0"
         << "-show_entries" << "stream=r_frame_rate"
         << "-of" << "default=noprint_wrappers=1:nokey=1"
         << m_videoFilePath;

    QProcess probe;
    probe.start(ffprobePath, args);
    if (!probe.waitForFinished(3000))
    {
        return kDefaultFps;
    }

    QString output = probe.readAllStandardOutput().trimmed();
    if (output.isEmpty())
    {
        return kDefaultFps;
    }

    // Parse fraction: "24000/1001" -> 23.976, "25/1" -> 25.0
    double fps = kDefaultFps;
    if (output.contains('/'))
    {
        QStringList parts = output.split('/');
        if (parts.size() == 2)
        {
            bool okNum = false;
            bool okDen = false;
            double numerator = parts[0].toDouble(&okNum);
            double denominator = parts[1].toDouble(&okDen);
            if (okNum && okDen && denominator > 0.0)
            {
                fps = numerator / denominator;
            }
        }
    }
    else
    {
        bool ok = false;
        double parsed = output.toDouble(&ok);
        if (ok && parsed > 0.0)
        {
            fps = parsed;
        }
    }

    // Clamp to reasonable range
    if (fps < kMinFps || fps > kMaxFps)
    {
        fps = kDefaultFps;
    }

    return fps;
}

QString MissingFilesDialog::formatTime(double timeS) const
{
    int hours = static_cast<int>(timeS) / 3600;
    int minutes = (static_cast<int>(timeS) % 3600) / 60;
    double seconds = timeS - hours * 3600 - minutes * 60;
    return QString("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 6, 'f', 3, QChar('0'));
}

void MissingFilesDialog::syncSliderFromTimeEdit()
{
    QTime t = ui->timeEdit->time();
    double timeS = t.hour() * 3600.0 + t.minute() * 60.0 + t.second() + t.msec() / 1000.0;
    int sliderVal = secondsToSliderValue(timeS);
    ui->timeSlider->setValue(sliderVal);
}

void MissingFilesDialog::syncTimeEditFromSlider(int sliderValue)
{
    double timeS = sliderValueToSeconds(sliderValue);
    int totalMs = static_cast<int>(timeS * 1000.0);
    int hours = totalMs / 3600000;
    totalMs %= 3600000;
    int minutes = totalMs / 60000;
    totalMs %= 60000;
    int secs = totalMs / 1000;
    int ms = totalMs % 1000;
    ui->timeEdit->setTime(QTime(hours, minutes, secs, ms));
}

double MissingFilesDialog::sliderValueToSeconds(int value) const
{
    return static_cast<double>(value) / m_fps;
}

int MissingFilesDialog::secondsToSliderValue(double seconds) const
{
    return static_cast<int>(seconds * m_fps);
}
