#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QClipboard>   // Для доступа к буферу обмена
#include <QMimeData>    // Для работы с данными в буфере
#include <QGuiApplication>
#include <QDebug>       // Для вывода в консоль
#include <QFile>        // Для сохранения в файл
#include <QDir>         // Для работы с директориями
#include <QDateTime>    // Для уникальных имен файлов

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Этот метод будет вызван при клике на кнопку analyzeButton
void MainWindow::on_analyzeButton_clicked()
{
    ui->logOutput->clear(); // Очищаем поле вывода
    ui->logOutput->append("Начинаем анализ буфера обмена...");

    // Получаем доступ к системному буферу обмена
    QClipboard *clipboard = QGuiApplication::clipboard();
    // Получаем объект QMimeData, который содержит все форматы
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData) {
        ui->logOutput->append("Ошибка: не удалось получить данные из буфера обмена.");
        return;
    }

    // Создаем подпапку для сохранения результатов, чтобы не мусорить
    // Имя папки будет содержать текущую дату и время
    QString dirName = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QDir dir;
    if (!dir.mkpath(dirName)) {
        ui->logOutput->append("Ошибка: не удалось создать директорию для сохранения файлов.");
        return;
    }

    ui->logOutput->append(QString("Данные будут сохранены в папку: %1").arg(QDir(dirName).absolutePath()));
    ui->logOutput->append("------------------------------------");

    // Получаем список всех доступных форматов (MIME-типов)
    QStringList formats = mimeData->formats();

    if (formats.isEmpty()) {
        ui->logOutput->append("Буфер обмена пуст или не содержит известных форматов.");
        return;
    }

    ui->logOutput->append(QString("Найдено форматов: %1").arg(formats.count()));

    // Проходим по каждому формату
    for (const QString &format : formats) {
        // Получаем данные для текущего формата в виде массива байт
        QByteArray data = mimeData->data(format);

        ui->logOutput->append(QString("\nФормат: %1").arg(format));
        ui->logOutput->append(QString("Размер данных: %1 байт").arg(data.size()));

        // Формируем имя файла. Заменяем символы, недопустимые в именах файлов.
        QString safeFormatName = format;
        safeFormatName.replace('/', '_').replace(':', '-');
        QString fileName = QString("%1/%2.bin").arg(dirName, safeFormatName);

        // Сохраняем байты в файл
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
            ui->logOutput->append(QString("Данные сохранены в файл: %1").arg(fileName));
        } else {
            ui->logOutput->append(QString("!!! Ошибка записи в файл: %1").arg(fileName));
        }
    }
    ui->logOutput->append("\n------------------------------------");
    ui->logOutput->append("Анализ завершен.");
}
