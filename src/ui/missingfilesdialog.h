#ifndef MISSINGFILESDIALOG_H
#define MISSINGFILESDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
#include <QMap>

namespace Ui
{
class MissingFilesDialog;
} // namespace Ui

class MissingFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MissingFilesDialog(QWidget* parent = nullptr);
    ~MissingFilesDialog() override;

    void setAudioPathVisible(bool visible);
    void setMissingFonts(const QStringList& fontNames);
    void setChaptersInputVisible(bool visible);
    void setChaptersPrompt(const QString& text);
    void setAudioPrompt(const QString& text);

    [[nodiscard]] QString getAudioPath() const;
    [[nodiscard]] QMap<QString, QString> getResolvedFonts() const;
    [[nodiscard]] QString getChaptersXmlPath() const;
    [[nodiscard]] bool getBuildWithoutChapters() const;

private slots:
    void on_browseAudioButton_clicked();
    void on_browseChaptersButton_clicked();
    void on_fontsListWidget_itemDoubleClicked(QListWidgetItem* item);

private:
    Ui::MissingFilesDialog* ui;
    QMap<QString, QString> m_resolvedFonts;
};

#endif // MISSINGFILESDIALOG_H
