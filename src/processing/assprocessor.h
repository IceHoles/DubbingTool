#ifndef ASSPROCESSOR_H
#define ASSPROCESSOR_H

#include "appsettings.h"
#include "releasetemplate.h"

#include <QObject>
#include <QStringList>

class AssProcessor : public QObject
{
    Q_OBJECT

public:
    explicit AssProcessor(QObject* parent = nullptr);

    bool processExistingFile(const QString& inputPath, const QString& outputPathBase, const ReleaseTemplate& t);
    bool copySignsFile(const QString& inputPath, const QString& outputPath);
    bool processFromTwoSources(const QString& dialoguesInputPath, const QString& signsInputPath,
                               const QString& outputPathBase, const ReleaseTemplate& t);
    bool convertToSrt(const QString& inputAssPath, const QString& outputSrtPath, const QStringList& signStyles);
    bool applySubstitutions(const QString& filePath, const QMap<QString, QString>& substitutions);

signals:
    void logMessage(const QString&, LogCategory, LogLevel = LogLevel::Info);

private:
    QString convertAssTagsToSrt(const QString& assText);
};

#endif // ASSPROCESSOR_H
