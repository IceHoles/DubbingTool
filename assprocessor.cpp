#include "assprocessor.h"
#include <QFile>
#include <QTextStream>
#include <QTime>
#include <QDebug>
#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>


// Вспомогательная функция для записи
static bool writeAssFile(const QString& path, const QStringList& lines)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly)) { return false; }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    for(const QString &line : lines) {
        out << line << "\n";
    }
    file.close();
    return true;
}

AssProcessor::AssProcessor(QObject *parent) : QObject{parent} {}

bool AssProcessor::processExistingFile(const QString &inputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime)
{
    emit logMessage("Начало обработки файла субтитров: " + inputPath);

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly)) {
        emit logMessage("Ошибка: не удалось открыть для чтения файл " + inputPath);
        return false;
    }

    QTextStream in(&inputFile);
    in.setEncoding(QStringConverter::Utf8);
    int playResX = 0;
    QStringList originalLines;
    while (!in.atEnd()) {
        QString line = in.readLine();
        originalLines << line;
        if (line.startsWith("PlayResX:")) {
            playResX = line.split(':').last().trimmed().toInt();
        }
    }
    inputFile.close();

    if (playResX == 0) {
        emit logMessage("Предупреждение: не удалось определить PlayResX. Будет использован стиль по умолчанию.");
    } else {
        emit logMessage(QString("Определено разрешение субтитров: %1px по ширине.").arg(playResX));
    }

    int eventsIndex = -1;
    for(int i = 0; i < originalLines.size(); ++i) {
        if (originalLines[i].trimmed() == "[Events]") {
            eventsIndex = i;
            break;
        }
    }
    if (eventsIndex == -1) {
        emit logMessage("Критическая ошибка: секция [Events] не найдена в файле субтитров.");
        return false;
    }
    // +2 чтобы захватить и [Events] и строку Format:
    QStringList headers = originalLines.mid(0, eventsIndex + 2);
    QStringList events = originalLines.mid(eventsIndex + 2);

    QStringList tbLines = generateTb(t, startTime, playResX);

    int styleFormatIndex = -1;
    for(int i = 0; i < headers.size(); ++i) {
        if (headers[i].trimmed().startsWith("Format:", Qt::CaseInsensitive) && headers[i-1].trimmed() == "[V4+ Styles]") {
            styleFormatIndex = i;
            break;
        }
    }
    if (styleFormatIndex != -1) {
        headers.insert(styleFormatIndex + 1, "Style: ТБ,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1");
    }

    QStringList fullSubsEvents, signsOnlyEvents;
    for (const QString &line : events) {
        if (!line.startsWith("Dialogue:")) continue;
        QStringList parts = line.split(',');
        if (parts.size() < 10) continue;
        QString style = parts[3].trimmed();
        QString actor = parts[4].trimmed();
        if (t.signStyles.contains(style, Qt::CaseInsensitive) || t.signStyles.contains(actor, Qt::CaseInsensitive)) {
            fullSubsEvents.append(line);
            signsOnlyEvents.append(line);
        } else {
            fullSubsEvents.append(line);
        }
    }

    if (!writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents + tbLines)) {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_full.ass");
        return false;
    }
    emit logMessage("Создан файл с полными субтитрами: " + outputPathBase + "_full.ass");

    if (!writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents + tbLines)) {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_signs.ass");
        return false;
    }
    emit logMessage("Создан файл только с надписями: " + outputPathBase + "_signs.ass");

    return true;
}

bool AssProcessor::generateTbOnlyFile(const QString &outputPath, const ReleaseTemplate &t, const QString& startTime, int resolutionX)
{
    emit logMessage("Генерация файла, содержащего только ТБ...");

    QStringList headers;
    headers << "[Script Info]" << "; Script generated by DubbingTool" << "Title: Dubbing Credits"
            << "ScriptType: v4.00+" << "WrapStyle: 0" << "ScaledBorderAndShadow: yes"
            << QString("PlayResX: %1").arg(resolutionX)
            << QString("PlayResY: %1").arg(resolutionX * 9 / 16)
            << ""
            << "[V4+ Styles]"
            << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding"
            << "Style: ТБ,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1"
            << ""
            << "[Events]"
            << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text";

    QStringList tbLines = generateTb(t, startTime, resolutionX);

    return writeAssFile(outputPath, headers + tbLines);
}

