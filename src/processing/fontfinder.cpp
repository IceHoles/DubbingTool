#include "fontfinder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>

#include <dwrite.h>
#include <dwrite_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

FontFinder::FontFinder(QObject* parent) : QObject(parent)
{
    initDirectWrite();
}

FontFinder::~FontFinder()
{
    cleanupDirectWrite();
}

bool FontFinder::initDirectWrite()
{
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&m_dwriteFactory));

    if (FAILED(hr) || m_dwriteFactory == nullptr)
    {
        return false;
    }

    auto* factory = static_cast<IDWriteFactory*>(m_dwriteFactory);
    hr = factory->GetSystemFontCollection(reinterpret_cast<IDWriteFontCollection**>(&m_fontCollection), FALSE);

    return SUCCEEDED(hr) && m_fontCollection != nullptr;
}

void FontFinder::cleanupDirectWrite()
{
    if (m_fontCollection != nullptr)
    {
        static_cast<IDWriteFontCollection*>(m_fontCollection)->Release();
        m_fontCollection = nullptr;
    }
    if (m_dwriteFactory != nullptr)
    {
        static_cast<IDWriteFactory*>(m_dwriteFactory)->Release();
        m_dwriteFactory = nullptr;
    }
}

void FontFinder::findFontsInSubs(const QStringList& subFilesToCheck)
{
    // Use QTimer to make this async and not block the UI
    QTimer::singleShot(
        0, this,
        [this, subFilesToCheck]()
        {
            FontFinderResult result;

            if (m_dwriteFactory == nullptr || m_fontCollection == nullptr)
            {
                emit logMessage("Ошибка: не удалось инициализировать DirectWrite", LogCategory::APP);
                emit finished(result);
                return;
            }

            if (subFilesToCheck.isEmpty())
            {
                emit finished(result);
                return;
            }

            // Collect additional font directories (attached_fonts next to ASS files)
            QStringList additionalFontDirs;
            for (const QString& assPath : subFilesToCheck)
            {
                QFileInfo fileInfo(assPath);
                QString attachedFontsDir = fileInfo.absolutePath() + "/attached_fonts";
                if (QDir(attachedFontsDir).exists())
                {
                    additionalFontDirs.append(attachedFontsDir);
                }
            }

            // Load additional fonts
            QMap<QString, QString> additionalFonts = loadAdditionalFonts(additionalFontDirs);

            // Parse all ASS files and collect unique font styles
            QSet<AssStyleInfo> allStyles;
            for (const QString& assPath : subFilesToCheck)
            {
                QSet<AssStyleInfo> fileStyles = parseAssFile(assPath);
                allStyles.unite(fileStyles);
            }

            emit logMessage(QString("Найдено %1 уникальных шрифтовых стилей для поиска").arg(allStyles.size()),
                            LogCategory::APP);

            // Track which fonts we've already found to avoid duplicates
            QSet<QString> foundPaths;

            // Find each font
            for (const AssStyleInfo& style : allStyles)
            {
                QString fontPath = findFontFile(style, additionalFontDirs);

                if (!fontPath.isEmpty() && !foundPaths.contains(fontPath))
                {
                    foundPaths.insert(fontPath);
                    FoundFontInfo info;
                    info.path = fontPath;
                    info.familyName = style.fontName;
                    result.foundFonts.append(info);
                    emit logMessage(QString("Найден шрифт: %1 -> %2").arg(style.fontName, fontPath), LogCategory::APP);
                }
                else if (fontPath.isEmpty())
                {
                    // Only add to not found if we haven't found any variant of this font
                    bool alreadyFound = false;
                    for (const FoundFontInfo& found : result.foundFonts)
                    {
                        if (found.familyName.compare(style.fontName, Qt::CaseInsensitive) == 0)
                        {
                            alreadyFound = true;
                            break;
                        }
                    }
                    if (!alreadyFound && !result.notFoundFontNames.contains(style.fontName))
                    {
                        result.notFoundFontNames.append(style.fontName);
                        emit logMessage(QString("Шрифт не найден: %1 (Bold: %2, Italic: %3)")
                                            .arg(style.fontName)
                                            .arg(style.bold ? "да" : "нет")
                                            .arg(style.italic ? "да" : "нет"),
                                        LogCategory::APP);
                    }
                }
            }

            emit finished(result);
        });
}

