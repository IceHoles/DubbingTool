# Prints Cobertura line-rate and optionally fails below MinimumLinePercent (0-100).
# If MinimumLinePercent is omitted, COVERAGE_FAIL_UNDER_LINE env var is used when set.
param(
    [Parameter(Mandatory = $true)]
    [string]$CoberturaPath,
    [double]$MinimumLinePercent = -1.0
)

$ErrorActionPreference = "Stop"

if ($MinimumLinePercent -lt 0.0)
{
    $envMin = $env:COVERAGE_FAIL_UNDER_LINE
    if ($envMin -ne $null -and $envMin -ne "")
    {
        $MinimumLinePercent = [double]$envMin
    }
    else
    {
        $MinimumLinePercent = 0.0
    }
}

if (-not (Test-Path -LiteralPath $CoberturaPath))
{
    Write-Error "Missing Cobertura file: $CoberturaPath"
    exit 1
}

[xml]$doc = Get-Content -LiteralPath $CoberturaPath
$root = $doc.DocumentElement
$rateText = $root.GetAttribute("line-rate")
if ($rateText -eq "")
{
    Write-Error "Cobertura root has no line-rate attribute"
    exit 1
}

$rate = [double]$rateText
$pct = [math]::Round($rate * 100.0, 2)
Write-Host "Line coverage (cobertura): $pct% (minimum requested: $MinimumLinePercent%)"

if (($MinimumLinePercent -gt 0.0) -and ($pct + 0.0001 -lt $MinimumLinePercent))
{
    Write-Error "Line coverage $pct% is below minimum $MinimumLinePercent%"
    exit 1
}

exit 0
