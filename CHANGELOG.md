# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.0] - 2026-02-05

### Added
- CMake build system (replaces qmake)
- clang-tidy configuration for static analysis
- clangd configuration for IDE integration
- `.gitignore` file
- `.vscode/extensions.json` with recommended extensions
- `.vscode/settings.json` with project settings
- `TODO.md` with future improvements roadmap
- `CHANGELOG.md`

### Changed
- **Project structure reorganized:**
  - `src/core/` — core classes (AppSettings, ProcessManager, WorkflowManager)
  - `src/processing/` — processing classes (AssProcessor, FontFinder, RenderHelper, etc.)
  - `src/ui/` — UI classes (MainWindow, dialogs, widgets)
  - `src/models/` — data models (ReleaseTemplate)
  - `ui/` — Qt Designer .ui files
  - `resources/` — resources (.qrc, .rc, icons, font_finder_wrapper)
- CI workflow updated for CMake build
- Fixed clang warning: use `u'...'` for Cyrillic char literals in `assprocessor.cpp`

### Removed
- `DubbingTool.pro` (qmake project file, replaced by CMakeLists.txt)

## [1.0.0] - Initial Release

### Added
- Automatic torrent download via qBittorrent Web API
- MKV processing (extract/merge tracks, fonts)
- ASS subtitle processing with credits generation
- Audio conversion (AAC/FLAC)
- MP4 rendering with hardcoded subtitles
- Post generation for Telegram/VK
- Manual assembly and render modes
- Release template system
