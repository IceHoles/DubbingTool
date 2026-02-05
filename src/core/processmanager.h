#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QObject>
#include <QProcess>
#include <QList>


class ProcessManager : public QObject
{
    Q_OBJECT
public:
    explicit ProcessManager(QObject *parent = nullptr);
    ~ProcessManager();

    void startProcess(const QString &program, const QStringList &arguments);
    bool executeAndWait(const QString &program, const QStringList &arguments, QByteArray &output);
    void killProcess();
    bool wasKilled() const;

signals:
    void processOutput(const QString &output);
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(const QString &error);
    void processStdErr(const QString &output);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    // Храним список всех запущенных этим менеджером процессов
    QList<QProcess*> m_activeProcesses;
    bool m_wasKilled = false;
};

#endif // PROCESSMANAGER_H
