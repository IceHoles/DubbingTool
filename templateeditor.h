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

    // Заполняет поля редактора данными из шаблона
    void setTemplate(const ReleaseTemplate &t);

    // Собирает данные из полей редактора в объект шаблона
    ReleaseTemplate getTemplate() const;

private:
    Ui::TemplateEditor *ui;

private slots:
    void on_selectStylesButton_clicked();
    void on_addTbStyleButton_clicked();
    void on_removeTbStyleButton_clicked();
    void on_tbStylesTable_cellChanged(int row, int column);
    void on_helpButton_clicked();
};

#endif // TEMPLATEEDITOR_H
