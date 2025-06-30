#include "processmanager.h"
#include <QFileInfo>

ProcessManager::ProcessManager(QObject *parent)
    : QObject{parent}
{
}

ProcessManager::~ProcessManager()
{
    killProcess();
}

void ProcessManager::startProcess(const QString &program, const QStringList &arguments)
{
    emit processOutput(QString("Запуск (асинхронный): %1 %2").arg(program, arguments.join(" ")));

    QProcess* newProcess = new QProcess(this);
    m_activeProcesses.append(newProcess);

    connect(newProcess, &QProcess::readyReadStandardOutput, this, &ProcessManager::onReadyReadStandardOutput);
    connect(newProcess, &QProcess::readyReadStandardError, this, &ProcessManager::onReadyReadStandardError);

    connect(newProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        Q_UNUSED(error);
        QProcess* p = qobject_cast<QProcess*>(sender());
        emit processError("Не удалось запустить процесс: " + p->errorString());
    });

    // Когда процесс завершается, отправляем сигнал и удаляем его из нашего списка
    connect(newProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, newProcess](int exitCode, QProcess::ExitStatus exitStatus){
        emit processOutput(QString("Процесс (асинхронный) завершен с кодом %1.").arg(exitCode));
        emit processFinished(exitCode, exitStatus);
        m_activeProcesses.removeOne(newProcess);
        newProcess->deleteLater();
    });

    newProcess->start(program, arguments);
}

// Синхронный метод остается без изменений
bool ProcessManager::executeAndWait(const QString &program, const QStringList &arguments, QByteArray &output)
{
    emit processOutput(QString("Запуск (синхронный): %1 %2").arg(program, arguments.join(" ")));

    QProcess syncProcess;
    syncProcess.start(program, arguments);

    if (!syncProcess.waitForFinished(30000)) {
        emit processError("Процесс '" + program + "' не завершился за 30 секунд (timeout).");
        return false;
    }

    if (syncProcess.exitStatus() != QProcess::NormalExit || syncProcess.exitCode() != 0) {
        emit processError("Процесс '" + program + "' завершился с ошибкой.");
        emit processOutput("STDERR: " + syncProcess.readAllStandardError());
        return false;
    }

    output = syncProcess.readAllStandardOutput();
    emit processOutput("Процесс успешно завершен. Получено " + QString::number(output.size()) + " байт данных.");
    return true;
}

void ProcessManager::killProcess()
{
    if (m_activeProcesses.isEmpty()) return;

    emit processOutput(QString("Принудительное завершение %1 дочерних процессов...").arg(m_activeProcesses.count()));

    // Копируем список, так как он может изменяться во время итерации
    QList<QProcess*> processesToKill = m_activeProcesses;
    m_activeProcesses.clear();

    for(QProcess* process : processesToKill) {
        if(process && process->state() == QProcess::Running) {
            process->terminate();
            if (!process->waitForFinished(500)) {
                process->kill();
            }
        }
    }
}

void ProcessManager::onReadyReadStandardOutput()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (!process) return;
    emit processOutput(QString::fromUtf8(process->readAllStandardOutput()));
}

void ProcessManager::onReadyReadStandardError()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (!process) return;
    emit processStdErr(QString::fromUtf8(process->readAllStandardError()));
}
