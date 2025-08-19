#ifndef WORKFLOWMANAGER_H
#define WORKFLOWMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QXmlStreamReader>
#include <QSettings>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include "releasetemplate.h"
#include "processmanager.h"
#include "rerenderdialog.h"
#include "renderhelper.h"
#include "assprocessor.h"
#include "postgenerator.h"
#include "fontfinder.h"
#include "appsettings.h"
#include "trackselectordialog.h"
#include "torrentselectordialog.h"


class AssProcessor;
class MainWindow;
class ProcessManager;

struct PathManager {
    QString basePath;
    QString sourcesPath;
    QString subsPath;
    QString ruAudioPath;
    QString resultPath;

    // Конструктор, который создает все папки
    PathManager(const QString& baseSavePath) {
        basePath = baseSavePath;
        sourcesPath = QDir(basePath).filePath("Sources");
        subsPath = QDir(basePath).filePath("Processed Subs");
        ruAudioPath = QDir(basePath).filePath("RU Audio");
        resultPath = QDir(basePath).filePath("Result");

        QDir(sourcesPath).mkpath(".");
        QDir(subsPath).mkpath(".");
        QDir(ruAudioPath).mkpath(".");
        QDir(resultPath).mkpath(".");
    }

    // Методы для получения путей к конкретным файлам
    QString sourceMkv(const QString& originalName) const { return QDir(sourcesPath).filePath(originalName); }
    QString extractedVideo(const QString& extension) const { return QDir(sourcesPath).filePath("video." + extension); }
    QString extractedAudio(const QString& extension) const { return QDir(sourcesPath).filePath("audio_original." + extension); }
    QString extractedSubs(const QString& extension) const { return QDir(sourcesPath).filePath("subtitles_original." + extension); }
    QString attachedFontsDir() const { return QDir(sourcesPath).filePath("attached_fonts"); }

    QString processedFullSubs() const { return QDir(subsPath).filePath("subtitles_processed_full.ass"); }
    QString processedSignsSubs() const { return QDir(subsPath).filePath("subtitles_processed_signs.ass"); }
    QString masterSrt() const { return QDir(subsPath).filePath("master_subtitles.srt"); }

    QString convertedRuAudio(const QString& extension) const { return QDir(ruAudioPath).filePath("russian_audio." + extension); }

    QString finalMkv(const QString& fileName) const { return QDir(resultPath).filePath(fileName); }
    QString finalMp4(const QString& fileName) const { return QDir(resultPath).filePath(fileName); }
    QString masterMkv(const QString& fileName) const { return QDir(resultPath).filePath(fileName); }
};

class WorkflowManager : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowManager(ReleaseTemplate t, const QString &episodeNumberForPost, const QString &episodeNumberForSearch, const QSettings &settings, MainWindow *mainWindow);
    ~WorkflowManager();
    void start();
    void startWithManualFile(const QString &filePath);
    void killChildProcesses();
    ProcessManager* getProcessManager() const;

public slots:
    void cancelOperation();
    void resumeWithMissingFiles(const QString &audioPath, const QMap<QString, QString> &resolvedFonts, const QString &time);
    void resumeWithSelectedTorrent(const TorrentInfo &selected);
    void resumeWithSignStyles(const QStringList &styles);
    void resumeWithSelectedAudioTrack(int trackId);
    void resumeAfterSubEdit();

signals:
    void logMessage(const QString &message, LogCategory category = LogCategory::APP);
    void postsReady(const ReleaseTemplate &t, const EpisodeData &data);
    void filesReady(const QString &mkvPath, const QString &mp4Path);
    void finished(const ReleaseTemplate &t, const EpisodeData &data, const QString &mkvPath, const QString &mp4Path);
    void workflowAborted();
    void missingFilesRequest(const QStringList &missingFonts, bool requireWav = false, bool requireTime = false);
    void progressUpdated(int percentage, const QString& stageName = "");
    void signStylesRequest(const QString &subFilePath);
    void multipleTorrentsFound(const QList<TorrentInfo> &candidates);
    void multipleAudioTracksFound(const QList<AudioTrackInfo> &candidates);
    void bitrateCheckRequest(const RenderPreset &preset, double actualBitrate);
    void pauseForSubEditRequest(const QString &subFilePath);

