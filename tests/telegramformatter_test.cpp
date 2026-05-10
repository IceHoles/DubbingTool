/**
 * @file telegramformatter_test.cpp
 * @brief Unit tests for TelegramFormatter (TD mime / clipboard round-trip, branch coverage).
 */

#include <memory>

#include <QClipboard>
#include <QCoreApplication>
#include <QDataStream>
#include <QGuiApplication>
#include <QMimeData>
#include <QtTest/QtTest>

#include "telegramformatter.h"

extern void telegramFormatter_test_hook_applyTokensEmpty();

namespace
{

[[nodiscard]] QString normalizeNewlines(QString s)
{
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return s;
}

[[nodiscard]] QString markdownRoundTrip(const QString& markdown)
{
    std::unique_ptr<QMimeData> mime(TelegramFormatter::createTdMimeDataFromMarkdown(markdown));
    QCoreApplication::processEvents();
    return TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get());
}


[[nodiscard]] QByteArray utf16BigEndianPayload(const QString& tagContent)
{
    QByteArray tagData;
    for (const QChar& ch : tagContent)
    {
        const ushort u = ch.unicode();
        tagData.append(static_cast<char>(static_cast<uchar>(u >> 8)));
        tagData.append(static_cast<char>(static_cast<uchar>(u & 0xFF)));
    }
    return tagData;
}

struct TdTagDesc
{
    quint32 position{};
    quint32 length{};
    QString content;
};

[[nodiscard]] QByteArray buildTdFieldTagsBlob(const QList<TdTagDesc>& tagDescs)
{
    QByteArray tagsBinary;
    QDataStream stream(&tagsBinary, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(tagDescs.size());
    for (const TdTagDesc& tag : tagDescs)
    {
        const QByteArray payload = utf16BigEndianPayload(tag.content);
        stream << tag.position;
        stream << tag.length;
        stream << static_cast<quint32>(payload.size());
        stream.writeRawData(payload.constData(), payload.size());
    }
    return tagsBinary;
}

[[nodiscard]] std::unique_ptr<QMimeData> makeTdMime(const QByteArray& utf8Text, const QByteArray& tagsBlob)
{
    auto mime = std::make_unique<QMimeData>();
    mime->setData(QStringLiteral("application/x-td-field-text"), utf8Text);
    if (!tagsBlob.isEmpty())
    {
        mime->setData(QStringLiteral("application/x-td-field-tags"), tagsBlob);
    }
    return mime;
}

[[nodiscard]] std::unique_ptr<QMimeData> makeTdMimeWithTagsField(const QByteArray& utf8Text,
                                                                 const QByteArray& tagsBlob)
{
    auto mime = std::make_unique<QMimeData>();
    mime->setData(QStringLiteral("application/x-td-field-text"), utf8Text);
    mime->setData(QStringLiteral("application/x-td-field-tags"), tagsBlob);
    return mime;
}

} // namespace

class TelegramFormatterTest : public QObject
{
    Q_OBJECT

private slots:
    void nullMime_returnsEmpty();
    void mimeWithoutTdText_returnsEmpty();
    void tdTextFieldPresentButEmpty_returnsEmpty();
    void plainText_noTags_returnsSame();
    void tagsFieldPresentButEmptyBytes_returnsPlainText();
    void stripsTrailingNulCharacterFromTextField();
    void emptyTagsCount_returnsPlainText();
    void emptyText_withNonEmptyTags_returnsEmptyString();
    void boldSpan_recoversMarkdown();
    void underlineSpan_recoversMarkdown();
    void strikeSpan_recoversMarkdown();
    void spoilerSpan_recoversMarkdown();
    void supSpan_recoversMarkdown();
    void codeSpan_recoversMarkdown();
    void linkSpan_recoversMarkdown();
    void quoteGtSpan_recoversMarkdown();
    void quoteGtCaretSpan_recoversMarkdown();
    void customEmojiSpan_recoversMarkdown();
    void combinedBoldAndUnderline_tokensSorted();
    void boldLink_nestedOrder();
    void boldQuote_nestedOrder();
    void adjacentSegments_sameTokens_mergedIntoOneApply();
    void whitespaceOnlySegment_styleToken_appendsRawWhitespace();
    void whitespaceOnlySegment_linkToken_applyTokens();
    void whitespaceOnlySegment_customEmojiToken_applyTokens();
    void outOfRangeTagPositions_boundaryClampDoesNotApplyStyleTokens();
    void truncatedTagsBlob_readsPartialTagThenStops();
    void singleNulByteInText_choppedToEmptyPlainText();
    void zeroLengthTagPayload_coverageParseLoop();

