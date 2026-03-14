#include "missingfilescontroller.h"

#include "appsettings.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
#include <QTime>
#include <QUrl>
#include <QVideoSink>

MissingFilesController::MissingFilesController(QObject* parent) : QObject(parent)
{
    // Инициализируем плеер
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer = new QMediaPlayer(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);

    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &MissingFilesController::onPlayerPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &MissingFilesController::onMediaStatusChanged);
}

MissingFilesController::~MissingFilesController()
{
    if (m_mediaPlayer)
    {
        m_mediaPlayer->stop();
    }
}

void MissingFilesController::setupRequest(bool audioReq, bool isWavReq, const QStringList& fonts, bool timeReq,
                                          const QString& timeReason, const QString& videoPath, double durationS)
{
    m_audioReq = audioReq;
    if (isWavReq)
    {
        m_audioPrompt = "Для SRT-мастера требуется несжатый WAV-файл:";
    }
    else
    {
        m_audioPrompt = "Не удалось найти русскую аудиодорожку. Укажите путь к ней:";
    }
    m_audioPath.clear();

    m_missingFonts = fonts;
    m_resolvedFonts.clear();

    m_timeReq = timeReq;
    m_timePrompt = timeReason.isEmpty() ? "Не удалось определить время эндинга. Укажите вручную:" : timeReason;

    m_videoFilePath = videoPath;
    m_videoDurationS = durationS;

    m_currentSliderVal = 0;
    m_currentTimeStr = "0:00:00.000";
    m_initialSeekDone = false;

    // Сбрасываем плеер
    m_mediaPlayer->stop();
    m_mediaPlayer->setSource(QUrl());

    // Если нужно время - настраиваем плеер
    if (timeReq && !m_videoFilePath.isEmpty() && QFileInfo::exists(m_videoFilePath))
    {
        m_fps = detectFps(m_videoFilePath);
        m_frameStepS = 1.0 / m_fps;

        if (m_videoDurationS > 0.0)
        {
            // Начинаем за 3 минуты до конца (обычно там эндинг)
            double initialTimeS = qMax(0.0, m_videoDurationS - 180.0);
            m_currentSliderVal = secondsToSliderValue(initialTimeS);
            updateTimeStrFromSlider();
        }

        // QML сам подхватит этот Source через свойство mediaPlayer
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_videoFilePath));
    }

    // Сообщаем QML, что свойства обновились
    emit statusChanged();
    emit audioPathChanged();
    emit fontsChanged();
    emit timeChanged();
    emit playerStateChanged();

    // Просим QML показать окно
    emit openDialogRequested();
}

QString MissingFilesController::currentTimeStr() const
{
    return m_currentTimeStr;
}

int MissingFilesController::sliderMaximum() const
{
    if (m_videoDurationS <= 0.0)
        return 1000;
    return static_cast<int>(m_videoDurationS * m_fps);
}

int MissingFilesController::currentSliderValue() const
{
    return m_currentSliderVal;
}
bool MissingFilesController::isPlaying() const
{
    return m_mediaPlayer && m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState;
}

void MissingFilesController::setAudioPath(const QString& path)
{
    if (m_audioPath != path)
    {
        m_audioPath = path;
        emit audioPathChanged();
    }
}

void MissingFilesController::setCurrentSliderValue(int value)
{
    if (m_syncInProgress)
        return;
    m_syncInProgress = true;

    m_currentSliderVal = value;
    updateTimeStrFromSlider();

    if (m_mediaPlayer)
    {
        double timeS = sliderValueToSeconds(value);
        qint64 positionMs = static_cast<qint64>(timeS * 1000.0);
        m_mediaPlayer->setPosition(positionMs);
    }

    m_syncInProgress = false;
    emit timeChanged();
}

void MissingFilesController::resolveFont(const QString& fontName, const QString& fontPath)
{
    m_resolvedFonts[fontName] = fontPath;
}

void MissingFilesController::openVideoInExternalPlayer()
{
    if (!m_videoFilePath.isEmpty())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_videoFilePath));
    }
}

void MissingFilesController::togglePlayPause()
{
    if (!m_mediaPlayer)
        return;

    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        m_mediaPlayer->pause();
    }
    else
    {
        m_mediaPlayer->play();
    }
    emit playerStateChanged();
}

