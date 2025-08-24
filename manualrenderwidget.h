#ifndef MANUALRENDERWIDGET_H
#define MANUALRENDERWIDGET_H

#include <QWidget>

namespace Ui {
class ManualRenderWidget;
}

class ManualRenderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualRenderWidget(QWidget *parent = nullptr);
    ~ManualRenderWidget();

    QVariantMap getParameters() const;
    QString getCurrentPresetName() const;

public slots:
    void updateRenderPresets();
    void setRendering(bool rendering);

private slots:
    void on_browseInputButton_clicked();
    void on_browseOutputButton_clicked();
    void on_renderButton_clicked();
    void updateHardsubOptions();
    void analyzeMkvForSubtitles(const QString& path);

signals:
    void renderRequested();

private:
    Ui::ManualRenderWidget *ui;
};

#endif // MANUALRENDERWIDGET_H