QString AssProcessor::balanceCastLine(const QStringList& actors, bool shouldSort)
{
    if (actors.isEmpty()) return "";
    if (actors.size() <= 2) return actors.join(", ");

    QStringList currentActors = actors;
    if (shouldSort) {
        std::sort(currentActors.begin(), currentActors.end());
    }

    QString bestCombination;
    int minDiff = INT_MAX;

    do {
        for (int i = 1; i < currentActors.size(); ++i) {
            QStringList line1_actors = currentActors.mid(0, i);
            QStringList line2_actors = currentActors.mid(i);
            QString line1_text = line1_actors.join(", ");
            QString line2_text = line2_actors.join(", ");

            if (line1_text.length() <= line2_text.length()) {
                int diff = line2_text.length() - line1_text.length();
                if (diff < minDiff) {
                    minDiff = diff;
                    bestCombination = line1_text + ",\\N" + line2_text;
                }
            }
        }
    } while (shouldSort && std::next_permutation(currentActors.begin(), currentActors.end()));

    // Если по какой-то причине комбинация не найдена, просто делим пополам
    if (bestCombination.isEmpty()) {
        int splitPoint = actors.size() / 2;
        bestCombination = actors.mid(0, splitPoint).join(", ") + "\\N" + actors.mid(splitPoint).join(", ");
    }
    return bestCombination;
}

