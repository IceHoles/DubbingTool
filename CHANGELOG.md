# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Главы (MKV / MP4):** модуль `ChapterHelper` — разбор Matroska XML и JSON ffprobe, запись XML для mkvmerge, ffmetadata и пост-обработка MP4 (`applyChaptersToMp4`) без дублирования chapter-track в контейнере (`-map 0:v -map 0:a`).
- Шаблон: флаг ожидания глав (`chaptersEnabled`), предупреждение в `MissingFilesDialog`, если глав нет ни во входе, ни во внешнем XML; ручная сборка/рендер: свой XML глав, строка пути в UI.
- **Ручная сборка — шрифты:** кнопка «Очистить список»; список очищается после **успешной** сборки MKV (следующая серия без «хвоста» прошлых шрифтов); `ManualAssembler::finished(bool success)`.
- `docs/concat-cfr-debug-report.md` — отчёт по отладке concat (CFR/setts, швы, `\fad`, проверки ffprobe/framemd5).

### Changed
- **Ручная сборка — главы:** путь к XML глав (`chaptersXmlPath`) учитывается **всегда**, если файл существует (не требуется галочка «Свой XML»). В режиме «Ручной» поле пути к XML показывается всегда. Рендер MP4: в команду ffmpeg добавлен `-map_chapters -1`, чтобы не копировать главы из исходного MKV в промежуточный MP4 перед записью глав из ffmetadata; при первой главе не с `00:00` добавляется техническая пустая глава `00:00`, чтобы FFmpeg не сдвигал `OP` в ноль.
- **Ручная сборка MKV (`ManualAssembler`):** явные флаги дорожек как в авто-сборке — русское аудио default, оригинал `default no`; надписи `default + forced`; полные субтитры `default no` и `forced no`.
- **Авто-сборка MKV:** для полных субтитров добавлен явный `--forced-display-flag 0:no` (согласовано с ручным режимом).
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
