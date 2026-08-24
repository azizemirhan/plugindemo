<#
    Controlled install of a production AuroraVerb VST3 into the global folder.

    Needs an ELEVATED shell: C:\Program Files\Common Files\VST3 is not writable
    by a normal user, which is why this is a separate, deliberate step rather
    than a post-build copy. Every preset ships with
    AV_COPY_PLUGIN_AFTER_BUILD=OFF precisely so that no build can put a binary
    there as a side effect.

    The script refuses to proceed unless each gate passes, and restores the
    backup by itself if the installed hash does not come out as expected.

    Usage, from an elevated PowerShell:

        .\Scripts\install-production-vst3.ps1 `
            -ExpectedSourceHash 2B451890E229F7FDC4A202898AB0384BCAAEF88E2A877FD520D5464DC9E02BD8 `
            -BackupDir "C:\Users\<you>\AppData\Local\Programs\Common\VST3-AuroraVerb-backups\global-pre-hardening-2026-08-23_23-59-36"

    Add -WhatIf to see what it would do without touching anything.
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $Source = 'C:\JuceBuilds\av-production\AuroraVerb_artefacts\Release\VST3\AuroraVerb.vst3',

    [string] $Target = 'C:\Program Files\Common Files\VST3\AuroraVerb.vst3',

    # Refuse to install anything else. No default: naming the hash is the point.
    [Parameter(Mandatory = $true)]
    [string] $ExpectedSourceHash,

    # An existing verified rollback backup. Required, so that install and
    # rollback are never separated.
    [Parameter(Mandatory = $true)]
    [string] $BackupDir
)

$ErrorActionPreference = 'Stop'

function Get-BundleHash([string] $bundlePath) {
    $binary = Join-Path $bundlePath 'Contents\x86_64-win\AuroraVerb.vst3'

    if (-not (Test-Path -LiteralPath $binary)) { return $null }

    return (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
}

# --- Gate 0: elevation --------------------------------------------------------

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$elevated = (New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole(
                [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $elevated) {
    throw "Not elevated. Re-run this script from an administrator PowerShell."
}

# --- Gate 1: no host may be holding the old binary ---------------------------

$hostProcesses = @('FL64', 'FL', 'FLStudio', 'Ableton Live', 'Cubase', 'reaper',
                   'Studio One', 'Bitwig Studio', 'Cakewalk', 'pluginval')

$running = Get-Process -ErrorAction SilentlyContinue |
           Where-Object { $hostProcesses -contains $_.ProcessName }

if ($running) {
    throw ("Close these first, or they will keep the old plugin loaded: " +
           (($running | ForEach-Object { $_.ProcessName }) -join ', '))
}

# --- Gate 2: the source is the artefact that was verified ---------------------

$sourceHash = Get-BundleHash $Source

if (-not $sourceHash) { throw "No plugin binary inside '$Source'." }

if ($sourceHash -ne $ExpectedSourceHash) {
    throw "Source hash mismatch.`n  expected $ExpectedSourceHash`n  found    $sourceHash"
}

Write-Host "Source verified: $sourceHash" -ForegroundColor Green

# --- Gate 3: the rollback exists and is readable ------------------------------

$backupBundle = Join-Path $BackupDir 'AuroraVerb.vst3'
$backupHash = Get-BundleHash $backupBundle

if (-not $backupHash) { throw "No usable rollback bundle at '$backupBundle'." }

Write-Host "Rollback ready:  $backupHash" -ForegroundColor Green
Write-Host "                 $backupBundle"

# --- Install ------------------------------------------------------------------

if ($PSCmdlet.ShouldProcess($Target, "replace with the production artefact")) {

    if (Test-Path -LiteralPath $Target) {
        Remove-Item -LiteralPath $Target -Recurse -Force
    }

    Copy-Item -LiteralPath $Source -Destination $Target -Recurse -Force

    $installedHash = Get-BundleHash $Target

    if ($installedHash -ne $ExpectedSourceHash) {
        Write-Warning "Installed hash is wrong. Rolling back."
        Write-Warning "  expected $ExpectedSourceHash"
        Write-Warning "  found    $installedHash"

        if (Test-Path -LiteralPath $Target) {
            Remove-Item -LiteralPath $Target -Recurse -Force
        }

        Copy-Item -LiteralPath $backupBundle -Destination $Target -Recurse -Force

        $restored = Get-BundleHash $Target

        if ($restored -ne $backupHash) {
            throw "ROLLBACK ALSO FAILED. Restore '$backupBundle' by hand before opening any host."
        }

        throw "Install failed and was rolled back. Restored $restored. Do not open a host until this is understood."
    }

    Write-Host ""
    Write-Host "Installed: $installedHash" -ForegroundColor Green
    Write-Host "at $Target"
    Write-Host ""
    Write-Host "Rollback stays at:" -ForegroundColor Yellow
    Write-Host "  $backupBundle"
}
