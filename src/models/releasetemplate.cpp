#include "releasetemplate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

ReleaseTemplate::ReleaseTemplate()
{
}

ReleaseTemplate ReleaseTemplate::newTemplateDefaults()
{
    ReleaseTemplate t;
    t.templateName = QStringLiteral("Новый шаблон");
    t.seriesTitle = QStringLiteral("Как в архиве mkv, но без [DUB] и - 00.mkv");
    t.seriesTitleForPost = QStringLiteral("Как в постах, без кавычек");
    t.rssUrl = QUrl(QStringLiteral("https://example.com/rss.xml"));
    t.animationStudio = QStringLiteral("STUDIO (с шикимори или MAL)");
    t.subAuthor =
        QStringLiteral("Crunchyroll (или Имя Фамилия, если перевод свой + галочка внизу \"Свой перевод\")");
    t.originalLanguage = QStringLiteral("ja");
    t.endingChapterName = QStringLiteral("Ending Start");
    t.totalEpisodes = 12;

    t.director = QStringLiteral("Режиссер Дубляжа");
    t.soundEngineer = QStringLiteral("Звукорежиссер");
    t.timingAuthor = QStringLiteral("Разметка");
    t.releaseBuilder = QStringLiteral("Сборщик Релиза");
    t.cast << QStringLiteral("Актер 1") << QStringLiteral("Актер 2") << QStringLiteral("Актер 3");

    t.postTemplates[QStringLiteral("tg_mp4")] =
        QStringLiteral(
            "▶️Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
            "📌«%SERIES_TITLE%» в дубляже от ТО Дубляжная\n\n"
            "🎁Сериал озвучен при поддержке [онлайн-кинотеатра TVOЁ](https://tvoe.live/), если вы хотите поддержать "
            "Дубляжную, то смотрите нашу озвучку именно там, ведь TVOЁ дарит скидку нашим подписчикам по промокоду - "
            "`Dublyazhnaya`, где 1 месяц 99 рублей вместо 299 руб\n\n"
            "Помимо ТГ сериал можно посмотреть здесь:\n\n"
            "[TVOЁ](https://tvoe.live/p/) (~~299~~ 99 руб. по промокоду: `Dublyazhnaya`)\n\n"
            "[Архив MKV](https://t.me/+CVpSSg33UwI4MzYy)\n\n"
            "🎙Роли дублировали:\n%CAST_LIST%\n\n"
            "📝Режиссёр дубляжа:\n%DIRECTOR%\n\n"
            "🪄Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
            "📚Перевод:\n%SUB_AUTHOR%\n\n"
            "✏️Разметка:\n%TIMING_AUTHOR%\n\n"
            "✨Локализация постера:\nКирилл Хоримиев\n\n"
            "📦Сборка релиза:\n%RELEASE_BUILDER%\n\n"
            "#Дубрелиз@dublyajnaya #Хештег@dublyajnaya #Дубляж@dublyajnaya");
    t.postTemplates[QStringLiteral("tg_mkv")] =
        QStringLiteral("%SERIES_TITLE%\n"
                       "Серия %EPISODE_NUMBER%/%TOTAL_EPISODES%\n"
                       "#Хештег");
    t.postTemplates[QStringLiteral("vk")] =
        QStringLiteral("«%SERIES_TITLE%» в дубляже от ТО Дубляжная\n\n"
                       "Серия: %EPISODE_NUMBER%/%TOTAL_EPISODES%\n\n"
                       "Роли дублировали:\n%CAST_LIST%\n\n"
                       "Режиссёр дубляжа:\n%DIRECTOR%\n\n"
                       "Звукорежиссёр:\n%SOUND_ENGINEER%\n\n"
                       "Перевод:\n%SUB_AUTHOR%\n\n"
                       "Локализация видеоряда:\n%VIDEO_LOCALIZATION_AUTHOR%\n\n"
                       "Локализация надписей:\n%SIGNS_AUTHOR%\n\n"
                       "Локализация постера:\n\n"
                       "Сборка релиза:\n%RELEASE_BUILDER%\n\n"
                       "#Хештег@dublyajnaya");
    t.postTemplates[QStringLiteral("vk_comment")] =
        QStringLiteral(
            "А также вы можете поддержать наш коллектив на бусти: "
            "https://boosty.to/dubl/single-payment/donation/634652\n\n"
            "ТГ: https://t.me/dublyajnaya\n\n"
            "TVOЁ (99 руб. по промокоду: Dublyazhnaya): https://tvoe.live/p/");

    PostTemplateMeta tgMp4Meta;
    tgMp4Meta.title = QStringLiteral("Telegram (Канал)");
    tgMp4Meta.platform = QStringLiteral("telegram");
    tgMp4Meta.category = QStringLiteral("База");
    tgMp4Meta.tags = {QStringLiteral("tg"), QStringLiteral("mp4"), QStringLiteral("дубляж")};
    tgMp4Meta.sortOrder = 10;
    tgMp4Meta.builtin = true;
    t.postTemplateMeta.insert(QStringLiteral("tg_mp4"), tgMp4Meta);

    PostTemplateMeta tgMkvMeta = tgMp4Meta;
    tgMkvMeta.title = QStringLiteral("Telegram (Архив)");
    tgMkvMeta.tags = {QStringLiteral("tg"), QStringLiteral("mkv"), QStringLiteral("архив")};
    tgMkvMeta.sortOrder = 20;
    t.postTemplateMeta.insert(QStringLiteral("tg_mkv"), tgMkvMeta);

    PostTemplateMeta vkMeta;
    vkMeta.title = QStringLiteral("VK (Пост)");
    vkMeta.platform = QStringLiteral("vk");
    vkMeta.category = QStringLiteral("База");
    vkMeta.tags = {QStringLiteral("vk"), QStringLiteral("пост")};
    vkMeta.sortOrder = 30;
    vkMeta.builtin = true;
    t.postTemplateMeta.insert(QStringLiteral("vk"), vkMeta);

    PostTemplateMeta vkCommentMeta = vkMeta;
    vkCommentMeta.title = QStringLiteral("VK (Комментарий)");
    vkCommentMeta.tags = {QStringLiteral("vk"), QStringLiteral("комментарий")};
    vkCommentMeta.sortOrder = 40;
    t.postTemplateMeta.insert(QStringLiteral("vk_comment"), vkCommentMeta);

    t.uploadUrls << QStringLiteral("https://vk.com/dublyajnaya") << QStringLiteral("https://converter.kodikres.com/media-files")
                 << QStringLiteral("https://smotret-anime.app/") << QStringLiteral("https://v5.animelib.org/ru");

    return t;
}

