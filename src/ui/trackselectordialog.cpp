#include "trackselectordialog.h"

#include "ui_trackselectordialog.h"

#include <QPushButton>

TrackSelectorDialog::TrackSelectorDialog(const QList<AudioTrackInfo>& tracks, QWidget* parent, bool allowDecline)
    : QDialog(parent), ui(new Ui::TrackSelectorDialog), m_tracks(tracks)
{
    ui->setupUi(this);

    if (allowDecline)
    {
        if (auto* btn = ui->buttonBox->button(QDialogButtonBox::Cancel))
        {
            btn->setText(tr("Не использовать звук из видео"));
        }
        if (auto* btn = ui->buttonBox->button(QDialogButtonBox::Ok))
        {
            btn->setText(tr("Выбрать"));
        }
    }

    for (const auto& track : m_tracks)
    {
        QString itemText = QString("ID: %1 | Кодек: %2 | Язык: %3 | Имя: %4")
                               .arg(track.id)
                               .arg(track.codec)
                               .arg(track.language)
                               .arg(track.name.isEmpty() ? "Без имени" : track.name);
        if (track.channels > 0)
        {
            itemText += QString(" | Каналы: %1").arg(track.channels);
        }
        ui->tracksListWidget->addItem(itemText);
    }

    if (ui->tracksListWidget->count() > 0)
    {
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
    if (currentRow >= 0 && currentRow < m_tracks.size())
    {
        return m_tracks[currentRow].id;
    }
    return -1; // В случае ошибки или отмены
}

void TrackSelectorDialog::setSelectedTrackId(int trackId)
{
    for (int i = 0; i < m_tracks.size(); ++i)
    {
        if (m_tracks[i].id == trackId)
        {
            ui->tracksListWidget->setCurrentRow(i);
            break;
        }
    }
}
