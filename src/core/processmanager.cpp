#include "processmanager.h"

#include <QFileInfo>

ProcessManager::ProcessManager(QObject* parent) : QObject{parent}
{
}

ProcessManager::~ProcessManager()
{
    killProcess();
}

static QString formatCommand(const QString& program, const QStringList& arguments)
{
    QStringList escapedArgs;
    for (const QString& arg : arguments)
    {
        if (arg.contains(' '))
        {
            escapedArgs.append(QString("\"%1\"").arg(arg));
        }
        else
        {
            escapedArgs.append(arg);
        }
    }
    return QString("%1 %2").arg(program, escapedArgs.join(" "));
}

void ProcessManager::startProcess(const QString& program, const QStringList& arguments)
{
    m_wasKilled = false;
    emit processOutput(QString("Запуск (асинхронный): %1").arg(formatCommand(program, arguments)));

    QProcess* newProcess = new QProcess(this);
    if (!m_workingDir.isEmpty())
    {
        newProcess->setWorkingDirectory(m_workingDir);
    }
    m_activeProcesses.append(newProcess);

    connect(newProcess, &QProcess::readyReadStandardOutput, this, &ProcessManager::onReadyReadStandardOutput);
    connect(newProcess, &QProcess::readyReadStandardError, this, &ProcessManager::onReadyReadStandardError);

    connect(newProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                Q_UNUSED(error);
                QProcess* p = qobject_cast<QProcess*>(sender());
                emit processError("Не удалось запустить процесс: " + p->errorString());
            });

    // Когда процесс завершается, отправляем сигнал и удаляем его из нашего списка
    connect(newProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, newProcess](int exitCode, QProcess::ExitStatus exitStatus)
            {
                flushProcessBuffers(newProcess);
                emit processOutput(QString("Процесс (асинхронный) завершен с кодом %1.").arg(exitCode));
                emit processFinished(exitCode, exitStatus);
                m_activeProcesses.removeOne(newProcess);
                m_stdoutBuffers.remove(newProcess);
                m_stderrBuffers.remove(newProcess);
                newProcess->deleteLater();
            });

    newProcess->start(program, arguments);
    m_workingDir.clear();
}

bool ProcessManager::executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output,
                                    int timeoutMs)
{
    emit processOutput(QString("Запуск (синхронный): %1").arg(formatCommand(program, arguments)));

    QProcess syncProcess;
    if (!m_workingDir.isEmpty())
    {
        syncProcess.setWorkingDirectory(m_workingDir);
    }
    syncProcess.start(program, arguments);

    if (!syncProcess.waitForFinished(timeoutMs))
    {
        emit processError("Процесс '" + program + "' не завершился за " + QString::number(timeoutMs) +
                          " мс (timeout).");
        m_workingDir.clear();
        return false;
    }

    if (syncProcess.exitStatus() != QProcess::NormalExit || syncProcess.exitCode() != 0)
    {
        QString errorString = QString("Процесс '%1' завершился с ошибкой. Код: %2, Статус: %3.")
                                  .arg(QFileInfo(program).fileName())
                                  .arg(syncProcess.exitCode())
                                  .arg(syncProcess.exitStatus() == QProcess::NormalExit ? "Normal" : "Crash");
        emit processError(errorString);
        QByteArray stderrData = syncProcess.readAllStandardError();
        if (!stderrData.isEmpty())
        {
            emit processError("STDERR: " + QString::fromUtf8(stderrData));
        }
        m_workingDir.clear();
        return false;
    }

    output = syncProcess.readAllStandardOutput();
    QString outStr = QString::fromUtf8(output);
    emit processOutput("Процесс успешно завершен. Получено " + QString::number(output.size()) + " байт данных:\n" +
                       outStr.trimmed());
    m_workingDir.clear();
    return true;
}

void ProcessManager::killProcess()
{
    if (m_activeProcesses.isEmpty())
        return;

    m_wasKilled = true;

    emit processOutput(QString("Принудительное завершение %1 дочерних процессов...").arg(m_activeProcesses.count()));

    // Копируем список, так как он может изменяться во время итерации
    QList<QProcess*> processesToKill = m_activeProcesses;
    m_activeProcesses.clear();

    for (QProcess* process : processesToKill)
    {
        if (process && process->state() == QProcess::Running)
        {
            process->terminate();
            if (!process->waitForFinished(500))
            {
                process->kill();
            }
        }
    }
}

bool ProcessManager::wasKilled() const
{
    return m_wasKilled;
}

void ProcessManager::onReadyReadStandardOutput()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (!process)
        return;
    emitBufferedLines(process, process->readAllStandardOutput(), false);
}

void ProcessManager::onReadyReadStandardError()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (!process)
        return;
    emitBufferedLines(process, process->readAllStandardError(), true);
}

void ProcessManager::emitBufferedLines(QProcess* process, const QByteArray& chunk, bool isStdErr)
{
    QString& buffer = isStdErr ? m_stderrBuffers[process] : m_stdoutBuffers[process];
    QString incoming = QString::fromUtf8(chunk);
    incoming.replace("\r\n", "\n");
    incoming.replace('\r', '\n');
    buffer += incoming;

    qsizetype lineEnd = buffer.indexOf('\n');
    while (lineEnd >= 0)
    {
        QString line = buffer.left(lineEnd);
        buffer.remove(0, lineEnd + 1);
        if (!line.isEmpty())
        {
            if (isStdErr)
            {
                emit processStdErr(line);
            }
            else
            {
                emit processOutput(line);
            }
        }
        lineEnd = buffer.indexOf('\n');
    }
}

void ProcessManager::flushProcessBuffers(QProcess* process)
{
    auto flushOne = [&](QHash<QProcess*, QString>& map, bool isStdErr)
    {
        if (!map.contains(process))
        {
            return;
        }
        const QString tail = map.value(process);
        if (!tail.trimmed().isEmpty())
        {
            if (isStdErr)
            {
                emit processStdErr(tail);
            }
            else
            {
                emit processOutput(tail);
            }
        }
        map.remove(process);
    };

    flushOne(m_stdoutBuffers, false);
    flushOne(m_stderrBuffers, true);
}
