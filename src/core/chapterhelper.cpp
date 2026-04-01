#include "chapterhelper.h"

#include "processmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>
#include <algorithm>

namespace
{
QByteArray stripUtf8Bom(QByteArray data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
    {
        return data.mid(3);
    }
    return data;
}

/// Matroska XML for mkvmerge --chapters: ChapterTimeStart must be HH:MM:SS.nnnnnnnnn (not raw nanoseconds).
QString chapterTimeStartStringForMkvmerge(qint64 startNs)
{
    if (startNs < 0)
    {
        startNs = 0;
    }
    const qint64 secPart = startNs / 1000000000LL;
    const qint64 fracNs = startNs % 1000000000LL;
    const qint64 h = secPart / 3600;
    const int m = static_cast<int>((secPart % 3600) / 60);
    const int s = static_cast<int>(secPart % 60);
    const QString frac = QString::number(fracNs).rightJustified(9, QLatin1Char('0'));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(frac);
}

qint64 parseMatroskaTimeToNs(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
    {
        return -1;
    }
    bool ok = false;
    const qint64 plain = t.toLongLong(&ok);
    if (ok && plain >= 0)
    {
        return plain;
    }

    const QStringList parts = t.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3)
    {
        return -1;
    }

    bool okHours = true;
    bool okMinutes = true;
    bool okSeconds = true;
    qint64 hours = 0;
    qint64 minutes = 0;
    QString secToken;
    if (parts.size() == 3)
    {
        hours = parts.at(0).toLongLong(&okHours);
        minutes = parts.at(1).toLongLong(&okMinutes);
        secToken = parts.at(2);
    }
    else
    {
        minutes = parts.at(0).toLongLong(&okMinutes);
        secToken = parts.at(1);
    }
    if (!okHours || !okMinutes)
    {
        return -1;
    }

    int fracSeparator = secToken.indexOf(QLatin1Char('.'));
    if (fracSeparator < 0)
    {
        fracSeparator = secToken.indexOf(QLatin1Char(','));
    }
    const QString secWholeToken = fracSeparator >= 0 ? secToken.left(fracSeparator) : secToken;
    QString frac = fracSeparator >= 0 ? secToken.mid(fracSeparator + 1) : QString();
    const qint64 sec = secWholeToken.toLongLong(&okSeconds);
    if (!okSeconds)
    {
        return -1;
    }
    while (frac.length() < 9)
    {
        frac += QLatin1Char('0');
    }
    if (frac.length() > 9)
    {
        frac = frac.left(9);
    }
    const qint64 fracNs = frac.toLongLong();
    const qint64 secPart = hours * 3600LL + minutes * 60LL + sec;
    return fracNs + secPart * 1000000000LL;
}

void parseChapterAtomElement(const QDomElement& atomEl, QList<ChapterMarker>& out)
{
    qint64 startNs = -1;
    qint64 endNs = -1;
    QString title;
    QList<QDomElement> nestedAtoms;

    QDomNode n = atomEl.firstChild();
    while (!n.isNull())
    {
        if (n.isElement())
        {
            QDomElement e = n.toElement();
            if (e.tagName() == QLatin1String("ChapterTimeStart"))
            {
                startNs = parseMatroskaTimeToNs(e.text());
            }
            else if (e.tagName() == QLatin1String("ChapterTimeEnd"))
            {
                endNs = parseMatroskaTimeToNs(e.text());
            }
            else if (e.tagName() == QLatin1String("ChapterDisplay"))
            {
                QDomNode dn = e.firstChild();
                while (!dn.isNull())
                {
                    if (dn.isElement() && dn.toElement().tagName() == QLatin1String("ChapterString"))
                    {
                        title = dn.toElement().text();
                        break;
                    }
                    dn = dn.nextSibling();
                }
            }
            else if (e.tagName() == QLatin1String("ChapterAtom"))
            {
                nestedAtoms.append(e);
            }
        }
        n = n.nextSibling();
    }

    if (startNs >= 0)
    {
        ChapterMarker m;
        m.startNs = startNs;
        m.endNs = endNs;
        m.title = title.isEmpty() ? QStringLiteral("Chapter") : title;
        out.append(m);
    }
    for (const QDomElement& na : nestedAtoms)
    {
        parseChapterAtomElement(na, out);
    }
}

