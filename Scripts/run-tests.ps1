<#
    Runs the offline DSP test suites and prints the report.

    Tries the console executable first. If Windows Smart App Control blocks it
    (it refuses to start locally-built unsigned executables), falls back to
    loading AuroraVerbTests.dll, which the same policy permits.

    Usage:
        .\Scripts\run-tests.ps1
        .\Scripts\run-tests.ps1 -BuildDir C:\JuceBuilds\valhalla-reverb
#>

[CmdletBinding()]
param(
    [string] $BuildDir,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string] $Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

if (-not $BuildDir) {
    if ($root -match '[^\x00-\x7F]') {
        $BuildDir = Join-Path $env:SystemDrive ('JuceBuilds\' + (Split-Path -Leaf $root))
    } else {
        $BuildDir = Join-Path $root 'build'
    }
}

$testDir = Join-Path $BuildDir 'tests'
$exe     = Join-Path $testDir 'AuroraVerbTests.exe'
$dll     = Join-Path $testDir 'AuroraVerbTests.dll'

if (-not (Test-Path $exe) -and -not (Test-Path $dll)) {
    throw "No test artefacts in $testDir. Build with: .\Scripts\build.ps1 -Tests"
}

# The two artefacts are separate CMake targets built from the same sources, and
# this script silently chooses between them. Building only one therefore means
# the fallback runs a different, older suite than the one just compiled - and
# says ALL TESTS PASSED while doing it.
#
# That has now happened twice here. Once a corrected loudness meter shipped in
# the executable while the renders were still made by the previous DLL. Once a
# whole new test suite passed on the executable, the executable was then blocked
# by Smart App Control, and the DLL fallback reported 1444 of 1444 - twenty-four
# checks short, with the new suite absent and nothing saying so.
# Each artefact is compared against the SOURCES, not against the other one.
#
# Comparing the two to each other looked right and was not: building them in
# separate invocations leaves ten minutes between two artefacts that are both
# perfectly current, and the check then refuses a run it should have allowed.
# What actually matters is whether either is older than the code it claims to
# test.
$repoRoot = Split-Path -Parent $PSScriptRoot

$newestSource = Get-ChildItem -Path (Join-Path $repoRoot 'Source'), (Join-Path $repoRoot 'Tests') `
                              -Recurse -File -Include *.cpp, *.h, *.hpp -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1

if ($newestSource) {
    $stale = @()

    foreach ($artefact in @($exe, $dll)) {
        if ((Test-Path $artefact) -and
            ((Get-Item $artefact).LastWriteTime -lt $newestSource.LastWriteTime)) {
            $stale += (Split-Path -Leaf $artefact)
        }
    }

    if ($stale.Count -gt 0) {
        Write-Warning "Newer than the build: $($newestSource.Name) ($($newestSource.LastWriteTime))"
        Write-Warning "Stale artefact(s): $($stale -join ', ')"
        Write-Warning "This script chooses between the two silently, so a pass would not mean what it says."
        Write-Warning "Build both:  cmake --build `"$BuildDir`" --config Release --target AuroraVerbTests --target AuroraVerbTestsShared"
        throw "Refusing to run a stale test binary."
    }
}

# --- Route 1: run the executable -------------------------------------------

if (Test-Path $exe) {
    $blocked = $false

    try {
        & $exe
        $code = $LASTEXITCODE
    } catch {
        $blocked = $true
    }

    if (-not $blocked) {
        exit $code
    }

    Write-Warning "Smart App Control blocked $exe. Falling back to the DLL entry point."
}

# --- Route 2: load the DLL --------------------------------------------------

if (-not (Test-Path $dll)) {
    throw "AuroraVerbTests.dll not found in $testDir."
}

$reportPath = Join-Path ([System.IO.Path]::GetTempPath()) 'auroraverb-tests.txt'

$signature = @"
using System;
using System.Runtime.InteropServices;

public static class AuroraVerbTestRunner
{
    [DllImport(@"__DLL_PATH__", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int RunAuroraVerbTestsToFile(string path);
}
"@

$signature = $signature.Replace('__DLL_PATH__', $dll)

if (-not ('AuroraVerbTestRunner' -as [type])) {
    Add-Type -TypeDefinition $signature -Language CSharp
}

$code = [AuroraVerbTestRunner]::RunAuroraVerbTestsToFile($reportPath)

if (Test-Path $reportPath) {
    Get-Content $reportPath
} else {
    Write-Warning "The test DLL produced no report file."
}

exit $code
