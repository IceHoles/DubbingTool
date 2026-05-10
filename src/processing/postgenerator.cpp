#include "postgenerator.h"

#include <algorithm>

#include <QRegularExpression>

namespace
{
constexpr qsizetype kNoMatch = static_cast<qsizetype>(-1);

void stripLightMarkdownNoiseForCreditsParse(QString& text)
{
    text.replace(QRegularExpression(QStringLiteral("\\*+")), QString());
    text.replace(QRegularExpression(QStringLiteral("_+")), QString());
    // Do not strip backticks: they are inline monospace in the post body; removing them breaks
    // "Вставить пост и извлечь поля" after Telegram TD clipboard conversion.
    text.replace(QChar(0xFF1A), u':');
    text.remove(QChar(0x200B));
    text.remove(QChar(0xFEFF));
}

QString normalizeForStructureParsing(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    stripLightMarkdownNoiseForCreditsParse(text);
    return text.trimmed();
}

[[nodiscard]] qsizetype findSectionHeaderStart(const QString& postText, const QStringList& anchors,
                                               bool soundMainExcludeSongLine = false)
{
    for (const QString& anchor : anchors)
    {
        const QString escaped = QRegularExpression::escape(anchor);
        QString rxBody;
        if (soundMainExcludeSongLine &&
            (anchor.compare(QStringLiteral("звукорежиссёр"), Qt::CaseInsensitive) == 0 ||
             anchor.compare(QStringLiteral("звукорежиссер"), Qt::CaseInsensitive) == 0))
        {
            rxBody =
                QStringLiteral("(?im)^[^\n]*(?<![\\p{L}])%1(?!\\s*песен(?![\\p{L}]))(?![\\p{L}])[^\n]*[:：]")
                    .arg(escaped);
        }
        else
        {
            rxBody = QStringLiteral("(?im)^[^\n]*(?<![\\p{L}])%1(?![\\p{L}])[^\n]*[:：]").arg(escaped);
        }
        const QRegularExpression rx(rxBody);
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
        const QString rxBody =
            QStringLiteral("(?im)^[^\n]*(?<![\\p{L}])%1(?![\\p{L}])[^\n]*[:：]").arg(escaped);
        const QRegularExpression rx(rxBody);
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

struct CreditsSectionParse
{
    QString value;
    qsizetype spanStart = -1;
    qsizetype spanLength = 0;
};

CreditsSectionParse extractCreditsSection(const QString& postText, const QStringList& anchors, const QStringList& allAnchors,
                                          bool soundMainExcludeSongLine = false)
{
    CreditsSectionParse out;
    const qsizetype sectionStart = findSectionHeaderStart(postText, anchors, soundMainExcludeSongLine);
    if (sectionStart < 0)
    {
        return out;
    }

    const qsizetype lineEnd = postText.indexOf('\n', sectionStart);
    const qsizetype headerEnd = (lineEnd >= 0) ? lineEnd : postText.size();
    const QString headerLine = postText.mid(sectionStart, headerEnd - sectionStart);
    qsizetype colonPos = headerLine.indexOf(':');
    if (colonPos < 0)
    {
        colonPos = headerLine.indexOf(QChar(0xFF1A));
    }
    if (colonPos < 0)
    {
        return out;
    }
    const QString inlineValue = headerLine.mid(colonPos + 1).trimmed();
    if (!inlineValue.isEmpty())
    {
        out.value = inlineValue;
        const qsizetype valueStart = sectionStart + colonPos + 1;
        const QString rawInline = postText.mid(valueStart, headerEnd - valueStart);
        qsizetype lead = 0;
        while (lead < rawInline.size() && rawInline.at(lead).isSpace())
        {
            ++lead;
        }
        qsizetype trail = rawInline.size() - 1;
        while (trail >= lead && rawInline.at(trail).isSpace())
        {
            --trail;
        }
        if (trail < lead)
        {
            return out;
        }
        out.spanStart = valueStart + lead;
        out.spanLength = trail - lead + 1;
        return out;
    }

    const qsizetype bodyStart = (lineEnd >= 0) ? (lineEnd + 1) : headerEnd;
    if (bodyStart >= postText.size())
    {
        return out;
    }

    qsizetype bodyEnd = findNextSectionHeaderStart(postText, bodyStart, allAnchors);
    if (bodyEnd < 0)
    {
        bodyEnd = postText.size();
    }

    QString rawBody = postText.mid(bodyStart, bodyEnd - bodyStart);
    const qsizetype hashtagsPos = rawBody.indexOf(QRegularExpression(QStringLiteral("(?m)^\\s*#")));
    if (hashtagsPos >= 0)
    {
        rawBody = rawBody.left(hashtagsPos);
        bodyEnd = bodyStart + hashtagsPos;
    }

    qsizetype lead = 0;
    while (lead < rawBody.size() && rawBody.at(lead).isSpace())
    {
        ++lead;
    }
    qsizetype trail = rawBody.size() - 1;
    while (trail >= lead && rawBody.at(trail).isSpace())
    {
        --trail;
    }
    if (trail < lead)
    {
        return out;
    }

    out.spanStart = bodyStart + lead;
    out.spanLength = trail - lead + 1;
    out.value = rawBody.mid(lead, trail - lead + 1).trimmed();
    return out;
}

struct TemplateSpanOp
{
    qsizetype start = 0;
    qsizetype length = 0;
    QString replacement;
    QString fieldKey;
    QString fieldValue;
};

bool replaceFirstIfFound(QString& text, const QString& needle, const QString& replacement)
{
    if (needle.trimmed().isEmpty())
    {
        return false;
    }
    const qsizetype pos = text.indexOf(needle);
    if (pos < 0)
    {
        return false;
    }
    text.replace(pos, needle.size(), replacement);
    return true;
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

[[nodiscard]] QStringList creditsBlockAnchorList()
{
    return {QStringLiteral("роли дублировали"),
            QStringLiteral("роли озвучивали"),
            QStringLiteral("озвучивали роли"),
            QStringLiteral("режиссёр дубляжа"),
            QStringLiteral("режиссер дубляжа"),
            QStringLiteral("режиссёр"),
            QStringLiteral("режиссер"),
            QStringLiteral("куратор закадра"),
            QStringLiteral("звукорежиссёр песен"),
            QStringLiteral("звукорежиссер песен"),
            QStringLiteral("звукорежиссёр"),
            QStringLiteral("звукорежиссер"),
            QStringLiteral("звукорежиссёр эпизода"),
            QStringLiteral("звукорежиссер эпизода"),
            QStringLiteral("звукорежиссёр записи"),
            QStringLiteral("звукорежиссер записи"),
            QStringLiteral("редактор перевода"),
            QStringLiteral("перевод"),
            QStringLiteral("разметка"),
            QStringLiteral("тайминг"),
            QStringLiteral("локализация надписей"),
            QStringLiteral("локализация видеоряда"),
            QStringLiteral("локализация постера"),
            QStringLiteral("сборка релиза"),
            QStringLiteral("постер")};
}

[[nodiscard]] QString extractPosterLocalizationPlain(const QString& postText)
{
    const CreditsSectionParse sec =
        extractCreditsSection(postText, {QStringLiteral("локализация постера")}, creditsBlockAnchorList(), false);
    return sec.value.trimmed();
}

[[nodiscard]] bool shouldOmitVkHashtagToken(const QString& token)
{
    const QString t = token.trimmed().toLower();
    return t.startsWith(QStringLiteral("#дубрелиз")) || t.startsWith(QStringLiteral("#дубляж"));
}

[[nodiscard]] QString filterHashtagLineForVk(const QString& lineIn)
{
    QStringList kept;
    const QString line = lineIn.trimmed();
    for (const QString& raw : line.split(' ', Qt::SkipEmptyParts))
    {
        const QString tok = raw.trimmed();
        if (!tok.startsWith('#'))
        {
            continue;
        }
        if (!shouldOmitVkHashtagToken(tok))
        {
            kept << tok;
        }
    }
    return kept.join(QStringLiteral(" ")).trimmed();
}

[[nodiscard]] QString filteredTrailingHashtagBlockFromParsedTemplate(const QString& parsedTemplate)
{
    if (parsedTemplate.trimmed().isEmpty())
    {
        return QString();
    }
    const QStringList lines = parsedTemplate.split('\n');
    QStringList collected;
    for (qsizetype i = lines.size() - 1; i >= 0; --i)
    {
        const QString rawLine = lines.at(i);
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty())
        {
            if (!collected.isEmpty())
            {
                break;
            }
            continue;
        }
        if (trimmed.startsWith('#'))
        {
            const QString filtered = filterHashtagLineForVk(rawLine);
            if (!filtered.isEmpty())
            {
                collected.prepend(filtered);
            }
            else if (!collected.isEmpty())
            {
                break;
            }
        }
        else
        {
            if (!collected.isEmpty())
            {
                break;
            }
        }
    }
    return collected.join(QStringLiteral("\n")).trimmed();
}

[[nodiscard]] bool sourceLooksLikeTvoeSupportedRelease(const QString& postText)
{
    return postText.toLower().contains(QStringLiteral("tvoe"));
}

[[nodiscard]] QString builtinVkTvoePromoParagraph()
{
    return QStringLiteral(
        "🎁Сериал озвучен при поддержке онлайн-кинотеатра TVOЁ, если вы хотите поддержать Дубляжную, то смотрите нашу "
        "озвучку именно там, ведь TVOЁ дарит скидку нашим подписчикам по промокоду - Dublyazhnaya, где 1 месяц 99 "
        "рублей вместо 299 руб tvoe.cc/inby");
}

[[nodiscard]] QString parseFieldValueForVk(const PostParseResult& parseResult, const QString& key)
{
    return parseResult.fields.value(key).trimmed();
}

[[nodiscard]] QString buildVkPostTemplateFromParseResultImpl(const PostParseResult& parseResult,
                                                            const QString& sourcePostText)
{
    const QString parsedTemplateLower =
        parseFieldValueForVk(parseResult, QStringLiteral("%PARSED_TEMPLATE_TEXT%")).toLower();
    const QString seriesTitle = parseFieldValueForVk(parseResult, QStringLiteral("%SERIES_TITLE%"));
    const QString totalEpisodes = parseFieldValueForVk(parseResult, QStringLiteral("%TOTAL_EPISODES%"));
    const QString castList = parseFieldValueForVk(parseResult, QStringLiteral("%CAST_LIST%"));
    const QString director = parseFieldValueForVk(parseResult, QStringLiteral("%DIRECTOR%"));
    const QString soundEngineer = parseFieldValueForVk(parseResult, QStringLiteral("%SOUND_ENGINEER%"));
    const QString episodeEngineer = parseFieldValueForVk(parseResult, QStringLiteral("%EPISODE_ENGINEER%"));
    const QString recordingEngineer = parseFieldValueForVk(parseResult, QStringLiteral("%RECORDING_ENGINEER%"));
    const QString subAuthor = parseFieldValueForVk(parseResult, QStringLiteral("%SUB_AUTHOR%"));
    const QString timingAuthor = parseFieldValueForVk(parseResult, QStringLiteral("%TIMING_AUTHOR%"));
    const QString signsAuthor = parseFieldValueForVk(parseResult, QStringLiteral("%SIGNS_AUTHOR%"));
    const QString videoLocalization = parseFieldValueForVk(parseResult, QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%"));
    const QString releaseBuilder = parseFieldValueForVk(parseResult, QStringLiteral("%RELEASE_BUILDER%"));
    const bool isVoiceover = parsedTemplateLower.contains(QStringLiteral("закадр"));

    if (seriesTitle.isEmpty() && castList.isEmpty())
    {
        return QString();
    }

    QStringList lines;
    if (!seriesTitle.isEmpty())
    {
        lines << QStringLiteral("«%1» в %2 от ТО Дубляжная")
                     .arg(QStringLiteral("%SERIES_TITLE%"),
                          isVoiceover ? QStringLiteral("закадре") : QStringLiteral("дубляже"));
        lines << QString();
    }
    lines << QStringLiteral("Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%");
    lines << QString();
    if (sourceLooksLikeTvoeSupportedRelease(sourcePostText))
    {
        lines << builtinVkTvoePromoParagraph();
        lines << QString();
    }

    if (!castList.isEmpty())
    {
        lines << (isVoiceover ? QStringLiteral("Роли озвучивали:") : QStringLiteral("Роли дублировали:"));
        lines << QStringLiteral("%CAST_LIST%");
        lines << QString();
    }

    if (!director.isEmpty())
    {
        lines << (isVoiceover ? QStringLiteral("Куратор закадра:") : QStringLiteral("Режиссёр дубляжа:"));
        lines << QStringLiteral("%DIRECTOR%");
        lines << QString();
    }

    if (!episodeEngineer.isEmpty() || !recordingEngineer.isEmpty())
    {
        if (!episodeEngineer.isEmpty())
        {
            lines << QStringLiteral("Звукорежиссёр эпизода:");
            lines << QStringLiteral("%EPISODE_ENGINEER%");
            lines << QString();
        }
        if (!recordingEngineer.isEmpty())
        {
            lines << QStringLiteral("Звукорежиссёр записи:");
            lines << QStringLiteral("%RECORDING_ENGINEER%");
            lines << QString();
        }
    }
    else if (!soundEngineer.isEmpty())
    {
        lines << QStringLiteral("Звукорежиссёр:");
        lines << QStringLiteral("%SOUND_ENGINEER%");
        lines << QString();
    }

    if (!subAuthor.isEmpty())
    {
        lines << QStringLiteral("Перевод:");
        lines << QStringLiteral("%SUB_AUTHOR%");
        lines << QString();
    }

    if (!timingAuthor.isEmpty())
    {
        lines << QStringLiteral("Разметка:");
        lines << QStringLiteral("%TIMING_AUTHOR%");
        lines << QString();
    }

    if (!videoLocalization.isEmpty())
    {
        lines << QStringLiteral("Локализация видеоряда:");
        lines << QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%");
        lines << QString();
    }

    if (!signsAuthor.isEmpty())
    {
        lines << QStringLiteral("Локализация надписей:");
        lines << QStringLiteral("%SIGNS_AUTHOR%");
        lines << QString();
    }

    const QString posterAuthorPlain = PostGenerator::posterLocalizationPlainFromPostText(sourcePostText);
    lines << QStringLiteral("Локализация постера:");
    if (!posterAuthorPlain.isEmpty())
    {
        lines << posterAuthorPlain;
    }
    lines << QString();

    if (!releaseBuilder.isEmpty())
    {
        lines << QStringLiteral("Сборка релиза:");
        lines << QStringLiteral("%RELEASE_BUILDER%");
        lines << QString();
    }

    QString vk = lines.join('\n').trimmed();
    const QString tagBlock = filteredTrailingHashtagBlockFromParsedTemplate(
        parseFieldValueForVk(parseResult, QStringLiteral("%PARSED_TEMPLATE_TEXT%")));
    if (!tagBlock.isEmpty())
    {
        vk += QStringLiteral("\n\n%1").arg(tagBlock);
    }

    Q_UNUSED(totalEpisodes);
    return vk.trimmed();
}

} // namespace

QString PostGenerator::posterLocalizationPlainFromPostText(const QString& postTextRaw)
{
    const QString postText = normalizeForStructureParsing(postTextRaw);
    if (postText.isEmpty())
    {
        return QString();
    }
    return extractPosterLocalizationPlain(postText);
}

QString PostGenerator::buildVkPostTemplateFromParseResult(const PostParseResult& parseResult,
                                                          const QString& sourcePostText)
{
    return buildVkPostTemplateFromParseResultImpl(parseResult, sourcePostText);
}

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
    const QString videoLocalizationStr = t.videoLocalizationAuthor;
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
        markdownPost.replace("%VIDEO_LOCALIZATION_AUTHOR%", videoLocalizationStr);
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
            "%VIDEO_LOCALIZATION_AUTHOR%", "%TRANSLATION_EDITOR%", "%RELEASE_BUILDER%", "%LINK_ANILIB%",
            "%LINK_ANIME365%"};
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

    const QStringList allAnchors = creditsBlockAnchorList();

    QVector<TemplateSpanOp> spanOps;

    const QRegularExpression episodeRx(QStringLiteral("(?im)(серия\\s*[:№-]?\\s*)(\\d+)\\s*/\\s*(\\d+)"));
    const QRegularExpressionMatch episodeMatch = episodeRx.match(postText);
    if (episodeMatch.hasMatch())
    {
        TemplateSpanOp op;
        op.start = episodeMatch.capturedStart(0);
        op.length = episodeMatch.capturedLength(0);
        op.replacement = QString("%1%2/%3")
                             .arg(episodeMatch.captured(1), QStringLiteral("%EPISODE_NUMBER%"),
                                  QStringLiteral("%TOTAL_EPISODES%"));
        op.fieldKey = QStringLiteral("%EPISODE_NUMBER%");
        op.fieldValue = episodeMatch.captured(2).trimmed();
        spanOps.append(op);
        result.fields.insert(QStringLiteral("%EPISODE_NUMBER%"), op.fieldValue);
        result.fields.insert(QStringLiteral("%TOTAL_EPISODES%"), episodeMatch.captured(3).trimmed());
    }

    const QList<QRegularExpression> titlePatterns = {
        QRegularExpression(QStringLiteral("[«]([^»]+)[»]")),
        QRegularExpression(QString(QChar(0x201C)) + QStringLiteral("([^") + QChar(0x201D) + QStringLiteral("]+)") +
                           QChar(0x201D)),
        QRegularExpression(QString(QChar(0x2039)) + QStringLiteral("([^") + QChar(0x203A) + QStringLiteral("]+)") +
                           QChar(0x203A))};
    QRegularExpressionMatch titleMatch;
    bool hasTitleMatch = false;
    for (const QRegularExpression& rxTitle : titlePatterns)
    {
        const QRegularExpressionMatch m = rxTitle.match(postText);
        if (m.hasMatch())
        {
            titleMatch = m;
            hasTitleMatch = true;
            break;
        }
    }
    if (hasTitleMatch)
    {
        const QString title = titleMatch.captured(1).trimmed();
        if (!title.contains(QChar('%')))
        {
            TemplateSpanOp op;
            op.start = titleMatch.capturedStart(0);
            op.length = titleMatch.capturedLength(0);
            op.replacement = QStringLiteral("«%SERIES_TITLE%»");
            op.fieldKey = QStringLiteral("%SERIES_TITLE%");
            op.fieldValue = title;
            spanOps.append(op);
            result.fields.insert(QStringLiteral("%SERIES_TITLE%"), title);
        }
    }

    auto pushCreditsSection = [&](const QStringList& anchors, const QString& fieldKey, const QString& placeholder,
                                  bool soundMainExcludeSongLine = false)
    {
        const CreditsSectionParse sec =
            extractCreditsSection(postText, anchors, allAnchors, soundMainExcludeSongLine);
        if (sec.spanLength <= 0 || sec.value.trimmed().isEmpty())
        {
            return;
        }
        if (sec.value.contains(QChar('%')))
        {
            return;
        }
        TemplateSpanOp op;
        op.start = sec.spanStart;
        op.length = sec.spanLength;
        op.replacement = placeholder;
        op.fieldKey = fieldKey;
        op.fieldValue = sec.value;
        spanOps.append(op);
        result.fields.insert(fieldKey, sec.value);
    };

    pushCreditsSection({QStringLiteral("роли дублировали"), QStringLiteral("роли озвучивали"),
                        QStringLiteral("озвучивали роли")},
                       QStringLiteral("%CAST_LIST%"), QStringLiteral("%CAST_LIST%"));
    pushCreditsSection({QStringLiteral("режиссёр дубляжа"), QStringLiteral("режиссер дубляжа"), QStringLiteral("режиссёр"),
                        QStringLiteral("режиссер"), QStringLiteral("куратор закадра")},
                       QStringLiteral("%DIRECTOR%"), QStringLiteral("%DIRECTOR%"));
    pushCreditsSection({QStringLiteral("звукорежиссёр песен"), QStringLiteral("звукорежиссер песен")},
                       QStringLiteral("%SONG_ENGINEER%"), QStringLiteral("%SONG_ENGINEER%"));
    pushCreditsSection({QStringLiteral("звукорежиссёр"), QStringLiteral("звукорежиссер")}, QStringLiteral("%SOUND_ENGINEER%"),
                       QStringLiteral("%SOUND_ENGINEER%"), true);
    pushCreditsSection({QStringLiteral("звукорежиссёр эпизода"), QStringLiteral("звукорежиссер эпизода")},
                       QStringLiteral("%EPISODE_ENGINEER%"), QStringLiteral("%EPISODE_ENGINEER%"));
    pushCreditsSection({QStringLiteral("звукорежиссёр записи"), QStringLiteral("звукорежиссер записи")},
                       QStringLiteral("%RECORDING_ENGINEER%"), QStringLiteral("%RECORDING_ENGINEER%"));
    pushCreditsSection({QStringLiteral("редактор перевода")}, QStringLiteral("%TRANSLATION_EDITOR%"),
                       QStringLiteral("%TRANSLATION_EDITOR%"));
    pushCreditsSection({QStringLiteral("перевод")}, QStringLiteral("%SUB_AUTHOR%"), QStringLiteral("%SUB_AUTHOR%"));
    pushCreditsSection({QStringLiteral("разметка"), QStringLiteral("тайминг")}, QStringLiteral("%TIMING_AUTHOR%"),
                       QStringLiteral("%TIMING_AUTHOR%"));
    pushCreditsSection({QStringLiteral("локализация надписей")}, QStringLiteral("%SIGNS_AUTHOR%"),
                       QStringLiteral("%SIGNS_AUTHOR%"));
    pushCreditsSection({QStringLiteral("локализация видеоряда")}, QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%"),
                       QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%"));
    pushCreditsSection({QStringLiteral("сборка релиза")}, QStringLiteral("%RELEASE_BUILDER%"),
                       QStringLiteral("%RELEASE_BUILDER%"));

    std::sort(spanOps.begin(), spanOps.end(),
              [](const TemplateSpanOp& a, const TemplateSpanOp& b)
              {
                  return a.start > b.start;
              });

    QString working = postText;
    int mutationCount = 0;
    for (const TemplateSpanOp& op : spanOps)
    {
        if (op.start < 0 || op.length <= 0 || op.start + op.length > working.size())
        {
            continue;
        }
        working.replace(op.start, op.length, op.replacement);
        ++mutationCount;
    }

    const QString anime365Link = extractPlatformLink(postText, QStringLiteral("Anime365"));
    if (!anime365Link.isEmpty())
    {
        result.fields.insert(QStringLiteral("%LINK_ANIME365%"), anime365Link);
        if (replaceFirstIfFound(working, anime365Link, QStringLiteral("%LINK_ANIME365%")))
        {
            ++mutationCount;
        }
    }

    const QString anilibLink = extractPlatformLink(postText, QStringLiteral("AnimeLib(?:\\s*4[КK])?"));
    if (!anilibLink.isEmpty())
    {
        result.fields.insert(QStringLiteral("%LINK_ANILIB%"), anilibLink);
        if (replaceFirstIfFound(working, anilibLink, QStringLiteral("%LINK_ANILIB%")))
        {
            ++mutationCount;
        }
    }

    result.fields.insert(QStringLiteral("%PARSED_TEMPLATE_TEXT%"), working);
    result.structurePlaceholdersInParsedTemplate = mutationCount;
    result.success = true;
    return result;
}
