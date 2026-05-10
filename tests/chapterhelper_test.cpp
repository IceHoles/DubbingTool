/**
 * @file chapterhelper_test.cpp
 * @brief Unit tests for ChapterHelper parsers and chapter file writers.
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "chapterhelper.h"

class ChapterHelperTest : public QObject
{
    Q_OBJECT

private slots:
    void testParseMatroskaChapterXmlFile_fixture();
    void testLoadChaptersFromFile_ogm_fixture();
    void testParseFfprobeChaptersJson_fixture();
    void testParseMatroskaChapterXmlFile_missingFile();
    void testBuildChapterTimingSeconds();
    void testWriteMatroskaChapterXml_roundTrip();
    void testWriteFfmetadata_containsMarkers();
    void testWriteOgmChapterText_format();

private:
    QString fixturePath(const QString& name) const;
};

QString ChapterHelperTest::fixturePath(const QString& name) const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("test_data/%1").arg(name));
}

void ChapterHelperTest::testParseMatroskaChapterXmlFile_fixture()
{
    const QList<ChapterMarker> markers =
        ChapterHelper::parseMatroskaChapterXmlFile(fixturePath(QStringLiteral("chapters_matroska.xml")));
    QCOMPARE(markers.size(), 2);
    QCOMPARE(markers.at(0).title, QStringLiteral("Intro"));
    QVERIFY(markers.at(0).startNs >= 0);
    QCOMPARE(markers.at(1).title, QStringLiteral("Main"));
    QVERIFY(markers.at(1).startNs > markers.at(0).startNs);
}

void ChapterHelperTest::testLoadChaptersFromFile_ogm_fixture()
{
    const QList<ChapterMarker> markers =
        ChapterHelper::loadChaptersFromFile(fixturePath(QStringLiteral("chapters_ogm.txt")));
    QCOMPARE(markers.size(), 2);
    QCOMPARE(markers.at(0).title, QStringLiteral("Intro"));
    QCOMPARE(markers.at(1).title, QStringLiteral("Main"));
}

void ChapterHelperTest::testParseFfprobeChaptersJson_fixture()
{
    QFile f(fixturePath(QStringLiteral("ffprobe_chapters.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray json = f.readAll();
    f.close();

    const QList<ChapterMarker> markers = ChapterHelper::parseFfprobeChaptersJson(json);
    QCOMPARE(markers.size(), 2);
    QCOMPARE(markers.at(0).title, QStringLiteral("Intro"));
    QVERIFY(markers.at(0).endNs > markers.at(0).startNs);
    QCOMPARE(markers.at(1).title, QStringLiteral("Main"));
}

void ChapterHelperTest::testParseMatroskaChapterXmlFile_missingFile()
{
    const QList<ChapterMarker> markers =
        ChapterHelper::parseMatroskaChapterXmlFile(QStringLiteral("/nonexistent/path/chapters.xml"));
    QVERIFY(markers.isEmpty());
}

void ChapterHelperTest::testBuildChapterTimingSeconds()
{
    QList<ChapterMarker> markers;
    ChapterMarker a;
    a.startNs = 0;
    a.endNs = 5000000000LL;
    a.title = QStringLiteral("A");
    markers.append(a);
    ChapterMarker b;
    b.startNs = 5000000000LL;
    b.endNs = -1;
    b.title = QStringLiteral("B");
    markers.append(b);

    const QList<ChapterTimingSeconds> rows =
        ChapterHelper::buildChapterTimingSeconds(markers, 120000000000LL);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).title, QStringLiteral("A"));
    QCOMPARE(rows.at(1).title, QStringLiteral("B"));
    QVERIFY(rows.at(1).endSeconds >= rows.at(1).startSeconds);
}

void ChapterHelperTest::testWriteMatroskaChapterXml_roundTrip()
{
    QList<ChapterMarker> in;
    ChapterMarker ch;
    ch.startNs = 12345000000LL;
    ch.endNs = -1;
    ch.title = QStringLiteral("UnitChapter");
    in.append(ch);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("chapters_out.xml"));
    QVERIFY(ChapterHelper::writeMatroskaChapterXml(in, path));

    const QList<ChapterMarker> out = ChapterHelper::parseMatroskaChapterXmlFile(path);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).startNs, ch.startNs);
    QCOMPARE(out.at(0).title, ch.title);
}

void ChapterHelperTest::testWriteFfmetadata_containsMarkers()
{
    QList<ChapterMarker> markers;
    ChapterMarker ch;
    ch.startNs = 1000000000LL;
    ch.endNs = 5000000000LL;
    ch.title = QStringLiteral("Ep1");
    markers.append(ch);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("meta.txt"));
    QVERIFY(ChapterHelper::writeFfmetadata(markers, 60000000000LL, path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("[CHAPTER]")));
    QVERIFY(content.contains(QStringLiteral("TIMEBASE=1/1000000000")));
    QVERIFY(content.contains(QStringLiteral("START=1000000000")));
    QVERIFY(content.contains(QStringLiteral("title=Ep1")));
}

void ChapterHelperTest::testWriteOgmChapterText_format()
{
    QList<ChapterMarker> markers;
    ChapterMarker ch;
    ch.startNs = 3661000000000LL;
    ch.endNs = -1;
    ch.title = QStringLiteral("One");
    markers.append(ch);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ogm.txt"));
    QVERIFY(ChapterHelper::writeOgmChapterText(markers, path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("CHAPTER01=")));
    QVERIFY(content.contains(QStringLiteral("CHAPTER01NAME=One")));
}

QTEST_MAIN(ChapterHelperTest)

#include "chapterhelper_test.moc"
