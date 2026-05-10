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
    /// Successful structured replacements (section spans + links) while building %PARSED_TEMPLATE_TEXT%.
    int structurePlaceholdersInParsedTemplate = 0;
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
    [[nodiscard]] static QString posterLocalizationPlainFromPostText(const QString& postText);
    /**
     * @brief VK post body from parse result and original source text (poster author as plain text, not a placeholder).
     */
    [[nodiscard]] static QString buildVkPostTemplateFromParseResult(const PostParseResult& parseResult,
                                                                  const QString& sourcePostText);
};
#endif // POSTGENERATOR_H