    void mime_containsTdFields_afterCreateFromMarkdown();
    void markdown_roundTrip_plainText();
    void markdown_roundTrip_bold();
    void markdown_roundTrip_inlineCode();
    void markdown_roundTrip_underline();
    void markdown_roundTrip_strike();
    void markdown_roundTrip_spoiler();
    void markdown_roundTrip_superscript();
    void markdown_roundTrip_httpLink();
    void markdown_roundTrip_customEmojiPseudoMarkdown();
    void markdown_roundTrip_codeBlockNoLang();
    void markdown_roundTrip_codeBlockWithLang();
    void markdown_roundTrip_quoteGt();
    void markdown_roundTrip_quoteCaret();
    void markdown_roundTrip_quoteWithInnerBold();
    void parse_quoteLeadingPlainThenBold_coversNestedGapTagPath();
    void markdown_roundTrip_mixedBoldAndLink();
    void formatAndCopyToClipboard_setsClipboardToTdMime();
    void applyTokensToSegment_emptySegment_hook();
};

void TelegramFormatterTest::nullMime_returnsEmpty()
{
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(nullptr), QString());
}

void TelegramFormatterTest::mimeWithoutTdText_returnsEmpty()
{
    auto mime = std::make_unique<QMimeData>();
    mime->setText(QStringLiteral("hello"));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QString());
}

void TelegramFormatterTest::tdTextFieldPresentButEmpty_returnsEmpty()
{
    auto mime = std::make_unique<QMimeData>();
    mime->setData(QStringLiteral("application/x-td-field-text"), QByteArray());
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QString());
}

void TelegramFormatterTest::plainText_noTags_returnsSame()
{
    const QByteArray utf8 = QStringLiteral("hello").toUtf8();
    std::unique_ptr<QMimeData> mime = makeTdMime(utf8, {});
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QStringLiteral("hello"));
}

void TelegramFormatterTest::tagsFieldPresentButEmptyBytes_returnsPlainText()
{
    std::unique_ptr<QMimeData> mime =
        makeTdMimeWithTagsField(QStringLiteral("plain").toUtf8(), QByteArray());
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QStringLiteral("plain"));
}

void TelegramFormatterTest::stripsTrailingNulCharacterFromTextField()
{
    QByteArray utf8 = QStringLiteral("hello").toUtf8();
    utf8.append(char(0));
    std::unique_ptr<QMimeData> mime = makeTdMime(utf8, {});
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QStringLiteral("hello"));
}

void TelegramFormatterTest::emptyTagsCount_returnsPlainText()
{
    QByteArray tags;
    QDataStream stream(&tags, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(0);

    std::unique_ptr<QMimeData> mime = makeTdMimeWithTagsField(QStringLiteral("plain").toUtf8(), tags);
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QStringLiteral("plain"));
}

void TelegramFormatterTest::emptyText_withNonEmptyTags_returnsEmptyString()
{
    const QList<TdTagDesc> tagDescs = {{0, 0, QStringLiteral("**")}};
    std::unique_ptr<QMimeData> mime = makeTdMimeWithTagsField({}, buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QString());
}

void TelegramFormatterTest::boldSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 3, QStringLiteral("**")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("abc").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("**abc**"));
}

void TelegramFormatterTest::underlineSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("__")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("__z__"));
}

