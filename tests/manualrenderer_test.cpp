/**
 * @file manualrenderer_test.cpp
 * @brief Tests for FakeProcessRunner (IProcessRunner test double).
 *
 * Full ManualRenderer pipeline tests are deferred: they require stable preset/AppSettings
 * fixtures and a running Qt event loop; FakeProcessRunner is what future component tests
 * will drive for deterministic ffmpeg/mkvmerge/mp4box sequences.
 */

#include <QtTest/QtTest>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QStringList>

#include "fakeprocessrunner.h"
#include "iprocessrunner.h"

class ManualRendererTest : public QObject
{
    Q_OBJECT

private slots:
    void fake_executeAndWait_mkvmergeProbeJson();
    void fake_executeAndWait_ffprobeStreamsJson();
    void fake_startProcess_emitsFinished();
};

void ManualRendererTest::fake_executeAndWait_mkvmergeProbeJson()
{
    FakeProcessRunner fake;
    QByteArray output;
    const QStringList args = {QStringLiteral("-J"), QStringLiteral("dummy.mkv")};
    QVERIFY(fake.executeAndWait(QStringLiteral("mkvmerge.exe"), args, output));
    QVERIFY(output.contains("\"tracks\""));
    QVERIFY(output.contains("\"container\""));
}

void ManualRendererTest::fake_executeAndWait_ffprobeStreamsJson()
{
    FakeProcessRunner fake;
    QByteArray output;
    const QStringList args = {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"),
                              QStringLiteral("json"), QStringLiteral("-show_streams"), QStringLiteral("dummy.mkv")};
    QVERIFY(fake.executeAndWait(QStringLiteral("ffprobe.exe"), args, output));
    QVERIFY(output.contains("\"streams\""));
}

void ManualRendererTest::fake_startProcess_emitsFinished()
{
    FakeProcessRunner fake;
    QSignalSpy spy(&fake, &IProcessRunner::processFinished);
    fake.startProcess(QStringLiteral("ffmpeg.exe"), QStringList({QStringLiteral("-version")}));
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ManualRendererTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "manualrenderer_test.moc"
