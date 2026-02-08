#ifndef MISSINGFILESDIALOG_H
#define MISSINGFILESDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
#include <QMap>
#include <QMediaPlayer>

class QAudioOutput;
class QVideoWidget;

namespace Ui
{
class MissingFilesDialog;
} // namespace Ui

/**
 * @brief Dialog for requesting missing data from user, including TB time viewfinder
 *
 * Uses QMediaPlayer with QVideoWidget for real-time video preview with audio.
 * Frame stepping is achieved via setPosition(current +/- frameDurationMs).
 * The FFmpeg backend (default in Qt 6.5+) provides full MKV/codec support.
 */
class MissingFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MissingFilesDialog(QWidget* parent = nullptr);
    ~MissingFilesDialog();

    void setAudioPathVisible(bool visible);
    void setMissingFonts(const QStringList& fontNames);
    void setTimeInputVisible(bool visible);
    void setAudioPrompt(const QString& text);
    void setTimePrompt(const QString& text);

    /**
     * @brief Configure the viewfinder with video file and duration
     * @param videoPath Path to the source video file
     * @param durationS Duration of the video in seconds
     */
    void setVideoFile(const QString& videoPath, double durationS);

    [[nodiscard]] QString getAudioPath() const;
    [[nodiscard]] QMap<QString, QString> getResolvedFonts() const;
    [[nodiscard]] QString getTime() const;

private slots:
    void on_browseAudioButton_clicked();
    void on_fontsListWidget_itemDoubleClicked(QListWidgetItem* item);
    void slotOpenInPlayer();
    void slotSliderValueChanged(int value);
    void slotSliderPressed();
    void slotTimeEditChanged();
    void slotNextFrame();
    void slotPrevFrame();
    void slotPlayPause();
    void slotPlayerPositionChanged(qint64 position);
    void slotMediaStatusChanged(QMediaPlayer::MediaStatus status);

private:
    void stopPlayback();
    double detectFps() const;
    [[nodiscard]] QString formatTime(double timeS) const;
    void syncSliderFromTimeEdit();
    void syncTimeEditFromSlider(int sliderValue);
    [[nodiscard]] double sliderValueToSeconds(int value) const;
    [[nodiscard]] int secondsToSliderValue(double seconds) const;
    void setViewfinderEnabled(bool enabled);

    Ui::MissingFilesDialog* ui;
    QMap<QString, QString> m_resolvedFonts;
    QString m_videoFilePath;
    double m_videoDurationS = 0.0;
    bool m_syncInProgress = false;

    // Qt Multimedia player
    QMediaPlayer* m_mediaPlayer = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    // Frame rate (detected from video via ffprobe, fallback 25fps)
    double m_fps = 25.0;
    double m_frameStepS = 1.0 / 25.0;
    bool m_initialSeekDone = false;

    static constexpr double kDefaultFps = 25.0;
    static constexpr double kMinFps = 1.0;
    static constexpr double kMaxFps = 120.0;
};

#endif // MISSINGFILESDIALOG_H
