#include "telegramformatter.h"

#include <QByteArray>
#include <QChar>
#include <QClipboard>
#include <QDataStream>
#include <QGuiApplication>
#include <QIODevice>
#include <QList>
#include <QMap>
#include <QMimeData>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <algorithm>

struct FinalTag
{
    qsizetype position;
    qsizetype length;
    QString tagData;
    bool operator<(const FinalTag& other) const
    {
        return position < other.position;
    }
};

struct MatchInfo
{
    qsizetype start;
    qsizetype length;
    QString content;
    QString tag;
};

namespace
{

struct TagRecord
{
    qsizetype position{};
    qsizetype length{};
    QString tagContent; // already decoded UTF-16 BE string
};

QPair<QString, QList<FinalTag>> parseText(const QString& text, const QList<QPair<QRegularExpression, QString>>& rules)
{
    QString cleanText;
    QList<FinalTag> tags;

    QList<MatchInfo> allMatches;
    for (const auto& rulePair : rules)
    {
        auto it = rulePair.first.globalMatch(text);
        while (it.hasNext())
        {
            QRegularExpressionMatch match = it.next();
            if (rulePair.second == "code-block")
            {
                QString lang = match.captured(1);
                QString code = match.captured(2);
                allMatches.append({match.capturedStart(0), match.capturedLength(0), code, "```" + lang});
            }
            else if (rulePair.second == "custom-emoji")
            {
                allMatches.append({match.capturedStart(0), match.capturedLength(0), match.captured(1),
                                   "custom-emoji://" + match.captured(2)});
            }
            else if (rulePair.second == "link")
            {
                allMatches.append(
                    {match.capturedStart(0), match.capturedLength(0), match.captured(1), match.captured(2)});
            }
            else
            {
                allMatches.append(
                    {match.capturedStart(0), match.capturedLength(0), match.captured(1), rulePair.second});
            }
        }
    }

    QList<MatchInfo> topLevelMatches;
    for (const auto& matchA : allMatches)
    {
        bool isNested = false;
        for (const auto& matchB : allMatches)
        {
            if (&matchA == &matchB)
            {
                continue;
            }
            if (matchA.start >= matchB.start && (matchA.start + matchA.length) <= (matchB.start + matchB.length))
            {
                isNested = true;
                break;
            }
        }
        if (!isNested)
        {
            topLevelMatches.append(matchA);
        }
    }
    std::sort(topLevelMatches.begin(), topLevelMatches.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });

    qsizetype lastPos = 0;
    for (const auto& match : topLevelMatches)
    {
        cleanText.append(text.mid(lastPos, match.start - lastPos));

        QPair<QString, QList<FinalTag>> subResult;
        if (match.tag.startsWith("```"))
        {
            subResult.first = match.content;
        }
        else if (match.tag == ">" || match.tag == ">^")
        {
            QList<QPair<QRegularExpression, QString>> quoteRules = rules;
            quoteRules.removeIf([](const auto& rule) { return rule.second == "`"; });
            subResult = parseText(match.content, quoteRules);
        }
        else
        {
            subResult = parseText(match.content, rules);
        }

        qsizetype parentTagStartPos = cleanText.length();
        cleanText.append(subResult.first);

        qsizetype lastTaggedPosInSub = 0;
        std::sort(subResult.second.begin(), subResult.second.end());

        for (auto& subTag : subResult.second)
        {
            if (subTag.position > lastTaggedPosInSub)
            {
                tags.append({parentTagStartPos + lastTaggedPosInSub, subTag.position - lastTaggedPosInSub, match.tag});
            }
            QStringList combined = subTag.tagData.split('\\');
            combined.append(match.tag);
            std::sort(combined.begin(), combined.end());
            subTag.tagData = combined.join('\\');
            tags.append({parentTagStartPos + subTag.position, subTag.length, subTag.tagData});
            lastTaggedPosInSub = subTag.position + subTag.length;
        }
        if (lastTaggedPosInSub < subResult.first.length())
        {
            tags.append(
                {parentTagStartPos + lastTaggedPosInSub, subResult.first.length() - lastTaggedPosInSub, match.tag});
        }
        if (subResult.second.isEmpty() && !subResult.first.isEmpty())
        {
            tags.append({parentTagStartPos, subResult.first.length(), match.tag});
        }

        lastPos = match.start + match.length;
    }
    cleanText.append(text.mid(lastPos));

    return {cleanText, tags};
}

