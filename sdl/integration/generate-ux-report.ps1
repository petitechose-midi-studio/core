param(
    [string]$OutputRoot = ".captures/ux/workflows",
    [string]$WorkflowDir,
    [string]$ReportPath
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CoreRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
if (-not $WorkflowDir) {
    $WorkflowDir = Join-Path $ScriptDir "workflows"
}

$ResolvedOutputRoot = if ([System.IO.Path]::IsPathRooted($OutputRoot)) {
    [System.IO.Path]::GetFullPath($OutputRoot)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $CoreRoot $OutputRoot))
}
if (-not $ReportPath) {
    $ReportPath = Join-Path $ResolvedOutputRoot "report.md"
}
$ResolvedReportPath = if ([System.IO.Path]::IsPathRooted($ReportPath)) {
    [System.IO.Path]::GetFullPath($ReportPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $CoreRoot $ReportPath))
}

function ConvertTo-ReportPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Base
    )
    return [System.IO.Path]::GetRelativePath($Base, $Path).Replace("\", "/")
}

function Read-Ndjson {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }

    $Rows = @()
    foreach ($Line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($Line)) {
            continue
        }
        $Rows += ($Line | ConvertFrom-Json)
    }
    return $Rows
}

function Get-WorkflowDoc {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Title = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $PurposeLines = @()
    $Notes = @()
    foreach ($Line in Get-Content -LiteralPath $Path) {
        if ($Line -match "^\s*$") {
            if ($PurposeLines.Count -gt 0 -or $Notes.Count -gt 0) {
                break
            }
            continue
        }
        if ($Line -notmatch "^\s*#\s?(.*)$") {
            break
        }

        $Text = $Matches[1].Trim()
        if ($Text -match "^UX workflow:\s*(.+)$") {
            $Title = $Matches[1].Trim().TrimEnd(".")
            continue
        }
        if ($Text -match "^Purpose:\s*(.+)$") {
            $PurposeLines += $Matches[1].Trim()
            continue
        }
        if ($PurposeLines.Count -gt 0 -and $Text -notmatch "^[A-Z][A-Za-z_-]+:") {
            $PurposeLines += $Text
            continue
        }
        if ($Text.Length -gt 0) {
            $Notes += $Text
        }
    }

    return [pscustomobject]@{
        Title = $Title
        Purpose = ($PurposeLines -join " ").Trim()
        Notes = $Notes
    }
}

function Get-CaptureDeclarations {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Captures = @()
    $LineNumber = 0
    foreach ($Line in Get-Content -LiteralPath $Path) {
        $LineNumber += 1
        $Clean = ($Line -replace "\s+#.*$", "") -replace "\s+//.*$", ""
        if ($Clean -match "^\s*(\d+)\s+capture\s+(screen|controller)\s+([A-Za-z0-9_-]+)\s*$") {
            $Captures += [pscustomobject]@{
                DueMs = [int]$Matches[1]
                Scope = $Matches[2]
                Name = $Matches[3]
                Line = $LineNumber
            }
        }
    }
    return $Captures
}

function Get-BmpInfo {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{
            Exists = $false
            Width = 0
            Height = 0
            BitsPerPixel = 0
            Bytes = 0
            SampleUniquePixels = 0
            NonEmpty = $false
        }
    }

    $Bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($Bytes.Length -lt 54 -or [char]$Bytes[0] -ne "B" -or [char]$Bytes[1] -ne "M") {
        return [pscustomobject]@{
            Exists = $true
            Width = 0
            Height = 0
            BitsPerPixel = 0
            Bytes = $Bytes.Length
            SampleUniquePixels = 0
            NonEmpty = $false
        }
    }

    $DataOffset = [BitConverter]::ToInt32($Bytes, 10)
    $Width = [BitConverter]::ToInt32($Bytes, 18)
    $SignedHeight = [BitConverter]::ToInt32($Bytes, 22)
    $Height = [Math]::Abs($SignedHeight)
    $BitsPerPixel = [BitConverter]::ToInt16($Bytes, 28)
    $BytesPerPixel = [Math]::Max(1, [int]($BitsPerPixel / 8))
    $Unique = [System.Collections.Generic.HashSet[string]]::new()

    if ($Width -gt 0 -and $Height -gt 0 -and ($BitsPerPixel -eq 24 -or $BitsPerPixel -eq 32)) {
        $RowStride = [int]([Math]::Floor((($BitsPerPixel * $Width + 31) / 32)) * 4)
        $StepX = [Math]::Max(1, [int][Math]::Ceiling($Width / 64.0))
        $StepY = [Math]::Max(1, [int][Math]::Ceiling($Height / 64.0))
        for ($Y = 0; $Y -lt $Height; $Y += $StepY) {
            $Row = $DataOffset + ($Y * $RowStride)
            for ($X = 0; $X -lt $Width; $X += $StepX) {
                $Index = $Row + ($X * $BytesPerPixel)
                if ($Index + 2 -ge $Bytes.Length) {
                    continue
                }
                $PixelKey = "{0:X2}{1:X2}{2:X2}" -f @(
                    [int]$Bytes[$Index],
                    [int]$Bytes[$Index + 1],
                    [int]$Bytes[$Index + 2]
                )
                [void]$Unique.Add($PixelKey)
            }
        }
    } else {
        $Step = [Math]::Max(1, [int][Math]::Ceiling($Bytes.Length / 4096.0))
        for ($I = 0; $I -lt $Bytes.Length; $I += $Step) {
            [void]$Unique.Add(("{0:X2}" -f [int]$Bytes[$I]))
        }
    }

    return [pscustomobject]@{
        Exists = $true
        Width = $Width
        Height = $Height
        BitsPerPixel = $BitsPerPixel
        Bytes = $Bytes.Length
        Sha256 = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($Bytes)).Substring(0, 12).ToLowerInvariant()
        SampleUniquePixels = $Unique.Count
        NonEmpty = $Unique.Count -gt 1
    }
}

