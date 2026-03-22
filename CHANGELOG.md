# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `docs/concat-cfr-debug-report.md` — отчёт по отладке concat (CFR/setts, швы, `\fad`, проверки ffprobe/framemd5).

### Changed
- **Concat TB (авто `WorkflowManager` + ручной `ConcatTbRenderer`):**
  - учёт B-frame overlap по длительности TS и `trim` в сегменте 2;
  - для MP4: поправка «щели» в ~1 кадр между хвостом seg1 и головой seg2 (framemd5 + шаг кадра);
  - порядок vf: `trim → setpts → subtitles` с якорем субтитров `qMin(kfBeforeTb+overlap, tbStart)` (без обрезки fade-in);
  - финальная склейка: `-bsf:v setts` с рациональным FPS для CFR (`pts`+`dts`); VFR — без setts;
  - коррекция старта seg3 при перекрытии суммарной длительности seg1+seg2 с границей keyframe;
  - ручной путь: компенсация слишком длинного хвоста при `-ss` copy для MKV/WebM (seg3).
- **`VideoTrack` / метаданные:** поля `avgFrameRate`, `isCfr` для решения о setts.
- **`ManualRenderer`:** чтение `r_frame_rate` / `avg_frame_rate` из ffprobe, эвристика CFR, передача в `ConcatTbRenderer`.

### Removed
- Временная отладочная инструментация (логи в файлы, лишние ffmpeg-пробы в cleanup/join, неиспользуемые probe-хелперы).

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
