#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVariantMap>
#include <QList>
#include "releasetemplate.h"
#include "workflowmanager.h"
#include "manualassemblywidget.h"
#include "manualrenderwidget.h"
#include "publicationwidget.h"
#include "missingfilesdialog.h"
#include "styleselectordialog.h"


class ProcessManager; // Прямое объявление

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

public slots:
    void logMessage(const QString &message);
    void onManualAssemblyRequested(const QVariantMap& parameters);
    void onManualRenderRequested(const QVariantMap& parameters);
    void onRequestTemplateData(const QString& templateName);
    void onWorkflowAborted();
    void onMissingFilesRequest(const QStringList &missingFonts);
    void onPostsReady(const ReleaseTemplate &t, const EpisodeData &data);
    void onFilesReady(const QString &mkvPath, const QString &mp4Path);
    void onPostsUpdateRequest(const QMap<QString, QString>& viewLinks);
    void onSignStylesRequest(const QString &subFilePath);
signals:
    void missingFilesProvided(const QString &audioPath, const QMap<QString, QString> &resolvedFonts);
    void signStylesProvided(const QStringList &styles);
protected:
    void closeEvent(QCloseEvent *event) override; // Переопределяем событие закрытия

private slots:
    void on_createTemplateButton_clicked();
    void on_editTemplateButton_clicked();
    void on_deleteTemplateButton_clicked();
    void on_startButton_clicked();
    void on_selectMkvButton_clicked();
    void on_selectAudioButton_clicked();
    void on_actionSettings_triggered();
    void updateProgress(int percentage, const QString& stageName = "");
    void on_browseOverrideSubsButton_clicked();
    void on_browseOverrideSignsButton_clicked();

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

    // Список для отслеживания всех активных менеджеров процессов
    QList<ProcessManager*> m_activeProcessManagers;

    void loadTemplates();
    void saveTemplate(const ReleaseTemplate &t);
};
#endif // MAINWINDOW_H
