#ifndef CONCATTBRENDERER_H
#define CONCATTBRENDERER_H

#include "appsettings.h"
#include "processmanager.h"

#include <QObject>
#include <QString>

struct TbSegment
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    bool isValid() const { return endSeconds > startSeconds; }
};

class ConcatTbRenderer : public QObject
{
    Q_OBJECT

public:
    ConcatTbRenderer(const QString& inputMkvPath, const QString& outputMp4Path, const TbSegment& segment,
                     qint64 sourceDurationS, const QString& videoCodecExtension, const QString& hardsubMode,
                     int subtitleTrackIndex, const QString& externalSubsPath, int videoBitrateKbps,
                     const QString& videoFrameRate, const QString& videoAvgFrameRate, bool videoIsCfr,
                     bool reencodeAudioAac256, ProcessManager* processManager,
                     QObject* parent = nullptr);

    void start();

signals:
    void logMessage(const QString&, LogCategory);
    void progressUpdated(int percentage, const QString& stageName);
    void finished();

private:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void failAndFinish(const QString& message);
    void cleanupTempFiles(bool removeSegments);

    void renderMp4Concat();
    void concatFindKeyframe();
    void concatCutSegment1();
    void concatRenderSegment2();
    void concatCutSegment3();
    void concatJoinSegments();

    QString concatEncoderForCodec(const QString& extension) const;
    QString buildSubtitleFilter() const;
    void runFfmpegAsync(const QStringList& args, const QString& errorMessageForStep);

    QString m_inputMkvPath;
    QString m_outputMp4Path;
    TbSegment m_segment;
    qint64 m_sourceDurationS = 0;

    ProcessManager* m_processManager = nullptr;
    QString m_ffmpegPath;
    QString m_ffprobePath;
    QString m_resultPath;
    QString m_videoCodecExtension;
    QString m_hardsubMode;
    int m_subtitleTrackIndex = -1;
    QString m_externalSubsPath;
    int m_videoBitrateKbps = -1;
    QString m_videoFrameRate;
    QString m_videoAvgFrameRate;
    bool m_videoIsCfr = false;
    bool m_reencodeAudioAac256 = true;
    QString m_tempFilterSubsPath;
    QString m_pendingStepErrorMessage;
    bool m_isRunningAsyncStep = false;
    bool m_connectionsInitialized = false;

    enum class Step
    {
        Idle,
        CutSegment1,
        RenderSegment2,
        CutSegment3,
        JoinSegments
    };
    Step m_currentStep = Step::Idle;

    double m_concatTbStartSeconds = 0.0;
    double m_concatTbEndSeconds = 0.0;
    int m_concatSegmentCount = 0;
    double m_concatKfBeforeTbStart = 0.0;
    double m_concatKeyframeTime = 0.0;
    double m_lastSeg3StartBase = 0.0;
    double m_lastSeg3StartChosen = 0.0;
};

#endif // CONCATTBRENDERER_H

