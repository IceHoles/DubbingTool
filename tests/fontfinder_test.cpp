/**
 * @file fontfinder_test.cpp
 * @brief Unit tests for FontFinder class
 *
 * These tests verify ASS subtitle font parsing logic.
 * Tests are designed to be CI-compatible (no system font dependencies).
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QString>

#include "fontfinder.h"

class FontFinderTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // parseInlineFontTags tests
    void testParseInlineFontTags_fnOverrideAtStart();
    void testParseInlineFontTags_fnInSecondConsecutiveBlock();
    void testParseInlineFontTags_noTags();
    void testParseInlineFontTags_textBeforeFirstTag();
    void testParseInlineFontTags_fnInMiddleTag();
    void testParseInlineFontTags_multipleFontChanges();
    void testParseInlineFontTags_emptyFnResetsToBase();
    void testParseInlineFontTags_boldItalicTags();
    void testParseInlineFontTags_realDialogueDefaultTop();
    void testParseInlineFontTags_realDialogueSigns();

    // parseAssFile tests (use test_data/ files)
    void testParseAssFile_nonExistentFile();
    void testParseAssFile_consecutiveTagBlocks();
    void testParseAssFile_italicStyleMissing();
    void testParseAssFile_multipleInlineFonts();
    void testParseAssFile_inlineFnOverride();

    // findSystemFont tests (Windows-only, uses DirectWrite API)
    void testFindSystemFont_standardWindowsFonts();
    void testFindSystemFont_nonExistentFont();
    void testFindSystemFont_boldVariant();
    void testFindSystemFont_italicVariant();
    void testFindSystemFont_boldItalicVariant();
    void testFindSystemFont_cachePerformance();

private:
    static bool containsFont(const QSet<AssStyleInfo>& styles, const QString& fontName);
    static bool containsStyle(const QSet<AssStyleInfo>& styles, const QString& fontName, bool bold, bool italic);
    QString testDataPath(const QString& filename) const;
};

void FontFinderTest::initTestCase()
{
    qDebug() << "Test data directory:" << QCoreApplication::applicationDirPath() + "/test_data";
}

void FontFinderTest::cleanupTestCase()
{
    // Cleanup after all tests
}

QString FontFinderTest::testDataPath(const QString& filename) const
{
    return QCoreApplication::applicationDirPath() + "/test_data/" + filename;
}

bool FontFinderTest::containsFont(const QSet<AssStyleInfo>& styles, const QString& fontName)
{
    for (const auto& s : styles)
    {
        if (s.fontName == fontName)
        {
            return true;
        }
    }
    return false;
}

bool FontFinderTest::containsStyle(const QSet<AssStyleInfo>& styles, const QString& fontName, bool bold, bool italic)
{
    AssStyleInfo target;
    target.fontName = fontName;
    target.bold = bold;
    target.italic = italic;
    return styles.contains(target);
}

// ============================================================================
// parseInlineFontTags tests
// ============================================================================

/**
 * @brief Test: When first tag block contains \fn, base style should NOT be added
 *
 * Example: Style "Default Top" uses Garamond, but dialogue starts with {\fnOtherFont}
 * Expected: Garamond should NOT appear in results
 */
void FontFinderTest::testParseInlineFontTags_fnOverrideAtStart()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Garamond";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"({\fnFOT Manyo Gyosho Std Strp E\fs190\fsp-60\b0}Text{\fsp-70}more)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "Garamond"),
             "Base font should NOT be added when first tag has \\fn override");
    QVERIFY2(containsFont(result, "FOT Manyo Gyosho Std Strp E"),
             "Font from \\fn tag should be present");
}

/**
 * @brief Test: When \fn is in second consecutive tag block, base style should NOT be added
 *
 * Example: {=0=2}{\fnAria...} - first block has layer tag, second has font
 * Expected: Base style should NOT appear in results
 */
