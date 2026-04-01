#ifndef CHAPTERHELPER_H
#define CHAPTERHELPER_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

class ProcessManager;

struct ChapterMarker
{
    qint64 startNs = 0;
    qint64 endNs = -1;
    QString title;
};
Q_DECLARE_METATYPE(ChapterMarker)
Q_DECLARE_METATYPE(QList<ChapterMarker>)

struct ChapterTimingSeconds
{
    QString title;
    int startSeconds = 0;
    int endSeconds = 0;
};

namespace ChapterHelper
{
/// Parse Matroska chapter XML (file from mkvextract or external).
QList<ChapterMarker> parseMatroskaChapterXmlFile(const QString& path);
QList<ChapterMarker> parseMatroskaChapterXmlData(const QByteArray& data);

/// Auto-detect XML or legacy OGM-style chapter text (mkvextract).
QList<ChapterMarker> loadChaptersFromFile(const QString& path);

/// Extract embedded chapters from MKV to XML file; returns false on failure.
bool extractEmbeddedChaptersToFile(const QString& mkvextractPath, const QString& mkvPath, const QString& outXmlPath,
                                   ProcessManager* proc);

QList<ChapterMarker> parseFfprobeChaptersJson(const QByteArray& json);

/// Build second-based ranges from chapter starts.
QList<ChapterTimingSeconds> buildChapterTimingSeconds(const QList<ChapterMarker>& chapters, qint64 durationNs = 0);

/// Write XML suitable for mkvmerge --chapters
bool writeMatroskaChapterXml(const QList<ChapterMarker>& chapters, const QString& outPath);

/// FFmpeg ffmetadata format; duration used for last chapter end.
bool writeFfmetadata(const QList<ChapterMarker>& chapters, qint64 durationNs, const QString& outPath);

/// Remux MP4 with chapters from ffmetadata (stream copy). Returns false on error.
bool applyChaptersToMp4(const QString& mp4Path, const QList<ChapterMarker>& chapters, qint64 durationNs,
                        const QString& ffmpegPath, ProcessManager* proc, QString* errorMessage = nullptr);
} // namespace ChapterHelper

#endif
