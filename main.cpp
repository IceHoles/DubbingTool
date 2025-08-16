#include "mainwindow.h"
#include <QApplication>
#include "appsettings.h"


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    AppSettings::instance().load();

    MainWindow w;
    w.show();
    return a.exec();
}
