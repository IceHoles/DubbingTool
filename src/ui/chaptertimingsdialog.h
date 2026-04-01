#ifndef CHAPTERTIMINGSDIALOG_H
#define CHAPTERTIMINGSDIALOG_H

#include "chapterhelper.h"

#include <QDialog>

class QVBoxLayout;

class ChapterTimingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChapterTimingsDialog(const QList<ChapterTimingSeconds>& timings, QWidget* parent = nullptr);

private:
    QVBoxLayout* m_rowsLayout = nullptr;
};

#endif // CHAPTERTIMINGSDIALOG_H
