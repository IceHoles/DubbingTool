#ifndef POSTGENERATOR_H
#define POSTGENERATOR_H

#include "releasetemplate.h"

#include <QMap>
#include <QObject>

struct PostVersions
{
    QString html;
    QString markdown;
};

struct PostParseResult
{
    bool success = false;
    QMap<QString, QString> fields;
    QStringList errors;
};

struct EpisodeData
{
    QString episodeNumber;
    QStringList cast;
    QMap<QString, QString> viewLinks;
};

class PostGenerator : public QObject
{
    Q_OBJECT

public:
    explicit PostGenerator(QObject* parent = nullptr);
    QMap<QString, PostVersions> generate(const ReleaseTemplate& t, const EpisodeData& data);
    static QStringList supportedPlaceholders();
    static PostParseResult parsePostToFields(const QString& postText, const QString& sourceType);
};
#endif // POSTGENERATOR_H
