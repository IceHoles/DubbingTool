#include "missingfilesdialog.h"
#include "ui_missingfilesdialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QUrl>

#include "appsettings.h"


MissingFilesDialog::MissingFilesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MissingFilesDialog)
{
    ui->setupUi(this);
    ui->audioGroupBox->setVisible(false);
    ui->fontsGroupBox->setVisible(false);
    ui->timeGroupBox->setVisible(false);

    // Async FFmpeg process for frame extraction
    m_ffmpegProcess = new QProcess(this);
    connect(m_ffmpegProcess, &QProcess::finished,
            this, &MissingFilesDialog::slotExtractionFinished);
    connect(m_ffmpegProcess, &QProcess::errorOccurred,
            this, &MissingFilesDialog::slotExtractionError);

    // Viewfinder connections
    connect(ui->previewFrameButton, &QPushButton::clicked,
            this, &MissingFilesDialog::slotPreviewFrame);
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

    // Prepare temp path for preview image
    m_previewTempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath("dubbing_tool_tb_preview.jpg");
}

MissingFilesDialog::~MissingFilesDialog()
{
    // Prevent the pipeline from restarting during cleanup
    m_isPlaying = false;
    m_pendingSeekTimeS = -1.0;

    // Disconnect all signals before killing to avoid slotExtractionFinished
    // starting a new process on a half-destroyed object
    m_ffmpegProcess->disconnect();

    if (m_ffmpegProcess->state() != QProcess::NotRunning)
    {
        m_ffmpegProcess->kill();
        m_ffmpegProcess->waitForFinished(1000);
    }

    // Clean up temp preview file
    if (QFileInfo::exists(m_previewTempPath))
    {
        QFile::remove(m_previewTempPath);
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
        QListWidgetItem *item = new QListWidgetItem(name);
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

    // Detect actual frame rate from video (fallback to 25fps)
    bool hasVideo = !m_videoFilePath.isEmpty() && QFileInfo::exists(m_videoFilePath);
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

    // Extract initial frame at the starting position
    if (hasVideo && m_videoDurationS > 0.0)
    {
        double initialTimeS = sliderValueToSeconds(ui->timeSlider->value());
        requestFrameExtraction(initialTimeS);
    }
}

void MissingFilesDialog::setViewfinderEnabled(bool enabled)
{
    ui->previewFrameButton->setEnabled(enabled);
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

void MissingFilesDialog::slotPreviewFrame()
{
    double timeS = sliderValueToSeconds(ui->timeSlider->value());
    requestFrameExtraction(timeS);
}

void MissingFilesDialog::slotOpenInPlayer()
{
    if (!m_videoFilePath.isEmpty())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_videoFilePath));
    }
}

void MissingFilesDialog::slotSliderPressed()
{
    // User started dragging — stop playback
    if (m_isPlaying)
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

    // Pipeline: request extraction at the new position
    // If extraction is already running, this queues the latest position
    requestFrameExtraction(sliderValueToSeconds(value));
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

    // Request extraction at the new time
    QTime t = ui->timeEdit->time();
    double timeS = t.hour() * 3600.0 + t.minute() * 60.0 + t.second() + t.msec() / 1000.0;
    requestFrameExtraction(timeS);
}

// =============================================================================
// Transport controls
// =============================================================================

void MissingFilesDialog::slotPrevFrame()
{
    if (m_isPlaying)
    {
        stopPlayback();
    }

    double currentTimeS = sliderValueToSeconds(ui->timeSlider->value());
    double newTimeS = currentTimeS - m_frameStepS;
    if (newTimeS < 0.0)
    {
        newTimeS = 0.0;
    }

    m_syncInProgress = true;
    int newSlider = secondsToSliderValue(newTimeS);
    ui->timeSlider->setValue(newSlider);
    syncTimeEditFromSlider(newSlider);
    m_syncInProgress = false;

    requestFrameExtraction(newTimeS);
}

void MissingFilesDialog::slotNextFrame()
{
    if (m_isPlaying)
    {
        stopPlayback();
    }

    double currentTimeS = sliderValueToSeconds(ui->timeSlider->value());
    double newTimeS = currentTimeS + m_frameStepS;
    if (newTimeS > m_videoDurationS)
    {
        newTimeS = m_videoDurationS;
    }

    m_syncInProgress = true;
    int newSlider = secondsToSliderValue(newTimeS);
    ui->timeSlider->setValue(newSlider);
    syncTimeEditFromSlider(newSlider);
    m_syncInProgress = false;

    requestFrameExtraction(newTimeS);
}

