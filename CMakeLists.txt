cmake_minimum_required(VERSION 3.16)

project(DubbingTool
    VERSION 1.0.0
    DESCRIPTION "Dubbing Automation Tool"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tell AUTOUIC where to find .ui files
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Xml)

# Source files organized by module
set(SOURCES_CORE
    src/core/appsettings.cpp
    src/core/processmanager.cpp
    src/core/workflowmanager.cpp
)

set(SOURCES_PROCESSING
    src/processing/assprocessor.cpp
    src/processing/fontfinder.cpp
    src/processing/manualassembler.cpp
    src/processing/manualrenderer.cpp
    src/processing/postgenerator.cpp
    src/processing/renderhelper.cpp
    src/processing/telegramformatter.cpp
)

set(SOURCES_UI
    src/ui/main.cpp
    src/ui/mainwindow.cpp
    src/ui/manualassemblywidget.cpp
    src/ui/manualrenderwidget.cpp
    src/ui/missingfilesdialog.cpp
    src/ui/publicationwidget.cpp
    src/ui/rerenderdialog.cpp
    src/ui/settingsdialog.cpp
    src/ui/styleselectordialog.cpp
    src/ui/templateeditor.cpp
    src/ui/torrentselectordialog.cpp
    src/ui/trackselectordialog.cpp
)

set(SOURCES_MODELS
    src/models/releasetemplate.cpp
)

set(HEADERS_CORE
    src/core/appsettings.h
    src/core/processmanager.h
    src/core/workflowmanager.h
)

set(HEADERS_PROCESSING
    src/processing/assprocessor.h
    src/processing/fontfinder.h
    src/processing/manualassembler.h
    src/processing/manualrenderer.h
    src/processing/postgenerator.h
    src/processing/renderhelper.h
    src/processing/telegramformatter.h
)

set(HEADERS_UI
    src/ui/mainwindow.h
    src/ui/manualassemblywidget.h
    src/ui/manualrenderwidget.h
    src/ui/missingfilesdialog.h
    src/ui/placeholderwidget.h
    src/ui/publicationwidget.h
    src/ui/rerenderdialog.h
    src/ui/settingsdialog.h
    src/ui/styleselectordialog.h
    src/ui/templateeditor.h
    src/ui/torrentselectordialog.h
    src/ui/trackselectordialog.h
)

set(HEADERS_MODELS
    src/models/releasetemplate.h
)

set(FORMS
    ui/mainwindow.ui
    ui/manualassemblywidget.ui
    ui/manualrenderwidget.ui
    ui/missingfilesdialog.ui
    ui/publicationwidget.ui
    ui/rerenderdialog.ui
    ui/settingsdialog.ui
    ui/styleselectordialog.ui
    ui/templateeditor.ui
    ui/torrentselectordialog.ui
    ui/trackselectordialog.ui
)

set(RESOURCES
    resources/resources.qrc
)

# Combine all sources
set(ALL_SOURCES
    ${SOURCES_CORE}
    ${SOURCES_PROCESSING}
    ${SOURCES_UI}
    ${SOURCES_MODELS}
)

set(ALL_HEADERS
    ${HEADERS_CORE}
    ${HEADERS_PROCESSING}
    ${HEADERS_UI}
    ${HEADERS_MODELS}
)

qt_add_executable(${PROJECT_NAME}
    ${ALL_SOURCES}
    ${ALL_HEADERS}
    ${FORMS}
    ${RESOURCES}
)

# Include directories for finding headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/processing
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/ui
)

# Windows-specific settings
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dwrite
        gdi32
        user32
        ole32
    )
    
    # Add Windows resource file for icon
    target_sources(${PROJECT_NAME} PRIVATE resources/resources.rc)
    
    # Set Windows subsystem (GUI application, no console)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
)

# Enable warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# clang-tidy integration
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
        )
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()

# Install rules
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