function Get-TraceSummary {
    param(
        [Parameter(Mandatory = $true)][object[]]$TraceRows,
        [Parameter(Mandatory = $true)][object[]]$BindingRows
    )

    $Actions = @($TraceRows | Where-Object { $_.event -eq "action" })
    $RunEnd = @($TraceRows | Where-Object { $_.event -eq "run_end" } | Select-Object -First 1)
    $Drifts = @($Actions | Where-Object { $null -ne $_.drift_ms } | ForEach-Object { [int]$_.drift_ms })
    $MaxDrift = 0
    if ($Drifts.Count -gt 0) {
        $MaxDrift = ($Drifts | Measure-Object -Maximum).Maximum
    }

    $Dispatches = @($BindingRows | Where-Object { $_.stage -eq "dispatch" })
    $NoDispatches = @($BindingRows | Where-Object { $_.stage -eq "no_dispatch" })
    $Inputs = @($Actions | Where-Object {
            $_.action -eq "button" -or $_.action -eq "encoder"
        } | ForEach-Object {
            if ($_.action -eq "encoder") {
                "encoder:$($_.id)"
            } else {
                "button:$($_.id):$($_.value)"
            }
        } | Sort-Object -Unique)

    return [pscustomobject]@{
        ActionCount = $Actions.Count
        CaptureCount = @($Actions | Where-Object { $_.action -eq "capture" }).Count
        RunEnd = $RunEnd.Count -gt 0
        ActualMs = if ($RunEnd.Count -gt 0) { [int]$RunEnd[0].actual_ms } else { 0 }
        MaxDriftMs = $MaxDrift
        DispatchCount = $Dispatches.Count
        NoDispatchCount = $NoDispatches.Count
        Inputs = $Inputs
    }
}

if (-not (Test-Path -LiteralPath $WorkflowDir)) {
    throw "Workflow directory not found: $WorkflowDir"
}
if (-not (Test-Path -LiteralPath $ResolvedOutputRoot)) {
    throw "Workflow output directory not found: $ResolvedOutputRoot"
}

$Workflows = Get-ChildItem -LiteralPath $WorkflowDir -Filter "*.ux" | Sort-Object Name
if ($Workflows.Count -eq 0) {
    throw "No UX workflows found in $WorkflowDir"
}

$ReportDir = Split-Path -Parent $ResolvedReportPath
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

$Lines = @()
$Lines += "# UX Workflow Report"
$Lines += ""
$Lines += "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
$Lines += ""
$WorkflowDirRel = ConvertTo-ReportPath -Path $WorkflowDir -Base $CoreRoot
$OutputRootRel = ConvertTo-ReportPath -Path $ResolvedOutputRoot -Base $CoreRoot
$Lines += "Source scripts: $WorkflowDirRel"
$Lines += ""
$Lines += "Output root: $OutputRootRel"
$Lines += ""
$Lines += "This report is derived from UX scripts, replay traces, binding traces, and BMP captures. It intentionally avoids a second workflow manifest."
$Lines += ""
$Lines += "## Summary"
$Lines += ""
$Lines += "| Workflow | Result | Captures | Dispatches | Max Drift | Duration |"
$Lines += "|---|---:|---:|---:|---:|---:|"