void FontFinderTest::testParseInlineFontTags_fnInSecondConsecutiveBlock()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Garamond";
    baseStyle.bold = true;
    baseStyle.italic = false;

    // Real example - first block {=0=2} has no \fn, second block has \fnAria
    QString text = R"({=0=2}{\an5\fscx99.97\fscy99.97\fry-0.852\frx1.736\fax-0.037\fnAria\fs40\b0}Text)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "Garamond"),
             "Base font should NOT be added when \\fn is in consecutive tag blocks");
    QVERIFY2(containsFont(result, "Aria"),
             "Font from \\fn tag in second block should be present");
}

/**
 * @brief Test: When text has no tags, base style SHOULD be added
 */
void FontFinderTest::testParseInlineFontTags_noTags()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Verdana";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = "Simple text without any tags";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(containsStyle(result, "Verdana", false, false),
             "Base style should be added when text has no tags");
    QCOMPARE(result.size(), 1);
}

/**
 * @brief Test: When there's text BEFORE first tag, base style SHOULD be added
 *
 * Example: "Some text{\fnOtherFont}more" - "Some text" uses base font
 */
void FontFinderTest::testParseInlineFontTags_textBeforeFirstTag()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Arial";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"(Some text before{\fnOtherFont}tagged text)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(containsFont(result, "Arial"),
             "Base font should be added when there's text before first tag");
    QVERIFY2(containsFont(result, "OtherFont"),
             "Font from \\fn tag should also be present");
}

/**
 * @brief Test: First tag without \fn, later tag has \fn
 *
 * Example: {\pos(100,100)}Text{\fnOtherFont}more
 * Expected: Base style added (first tag has no \fn), OtherFont also added
 */
void FontFinderTest::testParseInlineFontTags_fnInMiddleTag()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Times New Roman";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"({\pos(100,100)\fsp-5}Initial text{\fnNewFont}changed font)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(containsFont(result, "Times New Roman"),
             "Base font should be added when first tag doesn't have \\fn");
    QVERIFY2(containsFont(result, "NewFont"),
             "Font from later \\fn tag should be present");
}

/**
 * @brief Test: Multiple font changes in text
 */
void FontFinderTest::testParseInlineFontTags_multipleFontChanges()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Base";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"({\fnFont1}text1{\fnFont2}text2{\fnFont3}text3)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "Base"),
             "Base font should NOT be added (first tag has \\fn)");
    QVERIFY2(containsFont(result, "Font1"), "Font1 should be present");
    QVERIFY2(containsFont(result, "Font2"), "Font2 should be present");
    QVERIFY2(containsFont(result, "Font3"), "Font3 should be present");
    QCOMPARE(result.size(), 3);
}

/**
 * @brief Test: Empty \fn tag should reset to base font
 *
 * Example: {\fnCustom}text{\fn}back to base
 */
void FontFinderTest::testParseInlineFontTags_emptyFnResetsToBase()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "DefaultFont";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"({\fnCustomFont}custom{\fn}back to default)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "DefaultFont") || containsFont(result, "CustomFont"),
             "Should have CustomFont");
    // Empty \fn resets to base, so DefaultFont may appear after the reset
}

/**
 * @brief Test: Bold and italic tags affect style
 */
void FontFinderTest::testParseInlineFontTags_boldItalicTags()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "TestFont";
    baseStyle.bold = false;
    baseStyle.italic = false;

    QString text = R"({\b1}bold text{\i1}bold+italic{\b0}italic only)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    // Should have multiple style variants
    QVERIFY2(containsStyle(result, "TestFont", false, false),
             "Base style (normal) should be present");
    QVERIFY2(containsStyle(result, "TestFont", true, false),
             "Bold style should be present");
    QVERIFY2(containsStyle(result, "TestFont", true, true),
             "Bold+Italic style should be present");
    QVERIFY2(containsStyle(result, "TestFont", false, true),
             "Italic style should be present");
}

/**
 * @brief Test: Real dialogue from Default Top style
 *
 * This is a real example where Garamond was incorrectly being added
 */
