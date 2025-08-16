#ifndef TRACKSELECTORDIALOG_H
#define TRACKSELECTORDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>


struct AudioTrackInfo {
    int id;
    QString codec;
    QString name;
    QString language;
};

namespace Ui {
class TrackSelectorDialog;
}

class TrackSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrackSelectorDialog(const QList<AudioTrackInfo> &tracks, QWidget *parent = nullptr);
    ~TrackSelectorDialog();

    int getSelectedTrackId() const;

private:
    Ui::TrackSelectorDialog *ui;
    QList<AudioTrackInfo> m_tracks;
};

#endif // TRACKSELECTORDIALOG_H
