#include "settingsdialog.h"

#include "appsettings.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QTableWidgetItem>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::SettingsDialog)
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
    AppSettings& settings = AppSettings::instance();

    ui->hostEdit->setText(settings.qbittorrentHost());
    ui->portSpinBox->setValue(settings.qbittorrentPort());
    ui->userEdit->setText(settings.qbittorrentUser());
    ui->passwordEdit->setText(settings.qbittorrentPassword());

    ui->mkvToolNixPathEdit->setText(settings.mkvmergePath());
    ui->mkvExtractPathEdit->setText(settings.mkvextractPath());
    ui->ffmpegPathEdit->setText(settings.ffmpegPath());
    ui->mp4boxPathEdit->setText(settings.mp4boxPath());
    ui->qbittorrentPathEdit->setText(settings.qbittorrentPath());
    ui->nugenAmbPathEdit->setText(settings.nugenAmbPath());
    ui->deleteTempFilesCheckBox->setChecked(settings.deleteTempFiles());
    ui->userFileActionComboBox->setCurrentIndex(static_cast<int>(settings.userFileAction()));
    ui->projectDirectoryEdit->setText(settings.projectDirectory());

    m_renderPresets = settings.renderPresets();
    ui->renderPresetsList->clear();
    for (const auto& preset : m_renderPresets)
    {
        ui->renderPresetsList->addItem(preset.name);
    }

    if (ui->renderPresetsList->count() > 0)
    {
        ui->renderPresetsList->setCurrentRow(0);
    }
    else
    {
        on_renderPresetsList_currentRowChanged(-1);
    }

    QSet<LogCategory> enabled = settings.enabledLogCategories();
    ui->logAppCheckBox->setChecked(enabled.contains(LogCategory::APP));
    ui->logMkvToolNixCheckBox->setChecked(enabled.contains(LogCategory::MKVTOOLNIX));
    ui->logFfmpegCheckBox->setChecked(enabled.contains(LogCategory::FFMPEG));
    ui->logQbittorrentCheckBox->setChecked(enabled.contains(LogCategory::QBITTORRENT));
    ui->logDebugCheckBox->setChecked(enabled.contains(LogCategory::DEBUG));
}

void SettingsDialog::accept()
{
    AppSettings& settings = AppSettings::instance();

    settings.setQbittorrentHost(ui->hostEdit->text());
    settings.setQbittorrentPort(ui->portSpinBox->value());
    settings.setQbittorrentUser(ui->userEdit->text());
    settings.setQbittorrentPassword(ui->passwordEdit->text());

    settings.setMkvmergePath(ui->mkvToolNixPathEdit->text());
    settings.setMkvextractPath(ui->mkvExtractPathEdit->text());
    settings.setFfmpegPath(ui->ffmpegPathEdit->text());
    settings.setMp4boxPath(ui->mp4boxPathEdit->text());
    settings.setQbittorrentPath(ui->qbittorrentPathEdit->text());
    settings.setNugenAmbPath(ui->nugenAmbPathEdit->text());
    settings.setDeleteTempFiles(ui->deleteTempFilesCheckBox->isChecked());
    settings.setUserFileAction(static_cast<UserFileAction>(ui->userFileActionComboBox->currentIndex()));
    settings.setProjectDirectory(ui->projectDirectoryEdit->text().trimmed());

    settings.setRenderPresets(m_renderPresets);

    QSet<LogCategory> enabled;
    if (ui->logAppCheckBox->isChecked())
    {
        enabled.insert(LogCategory::APP);
    }
    if (ui->logMkvToolNixCheckBox->isChecked())
    {
        enabled.insert(LogCategory::MKVTOOLNIX);
    }
    if (ui->logFfmpegCheckBox->isChecked())
    {
        enabled.insert(LogCategory::FFMPEG);
    }
    if (ui->logQbittorrentCheckBox->isChecked())
    {
        enabled.insert(LogCategory::QBITTORRENT);
    }
    if (ui->logDebugCheckBox->isChecked())
    {
        enabled.insert(LogCategory::DEBUG);
    }
    settings.setEnabledLogCategories(enabled);

    settings.save();
    QDialog::accept();
}