void ReleaseTemplate::read(const QJsonObject& json)
{
    releaseTags.clear();
    cast.clear();
    uploadUrls.clear();
    templateName = json["templateName"].toString();
    seriesTitle = json["seriesTitle"].toString();
    QJsonArray tagsArray = json["releaseTags"].toArray();
    for (const QJsonValue& value : tagsArray)
    {
        releaseTags.append(value.toString());
    }
    rssUrl = QUrl(json["rssUrl"].toString());
    animationStudio = json["animationStudio"].toString();
    subAuthor = json["subAuthor"].toString();
    originalLanguage = json["originalLanguage"].toString("jpn");
    endingChapterName = json["endingChapterName"].toString().trimmed();
    endingStartTime = json["endingStartTime"].toString();
    if (json.contains(QStringLiteral("chaptersEnabled")))
    {
        chaptersEnabled = json[QStringLiteral("chaptersEnabled")].toBool(true);
    }
    else
    {
        // Миграция: старое поле expectsChapters или по умолчанию true
        chaptersEnabled =
            json.contains(QStringLiteral("expectsChapters")) ? json[QStringLiteral("expectsChapters")].toBool() : true;
    }
    useManualTime = json["useManualTime"].toBool(false);
    useOriginalAudio = json["useOriginalAudio"].toBool(true);
    generateTb = json["generateTb"].toBool(true);
    createSrtMaster = json["createSrtMaster"].toBool(false);
    isCustomTranslation = json["isCustomTranslation"].toBool(false);
    director = json["director"].toString();
    assistantDirector = json["assistantDirector"].toString();
    soundEngineer = json["soundEngineer"].toString();
    songsSoundEngineer = json["songsSoundEngineer"].toString();
    episodeSoundEngineer = json["episodeSoundEngineer"].toString();
    recordingSoundEngineer = json["recordingSoundEngineer"].toString();
    videoLocalizationAuthor = json["videoLocalizationAuthor"].toString();
    timingAuthor = json["timingAuthor"].toString();
    signsAuthor = json["signsAuthor"].toString();
    translationEditor = json["translationEditor"].toString();
    releaseBuilder = json["releaseBuilder"].toString();
    targetAudioFormat = json["targetAudioFormat"].toString("aac");
    forceSignStyleRequest = json["forceSignStyleRequest"].toBool(false);
    pauseForSubEdit = json["pauseForSubEdit"].toBool(false);
    useConcatRender = json["useConcatRender"].toBool(false);

    QString typeStr = json["voiceoverType"].toString("Dubbing");
    voiceoverType = (typeStr == "Voiceover") ? VoiceoverType::Voiceover : VoiceoverType::Dubbing;

    defaultTbStyleName = json["defaultTbStyleName"].toString("default_1080p");
    renderPresetName = json["renderPresetName"].toString("NVIDIA (HEVC NVENC)");
    sourceHasSubtitles = json["sourceHasSubtitles"].toBool(true);
    seriesTitleForPost = json["seriesTitleForPost"].toString();
    totalEpisodes = json["totalEpisodes"].toInt();
    posterPath = json["posterPath"].toString();

    QJsonArray castArray = json["cast"].toArray();
    for (const QJsonValue& value : castArray)
    {
        cast.append(value.toString());
    }

    substitutions.clear();
    const QJsonObject substitutionsObj = json["substitutions"].toObject();
    for (const QString& key : substitutionsObj.keys())
    {
        substitutions.insert(key, substitutionsObj[key].toString());
    }

    postTemplates.clear();
    const QJsonObject postTemplatesObj = json["postTemplates"].toObject();
    for (const QString& key : postTemplatesObj.keys())
    {
        postTemplates.insert(key, postTemplatesObj[key].toString());
    }

    postTemplateMeta.clear();
    const QJsonObject postTemplateMetaObj = json["postTemplateMeta"].toObject();
    for (const QString& key : postTemplateMetaObj.keys())
    {
        PostTemplateMeta meta;
        meta.read(postTemplateMetaObj.value(key).toObject());
        postTemplateMeta.insert(key, meta);
    }

    // Backward compatibility: infer basic metadata for old templates without postTemplateMeta.
    for (auto it = postTemplates.constBegin(); it != postTemplates.constEnd(); ++it)
    {
        if (postTemplateMeta.contains(it.key()))
        {
            continue;
        }

        PostTemplateMeta inferred;
        inferred.title = it.key();
        if (it.key().startsWith("tg_"))
        {
            inferred.platform = "telegram";
        }
        else if (it.key().startsWith("vk"))
        {
            inferred.platform = "vk";
        }
        else
        {
            inferred.platform = "other";
        }
        inferred.category = "Общие";
        postTemplateMeta.insert(it.key(), inferred);
    }

    linkTemplates.clear();
    const QJsonObject linkTemplatesObj = json["linkTemplates"].toObject();
    for (const QString& key : linkTemplatesObj.keys())
    {
        linkTemplates.insert(key, linkTemplatesObj[key].toString());
    }

    QJsonArray urlsArray = json["uploadUrls"].toArray();
    for (const QJsonValue& value : urlsArray)
    {
        uploadUrls.append(value.toString());
    }
}

