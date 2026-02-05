#ifndef TORRENTSELECTORDIALOG_H
#define TORRENTSELECTORDIALOG_H

#include <QDialog>


struct TorrentInfo {
    QString title;
    QString magnetLink;
};

namespace Ui {
class TorrentSelectorDialog;
}

class TorrentSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TorrentSelectorDialog(const QList<TorrentInfo> &torrents, QWidget *parent = nullptr);
    ~TorrentSelectorDialog();

    int getSelectedIndex() const;

private:
    Ui::TorrentSelectorDialog *ui;
};

#endif // TORRENTSELECTORDIALOG_H
