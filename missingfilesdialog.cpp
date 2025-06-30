#include "missingfilesdialog.h"
#include "ui_missingfilesdialog.h"
#include <QFileDialog>
#include <QListWidgetItem>

MissingFilesDialog::MissingFilesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MissingFilesDialog)
{
    ui->setupUi(this);
    // По умолчанию скрываем обе группы, будем показывать по мере необходимости
    ui->audioGroupBox->setVisible(false);
    ui->fontsGroupBox->setVisible(false);
}

MissingFilesDialog::~MissingFilesDialog()
{
    delete ui;
}

void MissingFilesDialog::setAudioPathVisible(bool visible)
{
    ui->audioGroupBox->setVisible(visible);
}

void MissingFilesDialog::setMissingFonts(const QStringList &fontNames)
{
    if (fontNames.isEmpty()) {
        ui->fontsGroupBox->setVisible(false);
        return;
    }

    ui->fontsGroupBox->setVisible(true);
    ui->fontsListWidget->clear();
    for (const QString &name : fontNames) {
        QListWidgetItem* item = new QListWidgetItem(name);
        item->setForeground(Qt::red); // Красный цвет для ненайденных
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

void MissingFilesDialog::on_browseAudioButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выберите аудиофайл", "", "Аудиофайлы (*.wav *.flac *.aac)");
    if (!filePath.isEmpty()) {
        ui->audioPathEdit->setText(filePath);
    }
}

void MissingFilesDialog::on_fontsListWidget_itemDoubleClicked(QListWidgetItem *item)
{
    QString fontName = item->text();
    QString path = QFileDialog::getOpenFileName(this, "Выберите файл для шрифта '" + fontName + "'", "",
                                                "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)");
    if (!path.isEmpty()) {
        item->setText(fontName + " -> " + path);
        item->setForeground(Qt::darkGreen); // Зеленый для найденных
        m_resolvedFonts[fontName] = path;
    }
}
