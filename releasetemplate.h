#ifndef RELEASETEMPLATE_H
#define RELEASETEMPLATE_H

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QJsonObject>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>


struct TbStyleInfo {
    QString name = "default_1080p"; // Название пресета
    int resolutionX = 0; // PlayResX, к которому привязан стиль
    QString tags = "{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs50\\shad3\\bord1.3\\4c&H000000&\\4a&H00&}";
    int marginLeft = 10;
    int marginRight = 30;
    int marginV = 10;
    QString styleName = "Основной";

    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;
};


class ReleaseTemplate
{
public:
    ReleaseTemplate();

    // Основные данные релиза
    QString templateName;       // Имя самого шаблона
    QString seriesTitle;        // Название сериала, напр. "Kowloon Generic Romance"
    QUrl rssUrl;                // Ссылка на RSS фид

    // Данные для сборки MKV
    QString animationStudio;    // Название студии анимации
    QString subAuthor;          // Автор перевода субтитров
    QString originalLanguage = "jpn"; // Язык оригинала
    QStringList cast;           // Список актеров дубляжа по умолчанию
    QStringList signStyles;     // Стили ASS, которые считаются надписями
    QString endingChapterName;  // Название главы MKV, обозначающей эндинг
    QString endingStartTime;    // Тайминг эндинга, если нет главы
    bool useManualTime = false; // Использовать тайминг по парсингу главы
    bool generateTb = true;     // Генерировать ТБ?
    bool createSrtMaster = false; // Нужен ли .mkv с .srt и дорогой без сжатия
    QString director;           // Режиссер
    QString soundEngineer;      // Звукорежиссер
    QString timingAuthor;       // Разметка (тайминг)
    QString signsAuthor;        // <<< НОВОЕ ПОЛЕ: Локализация надписей
    QString releaseBuilder;     // Сборка релиза

    enum class VoiceoverType { Dubbing, Voiceover };
    VoiceoverType voiceoverType = VoiceoverType::Dubbing;

    // Данные для qBittorrent Web API
    QString webUiHost;
    int webUiPort;
    QString webUiUser;
    QString webUiPassword;

    // Шаблоны для постов
    QMap<QString, QString> postTemplates;   // Ключ: "VK", "Telegram", Значение: шаблон
    QString seriesTitleForPost; // Название для поста, может отличаться от основного
    int totalEpisodes = 0;
    QString posterPath;
    // Шаблоны ссылок. Ключ: "AnilibUpload", "AnilibView", "VKComment" и т.д.
    QMap<QString, QString> linkTemplates;
    // Ссылки для загрузки
    QStringList uploadUrls;

    QList<TbStyleInfo> tbStyles;        // Список доступных стилей ТБ
    QString defaultTbStyleName;         // Имя стиля ТБ, используемого по умолчанию
    bool sourceHasSubtitles = true;     // Есть ли субтитры в исходнике
    QString targetAudioFormat = "aac";

    // Методы для сохранения/загрузки
    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;
};

#endif // RELEASETEMPLATE_H
