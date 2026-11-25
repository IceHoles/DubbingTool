# DubbingTool — Agent Project Guidelines & Architecture Memo

## 1. Overview & Branch Strategy

- **Main branch**: `main` (CI-green, CMake-based Qt6 build).
- **Working branch**: `main` (commit and push phase-by-phase).
- **Historical context**: QML porting and automated batch mode were dropped. The project is focused on fast, reliable release engineering using Qt Widgets.

---

## 2. Environment & Build Setup (Local Windows)

- **OS**: Windows x64
- **Compiler**: VS 2026 Build Tools (MSVC 14.50) at `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`
- **Qt**: 6.10.2 MSVC 2022 64-bit at `C:\Qt\6.10.2\msvc2022_64`
- **Build System**: CMake + Ninja (`DubbingTool.pro` removed)
- **vcvars**: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`

### Critical Execution Rules (PowerShell / CMD)
- `cmake` is usually **not** in system PATH in plain PowerShell sessions.
- **The segment `(x86)` in `Program Files (x86)` is treated by PowerShell as a subexpression** if unquoted, which breaks commands.
- **Always invoke CMake via `cmd /c`** wrapping `vcvars64.bat` and `cmake` in the same session:
  ```cmd
  cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64"
  ```
  Build command:
  ```cmd
  cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake --build build --config Release"
  ```
- Or run the prepared helper scripts from cmd.exe: `build.bat` or `build-tests.bat`.

---

## 3. CI Overview & Clang-Tidy Policy

- **CI Workflow**: `.github/workflows/windows-ci.yml`
  - `format-check` (Ubuntu): `clang-format 21.1.8` (`--dry-run --Werror`) on `src/**/*.cpp|*.h|*.hpp`.
  - `build` (Windows): Qt 6.10.2 + MSVC + Ninja, `ctest`, `windeployqt` into `build/deploy`.
  - `clang-tidy` (Windows): LLVM 21.1.8 with `-DENABLE_CLANG_TIDY=ON`.
  - `coverage`: runs under OpenCppCoverage via `scripts/CoverageOpenCpp.ps1`.
- **Clang-tidy policy**:
  - `bugprone-*` and `clang-analyzer-*` are treated as errors.
  - Excluded false positives: `clang-analyzer-cplusplus.NewDelete`, `bugprone-reserved-identifier`.
  - Avoid implicit narrowing (`qsizetype` → `int`, `double` ↔ `qint64`). Prefer `auto` / `qsizetype` for Qt container sizes.

---

## 4. UI Architecture: Qt Widgets

- **Frontend**: Classic Qt Widgets (`ui/*.ui` and `src/ui/*`).
- **Core Tabs**:
  - **Ручная разборка** (`ManualExtractionWidget`)
  - **Ручная сборка** (`ManualAssemblyWidget`)
  - **Ручной рендер** (`ManualRenderWidget`)
  - «Автоматический режим» и «Публикация» скрыты по умолчанию (включаются через меню `Вид`).
- **Логирование и управление**:
  - Общий `QSplitter` внизу окна с логом, прогресс-баром и кнопкой отмены для всех ручных операций.

---

## 5. Рендеринг и сборка релизов

- ТБ (титры благодарности) и Concat smart render полностью удалены.
- Рендеринг выполняется через стандартный пайплайн ffmpeg/HandBrake с полным кодированием или хардсабом.
- Сборка MKV поддерживает исходные MP4/MKV контейнеры без разделения на отдельные видео и аудио треки.

---

## 6. Testing & Coverage

- Unit tests use QtTest + CTest.
- Test helper library: `DubbingToolTestSupport` in `CMakeLists.txt`.
- Coverage tool: **OpenCppCoverage** on Windows (MSVC), wrapped by `scripts/CoverageOpenCpp.ps1` producing Cobertura XML.
- Full testing details in `.agents/rules/testing-coverage.md` and `docs/AgentHandoffTestsCoverage.md`.

---

## 7. Git Safety Rules

- **Never run `git stash drop`** unless explicitly requested by the user. Prefer `git stash apply`.
- If a stash was dropped accidentally and you have the commit hash:
  `git stash store -m "recovered" <commit>` recreates `refs/stash`.
- If you encounter broken index / unmerged paths without `.git/MERGE_HEAD`: after `git fetch`, `git reset --hard origin/<branch>` clears the stuck state (always warn the user first about unsaved changes).
