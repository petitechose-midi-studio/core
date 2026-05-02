param(
    [string]$Exe,
    [string]$OutputRoot = ".captures/ux/workflows",
    [switch]$SkipBuild,
    [switch]$Report
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CoreRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$Python = if ($env:PYTHON) { $env:PYTHON } else { "python" }

$Args = @("script/ux_workflows.py", "run", "--output-root", $OutputRoot)
if ($Exe) {
    $Args += @("--exe", $Exe)
}
if ($SkipBuild) {
    $Args += "--skip-build"
}
if ($Report) {
    $Args += "--report"
}

Push-Location $CoreRoot
try {
    & $Python @Args
    $ExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($ExitCode -ne 0) {
    exit $ExitCode
}
