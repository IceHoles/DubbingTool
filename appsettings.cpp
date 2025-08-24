    #include "appsettings.h"
#include <QStandardPaths>
#include <QDir>


static QString findExecutablePath(const QString &exeName) {
    QString path = QStandardPaths::findExecutable(exeName);
    return path.isEmpty() ? exeName : QDir::toNativeSeparators(path);
}

AppSettings& AppSettings::instance() {
    static AppSettings self;
    return self;
}

AppSettings::AppSettings(QObject *parent) : QObject(parent) {}

void AppSettings::load() {
    QSettings settings("MyCompany", "DubbingTool");

    m_qbittorrentHost = settings.value("webUi/host", "http://127.0.0.1").toString();
    m_qbittorrentPort = settings.value("webUi/port", 8080).toInt();
    m_qbittorrentUser = settings.value("webUi/user", "admin").toString();
    m_qbittorrentPassword = settings.value("webUi/password", "").toString();
    m_mkvmergePath = settings.value("paths/mkvmerge", findExecutablePath("mkvmerge.exe")).toString();
    m_mkvextractPath = settings.value("paths/mkvextract", findExecutablePath("mkvextract.exe")).toString();
    m_ffmpegPath = settings.value("paths/ffmpeg", findExecutablePath("ffmpeg.exe")).toString();
    m_qbittorrentPath = settings.value("paths/qbittorrent", findExecutablePath("qbittorrent.exe")).toString();
    m_nugenAmbPath = settings.value("paths/nugenAmb", "").toString();
    m_deleteTempFiles = settings.value("general/deleteTempFiles", true).toBool();
    m_userFileAction = static_cast<UserFileAction>(settings.value("general/userFileAction", static_cast<int>(UserFileAction::UseOriginalPath)).toInt());

    m_tbStyles.clear();
    int tbStylesCount = settings.beginReadArray("tbStyles");
    for (int i = 0; i < tbStylesCount; ++i) {
        settings.setArrayIndex(i);
        TbStyleInfo style;
        style.read(settings.value("style").toJsonObject());
        m_tbStyles.append(style);
    }
    settings.endArray();

    m_renderPresets.clear();
    int renderPresetsCount = settings.beginReadArray("renderPresets");
    for (int i = 0; i < renderPresetsCount; ++i) {
        settings.setArrayIndex(i);
        RenderPreset preset;
        preset.name = settings.value("name").toString();
        preset.commandPass1 = settings.value("commandPass1").toString();
        preset.commandPass2 = settings.value("commandPass2").toString();
        preset.targetBitrateKbps = settings.value("targetBitrateKbps", 0).toInt();
        m_renderPresets.append(preset);
    }
    settings.endArray();

    if (m_tbStyles.isEmpty() || m_renderPresets.isEmpty()) {
        loadDefaults();
        save();
    }

    m_enabledLogCategories.clear();
    QVariantList enabledCategoriesInts = settings.value("logging/enabledCategories").toList();
    if (enabledCategoriesInts.isEmpty()) {
        m_enabledLogCategories.insert(LogCategory::APP);
    } else {
        for (const QVariant& val : enabledCategoriesInts) {
            m_enabledLogCategories.insert(static_cast<LogCategory>(val.toInt()));
        }
    }
}

