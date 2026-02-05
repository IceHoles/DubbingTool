#ifndef TELEGRAMFORMATTER_H
#define TELEGRAMFORMATTER_H

#include <QString>

class TelegramFormatter
{
public:
    // Главная функция, которую будем вызывать снаружи
    static void formatAndCopyToClipboard(const QString& markdownText);
};

#endif // TELEGRAMFORMATTER_H