QVector<TagRecord> parseTelegramTagsBinary(const QByteArray& tagsData)
{
    QVector<TagRecord> tags;
    if (tagsData.isEmpty())
        return tags;

    QDataStream stream(tagsData);
    stream.setByteOrder(QDataStream::BigEndian); // Заголовки (pos, len, size) всегда BE

    quint32 count = 0;
    stream >> count;

    for (quint32 i = 0; i < count && !stream.atEnd(); ++i)
    {
        quint32 pos = 0;
        quint32 len = 0;
        quint32 size = 0;
        stream >> pos >> len >> size;

        QByteArray tagBytes(size, 0);
        if (stream.readRawData(tagBytes.data(), size) != (int)size)
            break;

        // ИСПРАВЛЕНИЕ: Читаем UTF-16 Big Endian вручную
        QString content;
        content.reserve(size / 2);
        for (int j = 0; j < (int)size; j += 2)
        {
            // Собираем символ из двух байт: [high][low]
            uchar high = static_cast<uchar>(tagBytes[j]);
            uchar low = static_cast<uchar>(tagBytes[j + 1]);
            content.append(QChar((high << 8) | low));
        }

        tags.append({static_cast<qsizetype>(pos), static_cast<qsizetype>(len), content});
    }
    return tags;
}

QString applyTokensToSegment(const QString& segment, const QStringList& tokens)
{
    if (segment.isEmpty())
    {
        return segment;
    }

    // Разбираем токены по типам
    bool hasBold = false;
    bool hasUnderline = false;
    bool hasStrike = false;
    bool hasSpoiler = false;
    bool hasSup = false;
    bool hasCode = false;

    QString quoteType; // ">" или ">^"
    QString linkUrl;
    QString emojiSpec; // custom-emoji://...

    for (const QString& t : tokens)
    {
        if (t == QStringLiteral("**"))
        {
            hasBold = true;
        }
        else if (t == QStringLiteral("__"))
        {
            hasUnderline = true;
        }
        else if (t == QStringLiteral("~~"))
        {
            hasStrike = true;
        }
        else if (t == QStringLiteral("||"))
        {
            hasSpoiler = true;
        }
        else if (t == QStringLiteral("^^"))
        {
            hasSup = true;
        }
        else if (t == QStringLiteral("`"))
        {
            hasCode = true;
        }
        else if (t == QStringLiteral(">") || t == QStringLiteral(">^"))
        {
            quoteType = t;
        }
        else if (t.startsWith(QStringLiteral("custom-emoji://")))
        {
            emojiSpec = t;
        }
        else if (t.contains(QStringLiteral("://")))
        {
            linkUrl = t;
        }
    }

    QString text = segment;

    // Custom emoji: восстанавливаем наш псевдо-markdown [X](emoji:<id>?<size>)
    if (!emojiSpec.isEmpty())
    {
        const QString kPrefix = QStringLiteral("custom-emoji://");
        QString core = emojiSpec.mid(kPrefix.size()); // <document_id>?size
        text = QStringLiteral("[%1](emoji:%2)").arg(text, core);
    }

    // Ссылка: [text](url)
    if (!linkUrl.isEmpty())
    {
        text = QStringLiteral("[%1](%2)").arg(text, linkUrl);
    }

    // Внутренние стили. Фиксированный порядок, чтобы была стабильность.
    if (hasCode)
    {
        text = QStringLiteral("`%1`").arg(text);
    }
    if (hasBold)
    {
        text = QStringLiteral("**%1**").arg(text);
    }
    if (hasUnderline)
    {
        text = QStringLiteral("__%1__").arg(text);
    }
    if (hasStrike)
    {
        text = QStringLiteral("~~%1~~").arg(text);
    }
    if (hasSpoiler)
    {
        text = QStringLiteral("||%1||").arg(text);
    }
    if (hasSup)
    {
        text = QStringLiteral("^^%1^^").arg(text);
    }

    // Цитаты – наружный уровень
    if (quoteType == QStringLiteral(">"))
    {
        text = QStringLiteral(">%1<").arg(text);
    }
    else if (quoteType == QStringLiteral(">^"))
    {
        text = QStringLiteral(">^%1<^").arg(text);
    }

    return text;
}

} // end anonymous namespace

