#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QSettings>
#include <QList>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include "releasetemplate.h"


enum class LogCategory {
    APP,
    FFMPEG,
    MKVTOOLNIX,
    QBITTORRENT,
    DEBUG
};

enum class UserFileAction {
    Move,
    Copy,
    UseOriginalPath
};

struct RenderPreset {
    QString name;
    QString commandPass1;
    QString commandPass2;       // Может быть пустой для однопроходного
    int targetBitrateKbps = 0;  // Целевой битрейт в кбит/с. 0 - не проверять.

    bool isTwoPass() const { return !commandPass2.isEmpty(); }
};

class AppSettings : public QObject
{
    Q_OBJECT
private:
    explicit AppSettings(QObject *parent = nullptr);
    AppSettings(const AppSettings&) = delete;
    AppSettings& operator=(const AppSettings&) = delete;

public:
    static AppSettings& instance();
    void load();
    void save();

    QSet<LogCategory> enabledLogCategories() const;
    void setEnabledLogCategories(const QSet<LogCategory> &categories);
    QString qbittorrentHost() const;
    void setQbittorrentHost(const QString &host);
    int qbittorrentPort() const;
    void setQbittorrentPort(int port);
    QString qbittorrentUser() const;
    void setQbittorrentUser(const QString &user);
    QString qbittorrentPassword() const;
    void setQbittorrentPassword(const QString &password);
    QString mkvmergePath() const;
    void setMkvmergePath(const QString &path);
    QString mkvextractPath() const;
    void setMkvextractPath(const QString &path);
    QString ffmpegPath() const;
    void setFfmpegPath(const QString &path);
    QString ffprobePath() const;
    QString qbittorrentPath() const;
    void setQbittorrentPath(const QString &path);
    QString nugenAmbPath() const;
    void setNugenAmbPath(const QString &path);
    bool deleteTempFiles() const;
    void setDeleteTempFiles(bool enabled);
    UserFileAction userFileAction() const;
    void setUserFileAction(UserFileAction action);
    QList<TbStyleInfo> tbStyles() const;
    void setTbStyles(const QList<TbStyleInfo> &styles);
    TbStyleInfo findTbStyle(const QString& name) const;
    QList<RenderPreset> renderPresets() const;
    void setRenderPresets(const QList<RenderPreset> &presets);
    RenderPreset findRenderPreset(const QString& name) const;
    void setManualRenderPreset();
    RenderPreset manualRenderPreset(const QString& name) const;

private:
    void loadDefaults();
    QSet<LogCategory> m_enabledLogCategories;
    QString m_qbittorrentHost;
    int m_qbittorrentPort;
    QString m_qbittorrentUser;
    QString m_qbittorrentPassword;
    QString m_mkvmergePath;
    QString m_mkvextractPath;
    QString m_ffmpegPath;
    QString m_qbittorrentPath;
    QString m_nugenAmbPath;
    bool m_deleteTempFiles;
    UserFileAction m_userFileAction;
    QList<TbStyleInfo> m_tbStyles;
    QList<RenderPreset> m_renderPresets;
};

#endif // APPSETTINGS_H
