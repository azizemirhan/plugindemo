<#
    Renders the Phase 12 Stage 4 blind pack for human structural validation.

    Twelve files, s4-01.wav to s4-12.wav. Each file is ONE comparison: a
    source through one reverb, half a second of silence, then the same source
    through another.

      6  Hall against Room B   (4 centred source, 2 hard-left source)
      4  Hall against Plate A  (centred source)
      1  Room B against Plate A
      1  Hall against Hall, a catch trial

    100 % wet, Early/Late 0, the second reverb level-matched to the first by
    integrated energy. KEY.txt says what each file is; LISTENING.md has the
    questions and the answer sheet. Do not open KEY.txt before listening.

    Rendered at 48 kHz, the rate the analysis runs at. No search is run, so it
    takes a few seconds.

    Developer tooling. The suites never write files, so this has its own entry
    point in the test DLL, and nothing here exists in the shipped VST3.

    Usage:
        .\Scripts\export-phase12-stage4-pack.ps1
#>
[CmdletBinding()]
param(
    [string] $Output = "",
    [double] $SampleRate = 48000,
    [string] $BuildDir = "",

    # Visual Studio is a multi-configuration generator: it writes to
    # tests\<Config>\ and not to tests\. A single-configuration generator
    # (Ninja, Unix Makefiles) writes straight to tests\. Both layouts are
    # searched, this one first.
    [string] $Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $BuildDir) {
    # Matches build.ps1: a non-ASCII source path is redirected to C:\JuceBuilds.
    $leaf = Split-Path -Leaf $repoRoot
    $BuildDir = if ($repoRoot -match '^[\x00-\x7F]+$') {
        Join-Path $repoRoot "build"
    } else {
        Join-Path "C:\JuceBuilds" $leaf
    }
}

# A multi-config build leaves BOTH layouts populated - tests\<Config>\ from
# this generator and tests\ from whatever ran before - and the stale one is
# often months old. Picking tests\ because it exists is how a render ends up
# made by a build nobody remembers, so the configuration folder wins whenever
# it has a DLL, and the search order is reported rather than assumed.
$testsDir = Join-Path $BuildDir "tests"

$candidates = @(
    (Join-Path (Join-Path $testsDir $Config) "AuroraVerbTests.dll"),
    (Join-Path $testsDir "AuroraVerbTests.dll")
)

$dll = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $dll) {
    throw "Test DLL not found. Looked in:`n  $($candidates -join "`n  ")`nBuild with -DAV_BUILD_TESTS=ON first."
}

Write-Host "Using $dll"

# The renders come from the DLL and the suites come from the executable. They
# are separate CMake targets built from the same sources, so building only
# `--target AuroraVerbTests` leaves a stale DLL behind and the WAVs would not
# be made by the code the test run measured. Refuse rather than mislead.
$exe = Join-Path (Split-Path -Parent $dll) "AuroraVerbTests.exe"

if (Test-Path -LiteralPath $exe) {
    $dllTime = (Get-Item -LiteralPath $dll).LastWriteTime
    $exeTime = (Get-Item -LiteralPath $exe).LastWriteTime

    if ($exeTime -gt $dllTime.AddSeconds(30)) {
        Write-Warning "AuroraVerbTests.dll ($dllTime) is older than AuroraVerbTests.exe ($exeTime)."
        Write-Warning "The renders would not match what the test suite measured."
        Write-Warning "Build the DLL first:  cmake --build `"$BuildDir`" --config RelWithDebInfo --target AuroraVerbTestsShared"
        throw "Refusing to export from a stale DLL."
    }
}

if (-not $Output) {
    $Output = Join-Path $repoRoot (Join-Path "renders" "phase12-stage4")
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering the Phase 12 Stage 4 blind pack to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvPhase12Stage4PackLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$entry = $native::GetProcAddress($module, "ExportAuroraVerbPhase12Stage4Pack")

if ($entry -eq [IntPtr]::Zero) {
    throw "ExportAuroraVerbPhase12Stage4Pack not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvPhase12Stage4PackExporter
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public delegate int ExportDelegate(string directory, double sampleRate);

    public static int Run(IntPtr entry, string directory, double sampleRate)
    {
        var fn = (ExportDelegate)Marshal.GetDelegateForFunctionPointer(entry, typeof(ExportDelegate));
        return fn(directory, sampleRate);
    }
}
'@

Add-Type -TypeDefinition $delegateSource -Language CSharp

$written = [AvPhase12Stage4PackExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Write-Host ""
Write-Host "Read LISTENING.md first."
Write-Host ""