QSet<AssStyleInfo> FontFinder::parseAssFile(const QString& filePath)
{
    QSet<AssStyleInfo> styles;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit logMessage("Ошибка: не удалось открыть файл " + filePath, LogCategory::APP);
        return styles;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    // Map style names to their font info
    QMap<QString, AssStyleInfo> styleMap;
    bool inStylesSection = false;
    bool inEventsSection = false;

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();

        if (line == "[V4+ Styles]" || line == "[V4 Styles]")
        {
            inStylesSection = true;
            inEventsSection = false;
            continue;
        }
        if (line == "[Events]")
        {
            inStylesSection = false;
            inEventsSection = true;
            continue;
        }
        if (line.startsWith("[") && line.endsWith("]"))
        {
            inStylesSection = false;
            inEventsSection = false;
            continue;
        }

        // Parse style definitions
        // Format: Style: Name,Fontname,Fontsize,PrimaryColour,...,Bold,Italic,...
        if (inStylesSection && line.startsWith("Style:"))
        {
            QString styleData = line.mid(6).trimmed();
            QStringList parts = styleData.split(',');

            // V4+ Style format has 23 fields
            // Index 0: Name, 1: Fontname, 7: Bold (-1 = yes), 8: Italic (-1 = yes)
            if (parts.size() >= 9)
            {
                AssStyleInfo info;
                QString styleName = parts[0].trimmed();
                info.fontName = parts[1].trimmed();
                info.bold = (parts[7].trimmed() == "-1" || parts[7].trimmed() == "1");
                info.italic = (parts[8].trimmed() == "-1" || parts[8].trimmed() == "1");

                styleMap[styleName] = info;
                // Don't add to styles here - only add when actually used in dialogue
            }
        }

        // Parse dialogue lines for inline font overrides
        if (inEventsSection && line.startsWith("Dialogue:"))
        {
            QStringList parts = line.split(',');
            if (parts.size() >= 10)
            {
                QString styleName = parts[3].trimmed();
                QString text = parts.mid(9).join(',');

                // Get base style
                AssStyleInfo baseStyle;
                if (styleMap.contains(styleName))
                {
                    baseStyle = styleMap[styleName];
                }

                // Check if text has any style-changing inline tags (\fn, \b, \i)
                bool hasStyleOverride = text.contains("\\fn") || text.contains("\\b") || text.contains("\\i");

                if (hasStyleOverride)
                {
                    // Parse inline tags - they may override font/bold/italic
                    QSet<AssStyleInfo> inlineStyles = parseInlineFontTags(text, baseStyle);
                    styles.unite(inlineStyles);
                }
                else
                {
                    // No style override - use base style font
                    if (!baseStyle.fontName.isEmpty())
                    {
                        styles.insert(baseStyle);
                    }
                }
            }
        }
    }

    return styles;
}

