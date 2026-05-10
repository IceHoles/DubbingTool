/**
 * @file ffmpeg_smoke_test.cpp
 * @brief Optional smoke: ffmpeg/ffprobe available on PATH (CI installs via Chocolatey).
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

class FfmpegSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void ffmpeg_and_ffprobe_version();
};

static bool runSync(const QString& exe, const QStringList& args)
{
    QProcess p;
    p.start(exe, args);
    return p.waitForFinished(20000) && p.exitCode() == 0 && p.error() == QProcess::UnknownError;
}

void FfmpegSmokeTest::ffmpeg_and_ffprobe_version()
{
#ifdef Q_OS_WIN
    const QString ffmpeg = QStringLiteral("ffmpeg.exe");
    const QString ffprobe = QStringLiteral("ffprobe.exe");
#else
    const QString ffmpeg = QStringLiteral("ffmpeg");
    const QString ffprobe = QStringLiteral("ffprobe");
#endif

    const QString ffmpegPath = QStandardPaths::findExecutable(ffmpeg);
    const QString ffprobePath = QStandardPaths::findExecutable(ffprobe);
    if (ffmpegPath.isEmpty() || ffprobePath.isEmpty())
    {
        QSKIP("ffmpeg/ffprobe not on PATH");
    }

    QVERIFY(runSync(ffmpegPath, QStringList() << QStringLiteral("-version")));
    QVERIFY(runSync(ffprobePath, QStringList() << QStringLiteral("-version")));
}

QTEST_MAIN(FfmpegSmokeTest)

#include "ffmpeg_smoke_test.moc"
