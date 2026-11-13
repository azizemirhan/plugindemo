<#
    Makes a validation VST3 discoverable in FL Studio without installing it.

    It does not copy the plugin. It creates a directory junction in the system
    VST3 folder pointing at the build tree, so the host loads the validation
    artefact ITSELF - the bytes under test cannot drift from the bytes that
    were verified, and removing the link leaves nothing behind.

    Why the system folder, and not a search path
    --------------------------------------------
    FL Studio's custom "Plugin search paths" are for VST1/VST2. A VST3 has to
    be found in the standard Windows VST3 location:

        C:\Program Files\Common Files\VST3

    Two earlier attempts therefore could not have worked, whatever was in them:

      1. Custom search paths pointing into the build tree. Both were also at
         the wrong level - one at a path that did not exist, one at the bundle
         instead of its parent folder - but the level was never the deciding
         problem for a VST3.
      2. A junction in the per-user VST3 folder,
         %LOCALAPPDATA%\Programs\Common\VST3. That folder is a standard VST3
         location by the specification and some hosts scan it; this FL Studio
         did not find the plugin there.

    So the junction goes in the system folder. That needs Administrator, and
    this script elevates for exactly one command - the mkdir and mklink pair,
    or the rmdir - and does every verification unelevated.

    What it refuses to do
    ---------------------
    It will not overwrite anything. If AuroraVerb.vst3 already exists in the
    system folder as a real directory, it stops and prints that directory's
    hash, because a real directory there is an installed plugin and this script
    is not an installer. It only ever replaces a junction it can see points
    into an AuroraVerb build tree.

    -Remove never deletes the parent VST3 folder, only the junction.

    Usage:
        .\Scripts\prepare-fl-validation.ps1
        .\Scripts\prepare-fl-validation.ps1 -Remove
#>

[CmdletBinding()]
param(
    [string] $Bundle = 'C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3',

    # Naming the hash is the point: this script exists to make ONE artefact
    # reachable, and an artefact that does not hash as expected is a different
    # one. Pass '' only when deliberately pointing it at a fresh build.
    [string] $ExpectedHash = '2C52D78F701A3024167A5651A6A90D5F5ADB971B147B30871631365715BA6151',

    [switch] $Remove,

    # Leave the superseded per-user junction in place. Off by default: two
    # validation paths for one artefact is a thing to be confused by later.
    [switch] $KeepUserJunction,

    # Leave the AuroraVerb custom search-path entries in FL's registry. Off by
    # default, because a custom search path cannot find a VST3 and an entry
    # that cannot work is worse than no entry - it looks like provision.
    [switch] $KeepScanPaths,

    [string] $SearchKey = 'HKCU:\Software\Image-Line\Shared\Plugin search paths\List',

    # Overridable so the whole script can be exercised against a scratch
    # directory before it is pointed at Program Files.
    [string] $SystemVst3Dir = (Join-Path $env:CommonProgramFiles 'VST3'),

    # Only for a dry run against a scratch registry key.
    [switch] $SkipRunningCheck
)

$ErrorActionPreference = "Stop"

$systemVst3 = $SystemVst3Dir
$systemLink = Join-Path $systemVst3 'AuroraVerb.vst3'
$userVst3   = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$userLink   = Join-Path $userVst3 'AuroraVerb.vst3'

function Write-Section([string] $title) {
    Write-Host ""
    Write-Host "--- $title " -NoNewline
    Write-Host ("-" * [Math]::Max(1, 68 - $title.Length))
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal] $id).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

<#
    Can this user create and delete entries in `dir` without elevating?

    Asked rather than assumed. Program Files is not writable for an ordinary
    user, so the answer is normally no and the next step elevates - but a
    machine where that folder has been opened up, or a scratch directory used
    to exercise this script, should not raise a pointless UAC prompt.