void AppSettings::save() {
    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("webUi/host", m_qbittorrentHost);
    settings.setValue("webUi/port", m_qbittorrentPort);
    settings.setValue("webUi/user", m_qbittorrentUser);
    settings.setValue("webUi/password", m_qbittorrentPassword);
    settings.setValue("paths/mkvmerge", m_mkvmergePath);
    settings.setValue("paths/mkvextract", m_mkvextractPath);
    settings.setValue("paths/ffmpeg", m_ffmpegPath);
    settings.setValue("paths/qbittorrent", m_qbittorrentPath);
    settings.setValue("paths/nugenAmb", m_nugenAmbPath);
    settings.setValue("general/deleteTempFiles", m_deleteTempFiles);
    settings.setValue("general/userFileAction", static_cast<int>(m_userFileAction));

    settings.beginWriteArray("tbStyles");
    for (int i = 0; i < m_tbStyles.size(); ++i) {
        settings.setArrayIndex(i);
        QJsonObject styleObj;
        m_tbStyles[i].write(styleObj);
        settings.setValue("style", styleObj);
    }
    settings.endArray();

    settings.beginWriteArray("renderPresets");
    for (int i = 0; i < m_renderPresets.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", m_renderPresets[i].name);
        settings.setValue("commandPass1", m_renderPresets[i].commandPass1);
        settings.setValue("commandPass2", m_renderPresets[i].commandPass2);
        settings.setValue("targetBitrateKbps", m_renderPresets[i].targetBitrateKbps);
    }
    settings.endArray();

    QVariantList enabledCategoriesInts;
    for (const auto& category : m_enabledLogCategories) {
        enabledCategoriesInts.append(static_cast<int>(category));
    }
    settings.setValue("logging/enabledCategories", enabledCategoriesInts);
}

void AppSettings::loadDefaults() {
    if (m_tbStyles.isEmpty()) {
        m_tbStyles.append({ "1080p (По умолчанию)",     1920,   "{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs50\\shad3\\bord1.3\\4c&H000000&\\4a&H00&}", 10, 30, 10, "Основной" });
        m_tbStyles.append({ "720p",                     1280,   "{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs33\\shad2\\bord1\\4c&H000000&\\4a&H00&}", 10, 20, 10, "Основной" });
        m_tbStyles.append({ "360p (Crunchyroll)",       640,    "{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs16.667\\shad1\\bord0.433\\4c&H000000&\\4a&H00&}", 3, 10, 3, "Основной" });
        m_tbStyles.append({ "360p (Повелитель тайн)",   832,    "{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs22.222\\shad1.333\\bord0.578\\4c&H000000&\\4a&H00&}", 4, 6, 4, "Main" });
    }
    if (m_renderPresets.isEmpty()) {
        RenderPreset nvenc, cpu_2pass, intel;

        // Команды содержат универсальный плейсхолдер %SIGNS%
        // WorkflowManager подставит туда свой _signs.ass
        // ManualRenderer подставит туда %INPUT% с нужным номером дорожки субтитров/субтитры указанные вручную или удалит фильтр
        // %INPUT% - входной файл в кавычках
        // %OUTPUT% - выходной файл в кавычках
        // %SIGNS% - экранированный путь к файлу с надписями для фильтра subtitles

        nvenc.name = "NVIDIA (hevc_nvenc, 1-проход)";
        nvenc.commandPass1 = "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v hevc_nvenc -preset p7 -tune hq -profile:v main -rc vbr -b:v 4M -minrate 4M -maxrate 8M -bufsize 16M -rc-lookahead 32 -spatial-aq 1 -aq-strength 15 -multipass 2 -2pass 1 -c:a aac -b:a 256k -map 0:v:0 -map 0:a:m:language:rus -tag:v hvc1 -map_metadata -1 -movflags +faststart \"%OUTPUT%\"";
        nvenc.targetBitrateKbps = 4000;
        cpu_2pass.name = "CPU (libx265, 2-прохода)";
        cpu_2pass.commandPass1 = "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v libx265 -b:v 4000k -preset medium -x265-params pass=1 -an -f mp4 NUL"; // Для Windows, для Linux /dev/null
        cpu_2pass.commandPass2 = "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v libx265 -b:v 4000k -preset medium -x265-params pass=2 -c:a aac -b:a 256k -map 0:v:0 -map 0:a:m:language:rus -tag:v hvc1 -map_metadata -1 -movflags +faststart \"%OUTPUT%\"";
        cpu_2pass.targetBitrateKbps = 4000;
        intel.name = "INTEL (hevc_qsv, 1-проход)";
        intel.commandPass1 = "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v hevc_qsv -rate_control VBR -b:v 4150k -minrate 4100k -maxrate 8000k -bufsize 8000k -g 48 -look_ahead 1 -look_ahead_depth 32 -preset slow -profile:v main -low_power 0 -c:a aac -b:a 256k -map 0:v:0 -map 0:a:m:language:rus -tag:v hvc1 -map_metadata -1 -movflags +faststart \"%OUTPUT%\"";
        intel.targetBitrateKbps = 4000;
        m_renderPresets.append(nvenc);
        m_renderPresets.append(cpu_2pass);
        m_renderPresets.append(intel);
    }
}