QDomElement findEditionEntry(const QDomElement& chaptersEl)
{
    QDomElement firstEdition;
    QDomElement defaultEdition;
    QDomNode n = chaptersEl.firstChild();
    while (!n.isNull())
    {
        if (n.isElement())
        {
            QDomElement e = n.toElement();
            if (e.tagName() == QLatin1String("EditionEntry"))
            {
                if (firstEdition.isNull())
                {
                    firstEdition = e;
                }
                QDomNode c = e.firstChild();
                while (!c.isNull())
                {
                    if (c.isElement())
                    {
                        QDomElement ce = c.toElement();
                        if (ce.tagName() == QLatin1String("EditionFlagDefault") &&
                            ce.text().trimmed() == QLatin1String("1"))
                        {
                            defaultEdition = e;
                            break;
                        }
                    }
                    c = c.nextSibling();
                }
            }
        }
        n = n.nextSibling();
    }
    if (!defaultEdition.isNull())
    {
        return defaultEdition;
    }
    return firstEdition;
}

QList<ChapterMarker> parseOgmChaptersData(const QByteArray& data)
{
    QList<ChapterMarker> out;
    QMap<int, QString> numToTime;
    QMap<int, QString> numToName;
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression timeRe(QStringLiteral(R"(CHAPTER(\d+)=([^;\r\n]+))"));
    static const QRegularExpression nameRe(QStringLiteral(R"(CHAPTER(\d+)NAME=(.+))"));
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        {
            const QRegularExpressionMatch m = nameRe.match(line);
            if (m.hasMatch())
            {
                numToName.insert(m.captured(1).toInt(), m.captured(2).trimmed());
                continue;
            }
        }
        {
            const QRegularExpressionMatch m = timeRe.match(line);
            if (m.hasMatch() && !line.contains(QLatin1String("NAME"), Qt::CaseInsensitive))
            {
                numToTime.insert(m.captured(1).toInt(), m.captured(2).trimmed());
            }
        }
    }
    QList<int> keys = numToTime.keys();
    std::sort(keys.begin(), keys.end());
    for (int k : keys)
    {
        const qint64 ns = parseMatroskaTimeToNs(numToTime.value(k));
        if (ns < 0)
        {
            continue;
        }
        ChapterMarker m;
        m.startNs = ns;
        m.endNs = -1;
        m.title = numToName.value(k);
        if (m.title.isEmpty())
        {
            m.title = QStringLiteral("Chapter");
        }
        out.append(m);
    }
    std::sort(out.begin(), out.end(),
              [](const ChapterMarker& a, const ChapterMarker& b) { return a.startNs < b.startNs; });
    return out;
}

QString uidFromIndex(int i)
{
    const QByteArray h = QCryptographicHash::hash(
        QByteArray::number(i) + QByteArray::number(QDateTime::currentMSecsSinceEpoch()), QCryptographicHash::Sha256);
    quint64 v = 0;
    for (int k = 0; k < 8 && k < h.size(); ++k)
    {
        v = (v << 8) | static_cast<unsigned char>(h[k]);
    }
    return QString::number(v);
}
} // namespace

QList<ChapterMarker> ChapterHelper::parseMatroskaChapterXmlData(const QByteArray& data)
{
    QList<ChapterMarker> out;
    QDomDocument doc;
    QString err;
    int errLine = 0;
    if (!doc.setContent(data, &err, &errLine))
    {
        return out;
    }
    QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("Chapters"))
    {
        return out;
    }
    QDomElement edition = findEditionEntry(root);
    if (edition.isNull())
    {
        return out;
    }

    QDomNode n = edition.firstChild();
    while (!n.isNull())
    {
        if (n.isElement() && n.toElement().tagName() == QLatin1String("ChapterAtom"))
        {
            parseChapterAtomElement(n.toElement(), out);
        }
        n = n.nextSibling();
    }

    std::sort(out.begin(), out.end(),
              [](const ChapterMarker& a, const ChapterMarker& b) { return a.startNs < b.startNs; });
    return out;
}

QList<ChapterMarker> ChapterHelper::parseMatroskaChapterXmlFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return parseMatroskaChapterXmlData(f.readAll());
}

