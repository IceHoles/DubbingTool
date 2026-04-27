#include "postgenerator.h"

#include <QRegularExpression>

namespace
{
constexpr qsizetype kNoMatch = static_cast<qsizetype>(-1);

QString normalizeForStructureParsing(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text.trimmed();
}

qsizetype findSectionHeaderStart(const QString& postText, const QStringList& anchors)
{
    for (const QString& anchor : anchors)
    {
        const QString escaped = QRegularExpression::escape(anchor);
        const QRegularExpression rx(QString("(?im)^[^\\n]*\\b%1\\b[^\\n]*:").arg(escaped));
        const QRegularExpressionMatch match = rx.match(postText);
        if (match.hasMatch())
        {
            return match.capturedStart();
        }
    }
    return kNoMatch;
}

qsizetype findNextSectionHeaderStart(const QString& postText, qsizetype fromPos, const QStringList& allAnchors)
{
    qsizetype nextPos = kNoMatch;
    for (const QString& anchor : allAnchors)
    {
        const QString escaped = QRegularExpression::escape(anchor);
        const QRegularExpression rx(QString("(?im)^[^\\n]*\\b%1\\b[^\\n]*:").arg(escaped));
        const QRegularExpressionMatch match = rx.match(postText, fromPos);
        if (match.hasMatch())
        {
            const qsizetype pos = match.capturedStart();
            if (nextPos < 0 || pos < nextPos)
            {
                nextPos = pos;
            }
        }
    }
    return nextPos;
}

QString extractSectionValue(const QString& postText, const QStringList& anchors, const QStringList& allAnchors)
{
    const qsizetype sectionStart = findSectionHeaderStart(postText, anchors);
    if (sectionStart < 0)
    {
        return QString();
    }

    const qsizetype lineEnd = postText.indexOf('\n', sectionStart);
    const qsizetype headerEnd = (lineEnd >= 0) ? lineEnd : postText.size();
    const QString headerLine = postText.mid(sectionStart, headerEnd - sectionStart);
    const qsizetype colonIndex = headerLine.indexOf(':');
    const QString inlineValue = (colonIndex >= 0) ? headerLine.mid(colonIndex + 1).trimmed() : QString();
    if (!inlineValue.isEmpty())
    {
        return inlineValue;
    }

    const qsizetype bodyStart = (lineEnd >= 0) ? (lineEnd + 1) : headerEnd;
    if (bodyStart >= postText.size())
    {
        return QString();
    }

    qsizetype bodyEnd = findNextSectionHeaderStart(postText, bodyStart, allAnchors);
    if (bodyEnd < 0)
    {
        bodyEnd = postText.size();
    }

    QString value = postText.mid(bodyStart, bodyEnd - bodyStart).trimmed();
    const qsizetype hashtagsPos = value.indexOf(QRegularExpression("(?m)^\\s*#"));
    if (hashtagsPos >= 0)
    {
        value = value.left(hashtagsPos).trimmed();
    }
    return value;
}

void replaceFirstIfFound(QString& text, const QString& needle, const QString& replacement)
{
    if (needle.trimmed().isEmpty())
    {
        return;
    }
    const qsizetype pos = text.indexOf(needle);
    if (pos >= 0)
    {
        text.replace(pos, needle.size(), replacement);
    }
}

QString extractPlatformLink(const QString& postText, const QString& platformLabelPattern)
{
    const QString escaped = platformLabelPattern;

    // Markdown-like link: [Anime365](https://...)
    {
        const QRegularExpression markdownLinkRx(QString("(?im)\\[%1\\]\\((https?://[^\\s\\)]+)\\)").arg(escaped));
        const QRegularExpressionMatch match = markdownLinkRx.match(postText);
        if (match.hasMatch())
        {
            return match.captured(1).trimmed();
        }
    }

    // Parentheses style: Anime365 (https://...)
    {
        const QRegularExpression parenthesisRx(QString("(?im)%1\\s*\\((https?://[^\\s\\)]+)\\)").arg(escaped));
        const QRegularExpressionMatch match = parenthesisRx.match(postText);
        if (match.hasMatch())
        {
            return match.captured(1).trimmed();
        }
    }

    // Colon style: Anime365: https://...
    {
        const QRegularExpression colonRx(QString("(?im)%1\\s*:\\s*(https?://[^\\s\\n]+)").arg(escaped));
        const QRegularExpressionMatch match = colonRx.match(postText);
        if (match.hasMatch())
        {
            return match.captured(1).trimmed();
        }
    }

    // Next-line style:
    // Anime365
    // https://...
    {
        const QRegularExpression nextLineRx(QString("(?ims)%1\\s*\\n\\s*(https?://[^\\s\\n]+)").arg(escaped));
        const QRegularExpressionMatch match = nextLineRx.match(postText);
        if (match.hasMatch())
        {
            return match.captured(1).trimmed();
        }
    }

    return QString();
}
} // namespace

