#include "appsettings.h"
#include "mainwindow.h"
#include "setupwizarddialog.h"

#include <QApplication>
#include <QFileInfo>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    AppSettings::instance().load();

    const auto& settings = AppSettings::instance();
    bool toolsMissing = !QFileInfo::exists(settings.ffmpegPath()) || !QFileInfo::exists(settings.ffprobePath()) ||
                        !QFileInfo::exists(settings.mkvmergePath()) || !QFileInfo::exists(settings.mkvextractPath());

    if (!settings.isSetupCompleted() || toolsMissing)
    {
        SetupWizardDialog wizard;
        wizard.exec();
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
