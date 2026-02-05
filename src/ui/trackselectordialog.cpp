#include "trackselectordialog.h"
#include "ui_trackselectordialog.h"


TrackSelectorDialog::TrackSelectorDialog(const QList<AudioTrackInfo> &tracks, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TrackSelectorDialog),
    m_tracks(tracks)
{
    ui->setupUi(this);

    for (const auto& track : m_tracks) {
        QString itemText = QString("ID: %1 | Кодек: %2 | Язык: %3 | Имя: %4")
                               .arg(track.id)
                               .arg(track.codec)
                               .arg(track.language)
                               .arg(track.name.isEmpty() ? "Без имени" : track.name);
        ui->tracksListWidget->addItem(itemText);
    }

    if (ui->tracksListWidget->count() > 0) {
        ui->tracksListWidget->setCurrentRow(0);
    }
}

TrackSelectorDialog::~TrackSelectorDialog()
{
    delete ui;
}

int TrackSelectorDialog::getSelectedTrackId() const
{
    int currentRow = ui->tracksListWidget->currentRow();
    if (currentRow >= 0 && currentRow < m_tracks.size()) {
        return m_tracks[currentRow].id;
    }
    return -1; // В случае ошибки или отмены
}