PostGenerator::PostGenerator(QObject* parent) : QObject(parent)
{
}

QMap<QString, PostVersions> PostGenerator::generate(const ReleaseTemplate& t, const EpisodeData& data)
{
    QMap<QString, PostVersions> generatedPosts;

    const QString seriesTitle = t.seriesTitleForPost.isEmpty() ? t.seriesTitle : t.seriesTitleForPost;
    const QString episodeStr = data.episodeNumber;
    const QString totalEpisodesStr = (t.totalEpisodes > 0) ? QString::number(t.totalEpisodes) : "?";
    const QString castStr = data.cast.join(", ");
    const QString directorStr = t.director;
    const QString soundStr = t.soundEngineer;
    const QString songStr = t.songsSoundEngineer;
    const QString episodeEngineerStr = t.episodeSoundEngineer;
    const QString recordingEngineerStr = t.recordingSoundEngineer;
    const QString subAuthorStr = t.subAuthor;
    const QString timingStr = t.timingAuthor;
    const QString signsAuthorStr = t.signsAuthor;
    const QString translationEditorStr = t.translationEditor;
    const QString builderStr = t.releaseBuilder;

    const QString anilibLink = data.viewLinks.value("Anilib", "");
    const QString anime365Link = data.viewLinks.value("Anime365", "");

    for (auto it = t.postTemplates.constBegin(); it != t.postTemplates.constEnd(); ++it)
    {
        QString markdownTemplate = it.value();

        QString markdownPost = markdownTemplate;
        markdownPost.replace("%SERIES_TITLE%", seriesTitle);
        markdownPost.replace("%EPISODE_NUMBER%", episodeStr);
        markdownPost.replace("%TOTAL_EPISODES%", totalEpisodesStr);
        markdownPost.replace("%CAST_LIST%", castStr);
        markdownPost.replace("%DIRECTOR%", directorStr);
        markdownPost.replace("%SOUND_ENGINEER%", soundStr);
        markdownPost.replace("%SONG_ENGINEER%", songStr);
        markdownPost.replace("%EPISODE_ENGINEER%", episodeEngineerStr);
        markdownPost.replace("%RECORDING_ENGINEER%", recordingEngineerStr);
        markdownPost.replace("%SUB_AUTHOR%", subAuthorStr);
        markdownPost.replace("%TIMING_AUTHOR%", timingStr);
        markdownPost.replace("%SIGNS_AUTHOR%", signsAuthorStr);
        markdownPost.replace("%TRANSLATION_EDITOR%", translationEditorStr);
        markdownPost.replace("%RELEASE_BUILDER%", builderStr);
        markdownPost.replace("%LINK_ANILIB%", anilibLink);
        markdownPost.replace("%LINK_ANIME365%", anime365Link);

        QString htmlPost = markdownPost;
        htmlPost.replace("&", "&").replace("<", "<").replace(">", ">");
        htmlPost.replace(QRegularExpression("\\*\\*(.*?)\\*\\*"), "<b>\\1</b>");
        htmlPost.replace(QRegularExpression("\\*(.*?)\\*"), "<i>\\1</i>");
        htmlPost.replace(QRegularExpression("__(.*?)__"), "<u>\\1</u>");
        htmlPost.replace(QRegularExpression("~~(.*?)~~"), "<s>\\1</s>");
        htmlPost.replace(QRegularExpression("\\|\\|(.*?)\\|\\|"),
                         "<span style='background-color: #555; color: #555;'>\\1</span>");
        htmlPost.replace(QRegularExpression("`(.*?)`"), "<code>\\1</code>");
        htmlPost.replace(QRegularExpression("\\[(.*?)\\]\\((.*?)\\)"), "<a href=\"\\2\">\\1</a>");
        htmlPost.replace("\n", "<br>");

        generatedPosts.insert(it.key(), {htmlPost, markdownPost});
    }

    return generatedPosts;
}

