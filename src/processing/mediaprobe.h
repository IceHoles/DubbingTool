#ifndef MEDIAPROBE_H
#define MEDIAPROBE_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

struct MediaTrackInfo
{
    int id = -1;
    QString type;    // "video", "audio", "subtitles"
    QString codec;   // e.g. "MPEG-4p10/AVC/h.264", "AAC"
    QString codecId; // e.g. "V_MPEG4/ISO/AVC", "A_AAC"
    QString language = QStringLiteral("und");
    QString trackName;
    int channels = 0;
    int typeIndex = 0; // 0-based index among tracks of the same type
};

struct MediaAttachmentInfo
{
    int id = -1;
    QString fileName;
    QString contentType;
};

struct MediaProbeResult
{
    bool ok = false;
    qint64 durationNs = 0;
    QList<MediaTrackInfo> videoTracks;
    QList<MediaTrackInfo> audioTracks;
    QList<MediaTrackInfo> subtitleTracks;
    QList<MediaAttachmentInfo> attachments;
};

class MediaProbe : public QObject
{
    Q_OBJECT

public:
    explicit MediaProbe(QObject* parent = nullptr);
    ~MediaProbe() override;

    static MediaProbeResult parseMkvMergeJson(const QByteArray& json);

    void probeFileAsync(const QString& mkvmergePath, const QString& filePath);
    void cancel();

signals:
    void finished(const QString& filePath, const MediaProbeResult& result);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess* m_process = nullptr;
    QString m_currentFilePath;
};

#endif // MEDIAPROBE_H
