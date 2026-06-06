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

function Get-WorkflowExpectations {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Expectations = @()
    foreach ($Match in Select-String -LiteralPath $Path -Pattern "^\s*#\s*Expect:\s*(.+)$") {
        foreach ($Expectation in ($Match.Matches[0].Groups[1].Value -split ",")) {
            $Text = $Expectation.Trim().ToLowerInvariant()
            if ($Text.Length -gt 0) {
                $Expectations += $Text
            }
        }
    }
    return @($Expectations | Sort-Object -Unique)
}

function Test-PlayheadProgress {
    param([Parameter(Mandatory = $true)][string]$TracePath)

    if (-not (Test-Path -LiteralPath $TracePath)) {
        return $false
    }

    $Rows = @()
    foreach ($Line in Get-Content -LiteralPath $TracePath) {
        if (-not [string]::IsNullOrWhiteSpace($Line)) {
            $Rows += ($Line | ConvertFrom-Json)
        }
    }

    $DistinctPlayingSteps = @($Rows |
        Where-Object { $_.event -eq "action" -and $_.playing -eq $true -and $null -ne $_.playhead_step } |
        ForEach-Object { [int]$_.playhead_step } |
        Where-Object { $_ -ge 0 } |
        Sort-Object -Unique)
    return $DistinctPlayingSteps.Count -gt 1
}

function Get-CaptureForLabel {
    param(
        [Parameter(Mandatory = $true)][string]$OutDir,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $Matches = @(Get-ChildItem -LiteralPath $OutDir -Filter "*_${Label}_screen.bmp" -ErrorAction SilentlyContinue)
    if ($Matches.Count -ne 1) {
        return $null
    }
    return $Matches[0].FullName
}

function Test-CaptureMatch {
    param(
        [Parameter(Mandatory = $true)][string]$OutDir,
        [Parameter(Mandatory = $true)][string]$LeftLabel,
        [Parameter(Mandatory = $true)][string]$RightLabel
    )

    $LeftCapture = Get-CaptureForLabel -OutDir $OutDir -Label $LeftLabel
    $RightCapture = Get-CaptureForLabel -OutDir $OutDir -Label $RightLabel
    if (-not $LeftCapture -or -not $RightCapture) {
        return $false
    }

    $LeftHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $LeftCapture).Hash
    $RightHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $RightCapture).Hash
    return $LeftHash -eq $RightHash
}

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
    $Expectations = @(Get-WorkflowExpectations -Path $Workflow.FullName)
    $ExpectationFailures = @()

    $RunEnded = (Test-Path -LiteralPath $TracePath) -and
        [bool](Select-String -LiteralPath $TracePath -Pattern '"event":"run_end"' -Quiet)
    $HasDispatch = (Test-Path -LiteralPath $BindingTracePath) -and
        [bool](Select-String -LiteralPath $BindingTracePath -Pattern '"stage":"dispatch"' -Quiet)

    if ($Expectations -contains "playhead_progress") {
        if (-not (Test-PlayheadProgress -TracePath $TracePath)) {
            $ExpectationFailures += "playhead_progress"
        }
    }

    foreach ($Expectation in $Expectations) {
        if ($Expectation -notlike "capture_match:*") {
            continue
        }

        $MatchSpec = $Expectation.Substring("capture_match:".Length)
        $Labels = @($MatchSpec -split "=", 2)
        if ($Labels.Count -ne 2 -or $Labels[0].Length -eq 0 -or $Labels[1].Length -eq 0) {
            $ExpectationFailures += $Expectation
            continue
        }

        if (-not (Test-CaptureMatch -OutDir $ResolvedOutDir -LeftLabel $Labels[0] -RightLabel $Labels[1])) {
            $ExpectationFailures += $Expectation
        }
    }

    $Ok = ($ExitCode -eq 0) -and
        (Test-Path -LiteralPath $TracePath) -and
        (Test-Path -LiteralPath $BindingTracePath) -and
        $RunEnded -and
        $HasDispatch -and
        ($CaptureCount -ge $ExpectedCaptureCount) -and
        ($ExpectationFailures.Count -eq 0)

    if ($Ok) {
        $ExpectationText = if ($Expectations.Count -gt 0) { " expects=" + ($Expectations -join ",") } else { "" }
        Write-Host ("OK   {0} captures={1}/{2}{3}" -f $Workflow.Name, $CaptureCount, $ExpectedCaptureCount, $ExpectationText)
    } else {
        $Failures += ("FAIL {0} exit={1} captures={2}/{3} run_end={4} dispatch={5} expectation_failures={6}" -f
            $Workflow.Name, $ExitCode, $CaptureCount, $ExpectedCaptureCount, $RunEnded, $HasDispatch, ($ExpectationFailures -join ","))
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
