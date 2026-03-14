#pragma once

#include "processmanager.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

class ManualExtractionBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ManualExtractionBridge(QObject* parent = nullptr);

    Q_INVOKABLE QString identifyFile(const QString& path);
    Q_INVOKABLE void setDurationSeconds(double seconds);
    Q_INVOKABLE void startExtraction(const QString& filePath, const QVariantList& selectedItems);

signals:
    void logMessage(const QString& message);
    void progressUpdated(int value, const QString& status);
    void extractionFinished(int exitCode);

private slots:
    void onProcessFinished(int exitCode);
    void onProcessStdOut(const QString& output);
    void onProcessStdErr(const QString& output);

private:
    ProcessManager m_processManager;
    QString m_currentFile;
    double m_durationSec = 0.0;

    void buildMkvExtractArgs(const QString& filePath, const QVariantList& selectedItems, QStringList& trackArgs,
                             QStringList& attachmentArgs, bool& hasWork);
};

