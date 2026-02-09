#include "assprocessor.h"

#include "appsettings.h"

#include <QDate>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>
#include <QTime>
#include <algorithm>

static QMap<QChar, double> createCharWidthsMap()
{
    QMap<QChar, double> map;
    map[QChar(' ')] = 0.38;
    map[QChar('!')] = 0.445;
    map[QChar('"')] = 0.635;
    map[QChar('#')] = 1.063;
    map[QChar('$')] = 0.827;
    map[QChar('%')] = 1.557;
    map[QChar('&')] = 1.015;
    map[QChar('\'')] = 0.358;
    map[QChar('(')] = 0.59;
    map[QChar(')')] = 0.59;
    map[QChar('*')] = 0.827;
    map[QChar('+')] = 1.063;
    map[QChar(',')] = 0.406;
    map[QChar('-')] = 0.56;
    map[QChar('.')] = 0.406;
    map[QChar('/')] = 0.75;
    map[QChar('0')] = 0.827;
    map[QChar('1')] = 0.827;
    map[QChar('2')] = 0.827;
    map[QChar('3')] = 0.827;
    map[QChar('4')] = 0.827;
    map[QChar('5')] = 0.827;
    map[QChar('6')] = 0.827;
    map[QChar('7')] = 0.827;
    map[QChar('8')] = 0.827;
    map[QChar('9')] = 0.827;
    map[QChar(':')] = 0.472;
    map[QChar(';')] = 0.472;
    map[QChar('<')] = 1.063;
    map[QChar('=')] = 1.063;
    map[QChar('>')] = 1.063;
    map[QChar('?')] = 0.736;
    map[QChar('@')] = 1.195;
    map[QChar('A')] = 0.889;
    map[QChar('B')] = 0.891;
    map[QChar('C')] = 0.867;
    map[QChar('D')] = 0.984;
    map[QChar('E')] = 0.799;
    map[QChar('F')] = 0.755;
    map[QChar('G')] = 0.968;
    map[QChar('H')] = 0.992;
    map[QChar('I')] = 0.628;
    map[QChar('J')] = 0.65;
    map[QChar('K')] = 0.904;
    map[QChar('L')] = 0.743;
    map[QChar('M')] = 1.16;
    map[QChar('N')] = 1.001;
    map[QChar('O')] = 1.0;
    map[QChar('P')] = 0.854;
    map[QChar('Q')] = 1.0;
    map[QChar('R')] = 0.943;
    map[QChar('S')] = 0.822;
    map[QChar('T')] = 0.795;
    map[QChar('U')] = 0.959;
    map[QChar('V')] = 0.876;
    map[QChar('W')] = 1.335;
    map[QChar('X')] = 0.889;
    map[QChar('Y')] = 0.871;
    map[QChar('Z')] = 0.808;
    map[QChar('[')] = 0.59;
    map[QChar('\\')] = 0.75;
    map[QChar(']')] = 0.59;
    map[QChar('^')] = 1.063;
    map[QChar('_')] = 0.827;
    map[QChar('`')] = 0.709;
    map[QChar('a')] = 0.777;
    map[QChar('b')] = 0.821;
    map[QChar('c')] = 0.685;
    map[QChar('d')] = 0.817;
    map[QChar('e')] = 0.771;
    map[QChar('f')] = 0.497;
    map[QChar('g')] = 0.817;
    map[QChar('h')] = 0.831;
    map[QChar('i')] = 0.392;
    map[QChar('j')] = 0.471;
    map[QChar('k')] = 0.782;
    map[QChar('l')] = 0.392;
    map[QChar('m')] = 1.238;
    map[QChar('n')] = 0.831;
    map[QChar('o')] = 0.802;
    map[QChar('p')] = 0.817;
    map[QChar('q')] = 0.817;
    map[QChar('r')] = 0.563;
    map[QChar('s')] = 0.668;
    map[QChar('t')] = 0.54;
    map[QChar('u')] = 0.831;
    map[QChar('v')] = 0.751;
    map[QChar('w')] = 1.155;
    map[QChar('x')] = 0.785;
    map[QChar('y')] = 0.748;
    map[QChar('z')] = 0.683;
    map[QChar(u'А')] = 0.889;
    map[QChar(u'Б')] = 0.891;
    map[QChar(u'В')] = 0.891;
    map[QChar(u'Г')] = 0.734;
    map[QChar(u'Д')] = 0.996;
    map[QChar(u'Е')] = 0.799;
    map[QChar(u'Ж')] = 1.356;
    map[QChar(u'З')] = 0.826;
    map[QChar(u'И')] = 1.007;
    map[QChar(u'Й')] = 1.007;
    map[QChar(u'К')] = 0.909;
    map[QChar(u'Л')] = 1.008;
    map[QChar(u'М')] = 1.16;
    map[QChar(u'Н')] = 0.992;
    map[QChar(u'О')] = 1.0;
    map[QChar(u'П')] = 0.992;
    map[QChar(u'Р')] = 0.854;
    map[QChar(u'С')] = 0.867;
    map[QChar(u'Т')] = 0.795;
    map[QChar(u'У')] = 0.869;
    map[QChar(u'Ф')] = 1.139;
    map[QChar(u'Х')] = 0.889;
    map[QChar(u'Ц')] = 1.008;
    map[QChar(u'Ч')] = 0.933;
    map[QChar(u'Ш')] = 1.415;
    map[QChar(u'Щ')] = 1.432;
    map[QChar(u'Ъ')] = 1.047;
    map[QChar(u'Ы')] = 1.247;
    map[QChar(u'Ь')] = 0.879;
    map[QChar(u'Э')] = 0.867;
    map[QChar(u'Ю')] = 1.464;
    map[QChar(u'Я')] = 0.93;
    map[QChar(u'а')] = 0.777;
    map[QChar(u'б')] = 0.815;
    map[QChar(u'в')] = 0.789;
    map[QChar(u'г')] = 0.647;
    map[QChar(u'д')] = 0.84;
    map[QChar(u'е')] = 0.771;
    map[QChar(u'ж')] = 1.15;
    map[QChar(u'з')] = 0.681;
    map[QChar(u'и')] = 0.847;
    map[QChar(u'й')] = 0.847;
    map[QChar(u'к')] = 0.783;
    map[QChar(u'л')] = 0.849;
    map[QChar(u'м')] = 0.983;
    map[QChar(u'н')] = 0.843;
    map[QChar(u'о')] = 0.802;
    map[QChar(u'п')] = 0.84;
    map[QChar(u'р')] = 0.817;
    map[QChar(u'с')] = 0.685;
    map[QChar(u'т')] = 0.676;
    map[QChar(u'у')] = 0.748;
    map[QChar(u'ф')] = 1.153;
    map[QChar(u'х')] = 0.785;
    map[QChar(u'ц')] = 0.852;
    map[QChar(u'ч')] = 0.8;
    map[QChar(u'ш')] = 1.199;
    map[QChar(u'щ')] = 1.212;
    map[QChar(u'ъ')] = 0.879;
    map[QChar(u'ы')] = 1.098;
    map[QChar(u'ь')] = 0.746;
    map[QChar(u'э')] = 0.69;
    map[QChar(u'ю')] = 1.198;
    map[QChar(u'я')] = 0.791;
    map[QChar(u'ѐ')] = 0.771;
    map[QChar(u'ё')] = 0.771;

    return map;
}

