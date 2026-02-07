#ifndef SETUPWIZARDDIALOG_H
#define SETUPWIZARDDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QStackedWidget>

/**
 * @brief Setup wizard dialog shown on first launch or when required tools are missing.
 *
 * Contains three pages:
 * 1. Tool paths verification (ffmpeg, ffprobe, mkvmerge, mkvextract)
 * 2. qBittorrent Web API configuration
 * 3. Render preset selection
 */
class SetupWizardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetupWizardDialog(QWidget *parent = nullptr);

private:
    void buildToolsPage();
    void slotBrowseTool(int32_t toolIndex);
    void slotNextPage();
    void slotPrevPage();
    void slotFinish();
    void slotSkipQBittorrent();
    void buildQBittorrentPage();
    void buildPresetPage();
    void detectTools();
    void updateToolStatus(int32_t toolIndex, const QString &path);
    void updateNextButtonState();

    QStackedWidget *m_stack = nullptr;

    // Navigation buttons
    QPushButton *m_backButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_finishButton = nullptr;
    QPushButton *m_skipButton = nullptr;

    // Page 1: Tools
    struct ToolRow {
        QString exeName;
        QString displayName;
        QLabel *statusIcon = nullptr;
        QLineEdit *pathEdit = nullptr;
        QPushButton *browseButton = nullptr;
    };
    QList<ToolRow> m_tools;

    // Page 2: qBittorrent
    QLineEdit *m_qbtHostEdit = nullptr;
    QSpinBox *m_qbtPortSpin = nullptr;
    QLineEdit *m_qbtUserEdit = nullptr;
    QLineEdit *m_qbtPasswordEdit = nullptr;

    // Page 3: Render preset
    QComboBox *m_presetCombo = nullptr;
};

#endif // SETUPWIZARDDIALOG_H
