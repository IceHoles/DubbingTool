#ifndef MANUALRENDERWIDGET_H
#define MANUALRENDERWIDGET_H

#include <QWidget>

namespace Ui {
class ManualRenderWidget;
class ManualRenderer;
}

class ManualRenderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ManualRenderWidget(QWidget *parent = nullptr);
    ~ManualRenderWidget();

private slots:
    void on_browseInputButton_clicked();
    void on_browseOutputButton_clicked();
    void on_renderButton_clicked();

signals:
    void renderRequested(const QVariantMap& parameters);

private:
    Ui::ManualRenderWidget *ui;
};

#endif // MANUALRENDERWIDGET_H