QStringList AssProcessor::generateTb(const ReleaseTemplate &t, const QString &startTime, int detectedResX)
{
    if (!t.generateTb) {
        emit logMessage("Генерация ТБ отключена в шаблоне.");
        return QStringList(); // Возвращаем пустой список
    }

    QStringList tbLines;
    if (startTime.isEmpty()) {
        emit logMessage("Предупреждение: время начала ТБ не было определено. ТБ не будет сгенерирован.");
        return tbLines;
    }

    QTime currentTime = QTime::fromString(t.endingStartTime, "H:mm:ss.z");
    if (!currentTime.isValid()) {
        emit logMessage("Ошибка: неверный формат времени для ТБ. ТБ не будет сгенерирован.");
        return tbLines;
    }

    TbStyleInfo tbStyleInfo;
    bool styleFound = false;

    if (detectedResX > 0) {
        for(const auto& style : t.tbStyles) {
            if(style.resolutionX == detectedResX) {
                tbStyleInfo = style;
                styleFound = true;
                emit logMessage("Найден стиль ТБ для разрешения " + QString::number(detectedResX) + "px: '" + style.name + "'");
                break;
            }
        }
    }
    if (!styleFound) {
        for(const auto& style : t.tbStyles) {
            if(style.name == t.defaultTbStyleName) {
                tbStyleInfo = style;
                styleFound = true;
                emit logMessage("Стиль для разрешения не найден. Используется стиль по умолчанию: '" + t.defaultTbStyleName + "'");
                break;
            }
        }
    }
    if (!styleFound && !t.tbStyles.isEmpty()) {
        tbStyleInfo = t.tbStyles.first();
        styleFound = true;
        emit logMessage("Стиль по умолчанию не найден. Используется первый доступный стиль: '" + tbStyleInfo.name + "'");
    }
    if (!styleFound) {
        emit logMessage("Критическая ошибка: не найдено ни одного стиля для генерации ТБ. Проверьте настройки шаблона.");
        return tbLines;
    }

    auto generateDialogueLine = [&](const QString &text) {
        QTime endTime = currentTime.addSecs(3);
        QString startTimeStr = currentTime.toString("H:mm:ss") + "." + QString::number(currentTime.msec()/10).rightJustified(2, '0');
        QString endTimeStr = endTime.toString("H:mm:ss") + "." + QString::number(endTime.msec()/10).rightJustified(2, '0');
        currentTime = endTime;

        return QString("Dialogue: 0,%1,%2,ТБ,НАДПИСЬ,%3,%4,%5,,%6%7")
            .arg(startTimeStr)
            .arg(endTimeStr)
            .arg(tbStyleInfo.marginLeft)
            .arg(tbStyleInfo.marginRight)
            .arg(tbStyleInfo.marginV)
            .arg(tbStyleInfo.tags)
            .arg(text);
    };

    QString firstLine = (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover)
                            ? "Озвучено ТО Дубляжная в 2025 году"
                            : "Дублировано ТО Дубляжная в 2025 году";
    tbLines.append(generateDialogueLine(firstLine));

    if (!t.cast.isEmpty()) {
        QList<QString> remainingCast = t.cast;

        bool isFirstChunk = true;
        while(!remainingCast.isEmpty()) {
            QStringList chunk;
            int chunkSize = 4;
            if (remainingCast.size() == 5) {
                chunkSize = 5;
            } else if (remainingCast.size() == 6) {
                chunkSize = 3;
            }

            for(int i=0; i < chunkSize && !remainingCast.isEmpty(); ++i) {
                chunk.append(remainingCast.takeFirst());
            }

            QString balancedChunk = balanceCastLine(chunk, !isFirstChunk);
            if(isFirstChunk){
                balancedChunk =
                    (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover)
                        ? "Роли озвучивали:\\N" + balancedChunk
                        : "Роли дублировали:\\N" + balancedChunk;
            }
            tbLines.append(generateDialogueLine(balancedChunk));
            isFirstChunk = false;
        }
    }

    QString directorLabel = (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover)
                                ? "Куратор закадра: "
                                : "Режиссёр дубляжа: ";
    if (!t.director.isEmpty()) tbLines.append(generateDialogueLine(directorLabel + t.director));
    if (!t.soundEngineer.isEmpty()) tbLines.append(generateDialogueLine("Звукорежиссёр: " + t.soundEngineer));

        // --- НОВЫЙ БЛОК ДЛЯ ГЕНЕРАЦИИ СТРОКИ С АВТОРАМИ ПЕРЕВОДА ---
    QStringList authorsBlock;
    if (!t.timingAuthor.isEmpty()) {
        authorsBlock << QString("Разметка: %1").arg(t.timingAuthor);
    }
    if (!t.signsAuthor.isEmpty()) {
        authorsBlock << QString("Локализация надписей: %1").arg(t.signsAuthor);
    }
    if (!t.subAuthor.isEmpty()) {
        authorsBlock << QString("Перевод сериала на русский язык: %1").arg(t.subAuthor);
    }

    if (!authorsBlock.isEmpty()) {
        tbLines.append(generateDialogueLine(authorsBlock.join("\\N")));
    }
    // --- КОНЕЦ НОВОГО БЛОКА ---
    
    if (!t.releaseBuilder.isEmpty()) tbLines.append(generateDialogueLine("Сборка релиза: " + t.releaseBuilder));

    emit logMessage(QString("Сгенерировано %1 строк ТБ.").arg(tbLines.size()));
    return tbLines;
}

