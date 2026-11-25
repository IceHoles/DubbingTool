#include "assprocessor.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

static bool writeAssFile(const QString& path, const QStringList& lines)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    for (const QString& line : lines)
    {
        out << line << "\n";
    }
    file.close();
    return true;
}

AssProcessor::AssProcessor(QObject* parent) : QObject{parent}
{
}

bool AssProcessor::processExistingFile(const QString& inputPath, const QString& outputPathBase,
                                       const ReleaseTemplate& t)
{
    emit logMessage("Начало обработки файла субтитров: " + inputPath, LogCategory::APP);

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly))
    {
        emit logMessage("Ошибка: не удалось открыть для чтения файл " + inputPath, LogCategory::APP, LogLevel::Error);
        return false;
    }

    QTextStream in(&inputFile);
    in.setEncoding(QStringConverter::Utf8);
    int playResX = 0;
    QStringList originalLines;
    while (!in.atEnd())
    {
        QString line = in.readLine();
        originalLines << line;
        if (line.startsWith("PlayResX:"))
        {
            playResX = line.split(':').last().trimmed().toInt();
        }
    }
    inputFile.close();

    if (playResX == 0)
    {
        emit logMessage("Предупреждение: не удалось определить PlayResX. Будет использован стиль по умолчанию.",
                        LogCategory::APP, LogLevel::Warning);
    }
    else
    {
        emit logMessage(QString("Определено разрешение субтитров: %1px по ширине.").arg(playResX), LogCategory::APP);
    }

    int eventsIndex = -1;
    for (int i = 0; i < originalLines.size(); ++i)
    {
        if (originalLines[i].trimmed() == "[Events]")
        {
            eventsIndex = i;
            break;
        }
    }
    if (eventsIndex == -1)
    {
        emit logMessage("Критическая ошибка: секция [Events] не найдена в файле субтитров.", LogCategory::APP,
                        LogLevel::Error);
        return false;
    }
    // +2 чтобы захватить и [Events] и строку Format:
    QStringList headers = originalLines.mid(0, eventsIndex + 2);
    QStringList events = originalLines.mid(eventsIndex + 2);

    QStringList fullSubsEvents, signsOnlyEvents;
    for (const QString& line : events)
    {
        if (!line.startsWith("Dialogue:"))
            continue;
        QStringList parts = line.split(',');
        if (parts.size() < 10)
            continue;
        QString style = parts[3].trimmed();
        QString actor = parts[4].trimmed();
        if (t.signStyles.contains(style, Qt::CaseInsensitive) || t.signStyles.contains(actor, Qt::CaseInsensitive))
        {
            fullSubsEvents.append(line);
            signsOnlyEvents.append(line);
        }
        else
        {
            fullSubsEvents.append(line);
        }
    }

    if (!writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents))
    {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_full.ass", LogCategory::APP, LogLevel::Error);
        return false;
    }
    emit logMessage("Создан файл с полными субтитрами: " + outputPathBase + "_full.ass", LogCategory::APP);

    if (!writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents))
    {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_signs.ass", LogCategory::APP, LogLevel::Error);
        return false;
    }
    emit logMessage("Создан файл только с надписями: " + outputPathBase + "_signs.ass", LogCategory::APP);
    return true;
}

bool AssProcessor::copySignsFile(const QString& inputPath, const QString& outputPath)
{
    emit logMessage("Копирование файла надписей: " + inputPath, LogCategory::APP);
    if (QFileInfo(inputPath) == QFileInfo(outputPath))
    {
        return true;
    }
    if (QFile::exists(outputPath))
    {
        QFile::remove(outputPath);
    }
    if (!QFile::copy(inputPath, outputPath))
    {
        emit logMessage("Ошибка копирования в файл: " + outputPath, LogCategory::APP, LogLevel::Error);
        return false;
    }
    emit logMessage("Создан файл только с надписями: " + outputPath, LogCategory::APP);
    return true;
}