static double getVisualLength(const QString& text)
{
    static const QMap<QChar, double> CharWidths = createCharWidthsMap();

    double length = 0;
    for (const QChar& ch : text)
    {
        length += CharWidths.value(ch, 1.0);
    }
    return length;
}

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
                                       const ReleaseTemplate& t, const QString& startTime)
{
    emit logMessage("Начало обработки файла субтитров: " + inputPath, LogCategory::APP);

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly))
    {
        emit logMessage("Ошибка: не удалось открыть для чтения файл " + inputPath, LogCategory::APP);
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
                        LogCategory::APP);
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
        emit logMessage("Критическая ошибка: секция [Events] не найдена в файле субтитров.", LogCategory::APP);
        return false;
    }
    // +2 чтобы захватить и [Events] и строку Format:
    QStringList headers = originalLines.mid(0, eventsIndex + 2);
    QStringList events = originalLines.mid(eventsIndex + 2);

    QStringList tbLines = generateTb(t, startTime, playResX);

    int styleFormatIndex = -1;
    for (int i = 0; i < headers.size(); ++i)
    {
        if (headers[i].trimmed().startsWith("Format:", Qt::CaseInsensitive) &&
            headers[i - 1].trimmed() == "[V4+ Styles]")
        {
            styleFormatIndex = i;
            break;
        }
    }
    if (styleFormatIndex != -1)
    {
        headers.insert(
            styleFormatIndex + 1,
            "Style: ТБ,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1");
    }

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

    if (!writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents + tbLines))
    {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_full.ass", LogCategory::APP);
        return false;
    }
    emit logMessage("Создан файл с полными субтитрами: " + outputPathBase + "_full.ass", LogCategory::APP);

    if (!writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents + tbLines))
    {
        emit logMessage("Ошибка записи в файл: " + outputPathBase + "_signs.ass", LogCategory::APP);
        return false;
    }
    emit logMessage("Создан файл только с надписями: " + outputPathBase + "_signs.ass", LogCategory::APP);
    return true;
}

