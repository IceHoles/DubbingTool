/**
 * @file postgenerator_test.cpp
 * @brief Unit tests for PostGenerator::parsePostToFields (Telegram / VK post bodies).
 */

#include <QtTest/QtTest>
#include <QCoreApplication>

#include "postgenerator.h"

class PostGeneratorTest : public QObject
{
    Q_OBJECT

private slots:
    void tg_fullDubbingPost_extractsAllCredits();
    void tg_voiceover_castAnchor();
    void tg_markdownBoldAroundHeaders();
    void tg_title_guillemets();
    void tg_title_curlyQuotes();
    void tg_minimalEpisodeAndTitle();
    void tg_placeholderTitleInQuotes_skipsTitleField();
    void vk_sameBody_asTelegram();
    void tg_anime365MarkdownLink();
    void tg_emptyInput_errors();
    void invalidSourceType_errors();
    void tg_fullwidthColonHeader();
    void tg_episodeSoundEngineer();
    void tg_recordingSoundEngineer();
    void tg_translationEditor();
    void tg_songAndMainSoundEngineers_distinct();
    void tg_onlySongSoundEngineer();
    void tg_anilib_markdownFourK();
    void tg_anilib_parentheses();
    void tg_anilib_colon();
    void tg_anilib_nextLine();
    void tg_anime365_parentheses();
    void tg_anime365_colon();
    void tg_anime365_nextLine();
    void tg_ozvuchivaliRoli_anchor();
    void tg_timingAnchor();
    void tg_localizationVideoRow();
    void tg_localizationCaptions();
    void tg_inlineDirector_sameLine();
    void tg_castTruncatedBeforeHashtagBlock();
    void tg_episodeLine_numberSign();
    void tg_megaPost_allSupportedFields();
    void tg_mainSoundBeforeSong_inPost();
    void tg_razmetkaHeader();
    void vk_megaPost_matchesTelegram();
    void poster_plainFromPostText_extractsName();
    void vk_buildFromParse_includesPosterAuthorAsPlainText();
    void vk_buildFromParse_posterEmpty_stillHasBlockBeforeRelease();
    void tg_parseFromClipboardPath_preservesInlineBackticksInParsedTemplate();
};

namespace
{

[[nodiscard]] QString fullDubbingPost()
{
    return QStringLiteral(
        "▶️Серия: 5/13\n\n"
        "📌«Левша Эрен» в дубляже от ТО Дубляжная\n\n"
        "🎙Роли дублировали:\n"
        "Кирилл Кузнецов, Алина Стародубцева\n\n"
        "📝Режиссёр дубляжа:\n"
        "Мари Тристан\n\n"
        "🪄Звукорежиссёр:\n"
        "Богдан Георгияди\n\n"
        "📚Перевод:\n"
        "Crunchyroll\n\n"
        "✏️Разметка:\n"
        "Виолетта Парсигян\n\n"
        "✨Локализация надписей:\n"
        "Автор Надписей\n\n"
        "✨Локализация постера:\n"
        "Кирилл Хоримиев\n\n"
        "📦Сборка релиза:\n"
        "Иван Чезганов\n\n"
        "#Дубрелиз@dublyajnaya #Тест@dublyajnaya");
}

[[nodiscard]] QString megaPostAllFieldsBody()
{
    return QStringLiteral(
        "Серия: 2/20\n\n"
        "«Mega Title» в дубляже\n\n"
        "[AnimeLib 4K](https://anilib.example/m) [Anime365](https://anime365.example/m)\n\n"
        "Озвучивали роли:\n"
        "Actor A, Actor B\n\n"
        "Режиссёр дубляжа:\n"
        "Director X\n\n"
        "Звукорежиссёр песен:\n"
        "Song Eng\n\n"
        "Звукорежиссёр:\n"
        "Main Sound\n\n"
        "Звукорежиссёр эпизода:\n"
        "Ep Sound\n\n"
        "Звукорежиссёр записи:\n"
        "Rec Sound\n\n"
        "Редактор перевода:\n"
        "Trans Editor\n\n"
        "Перевод:\n"
        "Subber\n\n"
        "Тайминг:\n"
        "Timer\n\n"
        "Локализация надписей:\n"
        "Captions Editor\n\n"
        "Локализация видеоряда:\n"
        "Video Loc\n\n"
        "Сборка релиза:\n"
        "Builder B\n\n");
}

} // namespace

