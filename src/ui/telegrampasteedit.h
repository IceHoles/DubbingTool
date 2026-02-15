#pragma once

#include <QPlainTextEdit>

class TelegramPasteEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    using QPlainTextEdit::QPlainTextEdit;

protected:
    void insertFromMimeData(const QMimeData* source) override;
};