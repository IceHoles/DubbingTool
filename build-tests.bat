@echo off
REM Configure and build unit tests (Debug, Ninja). Optional: run ctest or OpenCppCoverage.
setlocal

if /i "%~1"=="help" (
    echo Usage:
    echo   build-tests.bat            CMake configure, build tests, run ctest
    echo   build-tests.bat notest     Configure and build only ^(no ctest^)
    echo   build-tests.bat coverage   Build then OpenCppCoverage ^(needs choco install opencppcoverage^)
    echo.
    echo Qt: set QT_PREFIX if not using default C:\Qt\6.10.2\msvc2022_64
    echo Gate: set COVERAGE_FAIL_UNDER_LINE for CheckCoberturaThreshold.ps1 ^(coverage mode^)
    exit /b 0
)

set "REPO_ROOT=%~dp0"
REM Strip trailing backslash so quoted paths like "%REPO_ROOT%" do not escape the closing quote in cmd.exe.
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"
cd /d "%REPO_ROOT%"

if "%QT_PREFIX%"=="" set "QT_PREFIX=C:\Qt\6.10.2\msvc2022_64"
set "BUILD_DIR=%REPO_ROOT%\build-tests"
set "BUILD_TYPE=Debug"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo vcvars64.bat failed. Adjust path to Visual Studio Build Tools.
    exit /b 1
)

cmake -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH=%QT_PREFIX% ^
    -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE%
if errorlevel 1 exit /b 1

set "PATH=%QT_PREFIX%\bin;%PATH%"

if /i "%~1"=="coverage" (
    REM Prefer Windows PowerShell 5.1; pwsh is optional and often missing on dev PCs.
    powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\scripts\CoverageOpenCpp.ps1" -RepoRoot "%REPO_ROOT%" -BuildDir "%BUILD_DIR%"
    if errorlevel 1 exit /b 1
    powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\scripts\CheckCoberturaThreshold.ps1" -CoberturaPath "%BUILD_DIR%\cobertura.xml"
    if errorlevel 1 exit /b 1
    echo Cobertura: "%BUILD_DIR%\cobertura.xml"
    exit /b 0
)

if /i "%~1"=="notest" (
    echo Build finished ^(notest^): "%BUILD_DIR%"
    exit /b 0
)

ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 exit /b 1

exit /b 0
