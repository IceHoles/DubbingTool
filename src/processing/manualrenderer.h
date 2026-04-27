#ifndef MANUALRENDERER_H
#define MANUALRENDERER_H

#include "appsettings.h"
#include "chapterhelper.h"
#include "concattbrenderer.h"
#include "renderhelper.h"

#include <QDir>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

class ProcessManager;
class ConcatTbRenderer;

enum class RenderState
{
    Init,
    VideoPass1,
    VideoPass2,
    AudioPass,
    MuxMP4Box
};

static QString escapePathForFfmpegFilter(const QString& path)
{
    QString escaped = QDir::toNativeSeparators(path);
    escaped.replace("\\", "/");
    // "C\:/path"
    escaped.replace(":", "\\:");
    // Docs: "Inside a single-quoted string, a single quote can be included by escaping it with a backslash."
    escaped.replace("'", "\\'");
    return escaped;
}

class ManualRenderer : public QObject
{
    Q_OBJECT

public:
    explicit ManualRenderer(const QVariantMap& params, QObject* parent = nullptr);
    ~ManualRenderer();

    void start();
    ProcessManager* getProcessManager() const;

public slots:
    void cancelOperation();

signals:
    void logMessage(const QString&, LogCategory, LogLevel = LogLevel::Info);
    void finished();
    void progressUpdated(int percentage, const QString& stageName = "");
    void bitrateCheckRequest(const RenderPreset& preset, double actualBitrate);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString& output);
    void onBitrateCheckFinished(RerenderDecision decision, const RenderPreset& newPreset);

private:
    void runStep();
    bool parsePreset(const QString& commandTemplate, QStringList& outVideoArgs, QStringList& outAudioArgs);
    void applyChaptersIfNeeded();
    void cleanupTempFiles();
    QList<ChapterMarker> prepareChapters();

    QVariantMap m_params;
    ProcessManager* m_processManager = nullptr;
    ConcatTbRenderer* m_concatRenderer = nullptr;
    RenderPreset m_preset;

    RenderState m_currentState = RenderState::Init;
    qint64 m_sourceDurationS = 0;
    QString m_tempConcatSubsPath;

    QString m_actualInputMkv;
    QString m_tempVideoMp4;
    QString m_tempAudioM4a;
    QString m_tempChaptersTxt;
    QString m_tempMuxedMp4;
    QString m_finalOutputMp4;

    QStringList m_currentVideoArgs;
    QStringList m_currentAudioArgs;
};

#endif // MANUALRENDERER_H
