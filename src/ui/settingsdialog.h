#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "appsettings.h"

#include <QDialog>

namespace Ui
{
class SettingsDialog;
} // namespace Ui

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();

private slots:
    void accept() override;
    void on_browseMkvToolNixButton_clicked();
    void on_browseMkvExtractButton_clicked();
    void on_browseFfmpegButton_clicked();
    void on_browseQbittorrentButton_clicked();
    void on_browseNugenAmbButton_clicked();
    void on_browseProjectDirectoryButton_clicked();
    void on_addTbStyleButton_clicked();
    void on_removeTbStyleButton_clicked();
    void on_renderPresetsList_currentRowChanged(int currentRow);
    void on_newRenderPresetButton_clicked();
    void on_saveRenderPresetButton_clicked();
    void on_deleteRenderPresetButton_clicked();

private:
    void loadSettings();
    void saveSettings();

    Ui::SettingsDialog* ui;
    QList<RenderPreset> m_renderPresets;
};

#endif // SETTINGSDIALOG_H
