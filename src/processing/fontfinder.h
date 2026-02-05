#ifndef FONTFINDER_H
#define FONTFINDER_H

#include <QObject>
#include <QStringList>
#include <QList>
#include <QProcess>
#include "appsettings.h"


struct FoundFontInfo {
    QString path;
    QString familyName;
};

struct FontFinderResult {
    QList<FoundFontInfo> foundFonts;
    QStringList notFoundFontNames;
};

class FontFinder : public QObject {
    Q_OBJECT

public:
    explicit FontFinder(QObject *parent = nullptr);
    ~FontFinder();

    void findFontsInSubs(const QStringList& subFilesToCheck);

signals:
    void logMessage(const QString&, LogCategory);
    void finished(const FontFinderResult& result);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    FontFinderResult parseJsonOutput(const QByteArray& jsonData);
    QString extractWrapper();
    QString m_wrapperPath;
    QProcess* m_process;
};

#endif // FONTFINDER_H