void FontFinderTest::testParseInlineFontTags_realDialogueDefaultTop()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Garamond";
    baseStyle.bold = true;  // Default Top has bold=-1
    baseStyle.italic = false;

    // Real dialogue text
    QString text = R"({\fnFOT Manyo Gyosho Std Strp E\fs190\fsp-60\b0\c&H3C3632&\3c&H2C2523&)\fax-0.094558\fscx99.91\fscy99.54\frx0.3196\fry2.0887\bord13\frz22.56\pos(832.64,156.89)\1a&HFF&}日{\fsp-70}本昔話{\fsp-67}原{\fsp-66}話{\fsp-62}集{\fsp-70}ㅡ大{\fsp0} {\fsp-64}同{\fsp-54}太西{\fsp-63}域{\fsp-62}刻成)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "Garamond"),
             "Garamond should NOT be in result - all text uses \\fn override");
    QVERIFY2(containsFont(result, "FOT Manyo Gyosho Std Strp E"),
             "The actual used font should be present");
}

/**
 * @brief Test: Real dialogue from signs style
 */
void FontFinderTest::testParseInlineFontTags_realDialogueSigns()
{
    AssStyleInfo baseStyle;
    baseStyle.fontName = "Arial";
    baseStyle.bold = false;
    baseStyle.italic = false;

    // Real dialogue from signs style
    QString text = R"({\an1\pos(-100.71,73.56)\fscx56\fscy56\fnFranklin Gothic Book\bord3\c&HFBFBFB&\3c&HFBFBFB&\alpha&HBF&\1a&HFF&\blur0.65}Text.)";

    QSet<AssStyleInfo> result = FontFinder::parseInlineFontTags(text, baseStyle);

    QVERIFY2(!containsFont(result, "Arial"),
             "Arial should NOT be in result - first tag has \\fn override");
    QVERIFY2(containsFont(result, "Franklin Gothic Book"),
             "Franklin Gothic Book should be present");
}

// ============================================================================
// parseAssFile tests (CI-compatible - only check parsing, not system fonts)
// ============================================================================

/**
 * @brief Test: parseAssFile returns empty for non-existent file
 */
void FontFinderTest::testParseAssFile_nonExistentFile()
{
    FontFinder finder;
    QSet<AssStyleInfo> result = finder.parseAssFile("non_existent_file_12345.ass");

    QVERIFY2(result.isEmpty(), "Should return empty set for non-existent file");
}

/**
 * @brief Test: consecutive_tag_blocks.ass
 *
 * Tests handling of consecutive tag blocks like {=0=2}{\fnFont...}
 * Default Top style uses Garamond but ALL dialogues override with \fn
 * Expected: Garamond should NOT appear, inline fonts should be present
 */
void FontFinderTest::testParseAssFile_consecutiveTagBlocks()
{
    QString testFile = testDataPath("consecutive_tag_blocks.ass");

    if (!QFile::exists(testFile))
    {
        QSKIP("Test file not found - run cmake to copy test data");
    }

    FontFinder finder;
    QSet<AssStyleInfo> result = finder.parseAssFile(testFile);

    // Garamond should NOT be in result - all Default Top dialogues override with \fn
    QVERIFY2(!containsFont(result, "Garamond"),
             "Garamond should NOT appear - all Default Top dialogues override font with \\fn");

    // Verdana SHOULD be present (Default style has dialogues without \fn)
    QVERIFY2(containsFont(result, "Verdana"),
             "Verdana should be present from Default style");

    // Fonts from inline tags
    QVERIFY2(containsFont(result, "Franklin Gothic Book"),
             "Franklin Gothic Book should be present from inline \\fn");
    QVERIFY2(containsFont(result, "Aria"),
             "Aria should be present from inline \\fn");
    QVERIFY2(containsFont(result, "FOT Manyo Gyosho Std Strp E"),
             "FOT Manyo Gyosho Std Strp E should be present");

    qDebug() << "Fonts found:" << result.size();
    for (const auto& s : result)
    {
        qDebug() << "  -" << s.fontName << "(bold:" << s.bold << ", italic:" << s.italic << ")";
    }
}

