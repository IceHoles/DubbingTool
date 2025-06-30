#ifndef ASSPROCESSOR_H
#define ASSPROCESSOR_H

#include <QObject>
#include <QStringList>
#include "releasetemplate.h"

class AssProcessor : public QObject
{
    Q_OBJECT
public:
    explicit AssProcessor(QObject *parent = nullptr);

    bool processExistingFile(const QString &inputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime);
    bool generateTbOnlyFile(const QString &outputPath, const ReleaseTemplate &t, const QString& startTime, int resolutionX = 1920);
    bool processFromTwoSources(const QString &dialoguesInputPath, const QString &signsInputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime);
    bool convertToSrt(const QString &inputAssPath, const QString &outputSrtPath, const QStringList &signStyles);
signals:
    void logMessage(const QString &message);

private:
    QStringList generateTb(const ReleaseTemplate &t, const QString &startTime, int detectedResX);
    // Приватный метод для умной балансировки строк
    QString balanceCastLine(const QStringList& actors, bool shouldSort);
};

#endif // ASSPROCESSOR_H
