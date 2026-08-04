param(
    [string] $OpenControlRoot,
    [string] $PinnedLibdepsRoot
)

$ErrorActionPreference = 'Stop'

$coreRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OpenControlRoot)) {
    $OpenControlRoot = Join-Path (Split-Path (Split-Path $coreRoot -Parent) -Parent) 'open-control'
}
$OpenControlRoot = (Resolve-Path -LiteralPath $OpenControlRoot).Path
$localNoteRoot = Join-Path $OpenControlRoot 'note'
$localFrameworkRoot = Join-Path $OpenControlRoot 'framework'
$fixture = Join-Path $coreRoot 'test\test_RealtimeMidiProducerEnvelope\test_main.cpp'

function Read-DeclaredPin([string] $DependencyName) {
    $lockText = Get-Content -LiteralPath (Join-Path $coreRoot 'oc-sdk.ini') -Raw
    $pattern = [regex]::Escape($DependencyName) + '=[^\r\n]*#([0-9a-fA-F]{40})'
    $match = [regex]::Match($lockText, $pattern)
    if (-not $match.Success) {
        throw "No commit pin found for $DependencyName in oc-sdk.ini"
    }
    return $match.Groups[1].Value.ToLowerInvariant()
}

function Find-PinnedLibdepsRoot(
    [string] $ExpectedNotePin,
    [string] $ExpectedFrameworkPin
) {
    if (-not [string]::IsNullOrWhiteSpace($PinnedLibdepsRoot)) {
        return (Resolve-Path -LiteralPath $PinnedLibdepsRoot).Path
    }
    $repositoryParent = Split-Path $coreRoot -Parent
    $candidates = @(
        (Join-Path $coreRoot '.pio\libdeps\native_ci'),
        (Join-Path $coreRoot '.pio\libdeps\dev'),
        (Join-Path $repositoryParent 'core\.pio\libdeps\native_ci'),
        (Join-Path $repositoryParent 'core\.pio\libdeps\dev')
    )
    foreach ($candidate in $candidates) {
        $candidateNote = Join-Path $candidate 'oc-note'
        $candidateFramework = Join-Path $candidate 'oc-framework'
        if (-not (Test-Path -LiteralPath $candidateNote) -or
            -not (Test-Path -LiteralPath $candidateFramework)) { continue }
        if ((Read-GitHead $candidateNote) -eq $ExpectedNotePin -and
            (Read-GitHead $candidateFramework) -eq $ExpectedFrameworkPin) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Exact declared oc-note/oc-framework cache not found; pass -PinnedLibdepsRoot'
}

function Read-GitHead([string] $Repository) {
    $head = (& git -C $Repository rev-parse HEAD).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot read Git HEAD for $Repository"
    }
    return $head
}

function Invoke-EnvelopeBuild(
    [string] $Label,
    [string] $NoteRoot,
    [string] $FrameworkRoot,
    [string] $Output
) {
    $arguments = @(
        '-std=c++20', '-O2', '-g0', '-Wl,--no-insert-timestamp',
        "-I$coreRoot\src",
        "-I$NoteRoot\src",
        "-I$FrameworkRoot\src",
        $fixture,
        (Join-Path $coreRoot 'src\sequencer\RealtimeMidiQueue.cpp'),
        (Join-Path $NoteRoot 'src\oc\note\sequencer\StepSequencerGraph.cpp'),
        (Join-Path $NoteRoot 'src\oc\note\sequencer\StepSequencerChord.cpp')
    )
    $chordSpecSource = Join-Path $NoteRoot 'src\oc\note\sequencer\StepSequencerChordSpec.cpp'
    if (Test-Path -LiteralPath $chordSpecSource) {
        $arguments += $chordSpecSource
    }
    $arguments += @(
        (Join-Path $NoteRoot 'src\oc\note\sequencer\StepSequencerExpander.cpp'),
        (Join-Path $FrameworkRoot 'src\oc\api\MidiAPI.cpp'),
        (Join-Path $FrameworkRoot 'src\oc\time\Time.cpp'),
        '-o',
        $Output
    )

    $timer = [Diagnostics.Stopwatch]::StartNew()
    & g++ @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label fixture compilation failed with exit code $LASTEXITCODE"
    }
    $timer.Stop()
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Output).Hash.ToLowerInvariant()
    Write-Output "$Label build_ms=$($timer.ElapsedMilliseconds) sha256=$hash"

    & $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Label fixture failed with exit code $LASTEXITCODE"
    }
}

$declaredNotePin = Read-DeclaredPin 'oc-note'
$declaredFrameworkPin = Read-DeclaredPin 'oc-framework'
$pinRoot = Find-PinnedLibdepsRoot $declaredNotePin $declaredFrameworkPin
$pinnedNoteRoot = Join-Path $pinRoot 'oc-note'
$pinnedFrameworkRoot = Join-Path $pinRoot 'oc-framework'
$actualNotePin = Read-GitHead $pinnedNoteRoot
$actualFrameworkPin = Read-GitHead $pinnedFrameworkRoot
if ($actualNotePin -ne $declaredNotePin) {
    throw "Cached oc-note $actualNotePin does not match declared pin $declaredNotePin"
}
if ($actualFrameworkPin -ne $declaredFrameworkPin) {
    throw "Cached oc-framework $actualFrameworkPin does not match declared pin $declaredFrameworkPin"
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
)
$tempRoot = Join-Path $tempBase ('ms-core-l-r02b-01-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null

try {
    Write-Output "compiler=$((& g++ --version | Select-Object -First 1))"
    Write-Output "core=$(Read-GitHead $coreRoot)"
    Write-Output "local-note=$(Read-GitHead $localNoteRoot)"
    Write-Output "local-framework=$(Read-GitHead $localFrameworkRoot)"
    Write-Output "pinned-note=$actualNotePin"
    Write-Output "pinned-framework=$actualFrameworkPin"

    Invoke-EnvelopeBuild `
        'local' `
        $localNoteRoot `
        $localFrameworkRoot `
        (Join-Path $tempRoot 'envelope-local.exe')
    Invoke-EnvelopeBuild `
        'pinned' `
        $pinnedNoteRoot `
        $pinnedFrameworkRoot `
        (Join-Path $tempRoot 'envelope-pinned.exe')
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $requiredPrefix = $tempBase + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTempRoot.StartsWith(
        $requiredPrefix,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Refusing to remove unexpected temporary path: $resolvedTempRoot"
    }
    if (Test-Path -LiteralPath $resolvedTempRoot) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force
    }
}
