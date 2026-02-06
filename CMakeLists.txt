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

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    # Minimal library for testing FontFinder (no UI dependencies)
    set(TESTABLE_SOURCES
        src/core/appsettings.cpp
        src/processing/fontfinder.cpp
        src/processing/assprocessor.cpp
        src/models/releasetemplate.cpp
    )

    set(TESTABLE_HEADERS
        src/core/appsettings.h
        src/processing/fontfinder.h
        src/processing/assprocessor.h
        src/models/releasetemplate.h
    )

    add_library(FontFinderLib STATIC
        ${TESTABLE_SOURCES}
        ${TESTABLE_HEADERS}
    )

    set_target_properties(FontFinderLib PROPERTIES AUTOMOC ON)

    target_include_directories(FontFinderLib PUBLIC
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/processing
        ${CMAKE_SOURCE_DIR}/src/models
    )

    target_link_libraries(FontFinderLib PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )

    if(WIN32)
        target_link_libraries(FontFinderLib PUBLIC
            dwrite
            gdi32
            user32
            ole32
        )
    endif()

    if(MSVC)
        target_compile_options(FontFinderLib PRIVATE /W3 /utf-8)
    else()
        target_compile_options(FontFinderLib PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # FontFinder tests
    add_executable(fontfinder_test tests/fontfinder_test.cpp)
    set_target_properties(fontfinder_test PROPERTIES AUTOMOC ON)
    target_link_libraries(fontfinder_test PRIVATE
        FontFinderLib
        Qt6::Test
    )

    # Copy test data files to build directory
    set(TEST_DATA_DIR ${CMAKE_SOURCE_DIR}/tests/test_data)
    file(GLOB TEST_DATA_FILES "${TEST_DATA_DIR}/*.ass")
    
    add_custom_command(TARGET fontfinder_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:fontfinder_test>/test_data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${TEST_DATA_FILES}
            "$<TARGET_FILE_DIR:fontfinder_test>/test_data/"
        COMMENT "Copying test data files..."
    )

    add_test(NAME FontFinderTest COMMAND fontfinder_test)
endif()
