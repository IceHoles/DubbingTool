#ifndef MANUALASSEMBLER_H
#define MANUALASSEMBLER_H

#include "appsettings.h"
#include "iprocessrunner.h"

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class IProcessRunner;

class ManualAssembler : public QObject
{
    Q_OBJECT

public:
    explicit ManualAssembler(const QVariantMap& params, QObject* parent = nullptr,
                             IProcessRunner* processRunner = nullptr);
    ~ManualAssembler() override;
    void start();
    IProcessRunner* getProcessRunner() const
    {
        return m_processRunner;
    }
    IProcessRunner* getProcessManager() const
    {
        return m_processRunner;
    }

public slots:
    void cancelOperation();

signals:
    void logMessage(const QString&, LogCategory, LogLevel = LogLevel::Info);
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
    IProcessRunner* m_processRunner = nullptr;
    bool m_ownsProcessRunner = false;

    enum class Step
    {
        Idle,
        NormalizingAudio,
        ConvertingAudio,
        AssemblingMkv
    };
    Step m_currentStep = Step::Idle;

    QString m_finalMkvPath;
    double m_sourceAudioDurationS = 0.0;
    QTimer* m_progressTimer;
    QString m_progressLogPath;
    QString m_originalAudioPathBeforeNormalization;
    bool m_didLaunchNugen = false;
};

#endif // MANUALASSEMBLER_H
