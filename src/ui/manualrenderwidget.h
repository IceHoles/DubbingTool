#ifndef MANUALRENDERWIDGET_H
#define MANUALRENDERWIDGET_H

#include <QWidget>

namespace Ui
{
class ManualRenderWidget;
} // namespace Ui

class ManualRenderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualRenderWidget(QWidget* parent = nullptr);
    ~ManualRenderWidget();

    QVariantMap getParameters() const;
    QString getCurrentPresetName() const;
    QString chapterTimingSourcePath() const;

public slots:
    void updateRenderPresets();
    void setRendering(bool rendering);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_browseInputButton_clicked();
    void on_browseOutputButton_clicked();
    void on_renderButton_clicked();
    void updateHardsubOptions();
    void analyzeMkvForSubtitles(const QString& path);

signals:
    void renderRequested();
    void chapterTimingsRequested(const QString& sourcePath);

private:
    void setInputVideoPath(const QString& path);

    Ui::ManualRenderWidget* ui;
};

#endif // MANUALRENDERWIDGET_H
