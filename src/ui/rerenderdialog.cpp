#include "rerenderdialog.h"

#include "ui_rerenderdialog.h"

#include "QPushButton"

RerenderDialog::RerenderDialog(const RenderPreset& preset, double actualBitrate, QWidget* parent)
    : QDialog(parent), ui(new Ui::RerenderDialog)
{
    ui->setupUi(this);

    ui->targetBitrateLabel->setText(QString("%1 kbps").arg(preset.targetBitrateKbps));
    ui->actualBitrateLabel->setText(QString::asprintf("%.0f kbps", actualBitrate));

    ui->command1Edit->setPlainText(preset.commandPass1);
    ui->command2Edit->setPlainText(preset.commandPass2);

    if (!preset.isTwoPass())
    {
        ui->command2Edit->setVisible(false);
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Перерендерить");
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText("Принять как есть");
}

RerenderDialog::~RerenderDialog()
{
    delete ui;
}

QString RerenderDialog::getCommandPass1() const
{
    return ui->command1Edit->toPlainText();
}

QString RerenderDialog::getCommandPass2() const
{
    return ui->command2Edit->toPlainText();
}