void TelegramFormatterTest::strikeSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("~~")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("~~z~~"));
}

void TelegramFormatterTest::spoilerSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("||")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("||z||"));
}

void TelegramFormatterTest::supSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("^^")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("^^z^^"));
}

void TelegramFormatterTest::codeSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("`")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("`z`"));
}

void TelegramFormatterTest::linkSpan_recoversMarkdown()
{
    const QString url = QStringLiteral("https://example.com/path");
    const QList<TdTagDesc> tagDescs = {{0, 2, url}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("go").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("[go](https://example.com/path)"));
}

void TelegramFormatterTest::quoteGtSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral(">")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral(">z<"));
}

void TelegramFormatterTest::quoteGtCaretSpan_recoversMarkdown()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral(">^")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral(">^z<^"));
}

void TelegramFormatterTest::customEmojiSpan_recoversMarkdown()
{
    const QString spec = QStringLiteral("custom-emoji://doc123?size=64");
    const QList<TdTagDesc> tagDescs = {{0, 1, spec}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("[z](emoji:doc123?size=64)"));
}

void TelegramFormatterTest::combinedBoldAndUnderline_tokensSorted()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("**\\__")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("x").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("__**x**__"));
}

void TelegramFormatterTest::boldLink_nestedOrder()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("**\\https://t.example/u")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("**[z](https://t.example/u)**"));
}

void TelegramFormatterTest::boldQuote_nestedOrder()
{
    const QList<TdTagDesc> tagDescs = {{0, 1, QStringLiteral("**\\>")}};
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("z").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral(">**z**<"));
}

void TelegramFormatterTest::adjacentSegments_sameTokens_mergedIntoOneApply()
{
    const QList<TdTagDesc> tagDescs = {
        {0, 2, QStringLiteral("**")},
        {2, 2, QStringLiteral("**")},
    };
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("abcd").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("**abcd**"));
}

void TelegramFormatterTest::whitespaceOnlySegment_styleToken_appendsRawWhitespace()
{
    const QString ws = QStringLiteral("\n  \t\r\n");
    const QList<TdTagDesc> tagDescs = {{0, static_cast<quint32>(ws.size()), QStringLiteral("**")}};
    std::unique_ptr<QMimeData> mime = makeTdMime(ws.toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), ws);
}

void TelegramFormatterTest::whitespaceOnlySegment_linkToken_applyTokens()
{
    const QString ws = QStringLiteral("   ");
    const QList<TdTagDesc> tagDescs = {{0, static_cast<quint32>(ws.size()),
                                        QStringLiteral("https://example.com/lbl")}};
    std::unique_ptr<QMimeData> mime = makeTdMime(ws.toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("[   ](https://example.com/lbl)"));
}

void TelegramFormatterTest::whitespaceOnlySegment_customEmojiToken_applyTokens()
{
    const QString ws = QStringLiteral("\n");
    const QList<TdTagDesc> tagDescs = {
        {0, static_cast<quint32>(ws.size()), QStringLiteral("custom-emoji://e1?size=20")}};
    std::unique_ptr<QMimeData> mime = makeTdMime(ws.toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("[\n](emoji:e1?size=20)"));
}

void TelegramFormatterTest::outOfRangeTagPositions_boundaryClampDoesNotApplyStyleTokens()
{
    const QList<TdTagDesc> tagDescs = {
        {999900u, 50u, QStringLiteral("**")},
    };
    std::unique_ptr<QMimeData> mime =
        makeTdMime(QStringLiteral("hi").toUtf8(), buildTdFieldTagsBlob(tagDescs));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("hi"));
}