void ReleaseTemplate::write(QJsonObject& json) const
{
    json["templateName"] = templateName;
    json["seriesTitle"] = seriesTitle;
    json["releaseTags"] = QJsonArray::fromStringList(releaseTags);
    json["rssUrl"] = rssUrl.toString();
    json["animationStudio"] = animationStudio;
    json["subAuthor"] = subAuthor;
    json["originalLanguage"] = originalLanguage;
    json["endingChapterName"] = endingChapterName;
    json["endingStartTime"] = endingStartTime;
    json[QStringLiteral("chaptersEnabled")] = chaptersEnabled;
    json["useManualTime"] = useManualTime;
    json["useOriginalAudio"] = useOriginalAudio;
    json["generateTb"] = generateTb;
    json["createSrtMaster"] = createSrtMaster;
    json["isCustomTranslation"] = isCustomTranslation;
    json["director"] = director;
    json["assistantDirector"] = assistantDirector;
    json["soundEngineer"] = soundEngineer;
    json["songsSoundEngineer"] = songsSoundEngineer;
    json["episodeSoundEngineer"] = episodeSoundEngineer;
    json["recordingSoundEngineer"] = recordingSoundEngineer;
    json["videoLocalizationAuthor"] = videoLocalizationAuthor;
    json["timingAuthor"] = timingAuthor;
    json["signsAuthor"] = signsAuthor;
    json["translationEditor"] = translationEditor;
    json["releaseBuilder"] = releaseBuilder;
    json["cast"] = QJsonArray::fromStringList(cast);
    json["signStyles"] = QJsonArray::fromStringList(signStyles);
    json["targetAudioFormat"] = targetAudioFormat;
    json["forceSignStyleRequest"] = forceSignStyleRequest;
    json["pauseForSubEdit"] = pauseForSubEdit;
    json["useConcatRender"] = useConcatRender;

    json["voiceoverType"] = (voiceoverType == VoiceoverType::Voiceover) ? "Voiceover" : "Dubbing";

    json["defaultTbStyleName"] = defaultTbStyleName;
    json["renderPresetName"] = renderPresetName;
    json["sourceHasSubtitles"] = sourceHasSubtitles;
    json["isCustomTranslation"] = isCustomTranslation;
    json["seriesTitleForPost"] = seriesTitleForPost;
    json["totalEpisodes"] = totalEpisodes;
    json["posterPath"] = posterPath;

    QJsonObject substitutionsObj;
    for (auto it = substitutions.constBegin(); it != substitutions.constEnd(); ++it)
    {
        substitutionsObj.insert(it.key(), it.value());
    }
    json["substitutions"] = substitutionsObj;

    QJsonObject postTemplatesObj;
    for (auto it = postTemplates.constBegin(); it != postTemplates.constEnd(); ++it)
    {
        postTemplatesObj.insert(it.key(), it.value());
    }
    json["postTemplates"] = postTemplatesObj;

    QJsonObject postTemplateMetaObj;
    for (auto it = postTemplateMeta.constBegin(); it != postTemplateMeta.constEnd(); ++it)
    {
        QJsonObject metaObj;
        it.value().write(metaObj);
        postTemplateMetaObj.insert(it.key(), metaObj);
    }
    json["postTemplateMeta"] = postTemplateMetaObj;

    QJsonObject linkTemplatesObj;
    for (auto it = linkTemplates.constBegin(); it != linkTemplates.constEnd(); ++it)
    {
        linkTemplatesObj.insert(it.key(), it.value());
    }
    json["linkTemplates"] = linkTemplatesObj;
    json["uploadUrls"] = QJsonArray::fromStringList(uploadUrls);
}