QStringList PostGenerator::supportedPlaceholders()
{
    return {"%SERIES_TITLE%",       "%EPISODE_NUMBER%",  "%TOTAL_EPISODES%", "%CAST_LIST%",
            "%DIRECTOR%",           "%SOUND_ENGINEER%",  "%SONG_ENGINEER%",  "%EPISODE_ENGINEER%",
            "%RECORDING_ENGINEER%", "%SUB_AUTHOR%",      "%TIMING_AUTHOR%",  "%SIGNS_AUTHOR%",
            "%TRANSLATION_EDITOR%", "%RELEASE_BUILDER%", "%LINK_ANILIB%",    "%LINK_ANIME365%"};
}

PostParseResult PostGenerator::parsePostToFields(const QString& postTextRaw, const QString& sourceType)
{
    PostParseResult result;
    const QString postText = normalizeForStructureParsing(postTextRaw);
    if (postText.isEmpty())
    {
        result.errors << "Текст поста пуст.";
        return result;
    }

    const QString normalizedSourceType = sourceType.trimmed().toLower();
    if (normalizedSourceType != "telegram" && normalizedSourceType != "vk")
    {
        result.errors << "Для парсинга поддерживаются только Telegram и VK посты.";
        return result;
    }

    QString templateText = postText;
    const QRegularExpression episodeRx("(?im)(серия\\s*[:№-]?\\s*)(\\d+)\\s*/\\s*(\\d+)");
    const QRegularExpressionMatch episodeMatch = episodeRx.match(postText);
    if (episodeMatch.hasMatch())
    {
        result.fields.insert("%EPISODE_NUMBER%", episodeMatch.captured(2).trimmed());
        result.fields.insert("%TOTAL_EPISODES%", episodeMatch.captured(3).trimmed());
        const QString episodeReplacement =
            QString("%1%2/%3").arg(episodeMatch.captured(1), "%EPISODE_NUMBER%", "%TOTAL_EPISODES%");
        templateText.replace(episodeMatch.capturedStart(0), episodeMatch.capturedLength(0), episodeReplacement);
    }

    const QRegularExpression titleRx("[«\"]([^»\"]+)[»\"]");
    const QRegularExpressionMatch titleMatch = titleRx.match(postText);
    if (titleMatch.hasMatch())
    {
        const QString title = titleMatch.captured(1).trimmed();
        result.fields.insert("%SERIES_TITLE%", title);
        replaceFirstIfFound(templateText, title, "%SERIES_TITLE%");
    }

    const QStringList allAnchors = {"роли дублировали",
                                    "роли озвучивали",
                                    "озвучивали роли",
                                    "режиссёр дубляжа",
                                    "режиссер дубляжа",
                                    "режиссёр",
                                    "режиссер",
                                    "куратор закадра",
                                    "звукорежиссёр",
                                    "звукорежиссер",
                                    "звукорежиссёр эпизода",
                                    "звукорежиссер эпизода",
                                    "звукорежиссёр записи",
                                    "звукорежиссер записи",
                                    "редактор перевода",
                                    "перевод",
                                    "разметка",
                                    "тайминг",
                                    "локализация надписей",
                                    "локализация видеоряда",
                                    "сборка релиза",
                                    "постер"};

    const QString castValue =
        extractSectionValue(postText, {"роли дублировали", "роли озвучивали", "озвучивали роли"}, allAnchors);
    if (!castValue.isEmpty())
    {
        result.fields.insert("%CAST_LIST%", castValue);
        replaceFirstIfFound(templateText, castValue, "%CAST_LIST%");
    }

    const QString directorValue = extractSectionValue(
        postText, {"режиссёр дубляжа", "режиссер дубляжа", "режиссёр", "режиссер", "куратор закадра"}, allAnchors);
    if (!directorValue.isEmpty())
    {
        result.fields.insert("%DIRECTOR%", directorValue);
        replaceFirstIfFound(templateText, directorValue, "%DIRECTOR%");
    }

    const QString soundValue = extractSectionValue(postText, {"звукорежиссёр", "звукорежиссер"}, allAnchors);
    if (!soundValue.isEmpty())
    {
        result.fields.insert("%SOUND_ENGINEER%", soundValue);
        replaceFirstIfFound(templateText, soundValue, "%SOUND_ENGINEER%");
    }

    const QString episodeEngineerValue =
        extractSectionValue(postText, {"звукорежиссёр эпизода", "звукорежиссер эпизода"}, allAnchors);
    if (!episodeEngineerValue.isEmpty())
    {
        result.fields.insert("%EPISODE_ENGINEER%", episodeEngineerValue);
        replaceFirstIfFound(templateText, episodeEngineerValue, "%EPISODE_ENGINEER%");
    }

    const QString recordingEngineerValue =
        extractSectionValue(postText, {"звукорежиссёр записи", "звукорежиссер записи"}, allAnchors);
    if (!recordingEngineerValue.isEmpty())
    {
        result.fields.insert("%RECORDING_ENGINEER%", recordingEngineerValue);
        replaceFirstIfFound(templateText, recordingEngineerValue, "%RECORDING_ENGINEER%");
    }

    const QString translationEditorValue = extractSectionValue(postText, {"редактор перевода"}, allAnchors);
    if (!translationEditorValue.isEmpty())
    {
        result.fields.insert("%TRANSLATION_EDITOR%", translationEditorValue);
        replaceFirstIfFound(templateText, translationEditorValue, "%TRANSLATION_EDITOR%");
    }

    const QString subAuthorValue = extractSectionValue(postText, {"перевод"}, allAnchors);
    if (!subAuthorValue.isEmpty())
    {
        result.fields.insert("%SUB_AUTHOR%", subAuthorValue);
        replaceFirstIfFound(templateText, subAuthorValue, "%SUB_AUTHOR%");
    }

    const QString timingValue = extractSectionValue(postText, {"разметка", "тайминг"}, allAnchors);
    if (!timingValue.isEmpty())
    {
        result.fields.insert("%TIMING_AUTHOR%", timingValue);
        replaceFirstIfFound(templateText, timingValue, "%TIMING_AUTHOR%");
    }

    const QString releaseBuilderValue = extractSectionValue(postText, {"сборка релиза"}, allAnchors);
    if (!releaseBuilderValue.isEmpty())
    {
        result.fields.insert("%RELEASE_BUILDER%", releaseBuilderValue);
        replaceFirstIfFound(templateText, releaseBuilderValue, "%RELEASE_BUILDER%");
    }

    const QString anime365Link = extractPlatformLink(postText, "Anime365");
    if (!anime365Link.isEmpty())
    {
        result.fields.insert("%LINK_ANIME365%", anime365Link);
        replaceFirstIfFound(templateText, anime365Link, "%LINK_ANIME365%");
    }

    const QString anilibLink = extractPlatformLink(postText, "AnimeLib(?:\\s*4[КK])?");
    if (!anilibLink.isEmpty())
    {
        result.fields.insert("%LINK_ANILIB%", anilibLink);
        replaceFirstIfFound(templateText, anilibLink, "%LINK_ANILIB%");
    }

    result.fields.insert("%PARSED_TEMPLATE_TEXT%", templateText);
    result.success = true;
    return result;
}
