#include "missingfilesdialog.h"

#include "ui_missingfilesdialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QListWidgetItem>

MissingFilesDialog::MissingFilesDialog(QWidget* parent) : QDialog(parent), ui(new Ui::MissingFilesDialog)
{
    ui->setupUi(this);
    ui->audioGroupBox->setVisible(false);
    ui->fontsGroupBox->setVisible(false);
    ui->chaptersGroupBox->setVisible(false);

    connect(ui->buildWithoutChaptersCheckBox, &QCheckBox::toggled, this,
            [this](bool without)
            {
                ui->chaptersPathEdit->setEnabled(!without);
                ui->browseChaptersButton->setEnabled(!without);
                if (without)
                {
                    ui->chaptersPathEdit->clear();
                }
            });
}

MissingFilesDialog::~MissingFilesDialog()
{
    delete ui;
}

void MissingFilesDialog::setAudioPathVisible(bool visible)
{
    ui->audioGroupBox->setVisible(visible);
}

void MissingFilesDialog::setMissingFonts(const QStringList& fontNames)
{
    if (fontNames.isEmpty())
    {
        ui->fontsGroupBox->setVisible(false);
        return;
    }

    ui->fontsGroupBox->setVisible(true);
    ui->fontsListWidget->clear();
    for (const QString& name : fontNames)
    {
        auto* item = new QListWidgetItem(name);
        item->setForeground(Qt::red);
        ui->fontsListWidget->addItem(item);
    }
}

QString MissingFilesDialog::getAudioPath() const
{
    return ui->audioPathEdit->text();
}

QMap<QString, QString> MissingFilesDialog::getResolvedFonts() const
{
    return m_resolvedFonts;
}

void MissingFilesDialog::setChaptersInputVisible(bool visible)
{
    ui->chaptersGroupBox->setVisible(visible);
    if (!visible)
    {
        ui->chaptersPathEdit->clear();
        ui->buildWithoutChaptersCheckBox->setChecked(false);
    }
}

void MissingFilesDialog::setChaptersPrompt(const QString& text)
{
    ui->chaptersLabel->setText(text);
}

void MissingFilesDialog::setAudioPrompt(const QString& text)
{
    ui->audioLabel->setText(text);
}

QString MissingFilesDialog::getChaptersXmlPath() const
{
    return ui->chaptersPathEdit->text().trimmed();
}

bool MissingFilesDialog::getBuildWithoutChapters() const
{
    return ui->buildWithoutChaptersCheckBox->isChecked();
}

void MissingFilesDialog::on_browseChaptersButton_clicked()
{
    QString path = QFileDialog::getOpenFileName(this, "Файл глав Matroska (XML)", "", "XML (*.xml);;Все файлы (*)");
    if (!path.isEmpty())
    {
        ui->buildWithoutChaptersCheckBox->setChecked(false);
        ui->chaptersPathEdit->setText(path);
    }
}

void MissingFilesDialog::on_browseAudioButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите аудиофайл", "",
                                                    "Аудиофайлы (*.wav *.flac *.aac *.eac3 *.m4a *.mka)");
    if (!filePath.isEmpty())
    {
        ui->audioPathEdit->setText(filePath);
    }
}

void MissingFilesDialog::on_fontsListWidget_itemDoubleClicked(QListWidgetItem* item)
{
    QString fontName = item->text();
    QString path = QFileDialog::getOpenFileName(this, "Выберите файл для шрифта '" + fontName + "'", "",
                                                "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");
    if (!path.isEmpty())
    {
        item->setText(fontName + " -> " + path);
        item->setForeground(Qt::darkGreen);
        m_resolvedFonts[fontName] = path;
    }
}
