#include "mediaprobe.h"

#include <QByteArray>

#include <QtTest/QtTest>

class MediaProbeTest : public QObject
{
    Q_OBJECT

private slots:
    void testParseVideoNotTrack0();
    void testParseAudioChannelsAndLanguages();
    void testParseSubtitlesAndAttachments();
    void testParseDuration();
    void testParseZeroAudioTracks();
    void testParseCorruptedAndEmptyJson();
};

void MediaProbeTest::testParseVideoNotTrack0()
{
    const QByteArray json = R"({
        "container": {"properties": {"duration": 50000000000}},
        "tracks": [
            {
                "id": 0,
                "type": "audio",
                "codec": "AAC",
                "properties": {"codec_id": "A_AAC", "language": "eng", "audio_channels": 2}
            },
            {
                "id": 1,
                "type": "video",
                "codec": "MPEG-4p10/AVC/h.264",
                "properties": {"codec_id": "V_MPEG4/ISO/AVC", "language": "und"}
            },
            {
                "id": 2,
                "type": "audio",
                "codec": "FLAC",
                "properties": {"codec_id": "A_FLAC", "language": "rus", "audio_channels": 6}
            }
        ]
    })";

    const MediaProbeResult res = MediaProbe::parseMkvMergeJson(json);
    QVERIFY(res.ok);
    QCOMPARE(res.videoTracks.size(), 1);
    QCOMPARE(res.videoTracks[0].id, 1);
    QCOMPARE(res.videoTracks[0].typeIndex, 0);
    QCOMPARE(res.videoTracks[0].codec, QStringLiteral("MPEG-4p10/AVC/h.264"));

    QCOMPARE(res.audioTracks.size(), 2);
    QCOMPARE(res.audioTracks[0].id, 0);
    QCOMPARE(res.audioTracks[0].typeIndex, 0);
    QCOMPARE(res.audioTracks[0].language, QStringLiteral("eng"));
    QCOMPARE(res.audioTracks[0].channels, 2);

    QCOMPARE(res.audioTracks[1].id, 2);
    QCOMPARE(res.audioTracks[1].typeIndex, 1);
    QCOMPARE(res.audioTracks[1].language, QStringLiteral("rus"));
    QCOMPARE(res.audioTracks[1].channels, 6);
}

void MediaProbeTest::testParseAudioChannelsAndLanguages()
{
    const QByteArray json = R"({
        "tracks": [
            {
                "id": 1,
                "type": "audio",
                "codec": "AC-3",
                "properties": {
                    "codec_id": "A_AC3",
                    "language": "jpn",
                    "track_name": "Surround 5.1",
                    "audio_channels": 6
                }
            }
        ]
    })";

    const MediaProbeResult res = MediaProbe::parseMkvMergeJson(json);
    QVERIFY(res.ok);
    QCOMPARE(res.audioTracks.size(), 1);
    const auto& track = res.audioTracks[0];
    QCOMPARE(track.id, 1);
    QCOMPARE(track.language, QStringLiteral("jpn"));
    QCOMPARE(track.trackName, QStringLiteral("Surround 5.1"));
    QCOMPARE(track.channels, 6);
    QCOMPARE(track.codecId, QStringLiteral("A_AC3"));
}

void MediaProbeTest::testParseSubtitlesAndAttachments()
{
    const QByteArray json = R"({
        "tracks": [
            {
                "id": 3,
                "type": "subtitles",
                "codec": "SubStationAlpha",
                "properties": {
                    "codec_id": "S_TEXT/ASS",
                    "language": "rus",
                    "track_name": "Full Subs"
                }
            },
            {
                "id": 4,
                "type": "subtitles",
                "codec": "SubRip/SRT",
                "properties": {
                    "codec_id": "S_TEXT/UTF8",
                    "language": "eng",
                    "track_name": "Signs"
                }
            }
        ],
        "attachments": [
            {
                "id": 1,
                "file_name": "CustomFont.ttf",
                "content_type": "application/x-truetype-font"
            },
            {
                "id": 2,
                "file_name": "OtherFont.otf",
                "content_type": "font/otf"
            }
        ]
    })";

    const MediaProbeResult res = MediaProbe::parseMkvMergeJson(json);
    QVERIFY(res.ok);
    QCOMPARE(res.subtitleTracks.size(), 2);
    QCOMPARE(res.subtitleTracks[0].id, 3);
    QCOMPARE(res.subtitleTracks[0].typeIndex, 0);
    QCOMPARE(res.subtitleTracks[0].language, QStringLiteral("rus"));
    QCOMPARE(res.subtitleTracks[1].id, 4);
    QCOMPARE(res.subtitleTracks[1].typeIndex, 1);
    QCOMPARE(res.subtitleTracks[1].language, QStringLiteral("eng"));

    QCOMPARE(res.attachments.size(), 2);
    QCOMPARE(res.attachments[0].id, 1);
    QCOMPARE(res.attachments[0].fileName, QStringLiteral("CustomFont.ttf"));
    QCOMPARE(res.attachments[0].contentType, QStringLiteral("application/x-truetype-font"));
    QCOMPARE(res.attachments[1].id, 2);
    QCOMPARE(res.attachments[1].fileName, QStringLiteral("OtherFont.otf"));
}

void MediaProbeTest::testParseDuration()
{
    const QByteArray json = R"({
        "container": {
            "properties": {
                "duration": 1420500000000
            }
        }
    })";

    const MediaProbeResult res = MediaProbe::parseMkvMergeJson(json);
    QVERIFY(res.ok);
    QCOMPARE(res.durationNs, 1420500000000LL);
}

void MediaProbeTest::testParseZeroAudioTracks()
{
    const QByteArray json = R"({
        "tracks": [
            {
                "id": 0,
                "type": "video",
                "codec": "MPEG-4p10/AVC/h.264",
                "properties": {"language": "und"}
            }
        ]
    })";

    const MediaProbeResult res = MediaProbe::parseMkvMergeJson(json);
    QVERIFY(res.ok);
    QCOMPARE(res.videoTracks.size(), 1);
    QCOMPARE(res.audioTracks.size(), 0);
}

void MediaProbeTest::testParseCorruptedAndEmptyJson()
{
    MediaProbeResult resEmpty = MediaProbe::parseMkvMergeJson(QByteArray());
    QVERIFY(!resEmpty.ok);

    MediaProbeResult resBroken = MediaProbe::parseMkvMergeJson(QByteArrayLiteral("{ invalid json ..."));
    QVERIFY(!resBroken.ok);

    MediaProbeResult resArray = MediaProbe::parseMkvMergeJson(QByteArrayLiteral("[1, 2, 3]"));
    QVERIFY(!resArray.ok);
}

QTEST_MAIN(MediaProbeTest)
#include "mediaprobe_test.moc"
