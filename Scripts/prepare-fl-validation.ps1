<#
    Makes a validation VST3 discoverable in FL Studio without installing it.

    This is the development / validation scan-path workflow, repaired. It does
    not copy the plugin anywhere by default, does not touch
    C:\Program Files\Common Files\VST3, and does not need an elevated shell.

    What went wrong, and what this fixes
    ------------------------------------
    FL Studio keeps its custom plugin search paths in the registry, under

        HKCU\Software\Image-Line\Shared\Plugin search paths\List

    as one numbered subkey per path plus a `Count`. Two entries had been added
    for AuroraVerb and neither could work:

      - one pointed at a path that does not exist, with the bundle name
        doubled:  ...\VST3\AuroraVerb.vst3\AuroraVerb.vst3\
      - one pointed AT the bundle:  ...\VST3\AuroraVerb.vst3\

    A VST3 host scans a FOLDER for `.vst3` bundles inside it. Given the bundle
    itself, it looks inside and finds `Contents`, which is not a bundle, so it
    finds nothing and reports nothing - there is no error to show, because
    nothing failed. The path FL needs is the bundle's PARENT:

        C:\...\RelWithDebInfo\VST3\

    This script replaces any AuroraVerb build-tree entry with that one path,
    leaves every unrelated search path untouched, and fixes `Count`.

    Why it refuses to run while FL Studio is open
    ---------------------------------------------
    FL writes this whole key back from memory when it exits. An edit made while
    it is running is silently discarded, which looks exactly like the edit not
    working. So the script checks, and says so, rather than appearing to succeed.

    -Junction
    ---------
    The fallback, if FL still will not pick the folder up. It creates a
    directory junction at

        %LOCALAPPDATA%\Programs\Common\VST3\AuroraVerb.vst3

    pointing at the build bundle. That is the per-user VST3 folder - the one
    README documents as where a development build lands, not the machine-wide
    production folder - and a junction needs no elevation. Because it is a link
    and not a copy, the thing the host loads IS the validation artefact: the
    hash cannot drift, and `-Remove` leaves no stale binary behind.

    Usage:
        .\Scripts\prepare-fl-validation.ps1
        .\Scripts\prepare-fl-validation.ps1 -Junction
        .\Scripts\prepare-fl-validation.ps1 -Remove
#>

[CmdletBinding()]
param(
    [string] $Bundle = 'C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3',

    # Naming the hash is the point: this script exists to make ONE artefact
    # reachable, and an artefact that does not hash as expected is a different
    # one. Pass '' only when deliberately pointing it at a fresh build.
    [string] $ExpectedHash = '2C52D78F701A3024167A5651A6A90D5F5ADB971B147B30871631365715BA6151',

    [switch] $Junction,
    [switch] $Remove,

    # The list-rewriting step deletes and recreates registry subkeys, which is
    # not something to test for the first time on the real key. Pointing this at
    # a scratch copy exercises the same code path against throwaway data.
    [string] $SearchKey = 'HKCU:\Software\Image-Line\Shared\Plugin search paths\List',

    # Skips the FL-is-running guard. Only for a scratch-key dry run; FL discards
    # edits to its own key made while it is open, so this is never useful there.
    [switch] $SkipRunningCheck
)

$ErrorActionPreference = "Stop"

$searchKey = $SearchKey
$userVst3  = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$productionVst3 = 'C:\Program Files\Common Files\VST3'

function Write-Section([string] $title) {
    Write-Host ""
    Write-Host "--- $title " -NoNewline
    Write-Host ("-" * [Math]::Max(1, 68 - $title.Length))
}

# ---------------------------------------------------------------------------
#  1. Is FL Studio running?
# ---------------------------------------------------------------------------

$flProcesses = if ($SkipRunningCheck) { $null }
               else { Get-Process -Name 'FL64', 'FL', 'FL (scaled)', 'FL64 (scaled)' -ErrorAction SilentlyContinue }

if ($flProcesses) {
    Write-Host "FL Studio is running:" -ForegroundColor Yellow
    $flProcesses | ForEach-Object { Write-Host "  $($_.ProcessName)  pid $($_.Id)  started $($_.StartTime)" }
    Write-Host ""
    Write-Host "FL rewrites its search-path registry key from memory when it exits," -ForegroundColor Yellow
    Write-Host "so an edit made now would be discarded without any error." -ForegroundColor Yellow
    throw "Close FL Studio and run this again."
}

# ---------------------------------------------------------------------------
#  2. Verify the artefact
# ---------------------------------------------------------------------------

Write-Section "the artefact"

if (-not (Test-Path -LiteralPath $Bundle -PathType Container)) {
    throw "Bundle not found: $Bundle"
}

$binary = Join-Path $Bundle 'Contents\x86_64-win\AuroraVerb.vst3'

if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Bundle has no Windows binary at Contents\x86_64-win\AuroraVerb.vst3"
}

$parent = Split-Path -Parent $Bundle
$info = Get-Item -LiteralPath $binary
$hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash

