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

void FakeProcessRunner::emitAsyncSuccess()
{
    if (m_wasKilled)
    {
        return;
    }
    emit processFinished(0, QProcess::NormalExit);
}

void FakeProcessRunner::startProcess(const QString& program, const QStringList& arguments)
{
    Q_UNUSED(m_workingDir);
    m_wasKilled = false;
    m_starts.append({program, arguments});
    QTimer::singleShot(0, this, [this]() { emitAsyncSuccess(); });
}

bool FakeProcessRunner::executeAndWait(const QString& program, const QStringList& arguments, QByteArray& output, int)
{
    Q_UNUSED(m_workingDir);
    m_wasKilled = false;
    const QString base = QFileInfo(program).fileName().toLower();

    if (base.contains(QStringLiteral("mkvmerge")) && arguments.contains(QStringLiteral("-J")))
    {
        output = QByteArrayLiteral(
            R"({"container":{"properties":{"duration":100000000000}},"tracks":[{"type":"video","codec":"h264"}]})");
        return true;
    }

    if (base == QStringLiteral("ffprobe.exe") || base == QStringLiteral("ffprobe"))
    {
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

void FakeProcessRunner::killProcess()
{
    m_wasKilled = true;
}

bool FakeProcessRunner::wasKilled() const
{
    return m_wasKilled;
}
