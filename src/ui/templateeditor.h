#ifndef TEMPLATEEDITOR_H
#define TEMPLATEEDITOR_H

#include <QDialog>
#include "releasetemplate.h"


namespace Ui {
class TemplateEditor;
}

class TemplateEditor : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateEditor(QWidget *parent = nullptr);
    ~TemplateEditor();

    void setTemplate(const ReleaseTemplate &t);
    ReleaseTemplate getTemplate() const;

private slots:
    void on_selectStylesButton_clicked();
    void on_helpButton_clicked();
    void slotValidateAndAccept();

private:
    static bool containsForbiddenChars(const QString &text);
    static QString forbiddenCharsFound(const QString &text);

    Ui::TemplateEditor *ui;
};

#endif // TEMPLATEEDITOR_H