void MissingFilesDialog::slotPlayPause()
{
    if (m_isPlaying)
    {
        stopPlayback();
    }
    else
    {
        m_isPlaying = true;
        ui->playPauseButton->setText(QString::fromUtf8("⏸"));
        ui->playPauseButton->setToolTip("Пауза");

        // Start playing from current position, track time precisely
        m_playbackTimeS = sliderValueToSeconds(ui->timeSlider->value());
        requestFrameExtraction(m_playbackTimeS);
    }
}

void MissingFilesDialog::stopPlayback()
{
    m_isPlaying = false;
    ui->playPauseButton->setText(QString::fromUtf8("▶"));
    ui->playPauseButton->setToolTip("Воспроизведение");
}

void MissingFilesDialog::advancePlayback()
{
    // Use precise playback time tracker (not slider value) to avoid rounding drift
    m_playbackTimeS += m_frameStepS;

    if (m_playbackTimeS > m_videoDurationS)
    {
        stopPlayback();
        return;
    }

    // Update slider and time edit to reflect new position
    m_syncInProgress = true;
    int nextSlider = secondsToSliderValue(m_playbackTimeS);
    ui->timeSlider->setValue(nextSlider);
    syncTimeEditFromSlider(nextSlider);
    m_syncInProgress = false;

    startExtraction(m_playbackTimeS);
}

// =============================================================================
// Async frame extraction pipeline
// =============================================================================

void MissingFilesDialog::requestFrameExtraction(double timeS)
{
    if (m_extractionInProgress)
    {
        // Extraction running — queue the latest position (overwrites previous pending)
        m_pendingSeekTimeS = timeS;
        return;
    }
    startExtraction(timeS);
}

void MissingFilesDialog::startExtraction(double timeS)
{
    QString ffmpegPath = AppSettings::instance().ffmpegPath();
    if (ffmpegPath.isEmpty() || !QFileInfo::exists(ffmpegPath))
    {
        ui->previewImageLabel->setText("FFmpeg не найден, превью недоступно");
        return;
    }

    m_extractionInProgress = true;
    m_pendingSeekTimeS = -1.0;

    QString timeStr = formatTime(timeS);

    QStringList args;
    args << "-ss" << timeStr
         << "-i" << m_videoFilePath
         << "-frames:v" << "1"
         << "-q:v" << "2"
         << "-f" << "image2"
         << "-y"
         << m_previewTempPath;

    m_ffmpegProcess->start(ffmpegPath, args);
}

void MissingFilesDialog::slotExtractionFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_extractionInProgress = false;

    if (exitStatus == QProcess::NormalExit && exitCode == 0)
    {
        showExtractedFrame();
    }

    // Defer next extraction to the next event loop iteration.
    // Restarting QProcess from within its own finished() handler
    // can fail silently on Windows if the process handle isn't fully released yet.
    if (m_pendingSeekTimeS >= 0.0)
    {
        double pending = m_pendingSeekTimeS;
        QTimer::singleShot(0, this, [this, pending]() {
            startExtraction(pending);
        });
        return;
    }

    if (m_isPlaying)
    {
        QTimer::singleShot(0, this, [this]() {
            if (m_isPlaying)
            {
                advancePlayback();
            }
        });
    }
}

void MissingFilesDialog::slotExtractionError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart)
    {
        m_extractionInProgress = false;
        m_pendingSeekTimeS = -1.0;
        ui->previewImageLabel->setText("Не удалось запустить FFmpeg");

        if (m_isPlaying)
        {
            stopPlayback();
        }
    }
    // Other errors (Crashed, Timedout) are followed by finished() signal
}

void MissingFilesDialog::showExtractedFrame()
{
    QPixmap pixmap(m_previewTempPath);
    if (pixmap.isNull())
    {
        return;
    }

    ui->previewImageLabel->setPixmap(
        pixmap.scaled(ui->previewImageLabel->size(),
                      Qt::KeepAspectRatio,
                      Qt::SmoothTransformation));
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

    // Parse fraction: "24000/1001" → 23.976, "25/1" → 25.0
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