void PostGeneratorTest::tg_fullDubbingPost_extractsAllCredits()
{
    const PostParseResult r = PostGenerator::parsePostToFields(fullDubbingPost(), QStringLiteral("telegram"));
    QVERIFY(r.success);
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_NUMBER%")), QStringLiteral("5"));
    QCOMPARE(r.fields.value(QStringLiteral("%TOTAL_EPISODES%")), QStringLiteral("13"));
    QCOMPARE(r.fields.value(QStringLiteral("%SERIES_TITLE%")), QStringLiteral("Левша Эрен"));
    QVERIFY(r.fields.value(QStringLiteral("%CAST_LIST%")).contains(QStringLiteral("Кузнецов")));
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Мари Тристан"));
    QCOMPARE(r.fields.value(QStringLiteral("%SOUND_ENGINEER%")), QStringLiteral("Богдан Георгияди"));
    QCOMPARE(r.fields.value(QStringLiteral("%SUB_AUTHOR%")), QStringLiteral("Crunchyroll"));
    QCOMPARE(r.fields.value(QStringLiteral("%TIMING_AUTHOR%")), QStringLiteral("Виолетта Парсигян"));
    QCOMPARE(r.fields.value(QStringLiteral("%SIGNS_AUTHOR%")), QStringLiteral("Автор Надписей"));
    QCOMPARE(r.fields.value(QStringLiteral("%RELEASE_BUILDER%")), QStringLiteral("Иван Чезганов"));

    const QString tpl = r.fields.value(QStringLiteral("%PARSED_TEMPLATE_TEXT%"));
    QVERIFY(tpl.contains(QStringLiteral("%CAST_LIST%")));
    QVERIFY(tpl.contains(QStringLiteral("%DIRECTOR%")));
    QVERIFY(tpl.contains(QStringLiteral("%SERIES_TITLE%")));
    QVERIFY(r.structurePlaceholdersInParsedTemplate >= 3);
}

void PostGeneratorTest::tg_voiceover_castAnchor()
{
    const QString post = QStringLiteral(
        "Серия: 2/24\n\n"
        "«Герой» в закадре\n\n"
        "Роли озвучивали:\n"
        "Ann One, Ann Two\n\n"
        "Куратор закадра:\n"
        "Boss Name\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QVERIFY(r.fields.value(QStringLiteral("%CAST_LIST%")).contains(QStringLiteral("Ann One")));
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Boss Name"));
}

void PostGeneratorTest::tg_markdownBoldAroundHeaders()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "**Роли дублировали:**\n"
        "Only Actor\n\n"
        "**Режиссёр дубляжа:**\n"
        "Dir Here\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%CAST_LIST%")), QStringLiteral("Only Actor"));
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Dir Here"));
}

void PostGeneratorTest::tg_title_guillemets()
{
    const QString post = QStringLiteral("Серия: 3/12\n\n\u00ABQuoted Title\u00BB в дубляже\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SERIES_TITLE%")), QStringLiteral("Quoted Title"));
}

void PostGeneratorTest::tg_title_curlyQuotes()
{
    const QString post = QStringLiteral("Серия: 4/20\n\n\u201CCurly Title\u201D в дубляже\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SERIES_TITLE%")), QStringLiteral("Curly Title"));
}

void PostGeneratorTest::tg_minimalEpisodeAndTitle()
{
    const QString post = QStringLiteral("Серия: 7/99\n\n«Мини»\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_NUMBER%")), QStringLiteral("7"));
    QCOMPARE(r.fields.value(QStringLiteral("%TOTAL_EPISODES%")), QStringLiteral("99"));
    QCOMPARE(r.fields.value(QStringLiteral("%SERIES_TITLE%")), QStringLiteral("Мини"));
}

