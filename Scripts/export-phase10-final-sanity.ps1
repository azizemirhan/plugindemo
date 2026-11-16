<#
    Renders the four Phase 10 final sanity files.

    Not blind and nothing to choose between. Everything is at the production
    candidate: cellOccupancy 0.40, envelopeDecay 1.5, the linear-in-power
    p = 0.50 gain law, the recalibrated BAL-4 calibration table, Size 80 %,
    Mix 30 %, everything else default.

    Four sources - snare, vocal, piano, pad - each sweeping Early/Late from
    0 to 100 % across its length. The sweep is deliberate: a static render
    cannot show unnatural level movement, which is one of the six things
    these files exist to check, and a sweep also walks through the handoff,
    the slap region and the ER-heavy end in a single pass while driving the
    real automation path.

    One behaviour is already measured and is not a surprise: on sustained
    narrowband material the two branches can become strongly correlated or
    anti-correlated at the source frequency, and the sweep then shows a level
    trend of several dB, up to 13 dB at small Size with a short Decay. It is
    present at every ER pattern measured and no calibration removes it. It is
    carried as an explicit product concern. The listening decides whether it
    is objectionable in use, not whether it exists.

    Nothing is level-matched; the level trend is part of what is checked.

    Developer tooling. The suites never write files, so this has its own entry
    point in the test DLL, and nothing here exists in the shipped VST3.

    Usage:
        .Scriptsexport-phase10-final-sanity.ps1
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
    $Output = Join-Path $repoRoot (Join-Path "renders" "phase10-final-sanity")
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering the Phase 10 final sanity set to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvPhase10FinalSanityLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$entry = $native::GetProcAddress($module, "ExportAuroraVerbPhase10FinalSanity")

if ($entry -eq [IntPtr]::Zero) {
    throw "ExportAuroraVerbPhase10FinalSanity not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvPhase10FinalSanityExporter
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

$written = [AvPhase10FinalSanityExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Write-Host ""
Write-Host "Read LISTENING.md first."
Write-Host ""