TbStyleInfo AppSettings::findTbStyle(const QString &name) const {
    for (const auto& style : m_tbStyles) {
        if (style.name == name) return style;
    }
    return m_tbStyles.isEmpty() ? TbStyleInfo() : m_tbStyles.first();
}

RenderPreset AppSettings::findRenderPreset(const QString &name) const {
    for (const auto& preset : m_renderPresets) {
        if (preset.name == name) return preset;
    }
    return m_renderPresets.isEmpty() ? RenderPreset() : m_renderPresets.first();
}


// Геттеры и сеттеры
QSet<LogCategory> AppSettings::enabledLogCategories() const { return m_enabledLogCategories; }
void AppSettings::setEnabledLogCategories(const QSet<LogCategory> &categories) { m_enabledLogCategories = categories; }
QString AppSettings::qbittorrentHost() const { return m_qbittorrentHost; }
void AppSettings::setQbittorrentHost(const QString &host) { m_qbittorrentHost = host; }
int AppSettings::qbittorrentPort() const { return m_qbittorrentPort; }
void AppSettings::setQbittorrentPort(int port) { m_qbittorrentPort = port; }
QString AppSettings::qbittorrentUser() const { return m_qbittorrentUser; }
void AppSettings::setQbittorrentUser(const QString &user) { m_qbittorrentUser = user; }
QString AppSettings::qbittorrentPassword() const { return m_qbittorrentPassword; }
void AppSettings::setQbittorrentPassword(const QString &password) { m_qbittorrentPassword = password; }
QString AppSettings::mkvmergePath() const { return m_mkvmergePath; }
void AppSettings::setMkvmergePath(const QString &path) { m_mkvmergePath = path; }
QString AppSettings::mkvextractPath() const { return m_mkvextractPath; }
void AppSettings::setMkvextractPath(const QString &path) { m_mkvextractPath = path; }
QString AppSettings::ffmpegPath() const { return m_ffmpegPath; }
void AppSettings::setFfmpegPath(const QString &path) { m_ffmpegPath = path; }
QString AppSettings::qbittorrentPath() const { return m_qbittorrentPath; }
void AppSettings::setQbittorrentPath(const QString &path) { m_qbittorrentPath = path; }
QString AppSettings::nugenAmbPath() const { return m_nugenAmbPath; }
void AppSettings::setNugenAmbPath(const QString &path) { m_nugenAmbPath = path; }
bool AppSettings::deleteTempFiles() const { return m_deleteTempFiles; }
void AppSettings::setDeleteTempFiles(bool enabled) { m_deleteTempFiles = enabled; }
UserFileAction AppSettings::userFileAction() const { return m_userFileAction; }
void AppSettings::setUserFileAction(UserFileAction action) { m_userFileAction = action; }
QList<TbStyleInfo> AppSettings::tbStyles() const { return m_tbStyles; }
void AppSettings::setTbStyles(const QList<TbStyleInfo> &styles) { m_tbStyles = styles; }
QList<RenderPreset> AppSettings::renderPresets() const { return m_renderPresets; }
void AppSettings::setRenderPresets(const QList<RenderPreset> &presets) { m_renderPresets = presets; }
