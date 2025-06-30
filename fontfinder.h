// fontfinder.h
#ifndef FONTFINDER_H
#define FONTFINDER_H

#include <QObject>
#include <QStringList>
#include <QList>
#include <QProcess> // Добавляем QProcess

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

    // Метод теперь не возвращает результат, а просто запускает процесс
    void findFontsInSubs(const QStringList& subFilesToCheck);

signals:
    void logMessage(const QString& message);
    // Сигнал, который будет испущен по завершении поиска
    void finished(const FontFinderResult& result);

private slots:
    // Слоты для обработки сигналов от QProcess
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    FontFinderResult parseJsonOutput(const QByteArray& jsonData);
    QString extractWrapper();
    QString m_wrapperPath;
    QProcess* m_process; // QProcess теперь член класса
};

#endif // FONTFINDER_H
