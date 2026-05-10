#ifndef FAKEPROCESSRUNNER_H
#define FAKEPROCESSRUNNER_H

#include "iprocessrunner.h"

#include <QList>
#include <QString>
#include <QStringList>

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

private:
    void emitAsyncSuccess();

    QList<StartInvocation> m_starts;
    QString m_workingDir;
    bool m_wasKilled = false;
};

#endif // FAKEPROCESSRUNNER_H