void TbStyleInfo::read(const QJsonObject& json)
{
    name = json["name"].toString("default_1080p");
    resolutionX = json["resolutionX"].toInt();
    tags = json["tags"].toString("{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs50\\shad3\\bord1.3\\4c&H000000&\\4a&H00&}");
    marginLeft = json["marginLeft"].toInt(10);
    marginRight = json["marginRight"].toInt(30);
    marginV = json["marginV"].toInt(10);
    styleName = json["styleName"].toString("Основной");
}

void PostTemplateMeta::read(const QJsonObject& json)
{
    title = json["title"].toString();
    platform = json["platform"].toString();
    category = json["category"].toString();
    sortOrder = json["sortOrder"].toInt(0);
    builtin = json["builtin"].toBool(false);
    tags.clear();
    const QJsonArray tagsArray = json["tags"].toArray();
    for (const QJsonValue& value : tagsArray)
    {
        tags.append(value.toString());
    }
}

void PostTemplateMeta::write(QJsonObject& json) const
{
    json["title"] = title;
    json["platform"] = platform;
    json["category"] = category;
    json["sortOrder"] = sortOrder;
    json["builtin"] = builtin;
    json["tags"] = QJsonArray::fromStringList(tags);
}

void TbStyleInfo::write(QJsonObject& json) const
{
    json["name"] = name;
    json["resolutionX"] = resolutionX;
    json["tags"] = tags;
    json["marginLeft"] = marginLeft;
    json["marginRight"] = marginRight;
    json["marginV"] = marginV;
    json["styleName"] = styleName;
}
