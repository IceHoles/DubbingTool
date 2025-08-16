#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "appsettings.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QTableWidgetItem>


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
    AppSettings& settings = AppSettings::instance();

    ui->hostEdit->setText(settings.qbittorrentHost());
    ui->portSpinBox->setValue(settings.qbittorrentPort());
    ui->userEdit->setText(settings.qbittorrentUser());
    ui->passwordEdit->setText(settings.qbittorrentPassword());

    ui->mkvToolNixPathEdit->setText(settings.mkvmergePath());
    ui->mkvExtractPathEdit->setText(settings.mkvextractPath());
    ui->ffmpegPathEdit->setText(settings.ffmpegPath());
    ui->qbittorrentPathEdit->setText(settings.qbittorrentPath());
    ui->deleteTempFilesCheckBox->setChecked(settings.deleteTempFiles());
    ui->userFileActionComboBox->setCurrentIndex(static_cast<int>(settings.userFileAction()));

    ui->tbStylesTable->setRowCount(0);
    for (const auto& style : settings.tbStyles()) {
        int row = ui->tbStylesTable->rowCount();
        ui->tbStylesTable->insertRow(row);
        ui->tbStylesTable->setItem(row, 0, new QTableWidgetItem(style.name));
        ui->tbStylesTable->setItem(row, 1, new QTableWidgetItem(QString::number(style.resolutionX)));
        ui->tbStylesTable->setItem(row, 2, new QTableWidgetItem(style.tags));
        ui->tbStylesTable->setItem(row, 3, new QTableWidgetItem(QString::number(style.marginLeft)));
        ui->tbStylesTable->setItem(row, 4, new QTableWidgetItem(QString::number(style.marginRight)));
        ui->tbStylesTable->setItem(row, 5, new QTableWidgetItem(QString::number(style.marginV)));
    }

    m_renderPresets = settings.renderPresets();
    ui->renderPresetsList->clear();
    for (const auto& preset : m_renderPresets) {
        ui->renderPresetsList->addItem(preset.name);
    }

    if (ui->renderPresetsList->count() > 0) {
        ui->renderPresetsList->setCurrentRow(0);
    } else {
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
    settings.setQbittorrentPath(ui->qbittorrentPathEdit->text());
    settings.setDeleteTempFiles(ui->deleteTempFilesCheckBox->isChecked());
    settings.setUserFileAction(static_cast<UserFileAction>(ui->userFileActionComboBox->currentIndex()));

    QList<TbStyleInfo> styles;
    for (int row = 0; row < ui->tbStylesTable->rowCount(); ++row) {
        TbStyleInfo style;
        style.name = ui->tbStylesTable->item(row, 0)->text();
        style.resolutionX = ui->tbStylesTable->item(row, 1)->text().toInt();
        style.tags = ui->tbStylesTable->item(row, 2)->text();
        style.marginLeft = ui->tbStylesTable->item(row, 3)->text().toInt();
        style.marginRight = ui->tbStylesTable->item(row, 4)->text().toInt();
        style.marginV = ui->tbStylesTable->item(row, 5)->text().toInt();
        styles.append(style);
    }
    settings.setTbStyles(styles);

    settings.setRenderPresets(m_renderPresets);

    QSet<LogCategory> enabled;
    if (ui->logAppCheckBox->isChecked()) enabled.insert(LogCategory::APP);
    if (ui->logMkvToolNixCheckBox->isChecked()) enabled.insert(LogCategory::MKVTOOLNIX);
    if (ui->logFfmpegCheckBox->isChecked()) enabled.insert(LogCategory::FFMPEG);
    if (ui->logQbittorrentCheckBox->isChecked()) enabled.insert(LogCategory::QBITTORRENT);
    if (ui->logDebugCheckBox->isChecked()) enabled.insert(LogCategory::DEBUG);
    settings.setEnabledLogCategories(enabled);

    settings.save();
    QDialog::accept();
}

void SettingsDialog::on_browseMkvToolNixButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvmerge.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty()) ui->mkvToolNixPathEdit->setText(filePath);
}
void SettingsDialog::on_browseMkvExtractButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите mkvextract.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty()) ui->mkvExtractPathEdit->setText(filePath);
}
void SettingsDialog::on_browseFfmpegButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите ffmpeg.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty()) ui->ffmpegPathEdit->setText(filePath);
}
void SettingsDialog::on_browseQbittorrentButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите qbittorrent.exe", "", "Исполняемые файлы (*.exe)");
    if (!filePath.isEmpty()) ui->qbittorrentPathEdit->setText(filePath);
}

void SettingsDialog::on_addTbStyleButton_clicked()
{
    int row = ui->tbStylesTable->rowCount();
    ui->tbStylesTable->insertRow(row);
    ui->tbStylesTable->setItem(row, 0, new QTableWidgetItem("Новый стиль"));
    ui->tbStylesTable->setItem(row, 1, new QTableWidgetItem("1920"));
    ui->tbStylesTable->setItem(row, 2, new QTableWidgetItem("{\\fad(500,500)\\b1\\an3\\fnTahoma\\fs50\\shad3\\bord1.3\\4c&H000000&\\4a&H00&}"));
    ui->tbStylesTable->setItem(row, 3, new QTableWidgetItem("10"));
    ui->tbStylesTable->setItem(row, 4, new QTableWidgetItem("30"));
    ui->tbStylesTable->setItem(row, 5, new QTableWidgetItem("10"));
}

void SettingsDialog::on_removeTbStyleButton_clicked()
{
    int currentRow = ui->tbStylesTable->currentRow();
    if (currentRow >= 0) {
        ui->tbStylesTable->removeRow(currentRow);
    }
}

void SettingsDialog::on_renderPresetsList_currentRowChanged(int currentRow)
{
    if (currentRow < 0 || currentRow >= m_renderPresets.size()) {
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
    if (currentRow < 0) return;

    QString name = ui->renderPresetNameEdit->text().trimmed();
    if (name.isEmpty()) {
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
    newPreset.commandPass1 = "ffmpeg -y -hide_banner -i \"%INPUT%\" -vf \"subtitles=%SIGNS%\" -c:v hevc_qsv ...  \"%OUTPUT%\"";
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
    if (currentRow < 0) return;

    auto reply = QMessageBox::question(this, "Подтверждение", "Удалить пресет '" + m_renderPresets[currentRow].name + "'?",
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        m_renderPresets.removeAt(currentRow);
        delete ui->renderPresetsList->takeItem(currentRow);

        AppSettings::instance().setRenderPresets(m_renderPresets);
        AppSettings::instance().save();
    }
}