Write-Host "  bundle   $Bundle"
Write-Host "  parent   $parent"
Write-Host "  binary   $($info.Length) bytes, $($info.LastWriteTime)"
Write-Host "  sha256   $hash"

if ($ExpectedHash -and $hash -ne $ExpectedHash.ToUpper()) {
    Write-Host "  expected $($ExpectedHash.ToUpper())" -ForegroundColor Red
    throw "Hash mismatch. This is not the artefact this script was pointed at."
}

if ($ExpectedHash) { Write-Host "  hash matches." -ForegroundColor Green }

# ---------------------------------------------------------------------------
#  3. The search-path list, before
# ---------------------------------------------------------------------------

Write-Section "FL Studio custom search paths, before"

if (-not (Test-Path -LiteralPath $searchKey)) {
    throw "FL Studio search-path key not found at $searchKey. Has FL Studio ever run?"
}

function Get-SearchPathEntries {
    $out = @()

    foreach ($sub in (Get-ChildItem -LiteralPath $searchKey -ErrorAction SilentlyContinue | Sort-Object { [int] $_.PSChildName })) {
        $k = Get-Item -LiteralPath $sub.PSPath

        $out += [pscustomobject]@{
            Index     = [int] $sub.PSChildName
            Path      = [string] $k.GetValue('path')
            Custom    = [int] $k.GetValue('custom')
            PlugClass = [int] $k.GetValue('plugclass')
            Selected  = [int] $k.GetValue('selected')
        }
    }

    return $out
}

$before = Get-SearchPathEntries
$countBefore = (Get-ItemProperty -LiteralPath $searchKey).Count

foreach ($e in $before) {
    # %FLPath% is FL's own token for its install folder; it is not a broken path.
    $exists = if ($e.Path -like '*%*') { 'token' }
              elseif (Test-Path -LiteralPath $e.Path) { 'exists' }
              else { 'MISSING' }

    Write-Host ("  [{0}] custom={1} class={2} sel={3} {4,-8} {5}" -f `
                $e.Index, $e.Custom, $e.PlugClass, $e.Selected, $exists, $e.Path)
}

Write-Host "  Count value = $countBefore, subkeys = $($before.Count)"

if ($countBefore -ne $before.Count) {
    Write-Host "  Count disagrees with the number of entries; it will be corrected." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
#  4. Back the key up before touching it
# ---------------------------------------------------------------------------

$backupDir = Join-Path $env:LOCALAPPDATA 'AuroraVerb-fl-backups'
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$backupFile = Join-Path $backupDir "plugin-search-paths-$stamp.reg"

# Back up the parent of the list, so `Count` and the subkeys are captured
# together. Derived from $searchKey rather than hardcoded, so a scratch-key dry
# run backs up the scratch key and not FL's real one.
$exportTarget = (Split-Path -Parent $searchKey) -replace '^HKCU:\\', 'HKCU\'

& reg.exe export $exportTarget $backupFile /y | Out-Null

if (-not (Test-Path -LiteralPath $backupFile)) {
    throw "Could not back the registry key up to $backupFile. Nothing was changed."
}

Write-Host ""
Write-Host "  backup   $backupFile"

# ---------------------------------------------------------------------------
#  5. Decide what belongs to us
# ---------------------------------------------------------------------------

# Only entries pointing inside an AuroraVerb build tree are ours to touch.
# Everything else - FL's own folders, the VST2 folder, anything the user added
# for another plugin - is left exactly as it is.
$buildRootPattern = '*AuroraVerb_artefacts*'

$ours  = @($before | Where-Object { $_.Path -like $buildRootPattern })
$keep  = @($before | Where-Object { $_.Path -notlike $buildRootPattern })

Write-Section "what will change"

if ($ours.Count -eq 0) {
    Write-Host "  no existing AuroraVerb build-tree entries"
} else {
    foreach ($e in $ours) { Write-Host "  remove  $($e.Path)" }
}

$wanted = $parent.TrimEnd('\') + '\'

if ($Remove) {
    Write-Host "  -Remove given: no AuroraVerb path will be added back"
} else {
    Write-Host "  add     $wanted"
}

Write-Host "  untouched: $($keep.Count) unrelated path(s)"

# ---------------------------------------------------------------------------
#  6. Rewrite the list
# ---------------------------------------------------------------------------

$final = @()

foreach ($e in $keep) { $final += $e }

if (-not $Remove) {
    $final += [pscustomobject]@{
        Index     = -1
        Path      = $wanted
        Custom    = 1
        PlugClass = 0
        Selected  = 1
    }
}

# FL expects subkeys numbered 0..N-1 with no gaps, so the list is rewritten
# from scratch rather than patched in place.
foreach ($sub in (Get-ChildItem -LiteralPath $searchKey)) {
    Remove-Item -LiteralPath $sub.PSPath -Recurse -Force
}

for ($i = 0; $i -lt $final.Count; $i++) {
    $e = $final[$i]
    $path = Join-Path $searchKey $i

    New-Item -Path $path -Force | Out-Null

    New-ItemProperty -LiteralPath $path -Name 'path'      -Value $e.Path      -PropertyType String | Out-Null
    New-ItemProperty -LiteralPath $path -Name 'custom'    -Value $e.Custom    -PropertyType DWord  | Out-Null
    New-ItemProperty -LiteralPath $path -Name 'plugclass' -Value $e.PlugClass -PropertyType DWord  | Out-Null
    New-ItemProperty -LiteralPath $path -Name 'selected'  -Value $e.Selected  -PropertyType DWord  | Out-Null
}

Set-ItemProperty -LiteralPath $searchKey -Name 'Count' -Value $final.Count -Type DWord

Write-Section "FL Studio custom search paths, after"

foreach ($e in (Get-SearchPathEntries)) {
    $exists = if ($e.Path -like '*%*') { 'token' }
              elseif (Test-Path -LiteralPath $e.Path) { 'exists' }
              else { 'MISSING' }

    Write-Host ("  [{0}] custom={1} class={2} sel={3} {4,-8} {5}" -f `
                $e.Index, $e.Custom, $e.PlugClass, $e.Selected, $exists, $e.Path)
}

