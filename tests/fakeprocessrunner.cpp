#include "fakeprocessrunner.h"

#include <QFileInfo>
#include <QTimer>

FakeProcessRunner::FakeProcessRunner(QObject* parent) : IProcessRunner(parent)
{
}

void FakeProcessRunner::setWorkingDirectory(const QString& dir)
{
    m_workingDir = dir;
}

void FakeProcessRunner::killProcess()
{
    m_wasKilled = true;
}

bool FakeProcessRunner::wasKilled() const
{
    return m_wasKilled;
}

void FakeProcessRunner::startProcess(const QString& program, const QStringList& arguments)
{
    Q_UNUSED(m_workingDir);
    m_wasKilled = false;
    m_starts.append({program, arguments});

    int code = m_asyncExitCode;
    QProcess::ExitStatus status = m_asyncExitStatus;
    if (!m_asyncExitCodes.isEmpty())
    {
        code = m_asyncExitCodes.takeFirst();
    }

    QTimer::singleShot(0, this,
                       [this, code, status]()
                       {
                           if (!m_wasKilled)
                           {
                               emit processFinished(code, status);
                           }
                       });
}

bool FakeProcessRunner::executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output,
                                       int timeoutMs)
{
    Q_UNUSED(m_workingDir);
    m_wasKilled = false;
    m_executes.append({program, arguments, timeoutMs});

    if (m_executeHandler)
    {
        return m_executeHandler(program, arguments, output);
    }

    const QString base = QFileInfo(program).fileName().toLower();
    for (auto it = m_customExecuteResponses.cbegin(); it != m_customExecuteResponses.cend(); ++it)
    {
        if (program.contains(it.key(), Qt::CaseInsensitive) || base.contains(it.key(), Qt::CaseInsensitive))
        {
            output = it.value().second;
            return it.value().first;
        }
    }

    if (base.contains(QStringLiteral("mkvmerge")) && arguments.contains(QStringLiteral("-J")))
    {
        output = QByteArrayLiteral(
            R"({"container":{"properties":{"duration":100000000000}},"tracks":[{"id":0,"type":"video","codec":"h264"}]})");
        return true;
    }

    if (base == QStringLiteral("ffprobe.exe") || base == QStringLiteral("ffprobe"))
    {
        if (arguments.contains(QStringLiteral("-show_entries")) &&
            arguments.contains(QStringLiteral("stream=bit_rate")))
        {
            output = QByteArrayLiteral(R"({"streams":[{"bit_rate":"4000000"}]})");
            return true;
        }
        if (arguments.contains(QStringLiteral("-show_streams")))
        {
            output = QByteArrayLiteral(
                R"({"streams":[{"codec_type":"video","bit_rate":"5000000","r_frame_rate":"24/1","avg_frame_rate":"24/1"}]})");
            return true;
        }
        output = QByteArrayLiteral(R"({"streams":[{"codec_type":"video","bit_rate":"5000000"}]})");
        return true;
    }

    if (base.contains(QStringLiteral("ffmpeg")))
    {
        output.clear();
        return true;
    }

    output.clear();
    return true;
}