QList<ChapterMarker> ChapterHelper::loadChaptersFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QByteArray data = stripUtf8Bom(f.readAll());
    const QByteArray trim = data.trimmed();
    if (trim.startsWith("<?xml") || data.toLower().contains("<chapters"))
    {
        return parseMatroskaChapterXmlData(data);
    }
    return parseOgmChaptersData(data);
}

bool ChapterHelper::extractEmbeddedChaptersToFile(const QString& mkvextractPath, const QString& mkvPath,
                                                  const QString& outXmlPath, ProcessManager* proc)
{
    if (!QFileInfo::exists(mkvextractPath) || !QFileInfo::exists(mkvPath))
    {
        return false;
    }
    QByteArray dummy;
    const QStringList args = {mkvPath, QStringLiteral("chapters"), outXmlPath};
    if (!proc->executeAndWait(mkvextractPath, args, dummy))
    {
        QFile::remove(outXmlPath);
        return false;
    }
    QFile test(outXmlPath);
    if (!test.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QByteArray data = stripUtf8Bom(test.readAll());
    test.close();
    if (data.trimmed().isEmpty())
    {
        QFile::remove(outXmlPath);
        return false;
    }

    // Same routing as loadChaptersFromFile (avoid rejecting valid Matroska XML: BOM broke old sniff;
    // substring "CHAPTER" exists only in OGM text format, not in XML tags like ChapterAtom).
    QList<ChapterMarker> markers;
    const QByteArray trim = data.trimmed();
    if (trim.startsWith("<?xml") || data.toLower().contains("<chapters"))
    {
        markers = parseMatroskaChapterXmlData(data);
    }
    else
    {
        markers = parseOgmChaptersData(data);
    }
    if (!markers.isEmpty())
    {
        return true;
    }

    const QByteArray low = data.left(2048).toLower();
    if (low.startsWith("<?xml") || low.contains("<chapters") || low.contains("chapteratom") ||
        low.contains("<editionentry") || data.contains("CHAPTER"))
    {
        return true;
    }
    QFile::remove(outXmlPath);
    return false;
}

QList<ChapterMarker> ChapterHelper::parseFfprobeChaptersJson(const QByteArray& json)
{
    QList<ChapterMarker> out;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject())
    {
        return out;
    }
    const QJsonArray chapters = doc.object().value(QStringLiteral("chapters")).toArray();
    for (const QJsonValue& v : chapters)
    {
        const QJsonObject o = v.toObject();
        auto parseJsonSeconds = [](const QJsonValue& value, bool* okOut) -> double
        {
            bool okLocal = false;
            double seconds = 0.0;
            if (value.isString())
            {
                seconds = value.toString().toDouble(&okLocal);
            }
            else if (value.isDouble())
            {
                seconds = value.toDouble();
                okLocal = true;
            }
            if (okOut)
            {
                *okOut = okLocal;
            }
            return seconds;
        };

        const QJsonValue st = o.value(QStringLiteral("start_time"));
        bool startOk = false;
        const double startSec = parseJsonSeconds(st, &startOk);
        if (!startOk)
        {
            continue;
        }

        const QJsonValue et = o.value(QStringLiteral("end_time"));
        bool endOk = false;
        const double endSec = parseJsonSeconds(et, &endOk);

        QString title;
        const QJsonObject tags = o.value(QStringLiteral("tags")).toObject();
        if (tags.contains(QStringLiteral("title")))
        {
            title = tags.value(QStringLiteral("title")).toString();
        }
        ChapterMarker m;
        m.startNs = static_cast<qint64>(startSec * 1e9);
        m.endNs = endOk ? static_cast<qint64>(endSec * 1e9) : -1;
        m.title = title.isEmpty() ? QStringLiteral("Chapter") : title;
        out.append(m);
    }
    std::sort(out.begin(), out.end(),
              [](const ChapterMarker& a, const ChapterMarker& b) { return a.startNs < b.startNs; });
    return out;
}

