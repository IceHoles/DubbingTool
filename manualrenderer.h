#ifndef MANUALRENDERER_H
#define MANUALRENDERER_H

#include <QObject>
#include <QVariantMap>
#include <QProcess>
#include <QDir>
#include "appsettings.h"
#include "renderhelper.h"


class ProcessManager;

static QString escapePathForFfmpegFilter(const QString& path)
{
    QString escaped = QDir::toNativeSeparators(path);
    escaped.replace('\\', "/");
    escaped.replace(':', "\\:");
    return escaped;
}

class ManualRenderer : public QObject
{
    Q_OBJECT
public:
    explicit ManualRenderer(const QVariantMap &params, QObject *parent = nullptr);
    ~ManualRenderer();
    void start();
    ProcessManager* getProcessManager() const;

public slots:
    void cancelOperation();

signals:
    void logMessage(const QString&, LogCategory);
    void finished();
    void progressUpdated(int percentage);
    void bitrateCheckRequest(const RenderPreset &preset, double actualBitrate);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString &output);
    void onBitrateCheckFinished(RerenderDecision decision, const RenderPreset &newPreset);

private:
    enum class Step { Idle, Pass1, Pass2 };
    void runPass(Step pass);
    QStringList prepareCommandArguments(const QString& commandTemplate);

    Step m_currentStep = Step::Idle;
    QVariantMap m_params;
    ProcessManager *m_processManager;
    qint64 m_sourceDurationS = 0;
    RenderPreset m_preset;
};

#endif // MANUALRENDERER_H