void MissingFilesController::nextFrame()
{
    if (!m_mediaPlayer)
        return;
    stopPlayback();

    qint64 currentMs = m_mediaPlayer->position();
    qint64 frameDurationMs = static_cast<qint64>(m_frameStepS * 1000.0);
    qint64 newMs = qMin(currentMs + frameDurationMs, static_cast<qint64>(m_videoDurationS * 1000.0));

    m_mediaPlayer->setPosition(newMs);
}

void MissingFilesController::prevFrame()
{
    if (!m_mediaPlayer)
        return;
    stopPlayback();

    qint64 currentMs = m_mediaPlayer->position();
    qint64 frameDurationMs = static_cast<qint64>(m_frameStepS * 1000.0);
    qint64 newMs = qMax(Q_INT64_C(0), currentMs - frameDurationMs);

    m_mediaPlayer->setPosition(newMs);
}

void MissingFilesController::stopPlayer()
{
    stopPlayback();
}

void MissingFilesController::stopPlayback()
{
    if (m_mediaPlayer && m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        m_mediaPlayer->pause();
        emit playerStateChanged();
    }
}

void MissingFilesController::acceptDialog()
{
    stopPlayer();
    emit dialogFinished(true, m_audioPath, m_resolvedFonts, m_currentTimeStr);
}

void MissingFilesController::rejectDialog()
{
    stopPlayer();
    emit dialogFinished(false, "", QMap<QString, QString>(), "");
}

void MissingFilesController::onPlayerPositionChanged(qint64 position)
{
    if (m_syncInProgress)
        return;
    m_syncInProgress = true;

    double timeS = static_cast<double>(position) / 1000.0;
    m_currentSliderVal = secondsToSliderValue(timeS);
    updateTimeStrFromSlider();

    m_syncInProgress = false;
    emit timeChanged();
}

void MissingFilesController::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (!m_initialSeekDone && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia))
    {
        m_initialSeekDone = true;
        double initialTimeS = sliderValueToSeconds(m_currentSliderVal);
        qint64 positionMs = static_cast<qint64>(initialTimeS * 1000.0);
        m_mediaPlayer->pause();
        m_mediaPlayer->setPosition(positionMs);
        emit playerStateChanged();
    }
}

void MissingFilesController::updateTimeStrFromSlider()
{
    double timeS = sliderValueToSeconds(m_currentSliderVal);
    int totalMs = static_cast<int>(timeS * 1000.0);
    int hours = totalMs / 3600000;
    totalMs %= 3600000;
    int minutes = totalMs / 60000;
    totalMs %= 60000;
    int secs = totalMs / 1000;
    int ms = totalMs % 1000;

    m_currentTimeStr = QString("%1:%2:%3.%4")
                           .arg(hours)
                           .arg(minutes, 2, 10, QChar('0'))
                           .arg(secs, 2, 10, QChar('0'))
                           .arg(ms, 3, 10, QChar('0'));
}

double MissingFilesController::detectFps(const QString& videoPath) const
{
    QString ffprobePath = AppSettings::instance().ffprobePath();
    if (ffprobePath.isEmpty() || !QFileInfo::exists(ffprobePath))
        return 25.0;

    QStringList args;
    args << "-v" << "quiet" << "-select_streams" << "v:0"
         << "-show_entries" << "stream=r_frame_rate"
         << "-of" << "default=noprint_wrappers=1:nokey=1" << videoPath;

    QProcess probe;
    probe.start(ffprobePath, args);
    if (!probe.waitForFinished(3000))
        return 25.0;

    QString output = probe.readAllStandardOutput().trimmed();
    if (output.isEmpty())
        return 25.0;

    double fps = 25.0;
    if (output.contains('/'))
    {
        QStringList parts = output.split('/');
        if (parts.size() == 2)
        {
            bool okNum, okDen;
            double num = parts[0].toDouble(&okNum);
            double den = parts[1].toDouble(&okDen);
            if (okNum && okDen && den > 0.0)
                fps = num / den;
        }
    }
    else
    {
        bool ok;
        double parsed = output.toDouble(&ok);
        if (ok && parsed > 0.0)
            fps = parsed;
    }

    return (fps >= 1.0 && fps <= 120.0) ? fps : 25.0;
}

double MissingFilesController::sliderValueToSeconds(int value) const
{
    return static_cast<double>(value) / m_fps;
}

int MissingFilesController::secondsToSliderValue(double seconds) const
{
    return static_cast<int>(seconds * m_fps);
}

void MissingFilesController::setVideoSink(QObject* sink)
{
    if ((m_mediaPlayer != nullptr) && (sink != nullptr))
    {
        m_mediaPlayer->setVideoOutput(sink);
    }
}