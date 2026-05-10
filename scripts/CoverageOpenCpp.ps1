# Runs OpenCppCoverage around ctest; writes Cobertura XML under the build directory.
# Prerequisite: OpenCppCoverage on PATH (e.g. choco install opencppcoverage -y).
param(
    [string]$RepoRoot = "",
    [string]$BuildDir = "",
    [string]$OutXml = ""
)

$ErrorActionPreference = "Stop"

if ($RepoRoot -eq "")
{
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

if ($BuildDir -eq "")
{
    $BuildDir = Join-Path $RepoRoot "build"
}

if ($OutXml -eq "")
{
    $OutXml = Join-Path $BuildDir "cobertura.xml"
}

if (($null -eq $env:Qt6_DIR) -or ($env:Qt6_DIR -eq ""))
{
    $defaultQtRoot = "C:\Qt\6.10.2\msvc2022_64"
    if (Test-Path $defaultQtRoot)
    {
        $env:Qt6_DIR = $defaultQtRoot
    }
}

if (($null -ne $env:Qt6_DIR) -and ($env:Qt6_DIR -ne ""))
{
    $qtBin = Join-Path $env:Qt6_DIR "bin"
    $env:PATH = "$qtBin;$env:PATH"
}

# When this script is launched from a tool that does not inherit MSVC CMake PATH (ctest),
# prepend the Build Tools embedded CMake bin if present.
$vsCmakeBin = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if (Test-Path (Join-Path $vsCmakeBin "ctest.exe"))
{
    $env:PATH = "$vsCmakeBin;$env:PATH"
}

$openCppInstall = Join-Path ${env:ProgramFiles} "OpenCppCoverage"
if (Test-Path (Join-Path $openCppInstall "OpenCppCoverage.exe"))
{
    $env:PATH = "$openCppInstall;$env:PATH"
}

$sourcesSrc = Join-Path $RepoRoot "src"
$sourcesTests = Join-Path $RepoRoot "tests"

function resolveCommandPath($cmd)
{
    if ($cmd -eq $null)
    {
        return ""
    }

    $p = $cmd.Path
    if (-not [string]::IsNullOrWhiteSpace($p))
    {
        return $p
    }

    $d = $cmd.Definition
    if (-not [string]::IsNullOrWhiteSpace($d))
    {
        return $d
    }

    $s = $cmd.Source
    if (-not [string]::IsNullOrWhiteSpace($s))
    {
        return $s
    }

    return ""
}

$openCpp = Get-Command "OpenCppCoverage.exe" -ErrorAction SilentlyContinue
$openCppPath = resolveCommandPath $openCpp
if ([string]::IsNullOrWhiteSpace($openCppPath))
{
    Write-Error "OpenCppCoverage.exe not found on PATH. Install: choco install opencppcoverage -y"
    exit 1
}

$ctest = Get-Command "ctest.exe" -ErrorAction SilentlyContinue
$ctestPath = resolveCommandPath $ctest
if ([string]::IsNullOrWhiteSpace($ctestPath))
{
    Write-Error "ctest.exe not found on PATH"
    exit 1
}

# Clipboard-dependent tests may fail inside OpenCppCoverage; telegramformatter_test skips those when set.
$env:DUBBINGTOOL_UNDER_OPENCPP_COVERAGE = "1"

$cppCoverageArgs = @(
    "--sources", $sourcesSrc,
    "--sources", $sourcesTests,
    "--export_type", "cobertura:$OutXml",
    "--cover_children",
    "--continue_after_cpp_exception",
    "--",
    $ctestPath,
    "--test-dir", $BuildDir,
    "--output-on-failure"
)

& $openCppPath @cppCoverageArgs
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0)
{
    exit $exitCode
}

Write-Host "Wrote $OutXml"
