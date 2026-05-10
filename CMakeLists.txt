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

option(ENABLE_COVERAGE "Compiler instrumentation for coverage (GCC or Clang only; MSVC: scripts/CoverageOpenCpp.ps1)" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate)
    else()
        message(WARNING "ENABLE_COVERAGE applies only to GNU or Clang; with MSVC use OpenCppCoverage (see scripts/CoverageOpenCpp.ps1).")
    endif()
endif()

# Tell AUTOUIC where to find .ui files
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Xml Multimedia MultimediaWidgets)

# Source files organized by module
set(SOURCES_CORE
    src/core/appsettings.cpp
    src/core/chapterhelper.cpp
    src/core/iprocessrunner.cpp
    src/core/processmanager.cpp
    src/core/workflowmanager.cpp
)

set(SOURCES_PROCESSING
    src/processing/assprocessor.cpp
    src/processing/concattbrenderer.cpp
    src/processing/fontfinder.cpp
    src/processing/manualassembler.cpp
    src/processing/manualrenderer.cpp
    src/processing/postgenerator.cpp
    src/processing/renderhelper.cpp
    src/processing/telegramformatter.cpp
)

set(SOURCES_UI
    src/ui/main.cpp
    src/ui/chaptertimingsdialog.cpp
    src/ui/mainwindow.cpp
    src/ui/manualassemblywidget.cpp
    src/ui/manualrenderwidget.cpp
    src/ui/missingfilesdialog.cpp
    src/ui/publicationwidget.cpp
    src/ui/rerenderdialog.cpp
    src/ui/settingsdialog.cpp
    src/ui/setupwizarddialog.cpp
    src/ui/styleselectordialog.cpp
    src/ui/templateeditor.cpp
    src/ui/torrentselectordialog.cpp        
    src/ui/trackselectordialog.cpp
    src/ui/telegrampasteedit.cpp
    src/ui/manualextractionwidget.cpp
)

set(SOURCES_MODELS
    src/models/releasetemplate.cpp
)

set(HEADERS_CORE
    src/core/appsettings.h
    src/core/chapterhelper.h
    src/core/iprocessrunner.h
    src/core/processmanager.h
    src/core/workflowmanager.h
)

set(HEADERS_PROCESSING
    src/processing/assprocessor.h
    src/processing/concattbrenderer.h
    src/processing/fontfinder.h
    src/processing/manualassembler.h
    src/processing/manualrenderer.h
    src/processing/postgenerator.h
    src/processing/renderhelper.h
    src/processing/telegramformatter.h
)

set(HEADERS_UI
    src/ui/chaptertimingsdialog.h
    src/ui/mainwindow.h
    src/ui/manualassemblywidget.h
    src/ui/manualrenderwidget.h
    src/ui/missingfilesdialog.h
    src/ui/placeholderwidget.h
    src/ui/publicationwidget.h
    src/ui/rerenderdialog.h
    src/ui/settingsdialog.h
    src/ui/setupwizarddialog.h
    src/ui/styleselectordialog.h
    src/ui/templateeditor.h
    src/ui/telegrampasteedit.h
    src/ui/torrentselectordialog.h
    src/ui/trackselectordialog.h
    src/ui/manualextractionwidget.h
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
    ui/manualextractionwidget.ui
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
    Qt6::Multimedia
    Qt6::MultimediaWidgets
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
        set(CLANG_TIDY_COMMAND
            "${CLANG_TIDY_EXE}"
            "--warnings-as-errors=bugprone-*,clang-analyzer-*,-clang-analyzer-cplusplus.NewDelete,-bugprone-reserved-identifier"
        )
        set_target_properties(${PROJECT_NAME} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}"
        )
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()

include(GNUInstallDirs)

# Ship default post template catalog next to the executable (edit data/post_template_catalog.json for releases).
set(POST_TEMPLATE_CATALOG ${CMAKE_SOURCE_DIR}/data/post_template_catalog.json)
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${POST_TEMPLATE_CATALOG}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/post_template_catalog.json"
    COMMENT "Copy post_template_catalog.json next to executable")

