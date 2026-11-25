#include "appsettings.h"
#include "fakeprocessrunner.h"
#include "manualrenderer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QtTest/QtTest>

class ManualRendererTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_testEnvDir;

private slots:
    void initTestCase();
    void fake_executeAndWait_mkvmergeProbeJson();
    void fake_executeAndWait_ffprobeStreamsJson();
    void fake_startProcess_emitsFinished();
    void testParsePresetCompositeVfWithoutHardsub();
    void testParsePresetUnquotedVfSubtitlesSignsWithoutHardsub();
    void testParsePresetTransfersAudioOptionsAndBitrate();
    void testInputSameAsOutputAborts();
    void testFullRenderPipeline();
    void testParsePresetEmptyTemplateReturnsFalse();
};

void ManualRendererTest::initTestCase()
{
    AppSettings::instance().setRenderPresets(AppSettings::defaultRenderPresets());

    QVERIFY(m_testEnvDir.isValid());

    const QString fakeMp4box = m_testEnvDir.filePath(QStringLiteral("mp4box.exe"));
    QFile fBox(fakeMp4box);
    QVERIFY(fBox.open(QIODevice::WriteOnly));
    fBox.write("fake mp4box");
    fBox.close();

    AppSettings::instance().setMp4boxPath(fakeMp4box);
    AppSettings::instance().setFfmpegPath(QStringLiteral("ffmpeg.exe"));
    AppSettings::instance().setMkvmergePath(QStringLiteral("mkvmerge.exe"));
}

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
    const QStringList args = {QStringLiteral("-v"),   QStringLiteral("quiet"),         QStringLiteral("-print_format"),
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

void ManualRendererTest::testParsePresetCompositeVfWithoutHardsub()
{
    QVariantMap params;
    params["useHardsub"] = false;
    params["inputMkv"] = "sample.mkv";
    params["outputMp4"] = "sample.mp4";

    ManualRenderer renderer(params, nullptr);

    const QString presetTemplate = "ffmpeg -y -i %INPUT% -vf \"scale=1920:1080,subtitles=%SIGNS%,format=yuv420p\" -c:v "
                                   "libx265 -b:v 4000k %OUTPUT%";
    QStringList outVideoArgs;
    QStringList outAudioArgs;

    QVERIFY(renderer.parsePreset(presetTemplate, outVideoArgs, outAudioArgs));

    QVERIFY(outVideoArgs.contains("-vf"));
    int idxVf = outVideoArgs.indexOf("-vf");
    QCOMPARE(outVideoArgs.at(idxVf + 1), QStringLiteral("scale=1920:1080,format=yuv420p"));
    QVERIFY(!outVideoArgs.contains("subtitles=%SIGNS%"));
}

void ManualRendererTest::testParsePresetUnquotedVfSubtitlesSignsWithoutHardsub()
{
    QVariantMap params;
    params["useHardsub"] = false;
    params["inputMkv"] = "sample.mkv";
    params["outputMp4"] = "sample.mp4";

    ManualRenderer renderer(params, nullptr);

    const QString presetTemplate = "ffmpeg -y -i %INPUT% -vf subtitles=%SIGNS% -c:v libx265 -crf 20 %OUTPUT%";
    QStringList outVideoArgs;
    QStringList outAudioArgs;

    QVERIFY(renderer.parsePreset(presetTemplate, outVideoArgs, outAudioArgs));

    QVERIFY(!outVideoArgs.contains("-vf"));
    QVERIFY(outVideoArgs.contains("-c:v"));
    int idxCv = outVideoArgs.indexOf("-c:v");
    QCOMPARE(outVideoArgs.at(idxCv + 1), QStringLiteral("libx265"));
}

void ManualRendererTest::testParsePresetTransfersAudioOptionsAndBitrate()
{
    QVariantMap params;
    params["useHardsub"] = false;
    params["reencodeAudioAac"] = true;
    params["inputMkv"] = "sample.mkv";
    params["outputMp4"] = "sample.mp4";

    ManualRenderer renderer(params, nullptr);

    const QString presetTemplate = "ffmpeg -y -i %INPUT% -c:v libx265 -c:a aac -b:a 192k -ac 2 -ar 48000 -af "
                                   "volume=1.5 -profile:a aac_low %OUTPUT%";
    QStringList outVideoArgs;
    QStringList outAudioArgs;

    QVERIFY(renderer.parsePreset(presetTemplate, outVideoArgs, outAudioArgs));

    QVERIFY(outAudioArgs.contains("-ac"));
    int idxAc = outAudioArgs.indexOf("-ac");
    QCOMPARE(outAudioArgs.at(idxAc + 1), QStringLiteral("2"));

    QVERIFY(outAudioArgs.contains("-ar"));
    int idxAr = outAudioArgs.indexOf("-ar");
    QCOMPARE(outAudioArgs.at(idxAr + 1), QStringLiteral("48000"));

    QVERIFY(outAudioArgs.contains("-af"));
    int idxAf = outAudioArgs.indexOf("-af");
    QCOMPARE(outAudioArgs.at(idxAf + 1), QStringLiteral("volume=1.5"));

    QVERIFY(outAudioArgs.contains("-profile:a"));
    int idxProf = outAudioArgs.indexOf("-profile:a");
    QCOMPARE(outAudioArgs.at(idxProf + 1), QStringLiteral("aac_low"));

    QVERIFY(outAudioArgs.contains("-b:a"));
    int idxBa = outAudioArgs.indexOf("-b:a");
    QCOMPARE(outAudioArgs.at(idxBa + 1), QStringLiteral("320k"));
}

void ManualRendererTest::testInputSameAsOutputAborts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString file = tempDir.filePath("video.mp4");
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("dummy");
    f.close();

    QVariantMap params;
    params["inputMkv"] = file;
    params["outputMp4"] = file;

    FakeProcessRunner fake;
    ManualRenderer renderer(params, nullptr, &fake);

    QSignalSpy spyFinished(&renderer, &ManualRenderer::finished);
    renderer.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);
    QVERIFY(fake.startInvocations().isEmpty());
}

