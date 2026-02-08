#ifndef MANUALASSEMBLYWIDGET_H
#define MANUALASSEMBLYWIDGET_H

#include "fontfinder.h"
#include "releasetemplate.h"

#include <QLineEdit>
#include <QWidget>

namespace Ui
{
class ManualAssemblyWidget;
} // namespace Ui

class ManualAssemblyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualAssemblyWidget(QWidget* parent = nullptr);
    ~ManualAssemblyWidget();

    QVariantMap getParameters() const;

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
    void onModeSwitched(bool isManualMode);

private:
    void browseForFile(QLineEdit* lineEdit, const QString& caption, const QString& filter);
    void updateUiState(bool isManualMode);
    FontFinder* m_fontFinder;
    QIcon m_templateModeIcon;
    QIcon m_manualModeIcon;

    Ui::ManualAssemblyWidget* ui;
signals:
    void templateDataRequested(const QString& templateName);
    void assemblyRequested();

public slots:
    void onTemplateDataReceived(const ReleaseTemplate& t);
    void setAssembling(bool assembling);
};

#endif // MANUALASSEMBLYWIDGET_H
