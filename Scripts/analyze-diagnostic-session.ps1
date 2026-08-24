<#
    Runs the offline analysis over a diagnostic session folder.

    The heavy measurements - RT60, band decay, modal tracking, true peak,
    third-octave cancellation - are the ones PHASE 3 to PHASE 5C already built,
    called through DiagnosticAnalyzer in the test DLL. There is no second copy
    of any formula.

    Exit codes:
        0  PASS
        1  PASS WITH WARNINGS
        2  FAIL
        3  session missing or invalid

    Usage:
        .\Scripts\analyze-diagnostic-session.ps1
        .\Scripts\analyze-diagnostic-session.ps1 -Session "C:\...\2026-08-05_12-00-00_abc12345"
#>

[CmdletBinding()]
param(
    [string] $Session = "",
    [string] $Root = "",
    [string] $BuildDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

# --- Locate the session -----------------------------------------------------

if ($Session) {
    # An explicit path is an instruction, not a hint. None of the selection
    # logic runs, so passing -Session is always a way round a wrong guess.
    Write-Host "Session: $Session"
    Write-Host "  chosen by: the -Session argument"
} else {
    . (Join-Path $PSScriptRoot "Select-DiagnosticSession.ps1")

    if (-not $Root) {
        $base = if ($env:AV_DIAGNOSTIC_ROOT) { $env:AV_DIAGNOSTIC_ROOT }
                elseif ($env:USERPROFILE)    { $env:USERPROFILE }
                else                         { "." }

        $Root = Join-Path $base "AuroraVerbDiagnostics"
    }

    $choice = Select-DiagnosticSession -Root $Root

    if ($choice.Status -eq 'none') {
        Write-Host $choice.Reason -ForegroundColor Red
        exit 3
    }

    if ($choice.Status -eq 'ambiguous') {
        Write-Host "Cannot tell which session you mean: $($choice.Reason)" -ForegroundColor Red
        Write-Host ""

        if ($choice.Growing.Count -gt 0) {
            Write-Host "Being written right now:"
            $choice.Growing | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host "Live but idle:"
            $choice.Live | ForEach-Object { Write-Host "  $_" }
        }

        Write-Host ""
        Write-Host "Re-run with an explicit path, for example:"
        Write-Host "  .\Scripts\analyze-diagnostic-session.ps1 -Session `"$(Join-Path $Root ($choice.Live | Select-Object -First 1))`""
        exit 3
    }

    $Session = $choice.Path

    # Printed before anything is analysed, so a wrong choice is visible while
    # it can still be stopped rather than after a report has been written.
    Write-Host "Session: $Session"
    Write-Host "  chosen by: $($choice.Reason)"
}

Write-Host ""

if (-not (Test-Path -LiteralPath $Session)) {
    Write-Host "Session folder not found: $Session" -ForegroundColor Red
    exit 3
}

# --- Validate what is there before analysing it -----------------------------

$summaryPath = Join-Path $Session "summary.txt"
$jsonPath    = Join-Path $Session "session.json"
$outputWav   = Join-Path $Session "output.wav"

$problems = @()

if (-not (Test-Path -LiteralPath $jsonPath))  { $problems += "session.json is missing" }
if (-not (Test-Path -LiteralPath $summaryPath)) { $problems += "summary.txt is missing" }

if (Test-Path -LiteralPath $jsonPath) {
    try {
        $null = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    } catch {
        $problems += "session.json is not valid JSON"
    }
}

if ($problems.Count -gt 0) {
    Write-Host ""
    Write-Host "Session is incomplete:" -ForegroundColor Red
    $problems | ForEach-Object { Write-Host "  - $_" }
    Write-Host ""
    Write-Host "A session only writes its report when the host releases the plugin."
    exit 3
}

if (-not (Test-Path -LiteralPath $outputWav)) {
    Write-Host ""
    Write-Host "No output.wav in this session - it ran without audio capture." -ForegroundColor Yellow
    Write-Host "The live report is still valid; only the offline analysis needs the capture."
    Write-Host "Build with -DAV_DEV_AUDIO_CAPTURE=ON to record it."
}

# --- Run the analyser -------------------------------------------------------

if (-not $BuildDir) {
    $leaf = Split-Path -Leaf $repoRoot
    $BuildDir = if ($repoRoot -match '^[\x00-\x7F]+$') {
        Join-Path $repoRoot "build"
    } else {
        Join-Path "C:\JuceBuilds" $leaf
    }
}

$dll = Join-Path $BuildDir "tests\AuroraVerbTests.dll"

$analysisCode = 0

if (Test-Path -LiteralPath $outputWav) {
    if (-not (Test-Path -LiteralPath $dll)) {
        Write-Host ""
        Write-Host "Test DLL not found at $dll" -ForegroundColor Red
        Write-Host "Build with -DAV_BUILD_TESTS=ON to run the offline analysis."
        exit 3
    }

    Write-Host "Running offline analysis..."

    $signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

    $native = Add-Type -MemberDefinition $signature -Name "AvAnalyzerLoader" -Namespace "Av" -PassThru

    $module = $native::LoadLibrary($dll)

    if ($module -eq [IntPtr]::Zero) {
        Write-Host "Could not load $dll" -ForegroundColor Red
        exit 3
    }

    $entry = $native::GetProcAddress($module, "AnalyseAuroraVerbSession")

    if ($entry -eq [IntPtr]::Zero) {
        Write-Host "AnalyseAuroraVerbSession not found in $dll" -ForegroundColor Red
        exit 3
    }

    $delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvAnalyzer
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public delegate int AnalyseDelegate(string folder);

    public static int Run(IntPtr entry, string folder)
    {
        var fn = (AnalyseDelegate)Marshal.GetDelegateForFunctionPointer(entry, typeof(AnalyseDelegate));
        return fn(folder);
    }
}
'@

    Add-Type -TypeDefinition $delegateSource -Language CSharp

    $analysisCode = [AvAnalyzer]::Run($entry, $Session)

    if ($analysisCode -eq 3) {
        Write-Host "The analyser could not read the capture." -ForegroundColor Red
        exit 3
    }

    Write-Host ""
    Get-Content -LiteralPath (Join-Path $Session "analysis.txt")
}

# --- Merge the live verdict with the offline one ----------------------------

$liveVerdict = "PASS"

$verdictLine = Select-String -LiteralPath $summaryPath -Pattern "Overall technical result:" |
               Select-Object -First 1

if ($verdictLine) {
    $liveVerdict = ($verdictLine.Line -replace '.*Overall technical result:\s*', '').Trim()
}

Write-Host ""
Write-Host "================================================================"
Write-Host " Combined technical result"
Write-Host "================================================================"
Write-Host ""
$offlineLabel = switch ($analysisCode) {
    0       { "PASS" }
    1       { "PASS WITH WARNINGS" }
    2       { "FAIL" }
    default { "not run" }
}

Write-Host "  Live session verdict:  $liveVerdict"
Write-Host "  Offline analysis:      $offlineLabel"

$liveCode = switch ($liveVerdict) {
    "PASS"               { 0 }
    "PASS WITH WARNINGS" { 1 }
    "FAIL"               { 2 }
    default              { 3 }
}

# The worse of the two wins: an offline PASS cannot clear a live NaN.
$final = [Math]::Max($liveCode, $analysisCode)

$label = switch ($final) { 0 { "PASS" } 1 { "PASS WITH WARNINGS" } 2 { "FAIL" } default { "INVALID" } }
$colour = switch ($final) { 0 { "Green" } 1 { "Yellow" } default { "Red" } }

Write-Host ""
Write-Host "  Overall: " -NoNewline
Write-Host $label -ForegroundColor $colour
Write-Host ""
Write-Host "  This is a technical result. It says whether the plugin behaved correctly,"
Write-Host "  not whether the reverb sounds good."
Write-Host ""
Write-Host "  Reports: $summaryPath"
Write-Host "           $jsonPath"

# Paths built into variables first. Windows PowerShell 5.1 mis-parses a
# single-quoted string inside a "$( ... )" subexpression and reports the whole
# script as having an unterminated string, several hundred lines away.
$analysisTextPath = Join-Path $Session "analysis.txt"
$analysisJsonPath = Join-Path $Session "analysis.json"

if (Test-Path -LiteralPath $analysisTextPath) {
    Write-Host "           $analysisTextPath"
    Write-Host "           $analysisJsonPath"
}

exit $final
