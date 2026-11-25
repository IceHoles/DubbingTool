#ifndef RELEASETEMPLATE_H
#define RELEASETEMPLATE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>

struct PostTemplateMeta
{
    QString title;
    QString platform;
    QString category;
    QStringList tags;
    int sortOrder = 0;
    bool builtin = false;

    void read(const QJsonObject& json);
    void write(QJsonObject& json) const;
};

class ReleaseTemplate
{
public:
    ReleaseTemplate();

    [[nodiscard]] static ReleaseTemplate newTemplateDefaults();

    // Основные данные релиза
    QString templateName;    // Имя самого шаблона
    QString seriesTitle;     // Название сериала
    QStringList releaseTags; // Теги для поиска в названии, например, "[HorribleSubs]", "[Erai-raws]", "(Chinese audio)"
    QUrl rssUrl;             // Ссылка на RSS фид

    // Данные для qBittorrent Web API
    QString webUiHost;
    int webUiPort;
    QString webUiUser;
    QString webUiPassword;

    // Данные для сборки MKV
    QString animationStudio;          // Название студии анимации
    QString subAuthor;                // Автор перевода субтитров
    QString originalLanguage = "jpn"; // Язык оригинала
    QStringList cast;                 // Список актеров дубляжа по умолчанию
    QStringList signStyles;           // Стили ASS, которые считаются надписями
    /// Если true — в авто-режиме показывается поле пути к XML глав на главной странице; при отсутствии глав —
    /// предупреждение в лог.
    bool chaptersEnabled = true;
    bool useOriginalAudio = false;    // Использовать оригинальную аудиодорожку
    bool isCustomTranslation = false; // Свой перевод или нет
    bool createSrtMaster = false;     // Нужен ли .mkv с .srt и аудио без сжатия
    QString director;                 // Режиссер
    QString assistantDirector;        // Помощник режиссера
    QString soundEngineer;            // Звукорежиссер
    QString songsSoundEngineer;       // Звукорежиссер песен
    QString episodeSoundEngineer;     // Звукорежиссер эпизода
    QString recordingSoundEngineer;   // Звукорежиссер записи
    QString videoLocalizationAuthor;  // Локализация видеоряда
    QString timingAuthor;             // Разметка (тайминг)
    QString signsAuthor;              // Локализация надписей
    QString translationEditor;        // Редактор перевода
    QString releaseBuilder;           // Сборка релиза
    enum class VoiceoverType
    {
        Dubbing,
        Voiceover
    };
    VoiceoverType voiceoverType = VoiceoverType::Dubbing; // Закадр ? Дубляж
    bool sourceHasSubtitles = true;                       // Есть ли субтитры в исходнике
    QString targetAudioFormat = "aac";                    // Формат аудио для .mkv
    bool forceSignStyleRequest = false;                   // Всегда запрашивать стили для надписей
    bool pauseForSubEdit = false;                         // Пауза для ручной правки субтитров
    QMap<QString, QString> substitutions;                 // Карта замен "Найти" -> "Заменить на"

    // Шаблоны для постов
    QMap<QString, QString> postTemplates;             // Ключ: "VK", "Telegram", Значение: шаблон
    QMap<QString, PostTemplateMeta> postTemplateMeta; // Метаданные для поиска/группировки шаблонов постов
    QString seriesTitleForPost;                       // Название для поста, может отличаться от основного
    int totalEpisodes = 0;                            // Общее число эпизодов для постов
    QString posterPath;                               // Путь к постеру
    QMap<QString, QString> linkTemplates;             // Ссылки на серию в постах

    // Ссылки для загрузки
    QStringList uploadUrls;
    QString renderPresetName;

    void read(const QJsonObject& json);
    void write(QJsonObject& json) const;
};

#endif // RELEASETEMPLATE_H
