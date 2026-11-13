<#
    Renders the Phase 7 damping default A/B listening set.

    Two damping operating points - the default the plugin shipped with before
    Phase 7 (High Damp 50 %, High Damp Freq 6 kHz) and the one it ships with
    now (45 %, 10 kHz) - with everything else held identical. Five sources, at
    the shipping Mix and at 100% wet, each pair written as rendered and
    loudness-matched.

    The set is blind: the two sides are written as 1 and 2, the mapping is
    permuted and differs between sources, and the mapping plus every per-file
    measurement live in KEY-do-not-open-first.txt.

    Developer tooling. The suites never write files, so this has its own entry
    point in the test DLL, and nothing here exists in the shipped VST3.

    Usage:
        .\Scripts\export-damping-listening.ps1
        .\Scripts\export-damping-listening.ps1 -Output D:\renders -SampleRate 48000
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
    $Output = Join-Path $repoRoot "renders\damping-ab"
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering the damping A/B set to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvDampingListeningLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$entry = $native::GetProcAddress($module, "ExportAuroraVerbDampingListening")

if ($entry -eq [IntPtr]::Zero) {
    throw "ExportAuroraVerbDampingListening not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvDampingListeningExporter
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

$written = [AvDampingListeningExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Write-Host ""
Write-Host "Read MANIFEST.txt first. Do not open KEY-do-not-open-first.txt"
Write-Host "until the six answers are written down."
