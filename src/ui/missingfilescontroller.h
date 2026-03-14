#pragma once

#include <QAudioOutput>
#include <QMap>
#include <QMediaPlayer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

class MissingFilesController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // Свойства для отображения в UI
    Q_PROPERTY(bool audioRequired READ audioRequired NOTIFY statusChanged)
    Q_PROPERTY(QString audioPrompt READ audioPrompt NOTIFY statusChanged)
    Q_PROPERTY(QString audioPath READ audioPath WRITE setAudioPath NOTIFY audioPathChanged)

    Q_PROPERTY(bool fontsRequired READ fontsRequired NOTIFY statusChanged)
    Q_PROPERTY(QStringList missingFonts READ missingFonts NOTIFY fontsChanged)

    Q_PROPERTY(bool timeRequired READ timeRequired NOTIFY statusChanged)
    Q_PROPERTY(QString timePrompt READ timePrompt NOTIFY statusChanged)
    Q_PROPERTY(QString currentTimeStr READ currentTimeStr NOTIFY timeChanged)

    // Свойства плеера
    Q_PROPERTY(QMediaPlayer* mediaPlayer READ mediaPlayer CONSTANT)
    Q_PROPERTY(int sliderMaximum READ sliderMaximum NOTIFY playerStateChanged)
    Q_PROPERTY(int currentSliderValue READ currentSliderValue WRITE setCurrentSliderValue NOTIFY timeChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playerStateChanged)

public:
    explicit MissingFilesController(QObject* parent = nullptr);
    ~MissingFilesController();

    // Запуск диалога с параметрами
    void setupRequest(bool audioReq, bool isWavReq, const QStringList& fonts, bool timeReq, const QString& timeReason,
                      const QString& videoPath, double durationS);

    // Геттеры
    bool audioRequired() const
    {
        return m_audioReq;
    }
    QString audioPrompt() const
    {
        return m_audioPrompt;
    }
    QString audioPath() const
    {
        return m_audioPath;
    }

    bool fontsRequired() const
    {
        return !m_missingFonts.isEmpty();
    }
    QStringList missingFonts() const
    {
        return m_missingFonts;
    }
    QMap<QString, QString> resolvedFonts() const
    {
        return m_resolvedFonts;
    }

    bool timeRequired() const
    {
        return m_timeReq;
    }
    QString timePrompt() const
    {
        return m_timePrompt;
    }
    QString currentTimeStr() const;

    QMediaPlayer* mediaPlayer() const
    {
        return m_mediaPlayer;
    }
    int sliderMaximum() const;
    int currentSliderValue() const;
    bool isPlaying() const;

    // Сеттеры
    void setAudioPath(const QString& path);
    void setCurrentSliderValue(int value);

    // Методы для вызова из QML
    Q_INVOKABLE void resolveFont(const QString& fontName, const QString& fontPath);
    Q_INVOKABLE void openVideoInExternalPlayer();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void nextFrame();
    Q_INVOKABLE void prevFrame();
    Q_INVOKABLE void stopPlayer();
    Q_INVOKABLE void setVideoSink(QObject* sink);
    // Ответ пользователю
    Q_INVOKABLE void acceptDialog();
    Q_INVOKABLE void rejectDialog();

signals:
    void statusChanged();
    void audioPathChanged();
    void fontsChanged();
    void timeChanged();
    void playerStateChanged();
    void openDialogRequested(); // Сигнал для QML, чтобы показать окно

    // Сигнал в главный контроллер, что данные получены
    void dialogFinished(bool accepted, const QString& audioPath, const QMap<QString, QString>& fonts,
                        const QString& timeStr);

private slots:
    void onPlayerPositionChanged(qint64 position);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);

private:
    void stopPlayback();
    double detectFps(const QString& videoPath) const;
    double sliderValueToSeconds(int value) const;
    int secondsToSliderValue(double seconds) const;
    void updateTimeStrFromSlider();

    bool m_audioReq = false;
    QString m_audioPrompt;
    QString m_audioPath;

    QStringList m_missingFonts;
    QMap<QString, QString> m_resolvedFonts;

    bool m_timeReq = false;
    QString m_timePrompt;
    QString m_videoFilePath;
    double m_videoDurationS = 0.0;

    QMediaPlayer* m_mediaPlayer = nullptr;
    QAudioOutput* m_audioOutput = nullptr;

    double m_fps = 25.0;
    double m_frameStepS = 1.0 / 25.0;
    bool m_initialSeekDone = false;
    int m_currentSliderVal = 0;
    QString m_currentTimeStr = "0:00:00.000";
    bool m_syncInProgress = false;
};