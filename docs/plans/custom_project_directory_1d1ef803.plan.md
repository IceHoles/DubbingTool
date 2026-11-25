---
name: Custom project directory
overview: Add a configurable project directory setting (replacing hardcoded `downloads/`), simplify PathManager to 2 directories (Sources + Result), and enhance UseOriginalPath mode to work next to source files.
todos:
  - id: pathmanager-simplify
    content: "Simplify PathManager: remove subsPath/ruAudioPath, keep only sourcesPath + resultPath. Redirect processedFullSubs/processedSignsSubs/masterSrt/convertedRuAudio to sourcesPath."
    status: completed
  - id: pathmanager-useoriginal
    content: "Add useOriginalPath param to PathManager constructor: set resultPath = basePath when active."
    status: completed
  - id: workflow-callers-fix
    content: Update all callers of m_paths->ruAudioPath (3 places in workflowmanager.cpp) to m_paths->sourcesPath.
    status: completed
  - id: appsettings
    content: Add projectDirectory setting to AppSettings (member, getter/setter, effectiveProjectDirectory(), load/save).
    status: completed
  - id: workflow-basepath
    content: Update start() and startWithManualFile() to use effectiveProjectDirectory() and UseOriginalPath logic.
    status: completed
  - id: settings-ui
    content: Add project directory QLineEdit + browse button to settingsdialog.ui and wire up in SettingsDialog.
    status: completed
isProject: false
---

# Customizable Project Directory and Simplified PathManager

## Current Architecture

`PathManager` (struct in `[workflowmanager.h](src/core/workflowmanager.h)`, lines 64-161) creates 4 subdirectories:

```
downloads/{seriesTitle}/Episode {N}/
  ├── Sources/           (extracted video, audio, subs, fonts)
  ├── Processed Subs/    (processed .ass, master .srt)
  ├── RU Audio/          (converted russian audio)
  └── Result/            (final MKV/MP4)
```

The `downloads/` prefix is hardcoded in `[workflowmanager.cpp](src/core/workflowmanager.cpp)` lines 74-75 and 96-97.

## Changes

### 1. Simplify PathManager to 2 directories: Sources + Result

**File:** `[workflowmanager.h](src/core/workflowmanager.h)` (lines 64-161)

Remove `subsPath` and `ruAudioPath` members entirely. All intermediate files go into `sourcesPath`. New structure:

```
{base}/
  ├── Sources/
  │   ├── video.h264
  │   ├── audio_original.aac
  │   ├── subtitles_original.ass
  │   ├── attached_fonts/
  │   ├── subtitles_processed_full.ass    (was in Processed Subs/)
  │   ├── subtitles_processed_signs.ass   (was in Processed Subs/)
  │   ├── master_subtitles.srt            (was in Processed Subs/)
  │   ├── russian_audio.aac               (was in RU Audio/)
  │   └── ffmpeg_progress.log             (was in RU Audio/)
  └── Result/
      └── [Final].mkv
```

Updated PathManager constructor with `useOriginalPath` parameter:

```cpp
PathManager(const QString& baseSavePath, bool useOriginalPath = false)
{
    basePath = sanitizeForPath(baseSavePath);
    sourcesPath = QDir(basePath).filePath("Sources");
    resultPath = useOriginalPath ? basePath : QDir(basePath).filePath("Result");

    QDir(sourcesPath).mkpath(".");
    QDir(resultPath).mkpath(".");
}
```

Methods `processedFullSubs()`, `processedSignsSubs()`, `masterSrt()`, `convertedRuAudio()` all change to use `sourcesPath` instead of the removed `subsPath`/`ruAudioPath`.

### 2. Fix callers of removed paths

**File:** `[workflowmanager.cpp](src/core/workflowmanager.cpp)`

3 places reference `m_paths->ruAudioPath` directly (not through a method):

- Line 1498: `m_ffmpegProgressFile = QDir(m_paths->ruAudioPath).filePath(...)` -> change to `m_paths->sourcesPath`
- Line 2767: `handleUserFile(response.audioPath, m_paths->ruAudioPath)` -> change to `m_paths->sourcesPath`
- Line 3128: `handleUserFile(oldAudioPath, m_paths->ruAudioPath)` -> change to `m_paths->sourcesPath`

