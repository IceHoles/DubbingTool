#ifndef RENDERHELPER_H
#define RENDERHELPER_H

#include "appsettings.h"
#include <QObject>


class ProcessManager;

enum class RerenderDecision {
    Accept,
    Rerender
};

class RenderHelper : public QObject
{
    Q_OBJECT
public:
    explicit RenderHelper(RenderPreset preset,
                          const QString &outputMp4Path,
                          ProcessManager* procManager,
                          QObject *parent = nullptr);

    void startCheck();

signals:
    void finished(RerenderDecision decision, const RenderPreset &newPreset);
    void logMessage(const QString& message, LogCategory category = LogCategory::APP);
    void showDialogRequest(const RenderPreset &preset, double actualBitrate);

public slots:
    void onDialogFinished(bool accepted, const QString& pass1, const QString& pass2);

private:
    RenderPreset m_preset;
    QString m_outputMp4Path;
    ProcessManager* m_procManager;
};

#endif // RENDERHELPER_H
