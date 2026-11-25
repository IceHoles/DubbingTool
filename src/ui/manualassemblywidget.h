#ifndef MANUALASSEMBLYWIDGET_H
#define MANUALASSEMBLYWIDGET_H

#include "fontfinder.h"

#include <QLineEdit>
#include <QList>
#include <QWidget>

namespace Ui
{
class ManualAssemblyWidget;
} // namespace Ui

struct ContainerAudioTrack
{
    int id = -1;
    QString codec;
    QString language;
    QString trackName;
    int channels = 0;
};

class ManualAssemblyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualAssemblyWidget(QWidget* parent = nullptr);
    ~ManualAssemblyWidget() override;

    QVariantMap getParameters() const;
    QString chapterTimingSourcePath() const;

    /// Clears the font attachment list (e.g. after a successful MKV build for the next episode).
    void clearFontsList();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_browseVideo_clicked();
    void on_browseOriginalAudio_clicked();
    void on_selectAudioFromVideoButton_clicked();
    void on_browseRussianAudio_clicked();
    void on_browseSubtitles_clicked();
    void on_browseSigns_clicked();
    void on_analyzeSubsButton_clicked();
    void on_addFontsButton_clicked();
    void on_clearFontsButton_clicked();
    void on_assembleButton_clicked();
    void on_browseWorkDirButton_clicked();
    void on_browseChaptersXmlManualAssemblyButton_clicked();
    void onFontFinderFinished(const FontFinderResult& result);

private:
    bool browseForFile(QLineEdit* lineEdit, const QString& caption, const QString& filter);
    void updateChaptersUiVisibility();
    void updateAutoOutputName(const QString& videoPath);
    void probeVideoAudioTracks(const QString& videoPath, bool autoPrompt = false);
    bool promptUserForAudioTrack(const QList<ContainerAudioTrack>& tracks);

    FontFinder* m_fontFinder;

    QString m_lastAutoOutputName;
    QString m_lastProbedVideoPath;
    QList<ContainerAudioTrack> m_videoAudioTracks;
    int m_selectedVideoTrackId = -1;
    int m_selectedVideoAudioTrackId = -1;
    bool m_hasAudioInVideo = false;

    Ui::ManualAssemblyWidget* ui;

signals:
    void assemblyRequested();
    void chapterTimingsRequested(const QString& sourcePath);

public slots:
    void setAssembling(bool assembling);
};

#endif // MANUALASSEMBLYWIDGET_H
