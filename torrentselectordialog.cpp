#include "torrentselectordialog.h"
#include "ui_torrentselectordialog.h"


TorrentSelectorDialog::TorrentSelectorDialog(const QList<TorrentInfo> &torrents, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TorrentSelectorDialog)
{
    ui->setupUi(this);

    for (const auto& torrent : torrents) {
        ui->torrentsListWidget->addItem(torrent.title);
    }

    if (ui->torrentsListWidget->count() > 0) {
        ui->torrentsListWidget->setCurrentRow(0);
    }
}

TorrentSelectorDialog::~TorrentSelectorDialog()
{
    delete ui;
}

int TorrentSelectorDialog::getSelectedIndex() const
{
    return ui->torrentsListWidget->currentRow();
}
