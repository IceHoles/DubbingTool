#ifndef IPROCESSRUNNER_H
#define IPROCESSRUNNER_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

/**
 * @brief Abstraction for starting external tools (ffmpeg, mkvmerge, …).
 *
 * Default implementation is ProcessManager. Tests use FakeProcessRunner to
 * avoid real processes and to assert on command sequences.
 */
class IProcessRunner : public QObject
{
    Q_OBJECT

public:
    explicit IProcessRunner(QObject* parent = nullptr);
    ~IProcessRunner() override;

    IProcessRunner(const IProcessRunner&) = delete;
    IProcessRunner& operator=(const IProcessRunner&) = delete;

    virtual void startProcess(const QString& program, const QStringList& arguments) = 0;
    virtual bool executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output,
                                int timeoutMs = 30000) = 0;
    virtual void killProcess() = 0;
    virtual bool wasKilled() const = 0;
    virtual void setWorkingDirectory(const QString& dir) = 0;

signals:
    void processOutput(const QString& output);
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(const QString& error);
    void processStdErr(const QString& output);
};

#endif // IPROCESSRUNNER_H
