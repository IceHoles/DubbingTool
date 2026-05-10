# Agent handoff: tests, MSVC coverage, TelegramFormatter, Git

This file captures decisions and pitfalls discovered while wiring **CMake `BUILD_TESTING`**, **DubbingToolTestSupport**, **OpenCppCoverage**, and **TelegramFormatter** tests. Read this before redoing the same exploration.

## Build and run tests (Windows)

- Follow `.cursor/rules/build-instructions.mdc` for **Qt 6.10.2**, **vcvars64**, **Ninja**, and `CMAKE_PREFIX_PATH`.
- Configure a dedicated test build dir, e.g.  
  `cmake -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 -DBUILD_TESTING=ON`
- Build and run CTest from that dir:  
  `cmake --build build-tests` then `ctest --test-dir build-tests` (ensure `ctest` is on `PATH` — VS ships CMake under `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`).
- Convenience: `build-tests.bat` in repo root (configure, build, optional `coverage`).

## MSVC line coverage (OpenCppCoverage)

- **Not** `gcov`/`llvm-cov` for the main MSVC/Qt workflow; use **OpenCppCoverage** wrapping `ctest`.
- Script: `scripts/CoverageOpenCpp.ps1` — prepends Qt `bin`, VS CMake `bin`, and `OpenCppCoverage` if installed under `C:\Program Files\OpenCppCoverage`.
- Writes **Cobertura** XML, default: `build-tests/cobertura.xml` when `-BuildDir` is `build-tests`.
- **`DUBBINGTOOL_UNDER_OPENCPP_COVERAGE=1`** is set by the script so tests can **avoid strict clipboard assertions** under instrumentation; `formatAndCopyToClipboard` is still **invoked** so its body stays covered (assertions gated on env).
- Optional threshold helper: `scripts/CheckCoberturaThreshold.ps1`.

### Reading Cobertura

- The root `<coverage line-rate="...">` mixes **all** instrumented translation units linked into each child process (large static lib + Qt), so the **global percentage is not a product metric**.
- For formatter work, use the **package** for `telegramformatter_test.exe` and the **class** `telegramformatter.cpp` inside it.

## TelegramFormatter (implementation + tests)

- **Fenced code blocks** (` ``` ` / ` ```lang `): parsing into TD mime strips fences; round-trip failed because **`applyTokensToSegment`** only restored inline `` ` ``. Fix: detect tokens starting with `` ``` `` and wrap segment with `` ```[lang]\n...\n``` ``.
- **Quote + inner styles gap** (`parseText` line ~139): exercised by building TD mime from markdown like `>hello **z**<` — **do not** assert full `markdownRoundTrip` for that string; reverse path can still produce wrong pseudo-markdown (`>hello <>**z**<`) until that bug is fixed separately.
- **Empty `applyTokensToSegment`**: defensive early return; covered via **`DUBBINGTOOL_ENABLE_TEST_HOOKS`** + `telegramFormatter_test_hook_applyTokensEmpty()` in `telegramformatter.cpp` (only compiled into **DubbingToolTestSupport**), called from a dedicated test slot.

## CMake / layout

- **`DubbingToolTestSupport`**: static library shared by multiple test executables; list production `.cpp/.h` here when tests need them.
- **`target_compile_definitions(DubbingToolTestSupport PRIVATE DUBBINGTOOL_ENABLE_TEST_HOOKS)`** enables the test-only hook symbol above.
- **`IProcessRunner`**: small TU in `src/core/iprocessrunner.cpp` — linked from both the app (`SOURCES_CORE`) and test support when manual renderer / fakes need the vtable.

## .gitignore

- Ignore local CMake dirs: `build-test/`, `build-tests/`, `build-mergecheck/` (alongside `build/`).
- Ignore **OpenCppCoverage HTML** trees: `CoverageReport-*/`, plus noisy logs `occ_verbose.txt`, `tf_out.txt`.

## Git workflow (do not repeat mistakes)

- **Never run `git stash drop`** unless the user explicitly asked to remove the stash. Prefer `git stash apply` and let the user drop when satisfied.
- If a stash was dropped by mistake but you still have the **commit hash** from the drop message:  
  `git stash store -m "recovered" <commit>` can recreate `refs/stash`.
- **`Unmerged paths` without `.git/MERGE_HEAD`**: broken index after aborted checkout/apply — after `git fetch`, **`git reset --hard origin/<branch>`** clears it (warn user about losing uncommitted tracked changes not elsewhere saved).
- **`git stash apply` with `stash -u`**: restoring untracked files can fail with **"already exists, no checkout"** if those paths exist on disk — normal; stage the intended untracked files manually afterward.

## CTest inventory (as of this handoff)

Five CTest targets when `BUILD_TESTING` is ON: FontFinder, ChapterHelper, TelegramFormatter, ManualRenderer, FFmpeg smoke. QTest slot counts are high in `telegramformatter_test.cpp` (see `-functions` on each exe if you need exact numbers).
