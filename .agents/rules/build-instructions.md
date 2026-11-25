---
trigger: always_on
description: Build instructions for DubbingTool Qt project
---

# Build Instructions for DubbingTool

## Environment
- **Qt:** 6.10.2 MSVC 2022 64-bit at `C:\Qt\6.10.2\msvc2022_64`
- **Compiler:** VS 2026 Build Tools (MSVC 14.50) at `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`
- **vcvars:** `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
- **Build System:** CMake + Ninja

## CMake Build (Recommended)

### Configure
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64"
```

### Build
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake --build build --config Release"
```

### Output
- Executable: `build\DubbingTool.exe`
- `compile_commands.json` generated for clangd in `build/`

## Deploy Qt DLLs (windeployqt)
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe c:\Users\icehole\git\DubbingTool\build\DubbingTool.exe"
```

## Build with clang-tidy
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && cd /d \"c:\Users\icehole\git\DubbingTool\" && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 -DENABLE_CLANG_TIDY=ON && cmake --build build"
```

## PowerShell vs cmd (paths and quoting)
- **Recommended:** run `build.bat` or `build-tests.bat` from `cmd.exe` or `cmd /c build.bat` from PowerShell.
- **Why not raw CMake in PowerShell:** the segment `(x86)` in `Program Files (x86)` is parsed by PowerShell as a subexpression unless the path is in quotes. A broken pattern is `cmd /c "C:\Program Files (x86)\...` (unquoted `(x86)`).
- **Safe pattern in PowerShell:** call cmd with a single-quoted outer string:
  ```powershell
  cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cd /d c:\Users\icehole\git\DubbingTool && cmake --build build'
  ```
- **`build-tests.bat`:** sets `REPO_ROOT` from `%~dp0` and strips a trailing backslash so `"%REPO_ROOT%"` does not produce `\"` before the closing quote.
