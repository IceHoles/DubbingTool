#ifndef TRACKSELECTORDIALOG_H
#define TRACKSELECTORDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

struct AudioTrackInfo
{
    int id = -1;
    QString codec;
    QString name;
    QString language;
    int channels = 0;
};

namespace Ui
{
class TrackSelectorDialog;
} // namespace Ui

class TrackSelectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrackSelectorDialog(const QList<AudioTrackInfo>& tracks, QWidget* parent = nullptr,
                                 bool allowDecline = false);
    ~TrackSelectorDialog() override;

    int getSelectedTrackId() const;
    void setSelectedTrackId(int trackId);

private:
    Ui::TrackSelectorDialog* ui;
    QList<AudioTrackInfo> m_tracks;
};

#endif // TRACKSELECTORDIALOG_H