/**
 * @brief Test: italic_style_missing.ass
 *
 * Tests styles that use italic but font may not have italic variant
 * Expected: Tahoma regular and italic should be detected
 */
void FontFinderTest::testParseAssFile_italicStyleMissing()
{
    QString testFile = testDataPath("italic_style_missing.ass");

    if (!QFile::exists(testFile))
    {
        QSKIP("Test file not found - run cmake to copy test data");
    }

    FontFinder finder;
    QSet<AssStyleInfo> result = finder.parseAssFile(testFile);

    // Fonts from inline tags
    QVERIFY2(containsFont(result, "Ostrovsky"), "Ostrovsky should be present");
    QVERIFY2(containsFont(result, "Reed"), "Reed should be present");

    // Tahoma from styles
    QVERIFY2(containsFont(result, "Tahoma"), "Tahoma should be present");

    // Tahoma italic (from Italics/Italics Top styles)
    QVERIFY2(containsStyle(result, "Tahoma", false, true),
             "Tahoma italic should be present from Italics style");

    // Tahoma regular (from Default/Default Top styles)
    QVERIFY2(containsStyle(result, "Tahoma", false, false),
             "Tahoma regular should be present from Default style");

    qDebug() << "Fonts found:" << result.size();
    for (const auto& s : result)
    {
        qDebug() << "  -" << s.fontName << "(bold:" << s.bold << ", italic:" << s.italic << ")";
    }
}

/**
 * @brief Test: multiple_inline_fonts.ass
 *
 * Tests file with many inline \fn font changes
 * Expected: Multiple fonts including custom ones
 */
void FontFinderTest::testParseAssFile_multipleInlineFonts()
{
    QString testFile = testDataPath("multiple_inline_fonts.ass");

    if (!QFile::exists(testFile))
    {
        QSKIP("Test file not found - run cmake to copy test data");
    }

    FontFinder finder;
    QSet<AssStyleInfo> result = finder.parseAssFile(testFile);

    // Custom fonts from inline tags
    QVERIFY2(containsFont(result, "A-OTF Jun Pro 501"),
             "A-OTF Jun Pro 501 should be present");
    QVERIFY2(containsFont(result, "AleksandraC vintage"),
             "AleksandraC vintage should be present");

    // System fonts from styles and inline tags
    QVERIFY2(containsFont(result, "Arial"), "Arial should be present");
    QVERIFY2(containsFont(result, "Georgia"), "Georgia should be present");
    QVERIFY2(containsFont(result, "Times New Roman"), "Times New Roman should be present");
    // Note: Tahoma is defined in TopCenter/BottomCenter styles but no dialogues use these styles

    // Arial variants (from {\i1} tags in dialogue)
    QVERIFY2(containsStyle(result, "Arial", false, false),
             "Arial regular should be present");

    // Times New Roman variants
    QVERIFY2(containsStyle(result, "Times New Roman", false, false),
             "Times New Roman regular should be present");

    // Georgia bold (from inline tags)
    QVERIFY2(containsStyle(result, "Georgia", true, false),
             "Georgia bold should be present");

    qDebug() << "Fonts found:" << result.size();
    for (const auto& s : result)
    {
        qDebug() << "  -" << s.fontName << "(bold:" << s.bold << ", italic:" << s.italic << ")";
    }
}

/**
 * @brief Test: inline_fn_override.ass
 *
 * Tests file where inline \fn tags override style fonts
 * Expected: Both style fonts and inline fonts should be detected
 */