$WorkflowSections = @()
foreach ($Workflow in $Workflows) {
    $Name = $Workflow.BaseName
    $OutDir = Join-Path $ResolvedOutputRoot $Name
    $TracePath = Join-Path $OutDir "trace.ndjson"
    $BindingTracePath = Join-Path $OutDir "binding-trace.ndjson"
    $TraceRows = @(Read-Ndjson -Path $TracePath)
    $BindingRows = @(Read-Ndjson -Path $BindingTracePath)
    $Doc = Get-WorkflowDoc -Path $Workflow.FullName
    $DeclaredCaptures = @(Get-CaptureDeclarations -Path $Workflow.FullName)
    $Summary = Get-TraceSummary -TraceRows $TraceRows -BindingRows $BindingRows
    $CaptureRows = @($TraceRows | Where-Object { $_.event -eq "action" -and $_.action -eq "capture" })

    $CaptureOk = $CaptureRows.Count -ge $DeclaredCaptures.Count
    $Result = if ($Summary.RunEnd -and $Summary.DispatchCount -gt 0 -and $CaptureOk) { "OK" } else { "CHECK" }
    $Lines += "| $Name | $Result | $($CaptureRows.Count)/$($DeclaredCaptures.Count) | $($Summary.DispatchCount) | $($Summary.MaxDriftMs)ms | $($Summary.ActualMs)ms |"

    $Section = @()
    $Section += "## $Name"
    $Section += ""
    $Section += "**Intent:** $($Doc.Title)"
    if ($Doc.Purpose) {
        $Section += ""
        $Section += "**Purpose:** $($Doc.Purpose)"
    }
    $Section += ""
    $ScriptRel = ConvertTo-ReportPath -Path $Workflow.FullName -Base $CoreRoot
    $OutRel = ConvertTo-ReportPath -Path $OutDir -Base $CoreRoot
    $Section += "- Script: $ScriptRel"
    $Section += "- Output: $OutRel"
    $Section += "- Actions: $($Summary.ActionCount)"
    $Section += "- Inputs: $($Summary.Inputs -join ', ')"
    $Section += "- Binding dispatches: $($Summary.DispatchCount)"
    $Section += "- No-dispatch rows: $($Summary.NoDispatchCount)"
    $Section += "- Max timing drift: $($Summary.MaxDriftMs)ms"
    $Section += ""
    $Section += "### Capture Timeline"
    $Section += ""
    $Section += "| Due | Name | Scope | Image | Dimensions | Sample Colors | Hash | Bytes |"
    $Section += "|---:|---|---|---|---:|---:|---|---:|"

    $VisualRows = @()

    foreach ($Capture in $CaptureRows) {
        $CapturePath = $Capture.capture
        if (-not [System.IO.Path]::IsPathRooted($CapturePath)) {
            $CapturePath = [System.IO.Path]::GetFullPath((Join-Path $CoreRoot $CapturePath))
        }
        $Bmp = Get-BmpInfo -Path $CapturePath
        $RelImage = if ($Bmp.Exists) { ConvertTo-ReportPath -Path $CapturePath -Base $ReportDir } else { "" }
        $ImageName = [System.IO.Path]::GetFileName($CapturePath)
        $ImageCell = if ($Bmp.Exists) { "[$ImageName]($RelImage)" } else { "missing" }
        $Dimensions = if ($Bmp.Exists) { "$($Bmp.Width)x$($Bmp.Height)x$($Bmp.BitsPerPixel)" } else { "-" }
        $Bytes = if ($Bmp.Exists) { $Bmp.Bytes } else { 0 }
        $Hash = if ($Bmp.Exists) { $Bmp.Sha256 } else { "-" }
        $Section += "| $($Capture.due_ms)ms | $($Capture.id) | $($Capture.scope) | $ImageCell | $Dimensions | $($Bmp.SampleUniquePixels) | $Hash | $Bytes |"
        if ($Bmp.Exists) {
            $VisualRows += [pscustomobject]@{
                DueMs = $Capture.due_ms
                Id = $Capture.id
                RelImage = $RelImage
                ImageName = $ImageName
                Dimensions = $Dimensions
                SampleUniquePixels = $Bmp.SampleUniquePixels
                Hash = $Hash
            }
        }
    }

    if ($VisualRows.Count -gt 0) {
        $Section += ""
        $Section += "### Visual Sequence"
        foreach ($Visual in $VisualRows) {
            $Section += ""
            $Section += "#### $($Visual.DueMs)ms - $($Visual.Id)"
            $Section += ""
            $Section += ("![{0}]({1})" -f $Visual.ImageName, $Visual.RelImage)
            $Section += ""
            $Section += "Dimensions: $($Visual.Dimensions) | sample colors: $($Visual.SampleUniquePixels) | hash: $($Visual.Hash)"
        }
    }

    $Section += ""
    $Section += "### Replay Trace"
    $Section += ""
    $Section += "| Due | Actual | Drift | Action | Id | Value |"
    $Section += "|---:|---:|---:|---|---|---|"
    foreach ($Action in @($TraceRows | Where-Object { $_.event -eq "action" })) {
        $Section += "| $($Action.due_ms)ms | $($Action.actual_ms)ms | $($Action.drift_ms)ms | $($Action.action) | $($Action.id) | $($Action.value) |"
    }

    $WorkflowSections += ,($Section -join "`n")
}

$Lines += ""
$Lines += "## Workflows"
$Lines += ""
$Lines += ($WorkflowSections -join "`n`n")

Set-Content -LiteralPath $ResolvedReportPath -Value ($Lines -join "`n") -Encoding utf8
Write-Host "UX report written: $ResolvedReportPath"
