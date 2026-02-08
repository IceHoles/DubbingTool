#include "styleselectordialog.h"

#include "ui_styleselectordialog.h"

#include <QFile>
#include <QListWidgetItem>
#include <QSet>
#include <QTextStream>

StyleSelectorDialog::StyleSelectorDialog(QWidget* parent) : QDialog(parent), ui(new Ui::StyleSelectorDialog)
{
    ui->setupUi(this);
}

StyleSelectorDialog::~StyleSelectorDialog()
{
    delete ui;
}

void StyleSelectorDialog::analyzeFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QSet<QString> foundStyles;
    QSet<QString> foundActors;

    QTextStream in(&file);
    bool inEvents = false;
    while (!in.atEnd())
    {
        QString line = in.readLine();
        if (line.startsWith("[Events]"))
        {
            inEvents = true;
            continue;
        }
        if (!inEvents)
            continue;
        if (line.startsWith("[Fonts]") || line.startsWith("[Graphics]"))
            break; // Дальше нет смысла искать

        if (line.startsWith("Style:"))
        {
            QStringList parts = line.split(QChar(','), Qt::SkipEmptyParts);
            if (!parts.isEmpty())
            {
                foundStyles.insert(parts[0].split(':').last().trimmed());
            }
        }
        else if (line.startsWith("Dialogue:"))
        {
            QStringList parts = line.split(QChar(','), Qt::KeepEmptyParts);
            if (parts.size() > 4)
            {
                QString style = parts[3].trimmed();
                QString actor = parts[4].trimmed();
                if (!style.isEmpty())
                    foundStyles.insert(style);
                if (!actor.isEmpty())
                    foundActors.insert(actor);
            }
        }
    }

    for (const QString& style : foundStyles)
    {
        QListWidgetItem* item = new QListWidgetItem(style, ui->stylesListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    ui->stylesListWidget->sortItems();
    for (const QString& actor : foundActors)
    {
        QListWidgetItem* item = new QListWidgetItem(actor, ui->actorsListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    ui->actorsListWidget->sortItems();
}

QStringList StyleSelectorDialog::getSelectedStyles() const
{
    QStringList selected;
    for (int i = 0; i < ui->stylesListWidget->count(); ++i)
    {
        QListWidgetItem* item = ui->stylesListWidget->item(i);
        if (item->checkState() == Qt::Checked)
        {
            selected.append(item->text());
        }
    }
    for (int i = 0; i < ui->actorsListWidget->count(); ++i)
    {
        QListWidgetItem* item = ui->actorsListWidget->item(i);
        if (item->checkState() == Qt::Checked)
        {
            selected.append(item->text());
        }
    }
    return selected;
}