# Install rules
install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${POST_TEMPLATE_CATALOG} DESTINATION ${CMAKE_INSTALL_BINDIR})

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    # Static library shared by unit tests: add new production .cpp/.h here when tests need them.
    set(TEST_SUPPORT_SOURCES
        src/core/appsettings.cpp
        src/core/chapterhelper.cpp
        src/core/iprocessrunner.cpp
        src/core/processmanager.cpp
        src/processing/fontfinder.cpp
        src/processing/assprocessor.cpp
        src/processing/telegramformatter.cpp
        src/models/releasetemplate.cpp
    )

    set(TEST_SUPPORT_HEADERS
        src/core/appsettings.h
        src/core/chapterhelper.h
        src/core/iprocessrunner.h
        src/core/processmanager.h
        src/processing/fontfinder.h
        src/processing/assprocessor.h
        src/processing/telegramformatter.h
        src/models/releasetemplate.h
    )

    add_library(DubbingToolTestSupport STATIC
        ${TEST_SUPPORT_SOURCES}
        ${TEST_SUPPORT_HEADERS}
    )

    set_target_properties(DubbingToolTestSupport PROPERTIES AUTOMOC ON)

    target_include_directories(DubbingToolTestSupport PUBLIC
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/processing
        ${CMAKE_SOURCE_DIR}/src/models
    )

    target_link_libraries(DubbingToolTestSupport PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Xml
    )

    if(WIN32)
        target_link_libraries(DubbingToolTestSupport PUBLIC
            dwrite
            gdi32
            user32
            ole32
        )
    endif()

    if(MSVC)
        target_compile_options(DubbingToolTestSupport PRIVATE /W3 /utf-8)
    else()
        target_compile_options(DubbingToolTestSupport PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_compile_definitions(DubbingToolTestSupport PRIVATE DUBBINGTOOL_ENABLE_TEST_HOOKS)

    set(TEST_DATA_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/test_data)

    macro(dubbingtool_copy_test_data targetName)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${TEST_DATA_SOURCE_DIR}"
                    "$<TARGET_FILE_DIR:${targetName}>/test_data"
            COMMENT "Copy tests/test_data for ${targetName}"
        )
    endmacro()

    add_executable(fontfinder_test tests/fontfinder_test.cpp)
    set_target_properties(fontfinder_test PROPERTIES AUTOMOC ON)
    target_link_libraries(fontfinder_test PRIVATE DubbingToolTestSupport Qt6::Test)
    dubbingtool_copy_test_data(fontfinder_test)
    add_test(NAME FontFinderTest COMMAND fontfinder_test)

    add_executable(chapterhelper_test tests/chapterhelper_test.cpp)
    set_target_properties(chapterhelper_test PROPERTIES AUTOMOC ON)
    target_link_libraries(chapterhelper_test PRIVATE DubbingToolTestSupport Qt6::Test)
    dubbingtool_copy_test_data(chapterhelper_test)
    add_test(NAME ChapterHelperTest COMMAND chapterhelper_test)

    add_executable(telegramformatter_test tests/telegramformatter_test.cpp)
    set_target_properties(telegramformatter_test PROPERTIES AUTOMOC ON)
    target_link_libraries(telegramformatter_test PRIVATE DubbingToolTestSupport Qt6::Test)
    add_test(NAME TelegramFormatterTest COMMAND telegramformatter_test)

    add_executable(manualrenderer_test
        tests/manualrenderer_test.cpp
        tests/fakeprocessrunner.cpp
        tests/fakeprocessrunner.h
    )
    set_target_properties(manualrenderer_test PROPERTIES AUTOMOC ON)
    target_include_directories(manualrenderer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/tests
    )
    target_link_libraries(manualrenderer_test PRIVATE DubbingToolTestSupport Qt6::Test)
    add_test(NAME ManualRendererTest COMMAND manualrenderer_test)

    add_executable(ffmpeg_smoke_test tests/ffmpeg_smoke_test.cpp)
    set_target_properties(ffmpeg_smoke_test PROPERTIES AUTOMOC ON)
    target_link_libraries(ffmpeg_smoke_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME FfmpegSmokeTest COMMAND ffmpeg_smoke_test)

    # Qt DLLs are not on PATH by default when running ctest from a developer shell.
    if(WIN32)
        get_filename_component(_qt_bin_for_tests "${Qt6_DIR}/../../../bin" ABSOLUTE)
        set(_ctest_path "${_qt_bin_for_tests};$ENV{PATH}")
        string(REPLACE ";" "\\;" _ctest_path_esc "${_ctest_path}")
        set_tests_properties(FontFinderTest ChapterHelperTest TelegramFormatterTest ManualRendererTest FfmpegSmokeTest PROPERTIES
            ENVIRONMENT "PATH=${_ctest_path_esc}")
    endif()
endif()
