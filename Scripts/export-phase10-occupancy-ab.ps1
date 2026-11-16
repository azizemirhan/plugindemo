<#
    Renders the final Phase 10 occupancy A/B: cellOccupancy 0.20 vs 0.40.

    Four files, two blind cases:

        O1  snare, Size 100, Early/Late 25, 100 % wet
        O2  vocal, Size 100, Early/Late 50, 100 % wet

    This replaces the earlier eight-file set, whose primary comparison the
    research has overtaken. That set compared the production 0.70 against
    0.20; 0.70 is no longer a structural candidate, carrying a 16.47 dB
    large-Size handoff valley against 7.26 dB at 0.20 with no gain in
    aperiodicity to show for it.

    The two candidates here are the measurement finalists and the
    measurements disagree about them: 0.20 has the better handoff (7.26 vs
    8.86 dB at Size 100) and 0.40 the better third-octave flatness (3.76 vs
    5.18 dB of ripple). Both are fully aperiodic and indistinguishable on
    correlation, mono downmix and CPU. That is where measurement stops and a
    listener is needed.

    NEITHER IS A SHIPPING CANDIDATE and the production constant is untouched
    at 0.70. Both values exist here only as render-time Pattern data.

    Nothing is loudness-matched. The A/B mapping is randomised independently
    per case, appears in no filename, and lives in KEY.txt.

    Developer tooling. The suites never write files, so this has its own entry
    point in the test DLL, and nothing here exists in the shipped VST3.

    Usage:
        .\Scripts\export-phase10-occupancy-ab.ps1
        .\Scripts\export-phase10-occupancy-ab.ps1 -Output D:\renders -SampleRate 48000
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
    $Output = Join-Path $repoRoot (Join-Path "renders" "phase10-occupancy-ab")
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering the Phase 10 occupancy A/B to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvPhase10OccupancyAbLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$entry = $native::GetProcAddress($module, "ExportAuroraVerbPhase10OccupancyAb")

if ($entry -eq [IntPtr]::Zero) {
    throw "ExportAuroraVerbPhase10OccupancyAb not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvPhase10OccupancyAbExporter
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

$written = [AvPhase10OccupancyAbExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Write-Host ""
Write-Host "Read LISTENING.md first. Do not open KEY.txt"
Write-Host "until both case answers are written down."