void TelegramFormatter::formatAndCopyToClipboard(const QString& markdownText)
{
    const QList<QPair<QRegularExpression, QString>> rules = {
        {QRegularExpression("```(\\w*)\\r?\\n?([\\s\\S]*?)\\r?\\n?```"),   "code-block"},
        {    QRegularExpression("\\[([^\\]]+)\\]\\(emoji:([^\\)]+)\\)"), "custom-emoji"},
        {                    QRegularExpression(">\\^([\\s\\S]*?)<\\^"),           ">^"},
        {                          QRegularExpression(">([\\s\\S]*?)<"),            ">"},
        {                        QRegularExpression("`([^`\\r\\n]+?)`"),            "`"},
        {                       QRegularExpression("\\*\\*(.*?)\\*\\*"),           "**"},
        {                               QRegularExpression("__(.*?)__"),           "__"},
        {                               QRegularExpression("~~(.*?)~~"),           "~~"},
        {                       QRegularExpression("\\|\\|(.*?)\\|\\|"),           "||"},
        {                       QRegularExpression("\\^\\^(.*?)\\^\\^"),           "^^"},
        {QRegularExpression("\\[([^\\]]+)\\]\\((?!emoji:)([^\\)]+)\\)"),         "link"}
    };

    auto result = parseText(markdownText, rules);
    QString cleanText = result.first;
    QList<FinalTag> finalTags = result.second;

    std::sort(finalTags.begin(), finalTags.end());

    QByteArray tagsBinary;
    QDataStream stream(&tagsBinary, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << (quint32)finalTags.size();

    for (const auto& tag : finalTags)
    {
        const QString& tagContent = tag.tagData;
        QByteArray tagData;
        for (const QChar& ch : tagContent)
        {
            ushort u = ch.unicode();
            tagData.append(static_cast<char>(u >> 8));
            tagData.append(static_cast<char>(u & 0xFF));
        }
        stream << (quint32)tag.position;
        stream << (quint32)tag.length;
        stream << (quint32)tagData.size();
        stream.writeRawData(tagData.constData(), tagData.size());
    }

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("application/x-td-field-text", cleanText.toUtf8());
    mimeData->setData("application/x-td-field-tags", tagsBinary);
    mimeData->setText(cleanText);

    QGuiApplication::clipboard()->setMimeData(mimeData);
}

QString TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasFormat("application/x-td-field-text"))
    {
        return {};
    }

    // 1. Извлекаем текст
    QByteArray textBytes = mimeData->data("application/x-td-field-text");
    QString cleanText = QString::fromUtf8(textBytes);
    if (!cleanText.isEmpty() && cleanText.back() == QChar(0))
        cleanText.chop(1);

    // 2. Извлекаем теги (используя наш Helper)
    QVector<TagRecord> tags;
    if (mimeData->hasFormat("application/x-td-field-tags"))
    {
        tags = parseTelegramTagsBinary(mimeData->data("application/x-td-field-tags"));
    }

    if (tags.isEmpty())
        return cleanText;

    // 3. Собираем границы сегментов
    QSet<qsizetype> boundaries = {0, cleanText.length()};
    for (const auto& tag : tags)
    {
        boundaries.insert(std::clamp(tag.position, 0LL, cleanText.length()));
        boundaries.insert(std::clamp(tag.position + tag.length, 0LL, cleanText.length()));
    }

    QList<qsizetype> sortedBoundaries = boundaries.values();
    std::sort(sortedBoundaries.begin(), sortedBoundaries.end());
    struct StyledSegment
    {
        QString text;
        QStringList tokens;
    };
    QList<StyledSegment> mergedSegments;

    for (int i = 0; i < sortedBoundaries.size() - 1; ++i)
    {
        qsizetype start = sortedBoundaries[i];
        qsizetype end = sortedBoundaries[i + 1];
        if (start >= end)
            continue;

        QString segmentText = cleanText.mid(start, end - start);
        QStringList activeTokens;
        for (const auto& tag : tags)
        {
            if (tag.position <= start && (tag.position + tag.length) >= end)
            {
                activeTokens.append(tag.tagContent.split('\\', Qt::SkipEmptyParts));
            }
        }
        activeTokens.removeDuplicates();
        activeTokens.sort(); // Сортируем для корректного сравнения списков

        // Если токены такие же, как у предыдущего сегмента — просто добавляем текст
        if (!mergedSegments.isEmpty() && mergedSegments.last().tokens == activeTokens)
        {
            mergedSegments.last().text += segmentText;
        }
        else
        {
            mergedSegments.append({segmentText, activeTokens});
        }
    }

    QString result;
    for (const auto& seg : mergedSegments)
    {
        // Добавляем проверку: если в сегменте только пробелы/переносы,
        // и это не ссылка/эмодзи, то не оборачиваем в стили (жирный/курсив),
        // чтобы не плодить лишние символы в пустых строках.
        if (seg.text.trimmed().isEmpty() && !seg.tokens.isEmpty())
        {
            bool isMedia = false;
            for (const auto& t : seg.tokens)
                if (t.contains("://") || t.startsWith("custom-emoji"))
                    isMedia = true;

            if (!isMedia)
            {
                result.append(seg.text);
                continue;
            }
        }

        result.append(applyTokensToSegment(seg.text, seg.tokens));
    }

    return result;
}