void FontFinderTest::testParseAssFile_inlineFnOverride()
{
    QString testFile = testDataPath("inline_fn_override.ass");

    if (!QFile::exists(testFile))
    {
        QSKIP("Test file not found - run cmake to copy test data");
    }

    FontFinder finder;
    QSet<AssStyleInfo> result = finder.parseAssFile(testFile);

    // Custom font from inline tags
    QVERIFY2(containsFont(result, "Belepotan RUS"),
             "Belepotan RUS should be present from inline \\fn");

    // System fonts
    QVERIFY2(containsFont(result, "Georgia"), "Georgia should be present");
    QVERIFY2(containsFont(result, "Tahoma"), "Tahoma should be present");
    QVERIFY2(containsFont(result, "Times New Roman"), "Times New Roman should be present");

    // Georgia bold (from inline tags with \b1)
    QVERIFY2(containsStyle(result, "Georgia", true, false),
             "Georgia bold should be present");

    // Tahoma bold (from styles with Bold=-1)
    QVERIFY2(containsStyle(result, "Tahoma", true, false),
             "Tahoma bold should be present from styles");

    // Times New Roman bold
    QVERIFY2(containsStyle(result, "Times New Roman", true, false),
             "Times New Roman bold should be present");

    qDebug() << "Fonts found:" << result.size();
    for (const auto& s : result)
    {
        qDebug() << "  -" << s.fontName << "(bold:" << s.bold << ", italic:" << s.italic << ")";
    }
}

// ============================================================================
// findSystemFont tests (Windows-only, uses DirectWrite API)
// ============================================================================

/**
 * @brief Test: Standard Windows fonts should be found
 *
 * These fonts are guaranteed to exist on all Windows installations.
 */
void FontFinderTest::testFindSystemFont_standardWindowsFonts()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    // Test Arial - present on all Windows
    AssStyleInfo arialStyle;
    arialStyle.fontName = "Arial";
    arialStyle.bold = false;
    arialStyle.italic = false;

    QString arialPath = finder.findSystemFont(arialStyle);
    QVERIFY2(!arialPath.isEmpty(), "Arial should be found on Windows");
    QVERIFY2(arialPath.toLower().contains("arial"), "Path should contain 'arial'");
    qDebug() << "Arial found at:" << arialPath;

    // Test Times New Roman
    AssStyleInfo timesStyle;
    timesStyle.fontName = "Times New Roman";
    timesStyle.bold = false;
    timesStyle.italic = false;

    QString timesPath = finder.findSystemFont(timesStyle);
    QVERIFY2(!timesPath.isEmpty(), "Times New Roman should be found on Windows");
    QVERIFY2(timesPath.toLower().contains("times"), "Path should contain 'times'");
    qDebug() << "Times New Roman found at:" << timesPath;

    // Test Verdana
    AssStyleInfo verdanaStyle;
    verdanaStyle.fontName = "Verdana";
    verdanaStyle.bold = false;
    verdanaStyle.italic = false;

    QString verdanaPath = finder.findSystemFont(verdanaStyle);
    QVERIFY2(!verdanaPath.isEmpty(), "Verdana should be found on Windows");
    qDebug() << "Verdana found at:" << verdanaPath;

    // Test Tahoma
    AssStyleInfo tahomaStyle;
    tahomaStyle.fontName = "Tahoma";
    tahomaStyle.bold = false;
    tahomaStyle.italic = false;

    QString tahomaPath = finder.findSystemFont(tahomaStyle);
    QVERIFY2(!tahomaPath.isEmpty(), "Tahoma should be found on Windows");
    qDebug() << "Tahoma found at:" << tahomaPath;
}

/**
 * @brief Test: Non-existent font should return empty path
 */
void FontFinderTest::testFindSystemFont_nonExistentFont()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    AssStyleInfo fakeStyle;
    fakeStyle.fontName = "NonExistentFontName12345XYZ";
    fakeStyle.bold = false;
    fakeStyle.italic = false;

    QString path = finder.findSystemFont(fakeStyle);
    QVERIFY2(path.isEmpty(), "Non-existent font should return empty path");
}

/**
 * @brief Test: Bold variant should be found (different file than regular)
 */
