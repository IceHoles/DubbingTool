#include "mediaprobe.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

MediaProbe::MediaProbe(QObject* parent) : QObject(parent)
{
}

MediaProbe::~MediaProbe()
{
    cancel();
}

MediaProbeResult MediaProbe::parseMkvMergeJson(const QByteArray& json)
{
    MediaProbeResult result;
    if (json.trimmed().isEmpty())
    {
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return result;
    }

    const QJsonObject root = doc.object();
    result.ok = true;

    if (root.contains(QStringLiteral("container")))
    {
        const QJsonObject container = root[QStringLiteral("container")].toObject();
        if (container.contains(QStringLiteral("properties")))
        {
            const QJsonObject props = container[QStringLiteral("properties")].toObject();
            if (props.contains(QStringLiteral("duration")))
            {
                result.durationNs = static_cast<qint64>(props[QStringLiteral("duration")].toDouble());
            }
        }
    }

    int videoCounter = 0;
    int audioCounter = 0;
    int subsCounter = 0;

    if (root.contains(QStringLiteral("tracks")))
    {
        const QJsonArray tracks = root[QStringLiteral("tracks")].toArray();
        for (const QJsonValue& v : tracks)
        {
            const QJsonObject track = v.toObject();
            MediaTrackInfo info;
            info.id = track[QStringLiteral("id")].toInt();
            info.type = track[QStringLiteral("type")].toString();
            info.codec = track[QStringLiteral("codec")].toString();

            const QJsonObject props = track[QStringLiteral("properties")].toObject();
            info.codecId = props[QStringLiteral("codec_id")].toString();
            info.language = props.contains(QStringLiteral("language")) ? props[QStringLiteral("language")].toString()
                                                                       : QStringLiteral("und");
            info.trackName = props[QStringLiteral("track_name")].toString();
            info.channels = props[QStringLiteral("audio_channels")].toInt();

            if (info.type == QStringLiteral("video"))
            {
                info.typeIndex = videoCounter++;
                result.videoTracks.append(info);
            }
            else if (info.type == QStringLiteral("audio"))
            {
                info.typeIndex = audioCounter++;
                result.audioTracks.append(info);
            }
            else if (info.type == QStringLiteral("subtitles"))
            {
                info.typeIndex = subsCounter++;
                result.subtitleTracks.append(info);
            }
        }
    }

    if (root.contains(QStringLiteral("attachments")))
    {
        const QJsonArray atts = root[QStringLiteral("attachments")].toArray();
        for (const QJsonValue& v : atts)
        {
            const QJsonObject att = v.toObject();
            MediaAttachmentInfo info;
            info.id = att[QStringLiteral("id")].toInt();
            info.fileName = att[QStringLiteral("file_name")].toString();
            info.contentType = att[QStringLiteral("content_type")].toString();
            result.attachments.append(info);
        }
    }

    return result;
}

void MediaProbe::probeFileAsync(const QString& mkvmergePath, const QString& filePath)
{
    cancel();
    m_currentFilePath = filePath;

    if (mkvmergePath.isEmpty() || !QFileInfo::exists(mkvmergePath) || filePath.isEmpty() ||
        !QFileInfo::exists(filePath))
    {
        MediaProbeResult res;
        res.ok = false;
        emit finished(m_currentFilePath, res);
        return;
    }

    m_process = new QProcess(this);
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                if (error == QProcess::FailedToStart)
                {
                    if (m_process)
                    {
                        m_process->disconnect(this);
                        m_process->deleteLater();
                        m_process = nullptr;
                    }
                    MediaProbeResult res;
                    res.ok = false;
                    emit finished(m_currentFilePath, res);
                }
            });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &MediaProbe::onProcessFinished);
    m_process->start(mkvmergePath, {QStringLiteral("--identify"), QStringLiteral("--identification-format"),
                                    QStringLiteral("json"), filePath});
}

void MediaProbe::cancel()
{
    if (m_process)
    {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void MediaProbe::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_process)
    {
        return;
    }

    const QByteArray output = m_process->readAllStandardOutput();
    m_process->deleteLater();
    m_process = nullptr;

    if ((exitCode == 0 || exitCode == 1) && exitStatus == QProcess::NormalExit)
    {
        MediaProbeResult result = parseMkvMergeJson(output);
        emit finished(m_currentFilePath, result);
    }
    else
    {
        MediaProbeResult result;
        result.ok = false;
        emit finished(m_currentFilePath, result);
    }
}
