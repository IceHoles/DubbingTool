#ifndef MANUALASSEMBLER_H
#define MANUALASSEMBLER_H

#include "appsettings.h"

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class ProcessManager;
class AssProcessor;
class ReleaseTemplate;

class ManualAssembler : public QObject
{
    Q_OBJECT

public:
    explicit ManualAssembler(const QVariantMap& params, QObject* parent = nullptr);
    ~ManualAssembler();
    void start();
    ProcessManager* getProcessManager() const;

public slots:
    void cancelOperation();

signals:
    void logMessage(const QString&, LogCategory);
    void progressUpdated(int percentage, const QString& stageName = "");
    /// Emitted when the worker stops. \a success is true only after MKV was built successfully.
    void finished(bool success);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessText(const QString& output);
    void onConversionProgress();

private:
    void normalizeAudio();
    void convertAudio();
    void processSubtitlesAndAssemble();
    QString resolveChaptersPathForMkvMerge();
    void assemble();

    QVariantMap m_params;
    ProcessManager* m_processManager;
    AssProcessor* m_assProcessor;

    enum class Step
    {
        Idle,
        NormalizingAudio,
        ConvertingAudio,
        AssemblingMkv
    };
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
