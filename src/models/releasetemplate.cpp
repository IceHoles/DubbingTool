#include "releasetemplate.h"
#include <QJsonArray>
#include <QJsonObject>


ReleaseTemplate::ReleaseTemplate() {}

// Вспомогательная функция для чтения QMap<QString, QUrl> из QJsonObject
static QMap<QString, QUrl> readUrlMap(const QJsonObject &json) {
    QMap<QString, QUrl> map;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        map.insert(it.key(), QUrl(it.value().toString()));
    }
    return map;
}

// Вспомогательная функция для записи QMap<QString, QUrl> в QJsonObject
static QJsonObject writeUrlMap(const QMap<QString, QUrl> &map) {
    QJsonObject json;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        json[it.key()] = it.value().toString();
    }
    return json;
}


void ReleaseTemplate::read(const QJsonObject &json)
{
    templateName = json["templateName"].toString();
    seriesTitle = json["seriesTitle"].toString();
    QJsonArray tagsArray = json["releaseTags"].toArray();
    for (const QJsonValue &value : tagsArray) {
        releaseTags.append(value.toString());
    }
    rssUrl = QUrl(json["rssUrl"].toString());
    animationStudio = json["animationStudio"].toString();
    subAuthor = json["subAuthor"].toString();
    originalLanguage = json["originalLanguage"].toString("jpn");
    endingChapterName = json["endingChapterName"].toString();
    endingStartTime = json["endingStartTime"].toString();
    useManualTime = json["useManualTime"].toBool(false);
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
    for (const QJsonValue &value : castArray) {
        cast.append(value.toString());
    }

    substitutions.clear();
    const QJsonObject substitutionsObj = json["substitutions"].toObject();
    for (const QString &key : substitutionsObj.keys()) {
        substitutions.insert(key, substitutionsObj[key].toString());
    }

    postTemplates.clear();
    const QJsonObject postTemplatesObj = json["postTemplates"].toObject();
    for (const QString &key : postTemplatesObj.keys()) {
        postTemplates.insert(key, postTemplatesObj[key].toString());
    }

    linkTemplates.clear();
    const QJsonObject linkTemplatesObj = json["linkTemplates"].toObject();
    for (const QString &key : linkTemplatesObj.keys()) {
        linkTemplates.insert(key, linkTemplatesObj[key].toString());
    }

    QJsonArray urlsArray = json["uploadUrls"].toArray();
    for (const QJsonValue &value : urlsArray) {
        uploadUrls.append(value.toString());
    }
}

void ReleaseTemplate::write(QJsonObject &json) const
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
    json["useManualTime"] = useManualTime;
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
    for (auto it = substitutions.constBegin(); it != substitutions.constEnd(); ++it) {
        substitutionsObj.insert(it.key(), it.value());
    }
    json["substitutions"] = substitutionsObj;

    QJsonObject postTemplatesObj;
    for (auto it = postTemplates.constBegin(); it != postTemplates.constEnd(); ++it) {
        postTemplatesObj.insert(it.key(), it.value());
    }
    json["postTemplates"] = postTemplatesObj;

    QJsonObject linkTemplatesObj;
    for (auto it = linkTemplates.constBegin(); it != linkTemplates.constEnd(); ++it) {
        linkTemplatesObj.insert(it.key(), it.value());
    }
    json["linkTemplates"] = linkTemplatesObj;
    json["uploadUrls"] = QJsonArray::fromStringList(uploadUrls);
}

void TbStyleInfo::read(const QJsonObject &json) {
    name = json["name"].toString("default_1080p");
    resolutionX = json["resolutionX"].toInt();
    tags = json["tags"].toString("{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs50\\shad3\\bord1.3\\4c&H000000&\\4a&H00&}");
    marginLeft = json["marginLeft"].toInt(10);
    marginRight = json["marginRight"].toInt(30);
    marginV = json["marginV"].toInt(10);
    styleName = json["styleName"].toString("Основной");
}

void TbStyleInfo::write(QJsonObject &json) const {
    json["name"] = name;
    json["resolutionX"] = resolutionX;
    json["tags"] = tags;
    json["marginLeft"] = marginLeft;
    json["marginRight"] = marginRight;
    json["marginV"] = marginV;
    json["styleName"] = styleName;
}