bool AssProcessor::generateTbOnlyFile(const QString& outputPath, const ReleaseTemplate& t, const QString& startTime,
                                      int resolutionX)
{
    emit logMessage("Генерация файла, содержащего только ТБ...", LogCategory::APP);

    QStringList headers;
    headers << "[Script Info]" << "; Script generated by DubbingTool" << "Title: Dubbing Credits"
            << "ScriptType: v4.00+" << "WrapStyle: 0" << "ScaledBorderAndShadow: yes"
            << QString("PlayResX: %1").arg(resolutionX) << QString("PlayResY: %1").arg(resolutionX * 9 / 16) << ""
            << "[V4+ Styles]"
            << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
               "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
               "MarginL, MarginR, MarginV, Encoding"
            << "Style: ТБ,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1"
            << ""
            << "[Events]"
            << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text";

    QStringList tbLines = generateTb(t, startTime, resolutionX);

    return writeAssFile(outputPath, headers + tbLines);
}

QString AssProcessor::balanceCastLine(const QStringList& actors, bool shouldSort)
{
    if (actors.isEmpty())
        return "";
    auto n = actors.size();
    if (n <= 2)
        return actors.join(", ");

    // --- Шаг 1: Подготовка ---
    // Создаем вектор пар (визуальный вес, имя) для удобной сортировки и обработки
    std::vector<std::pair<double, QString>> weightedActors;
    for (const QString& actor : actors)
    {
        weightedActors.push_back({getVisualLength(actor), actor});
    }

    // Сортируем по возрастанию веса
    if (shouldSort)
    {
        std::sort(weightedActors.begin(), weightedActors.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    // --- Шаг 2: Определяем конфигурацию и готовимся к перебору ---
    auto k = n / 2; // Количество актеров в первой строке (для 3->1, 4->2, 5->2)

    QStringList bestLine1, bestLine2;
    double minDiff = std::numeric_limits<double>::max();

    const double commaWeight = getVisualLength(",");
    const double separatorWeight = getVisualLength(", ");

    // --- Шаг 3: Перебор всех комбинаций ---
    // Мы будем генерировать все комбинации индексов для первой строки
    std::vector<int> p(n);
    std::iota(p.begin(), p.end(), 0); // Заполняем 0, 1, 2, ..., n-1

    std::vector<bool> v(n);
    std::fill(v.begin() + k, v.end(), true); // Маска для генерации C(n, k)

    do
    {
        QStringList currentLine1, currentLine2;
        double line1Weight = 0, line2Weight = 0;

        // Распределяем актеров по строкам согласно текущей комбинации
        for (int i = 0; i < n; ++i)
        {
            if (!v[i])
            { // Индексы, где v[i] == false, идут в первую строку
                currentLine1.append(weightedActors[p[i]].second);
                line1Weight += weightedActors[p[i]].first;
            }
            else
            {
                currentLine2.append(weightedActors[p[i]].second);
                line2Weight += weightedActors[p[i]].first;
            }
        }

        // --- Детальный расчет весов с учетом разделителей ---
        if (currentLine1.size() > 1)
        {
            line1Weight += separatorWeight * static_cast<double>(currentLine1.size() - 1);
        }
        line1Weight += commaWeight; // Финальная запятая перед \N

        if (currentLine2.size() > 1)
        {
            line2Weight += separatorWeight * static_cast<double>(currentLine2.size() - 1);
        }

        // --- Проверяем условия и ищем лучший вариант ---
        if (line1Weight + 1.0 <= line2Weight)
        {
            double currentDiff = line2Weight - line1Weight;
            if (currentDiff < minDiff)
            {
                minDiff = currentDiff;
                bestLine1 = currentLine1;
                bestLine2 = currentLine2;
            }
        }

    } while (std::next_permutation(v.begin(), v.end()) && shouldSort);

    // --- Шаг 4: Формирование результата ---
    if (bestLine1.isEmpty() || bestLine2.isEmpty())
    {
        // Фоллбэк на случай, если ни одна комбинация не подошла (маловероятно)
        // Просто делим отсортированный список пополам
        auto splitPoint = n / 2;
        for (decltype(n) i = 0; i < n; ++i)
        {
            if (i < splitPoint)
                bestLine1.append(weightedActors[i].second);
            else
                bestLine2.append(weightedActors[i].second);
        }
    }

    return bestLine1.join(", ") + ",\\N" + bestLine2.join(", ");
}

QStringList AssProcessor::generateTb(const ReleaseTemplate& t, const QString& startTime, int detectedResX)
{
    if (!t.generateTb)
    {
        emit logMessage("Генерация ТБ отключена в шаблоне.", LogCategory::APP);
        return QStringList();
    }

    QStringList tbLines;
    if (startTime.isEmpty())
    {
        emit logMessage("Предупреждение: время начала ТБ не было определено. ТБ не будет сгенерирован.",
                        LogCategory::APP);
        return tbLines;
    }

    QTime currentTime = QTime::fromString(startTime, "H:mm:ss.zzz");

    if (!currentTime.isValid())
    {
        emit logMessage("Ошибка: неверный формат времени для ТБ: '" + startTime + "'. ТБ не будет сгенерирован.",
                        LogCategory::APP);
        return tbLines;
    }

    TbStyleInfo tbStyleInfo;
    bool styleFound = false;

    const auto allStyles = AppSettings::instance().tbStyles();

    if (detectedResX > 0)
    {
        for (const auto& style : allStyles)
        {
            if (style.resolutionX == detectedResX)
            {
                tbStyleInfo = style;
                styleFound = true;
                emit logMessage("Найден стиль ТБ для разрешения " + QString::number(detectedResX) + "px: '" +
                                    style.name + "'",
                                LogCategory::APP);
                break;
            }
        }
    }
    if (!styleFound)
    {
        for (const auto& style : allStyles)
        {
            if (style.name == t.defaultTbStyleName)
            {
                tbStyleInfo = style;
                styleFound = true;
                emit logMessage("Стиль для разрешения не найден. Используется стиль по умолчанию: '" +
                                    t.defaultTbStyleName + "'",
                                LogCategory::APP);
                break;
            }
        }
    }
    if (!styleFound && !allStyles.isEmpty())
    {
        tbStyleInfo = allStyles.first();
        emit logMessage("Стиль по умолчанию не найден. Используется первый доступный стиль: '" + tbStyleInfo.name + "'",
                        LogCategory::APP);
    }

    auto generateDialogueLine = [&](const QString& text)
    {
        QTime endTime = currentTime.addSecs(3);
        QString startTimeStr =
            currentTime.toString("H:mm:ss") + "." + QString::number(currentTime.msec() / 10).rightJustified(2, '0');
        QString endTimeStr =
            endTime.toString("H:mm:ss") + "." + QString::number(endTime.msec() / 10).rightJustified(2, '0');
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
                            ? QString("Озвучено ТО Дубляжная в %1 году").arg(QDate::currentDate().year())
                            : QString("Дублировано ТО Дубляжная в %1 году").arg(QDate::currentDate().year());
    tbLines.append(generateDialogueLine(firstLine));

    if (!t.cast.isEmpty())
    {
        QList<QString> remainingCast = t.cast;

        bool isFirstChunk = true;
        while (!remainingCast.isEmpty())
        {
            QStringList chunk;
            int chunkSize = 4;
            if (remainingCast.size() == 5)
            {
                chunkSize = 5;
            }
            else if (remainingCast.size() == 6)
            {
                chunkSize = 3;
            }

            for (int i = 0; i < chunkSize && !remainingCast.isEmpty(); ++i)
            {
                chunk.append(remainingCast.takeFirst());
            }

            QString balancedChunk = balanceCastLine(chunk, !isFirstChunk);
            if (isFirstChunk)
            {
                balancedChunk = (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover)
                                    ? "Роли озвучивали:\\N" + balancedChunk
                                    : "Роли дублировали:\\N" + balancedChunk;
            }
            tbLines.append(generateDialogueLine(balancedChunk));
            isFirstChunk = false;
        }
    }

    QString directorLabel =
        (t.voiceoverType == ReleaseTemplate::VoiceoverType::Voiceover) ? "Куратор закадра: " : "Режиссёр дубляжа: ";
    if (!t.director.isEmpty())
    {
        QStringList directorBlock;
        directorBlock << directorLabel + t.director;
        if (!t.assistantDirector.isEmpty())
        {
            directorBlock << QString("Помощник режиссёра: %1").arg(t.assistantDirector);
        }
        tbLines.append(generateDialogueLine(directorBlock.join("\\N")));
    }

    QStringList soundEngineersBlock;
    if (!t.soundEngineer.isEmpty())
    {
        soundEngineersBlock << QString("Звукорежиссёр: %1").arg(t.soundEngineer);
    }
    if (!t.songsSoundEngineer.isEmpty())
    {
        soundEngineersBlock << QString("Звукорежиссёр песен: %1").arg(t.songsSoundEngineer);
    }
    if (!soundEngineersBlock.isEmpty())
    {
        tbLines.append(generateDialogueLine(soundEngineersBlock.join("\\N")));
    }

    QStringList studioSoundEngineersBlock;
    if (!t.episodeSoundEngineer.isEmpty())
    {
        studioSoundEngineersBlock << QString("Звукорежиссёр эпизода: %1").arg(t.episodeSoundEngineer);
    }
    if (!t.recordingSoundEngineer.isEmpty())
    {
        studioSoundEngineersBlock << QString("Звукорежиссёр записи: %1").arg(t.recordingSoundEngineer);
    }
    if (!studioSoundEngineersBlock.isEmpty())
    {
        tbLines.append(generateDialogueLine(studioSoundEngineersBlock.join("\\N")));
    }

    if (!t.videoLocalizationAuthor.isEmpty())
    {
        tbLines.append(generateDialogueLine("Локализация видеоряда: " + t.videoLocalizationAuthor));
    }

    QStringList authorsBlock;
    if (!t.timingAuthor.isEmpty())
    {
        authorsBlock << QString("Разметка: %1").arg(t.timingAuthor);
    }
    if (!t.signsAuthor.isEmpty())
    {
        authorsBlock << QString("Локализация надписей: %1").arg(t.signsAuthor);
    }
    if (!t.translationEditor.isEmpty())
    {
        authorsBlock << QString("Редактура перевода: %1").arg(t.translationEditor);
    }
    if (!t.subAuthor.isEmpty())
    {
        authorsBlock << QString("Перевод сериала на русский язык: %1").arg(t.subAuthor);
    }

    if (!authorsBlock.isEmpty())
    {
        tbLines.append(generateDialogueLine(authorsBlock.join("\\N")));
    }

    if (!t.releaseBuilder.isEmpty())
        tbLines.append(generateDialogueLine("Сборка релиза: " + t.releaseBuilder));

    emit logMessage(QString("Сгенерировано %1 строк ТБ.").arg(tbLines.size()), LogCategory::APP);
    return tbLines;
}

bool AssProcessor::addTbToFile(const QString& inputPath, const QString& outputPath, const ReleaseTemplate& t,
                               const QString& startTime)
{
    emit logMessage("Обработка файла, содержащего только надписи: " + inputPath, LogCategory::APP);

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly))
    {
        emit logMessage("Ошибка: не удалось открыть для чтения файл " + inputPath, LogCategory::APP);
        return false;
    }
    QTextStream in(&inputFile);
    in.setEncoding(QStringConverter::Utf8);
    QStringList originalLines = in.readAll().split('\n');
    inputFile.close();
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
        return false;
    }

    QStringList headers = originalLines.mid(0, eventsIndex + 2);
    QStringList events = originalLines.mid(eventsIndex + 2);
    QStringList tbLines = generateTb(t, startTime, 0);
    int styleFormatIndex = -1;
    for (int i = 0; i < headers.size(); ++i)
    {
        if (headers[i].trimmed().startsWith("Format:", Qt::CaseInsensitive) &&
            headers[i - 1].trimmed() == "[V4+ Styles]")
        {
            styleFormatIndex = i;
            break;
        }
    }
    if (styleFormatIndex != -1)
    {
        headers.insert(
            styleFormatIndex + 1,
            "Style: ТБ,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1");
    }
    if (!writeAssFile(outputPath, headers + events + tbLines))
    {
        emit logMessage("Ошибка записи в файл: " + outputPath, LogCategory::APP);
        return false;
    }
    emit logMessage("Создан файл только с надписями: " + outputPath, LogCategory::APP);
    return true;
}