void TelegramFormatterTest::truncatedTagsBlob_readsPartialTagThenStops()
{
    QByteArray bad;
    QDataStream stream(&bad, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(2);
    const QByteArray payloadStarBold = utf16BigEndianPayload(QStringLiteral("**"));
    stream << static_cast<quint32>(0);
    stream << static_cast<quint32>(1);
    stream << static_cast<quint32>(payloadStarBold.size());
    stream.writeRawData(payloadStarBold.constData(), payloadStarBold.size());
    stream << static_cast<quint32>(0);
    stream << static_cast<quint32>(1);
    stream << static_cast<quint32>(400);

    std::unique_ptr<QMimeData> mime =
        makeTdMimeWithTagsField(QStringLiteral("x").toUtf8(), bad);
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()),
             QStringLiteral("**x**"));
}

void TelegramFormatterTest::singleNulByteInText_choppedToEmptyPlainText()
{
    QByteArray utf8;
    utf8.append(char(0));
    std::unique_ptr<QMimeData> mime = makeTdMime(utf8, {});
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QString());
}

void TelegramFormatterTest::zeroLengthTagPayload_coverageParseLoop()
{
    QByteArray tags;
    QDataStream stream(&tags, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(1);
    stream << static_cast<quint32>(0);
    stream << static_cast<quint32>(1);
    stream << static_cast<quint32>(0);

    std::unique_ptr<QMimeData> mime =
        makeTdMimeWithTagsField(QStringLiteral("y").toUtf8(), tags);
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime.get()), QStringLiteral("y"));
}

void TelegramFormatterTest::mime_containsTdFields_afterCreateFromMarkdown()
{
    std::unique_ptr<QMimeData> mime(TelegramFormatter::createTdMimeDataFromMarkdown(QStringLiteral("ping")));
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-td-field-text")));
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-td-field-tags")));
}

void TelegramFormatterTest::markdown_roundTrip_plainText()
{
    const QString in = QStringLiteral("hello world");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_bold()
{
    const QString in = QStringLiteral("**bold**");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_inlineCode()
{
    const QString in = QStringLiteral("`code`");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_underline()
{
    const QString in = QStringLiteral("__u__");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_strike()
{
    const QString in = QStringLiteral("~~s~~");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_spoiler()
{
    const QString in = QStringLiteral("||x||");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_superscript()
{
    const QString in = QStringLiteral("^^up^^");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_httpLink()
{
    const QString in = QStringLiteral("[lbl](https://example.com/p)");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_customEmojiPseudoMarkdown()
{
    const QString in = QStringLiteral("[sm](emoji:doc1?size=20)");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_codeBlockNoLang()
{
    const QString in = QStringLiteral("```\nline\n```");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_codeBlockWithLang()
{
    const QString in = QStringLiteral("```cpp\nint x;\n```");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_quoteGt()
{
    const QString in = QStringLiteral(">inner<");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_quoteCaret()
{
    const QString in = QStringLiteral(">^caret<^");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::markdown_roundTrip_quoteWithInnerBold()
{
    const QString in = QStringLiteral(">**z**<");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::parse_quoteLeadingPlainThenBold_coversNestedGapTagPath()
{
    std::unique_ptr<QMimeData> mime(
        TelegramFormatter::createTdMimeDataFromMarkdown(QStringLiteral(">hello **z**<")));
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-td-field-text")));
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-td-field-tags")));
}

void TelegramFormatterTest::markdown_roundTrip_mixedBoldAndLink()
{
    const QString in = QStringLiteral("**b** [lbl](https://a.example/z)");
    QCOMPARE(normalizeNewlines(markdownRoundTrip(in)), normalizeNewlines(in));
}

void TelegramFormatterTest::formatAndCopyToClipboard_setsClipboardToTdMime()
{
    TelegramFormatter::formatAndCopyToClipboard(QStringLiteral("clip"));
    if (!qEnvironmentVariableIsEmpty("DUBBINGTOOL_UNDER_OPENCPP_COVERAGE"))
    {
        return;
    }

    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-td-field-text")));
    QCOMPARE(TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime), QStringLiteral("clip"));
}

void TelegramFormatterTest::applyTokensToSegment_emptySegment_hook()
{
    telegramFormatter_test_hook_applyTokensEmpty();
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TelegramFormatterTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "telegramformatter_test.moc"
