#ifndef WORKFLOWMANAGER_H
#define WORKFLOWMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QXmlStreamReader>
#include <QSettings>
#include <QTimer>
#include "releasetemplate.h"
#include "processmanager.h"
#include "assprocessor.h"
#include "postgenerator.h"
#include "fontfinder.h"

class AssProcessor;
class MainWindow;
class ProcessManager;

class WorkflowManager : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowManager(ReleaseTemplate t, const QString &episode, const QSettings &settings, MainWindow *mainWindow);
    ~WorkflowManager();
    void start();
    void startWithManualFile(const QString &filePath);
    void killChildProcesses();
    ProcessManager* getProcessManager() const;
public slots:
    void resumeWithMissingFiles(const QString &audioPath, const QMap<QString, QString> &resolvedFonts);
    void resumeWithSignStyles(const QStringList &styles);
signals:
    void logMessage(const QString &message);
    void postsReady(const ReleaseTemplate &t, const EpisodeData &data);
    void filesReady(const QString &mkvPath, const QString &mp4Path);
    void finished(const ReleaseTemplate &t, const EpisodeData &data, const QString &mkvPath, const QString &mp4Path);
    void workflowAborted();
    void missingFilesRequest(const QStringList &missingFonts);
    void progressUpdated(int percentage, const QString& stageName = "");
    void signStylesRequest(const QString &subFilePath);
private slots:
    void onRssDownloaded(QNetworkReply *reply);
    void onLoginFinished(QNetworkReply *reply);
    void onTorrentAdded(QNetworkReply *reply);
    void onTorrentListReceived(QNetworkReply *reply);
    void onPollingTimerTimeout();
    void onTorrentProgressReceived(QNetworkReply *reply);
    void onTorrentFilesReceived(QNetworkReply *reply);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessStdOut(const QString &output);
    void onProcessStdErr(const QString &output);
    void onAudioConversionProgress();
    void onHashFindAttempt();

private:
    enum class Step { Idle,
                      AddingTorrent,
                      Polling,
                      GettingMkvInfo,
                      ExtractingTracks,
                      ExtractingAttachments,
                      ProcessingSubs,
                      ConvertingToSrt,
                      AssemblingSrtMaster,
                      ConvertingAudio,
                      AssemblingMkv,
                      RenderingMp4
    };
    Step m_currentStep = Step::Idle;
    Step m_lastStepBeforeRequest = Step::Idle;

    bool checkForExistingFile();
    void parseRssAndDownload(const QByteArray &rssData);
    void login();
    void addTorrent(const QString &magnetLink);
    void findTorrentHashAndStartPolling();
    void startPolling();
    void getTorrentFiles();
    void getMkvInfo();
    void extractTracks();
    void extractAttachments(const QJsonArray &attachments);
    void convertAudioIfNeeded();
    void convertToSrtAndAssembleMaster();
    void assembleMkv(const QString &m_finalAudioPath);
    void renderMp4();
    void finishWorkflow();
    void processSubtitles();

    QString getExtensionForCodec(const QString& codecId);

    MainWindow *m_mainWindow; // Указатель на главный класс UI
    ReleaseTemplate m_template;
    QString m_episodeNumber;
    QString m_magnetLink;
    QString m_savePath;
    QString m_torrentHash;
    QString m_mkvFilePath;
    QString m_mkvmergePath;
    QString m_mkvextractPath;
    QString m_userAudioPath; // Сохраняем путь, указанный пользователем
    QString m_finalAudioPath; // Путь к аудио для сборки (может быть исходным или конвертированным)
    QString m_ffmpegPath; // Путь к ffmpeg
    QString m_finalMkvPath;
    QString m_outputMp4Path;
    QString m_renderPreset;
    QString m_customRenderArgs;
    QString m_parsedEndingTime;
    QString m_overrideSubsPath;
    QString m_overrideSignsPath;

    bool m_wasUserInputRequested = false;

    struct TrackInfo {
        int id = -1;
        QString codecId;
        QString extension;
    };

    FontFinderResult m_fontResult;

    TrackInfo m_videoTrack;
    TrackInfo m_originalAudioTrack;
    TrackInfo m_subtitleTrack;

    QString m_webUiHost;
    int m_webUiPort;
    QString m_webUiUser;
    QString m_webUiPassword;

    int m_hashFindAttempts = 0; // Счетчик попыток
    QTimer *m_hashFindTimer;   // Таймер для поиска хеша

    const int MAX_HASH_FIND_ATTEMPTS = 5; // Попробуем 5 раз
    const int HASH_FIND_INTERVAL_MS = 2000; // с интервалом в 2 секунды

    QNetworkAccessManager *m_netManager;
    QList<QNetworkCookie> m_cookies;
    //QTimer *m_pollingTimer;
    QTimer *m_progressTimer;
    QString m_ffmpegProgressFile; // Файл для лога прогресса
    qint64 m_sourceDurationS = 0;
    ProcessManager *m_processManager;
    AssProcessor *m_assProcessor;
    QStringList m_tempFontPaths;
};

#endif // WORKFLOWMANAGER_H