QSet<AssStyleInfo> FontFinder::parseInlineFontTags(const QString& text, const AssStyleInfo& baseStyle) // static
{
    QSet<AssStyleInfo> styles;

    // Current state
    AssStyleInfo current = baseStyle;

    // Regex to find tag blocks: {...}
    QRegularExpression tagBlockRegex(R"(\{([^\}]*)\})");

    // Check if there's text before the first tag block that would use base style
    auto firstMatch = tagBlockRegex.match(text);
    bool hasTextBeforeFirstTag = !firstMatch.hasMatch() || firstMatch.capturedStart() > 0;

    // Check if ANY of the initial consecutive tag blocks contains \fn override
    // This handles cases like {=0=2}{\fnArial...} where \fn is in the second block
    bool initialTagsHaveFontOverride = false;
    if (!hasTextBeforeFirstTag)
    {
        // Text starts with tags - check all consecutive tag blocks at the start
        auto it = tagBlockRegex.globalMatch(text);
        qsizetype expectedPos = 0;
        while (it.hasNext())
        {
            auto match = it.next();
            // Check if this tag block is consecutive (no text gap)
            if (match.capturedStart() != expectedPos)
            {
                break; // There's text between tag blocks
            }

            QString tagContent = match.captured(1);
            if (tagContent.contains("\\fn"))
            {
                initialTagsHaveFontOverride = true;
                break;
            }
            expectedPos = match.capturedEnd();
        }
    }

    // Add base style only if there's text before first tag, or initial tags don't override font
    if (!baseStyle.fontName.isEmpty() && (hasTextBeforeFirstTag || !initialTagsHaveFontOverride))
    {
        styles.insert(baseStyle);
    }
    auto it = tagBlockRegex.globalMatch(text);

    while (it.hasNext())
    {
        auto match = it.next();
        QString tagBlock = match.captured(1);

        // Split by backslash to get individual tags
        QStringList tags = tagBlock.split('\\', Qt::SkipEmptyParts);

        bool fontChanged = false;

        for (const QString& tag : tags)
        {
            if (tag.startsWith("fn"))
            {
                // Font name change: \fnArial
                current.fontName = tag.mid(2).trimmed();
                if (current.fontName.isEmpty())
                {
                    current.fontName = baseStyle.fontName;
                }
                fontChanged = true;
            }
            else if (tag.startsWith("b") && tag.size() > 1 && tag[1].isDigit())
            {
                // Bold: \b1 or \b0 or \b700 etc
                QString value = tag.mid(1);
                if (value == "0")
                {
                    current.bold = false;
                }
                else
                {
                    // \b1 or weight >= 600 means bold
                    int weight = value.toInt();
                    current.bold = (weight >= 1 && (weight == 1 || weight >= 600));
                }
                fontChanged = true;
            }
            else if (tag.startsWith("i") && tag.size() > 1 && tag[1].isDigit())
            {
                // Italic: \i1 or \i0
                current.italic = (tag.mid(1).toInt() != 0);
                fontChanged = true;
            }
            else if (tag == "r" || tag.startsWith("r"))
            {
                // Reset to base style (or specific style via \rStyleName)
                // We don't have access to other styles here, so just reset to base
                current = baseStyle;
                fontChanged = true;
            }
        }

        if (fontChanged && !current.fontName.isEmpty())
        {
            styles.insert(current);
        }
    }

    return styles;
}

QString FontFinder::findSystemFont(const AssStyleInfo& style)
{
    if (!initDirectWrite())
    {
        return QString();
    }
    return findFontFile(style, QStringList());
}

