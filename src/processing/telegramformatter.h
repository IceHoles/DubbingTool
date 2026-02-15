#ifndef TELEGRAMFORMATTER_H
#define TELEGRAMFORMATTER_H

#include <QString>

class QMimeData;

class TelegramFormatter
{
public:
    // Главная функция, которую будем вызывать снаружи
    static void formatAndCopyToClipboard(const QString& markdownText);

    // Обратное преобразование: из Telegram-буфера обмена (application/x-td-field-*)
    // в внутренний псевдо-markdown формат.
    static QString fromTelegramClipboardToPseudoMarkdown(const QMimeData* mimeData);
};

#endif // TELEGRAMFORMATTER_H