QList<ChapterTimingSeconds> ChapterHelper::buildChapterTimingSeconds(const QList<ChapterMarker>& chapters,
                                                                     qint64 durationNs)
{
    QList<ChapterTimingSeconds> out;
    if (chapters.isEmpty())
    {
        return out;
    }

    QList<ChapterMarker> sorted = chapters;
    std::sort(sorted.begin(), sorted.end(),
              [](const ChapterMarker& a, const ChapterMarker& b) { return a.startNs < b.startNs; });

    const qint64 defaultLastEndNs = sorted.last().startNs + 1000000000LL;
    const qint64 boundedDurationNs = durationNs > 0 ? durationNs : defaultLastEndNs;

    out.reserve(sorted.size());
    for (int i = 0; i < sorted.size(); ++i)
    {
        const ChapterMarker& current = sorted.at(i);
        const qint64 fallbackEndNs = (i + 1 < sorted.size()) ? sorted.at(i + 1).startNs : boundedDurationNs;
        const qint64 explicitEndNs = current.endNs;
        const qint64 rawEndNs = (explicitEndNs > current.startNs) ? explicitEndNs : fallbackEndNs;
        const qint64 safeEndNs = std::max(rawEndNs, current.startNs);

        ChapterTimingSeconds row;
        row.title = current.title;
        row.startSeconds = static_cast<int>(std::max<qint64>(0, current.startNs / 1000000000LL));
        row.endSeconds = static_cast<int>(std::max<qint64>(row.startSeconds, safeEndNs / 1000000000LL));
        out.append(row);
    }
    return out;
}

bool ChapterHelper::writeMatroskaChapterXml(const QList<ChapterMarker>& chapters, const QString& outPath)
{
    if (chapters.isEmpty())
    {
        return false;
    }
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ts << "<Chapters>\n  <EditionEntry>\n";
    ts << "    <EditionUID>1</EditionUID>\n";
    ts << "    <EditionFlagHidden>0</EditionFlagHidden>\n";
    ts << "    <EditionFlagDefault>1</EditionFlagDefault>\n";
    ts << "    <EditionFlagOrdered>0</EditionFlagOrdered>\n";
    for (int i = 0; i < chapters.size(); ++i)
    {
        const ChapterMarker& ch = chapters.at(i);
        ts << "    <ChapterAtom>\n";
        ts << "      <ChapterUID>" << uidFromIndex(i) << "</ChapterUID>\n";
        ts << "      <ChapterTimeStart>" << chapterTimeStartStringForMkvmerge(ch.startNs) << "</ChapterTimeStart>\n";
        ts << "      <ChapterFlagHidden>0</ChapterFlagHidden>\n";
        ts << "      <ChapterFlagEnabled>1</ChapterFlagEnabled>\n";
        ts << "      <ChapterDisplay>\n";
        ts << "        <ChapterString>" << QString(ch.title).toHtmlEscaped() << "</ChapterString>\n";
        ts << "        <ChapterLanguage>und</ChapterLanguage>\n";
        ts << "      </ChapterDisplay>\n";
        ts << "    </ChapterAtom>\n";
    }
    ts << "  </EditionEntry>\n</Chapters>\n";
    return true;
}

bool ChapterHelper::writeFfmetadata(const QList<ChapterMarker>& chapters, qint64 durationNs, const QString& outPath)
{
    if (chapters.isEmpty())
    {
        return false;
    }
    qint64 durNs = durationNs;
    if (durNs <= 0)
    {
        durNs = chapters.last().startNs + 1000000000LL; // +1s fallback
    }

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << ";FFMETADATA1\n";
    for (int i = 0; i < chapters.size(); ++i)
    {
        const qint64 start = chapters.at(i).startNs;
        const qint64 fallbackEnd = (i + 1 < chapters.size()) ? chapters.at(i + 1).startNs : durNs;
        const qint64 explicitEnd = chapters.at(i).endNs;
        const qint64 end = (explicitEnd > start) ? explicitEnd : fallbackEnd;
        if (end <= start)
        {
            continue;
        }
        ts << "[CHAPTER]\n";
        ts << "TIMEBASE=1/1000000000\n";
        ts << "START=" << start << "\n";
        ts << "END=" << end << "\n";
        QString t = chapters.at(i).title;
        t.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        t.replace(QLatin1Char('='), QLatin1String("\\="));
        t.replace(QLatin1Char('#'), QLatin1String("\\#"));
        t.replace(QLatin1Char(';'), QLatin1String("\\;"));
        ts << "title=" << t << "\n";
    }
    return true;
}

