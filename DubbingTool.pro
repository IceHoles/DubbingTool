QT += core gui widgets network xml

TARGET = DubbingTool
TEMPLATE = app

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    appsettings.cpp \
    assprocessor.cpp \
    fontfinder.cpp \
    main.cpp \
    mainwindow.cpp \
    manualassembler.cpp \
    manualassemblywidget.cpp \
    manualrenderer.cpp \
    manualrenderwidget.cpp \
    missingfilesdialog.cpp \
    postgenerator.cpp \
    processmanager.cpp \
    publicationwidget.cpp \
    releasetemplate.cpp \
    renderhelper.cpp \
    rerenderdialog.cpp \
    settingsdialog.cpp \
    styleselectordialog.cpp \
    templateeditor.cpp \
    torrentselectordialog.cpp \
    trackselectordialog.cpp \
    workflowmanager.cpp

HEADERS += \
    appsettings.h \
    assprocessor.h \
    fontfinder.h \
    mainwindow.h \
    manualassembler.h \
    manualassemblywidget.h \
    manualrenderer.h \
    manualrenderwidget.h \
    missingfilesdialog.h \
    postgenerator.h \
    processmanager.h \
    publicationwidget.h \
    releasetemplate.h \
    renderhelper.h \
    rerenderdialog.h \
    settingsdialog.h \
    styleselectordialog.h \
    templateeditor.h \
    torrentselectordialog.h \
    trackselectordialog.h \
    workflowmanager.h

FORMS += \
    mainwindow.ui \
    manualassemblywidget.ui \
    manualrenderwidget.ui \
    missingfilesdialog.ui \
    publicationwidget.ui \
    rerenderdialog.ui \
    settingsdialog.ui \
    styleselectordialog.ui \
    templateeditor.ui \
    torrentselectordialog.ui \
    trackselectordialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32 {
    LIBS += dwrite.lib gdi32.lib user32.lib ole32.lib
    RC_FILE = resources.rc
}

RESOURCES += \
    resources.qrc