#>
function Test-DirectoryWritable([string] $dir) {
    if (-not (Test-Path -LiteralPath $dir)) {
        # It does not exist yet, so the question is whether its parent allows
        # creating it. Walk up to the nearest ancestor that does exist.
        $probeParent = Split-Path -Parent $dir

        while ($probeParent -and -not (Test-Path -LiteralPath $probeParent)) {
            $probeParent = Split-Path -Parent $probeParent
        }

        if (-not $probeParent) { return $false }

        $dir = $probeParent
    }

    $probe = Join-Path $dir (".auroraverb-write-probe-" + [Guid]::NewGuid().ToString('N'))

    try {
        New-Item -ItemType Directory -Path $probe -ErrorAction Stop | Out-Null
        Remove-Item -LiteralPath $probe -Force -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

<#
    Runs one cmd command, elevating only when the target actually needs it.

    Elevation is scoped to this single call on purpose: everything that reads,
    hashes or verifies runs as the ordinary user, so the only thing
    Administrator is ever used for is the one write under Program Files.
#>
function Invoke-Privileged([string] $inner, [string] $what, [bool] $needsElevation) {
    Write-Host "  $what"
    Write-Host "    cmd: $inner"

    if (-not $needsElevation) {
        $p = Start-Process -FilePath cmd.exe -ArgumentList '/c', $inner -Wait -PassThru -NoNewWindow
    } else {
        Write-Host "    elevating for this one command - Windows will ask" -ForegroundColor Yellow
        $p = Start-Process -FilePath cmd.exe -ArgumentList '/c', $inner -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    }

    if ($p.ExitCode -ne 0) {
        throw "$what failed (cmd exit code $($p.ExitCode))."
    }
}

<# Describes whatever is at a path: absent, a junction (with its target), or a real directory. #>
function Get-LinkState([string] $path) {
    if (-not (Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ Kind = 'absent'; Target = $null }
    }

    $item = Get-Item -LiteralPath $path -Force

    if ($item.LinkType -eq 'Junction' -or $item.LinkType -eq 'SymbolicLink') {
        return [pscustomobject]@{ Kind = 'junction'; Target = ($item.Target | Select-Object -First 1) }
    }

    return [pscustomobject]@{ Kind = 'directory'; Target = $null }
}

function Get-BundleHash([string] $bundlePath) {
    $binary = Join-Path $bundlePath 'Contents\x86_64-win\AuroraVerb.vst3'

    if (-not (Test-Path -LiteralPath $binary)) { return $null }

    return (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
}

# ---------------------------------------------------------------------------
#  1. FL Studio must be closed
# ---------------------------------------------------------------------------

$flProcesses = if ($SkipRunningCheck) { $null }
               else { Get-Process -Name 'FL64', 'FL', 'FL (scaled)', 'FL64 (scaled)' -ErrorAction SilentlyContinue }

if ($flProcesses) {
    Write-Host "FL Studio is running:" -ForegroundColor Yellow
    $flProcesses | ForEach-Object { Write-Host "  $($_.ProcessName)  pid $($_.Id)" }
    Write-Host ""
    Write-Host "A plugin folder must not change under a running host, and FL also" -ForegroundColor Yellow
    Write-Host "rewrites its search-path registry key from memory when it exits." -ForegroundColor Yellow
    throw "Close FL Studio and run this again."
}

# ---------------------------------------------------------------------------
#  2. Verify the source artefact
# ---------------------------------------------------------------------------

Write-Section "the validation artefact"

if (-not $Remove) {
    if (-not (Test-Path -LiteralPath $Bundle -PathType Container)) {
        throw "Bundle not found: $Bundle"
    }

    $binary = Join-Path $Bundle 'Contents\x86_64-win\AuroraVerb.vst3'

    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Bundle has no Windows binary at Contents\x86_64-win\AuroraVerb.vst3"
    }

    $info = Get-Item -LiteralPath $binary
    $sourceHash = Get-BundleHash $Bundle

    Write-Host "  source   $Bundle"
    Write-Host "  binary   $($info.Length) bytes, $($info.LastWriteTime)"
    Write-Host "  sha256   $sourceHash"

    if ($ExpectedHash -and $sourceHash -ne $ExpectedHash.ToUpper()) {
        Write-Host "  expected $($ExpectedHash.ToUpper())" -ForegroundColor Red
        throw "Hash mismatch. This is not the artefact this script was pointed at."
    }

    if ($ExpectedHash) { Write-Host "  source hash matches." -ForegroundColor Green }
} else {
    Write-Host "  -Remove: the source is not touched and is not re-verified."
    $sourceHash = $null
}

# ---------------------------------------------------------------------------
#  3. The system junction
# ---------------------------------------------------------------------------

Write-Section "system VST3 folder"

$needsElevation = -not (Test-DirectoryWritable $systemVst3)

Write-Host "  folder   $systemVst3"
Write-Host "  link     $systemLink"
Write-Host ("  access   {0}" -f $(if ($needsElevation) { "not writable by this user - will elevate for one command" }
                                 else { "writable by this user - no elevation needed" }))

if ((Test-Admin) -and $needsElevation) {
    # Already elevated, so the probe failing means something other than
    # privilege is in the way; say so rather than raising a UAC prompt that
    # cannot help.
    Write-Host "  this shell is already elevated, yet the folder is not writable." -ForegroundColor Yellow
    $needsElevation = $false
}

$state = Get-LinkState $systemLink

switch ($state.Kind) {
    'absent'    { Write-Host "  state    nothing there" }
    'junction'  { Write-Host "  state    junction -> $($state.Target)" }
    'directory' {
        $existingHash = Get-BundleHash $systemLink
        Write-Host "  state    a REAL DIRECTORY, not a link" -ForegroundColor Red
        Write-Host "  sha256   $existingHash" -ForegroundColor Red
    }
}

if ($state.Kind -eq 'directory') {
    throw @"
$systemLink is a real directory, which means a plugin is installed there.
This script does not overwrite an installed plugin, and will not remove a
directory it did not create. Deal with it deliberately, by hand, first.
"@
}

if ($state.Kind -eq 'junction' -and $state.Target -notlike '*AuroraVerb_artefacts*') {
    throw @"
$systemLink is a junction, but it points somewhere this script does not
recognise as an AuroraVerb build tree:
  $($state.Target)
Refusing to replace it.
"@
}

if ($Remove) {
    if ($state.Kind -eq 'junction') {
        # rmdir deletes the link and not its target. Remove-Item on a reparse
        # point has historically followed it, so cmd's rmdir is deliberate.
        Invoke-Privileged ('rmdir "{0}"' -f $systemLink) "removing the system junction" $needsElevation
    } else {
        Write-Host "  nothing to remove"
    }
} else {
    if ($state.Kind -eq 'junction' -and $state.Target -eq $Bundle) {
        Write-Host "  already points at this exact bundle; leaving it alone"
    } else {
        $inner = if ($state.Kind -eq 'junction') {
            'rmdir "{0}" && mklink /J "{0}" "{1}"' -f $systemLink, $Bundle
        } else {
            'if not exist "{0}" mkdir "{0}" && mklink /J "{1}" "{2}"' -f $systemVst3, $systemLink, $Bundle
        }

        Invoke-Privileged $inner "creating the system junction" $needsElevation
    }
}

# ---------------------------------------------------------------------------
#  4. Verify through the junction
# ---------------------------------------------------------------------------

if (-not $Remove) {
    Write-Section "verification, through the junction"

    $after = Get-LinkState $systemLink

    if ($after.Kind -ne 'junction') {
        throw "Expected a junction at $systemLink but found '$($after.Kind)'."
    }

    $throughHash = Get-BundleHash $systemLink

    Write-Host "  link     $systemLink"
    Write-Host "  resolves $($after.Target)"
    Write-Host "  sha256   $throughHash"

    if ($throughHash -ne $sourceHash) {
        throw "The junction does not reach the artefact that was verified."
    }

    Write-Host "  the host will load the verified artefact itself, not a copy." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
#  5. The superseded per-user junction
# ---------------------------------------------------------------------------

Write-Section "per-user VST3 folder"

$userState = Get-LinkState $userLink

Write-Host "  link     $userLink"

switch ($userState.Kind) {
    'absent'    { Write-Host "  state    nothing there" }
    'junction'  { Write-Host "  state    junction -> $($userState.Target)" }
    'directory' { Write-Host "  state    a real directory - left alone" -ForegroundColor Yellow }
}

if ($userState.Kind -eq 'junction') {
    if ($KeepUserJunction -and -not $Remove) {
        Write-Host "  -KeepUserJunction given; leaving it"
    } elseif ($userState.Target -notlike '*AuroraVerb_artefacts*') {
        Write-Host "  points outside an AuroraVerb build tree; leaving it" -ForegroundColor Yellow
    } else {
        # The per-user folder is writable by this user, so no elevation here.
        & cmd.exe /c rmdir "`"$userLink`"" | Out-Null

        if (Test-Path -LiteralPath $userLink) {
            Write-Host "  could not remove it" -ForegroundColor Yellow
        } else {
            Write-Host "  removed, so only one validation path remains" -ForegroundColor Green
        }
    }
}

# ---------------------------------------------------------------------------
#  6. The custom search-path entries, which cannot work for a VST3
# ---------------------------------------------------------------------------

Write-Section "FL Studio custom search paths"

if ($KeepScanPaths) {
    Write-Host "  -KeepScanPaths given; the registry is not touched"
} elseif (-not (Test-Path -LiteralPath $SearchKey)) {
    Write-Host "  key not found: $SearchKey"
} else {
    function Get-SearchPathEntries {
        $out = @()

        foreach ($sub in (Get-ChildItem -LiteralPath $SearchKey -ErrorAction SilentlyContinue | Sort-Object { [int] $_.PSChildName })) {
            $k = Get-Item -LiteralPath $sub.PSPath

            $out += [pscustomobject]@{
                Path      = [string] $k.GetValue('path')
                Custom    = [int] $k.GetValue('custom')
                PlugClass = [int] $k.GetValue('plugclass')
                Selected  = [int] $k.GetValue('selected')
            }
        }

        return $out
    }

    $before = @(Get-SearchPathEntries)
    $ours   = @($before | Where-Object { $_.Path -like '*AuroraVerb_artefacts*' })
    $keep   = @($before | Where-Object { $_.Path -notlike '*AuroraVerb_artefacts*' })

    foreach ($e in $before) {
        $mark = if ($e.Path -like '*AuroraVerb_artefacts*') { 'remove ' } else { 'keep   ' }
        Write-Host "  $mark $($e.Path)"
    }

    if ($ours.Count -eq 0) {
        Write-Host "  no AuroraVerb entries to remove"
    } else {
        $backupDir = Join-Path $env:LOCALAPPDATA 'AuroraVerb-fl-backups'
        New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

        $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
        $backupFile = Join-Path $backupDir "plugin-search-paths-$stamp.reg"
        $exportTarget = (Split-Path -Parent $SearchKey) -replace '^HKCU:\\', 'HKCU\'

        & reg.exe export $exportTarget $backupFile /y | Out-Null

        if (-not (Test-Path -LiteralPath $backupFile)) {
            throw "Could not back the registry key up. The registry was not changed."
        }

        Write-Host "  backup   $backupFile"

        # FL expects subkeys numbered 0..N-1 with no gaps, so the list is
        # rewritten from scratch rather than patched in place.
        foreach ($sub in (Get-ChildItem -LiteralPath $SearchKey)) {
            Remove-Item -LiteralPath $sub.PSPath -Recurse -Force
        }

        for ($i = 0; $i -lt $keep.Count; $i++) {
            $e = $keep[$i]
            $path = Join-Path $SearchKey $i

            New-Item -Path $path -Force | Out-Null

            New-ItemProperty -LiteralPath $path -Name 'path'      -Value $e.Path      -PropertyType String | Out-Null
            New-ItemProperty -LiteralPath $path -Name 'custom'    -Value $e.Custom    -PropertyType DWord  | Out-Null
            New-ItemProperty -LiteralPath $path -Name 'plugclass' -Value $e.PlugClass -PropertyType DWord  | Out-Null
            New-ItemProperty -LiteralPath $path -Name 'selected'  -Value $e.Selected  -PropertyType DWord  | Out-Null
        }

        Set-ItemProperty -LiteralPath $SearchKey -Name 'Count' -Value $keep.Count -Type DWord

        Write-Host "  $($ours.Count) removed, $($keep.Count) unrelated path(s) kept, Count = $($keep.Count)"
    }
}

# ---------------------------------------------------------------------------
#  7. Where things stand
# ---------------------------------------------------------------------------

Write-Section "result"

foreach ($p in @($systemLink, $userLink)) {
    $s = Get-LinkState $p

    $desc = switch ($s.Kind) {
        'absent'    { 'absent' }
        'junction'  { "junction -> $($s.Target)" }
        'directory' { 'REAL DIRECTORY' }
    }

    Write-Host ("  {0,-56} {1}" -f $p, $desc)
}

Write-Section "next"

if ($Remove) {
    Write-Host "  AuroraVerb is no longer reachable by any host."
    Write-Host ""
    Write-Host "  The parent folder $systemVst3 was deliberately left in place;"
    Write-Host "  this script never removes it. Delete it by hand if it is empty"
    Write-Host "  and you want it gone."
} else {
    Write-Host "  1. Start FL Studio."
    Write-Host "  2. Options > Manage plugins."
    Write-Host "  3. Leave 'Verify plugins' ON, so a plugin that fails to load is"
    Write-Host "     reported instead of skipped silently."
    Write-Host "  4. Press 'Find more plugins'."
    Write-Host "     A full rescan is not needed; the folder is new to FL, so a"
    Write-Host "     normal scan finds it. FL has no cached AuroraVerb entry to"
    Write-Host "     clear - its plugin database has never recorded one."
    Write-Host "  5. AuroraVerb appears under Effects, maker IndieDSP."
    Write-Host ""
    Write-Host "  This is the validation profile, AV_DEV_DIAGNOSTICS=ON, so every"
    Write-Host "  instantiation writes a session folder under"
    Write-Host "  %USERPROFILE%\AuroraVerbDiagnostics. Designed behaviour, not a fault."
}

Write-Host ""

if ($Remove) {
    Write-Host "  To put it back:  .\Scripts\prepare-fl-validation.ps1"
} else {
    Write-Host "  To undo:  .\Scripts\prepare-fl-validation.ps1 -Remove"
}

Write-Host ""