void SettingsDialog::on_browseMkvToolNixButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvmerge.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->mkvToolNixPathEdit->setText(filePath);
    }
}
void SettingsDialog::on_browseMkvExtractButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvextract.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->mkvExtractPathEdit->setText(filePath);
    }
}
void SettingsDialog::on_browseFfmpegButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ffmpeg.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->ffmpegPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseMp4boxButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mp4box.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->mp4boxPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseQbittorrentButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите qbittorrent.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->qbittorrentPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseNugenAmbButton_clicked()
{
    QString filePath =
        QFileDialog::getOpenFileName(this, "Выберите NUGEN Audio AMB.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty())
    {
        ui->nugenAmbPathEdit->setText(filePath);
    }
}

void SettingsDialog::on_browseProjectDirectoryButton_clicked()
{
    QString dir =
        QFileDialog::getExistingDirectory(this, "Выберите директорию проектов", ui->projectDirectoryEdit->text());
    if (!dir.isEmpty())
    {
        ui->projectDirectoryEdit->setText(dir);
    }
}

void SettingsDialog::on_renderPresetsList_currentRowChanged(int currentRow)
{
    if (currentRow < 0 || currentRow >= m_renderPresets.size())
    {
        ui->renderPresetNameEdit->clear();
        ui->renderPresetCommandEdit_Pass1->clear();
        ui->renderPresetCommandEdit_Pass2->clear();
        ui->renderPresetEditorGroup->setEnabled(false);
        return;
    }

    ui->renderPresetEditorGroup->setEnabled(true);
    const auto& preset = m_renderPresets[currentRow];
    ui->renderPresetNameEdit->setText(preset.name);
    ui->renderPresetCommandEdit_Pass1->setPlainText(preset.commandPass1);
    ui->renderPresetCommandEdit_Pass2->setPlainText(preset.commandPass2);
    ui->targetBitrateSpinBox->setValue(preset.targetBitrateKbps);
}

void SettingsDialog::on_saveRenderPresetButton_clicked()
{
    int currentRow = ui->renderPresetsList->currentRow();
    if (currentRow < 0)
    {
        return;
    }

    QString name = ui->renderPresetNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        QMessageBox::warning(this, "Ошибка", "Имя пресета не может быть пустым.");
        return;
    }

    m_renderPresets[currentRow].name = name;
    m_renderPresets[currentRow].commandPass1 = ui->renderPresetCommandEdit_Pass1->toPlainText();
    m_renderPresets[currentRow].commandPass2 = ui->renderPresetCommandEdit_Pass2->toPlainText();
    m_renderPresets[currentRow].targetBitrateKbps = ui->targetBitrateSpinBox->value();
    ui->renderPresetsList->item(currentRow)->setText(name);

    AppSettings& settings = AppSettings::instance();
    settings.setRenderPresets(m_renderPresets);
    settings.save();
}

void SettingsDialog::on_newRenderPresetButton_clicked()
{
    RenderPreset newPreset;
    newPreset.name = "Новый пресет";
    newPreset.commandPass1 =
        "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v hevc_qsv ...  \"%OUTPUT%\"";
    newPreset.targetBitrateKbps = 4000;
    m_renderPresets.append(newPreset);

    ui->renderPresetsList->addItem(newPreset.name);
    ui->renderPresetsList->setCurrentRow(ui->renderPresetsList->count() - 1);

    AppSettings::instance().setRenderPresets(m_renderPresets);
    AppSettings::instance().save();

    ui->renderPresetNameEdit->selectAll();
    ui->renderPresetNameEdit->setFocus();
}

void SettingsDialog::on_deleteRenderPresetButton_clicked()
{
    int currentRow = ui->renderPresetsList->currentRow();
    if (currentRow < 0)
    {
        return;
    }

    auto reply =
        QMessageBox::question(this, "Подтверждение", "Удалить пресет '" + m_renderPresets[currentRow].name + "'?",
                              QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
    {
        m_renderPresets.removeAt(currentRow);
        delete ui->renderPresetsList->takeItem(currentRow);

        AppSettings::instance().setRenderPresets(m_renderPresets);
        AppSettings::instance().save();
    }
}
