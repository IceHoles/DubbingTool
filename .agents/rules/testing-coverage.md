---
trigger: model_decision
description: Guidelines for CTest, MSVC OpenCppCoverage, DubbingToolTestSupport, and TelegramFormatter test hooks
---

# Testing & Coverage Guide for DubbingTool

## Build and Run Tests (Windows)

- Configure a dedicated test build directory:
  ```cmd
  cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 -DBUILD_TESTING=ON"
  ```
- Build and run CTest:
  ```cmd
  cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake --build build-tests && ctest --test-dir build-tests"
  ```
- Convenience batch file: `build-tests.bat` in repo root.

## MSVC Line Coverage (OpenCppCoverage)

- Do **not** use `gcov`/`llvm-cov` for the MSVC/Qt workflow; use **OpenCppCoverage** wrapping `ctest`.
- Script: `scripts/CoverageOpenCpp.ps1` — prepends Qt `bin`, VS CMake `bin`, and OpenCppCoverage.
- Outputs Cobertura XML: `build-tests/cobertura.xml`.
- Env var **`DUBBINGTOOL_UNDER_OPENCPP_COVERAGE=1`** is set by the script so tests can skip strict clipboard assertions under instrumentation, while still executing `formatAndCopyToClipboard` for code coverage.
- Threshold check: `scripts/CheckCoberturaThreshold.ps1`.

### Reading Cobertura Output
- The root `<coverage line-rate="...">` combines all instrumented units linked into child processes (static lib + Qt), so the global percentage is not a standalone metric.
- For formatter work, inspect the package for `telegramformatter_test.exe` and the class `telegramformatter.cpp`.

## TelegramFormatter (Implementation & Tests)

- **Fenced code blocks** (`` ``` ``): TD mime strips fences. `applyTokensToSegment` detects tokens starting with `` ``` `` and wraps the segment with `` ```[lang]\n...\n``` ``.
- **Quote + inner styles**: TD mime from markdown like `>hello **z**<` — do not assert full roundtrip; reverse path can still produce `>hello <>**z**<`.
- **Empty `applyTokensToSegment`**: covered via `DUBBINGTOOL_ENABLE_TEST_HOOKS` + `telegramFormatter_test_hook_applyTokensEmpty()` compiled only into `DubbingToolTestSupport`.

## CMake Test Architecture

- **`DubbingToolTestSupport`**: static library shared by multiple test executables (`FontFinder`, `ChapterHelper`, `TelegramFormatter`, `ManualRenderer`, `FFmpeg smoke`).
- **`IProcessRunner`**: abstraction in `src/core/iprocessrunner.cpp` / `iprocessrunner.h` — allows `FakeProcessRunner` for unit tests without launching actual external processes.

## Git Safety

- **Never run `git stash drop`** unless the user explicitly requested it. Prefer `git stash apply`.
- Recover dropped stash by hash: `git stash store -m "recovered" <commit>`.
- Recover unmerged broken index without `MERGE_HEAD`: `git reset --hard origin/<branch>`.
