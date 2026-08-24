<#
    Renders the developer listening set to WAV.

    Developer tooling, not part of a test run: the suites must not write files
    as a side effect, so this has its own entry point in the test DLL. Nothing
    here exists in the shipped VST3.

    Usage:
        .\Scripts\export-signals.ps1
        .\Scripts\export-signals.ps1 -Output C:\renders -SampleRate 96000
#>

[CmdletBinding()]
param(
    [string] $Output = "",
    [double] $SampleRate = 48000,
    [string] $BuildDir = "",

    # The four gain-staging A/B files instead of the listening set: dry
    # reference, the chain as it stands, the same loudness-matched to the dry
    # reference, and 100% wet.
    [switch] $GainStaging
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

$dll = Join-Path $BuildDir "tests\AuroraVerbTests.dll"

if (-not (Test-Path -LiteralPath $dll)) {
    throw "Test DLL not found at $dll. Build with -DAV_BUILD_TESTS=ON first."
}

# The renders come from the DLL, the suites come from the executable, and they
# are separate CMake targets built from the same sources. Building only
# `--target AuroraVerbTests` leaves the DLL behind, so the tests report a fixed
# measurement while the exported WAVs are still made with the broken one. That
# happened: a corrected loudness meter shipped in the exe and the renders were
# produced by the previous DLL forty minutes later.
$exe = Join-Path $BuildDir "tests\AuroraVerbTests.exe"

if (Test-Path -LiteralPath $exe) {
    $dllTime = (Get-Item -LiteralPath $dll).LastWriteTime
    $exeTime = (Get-Item -LiteralPath $exe).LastWriteTime

    if ($exeTime -gt $dllTime.AddSeconds(30)) {
        Write-Warning "AuroraVerbTests.dll ($dllTime) is older than AuroraVerbTests.exe ($exeTime)."
        Write-Warning "The renders would not match what the test suite measured."
        Write-Warning "Build the DLL first:  cmake --build `"$BuildDir`" --config Release --target AuroraVerbTestsShared"
        throw "Refusing to export from a stale DLL."
    }
}

if (-not $Output) {
    $Output = Join-Path $repoRoot "renders"
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

Write-Host "Rendering to $Output at $SampleRate Hz..."

$signature = @'
[DllImport("KERNEL32.DLL", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibrary(string path);

[DllImport("KERNEL32.DLL", SetLastError = true)]
public static extern IntPtr GetProcAddress(IntPtr module, string name);
'@

$native = Add-Type -MemberDefinition $signature -Name "AvExportLoader" -Namespace "Av" -PassThru

$module = $native::LoadLibrary($dll)

if ($module -eq [IntPtr]::Zero) {
    throw "Could not load $dll (error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$export = if ($GainStaging) { "ExportAuroraVerbGainStaging" } else { "ExportAuroraVerbTestSignals" }

$entry = $native::GetProcAddress($module, $export)

if ($entry -eq [IntPtr]::Zero) {
    throw "$export not found in $dll."
}

$delegateSource = @'
using System;
using System.Runtime.InteropServices;

public static class AvExporter
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

$written = [AvExporter]::Run($entry, $Output, $SampleRate)

if ($written -lt 0) {
    throw "Export failed (returned $written). Is $Output writable?"
}

Write-Host "$written files written."
Get-ChildItem -LiteralPath $Output -Filter *.wav | Select-Object Name, Length