QString FontFinder::findFontFile(const AssStyleInfo& style, const QStringList& additionalFontDirs)
{
    if (style.fontName.isEmpty())
    {
        return QString();
    }

    auto* collection = static_cast<IDWriteFontCollection*>(m_fontCollection);
    if (collection == nullptr)
    {
        return QString();
    }

    // Convert font name to wide string
    std::wstring fontNameW = style.fontName.toStdWString();

    // Find font family
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    HRESULT hr = collection->FindFamilyName(fontNameW.c_str(), &familyIndex, &exists);

    if (FAILED(hr) || exists == FALSE)
    {
        // Try searching in additional font directories
        QMap<QString, QString> additionalFonts = loadAdditionalFonts(additionalFontDirs);
        QString lowerName = style.fontName.toLower();
        if (additionalFonts.contains(lowerName))
        {
            return additionalFonts[lowerName];
        }
        return QString();
    }

    // Get font family
    ComPtr<IDWriteFontFamily> fontFamily;
    hr = collection->GetFontFamily(familyIndex, &fontFamily);
    if (FAILED(hr) || fontFamily == nullptr)
    {
        return QString();
    }

    // Determine weight and style
    DWRITE_FONT_WEIGHT weight = style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE fontStyle = style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;

    // Get matching font
    ComPtr<IDWriteFont> font;
    hr = fontFamily->GetFirstMatchingFont(weight, stretch, fontStyle, &font);
    if (FAILED(hr) || font == nullptr)
    {
        return QString();
    }

    // Get font face
    ComPtr<IDWriteFontFace> fontFace;
    hr = font->CreateFontFace(&fontFace);
    if (FAILED(hr) || fontFace == nullptr)
    {
        return QString();
    }

    // Get font files
    UINT32 fileCount = 0;
    hr = fontFace->GetFiles(&fileCount, nullptr);
    if (FAILED(hr) || fileCount == 0)
    {
        return QString();
    }

    std::vector<IDWriteFontFile*> fontFiles(fileCount);
    hr = fontFace->GetFiles(&fileCount, fontFiles.data());
    if (FAILED(hr))
    {
        return QString();
    }

    QString fontPath;

    for (UINT32 i = 0; i < fileCount && fontPath.isEmpty(); ++i)
    {
        IDWriteFontFile* fontFile = fontFiles[i];
        if (fontFile == nullptr)
        {
            continue;
        }

        // Get reference key
        const void* refKey = nullptr;
        UINT32 refKeySize = 0;
        hr = fontFile->GetReferenceKey(&refKey, &refKeySize);
        if (FAILED(hr))
        {
            fontFile->Release();
            continue;
        }

        // Get loader
        ComPtr<IDWriteFontFileLoader> loader;
        hr = fontFile->GetLoader(&loader);
        if (FAILED(hr))
        {
            fontFile->Release();
            continue;
        }

        // Try to cast to local font file loader
        ComPtr<IDWriteLocalFontFileLoader> localLoader;
        hr = loader.As(&localLoader);
        if (SUCCEEDED(hr) && localLoader != nullptr)
        {
            // Get path length
            UINT32 pathLength = 0;
            hr = localLoader->GetFilePathLengthFromKey(refKey, refKeySize, &pathLength);
            if (SUCCEEDED(hr) && pathLength > 0)
            {
                // Get path
                std::vector<wchar_t> pathBuffer(pathLength + 1);
                hr = localLoader->GetFilePathFromKey(refKey, refKeySize, pathBuffer.data(), pathLength + 1);
                if (SUCCEEDED(hr))
                {
                    fontPath = QString::fromWCharArray(pathBuffer.data());
                }
            }
        }

        fontFile->Release();
    }

    return fontPath;
}

QMap<QString, QString> FontFinder::loadAdditionalFonts(const QStringList& dirs) // static
{
    QMap<QString, QString> fonts;

    QStringList fontExtensions = {"*.ttf", "*.otf", "*.ttc", "*.woff", "*.woff2"};

    for (const QString& dirPath : dirs)
    {
        QDir dir(dirPath);
        if (!dir.exists())
        {
            continue;
        }

        QFileInfoList files = dir.entryInfoList(fontExtensions, QDir::Files);
        for (const QFileInfo& fileInfo : files)
        {
            // Use filename without extension as family name (rough approximation)
            QString baseName = fileInfo.baseName().toLower();
            // Remove common suffixes like -Bold, -Italic, etc.
            baseName = baseName.replace(
                QRegularExpression(
                    R"([-_](bold|italic|regular|light|medium|semibold|black|thin|heavy|oblique|condensed|expanded))"),
                "");
            fonts[baseName] = fileInfo.absoluteFilePath();
        }
    }

    return fonts;
}

bool FontFinder::fontFileMatchesStyle(const QString& fontPath, const AssStyleInfo& style) // static
{
    Q_UNUSED(fontPath)
    Q_UNUSED(style)
    // This is a simplified check - DirectWrite already does the matching for us
    return true;
}