### 3. Add `projectDirectory` setting to AppSettings

**Files:** `[appsettings.h](src/core/appsettings.h)`, `[appsettings.cpp](src/core/appsettings.cpp)`

- Add `QString m_projectDirectory` member (default: empty = use `"downloads"` relative to CWD)
- Getter/setter: `projectDirectory()` / `setProjectDirectory()`
- Helper: `effectiveProjectDirectory()` returns `m_projectDirectory` if non-empty, else `"downloads"`
- Load key: `"general/projectDirectory"`, save alongside other settings

### 4. Update WorkflowManager base path logic

**File:** `[workflowmanager.cpp](src/core/workflowmanager.cpp)`

`**start()` (torrent workflow):**

```cpp
QString baseDir = AppSettings::instance().effectiveProjectDirectory();
QString baseDownloadPath = QString("%1/%2/Episode %3")
    .arg(baseDir, m_template.seriesTitle, m_episodeNumberForSearch);
m_paths = new PathManager(baseDownloadPath);
```

`**startWithManualFile()` (manual file workflow):**

```cpp
UserFileAction action = AppSettings::instance().userFileAction();
QString baseDownloadPath;
if (action == UserFileAction::UseOriginalPath)
{
    QFileInfo fileInfo(filePath);
    baseDownloadPath = fileInfo.absolutePath();
}
else
{
    QString baseDir = AppSettings::instance().effectiveProjectDirectory();
    baseDownloadPath = QString("%1/%2/Episode %3")
        .arg(baseDir, m_template.seriesTitle, m_episodeNumberForSearch);
}
bool useOriginal = (action == UserFileAction::UseOriginalPath);
m_paths = new PathManager(baseDownloadPath, useOriginal);
```

When `useOriginalPath = true`:

- `sourcesPath` = `{sourceFileDir}/Sources/` -- intermediate files next to source
- `resultPath` = `{sourceFileDir}` -- result goes directly next to source (no `Result/` subdirectory)

### 5. Add UI for project directory

**Files:** `[settingsdialog.ui](ui/settingsdialog.ui)`, `[settingsdialog.h](src/ui/settingsdialog.h)`, `[settingsdialog.cpp](src/ui/settingsdialog.cpp)`

Add a row in tab 1, file handling group (after `userFileActionComboBox`):

- QLabel: "Директория проектов:"
- QLineEdit `projectDirectoryEdit` with placeholder "downloads (по умолчанию)"
- QPushButton "Обзор..." -> `QFileDialog::getExistingDirectory()`
- Wire load/save in `loadSettings()` / `accept()`

### 6. Edge cases

- **Empty project directory**: falls back to `"downloads"` (backward-compatible)
- **Relative vs absolute**: relative treated relative to CWD (same as current `"downloads"`)
- **UseOriginalPath + torrent `start()**`: uses `effectiveProjectDirectory()` since no source file exists yet; UseOriginalPath only affects `handleUserFile()` behavior
- **File name conflicts**: generic names (`video.h264`, `audio_original.aac`) in Sources/ are fine since one episode is processed at a time per directory

## Summary of affected files

- `[src/core/workflowmanager.h](src/core/workflowmanager.h)` -- Simplify PathManager: remove `subsPath`/`ruAudioPath`, add `useOriginalPath` constructor param
- `[src/core/workflowmanager.cpp](src/core/workflowmanager.cpp)` -- Update `start()`/`startWithManualFile()` base path logic, fix 3 `ruAudioPath` references
- `[src/core/appsettings.h](src/core/appsettings.h)` -- Add `m_projectDirectory`, getter/setter, `effectiveProjectDirectory()`
- `[src/core/appsettings.cpp](src/core/appsettings.cpp)` -- Load/save `m_projectDirectory`
- `[ui/settingsdialog.ui](ui/settingsdialog.ui)` -- Add project directory QLineEdit + browse button
- `[src/ui/settingsdialog.h](src/ui/settingsdialog.h)` -- Add browse button slot
- `[src/ui/settingsdialog.cpp](src/ui/settingsdialog.cpp)` -- Load/save project directory, browse handler

