#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QClipboard>
#include <QMimeData>
#include <QDataStream>
#include <QMessageBox>
#include <QIODevice>
#include <algorithm>
#include <QMap>

struct FinalTag {
    qsizetype position;
    qsizetype length;
    QString tagData;
    bool operator<(const FinalTag& other) const { return position < other.position; }
};

struct MatchInfo {
    qsizetype start;
    qsizetype length;
    QString content;
    QString tag;
};

QPair<QString, QList<FinalTag>> parseText(const QString& text, const QList<QPair<QRegularExpression, QString>>& rules) {
    QString cleanText;
    QList<FinalTag> tags;

    QList<MatchInfo> allMatches;
    for (const auto& rulePair : rules) {
        auto it = rulePair.first.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            if (rulePair.second == "code-block") {
                QString lang = match.captured(1);
                QString code = match.captured(2);
                allMatches.append({match.capturedStart(0), match.capturedLength(0), code, "```" + lang});
            } else if (rulePair.second == "custom-emoji") {
                allMatches.append({match.capturedStart(0), match.capturedLength(0), match.captured(1), "custom-emoji://" + match.captured(2)});
            } else if (rulePair.second == "link") {
                allMatches.append({match.capturedStart(0), match.capturedLength(0), match.captured(1), match.captured(2)});
            } else {
                allMatches.append({match.capturedStart(0), match.capturedLength(0), match.captured(1), rulePair.second});
            }
        }
    }

    QList<MatchInfo> topLevelMatches;
    for (const auto& matchA : allMatches) {
        bool isNested = false;
        for (const auto& matchB : allMatches) {
            if (&matchA == &matchB) continue;
            if (matchA.start >= matchB.start && (matchA.start + matchA.length) <= (matchB.start + matchB.length)) {
                isNested = true;
                break;
            }
        }
        if (!isNested) {
            topLevelMatches.append(matchA);
        }
    }
    std::sort(topLevelMatches.begin(), topLevelMatches.end(), [](const auto& a, const auto& b){ return a.start < b.start; });

    qsizetype lastPos = 0;
    for (const auto& match : topLevelMatches) {
        cleanText.append(text.mid(lastPos, match.start - lastPos));

        QPair<QString, QList<FinalTag>> subResult;
        if (match.tag.startsWith("```")) {
            subResult.first = match.content;
        } else if (match.tag == ">" || match.tag == ">^") {
            QList<QPair<QRegularExpression, QString>> quoteRules = rules;
            quoteRules.removeIf([](const auto& rule){ return rule.second == "`"; });
            subResult = parseText(match.content, quoteRules);
        } else {
            subResult = parseText(match.content, rules);
        }

        qsizetype parentTagStartPos = cleanText.length();
        cleanText.append(subResult.first);

        qsizetype lastTaggedPosInSub = 0;
        std::sort(subResult.second.begin(), subResult.second.end());

        for (auto& subTag : subResult.second) {
            if (subTag.position > lastTaggedPosInSub) {
                tags.append({parentTagStartPos + lastTaggedPosInSub, subTag.position - lastTaggedPosInSub, match.tag});
            }
            QStringList combined = subTag.tagData.split('\\');
            combined.append(match.tag);
            std::sort(combined.begin(), combined.end());
            subTag.tagData = combined.join('\\');
            tags.append({parentTagStartPos + subTag.position, subTag.length, subTag.tagData});
            lastTaggedPosInSub = subTag.position + subTag.length;
        }
        if (lastTaggedPosInSub < subResult.first.length()) {
            tags.append({parentTagStartPos + lastTaggedPosInSub, subResult.first.length() - lastTaggedPosInSub, match.tag});
        }
        if (subResult.second.isEmpty() && !subResult.first.isEmpty()) {
            tags.append({parentTagStartPos, subResult.first.length(), match.tag});
        }

        lastPos = match.start + match.length;
    }
    cleanText.append(text.mid(lastPos));

    return {cleanText, tags};
}


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Telegram Formatter");
    ui->textEdit->setPlainText(
        "Это **жирный, __жирный курсив__ и снова просто жирный текст.**\n\n"
        "Кастомный эмодзи: [💙](emoji:5278229754099540071?30)\n\n"
        "```cpp\n"
        "// C++ code block\n"
        "std::cout << \"Hello\";\n"
        "```\n\n"
        ">^В цитате можно делать ~~зачеркнутый~~, но `моноширинный` не работает.<^\n"
        "Ссылка с [**жирным** текстом](https://qt.io/).\n"
        "Кастомный эмодзи: [💙](emoji:5278229754099540071?30)\n"
        ""
        );
}

MainWindow::~MainWindow() { delete ui; }
void MainWindow::on_copyButton_clicked() { processAndCopyText(); }

void MainWindow::processAndCopyText()
{
    const QString sourceText = ui->textEdit->toPlainText();

    const QList<QPair<QRegularExpression, QString>> rules = {
        {QRegularExpression("```(\\w*)\\r?\\n?([\\s\\S]*?)\\r?\\n?```"), "code-block"},
        {QRegularExpression("\\[([^\\]]+)\\]\\(emoji:([^\\)]+)\\)"), "custom-emoji"},
        {QRegularExpression(">\\^([\\s\\S]*?)<\\^"), ">^"},
        {QRegularExpression(">([\\s\\S]*?)<"), ">"},
        {QRegularExpression("`([^`\\r\\n]+?)`"), "`"},
        {QRegularExpression("\\*\\*(.*?)\\*\\*"), "**"},
        {QRegularExpression("__(.*?)__"), "__"},
        {QRegularExpression("~~(.*?)~~"), "~~"},
        {QRegularExpression("\\|\\|(.*?)\\|\\|"), "||"},
        {QRegularExpression("\\^\\^(.*?)\\^\\^"), "^^"},
        {QRegularExpression("\\[([^\\]]+)\\]\\((?!emoji:)([^\\)]+)\\)"), "link"}
    };

    auto result = parseText(sourceText, rules);
    QString cleanText = result.first;
    QList<FinalTag> finalTags = result.second;

    std::sort(finalTags.begin(), finalTags.end());

    QByteArray tagsBinary;
    QDataStream stream(&tagsBinary, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << (quint32)finalTags.size();

    for (const auto& tag : finalTags) {
        const QString& tagContent = tag.tagData;
        QByteArray tagData;
        for (const QChar& ch : tagContent) {
            tagData.append(static_cast<char>(ch.unicode() >> 8));
            tagData.append(static_cast<char>(ch.unicode() & 0xFF));
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
    QMessageBox::information(this, "Готово", "Форматированный текст скопирован в буфер обмена!");
}
