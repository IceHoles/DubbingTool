#ifndef MANUALASSEMBLER_H
#define MANUALASSEMBLER_H

#include <QObject>
#include <QVariantMap>
#include <QProcess>
#include <QTimer>
#include "appsettings.h"


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

public slots:
    void cancelOperation();

signals:
    void logMessage(const QString&, LogCategory);
    void progressUpdated(int percentage, const QString& stageName = "");
    void finished();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString &output);
    void onConversionProgress();

private:
    void normalizeAudio();
    void convertAudio();
    void processSubtitlesAndAssemble();
    void assemble();

    QVariantMap m_params;
    ProcessManager *m_processManager;
    AssProcessor *m_assProcessor;

    enum class Step { Idle, NormalizingAudio, ConvertingAudio, AssemblingMkv };
    Step m_currentStep = Step::Idle;

    QString m_ffmpegPath;
    QString m_finalMkvPath;
    double m_sourceAudioDurationS = 0.0;
    QTimer* m_progressTimer;
    QString m_progressLogPath;
    QString m_originalAudioPathBeforeNormalization;
    bool m_didLaunchNugen = false;
};

#endif // MANUALASSEMBLER_H
