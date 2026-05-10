#ifndef TELEGRAMFORMATTER_H
#define TELEGRAMFORMATTER_H

#include <QString>

class QMimeData;

class TelegramFormatter
{
public:
    static void formatAndCopyToClipboard(const QString& markdownText);

    /**
     * @brief Builds Telegram TD clipboard mime (application/x-td-field-text/tags) from pseudo-markdown.
     * Caller owns the returned pointer. Used by formatAndCopyToClipboard and unit tests (no real clipboard).
     */
    [[nodiscard]] static QMimeData* createTdMimeDataFromMarkdown(const QString& markdownText);

    static QString fromTelegramClipboardToPseudoMarkdown(const QMimeData* mimeData);
};

#endif // TELEGRAMFORMATTER_H