void PostGeneratorTest::tg_placeholderTitleInQuotes_skipsTitleField()
{
    const QString post = QStringLiteral("Серия: 1/1\n\n\u00AB%SERIES_TITLE%\u00BB в дубляже\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QVERIFY(!r.fields.contains(QStringLiteral("%SERIES_TITLE%")));
}

void PostGeneratorTest::vk_sameBody_asTelegram()
{
    const PostParseResult rTg = PostGenerator::parsePostToFields(fullDubbingPost(), QStringLiteral("telegram"));
    const PostParseResult rVk = PostGenerator::parsePostToFields(fullDubbingPost(), QStringLiteral("vk"));
    QVERIFY(rVk.success);
    QCOMPARE(rVk.fields.value(QStringLiteral("%CAST_LIST%")), rTg.fields.value(QStringLiteral("%CAST_LIST%")));
    QCOMPARE(rVk.fields.value(QStringLiteral("%SERIES_TITLE%")), rTg.fields.value(QStringLiteral("%SERIES_TITLE%")));
}

void PostGeneratorTest::tg_anime365MarkdownLink()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«X»\n\n"
        "Смотреть на [Anime365](https://anime365.example/watch/1)\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANIME365%")), QStringLiteral("https://anime365.example/watch/1"));
    QVERIFY(r.fields.value(QStringLiteral("%PARSED_TEMPLATE_TEXT%")).contains(QStringLiteral("%LINK_ANIME365%")));
}

void PostGeneratorTest::tg_emptyInput_errors()
{
    const PostParseResult r = PostGenerator::parsePostToFields(QStringLiteral("   \n\t  "), QStringLiteral("telegram"));
    QVERIFY(!r.success);
    QVERIFY(!r.errors.isEmpty());
}

void PostGeneratorTest::invalidSourceType_errors()
{
    const PostParseResult r = PostGenerator::parsePostToFields(QStringLiteral("Серия: 1/1"), QStringLiteral("twitter"));
    QVERIFY(!r.success);
    QVERIFY(!r.errors.isEmpty());
}

void PostGeneratorTest::tg_fullwidthColonHeader()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "Роли дублировали\uFF1A\n"
        "Solo Voice\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%CAST_LIST%")), QStringLiteral("Solo Voice"));
}

void PostGeneratorTest::tg_episodeSoundEngineer()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "«Ep»\n\n"
        "Звукорежиссёр эпизода:\n"
        "Episode Eng\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_ENGINEER%")), QStringLiteral("Episode Eng"));
}

void PostGeneratorTest::tg_recordingSoundEngineer()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "«Rec»\n\n"
        "Звукорежиссёр записи:\n"
        "Recording Eng\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%RECORDING_ENGINEER%")), QStringLiteral("Recording Eng"));
}

void PostGeneratorTest::tg_translationEditor()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "«T»\n\n"
        "Редактор перевода:\n"
        "Editor Name\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%TRANSLATION_EDITOR%")), QStringLiteral("Editor Name"));
}

void PostGeneratorTest::tg_songAndMainSoundEngineers_distinct()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "«X»\n\n"
        "Звукорежиссёр песен:\n"
        "Song Person\n\n"
        "Звукорежиссёр:\n"
        "Main Person\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SONG_ENGINEER%")), QStringLiteral("Song Person"));
    QCOMPARE(r.fields.value(QStringLiteral("%SOUND_ENGINEER%")), QStringLiteral("Main Person"));
}

void PostGeneratorTest::tg_onlySongSoundEngineer()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Y»\n\n"
        "Звукорежиссёр песен:\n"
        "Only Song\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SONG_ENGINEER%")), QStringLiteral("Only Song"));
    QVERIFY(!r.fields.contains(QStringLiteral("%SOUND_ENGINEER%")));
}

void PostGeneratorTest::tg_anilib_markdownFourK()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "[AnimeLib 4K](https://anilib.example/path)\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANILIB%")), QStringLiteral("https://anilib.example/path"));
}

void PostGeneratorTest::tg_anilib_parentheses()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "AnimeLib (https://anilib.example/paren)\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANILIB%")), QStringLiteral("https://anilib.example/paren"));
}

void PostGeneratorTest::tg_anilib_colon()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "AnimeLib: https://anilib.example/colon\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANILIB%")), QStringLiteral("https://anilib.example/colon"));
}

void PostGeneratorTest::tg_anilib_nextLine()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "AnimeLib 4\u041A\n"
        "https://anilib.example/next\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANILIB%")), QStringLiteral("https://anilib.example/next"));
}

void PostGeneratorTest::tg_anime365_parentheses()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "Anime365 (https://anime365.example/paren)\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANIME365%")), QStringLiteral("https://anime365.example/paren"));
}

void PostGeneratorTest::tg_anime365_colon()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "Anime365: https://anime365.example/colon\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANIME365%")), QStringLiteral("https://anime365.example/colon"));
}

