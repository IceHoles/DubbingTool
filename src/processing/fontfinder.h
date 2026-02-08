#ifndef FONTFINDER_H
#define FONTFINDER_H

#include "appsettings.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>

/**
 * @brief Information about a found font file
 */
struct FoundFontInfo
{
    QString path;
    QString familyName;
};

/**
 * @brief Result of font finding operation
 */
struct FontFinderResult
{
    QList<FoundFontInfo> foundFonts;
    QStringList notFoundFontNames;
};

/**
 * @brief Style information extracted from ASS file
 */
struct AssStyleInfo
{
    QString fontName;
    bool bold = false;
    bool italic = false;

    bool operator==(const AssStyleInfo& other) const
    {
        return fontName == other.fontName && bold == other.bold && italic == other.italic;
    }
};

inline size_t qHash(const AssStyleInfo& key, size_t seed = 0)
{
    return qHash(key.fontName, seed) ^ qHash(key.bold, seed) ^ qHash(key.italic, seed);
}

/**
 * @brief Native font finder using DirectWrite API
 *
 * Parses ASS subtitle files to extract font requirements (family, bold, italic)
 * and uses Windows DirectWrite API to find matching font files on the system.
 * This provides accurate font matching similar to libass/Aegisub behavior.
 */
class FontFinder : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FontFinder)

public:
    explicit FontFinder(QObject* parent = nullptr);
    ~FontFinder() override;

    /**
     * @brief Start asynchronous font finding for given ASS files
     * @param subFilesToCheck List of ASS file paths to analyze
     */
    void findFontsInSubs(const QStringList& subFilesToCheck);

    /**
     * @brief Parse inline font tags from ASS dialogue text
     * @param text Raw dialogue text with ASS tags
     * @param baseStyle Base style to use for defaults
     * @return Set of font styles found in inline tags
     *
     * This function extracts font requirements from inline ASS override tags.
     * It handles:
     * - \fn tags for font name changes
     * - \b tags for bold
     * - \i tags for italic
     *
     * The base style is only added to results if:
     * - There is text before the first tag block, OR
     * - The first tag block doesn't contain \fn override
     */
    static QSet<AssStyleInfo> parseInlineFontTags(const QString& text, const AssStyleInfo& baseStyle);

    /**
     * @brief Parse ASS file and extract unique font styles
     * @param filePath Path to ASS file
     * @return Set of unique font styles used in the file
     */
    QSet<AssStyleInfo> parseAssFile(const QString& filePath);

    /**
     * @brief Find system font file for given style (for testing)
     * @param style Font style to search for
     * @return Font file path if found, empty string otherwise
     *
     * This is a convenience method for testing that searches only system fonts.
     */
    QString findSystemFont(const AssStyleInfo& style);

signals:
    void logMessage(const QString& message, LogCategory category);
    void finished(const FontFinderResult& result);

private:
    /**
     * @brief Find font file path using DirectWrite API
     * @param style Font style to search for
     * @param additionalFontDirs Additional directories to search (e.g., attached_fonts)
     * @return Font file path if found, empty string otherwise
     */
    QString findFontFile(const AssStyleInfo& style, const QStringList& additionalFontDirs);

    /**
     * @brief Load fonts from additional directories (attached_fonts)
     * @param dirs Directories to scan for font files
     * @return Map of font family name (lowercase) to file path
     */
    static QMap<QString, QString> loadAdditionalFonts(const QStringList& dirs);

    /**
     * @brief Check if font file matches the required style
     * @param fontPath Path to font file
     * @param style Required style
     * @return true if font matches
     */
    static bool fontFileMatchesStyle(const QString& fontPath, const AssStyleInfo& style);

    /**
     * @brief Initialize DirectWrite factory
     * @return true if initialization succeeded
     */
    bool initDirectWrite();

    /**
     * @brief Clean up DirectWrite resources
     */
    void cleanupDirectWrite();

    void* m_dwriteFactory = nullptr;
    void* m_fontCollection = nullptr;
};

#endif // FONTFINDER_H