Write-Host "  Count value = $((Get-ItemProperty -LiteralPath $searchKey).Count)"

# ---------------------------------------------------------------------------
#  7. The junction, if asked for
# ---------------------------------------------------------------------------

$junctionPath = Join-Path $userVst3 'AuroraVerb.vst3'

if ($Remove) {
    if (Test-Path -LiteralPath $junctionPath) {
        $item = Get-Item -LiteralPath $junctionPath -Force

        if ($item.LinkType) {
            # A junction is removed with rmdir, which deletes the link and not
            # the target. Remove-Item on a reparse point has historically
            # followed it, so cmd's rmdir is used deliberately.
            & cmd.exe /c rmdir "`"$junctionPath`"" | Out-Null
            Write-Host ""
            Write-Host "  removed junction $junctionPath"
        } else {
            Write-Host ""
            Write-Host "  $junctionPath is a real folder, not a junction." -ForegroundColor Yellow
            Write-Host "  Not removing it: this script did not create it." -ForegroundColor Yellow
        }
    }
} elseif ($Junction) {
    Write-Section "junction into the per-user VST3 folder"

    if ($userVst3 -like "$productionVst3*") {
        throw "Refusing: the junction target resolves inside the production VST3 folder."
    }

    New-Item -ItemType Directory -Force -Path $userVst3 | Out-Null

    if (Test-Path -LiteralPath $junctionPath) {
        $item = Get-Item -LiteralPath $junctionPath -Force

        if (-not $item.LinkType) {
            throw "$junctionPath already exists as a real folder. Move or delete it by hand first; this script will not remove a directory it did not create."
        }

        & cmd.exe /c rmdir "`"$junctionPath`"" | Out-Null
    }

    & cmd.exe /c mklink /J "`"$junctionPath`"" "`"$Bundle`"" | Out-Null

    if (-not (Test-Path -LiteralPath $junctionPath)) {
        throw "mklink did not create $junctionPath"
    }

    # Prove the link reaches the artefact, by hashing through it.
    $linkedBinary = Join-Path $junctionPath 'Contents\x86_64-win\AuroraVerb.vst3'
    $linkedHash = (Get-FileHash -LiteralPath $linkedBinary -Algorithm SHA256).Hash

    Write-Host "  junction $junctionPath"
    Write-Host "  target   $Bundle"
    Write-Host "  sha256 through the junction  $linkedHash"

    if ($linkedHash -ne $hash) {
        throw "The junction does not reach the artefact that was verified."
    }

    Write-Host "  the host will load the verified artefact itself, not a copy." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
#  8. Confirm nothing was installed where it must not be
# ---------------------------------------------------------------------------

Write-Section "production folder, untouched"

if (Test-Path -LiteralPath $productionVst3) {
    Get-ChildItem -LiteralPath $productionVst3 -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "  - $($_.Name)" }
} else {
    Write-Host "  $productionVst3 does not exist on this machine"
}

Write-Section "next"

if ($Remove) {
    Write-Host "  Start FL Studio. AuroraVerb will no longer be found."
} else {
    Write-Host "  1. Start FL Studio."
    Write-Host "  2. Options > Manage plugins."
    Write-Host "  3. Confirm the path below is listed and ticked:"
    Write-Host "       $wanted"
    Write-Host "  4. Press 'Find more plugins'. Leave 'Verify plugins' ON for the"
    Write-Host "     first pass so a plugin that fails to load is reported rather"
    Write-Host "     than skipped silently."
    Write-Host "  5. AuroraVerb appears under Effects. If it does not, re-run this"
    Write-Host "     script with -Junction and scan again."
    Write-Host ""
    Write-Host "  This build has AV_DEV_DIAGNOSTICS=ON, so every instantiation"
    Write-Host "  writes a session folder under %USERPROFILE%\AuroraVerbDiagnostics."
    Write-Host "  That is the validation profile behaving as designed, not a fault."
}

Write-Host ""
Write-Host "  To undo:  .\Scripts\prepare-fl-validation.ps1 -Remove"
Write-Host "  Registry backup: $backupFile"
Write-Host ""