bool AssProcessor::processFromTwoSources(const QString& subsInputPath, const QString& signsInputPath,
                                         const QString& outputPathBase, const ReleaseTemplate& t)
{
    emit logMessage(QString("Объединение субтитров: диалоги из '%1', надписи из '%2'")
                        .arg(QFileInfo(subsInputPath).fileName())
                        .arg(QFileInfo(signsInputPath).fileName()),
                    LogCategory::APP);

    // 1. Читаем файл с диалогами для заголовков и разрешения
    QFile subsFile(subsInputPath);
    if (!subsFile.open(QIODevice::ReadOnly))
        return false;
    QTextStream inDialogues(&subsFile);
    inDialogues.setEncoding(QStringConverter::Utf8);

    int playResX = 0;
    int playResXSigns = 0;
    QStringList headers;
    QStringList fullSubsEvents;
    bool inEvents = false;

    while (!inDialogues.atEnd())
    {
        QString line = inDialogues.readLine();
        if (line.startsWith("PlayResX:"))
            playResX = line.split(':').last().trimmed().toInt();
        if (line.trimmed() == "[Events]")
            inEvents = true;

        if (!inEvents || !line.startsWith("Dialogue:"))
        {
            headers << line;
        }
        else
        {
            // Берем только строки, которые НЕ являются надписями
            QStringList parts = line.split(',');
            if (parts.size() < 5)
                continue;
            if (!t.signStyles.contains(parts[3].trimmed()) && !t.signStyles.contains(parts[4].trimmed()))
            {
                fullSubsEvents.append(line);
            }
        }
    }
    subsFile.close();

    // 2. Читаем файл с надписями
    QFile signsFile(signsInputPath);
    if (!signsFile.open(QIODevice::ReadOnly))
        return false;
    QTextStream inSigns(&signsFile);
    inSigns.setEncoding(QStringConverter::Utf8);
    QStringList signsOnlyEvents;
    inEvents = false;
    while (!inSigns.atEnd())
    {
        QString line = inSigns.readLine();
        if (line.startsWith("PlayResX:"))
            playResXSigns = line.split(':').last().trimmed().toInt();
        if (line.trimmed() == "[Events]")
            inEvents = true;
        if (inEvents && line.startsWith("Dialogue:"))
        {
            fullSubsEvents.append(line);
            signsOnlyEvents.append(line);
        }
    }
    signsFile.close();

    if (playResX != playResXSigns)
        emit logMessage(QString("PlayResX из субтитров и надписей не совпадают, отображение надписей может быть "
                                "некорректным: Субтитры %1 px, надписи %2 px ")
                            .arg(playResX)
                            .arg(playResXSigns),
                        LogCategory::APP);

    // 3. Записываем файлы
    writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents);
    writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents);

    return true;
}

QString AssProcessor::convertAssTagsToSrt(const QString& assText)
{
    QString result;
    QString text = assText;

    // Regex для поиска всех блоков с тегами
    QRegularExpression tagBlockRegex(R"(\{[^\}]+\})");

    qsizetype lastPos = 0;
    auto it = tagBlockRegex.globalMatch(text);

    while (it.hasNext())
    {
        auto match = it.next();
        // Добавляем обычный текст, который был перед тегом
        result.append(text.mid(lastPos, match.capturedStart() - lastPos));
        lastPos = match.capturedEnd();

        // Обрабатываем содержимое блока тегов
        QString blockContent = match.captured(0);
        blockContent = blockContent.mid(1, blockContent.length() - 2); // убираем {}

        // Блок может содержать несколько тегов, например {\i1\b1\fs30}
        QStringList tags = blockContent.split('\\', Qt::SkipEmptyParts);

        for (const QString& tag : tags)
        {
            if (tag == "i1")
                result.append("<i>");
            else if (tag == "i0")
                result.append("</i>");
            else if (tag == "b1")
                result.append("<b>");
            else if (tag == "b0")
                result.append("</b>");
            else if (tag == "u1")
                result.append("<u>");
            else if (tag == "u0")
                result.append("</u>");
            else if (tag.startsWith("an") && tag.length() == 3 && tag[2].isDigit() && tag[2] != '0' && tag[2] != '2')
            {
                // Восстанавливаем оригинальный тег для SRT
                result.append("{\\" + tag + "}");
            }
            // Все остальные теги (fs, fn, c, fad, и т.д.) просто игнорируются
        }
    }
    // Добавляем оставшийся текст после последнего тега
    result.append(text.mid(lastPos));

    // Тег \N обрабатывается отдельно и заменяется на стандартный перенос строки
    result.replace(QRegularExpression(R"(\\[Nn])"), "\n");

    return result;
}

