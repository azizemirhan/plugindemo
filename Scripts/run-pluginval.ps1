<#
    Runs pluginval at strictness 10 against all three build configurations.

    The three exist because they are three different plugins. The default build
    has no preview engine and no diagnostic layer; the preview build routes the
    wet path through the tank; the diagnostic preview build adds telemetry and
    audio capture on top. A pass in one says nothing about the others, and the
    diagnostic build is the only one that has ever been in a host, so validating
    only that one would leave the shipping configuration untested.

    JUCE is not re-fetched for each configuration. The first configure clones it;
    the others are pointed at that same checkout, which turns a twenty-minute
    configure into a short one and guarantees all three are built against
    identical JUCE sources.

    Usage:
        .\Scripts\run-pluginval.ps1
        .\Scripts\run-pluginval.ps1 -Strictness 10 -Repeat 1
#>

[CmdletBinding()]
param(
    [int]    $Strictness = 10,
    [int]    $Repeat = 1,
    [string] $Pluginval = "",
    [string] $BuildRoot = "C:\JuceBuilds",
    [string] $JucePath  = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

# --- Locate the tools -------------------------------------------------------

$cmakeExe = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmakeExe) {
    $fallback = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $fallback) { $cmakeExe = $fallback }
}
if (-not $cmakeExe) { throw "cmake was not found." }

