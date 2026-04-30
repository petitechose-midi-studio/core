param(
    [string]$Exe,
    [string]$OutputRoot = ".captures/ux/workflows",
    [switch]$SkipBuild,
    [switch]$Report
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CoreRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$WorkflowDir = Join-Path $ScriptDir "workflows"

if (-not $Exe) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $CoreRoot "..\..")).Path
    $Exe = Join-Path $WorkspaceRoot "bin\core\native\midi_studio_core.exe"
}

if (-not $SkipBuild) {
    Push-Location $CoreRoot
    try {
        ms build core --target native
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Native executable not found: $Exe"
}

$ResolvedOutputRoot = [System.IO.Path]::GetFullPath((Join-Path $CoreRoot $OutputRoot))
New-Item -ItemType Directory -Force -Path $ResolvedOutputRoot | Out-Null

$Workflows = Get-ChildItem -LiteralPath $WorkflowDir -Filter "*.ux" | Sort-Object Name
if ($Workflows.Count -eq 0) {
    throw "No UX workflow scripts found in $WorkflowDir"
}

$Failures = @()

foreach ($Workflow in $Workflows) {
    $OutDir = Join-Path $ResolvedOutputRoot $Workflow.BaseName
    $ResolvedOutDir = [System.IO.Path]::GetFullPath($OutDir)
    if (-not $ResolvedOutDir.StartsWith($ResolvedOutputRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to write outside output root: $ResolvedOutDir"
    }

    if (Test-Path -LiteralPath $ResolvedOutDir) {
        Remove-Item -LiteralPath $ResolvedOutDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $ResolvedOutDir | Out-Null

    Push-Location $CoreRoot
    try {
        & $Exe --ux-script $Workflow.FullName --ux-output $ResolvedOutDir
        $ExitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    $TracePath = Join-Path $ResolvedOutDir "trace.ndjson"
    $BindingTracePath = Join-Path $ResolvedOutDir "binding-trace.ndjson"
    $CaptureCount = @(Get-ChildItem -LiteralPath $ResolvedOutDir -Filter "*.bmp" -ErrorAction SilentlyContinue).Count
    $ExpectedCaptureCount = @(Select-String -LiteralPath $Workflow.FullName -Pattern "^\s*\d+\s+capture\s+" -CaseSensitive:$false).Count

    $RunEnded = (Test-Path -LiteralPath $TracePath) -and
        [bool](Select-String -LiteralPath $TracePath -Pattern '"event":"run_end"' -Quiet)
    $HasDispatch = (Test-Path -LiteralPath $BindingTracePath) -and
        [bool](Select-String -LiteralPath $BindingTracePath -Pattern '"stage":"dispatch"' -Quiet)

    $Ok = ($ExitCode -eq 0) -and
        (Test-Path -LiteralPath $TracePath) -and
        (Test-Path -LiteralPath $BindingTracePath) -and
        $RunEnded -and
        $HasDispatch -and
        ($CaptureCount -ge $ExpectedCaptureCount)

    if ($Ok) {
        Write-Host ("OK   {0} captures={1}/{2}" -f $Workflow.Name, $CaptureCount, $ExpectedCaptureCount)
    } else {
        $Failures += ("FAIL {0} exit={1} captures={2}/{3} run_end={4} dispatch={5}" -f
            $Workflow.Name, $ExitCode, $CaptureCount, $ExpectedCaptureCount, $RunEnded, $HasDispatch)
    }
}

if ($Failures.Count -gt 0) {
    $Failures | ForEach-Object { Write-Error $_ }
    throw ("UX workflow verification failed: {0}/{1}" -f $Failures.Count, $Workflows.Count)
}

Write-Host ("UX workflow verification OK: {0}/{0}" -f $Workflows.Count)

if ($Report) {
    & (Join-Path $ScriptDir "generate-ux-report.ps1") -OutputRoot $OutputRoot -WorkflowDir $WorkflowDir
}