bool AssProcessor::convertToSrt(const QString& inputAssPath, const QString& outputSrtPath,
                                const QStringList& signStyles)
{
    emit logMessage("Конвертация в SRT: " + QFileInfo(inputAssPath).fileName(), LogCategory::APP);

    QFile inputFile(inputAssPath);
    if (!inputFile.open(QIODevice::ReadOnly))
        return false;

    QFile outputFile(outputSrtPath);
    if (!outputFile.open(QIODevice::WriteOnly))
        return false;

    QTextStream in(&inputFile);
    in.setEncoding(QStringConverter::Utf8);
    QTextStream out(&outputFile);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);

    QMap<QString, QPair<QString, QString>> styleInfo; // Карта "Имя стиля" -> "SRT тег, тег \an"
    bool inStylesSection = false;

    while (!in.atEnd())
    {
        QString line = in.readLine();
        if (line.trimmed() == "[V4+ Styles]")
        {
            inStylesSection = true;
            continue;
        }
        if (inStylesSection && (line.trimmed().isEmpty() || line.startsWith("[")))
        {
            break;
        }

        if (inStylesSection && line.startsWith("Style:"))
        {
            // Используем split с KeepEmptyParts, чтобы не сдвигались индексы
            QStringList parts = line.split(',', Qt::KeepEmptyParts);
            // Формат v4+ имеет 23 поля, но Alignment - 19-е (индекс 18)
            if (parts.size() < 19)
                continue;

            QString styleName = parts[0].split(':').last().trimmed();

            // Теги форматирования (жирный, курсив)
            bool isItalic = (parts[8].trimmed() == "-1");
            bool isBold = (parts[7].trimmed() == "-1");
            QString formatTags;
            if (isBold)
                formatTags += "<b>";
            if (isItalic)
                formatTags += "<i>";

            // Тег выравнивания
            int alignment = parts[18].trimmed().toInt();
            QString alignmentTag;
            if (alignment != 0 && alignment != 2)
            {
                // Стандартные значения для SRT: 7, 8, 9, 4, 5, 6, 1, 2, 3
                alignmentTag = QString("{\\an%1}").arg(alignment);
            }
            styleInfo[styleName] = {formatTags, alignmentTag};
        }
    }

    in.seek(0);
    bool inEvents = false;
    int lineCounter = 1;

    while (!in.atEnd())
    {
        QString line = in.readLine();
        if (line.trimmed() == "[Events]")
        {
            inEvents = true;
            continue;
        }
        if (!inEvents || !line.startsWith("Dialogue:"))
            continue;

        QStringList parts = line.split(',');
        if (parts.size() < 10)
            continue;

        // Пропускаем надписи и ТБ
        QString style = parts[3].trimmed();
        QString actor = parts[4].trimmed();
        if (signStyles.contains(style, Qt::CaseInsensitive) || signStyles.contains(actor, Qt::CaseInsensitive) ||
            actor == "НАДПИСЬ")
        {
            continue;
        }

        // Форматируем время для SRT
        // ASS: H:MM:SS.cs -> SRT: HH:MM:SS,ms
        QString startTimeStr = parts[1].trimmed();
        QStringList st_parts = startTimeStr.split(':');
        QString srtStartTime = "00:00:00,000";
        if (st_parts.size() == 3)
        {
            int st_h = st_parts[0].toInt();
            int st_m = st_parts[1].toInt();
            QStringList st_sec_cs = st_parts[2].split('.');
            if (st_sec_cs.size() == 2)
            {
                int st_s = st_sec_cs[0].toInt();
                int st_ms = st_sec_cs[1].toInt() * 10; // cs -> ms
                srtStartTime = QString::asprintf("%02d:%02d:%02d,%03d", st_h, st_m, st_s, st_ms);
            }
        }

        // --- Обработка EndTime ---
        QString endTimeStr = parts[2].trimmed();
        QStringList et_parts = endTimeStr.split(':');
        QString srtEndTime = "00:00:00,000";
        if (et_parts.size() == 3)
        {
            int et_h = et_parts[0].toInt();
            int et_m = et_parts[1].toInt();
            QStringList et_sec_cs = et_parts[2].split('.');
            if (et_sec_cs.size() == 2)
            {
                int et_s = et_sec_cs[0].toInt();
                int et_ms = et_sec_cs[1].toInt() * 10; // cs -> ms
                srtEndTime = QString::asprintf("%02d:%02d:%02d,%03d", et_h, et_m, et_s, et_ms);
            }
        }

        // Собираем текст и конвертируем теги
        QString text = parts.mid(9).join(',');
        text = convertAssTagsToSrt(text);

        QString finalLine = text;

        if (styleInfo.contains(style))
        {
            const auto& info = styleInfo.value(style);
            QString openingTags = info.first;   // <b><i>
            QString alignmentTag = info.second; // {\an8}

            QString closingTags;
            if (openingTags.contains("<i>"))
                closingTags.prepend("</i>");
            if (openingTags.contains("<b>"))
                closingTags.prepend("</b>");

            // Собираем финальную строку: Сначала выравнивание, потом теги форматирования
            finalLine = alignmentTag + openingTags + text + closingTags;
        }

        // Записываем блок в SRT
        out << lineCounter << "\n";
        out << srtStartTime << " --> " << srtEndTime << "\n";
        out << finalLine.trimmed() << "\n\n";

        lineCounter++;
    }

    emit logMessage("Файл SRT успешно создан: " + QFileInfo(outputSrtPath).fileName(), LogCategory::APP);
    return true;
}