void ManualRendererTest::testFullRenderPipeline()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString inputPath = tempDir.filePath("input.mkv");
    QFile f(inputPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("dummy video data");
    f.close();

    QVariantMap params;
    params["inputMkv"] = inputPath;
    params["outputMp4"] = tempDir.filePath("output.mp4");
    params["renderPresetName"] = "NVIDIA (hevc_nvenc, 1-проход)";
    params["useHardsub"] = false;
    params["reencodeAudioAac"] = true;

    FakeProcessRunner fake;
    ManualRenderer renderer(params, nullptr, &fake);

    QSignalSpy spyFinished(&renderer, &ManualRenderer::finished);
    renderer.start();

    QTRY_COMPARE_WITH_TIMEOUT(spyFinished.count(), 1, 5000);

    const auto starts = fake.startInvocations();
    QVERIFY(starts.size() >= 3); // VideoPass1, AudioPass, MP4Box mux
    QVERIFY(starts[0].arguments.contains(QStringLiteral("-an")));
    QVERIFY(starts[1].arguments.contains(QStringLiteral("-vn")));
    QVERIFY(starts[1].arguments.contains(QStringLiteral("320k")));
    QVERIFY(starts[2].arguments.join(QLatin1Char(' ')).contains(QLatin1String("#video")));
    QVERIFY(starts[2].arguments.join(QLatin1Char(' ')).contains(QLatin1String("#audio")));
}

void ManualRendererTest::testParsePresetEmptyTemplateReturnsFalse()
{
    QVariantMap params;
    params["useHardsub"] = false;
    params["inputMkv"] = "sample.mkv";
    params["outputMp4"] = "sample.mp4";

    ManualRenderer renderer(params, nullptr);
    QStringList outVideoArgs;
    QStringList outAudioArgs;

    QCOMPARE(renderer.parsePreset(QString(), outVideoArgs, outAudioArgs), false);
    QCOMPARE(renderer.parsePreset(QStringLiteral("ffmpeg"), outVideoArgs, outAudioArgs), false);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ManualRendererTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "manualrenderer_test.moc"
