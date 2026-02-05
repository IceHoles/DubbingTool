#include "postgenerator.h"
#include <QRegularExpression>


PostGenerator::PostGenerator(QObject *parent) : QObject(parent) {}

QMap<QString, PostVersions> PostGenerator::generate(const ReleaseTemplate &t, const EpisodeData &data)
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

    for(auto it = t.postTemplates.constBegin(); it != t.postTemplates.constEnd(); ++it) {
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
        htmlPost.replace(QRegularExpression("\\|\\|(.*?)\\|\\|"), "<span style='background-color: #555; color: #555;'>\\1</span>");
        htmlPost.replace(QRegularExpression("`(.*?)`"), "<code>\\1</code>");
        htmlPost.replace(QRegularExpression("\\[(.*?)\\]\\((.*?)\\)"), "<a href=\"\\2\">\\1</a>");
        htmlPost.replace("\n", "<br>");

        generatedPosts.insert(it.key(), {htmlPost, markdownPost});
    }

    return generatedPosts;
}
