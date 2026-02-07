#ifndef MISSINGFILESDIALOG_H
#define MISSINGFILESDIALOG_H

#include <QDialog>
#include <QMap>
#include <QListWidgetItem>
#include <QProcess>

class QTimer;

namespace Ui {
class MissingFilesDialog;
}

/**
 * @brief Dialog for requesting missing data from user, including TB time viewfinder
 *
 * Contains an async frame extraction pipeline: when the user drags the slider,
 * requests are queued and processed as fast as FFmpeg can extract frames.
 * Play mode advances frame-by-frame at the extraction rate.
 */
class MissingFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MissingFilesDialog(QWidget *parent = nullptr);
    ~MissingFilesDialog();

    void setAudioPathVisible(bool visible);
    void setMissingFonts(const QStringList &fontNames);
    void setTimeInputVisible(bool visible);
    void setAudioPrompt(const QString &text);
    void setTimePrompt(const QString &text);

    /**
     * @brief Configure the viewfinder with video file and duration
     * @param videoPath Path to the source video file
     * @param durationS Duration of the video in seconds
     */
    void setVideoFile(const QString &videoPath, double durationS);

    QString getAudioPath() const;
    QMap<QString, QString> getResolvedFonts() const;
    QString getTime() const;

private slots:
    void on_browseAudioButton_clicked();
    void on_fontsListWidget_itemDoubleClicked(QListWidgetItem *item);
    void slotPreviewFrame();
    void slotOpenInPlayer();
    void slotSliderValueChanged(int value);
    void slotSliderPressed();
    void slotTimeEditChanged();
    void slotNextFrame();
    void slotPrevFrame();
    void slotPlayPause();
    void slotExtractionFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void slotExtractionError(QProcess::ProcessError error);

private:
    void requestFrameExtraction(double timeS);
    void startExtraction(double timeS);
    void showExtractedFrame();
    void stopPlayback();
    void advancePlayback();
    double detectFps() const;
    QString formatTime(double timeS) const;
    void syncSliderFromTimeEdit();
    void syncTimeEditFromSlider(int sliderValue);
    double sliderValueToSeconds(int value) const;
    int secondsToSliderValue(double seconds) const;
    void setViewfinderEnabled(bool enabled);

    Ui::MissingFilesDialog *ui;
    QMap<QString, QString> m_resolvedFonts;
    QString m_videoFilePath;
    double m_videoDurationS = 0.0;
    QString m_previewTempPath;
    bool m_syncInProgress = false;

    // Async extraction pipeline
    QProcess *m_ffmpegProcess = nullptr;
    bool m_extractionInProgress = false;
    double m_pendingSeekTimeS = -1.0; // -1 means no pending request

    // Play mode
    bool m_isPlaying = false;
    double m_playbackTimeS = 0.0; // Precise time tracker for play mode (avoids slider rounding)

    // Frame rate (detected from video via ffprobe, fallback 25fps)
    double m_fps = 25.0;
    double m_frameStepS = 1.0 / 25.0;

    static constexpr double kDefaultFps = 25.0;
    static constexpr double kMinFps = 1.0;
    static constexpr double kMaxFps = 120.0;
};

#endif // MISSINGFILESDIALOG_H
