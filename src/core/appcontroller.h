#pragma once

#include "missingfilescontroller.h"
#include "releasetemplate.h"
#include "workflowmanager.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

// Forward declaration
class WorkflowManager;

class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_DISABLE_COPY(AppController)

    Q_PROPERTY(MissingFilesController* missingFiles READ missingFiles CONSTANT)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)
    Q_PROPERTY(int currentProgress READ currentProgress NOTIFY progressChanged)
    Q_PROPERTY(QString currentStage READ currentStage NOTIFY stageChanged)
    Q_PROPERTY(QStringList templateList READ templateList NOTIFY templateListChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] int currentProgress() const;
    [[nodiscard]] QString currentStage() const;
    [[nodiscard]] QStringList templateList() const;
    [[nodiscard]] MissingFilesController* missingFiles() const;

    Q_INVOKABLE QStringList getRenderPresets();
    Q_INVOKABLE QStringList getTbStyles();

    Q_INVOKABLE void loadTemplates();
    Q_INVOKABLE QJsonObject getTemplateJson(const QString& name);
    Q_INVOKABLE QJsonObject getDefaultTemplateJson();
    Q_INVOKABLE void saveTemplateJson(const QJsonObject& json);
    Q_INVOKABLE void deleteTemplate(const QString& templateName);
    Q_INVOKABLE void startAutoWorkflow(const QString& templateName, const QString& episodeNum, const QString& mkvPath,
                                       const QString& audioPath, const QString& subsPath, const QString& signsPath,
                                       bool normalizeAudio, bool decoupleSrt);
    Q_INVOKABLE void cancelWorkflow();
    Q_INVOKABLE void resumeAfterSubEdit();
    Q_INVOKABLE void submitSignStyles(const QStringList& selectedStyles);
    Q_INVOKABLE QString extractEpisodeNumber(const QString& filePath);
    Q_INVOKABLE bool canDecoupleSubs(const QString& templateName, const QString& subsPath);
    /** Returns true if NUGEN Audio AMB path is set and AMBCmd.exe exists (so normalization can be used). */
    Q_INVOKABLE bool isNugenAmbAvailable() const;
    /** High-level stage names for the auto workflow progress strip (e.g. Скачивание, Извлечение, ...). */
    Q_INVOKABLE QStringList autoWorkflowStageNames() const;
    /** 0-based index of current stage in autoWorkflowStageNames(), or -1 if unknown/idle. */
    Q_INVOKABLE int currentPipelineStageIndex() const;
    Q_INVOKABLE void submitMissingFilesResponse(bool accepted, const QString& audioPath,
                                                const QMap<QString, QString>& fonts, const QString& time);

signals:
    void isBusyChanged();
    void progressChanged();
    void stageChanged();
    void templateListChanged();

    void pauseForSubEditRequested(const QString& subFilePath);
    void signStylesRequested(const QStringList& styles, const QStringList& actors);
    void userInputProvided(const UserInputResponse& response);

    void logMessage(const QString& msg);
    void showNotification(const QString& title, const QString& message, const QString& type);

private:
    bool m_isBusy = false;
    int m_currentProgress = 0;
    QString m_currentStage = "Ожидание";
    QStringList m_templateList;

    void setIsBusy(bool busy);
    void setProgress(int progress, const QString& stage);

    QMap<QString, ReleaseTemplate> m_templates;
    WorkflowManager* m_currentWorker = nullptr;
    MissingFilesController* m_missingFilesController = nullptr;
};