void PostGeneratorTest::tg_anime365_nextLine()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Z»\n\n"
        "Anime365\n"
        "https://anime365.example/next\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANIME365%")), QStringLiteral("https://anime365.example/next"));
}

void PostGeneratorTest::tg_ozvuchivaliRoli_anchor()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«A»\n\n"
        "Озвучивали роли:\n"
        "Voice One\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%CAST_LIST%")), QStringLiteral("Voice One"));
}

void PostGeneratorTest::tg_timingAnchor()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«B»\n\n"
        "Тайминг:\n"
        "Timer Only\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%TIMING_AUTHOR%")), QStringLiteral("Timer Only"));
}

void PostGeneratorTest::tg_localizationVideoRow()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«C»\n\n"
        "Локализация видеоряда:\n"
        "Video Person\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%")), QStringLiteral("Video Person"));
}

void PostGeneratorTest::tg_localizationCaptions()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«D»\n\n"
        "Локализация надписей:\n"
        "Caption Person\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SIGNS_AUTHOR%")), QStringLiteral("Caption Person"));
}

void PostGeneratorTest::tg_inlineDirector_sameLine()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«E»\n\n"
        "Режиссёр дубляжа: Inline Director\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Inline Director"));
}

void PostGeneratorTest::tg_castTruncatedBeforeHashtagBlock()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«F»\n\n"
        "Роли дублировали:\n"
        "Actor Only\n\n"
        "#tag@here\n\n"
        "Режиссёр дубляжа:\n"
        "Dir\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%CAST_LIST%")), QStringLiteral("Actor Only"));
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Dir"));
}

void PostGeneratorTest::tg_episodeLine_numberSign()
{
    const QString post = QStringLiteral(
        "Серия\u2116 3/12\n\n"
        "«G»\n\n"
        "Роли дублировали:\n"
        "One\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_NUMBER%")), QStringLiteral("3"));
    QCOMPARE(r.fields.value(QStringLiteral("%TOTAL_EPISODES%")), QStringLiteral("12"));
}

void PostGeneratorTest::tg_megaPost_allSupportedFields()
{
    const QString post = megaPostAllFieldsBody();
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_NUMBER%")), QStringLiteral("2"));
    QCOMPARE(r.fields.value(QStringLiteral("%TOTAL_EPISODES%")), QStringLiteral("20"));
    QCOMPARE(r.fields.value(QStringLiteral("%SERIES_TITLE%")), QStringLiteral("Mega Title"));
    QCOMPARE(r.fields.value(QStringLiteral("%CAST_LIST%")), QStringLiteral("Actor A, Actor B"));
    QCOMPARE(r.fields.value(QStringLiteral("%DIRECTOR%")), QStringLiteral("Director X"));
    QCOMPARE(r.fields.value(QStringLiteral("%SONG_ENGINEER%")), QStringLiteral("Song Eng"));
    QCOMPARE(r.fields.value(QStringLiteral("%SOUND_ENGINEER%")), QStringLiteral("Main Sound"));
    QCOMPARE(r.fields.value(QStringLiteral("%EPISODE_ENGINEER%")), QStringLiteral("Ep Sound"));
    QCOMPARE(r.fields.value(QStringLiteral("%RECORDING_ENGINEER%")), QStringLiteral("Rec Sound"));
    QCOMPARE(r.fields.value(QStringLiteral("%TRANSLATION_EDITOR%")), QStringLiteral("Trans Editor"));
    QCOMPARE(r.fields.value(QStringLiteral("%SUB_AUTHOR%")), QStringLiteral("Subber"));
    QCOMPARE(r.fields.value(QStringLiteral("%TIMING_AUTHOR%")), QStringLiteral("Timer"));
    QCOMPARE(r.fields.value(QStringLiteral("%SIGNS_AUTHOR%")), QStringLiteral("Captions Editor"));
    QCOMPARE(r.fields.value(QStringLiteral("%VIDEO_LOCALIZATION_AUTHOR%")), QStringLiteral("Video Loc"));
    QCOMPARE(r.fields.value(QStringLiteral("%RELEASE_BUILDER%")), QStringLiteral("Builder B"));
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANILIB%")), QStringLiteral("https://anilib.example/m"));
    QCOMPARE(r.fields.value(QStringLiteral("%LINK_ANIME365%")), QStringLiteral("https://anime365.example/m"));
    QVERIFY(r.fields.value(QStringLiteral("%PARSED_TEMPLATE_TEXT%")).contains(QStringLiteral("%CAST_LIST%")));
    QVERIFY(r.structurePlaceholdersInParsedTemplate >= 10);
}

