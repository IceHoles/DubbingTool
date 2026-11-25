#include "appsettings.h"
#include "fakeprocessrunner.h"
#include "manualassembler.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QtTest/QtTest>

class ManualAssemblerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testDirectAssemblyFromContainer();
    void testEmptyPathsNotAddedToArgs();
    void testEmptySubAuthorDoesNotProduceBrackets();
    void testMkvmergeWarningExitCode1Succeeds();
    void testAudioConversionUses320k();
};

void ManualAssemblerTest::initTestCase()
{
    AppSettings::instance().setMkvmergePath(QStringLiteral("mkvmerge.exe"));
    AppSettings::instance().setFfmpegPath(QStringLiteral("ffmpeg.exe"));
}

void ManualAssemblerTest::testDirectAssemblyFromContainer()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVariantMap params;
    params["videoPath"] = tempDir.filePath("sample.mp4");
    params["videoTrackId"] = 1;
    params["useAudioFromVideo"] = true;
    params["audioTrackFromVideoId"] = 2;
    params["russianAudioPath"] = tempDir.filePath("voice.mka");
    params["outputName"] = "result.mkv";
    params["workDir"] = tempDir.path();
    params["normalizeAudio"] = false;
    params["convertAudio"] = false;

    FakeProcessRunner fakeRunner;
    ManualAssembler assembler(params, nullptr, &fakeRunner);

    QSignalSpy spyFinished(&assembler, &ManualAssembler::finished);
    assembler.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);
    QCOMPARE(spyFinished.takeFirst().at(0).toBool(), true);

    const auto starts = fakeRunner.startInvocations();
    QVERIFY(!starts.isEmpty());
    const auto& mkvmergeCall = starts.last();
    const QStringList args = mkvmergeCall.arguments;

    QVERIFY(args.contains("--video-tracks"));
    int idxVideo = args.indexOf("--video-tracks");
    QCOMPARE(args.at(idxVideo + 1), QStringLiteral("1"));

    QVERIFY(args.contains("--audio-tracks"));
    int idxAudio = args.indexOf("--audio-tracks");
    QCOMPARE(args.at(idxAudio + 1), QStringLiteral("2"));

    QVERIFY(args.contains("--no-subtitles"));
    QVERIFY(args.contains("--no-attachments"));

    QVERIFY(args.contains("--track-order"));
    int idxOrder = args.indexOf("--track-order");
    QCOMPARE(args.at(idxOrder + 1), QStringLiteral("0:1,1:0,0:2"));
}

void ManualAssemblerTest::testEmptyPathsNotAddedToArgs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVariantMap params;
    params["videoPath"] = tempDir.filePath("video.mkv");
    params["russianAudioPath"] = "";
    params["subtitlesPath"] = "";
    params["signsPath"] = "";
    params["outputName"] = "result.mkv";
    params["workDir"] = tempDir.path();
    params["normalizeAudio"] = false;
    params["convertAudio"] = false;

    FakeProcessRunner fakeRunner;
    ManualAssembler assembler(params, nullptr, &fakeRunner);

    QSignalSpy spyFinished(&assembler, &ManualAssembler::finished);
    assembler.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);

    const auto starts = fakeRunner.startInvocations();
    QVERIFY(!starts.isEmpty());
    const QStringList args = starts.last().arguments;

    for (const QString& arg : args)
    {
        QVERIFY2(!arg.isEmpty(), "Empty argument passed to mkvmerge");
    }
}

void ManualAssemblerTest::testEmptySubAuthorDoesNotProduceBrackets()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVariantMap params;
    params["videoPath"] = tempDir.filePath("video.mkv");
    params["subtitlesPath"] = tempDir.filePath("dialogs.ass");
    params["studio"] = "DubStudio";
    params["subAuthor"] = "";
    params["outputName"] = "result.mkv";
    params["workDir"] = tempDir.path();
    params["normalizeAudio"] = false;
    params["convertAudio"] = false;

    FakeProcessRunner fakeRunner;
    ManualAssembler assembler(params, nullptr, &fakeRunner);

    QSignalSpy spyFinished(&assembler, &ManualAssembler::finished);
    assembler.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);

    const auto starts = fakeRunner.startInvocations();
    QVERIFY(!starts.isEmpty());
    const QStringList args = starts.last().arguments;

    for (const QString& arg : args)
    {
        QVERIFY2(!arg.contains("[]"), "Found empty brackets '[]' in arguments");
        QVERIFY2(!arg.contains("[ ]"), "Found empty brackets '[ ]' in arguments");
    }
}

void ManualAssemblerTest::testMkvmergeWarningExitCode1Succeeds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVariantMap params;
    params["videoPath"] = tempDir.filePath("video.mkv");
    params["outputName"] = "result.mkv";
    params["workDir"] = tempDir.path();
    params["normalizeAudio"] = false;
    params["convertAudio"] = false;

    FakeProcessRunner fakeRunner;
    fakeRunner.setAsyncExitCode(1); // mkvmerge exit code 1 (warning)

    ManualAssembler assembler(params, nullptr, &fakeRunner);
    QSignalSpy spyFinished(&assembler, &ManualAssembler::finished);

    assembler.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);
    QCOMPARE(spyFinished.takeFirst().at(0).toBool(), true);
}

void ManualAssemblerTest::testAudioConversionUses320k()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVariantMap params;
    params["videoPath"] = tempDir.filePath("video.mkv");
    params["russianAudioPath"] = tempDir.filePath("raw_audio.wav");
    params["outputName"] = "result.mkv";
    params["workDir"] = tempDir.path();
    params["normalizeAudio"] = false;
    params["convertAudio"] = true;
    params["convertAudioFormat"] = "aac";

    FakeProcessRunner fakeRunner;
    ManualAssembler assembler(params, nullptr, &fakeRunner);

    assembler.start();

    QTRY_VERIFY_WITH_TIMEOUT(!fakeRunner.startInvocations().isEmpty(), 5000);

    const auto starts = fakeRunner.startInvocations();
    const auto& firstCall = starts.first(); // ffmpeg audio conversion pass
    const QStringList args = firstCall.arguments;

    QVERIFY(args.contains("-b:a"));
    int idxBitrate = args.indexOf("-b:a");
    QCOMPARE(args.at(idxBitrate + 1), QStringLiteral("320k"));
    QVERIFY(!args.contains("256k"));
}

QTEST_MAIN(ManualAssemblerTest)
#include "manualassembler_test.moc"
