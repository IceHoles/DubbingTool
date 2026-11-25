#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include "iprocessrunner.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QProcess>

class ProcessManager : public IProcessRunner
{
    Q_OBJECT

public:
    explicit ProcessManager(QObject* parent = nullptr);
    ~ProcessManager() override;

    void startProcess(const QString& program, const QStringList& arguments) override;
    bool executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output,
                        int timeoutMs = 30000) override;
    void killProcess() override;
    bool wasKilled() const override;
    void setWorkingDirectory(const QString& dir) override
    {
        m_workingDir = dir;
    }

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    void emitBufferedLines(QProcess* process, const QByteArray& chunk, bool isStdErr);
    void flushProcessBuffers(QProcess* process);

    // Храним список всех запущенных этим менеджером процессов
    QList<QProcess*> m_activeProcesses;
    bool m_wasKilled = false;
    QString m_workingDir;
    QHash<QProcess*, QString> m_stdoutBuffers;
    QHash<QProcess*, QString> m_stderrBuffers;
};

#endif // PROCESSMANAGER_H
