#ifndef MISSINGFILESDIALOG_H
#define MISSINGFILESDIALOG_H

#include <QDialog>
#include <QMap>
#include <QListWidgetItem>


namespace Ui {
class MissingFilesDialog;
}

class MissingFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MissingFilesDialog(QWidget *parent = nullptr);
    ~MissingFilesDialog();

    // Функции для настройки диалога перед показом
    void setAudioPathVisible(bool visible);
    void setMissingFonts(const QStringList &fontNames);

    // Функции для получения результатов
    QString getAudioPath() const;
    QMap<QString, QString> getResolvedFonts() const;

private slots:
    void on_browseAudioButton_clicked();
    void on_fontsListWidget_itemDoubleClicked(QListWidgetItem *item);

private:
    Ui::MissingFilesDialog *ui;
    // Храним результат: Имя шрифта -> Путь к файлу
    QMap<QString, QString> m_resolvedFonts;
};

#endif // MISSINGFILESDIALOG_H