bool AssProcessor::processFromTwoSources(const QString &dialoguesInputPath, const QString &signsInputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime)
{
    emit logMessage(QString("Объединение субтитров: диалоги из '%1', надписи из '%2'").arg(QFileInfo(dialoguesInputPath).fileName()).arg(QFileInfo(signsInputPath).fileName()));

    // 1. Читаем файл с диалогами для заголовков и разрешения
    QFile dialoguesFile(dialoguesInputPath);
    if (!dialoguesFile.open(QIODevice::ReadOnly)) return false;
    QTextStream inDialogues(&dialoguesFile);
    inDialogues.setEncoding(QStringConverter::Utf8);

    int playResX = 0;
    QStringList headers;
    QStringList fullSubsEvents;
    bool inEvents = false;

    while (!inDialogues.atEnd()) {
        QString line = inDialogues.readLine();
        if (line.startsWith("PlayResX:")) playResX = line.split(':').last().trimmed().toInt();
        if (line.trimmed() == "[Events]") inEvents = true;

        if (!inEvents || !line.startsWith("Dialogue:")) {
            headers << line;
        } else {
            // Берем только строки, которые НЕ являются надписями
            QStringList parts = line.split(',');
            if (parts.size() < 5) continue;
            if (!t.signStyles.contains(parts[3].trimmed()) && !t.signStyles.contains(parts[4].trimmed())) {
                fullSubsEvents.append(line);
            }
        }
    }
    dialoguesFile.close();

    // 2. Читаем файл с надписями
    QFile signsFile(signsInputPath);
    if (!signsFile.open(QIODevice::ReadOnly)) return false;
    QTextStream inSigns(&signsFile);
    inSigns.setEncoding(QStringConverter::Utf8);
    QStringList signsOnlyEvents;
    inEvents = false;
    while (!inSigns.atEnd()) {
        QString line = inSigns.readLine();
        if (line.trimmed() == "[Events]") inEvents = true;
        if (inEvents && line.startsWith("Dialogue:")) {
            signsOnlyEvents.append(line);
        }
    }
    signsFile.close();

    // 3. Генерируем ТБ и вставляем стиль
    QStringList tbLines = generateTb(t, startTime, playResX);
    int styleFormatIndex = -1;
    // Ищем строку "Format: " в секции "[V4+ Styles]"
    for(int i = 0; i < headers.size(); ++i) {
        if (headers[i].trimmed().startsWith("Format:", Qt::CaseInsensitive)) {
            // Убеждаемся, что предыдущая строка - это заголовок секции
            if (i > 0 && headers[i-1].trimmed() == "[V4+ Styles]") {
                styleFormatIndex = i;
                break;
            }
        }
    }

    // Если нашли, куда вставлять, то вставляем
    if (styleFormatIndex != -1) {
        headers.insert(styleFormatIndex + 1, "Style: ТБ,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1");
    } else {
        // Если по какой-то причине секция стилей не найдена, это может быть проблемой,
        // но мы можем попробовать добавить ее целиком. Однако лучше просто выдать предупреждение.
        emit logMessage("Предупреждение: не удалось найти секцию [V4+ Styles] для добавления стиля ТБ.");
    }

    // 4. Записываем файлы
    writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents + tbLines);
    writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents + tbLines);

    return true;
}

bool AssProcessor::convertToSrt(const QString &inputAssPath, const QString &outputSrtPath, const QStringList &signStyles)
{
    emit logMessage("Конвертация в SRT: " + QFileInfo(inputAssPath).fileName());

    QFile inputFile(inputAssPath);
    if (!inputFile.open(QIODevice::ReadOnly)) return false;

    QFile outputFile(outputSrtPath);
    if (!outputFile.open(QIODevice::WriteOnly)) return false;

    QTextStream in(&inputFile);
    in.setEncoding(QStringConverter::Utf8);
    QTextStream out(&outputFile);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);

    bool inEvents = false;
    int lineCounter = 1;
    QRegularExpression tagRegex(R"(\{[^\}]+\})"); // Для удаления тегов

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed() == "[Events]") {
            inEvents = true;
            continue;
        }
        if (!inEvents || !line.startsWith("Dialogue:")) continue;

        QStringList parts = line.split(',');
        if (parts.size() < 10) continue;

        // Пропускаем надписи и ТБ
        QString style = parts[3].trimmed();
        QString actor = parts[4].trimmed();
        if (signStyles.contains(style, Qt::CaseInsensitive) || signStyles.contains(actor, Qt::CaseInsensitive) || actor == "НАДПИСЬ") {
            continue;
        }

        // Форматируем время для SRT
        // ASS: H:MM:SS.cs -> SRT: HH:MM:SS,ms
        QString startTime = parts[1].trimmed().replace('.', ',');
        QString endTime = parts[2].trimmed().replace('.', ',');
        // Добиваем миллисекунды до трех знаков
        startTime.append(QString(3 - startTime.split(',').last().length(), '0'));
        endTime.append(QString(3 - endTime.split(',').last().length(), '0'));

        // Собираем текст, удаляя все теги и заменяя \N на перенос строки
        QString text = parts.mid(9).join(',');
        text.remove(tagRegex);
        text.replace("\\N", "\n");

        // Записываем блок в SRT
        out << lineCounter << "\n";
        out << startTime << " --> " << endTime << "\n";
        out << text << "\n\n";

        lineCounter++;
    }

    emit logMessage("Файл SRT успешно создан: " + QFileInfo(outputSrtPath).fileName());
    return true;
}
