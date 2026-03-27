[CmdletBinding()]
param(
    [string]$DownstreamProjectPath,
    [string]$Environment = "dev"
)

$ErrorActionPreference = "Stop"

$coreRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path

if ([string]::IsNullOrWhiteSpace($DownstreamProjectPath)) {
    $DownstreamProjectPath = Join-Path $coreRoot "..\\plugin-bitwig"
}

try {
    $resolvedDownstreamPath = (Resolve-Path $DownstreamProjectPath).Path
} catch {
    throw "Downstream project path not found: $DownstreamProjectPath"
}

$platformioConfig = Join-Path $resolvedDownstreamPath "platformio.ini"
if (-not (Test-Path $platformioConfig)) {
    throw "No platformio.ini found in downstream project: $resolvedDownstreamPath"
}

Write-Host "[compat] Core root: $coreRoot"
Write-Host "[compat] Downstream project: $resolvedDownstreamPath"
Write-Host "[compat] Environment: $Environment"
Write-Host "[compat] Running downstream build against current exported core headers..."

Push-Location $resolvedDownstreamPath
try {
    & pio run -e $Environment
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

Write-Host "[compat] Downstream compatibility check passed."
