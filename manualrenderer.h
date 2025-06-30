#ifndef MANUALRENDERER_H
#define MANUALRENDERER_H

#include <QObject>
#include <QVariantMap>
#include <QProcess>

class ProcessManager;

class ManualRenderer : public QObject
{
    Q_OBJECT
public:
    explicit ManualRenderer(const QVariantMap &params, QObject *parent = nullptr);
    ~ManualRenderer();
    void start();
    ProcessManager* getProcessManager() const;

signals:
    void logMessage(const QString &message);
    void finished();
    void progressUpdated(int percentage);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString &output);

private:
    QVariantMap m_params;
    ProcessManager *m_processManager;
    qint64 m_sourceDurationS = 0;
    QString m_renderPreset;
};

#endif // MANUALRENDERER_H