bool AssProcessor::applySubstitutions(const QString& filePath, const QMap<QString, QString>& substitutions)
{
    if (substitutions.isEmpty() || filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        return true;
    }

    emit logMessage("Применение автоматических замен в файле: " + QFileInfo(filePath).fileName(), LogCategory::APP);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit logMessage("Ошибка: не удалось открыть файл для замен: " + filePath, LogCategory::APP, LogLevel::Error);
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QStringList lines = in.readAll().split('\n');
    file.close();

    int substitutionsCount = 0;
    for (QString& line : lines)
    {
        if (line.startsWith("Dialogue:"))
        {
            QStringList parts = line.split(',');
            if (parts.size() >= 10)
            {
                QString textPart = parts.mid(9).join(',');
                QString originalText = textPart;

                for (auto it = substitutions.constBegin(); it != substitutions.constEnd(); ++it)
                {
                    textPart.replace(it.key(), it.value());
                }

                if (textPart != originalText)
                {
                    substitutionsCount++;
                    // Собираем строку обратно
                    QStringList newLineParts = parts.mid(0, 9);
                    newLineParts.append(textPart);
                    line = newLineParts.join(',');
                }
            }
        }
    }

    if (substitutionsCount > 0)
    {
        emit logMessage(QString("Выполнено %1 замен.").arg(substitutionsCount), LogCategory::APP);
        return writeAssFile(filePath, lines);
    }
    else
    {
        emit logMessage("Замены не потребовались.", LogCategory::APP);
        return true;
    }
}
