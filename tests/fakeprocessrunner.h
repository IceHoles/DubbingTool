#ifndef FAKEPROCESSRUNNER_H
#define FAKEPROCESSRUNNER_H

#include "iprocessrunner.h"

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>

/**
 * @brief Test double for IProcessRunner: scripted executeAndWait and immediate async finish for startProcess.
 */
class FakeProcessRunner final : public IProcessRunner
{
    Q_OBJECT

public:
    struct StartInvocation
    {
        QString program;
        QStringList arguments;
    };

    struct ExecuteInvocation
    {
        QString program;
        QStringList arguments;
        int timeoutMs = 30000;
    };

    explicit FakeProcessRunner(QObject* parent = nullptr);

    void startProcess(const QString& program, const QStringList& arguments) override;
    bool executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output,
                        int timeoutMs = 30000) override;
    void killProcess() override;
    bool wasKilled() const override;
    void setWorkingDirectory(const QString& dir) override;

    [[nodiscard]] QList<StartInvocation> startInvocations() const
    {
        return m_starts;
    }

    void clearStartInvocations()
    {
        m_starts.clear();
    }

    [[nodiscard]] QList<ExecuteInvocation> executeInvocations() const
    {
        return m_executes;
    }

    void clearExecuteInvocations()
    {
        m_executes.clear();
    }

    void setAsyncExitCode(int exitCode, QProcess::ExitStatus exitStatus = QProcess::NormalExit)
    {
        m_asyncExitCode = exitCode;
        m_asyncExitStatus = exitStatus;
    }

    void setAsyncExitCodes(const QList<int>& exitCodes)
    {
        m_asyncExitCodes = exitCodes;
    }

    using ExecuteHandler =
        std::function<bool(const QString& program, const QStringList& arguments, QByteArray& output)>;
    void setExecuteHandler(ExecuteHandler handler)
    {
        m_executeHandler = std::move(handler);
    }

    void setCustomExecuteResponse(const QString& programSubstring, bool success, const QByteArray& output)
    {
        m_customExecuteResponses[programSubstring] = qMakePair(success, output);
    }

private:
    QList<StartInvocation> m_starts;
    QList<ExecuteInvocation> m_executes;
    QString m_workingDir;
    bool m_wasKilled = false;

    int m_asyncExitCode = 0;
    QProcess::ExitStatus m_asyncExitStatus = QProcess::NormalExit;
    QList<int> m_asyncExitCodes;

    ExecuteHandler m_executeHandler;
    QMap<QString, QPair<bool, QByteArray>> m_customExecuteResponses;
};

#endif // FAKEPROCESSRUNNER_H
