#include "telegrampasteedit.h"

#include "../processing/telegramformatter.h"

#include <QMimeData>

void TelegramPasteEdit::insertFromMimeData(const QMimeData* source)
{
    const QString telegramMarkdown = TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(source);

    if (!telegramMarkdown.isEmpty())
    {
        // Если формат распознан — вставляем обработанный текст
        insertPlainText(telegramMarkdown);
    }
    else
    {
        // Если нет — вызываем стандартное поведение (обычный текст, файлы и т.д.)
        QPlainTextEdit::insertFromMimeData(source);
    }
}