private slots:
    void onRssDownloaded(QNetworkReply *reply);
    void onLoginFinished(QNetworkReply *reply);
    void onTorrentAdded(QNetworkReply *reply);
    void onTorrentDeleted(QNetworkReply *reply);
    void onTorrentListReceived(QNetworkReply *reply);
    void onTorrentListForCheckReceived(QNetworkReply *reply);
    void onPollingTimerTimeout();
    void onTorrentProgressReceived(QNetworkReply *reply);
    void onTorrentFilesReceived(QNetworkReply *reply);
    void onFontFinderFinished(const FontFinderResult& result);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessStdOut(const QString &output);
    void onProcessStdErr(const QString &output);
    void onAudioConversionProgress();
    void onHashFindAttempt();
    void onBitrateCheckFinished(RerenderDecision decision, const RenderPreset &newPreset);

private:
    enum class Step { Idle,
                      AddingTorrent,
                      Polling,
                      GettingMkvInfo,
                      ExtractingAttachments,
                      ExtractingTracks,
                      ProcessingSubs,
                      FindingFonts,
                      ConvertingToSrt,
                      AssemblingSrtMaster,
                      ConvertingAudio,
                      AssemblingMkv,
                      RenderingMp4Pass1,
                      RenderingMp4Pass2
    };

    enum class ProcessingSubStep {
        Idle,
        RequestingStyles,
        RequestingTime,
        RunningAssProcessor,
        RequestingWav,
        Done
    };

    Step m_currentStep = Step::Idle;
    Step m_lastStepBeforeRequest = Step::Idle;

    void parseRssAndDownload(const QByteArray &rssData);
    void startDownload(const QString &magnetLink);
    void login();
    void addTorrent(const QString &magnetLink);
    void findTorrentHashAndStartPolling();
    void startPolling();
    void checkExistingTorrents();
    void downloadRss();
    void deleteTorrentAndRedownload();
    void getTorrentFiles();
    QString findMkvFileInSavePath();
    void getMkvInfo();
    QString parseChaptersWithMkvExtract();
    void extractTracks();
    void extractAttachments(const QJsonArray &attachments);
    void convertAudioIfNeeded();
    void convertToSrtAndAssembleMaster();
    void assembleMkv(const QString &m_finalAudioPath);
    void renderMp4();
    void runRenderPass(Step pass);
    void prepareUserFiles();
    void finishWorkflow();
    void processSubtitles();
    void runAssProcessing();
    void findFontsInProcessedSubs();

    QStringList prepareCommandArguments(const QString &commandTemplate);
    QString getExtensionForCodec(const QString& codecId);
    QString handleUserFile(const QString& sourcePath, const QString& destDir, const QString& newName = "");

    MainWindow *m_mainWindow; // Указатель на главный класс UI
    ReleaseTemplate m_template;
    QString m_episodeNumberForPost;
    QString m_episodeNumberForSearch;
    QString m_magnetLink;
    QString m_savePath;
    QString m_torrentHash;
    QString m_mkvFilePath;
    QString m_mkvmergePath;
    QString m_mkvextractPath;
    QString m_mainRuAudioPath;      // Путь к основному аудио (wav, aac, flac ...), указанному пользователем
    QString m_wavForSrtMasterPath;  // Путь к .wav файлу, запрошенному специально для мастер-копии
    QString m_finalAudioPath;
    QString m_ffmpegPath;
    QString m_finalMkvPath;
    QString m_outputMp4Path;
    QString m_customRenderArgs;
    QString m_parsedEndingTime;
    QString m_overrideSubsPath;
    QString m_overrideSignsPath;

    bool m_wasUserInputRequested = false;
    bool m_wereStylesRequested = false;

    struct TrackInfo {
        int id = -1;
        QString codecId;
        QString extension;
    };

    FontFinder *m_fontFinder;
    FontFinderResult m_fontResult;

    TrackInfo m_videoTrack;
    TrackInfo m_originalAudioTrack;
    TrackInfo m_subtitleTrack;
    QList<AudioTrackInfo> m_foundAudioTracks;

    RenderPreset m_renderPreset;

    QString m_webUiHost;
    int m_webUiPort;
    QString m_webUiUser;
    QString m_webUiPassword;
    bool m_qbtLaunchAttempted = false;
    int m_hashFindAttempts = 0; // Счетчик попыток
    QTimer *m_hashFindTimer;    // Таймер для поиска хеша

    const int MAX_HASH_FIND_ATTEMPTS = 5;   // Попробуем 5 раз
    const int HASH_FIND_INTERVAL_MS = 2000; // с интервалом в 2 секунды

    QNetworkAccessManager *m_netManager;
    QList<QNetworkCookie> m_cookies;
    QTimer *m_progressTimer;
    QString m_ffmpegProgressFile; // Файл для лога прогресса
    qint64 m_sourceDurationS = 0;
    ProcessManager *m_processManager;
    AssProcessor *m_assProcessor;
    QStringList m_tempFontPaths;

    PathManager* m_paths = nullptr;
};

#endif // WORKFLOWMANAGER_H
