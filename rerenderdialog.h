#ifndef RERENDERDIALOG_H
#define RERENDERDIALOG_H

#include "appsettings.h"
#include <QDialog>


namespace Ui {
class RerenderDialog;
}

class RerenderDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RerenderDialog(const RenderPreset &preset, double actualBitrate, QWidget *parent = nullptr);
    ~RerenderDialog();

    QString getCommandPass1() const;
    QString getCommandPass2() const;

private:
    Ui::RerenderDialog *ui;
};

#endif // RERENDERDIALOG_H
