#ifndef ASSPROCESSOR_H
#define ASSPROCESSOR_H

#include <QObject>
#include <QStringList>
#include "releasetemplate.h"
#include "appsettings.h"


class AssProcessor : public QObject
{
    Q_OBJECT
public:
    explicit AssProcessor(QObject *parent = nullptr);

    bool processExistingFile(const QString &inputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime);
    bool generateTbOnlyFile(const QString &outputPath, const ReleaseTemplate &t, const QString& startTime, int resolutionX = 1920);
    bool processFromTwoSources(const QString &dialoguesInputPath, const QString &signsInputPath, const QString &outputPathBase, const ReleaseTemplate &t, const QString& startTime);
    bool convertToSrt(const QString &inputAssPath, const QString &outputSrtPath, const QStringList &signStyles);
    bool applySubstitutions(const QString &filePath, const QMap<QString, QString> &substitutions);
    static int calculateTbLineCount(const ReleaseTemplate &t);

signals:
    void logMessage(const QString&, LogCategory);

private:
    QStringList generateTb(const ReleaseTemplate &t, const QString &startTime, int detectedResX);
    QString balanceCastLine(const QStringList& actors, bool shouldSort);
    QString convertAssTagsToSrt(const QString &assText);
};

#endif // ASSPROCESSOR_H
