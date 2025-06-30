#ifndef POSTGENERATOR_H
#define POSTGENERATOR_H

#include <QObject>
#include <QMap>
#include "releasetemplate.h"

struct PostVersions {
    QString html;
    QString markdown;
};

struct EpisodeData {
    QString episodeNumber;
    QStringList cast;
    QMap<QString, QString> viewLinks;
};

class PostGenerator : public QObject
{
    Q_OBJECT
public:
    explicit PostGenerator(QObject *parent = nullptr);
    QMap<QString, PostVersions> generate(const ReleaseTemplate& t, const EpisodeData& data);
};
#endif // POSTGENERATOR_H
