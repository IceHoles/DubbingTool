#include "appsettings.h"
#include "mainwindow.h"
#include "setupwizarddialog.h"

#include <QApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

int main(int argc, char* argv[])
{
    // Пока оставляем QApplication из-за старых виджетов
    QApplication a(argc, argv);

    // Включаем современный дизайн (стиль Windows 11)
    QQuickStyle::setStyle("FluentWinUI3");

    AppSettings::instance().load();

    const auto& settings = AppSettings::instance();
    bool toolsMissing = !QFileInfo::exists(settings.ffmpegPath()) || !QFileInfo::exists(settings.ffprobePath()) ||
                        !QFileInfo::exists(settings.mkvmergePath()) || !QFileInfo::exists(settings.mkvextractPath());

    if (!settings.isSetupCompleted() || toolsMissing)
    {
        SetupWizardDialog wizard;
        wizard.exec();
    }

    // --- ЗАПУСК НОВОГО QML ИНТЕРФЕЙСА ---
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &a, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("DubbingTool", "Main");
    // #ifdef Q_OS_WIN
    //     if (!engine.rootObjects().isEmpty())
    //     {
    //         // Кастуем именно к QQuickWindow
    //         QQuickWindow* qmlWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    //         if (qmlWindow)
    //         {
    //             // Теперь метод setColor доступен!
    //             qmlWindow->setColor(Qt::transparent);

    //             HWND hwnd = reinterpret_cast<HWND>(qmlWindow->winId());

    //             int backdropType = 2; // 2 = Mica
    //             DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));

    //             int darkMode = 1;
    //             DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
    //         }
    //     }
    // #endif
    // Пока оставляем и старое окно для сравнения и переноса функционала
    MainWindow w;
    w.show();

    return QApplication::exec();
}