#ifndef MANUALEXTRACTIONWIDGET_H
#define MANUALEXTRACTIONWIDGET_H

#include "appsettings.h"
#include "processmanager.h"

#include <QFileInfo>
#include <QTreeWidgetItem>
#include <QWidget>

namespace Ui
{
class ManualExtractionWidget;
} // namespace Ui

class ManualExtractionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualExtractionWidget(QWidget* parent = nullptr);
    ~ManualExtractionWidget();

signals:
    // Используем ваш enum LogCategory, убедитесь что он виден (через include appsettings.h или workflowmanager.h)
    void logMessage(const QString& message, LogCategory category);
    void progressUpdated(int value, const QString& status);

protected:
    // Драг-н-дроп
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onProcessStdOut(const QString& output);
    void onProcessStdErr(const QString& output);
    void onBrowseClicked();
    void onExtractClicked();
    void onProcessFinished(int exitCode);

private:
    Ui::ManualExtractionWidget* ui;
    ProcessManager* m_processManager;
    QString m_currentFile;
    double m_durationSec;

    void scanFile(const QString& path);
    void parseMkvJson(const QByteArray& data);

    // Адаптер кодеков MKV -> Расширение файла
    QString getExtensionForMkvCodec(const QString& codecId, const QString& trackType);
};

#endif // MANUALEXTRACTIONWIDGET_H