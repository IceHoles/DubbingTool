#ifndef PLACEHOLDERWIDGET_H
#define PLACEHOLDERWIDGET_H

#include <QWidget>

// Это простой наследник QWidget, который ничего не делает.
// Его единственная цель - заставить uic сгенерировать на него указатель.
class PlaceholderWidget : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget; // Используем конструкторы родителя
};

#endif // PLACEHOLDERWIDGET_H
