#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVariantMap>
#include <QPointer>
#include <QList>
#include "releasetemplate.h"
#include "postgenerator.h"
#include "workflowmanager.h"
#include "manualassemblywidget.h"
#include "manualrenderwidget.h"
#include "publicationwidget.h"
#include "missingfilesdialog.h"
#include "styleselectordialog.h"
#include "torrentselectordialog.h"
#include "trackselectordialog.h"
#include "rerenderdialog.h"
#include "appsettings.h"


class ProcessManager;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QString getAudioPath() const;
    QString getManualMkvPath() const;
    QString getOverrideSubsPath() const;
    QString getOverrideSignsPath() const;
    bool isNormalizationEnabled() const;

public slots:
    void logMessage(const QString &message, LogCategory category = LogCategory::APP);
    void onMultipleTorrentsFound(const QList<TorrentInfo> &candidates);
    void onRequestTemplateData(const QString& templateName);
    void onWorkflowAborted();
    void onUserInputRequired(const UserInputRequest &request);
    void onPostsReady(const ReleaseTemplate &t, const EpisodeData &data);
    void onMkvFileReady(const QString &mkvPath);
    void onFilesReady(const QString &mkvPath, const QString &mp4Path);
    void onPostsUpdateRequest(const QMap<QString, QString>& viewLinks);
    void onSignStylesRequest(const QString &subFilePath);
    void onMultipleAudioTracksFound(const QList<AudioTrackInfo> &candidates);
    void onBitrateCheckRequest(const RenderPreset &preset, double actualBitrate);
    void onPauseForSubEditRequest(const QString &subFilePath);

signals:
    void userInputProvided(const UserInputResponse &response);
    void signStylesProvided(const QStringList &styles);
    void torrentSelected(const TorrentInfo &selected);
    void audioTrackSelected(int trackId);
    void subEditFinished();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_createTemplateButton_clicked();
    void on_editTemplateButton_clicked();
    void on_deleteTemplateButton_clicked();
    void on_startButton_clicked();
    void on_cancelButton_clicked();
    void on_selectMkvButton_clicked();
    void on_selectAudioButton_clicked();
    void on_actionSettings_triggered();
    void updateProgress(int percentage, const QString& stageName = "");
    void on_browseOverrideSubsButton_clicked();
    void on_browseOverrideSignsButton_clicked();

    void startManualAssembly();
    void startManualRender();
    void finishWorkerProcess();

private:
    Ui::MainWindow *ui;
    QMap<QString, ReleaseTemplate> m_templates;
    QString m_editingTemplateFileName;

    ManualAssemblyWidget *m_manualAssemblyWidget;
    ManualRenderWidget *m_manualRenderWidget;
    PublicationWidget* m_publicationWidget;

    ReleaseTemplate m_lastTemplate;
    EpisodeData m_lastEpisodeData;
    QString m_lastMkvPath;
    QString m_lastMp4Path;

    QList<ProcessManager*> m_activeProcessManagers;
    QPointer<QObject> m_currentWorker;

    void setUiEnabled(bool enabled);
    void switchToCancelMode();
    void restoreUiAfterFinish();

    void loadTemplates();
    void saveTemplate(const ReleaseTemplate &t);
};
#endif // MAINWINDOW_H