bool AssProcessor::processFromTwoSources(const QString& subsInputPath, const QString& signsInputPath,
                                         const QString& outputPathBase, const ReleaseTemplate& t,
                                         const QString& startTime)
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

    // 3. Генерируем ТБ и вставляем стиль
    QStringList tbLines = generateTb(t, startTime, playResX);
    int styleFormatIndex = -1;
    // Ищем строку "Format: " в секции "[V4+ Styles]"
    for (int i = 0; i < headers.size(); ++i)
    {
        if (headers[i].trimmed().startsWith("Format:", Qt::CaseInsensitive))
        {
            // Убеждаемся, что предыдущая строка - это заголовок секции
            if (i > 0 && headers[i - 1].trimmed() == "[V4+ Styles]")
            {
                styleFormatIndex = i;
                break;
            }
        }
    }

    // Если нашли, куда вставлять, то вставляем
    if (styleFormatIndex != -1)
    {
        headers.insert(
            styleFormatIndex + 1,
            "Style: ТБ,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1");
    }
    else
    {
        // Если по какой-то причине секция стилей не найдена, это может быть проблемой,
        // но мы можем попробовать добавить ее целиком. Однако лучше просто выдать предупреждение.
        emit logMessage("Предупреждение: не удалось найти секцию [V4+ Styles] для добавления стиля ТБ.",
                        LogCategory::APP);
    }

    // 4. Записываем файлы
    writeAssFile(outputPathBase + "_full.ass", headers + fullSubsEvents + tbLines);
    writeAssFile(outputPathBase + "_signs.ass", headers + signsOnlyEvents + tbLines);

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
        emit logMessage("Ошибка: не удалось открыть файл для замен: " + filePath, LogCategory::APP);
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