void PostGeneratorTest::tg_mainSoundBeforeSong_inPost()
{
    const QString post = QStringLiteral(
        "Серия: 1/1\n\n"
        "«Order»\n\n"
        "Звукорежиссёр:\n"
        "Main Line\n\n"
        "Звукорежиссёр песен:\n"
        "Song Line\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%SOUND_ENGINEER%")), QStringLiteral("Main Line"));
    QCOMPARE(r.fields.value(QStringLiteral("%SONG_ENGINEER%")), QStringLiteral("Song Line"));
}

void PostGeneratorTest::tg_razmetkaHeader()
{
    const QString post = QStringLiteral(
        "Серия: 1/5\n\n"
        "«Rz»\n\n"
        "Разметка:\n"
        "Markup Author\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QCOMPARE(r.fields.value(QStringLiteral("%TIMING_AUTHOR%")), QStringLiteral("Markup Author"));
}

void PostGeneratorTest::vk_megaPost_matchesTelegram()
{
    const QString body = megaPostAllFieldsBody();
    const PostParseResult rTg = PostGenerator::parsePostToFields(body, QStringLiteral("telegram"));
    const PostParseResult rVk = PostGenerator::parsePostToFields(body, QStringLiteral("vk"));
    QVERIFY(rVk.success);
    QCOMPARE(rVk.fields.value(QStringLiteral("%RELEASE_BUILDER%")), rTg.fields.value(QStringLiteral("%RELEASE_BUILDER%")));
    QCOMPARE(rVk.fields.value(QStringLiteral("%LINK_ANIME365%")), rTg.fields.value(QStringLiteral("%LINK_ANIME365%")));
}

void PostGeneratorTest::poster_plainFromPostText_extractsName()
{
    QCOMPARE(PostGenerator::posterLocalizationPlainFromPostText(fullDubbingPost()),
             QStringLiteral("Кирилл Хоримиев"));
    QCOMPARE(PostGenerator::posterLocalizationPlainFromPostText(QStringLiteral("Серия: 1/1\n\n«T»\n\n")),
             QString());
}

void PostGeneratorTest::vk_buildFromParse_includesPosterAuthorAsPlainText()
{
    const QString body = fullDubbingPost();
    const PostParseResult r = PostGenerator::parsePostToFields(body, QStringLiteral("telegram"));
    QVERIFY(r.success);
    const QString vk = PostGenerator::buildVkPostTemplateFromParseResult(r, body);
    const qsizetype idx = vk.indexOf(QStringLiteral("Локализация постера:"));
    QVERIFY(idx >= 0);
    QVERIFY(vk.mid(idx).startsWith(QStringLiteral("Локализация постера:\nКирилл Хоримиев")));
    QVERIFY(!vk.contains(QStringLiteral("%POSTER")));
}

void PostGeneratorTest::vk_buildFromParse_posterEmpty_stillHasBlockBeforeRelease()
{
    const QString body = megaPostAllFieldsBody();
    const PostParseResult r = PostGenerator::parsePostToFields(body, QStringLiteral("telegram"));
    QVERIFY(r.success);
    const QString vk = PostGenerator::buildVkPostTemplateFromParseResult(r, body);
    QVERIFY(vk.contains(QStringLiteral("Локализация постера:\n\nСборка релиза:")));
}

void PostGeneratorTest::tg_parseFromClipboardPath_preservesInlineBackticksInParsedTemplate()
{
    const QString post = QStringLiteral(
        "Серия: 1/10\n\n"
        "«T»\n\n"
        "Промокод `Dublyazhnaya` здесь.\n\n");
    const PostParseResult r = PostGenerator::parsePostToFields(post, QStringLiteral("telegram"));
    QVERIFY(r.success);
    QVERIFY(r.fields.value(QStringLiteral("%PARSED_TEMPLATE_TEXT%")).contains(QStringLiteral("`Dublyazhnaya`")));
}

QTEST_MAIN(PostGeneratorTest)

#include "postgenerator_test.moc"
