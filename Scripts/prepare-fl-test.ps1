<#
    Creates the FL Studio test folder and fills it with deterministic sources.

    The sources are rendered by the existing export tool rather than copied from
    anywhere: they are synthesised from source code, so the repository carries
    no audio and the same build always produces the same bytes. The offline
    reference renders come from the same tool, so what is heard in FL Studio can
    be compared against what the offline chain produced from identical input.

    Usage:
        .\Scripts\prepare-fl-test.ps1
        .\Scripts\prepare-fl-test.ps1 -Output D:\AuroraVerbFlTest -SampleRate 48000
#>

[CmdletBinding()]
param(
    [string] $Output = "",
    [double] $SampleRate = 48000,
    [string] $BuildDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Output) {
    $Output = Join-Path $repoRoot "AuroraVerbFlTest"
}

$folders = @(
    (Join-Path $Output "Sources"),
    (Join-Path $Output "OfflineReference"),
    (Join-Path $Output "FlExports"),
    (Join-Path $Output "Diagnostics")
)

foreach ($folder in $folders) {
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
}

Write-Host "Test folder: $Output"

# --- Render the sources -----------------------------------------------------

$exporter = Join-Path $PSScriptRoot "export-signals.ps1"

if (-not (Test-Path -LiteralPath $exporter)) {
    throw "export-signals.ps1 not found next to this script."
}

$staging = Join-Path $Output "_staging"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Write-Host "Rendering deterministic sources at $SampleRate Hz..."

& $exporter -Output $staging -SampleRate $SampleRate -BuildDir $BuildDir | Out-Null

# Dry files are the sources to play into the plugin; wet files are the offline
# reference to compare the host's output against.
#
# Join-Path in Windows PowerShell 5.1 takes exactly two path arguments; a third
# positional one is not a deeper path, it is bound to a different parameter and
# fails. Nested calls, not a three-argument call.
$sourcesDir   = Join-Path $Output "Sources"
$referenceDir = Join-Path $Output "OfflineReference"

Get-ChildItem -LiteralPath $staging -Filter "*-dry.wav" | ForEach-Object {
    Move-Item -LiteralPath $_.FullName -Destination (Join-Path $sourcesDir $_.Name) -Force
}

Get-ChildItem -LiteralPath $staging -Filter "*-wet-*.wav" | ForEach-Object {
    Move-Item -LiteralPath $_.FullName -Destination (Join-Path $referenceDir $_.Name) -Force
}

Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue

$sourceCount = (Get-ChildItem -LiteralPath $sourcesDir -Filter *.wav).Count
$referenceCount = (Get-ChildItem -LiteralPath $referenceDir -Filter *.wav).Count

Write-Host "  Sources:           $sourceCount"
Write-Host "  Offline reference: $referenceCount"

# --- Copy the checklist ------------------------------------------------------

$checklistSource = Join-Path $repoRoot "Docs\FlStudioTestChecklist.md"
$checklistTarget = Join-Path $Output "TestChecklist.md"

if (Test-Path -LiteralPath $checklistSource) {
    Copy-Item -LiteralPath $checklistSource -Destination $checklistTarget -Force
    Write-Host "  Checklist:         $checklistTarget"
} else {
    Write-Host "  Checklist source not found at $checklistSource" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Ready. Next:"
Write-Host "  .\Scripts\start-fl-diagnostic.ps1"