int AssProcessor::calculateTbLineCount(const ReleaseTemplate& t)
{
    if (!t.generateTb)
    {
        return 0;
    }

    int lineCount = 0;

    // 1. Title line ("Дублировано/Озвучено ТО Дубляжная в YYYY году")
    ++lineCount;

    // 2. Cast chunks (same chunking logic as generateTbLines)
    if (!t.cast.isEmpty())
    {
        auto castSize = t.cast.size();
        while (castSize > 0)
        {
            ++lineCount;
            int chunkSize = 4;
            if (castSize == 5)
            {
                chunkSize = 5;
            }
            else if (castSize == 6)
            {
                chunkSize = 3;
            }
            castSize -= chunkSize;
        }
    }

    // 3. Director block (director + optional assistant director)
    if (!t.director.isEmpty())
    {
        ++lineCount;
    }

    // 4. Sound engineers block (soundEngineer and/or songsSoundEngineer)
    if (!t.soundEngineer.isEmpty() || !t.songsSoundEngineer.isEmpty())
    {
        ++lineCount;
    }

    // 5. Studio sound engineers block (episodeSoundEngineer and/or recordingSoundEngineer)
    if (!t.episodeSoundEngineer.isEmpty() || !t.recordingSoundEngineer.isEmpty())
    {
        ++lineCount;
    }

    // 6. Video localization author
    if (!t.videoLocalizationAuthor.isEmpty())
    {
        ++lineCount;
    }

    // 7. Authors block (timing, signs, translation editor, sub author)
    bool hasAuthorsBlock = !t.timingAuthor.isEmpty() || !t.signsAuthor.isEmpty() || !t.translationEditor.isEmpty() ||
                           !t.subAuthor.isEmpty();
    if (hasAuthorsBlock)
    {
        ++lineCount;
    }

    // 8. Release builder
    if (!t.releaseBuilder.isEmpty())
    {
        ++lineCount;
    }

    return lineCount;
}
