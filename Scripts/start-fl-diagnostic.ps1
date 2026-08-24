<#
    Prepares an FL Studio diagnostic session.

    What this does:
      - finds FL Studio in the usual install locations, and reports where
      - starts the diagnostic watcher in a separate terminal
      - launches FL Studio with normal user rights
      - prints the steps the user performs by hand

    What this deliberately does NOT do:
      - ask for administrator rights
      - modify, patch or automate FL Studio in any way
      - click anything in its interface
      - install or copy anything into its folders

    Scanning for the plugin, inserting it and listening are the user's steps.
    A script that drove the host would be testing the script rather than the
    plugin, and would be indistinguishable from the kind of automation a
    licensed application is entitled to object to.

    Usage:
        .\Scripts\start-fl-diagnostic.ps1
        .\Scripts\start-fl-diagnostic.ps1 -FlStudioPath "D:\FL\FL64.exe" -NoWatcher
#>

[CmdletBinding()]
param(
    [string] $FlStudioPath = "",
    [string] $DiagnosticRoot = "",
    [switch] $NoWatcher,
    [switch] $NoLaunch
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

# --- 1. Find FL Studio ------------------------------------------------------

function Find-FlStudio {
    param([string] $explicitPath)

    if ($explicitPath) {
        if (Test-Path -LiteralPath $explicitPath) { return $explicitPath }
        throw "FL Studio not found at the path given: $explicitPath"
    }

    # The registry first: it is where an installer records the real location,
    # whatever drive the user chose.
    $registryKeys = @(
        "HKLM:\SOFTWARE\Image-Line\Shared\Paths",
        "HKCU:\SOFTWARE\Image-Line\Shared\Paths"
    )

    foreach ($key in $registryKeys) {
        if (Test-Path $key) {
            try {
                $entry = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue

                foreach ($property in $entry.PSObject.Properties) {
                    if ($property.Value -is [string] -and $property.Value -match 'FL Studio') {
                        $candidate = Join-Path $property.Value "FL64.exe"
                        if (Test-Path -LiteralPath $candidate) { return $candidate }
                    }
                }
            } catch { }
        }
    }

    # Then the conventional install roots, on every fixed drive rather than
    # assuming C:.
    $drives = Get-PSDrive -PSProvider FileSystem |
              Where-Object { $_.Free -ne $null } |
              Select-Object -ExpandProperty Root

    # Version-agnostic: any "FL Studio <anything>" folder under an Image-Line
    # directory. A hard-coded list of version numbers goes stale every year -
    # this machine has FL Studio 2026, which a list written for 20/21/2024
    # would have missed had the registry lookup above not caught it first.
    $imageLineRoots = @(
        "Program Files\Image-Line",
        "Program Files (x86)\Image-Line"
    )

    foreach ($drive in $drives) {
        foreach ($root in $imageLineRoots) {
            $base = Join-Path $drive $root

            if (-not (Test-Path -LiteralPath $base)) { continue }

            $versions = Get-ChildItem -LiteralPath $base -Directory -Filter "FL Studio*" `
                                      -ErrorAction SilentlyContinue |
                        Sort-Object Name -Descending

            foreach ($version in $versions) {
                $candidate = Join-Path $version.FullName "FL64.exe"
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }
    }

    # Last resort: a shallow search of the Image-Line folders only, so this
    # never turns into a scan of the whole disk.
    foreach ($drive in $drives) {
        $imageLine = Join-Path $drive "Program Files\Image-Line"

        if (Test-Path -LiteralPath $imageLine) {
            $found = Get-ChildItem -LiteralPath $imageLine -Filter "FL64.exe" -Recurse `
                                   -Depth 3 -ErrorAction SilentlyContinue |
                     Select-Object -First 1

            if ($found) { return $found.FullName }
        }
    }

    return $null
}

$flPath = Find-FlStudio -explicitPath $FlStudioPath

if (-not $flPath) {
    Write-Host ""
    Write-Host "FL Studio was not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Looked in:"
    Write-Host "  - HKLM/HKCU SOFTWARE\Image-Line\Shared\Paths"
    Write-Host "  - <drive>\Program Files\Image-Line\FL Studio <version>\FL64.exe"
    Write-Host "  - <drive>\Program Files (x86)\Image-Line\FL Studio <version>\FL64.exe"
    Write-Host ""
    Write-Host "If it is installed somewhere else, pass the path:"
    Write-Host '  .\Scripts\start-fl-diagnostic.ps1 -FlStudioPath "D:\Path\To\FL64.exe"'
    exit 1
}

Write-Host "FL Studio found at:" -ForegroundColor Green
Write-Host "  $flPath"

# --- 2. Work out where diagnostics will land --------------------------------

if (-not $DiagnosticRoot) {
    $base = if ($env:AV_DIAGNOSTIC_ROOT) { $env:AV_DIAGNOSTIC_ROOT }
            elseif ($env:USERPROFILE)    { $env:USERPROFILE }
            else                         { "." }

    $DiagnosticRoot = Join-Path $base "AuroraVerbDiagnostics"
}

New-Item -ItemType Directory -Force -Path $DiagnosticRoot | Out-Null

Write-Host ""
Write-Host "Diagnostic sessions will be written to:"
Write-Host "  $DiagnosticRoot"

# Both standard Windows VST3 locations are checked and whichever actually holds
# the bundle is reported. Naming one of them up front was wrong as soon as the
# plugin moved: FL Studio does not scan the per-user folder unless it has been
# added to its search paths by hand, so the global folder is the one that works
# out of the box.
$vst3Locations = @(
    "$env:ProgramFiles\Common Files\VST3",
    (Join-Path $env:LOCALAPPDATA "Programs\Common\VST3")
)

$installed = @()

foreach ($dir in $vst3Locations) {
    $candidate = Join-Path $dir "AuroraVerb.vst3"
    $binary = Join-Path $candidate "Contents\x86_64-win\AuroraVerb.vst3"

    if (Test-Path -LiteralPath $binary) {
        $installed += [pscustomobject]@{ Bundle = $candidate; Binary = $binary }
    }
}

Write-Host ""

if ($installed.Count -eq 0) {
    Write-Host "AuroraVerb is not installed in either standard VST3 location:" -ForegroundColor Yellow
    $vst3Locations | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
    Write-Host "  Build the diagnostic preview and install it first:"
    Write-Host "    cmake -S . -B build -DAV_DEV_DIFFUSION_PREVIEW=ON -DAV_DEV_DIAGNOSTICS=ON -DAV_DEV_AUDIO_CAPTURE=ON"
    Write-Host "    cmake --build build --config Release"
} else {
    Write-Host "AuroraVerb found at:"

    foreach ($entry in $installed) {
        $hash = (Get-FileHash -LiteralPath $entry.Binary -Algorithm SHA256).Hash
        Write-Host "  $($entry.Bundle)"
        Write-Host "    SHA256 $hash"
    }

    if ($installed.Count -gt 1) {
        Write-Host ""
        Write-Host "  More than one copy is installed. FL Studio may load either of them," -ForegroundColor Yellow
        Write-Host "  and a diagnostic session cannot say which. Remove all but one." -ForegroundColor Yellow
    }
}

$vst3Dir = Split-Path -Parent ($(if ($installed.Count -gt 0) { $installed[0].Bundle } else { Join-Path $vst3Locations[0] "AuroraVerb.vst3" }))

# --- 3. Start the watcher ---------------------------------------------------

if (-not $NoWatcher) {
    $watcher = Join-Path $PSScriptRoot "watch-diagnostics.ps1"

    if (Test-Path -LiteralPath $watcher) {
        Write-Host ""
        Write-Host "Starting the diagnostic monitor in a separate window..."

        Start-Process -FilePath "powershell.exe" `
                      -ArgumentList @("-NoExit", "-ExecutionPolicy", "Bypass",
                                      "-File", "`"$watcher`"",
                                      "-Root", "`"$DiagnosticRoot`"")
    }
}

# --- 4. Launch FL Studio, with ordinary user rights --------------------------

if (-not $NoLaunch) {
    Write-Host ""
    Write-Host "Launching FL Studio (normal user rights, no elevation)..."

    # No -Verb RunAs: a plugin that needs an elevated host is a plugin with a
    # problem, and the diagnostic folder is under the user's own profile.
    Start-Process -FilePath $flPath
}

# --- 5. What the user does ---------------------------------------------------

Write-Host ""
Write-Host "================================================================"
Write-Host " Steps to perform in FL Studio" -ForegroundColor Cyan
Write-Host "================================================================"
Write-Host ""
Write-Host "  1. Options > Manage plugins."
Write-Host "  2. Confirm the VST3 search path includes:"
Write-Host "       $vst3Dir"
Write-Host "  3. Click 'Find more plugins' and wait for the scan to finish."
Write-Host "  4. Locate 'AuroraVerb' and mark it as a favourite."
Write-Host "  5. Open the mixer, select an insert slot, and add AuroraVerb."
Write-Host "  6. Load one of the test sources into the playlist, or play any"
Write-Host "     material through that mixer track."
$checklistPath = Join-Path $repoRoot "AuroraVerbFlTest\TestChecklist.md"

Write-Host "  7. Work through the checklist:"
Write-Host "       $checklistPath"
Write-Host "  8. Close FL Studio when finished. The final report is written"
Write-Host "     when the host releases the plugin."
Write-Host ""
Write-Host "  The monitor window shows live telemetry while you work. The"
Write-Host "  authoritative result is summary.txt in the session folder."
Write-Host ""
$sourcesPath = Join-Path $repoRoot "AuroraVerbFlTest\Sources"

Write-Host "  Session folders:  $DiagnosticRoot"
Write-Host "  Test sources:     $sourcesPath"
Write-Host ""
