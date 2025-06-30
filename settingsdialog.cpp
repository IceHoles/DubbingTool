#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QSettings>
#include <QFileDialog>
#include <QStandardPaths>


static QString findExecutable(const QString &exeName)
{
    QString path = QStandardPaths::findExecutable(exeName);
    if (!path.isEmpty()) {
        return QDir::toNativeSeparators(path);
    }
    return exeName;
}

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    loadSettings();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::loadSettings()
{
    QSettings settings("MyCompany", "DubbingTool");
    ui->hostEdit->setText(settings.value("webUi/host", "http://127.0.0.1").toString());
    ui->portSpinBox->setValue(settings.value("webUi/port", 8080).toInt());
    ui->userEdit->setText(settings.value("webUi/user", "admin").toString());
    ui->passwordEdit->setText(settings.value("webUi/password", "admin123").toString());

    ui->mkvToolNixPathEdit->setText(settings.value("paths/mkvmerge", findExecutable("mkvmerge.exe")).toString());
    ui->mkvExtractPathEdit->setText(settings.value("paths/mkvextract", findExecutable("mkvextract.exe")).toString());
    ui->ffmpegPathEdit->setText(settings.value("paths/ffmpeg", findExecutable("ffmpeg.exe")).toString());
    ui->qbittorrentPathEdit->setText(settings.value("paths/qbittorrent", findExecutable("qbittorrent.exe")).toString());
    ui->renderPresetComboBox->setCurrentText(settings.value("render/preset", "NVIDIA (hevc_nvenc)").toString());
    ui->customRenderArgsEdit->setText(settings.value("render/custom_args", "").toString());
}

void SettingsDialog::accept()
{
    QSettings settings("MyCompany", "DubbingTool");
    settings.setValue("webUi/host", ui->hostEdit->text());
    settings.setValue("webUi/port", ui->portSpinBox->value());
    settings.setValue("webUi/user", ui->userEdit->text());
    settings.setValue("webUi/password", ui->passwordEdit->text());

    // Сохраняем путь к mkvmerge и ffmpeg
    settings.setValue("paths/mkvmerge", ui->mkvToolNixPathEdit->text());
    settings.setValue("paths/mkvextract", ui->mkvExtractPathEdit->text());
    settings.setValue("paths/ffmpeg", ui->ffmpegPathEdit->text());
    settings.setValue("render/preset", ui->renderPresetComboBox->currentText());
    settings.setValue("render/custom_args", ui->customRenderArgsEdit->text());

    QDialog::accept();
}

void SettingsDialog::on_browseMkvToolNixButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvmerge.exe", "", "Исполняемые файлы (mkvmerge.exe)");
    if (!filePath.isEmpty()) {
        ui->mkvToolNixPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseMkvExtractButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvextract.exe", "", "Исполняемые файлы (mkvextract.exe)");
    if (!filePath.isEmpty()) {
        ui->mkvExtractPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseFfmpegButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ffmpeg.exe", "", "Исполняемые файлы (ffmpeg.exe)");
    if (!filePath.isEmpty()) {
        ui->ffmpegPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseQbittorrentButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите qbittorrent.exe", "", "Исполняемые файлы (qbittorrent.exe)");
    if (!filePath.isEmpty()) {
        ui->qbittorrentPathEdit->setText(filePath);
    }
}