bool ChapterHelper::applyChaptersToMp4(const QString& mp4Path, const QList<ChapterMarker>& chapters, qint64 durationNs,
                                       const QString& ffmpegPath, ProcessManager* proc, QString* errorMessage)
{
    if (chapters.isEmpty() || !QFileInfo::exists(mp4Path) || ffmpegPath.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("invalid args");
        }
        return false;
    }
    QList<ChapterMarker> chaptersForMp4 = chapters;
    if (!chaptersForMp4.isEmpty() && chaptersForMp4.first().startNs > 0)
    {
        ChapterMarker lead;
        lead.startNs = 0;
        lead.title = QStringLiteral(" ");
        chaptersForMp4.prepend(lead);
    }

    const QString dir = QFileInfo(mp4Path).absolutePath();
    const QString ffmetaPath = QDir(dir).filePath(QStringLiteral("chapters_apply.ffmeta"));
    if (!writeFfmetadata(chaptersForMp4, durationNs, ffmetaPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("writeFfmetadata failed");
        }
        return false;
    }
    const QString tmpPath = mp4Path + QStringLiteral(".chapters_tmp.mp4");
    QFile::remove(tmpPath);

    // Remux with -map_chapters -1 so the MP4 has no chapter atoms / menu tracks before we mux ffmetadata.
    // (Encode pass may still leave chapter metadata in the file despite -map_chapters -1 on that command.)
    const QString stripPath = mp4Path + QStringLiteral(".strip_chapters.mp4");
    QFile::remove(stripPath);
    QByteArray stripErr;
    QStringList stripArgs;
    stripArgs << QStringLiteral("-y") << QStringLiteral("-i") << QFileInfo(mp4Path).absoluteFilePath()
              << QStringLiteral("-map") << QStringLiteral("0:v") << QStringLiteral("-map") << QStringLiteral("0:a")
              << QStringLiteral("-map_chapters") << QStringLiteral("-1") << QStringLiteral("-codec")
              << QStringLiteral("copy") << QFileInfo(stripPath).absoluteFilePath();
    bool stripOk = proc->executeAndWait(ffmpegPath, stripArgs, stripErr);
    if (!stripOk || !QFileInfo::exists(stripPath))
    {
        stripArgs.clear();
        stripArgs << QStringLiteral("-y") << QStringLiteral("-i") << QFileInfo(mp4Path).absoluteFilePath()
                  << QStringLiteral("-map") << QStringLiteral("0:v") << QStringLiteral("-map_chapters")
                  << QStringLiteral("-1") << QStringLiteral("-codec") << QStringLiteral("copy")
                  << QFileInfo(stripPath).absoluteFilePath();
        stripErr.clear();
        stripOk = proc->executeAndWait(ffmpegPath, stripArgs, stripErr);
    }
    const QString inputForChapterMux = (stripOk && QFileInfo::exists(stripPath))
                                           ? QFileInfo(stripPath).absoluteFilePath()
                                           : QFileInfo(mp4Path).absoluteFilePath();

    QStringList args;
    // Only video+audio; chapters from ffmetadata (input 1).
    args << QStringLiteral("-y") << QStringLiteral("-i") << inputForChapterMux << QStringLiteral("-i")
         << QFileInfo(ffmetaPath).absoluteFilePath() << QStringLiteral("-map") << QStringLiteral("0:v")
         << QStringLiteral("-map") << QStringLiteral("0:a") << QStringLiteral("-map_chapters") << QStringLiteral("1")
         << QStringLiteral("-codec") << QStringLiteral("copy") << QFileInfo(tmpPath).absoluteFilePath();

    QByteArray errOut;
    if (!proc->executeAndWait(ffmpegPath, args, errOut))
    {
        QFile::remove(tmpPath);
        QFile::remove(stripPath);
        if (errorMessage)
        {
            *errorMessage = QString::fromUtf8(errOut);
        }
        return false;
    }
    if (!QFileInfo::exists(tmpPath))
    {
        QFile::remove(stripPath);
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("output missing");
        }
        return false;
    }

    QFile::remove(mp4Path);
    if (!QFile::rename(tmpPath, mp4Path))
    {
        QFile::remove(stripPath);
        QFile::remove(tmpPath);
        QFile::remove(ffmetaPath);
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("rename failed");
        }
        return false;
    }
    QFile::remove(ffmetaPath);
    QFile::remove(stripPath);
    return true;
}