if (-not $Pluginval) {
    $candidates = @(
        (Join-Path $BuildRoot "pluginval\pluginval.exe"),
        (Join-Path $repoRoot "Tools\pluginval\pluginval.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $Pluginval = $c; break }
    }
}
if (-not $Pluginval -or -not (Test-Path -LiteralPath $Pluginval)) {
    throw "pluginval.exe was not found. Pass -Pluginval with its path."
}

# The diagnostic preview build is configured first elsewhere; reuse whatever
# JUCE checkout already exists rather than cloning it three times.
if (-not $JucePath) {
    $existing = Join-Path $BuildRoot "valhalla-reverb\_deps\juce-src"
    if (Test-Path -LiteralPath (Join-Path $existing "CMakeLists.txt")) {
        $JucePath = $existing
    }
}

# --- The three configurations ----------------------------------------------

$configurations = @(
    @{
        Name = "default"
        Dir  = "av-pluginval-default"
        Args = @()
    },
    @{
        Name = "preview"
        Dir  = "av-pluginval-preview"
        Args = @("-DAV_DEV_DIFFUSION_PREVIEW=ON", "-DAV_DEV_ALLPASS_CANDIDATE=1")
    },
    @{
        Name = "diagnostic-preview"
        Dir  = "av-pluginval-diagnostic"
        Args = @("-DAV_DEV_DIFFUSION_PREVIEW=ON", "-DAV_DEV_ALLPASS_CANDIDATE=1",
                 "-DAV_DEV_DIAGNOSTICS=ON", "-DAV_DEV_AUDIO_CAPTURE=ON")
    }
)

# Used only to tell a policy block apart from a real load failure. See below.
if (-not ("AvLoader" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class AvLoader {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern IntPtr LoadLibraryW(string path);
}
"@
}

$results = @()

foreach ($configuration in $configurations) {
    $buildDir = Join-Path $BuildRoot $configuration.Dir

    Write-Host ""
    Write-Host "=== $($configuration.Name) ===" -ForegroundColor Cyan

    # AV_COPY_PLUGIN_AFTER_BUILD is ON by default, so an ordinary build installs
    # itself into the user's VST3 folder. Three validation builds in a row
    # therefore leave whichever one finished last sitting where the DAW will
    # find it - which happened here, and left a preview build with no diagnostic
    # layer in place of the one the next host session was supposed to use. A
    # validation build has no business installing anything.
    $configureArgs = @("-S", $repoRoot, "-B", $buildDir,
                       "-G", "Visual Studio 17 2022", "-A", "x64",
                       "-DAV_COPY_PLUGIN_AFTER_BUILD=OFF")

    if ($JucePath) {
        $configureArgs += "-DAV_JUCE_PATH=$($JucePath -replace '\\', '/')"
    }

    $configureArgs += $configuration.Args

    Write-Host "Configuring..."
    & $cmakeExe @configureArgs 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "Configure failed for $($configuration.Name)." }

    Write-Host "Building..."
    & $cmakeExe --build $buildDir --config Release --parallel --target AuroraVerb_VST3 2>&1 |
        Select-String -Pattern "error|warning C" | Select-Object -First 10
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $($configuration.Name)." }

    $vst3 = Join-Path $buildDir "AuroraVerb_artefacts\Release\VST3\AuroraVerb.vst3"

    if (-not (Test-Path -LiteralPath $vst3)) {
        throw "No VST3 produced for $($configuration.Name) at $vst3"
    }

    Write-Host "Validating at strictness $Strictness..."

    $logPath = Join-Path $BuildRoot ("pluginval-" + $configuration.Name + ".txt")

    # Redirected by cmd, not by PowerShell.
    #
    # Collecting pluginval's output with `& ... 2>&1` produced an empty string
    # while still returning exit code 0, so the first run of this reported three
    # passes backed by three empty logs. An exit code with no output behind it
    # is not evidence that anything ran. cmd's own redirection captures it.
    $command = '"{0}" --strictness-level {1} --validate-in-process --skip-gui-tests --repeat {2} --validate "{3}" > "{4}" 2>&1' `
               -f $Pluginval, $Strictness, $Repeat, $vst3, $logPath

    cmd /c $command
    $code = $LASTEXITCODE

    $text = if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath -Raw } else { "" }

    $failures = ([regex]::Matches($text, "(?m)^\s*\*\*\* FAILED")).Count

    # A run that produced no log did not happen, whatever it returned.
    $succeeded = ($code -eq 0) -and ($text -match "SUCCESS")

    $results += [pscustomobject]@{
        Configuration = $configuration.Name
        ExitCode      = $code
        Failures      = $failures
        LogLines      = ($text -split "`n").Count
        Verified      = $succeeded
        Vst3          = $vst3
    }

    if ($succeeded) {
        Write-Host "  PASS  (log: $logPath)" -ForegroundColor Green
    } elseif ($code -eq 0) {
        Write-Host "  UNVERIFIED  exit 0 but no SUCCESS line in $logPath" -ForegroundColor Yellow
    } else {
        Write-Host "  FAIL  exit $code, $failures failure(s) (log: $logPath)" -ForegroundColor Red

        # pluginval says "the plugin binary is missing or damaged" for a binary
        # that is neither: Smart App Control refusing to load it looks exactly
        # the same from the outside. Asking the loader directly is what tells
        # the two apart, and getting this wrong costs a long time chasing a
        # defect that is not in the plugin.
        $inner = Join-Path $vst3 "Contents\x86_64-win\AuroraVerb.vst3"

        if ((Test-Path -LiteralPath $inner) -and ($text -match "No types found")) {
            $handle = [AvLoader]::LoadLibraryW($inner)
            $win32  = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()

            if ($handle -ne [IntPtr]::Zero) {
                Write-Host "        the binary loads, so this is a real validation failure" -ForegroundColor Red
            } elseif ($win32 -eq 4551) {
                Write-Host "        LoadLibrary error 4551: Application Control policy blocked this file." -ForegroundColor Yellow
                Write-Host "        Not a plugin defect. Delete the .vst3 and rebuild; the new hash" -ForegroundColor Yellow
                Write-Host "        usually gets through, sometimes only after a second attempt." -ForegroundColor Yellow
            } else {
                Write-Host "        LoadLibrary failed with error $win32 - a genuine load failure." -ForegroundColor Red
            }
        }
    }
}

Write-Host ""
$results | Format-Table -AutoSize

$bad = @($results | Where-Object { -not $_.Verified })

if ($bad.Count -gt 0) {
    Write-Host "$($bad.Count) of $($results.Count) configurations did not pass verifiably." -ForegroundColor Red
    exit 1
}

Write-Host "All $($results.Count) configurations passed at strictness $Strictness." -ForegroundColor Green
exit 0