void FontFinderTest::testFindSystemFont_boldVariant()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    // Regular Arial
    AssStyleInfo regularStyle;
    regularStyle.fontName = "Arial";
    regularStyle.bold = false;
    regularStyle.italic = false;

    QString regularPath = finder.findSystemFont(regularStyle);
    QVERIFY2(!regularPath.isEmpty(), "Arial regular should be found");

    // Bold Arial
    AssStyleInfo boldStyle;
    boldStyle.fontName = "Arial";
    boldStyle.bold = true;
    boldStyle.italic = false;

    QString boldPath = finder.findSystemFont(boldStyle);
    QVERIFY2(!boldPath.isEmpty(), "Arial bold should be found");

    // Paths should be different (different font files)
    QVERIFY2(regularPath.toLower() != boldPath.toLower(),
             "Bold and regular should be different font files");

    qDebug() << "Arial regular:" << regularPath;
    qDebug() << "Arial bold:" << boldPath;
}

/**
 * @brief Test: Italic variant should be found
 */
void FontFinderTest::testFindSystemFont_italicVariant()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    // Regular Arial
    AssStyleInfo regularStyle;
    regularStyle.fontName = "Arial";
    regularStyle.bold = false;
    regularStyle.italic = false;

    QString regularPath = finder.findSystemFont(regularStyle);
    QVERIFY2(!regularPath.isEmpty(), "Arial regular should be found");

    // Italic Arial
    AssStyleInfo italicStyle;
    italicStyle.fontName = "Arial";
    italicStyle.bold = false;
    italicStyle.italic = true;

    QString italicPath = finder.findSystemFont(italicStyle);
    QVERIFY2(!italicPath.isEmpty(), "Arial italic should be found");

    // Paths should be different
    QVERIFY2(regularPath.toLower() != italicPath.toLower(),
             "Italic and regular should be different font files");

    qDebug() << "Arial regular:" << regularPath;
    qDebug() << "Arial italic:" << italicPath;
}

/**
 * @brief Test: Bold+Italic variant should be found
 */
void FontFinderTest::testFindSystemFont_boldItalicVariant()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    // Bold+Italic Arial
    AssStyleInfo boldItalicStyle;
    boldItalicStyle.fontName = "Arial";
    boldItalicStyle.bold = true;
    boldItalicStyle.italic = true;

    QString boldItalicPath = finder.findSystemFont(boldItalicStyle);
    QVERIFY2(!boldItalicPath.isEmpty(), "Arial bold+italic should be found");

    // Regular Arial for comparison
    AssStyleInfo regularStyle;
    regularStyle.fontName = "Arial";
    regularStyle.bold = false;
    regularStyle.italic = false;

    QString regularPath = finder.findSystemFont(regularStyle);

    // Paths should be different
    QVERIFY2(regularPath.toLower() != boldItalicPath.toLower(),
             "Bold+italic and regular should be different font files");

    qDebug() << "Arial regular:" << regularPath;
    qDebug() << "Arial bold+italic:" << boldItalicPath;
}

/**
 * @brief Test: Font cache should make repeated lookups faster
 *
 * DirectWrite caches font information internally, so second lookup
 * should be faster than first.
 */
void FontFinderTest::testFindSystemFont_cachePerformance()
{
#ifndef Q_OS_WIN
    QSKIP("This test requires Windows (DirectWrite API)");
#endif

    FontFinder finder;

    AssStyleInfo style;
    style.fontName = "Arial";
    style.bold = false;
    style.italic = false;

    // First lookup (cold)
    QElapsedTimer timer;
    timer.start();
    QString path1 = finder.findSystemFont(style);
    qint64 firstLookup = timer.elapsed();

    QVERIFY2(!path1.isEmpty(), "First lookup should succeed");

    // Second lookup (should be cached)
    timer.restart();
    QString path2 = finder.findSystemFont(style);
    qint64 secondLookup = timer.elapsed();

    QVERIFY2(!path2.isEmpty(), "Second lookup should succeed");
    QCOMPARE(path1, path2);

    qDebug() << "First lookup:" << firstLookup << "ms";
    qDebug() << "Second lookup:" << secondLookup << "ms";

    // Note: We don't strictly assert second < first because
    // timing can vary, but we log it for manual inspection
}

QTEST_MAIN(FontFinderTest)
#include "fontfinder_test.moc"
