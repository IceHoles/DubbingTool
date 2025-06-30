#ifndef MANUALASSEMBLER_H
#define MANUALASSEMBLER_H

#include <QObject>
#include <QVariantMap>
#include <QProcess> // <<< ИСПРАВЛЕНИЕ: Добавляем #include для QProcess

class ProcessManager;
class AssProcessor;
class ReleaseTemplate;

class ManualAssembler : public QObject
{
    Q_OBJECT
public:
    explicit ManualAssembler(const QVariantMap &params, QObject *parent = nullptr);
    ~ManualAssembler();
    void start();
    ProcessManager* getProcessManager() const;

signals:
    void logMessage(const QString &message);
    void progressUpdated(int percentage, const QString& stageName = "");
    void finished();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString &output);

private:
    void assemble(const ReleaseTemplate &t);

    QVariantMap m_params;
    ProcessManager *m_processManager;
    AssProcessor *m_assProcessor;

    enum class Step { Idle, AssemblingMkv, RenderingMp4 };
    Step m_currentStep = Step::Idle;

    QString m_ffmpegPath;
    QString m_finalMkvPath;
};

#endif // MANUALASSEMBLER_H
