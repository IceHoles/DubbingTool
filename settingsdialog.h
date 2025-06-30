#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

private slots:
    void accept();
    void on_browseMkvToolNixButton_clicked();
    void on_browseMkvExtractButton_clicked();
    void on_browseFfmpegButton_clicked();
    void on_browseQbittorrentButton_clicked();

private:
    void loadSettings();

    Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
