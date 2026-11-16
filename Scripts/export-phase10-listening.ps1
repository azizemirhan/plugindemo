<#
    Renders the Phase 10 Stage 3 early-reflection listening set.

    The production ER path is technically clean: legacy neutrality is
    bit-exact against a build of the Phase 9 commit on twelve cases, no
    late-path control changes a single sample of the early layer, the balance
    holds total wet energy to 0.03 dB, and the arrival schedule does not
    repeat at any period.

    Four cases over four sources - snare, piano, pad, vocal:

        A  the primary risk   100 % wet, Size 100 %, Early/Late 0/25/50
                              plus a blind Size 80 vs 100 pair at Early/Late 25
        B  slap and rhythm    snare, Size 80 %, Early/Late 15/50/75
        C  coloration         vocal and piano, Mix 30 %, Early/Late 0 and 50
        D  stereo and mono    snare, Early/Late 75 %, against its own downmix

    Case A exists to answer the one open question Stage 2 left behind: at
    Size 100 % there is a 6.02 dB energy valley between the early layer and
    the late field, down from 8.09 dB after raising the ER envelope decay to
    1.5, and the residual is structural rather than tunable. Whether it reads
    as natural separation or as a hole is not a measurement question, which
    is why envelope decay 1.5 is a production listening CANDIDATE.

    Nothing is loudness-matched. The balance law is energy-calibrated and
    holds total wet energy to 0.03 dB across its travel, so there is nothing
    worth matching away and matching would only hide whatever is really there.

    Only case A's Size pair is blind - a listener who knows which render the
    measurement complained about will hear what it predicted - and its
    mapping lives in KEY.txt.

    Developer tooling. The suites never write files, so this has its own entry
    point in the test DLL, and nothing here exists in the shipped VST3.

    Usage:
        .\Scripts\export-phase10-listening.ps1
        .\Scripts\export-phase10-listening.ps1 -Output D:\renders -SampleRate 48000
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
    $Output = Join-Path $repoRoot (Join-Path "renders" "phase10-stage3-ab")
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering the Phase 10 Stage 3 set to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvPhase10ListeningLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$entry = $native::GetProcAddress($module, "ExportAuroraVerbPhase10Listening")

if ($entry -eq [IntPtr]::Zero) {
    throw "ExportAuroraVerbPhase10Listening not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvPhase10ListeningExporter
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

$written = [AvPhase10ListeningExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Write-Host ""
Write-Host "Read LISTENING.md first. Do not open KEY.txt"
Write-Host "until the case A and case B answers are written down."
