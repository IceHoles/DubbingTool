#include "manualrenderwidget.h"
#include "ui_manualrenderwidget.h"
#include "manualrenderer.h"
#include <QVariantMap>
#include <QFileDialog>
#include <QMessageBox>

ManualRenderWidget::ManualRenderWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ManualRenderWidget)
{
    ui->setupUi(this);
}

ManualRenderWidget::~ManualRenderWidget()
{
    delete ui;
}

void ManualRenderWidget::on_browseInputButton_clicked()
{
    QString path = QFileDialog::getOpenFileName(this, "Выберите входной MKV", "", "Matroska Video (*.mkv)");
    if (!path.isEmpty()) {
        ui->inputMkvPathEdit->setText(path);
        // Автоматически предлагаем имя выходного файла
        ui->outputMp4PathEdit->setText(path.replace(".mkv", ".mp4"));
    }
}

void ManualRenderWidget::on_browseOutputButton_clicked()
{
    QString path = QFileDialog::getSaveFileName(this, "Выберите, куда сохранить MP4", "", "MPEG-4 Video (*.mp4)");
    if (!path.isEmpty()) {
        ui->outputMp4PathEdit->setText(path);
    }
}

void ManualRenderWidget::on_renderButton_clicked()
{
    QVariantMap params;
    params["inputMkv"] = ui->inputMkvPathEdit->text();
    params["outputMp4"] = ui->outputMp4PathEdit->text();
    params["renderPreset"] = ui->renderPresetComboBox->currentText();
    params["extraArgs"] = ui->extraArgsEdit->text();

    // --- ЗАПУСК РЕНДЕРА ---
    // Нам нужно как-то получить доступ к главному окну, чтобы запустить рендер
    // и заблокировать UI. Проще всего это сделать через сигналы.
    emit renderRequested(params);
}
