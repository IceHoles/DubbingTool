#ifndef MANUALASSEMBLYWIDGET_H
#define MANUALASSEMBLYWIDGET_H

#include <QWidget>
#include "releasetemplate.h"
#include "fontfinder.h"
#include <QLineEdit>


// Прямое объявление, чтобы избежать включения заголовка Qt Designer
namespace Ui {
class ManualAssemblyWidget;
}

class ManualAssemblyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualAssemblyWidget(QWidget *parent = nullptr);
    ~ManualAssemblyWidget();

    void updateTemplateList(const QStringList& templateNames);

private slots:
    void on_browseVideo_clicked();
    void on_browseOriginalAudio_clicked();
    void on_browseRussianAudio_clicked();
    void on_browseSubtitles_clicked();
    void on_browseSigns_clicked();
    void on_analyzeSubsButton_clicked();
    void on_addFontsButton_clicked();
    void on_assembleButton_clicked();
    void on_templateComboBox_currentIndexChanged(int index);
    void on_browseWorkDirButton_clicked();
    void onFontFinderFinished(const FontFinderResult& result);

private:
    void browseForFile(QLineEdit *lineEdit, const QString &caption, const QString &filter);
    FontFinder* m_fontFinder;

    Ui::ManualAssemblyWidget *ui;
signals:
    void assemblyRequested(const QVariantMap& parameters);
    void templateDataRequested(const QString& templateName);

public slots:
    void onTemplateDataReceived(const ReleaseTemplate& t);
};

#endif // MANUALASSEMBLYWIDGET_H
