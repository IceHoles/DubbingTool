#ifndef STYLESELECTORDIALOG_H
#define STYLESELECTORDIALOG_H

#include <QDialog>
#include <QStringList>

namespace Ui {
class StyleSelectorDialog;
}

class StyleSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StyleSelectorDialog(QWidget *parent = nullptr);
    ~StyleSelectorDialog();

    void analyzeFile(const QString &filePath);
    QStringList getSelectedStyles() const;

private:
    Ui::StyleSelectorDialog *ui;
};

#endif // STYLESELECTORDIALOG_H
