<#
    Regression tests for the diagnostic session selection.

    The defect these exist for: the analyzer picked "the newest folder by
    LastWriteTime", and Windows does not update a directory's timestamp while a
    file inside it is open. A live session therefore looked older than a
    finished one, so the rule failed exactly when it mattered. Two of the cases
    below reproduce that directly, on real folders with real timestamps, so a
    return to LastWriteTime cannot pass.

    No plugin, no build, no audio. Run it directly:
        .\Scripts\Test-SessionSelection.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "Select-DiagnosticSession.ps1")

$script:passed = 0
$script:failed = 0

function Check {
    param([bool] $Condition, [string] $Description, [string] $Detail = "")

    if ($Condition) {
        $script:passed++
        Write-Host "  PASS  $Description" -ForegroundColor Green
    } else {
        $script:failed++
        Write-Host "  FAIL  $Description" -ForegroundColor Red
        if ($Detail) { Write-Host "        $Detail" -ForegroundColor Red }
    }
}

function New-Scratch {
    param([string] $Name)

    $path = Join-Path ([System.IO.Path]::GetTempPath()) "av-session-selection\$Name"

    if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path }
    New-Item -ItemType Directory -Force -Path $path | Out-Null

    return $path
}

function New-Session {
    param(
        [string] $Root,
        [string] $Name,
        [switch] $Finished,          # writes summary.txt
        [datetime] $LastWrite,       # forced directory timestamp
        [int] $TimelineBytes = 100
    )

    $path = Join-Path $Root $Name
    New-Item -ItemType Directory -Force -Path $path | Out-Null

    Set-Content -LiteralPath (Join-Path $path "timeline.csv") `
                -Value ("x" * $TimelineBytes) -Encoding ascii

    if ($Finished) {
        Set-Content -LiteralPath (Join-Path $path "summary.txt") -Value "done" -Encoding ascii
    }

    if ($LastWrite) {
        (Get-Item -LiteralPath $path).LastWriteTime = $LastWrite
    }

    return $path
}

Write-Host ""
Write-Host "Diagnostic session selection"
Write-Host "----------------------------"

# =========================================================================
# 1. An older folder NAME carrying a newer LastWriteTime.
#
#    This is the shape of the real bug: the finished session was touched most
#    recently, the live one was not, and LastWriteTime picked the finished one.
# =========================================================================
{
    $root = New-Scratch "older-name-newer-stamp"

    $old = New-Session -Root $root -Name "2026-08-05_10-00-00_aaaaaaaa" -Finished `
                       -LastWrite (Get-Date "2026-08-05 23:59:00")
    $new = New-Session -Root $root -Name "2026-08-05_20-00-00_bbbbbbbb" -Finished `
                       -LastWrite (Get-Date "2026-08-05 10:00:00")

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0

    Check ($r.Status -eq 'selected') "an older name with a newer stamp still resolves"
    Check ($r.Path -eq $new) `
          "the newer folder NAME wins over the newer LastWriteTime" `
          "got $($r.Path)"
}.Invoke()

# =========================================================================
# 2. The mirror image: newer name, older stamp.
# =========================================================================
{
    $root = New-Scratch "newer-name-older-stamp"

    $a = New-Session -Root $root -Name "2026-08-05_08-00-00_cccccccc" -Finished `
                     -LastWrite (Get-Date "2026-08-06 06:00:00")
    $b = New-Session -Root $root -Name "2026-08-05_09-30-00_dddddddd" -Finished `
                     -LastWrite (Get-Date "2026-08-04 01:00:00")

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0

    Check ($r.Path -eq $b) `
          "a newer name with an older stamp is still the newest session" `
          "got $($r.Path)"
}.Invoke()

# =========================================================================
# 3. One live session among finished ones.
# =========================================================================
{
    $root = New-Scratch "single-live"

    New-Session -Root $root -Name "2026-08-05_10-00-00_eeeeeeee" -Finished | Out-Null
    New-Session -Root $root -Name "2026-08-05_11-00-00_ffffffff" -Finished | Out-Null

    # Deliberately the OLDEST name, so a "newest folder" rule of any kind would
    # get this wrong. Live beats new.
    $live = New-Session -Root $root -Name "2026-08-05_09-00-00_11111111"

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0

    Check ($r.Status -eq 'selected') "a single live session resolves"
    Check ($r.Path -eq $live) `
          "the live session wins even when its name is the oldest" `
          "got $($r.Path)"
    Check ($r.Reason -match 'live') "and the reason says why"
}.Invoke()

# =========================================================================
# 4. Several live, only one actually being written.
#
#    A host scan leaves a folder that never got a summary. Only watching for
#    growth tells it apart from the session that matters.
# =========================================================================
{
    $root = New-Scratch "one-growing"

    $scan = New-Session -Root $root -Name "2026-08-05_12-00-00_22222222"
    $real = New-Session -Root $root -Name "2026-08-05_12-00-01_33333333"

    $calls = @{}
    $probe = {
        param($path)
        if (-not $calls.ContainsKey($path)) { $calls[$path] = 0 }
        $calls[$path]++
        # Only the real session's timeline grows between the two probes.
        if ($path -eq $real) { return 100 * $calls[$path] }
        return 100
    }.GetNewClosure()

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0 -SizeProbe $probe

    Check ($r.Status -eq 'selected') "two live sessions, one growing, resolves"
    Check ($r.Path -eq $real) `
          "the growing session is chosen over the abandoned scan folder" `
          "got $($r.Path)"
}.Invoke()

# =========================================================================
# 5. Several live and several growing: refuse.
# =========================================================================
{
    $root = New-Scratch "two-growing"

    New-Session -Root $root -Name "2026-08-05_13-00-00_44444444" | Out-Null
    New-Session -Root $root -Name "2026-08-05_13-00-01_55555555" | Out-Null

    $calls = @{}
    $probe = {
        param($path)
        if (-not $calls.ContainsKey($path)) { $calls[$path] = 0 }
        $calls[$path]++
        return 100 * $calls[$path]        # both grow
    }.GetNewClosure()

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0 -SizeProbe $probe

    Check ($r.Status -eq 'ambiguous') `
          "two sessions being written at once is refused, not guessed at" `
          "got $($r.Status)"
    Check ($r.Growing.Count -eq 2) "and both are named in the refusal"
    Check ($null -eq $r.Path) "and no path is offered"
}.Invoke()

# =========================================================================
# 5b. Several live and NONE growing: also refuse.
#
#     Not in the original rule list, and worth stating: these are abandoned
#     sessions with no summary, and picking one would be a guess dressed up as
#     an answer.
# =========================================================================
{
    $root = New-Scratch "none-growing"

    New-Session -Root $root -Name "2026-08-05_14-00-00_66666666" | Out-Null
    New-Session -Root $root -Name "2026-08-05_14-00-01_77777777" | Out-Null

    $r = Select-DiagnosticSession -Root $root -GrowthCheckSeconds 0

    Check ($r.Status -eq 'ambiguous') `
          "several stalled live sessions are refused rather than guessed at" `
          "got $($r.Status)"
}.Invoke()

# =========================================================================
# 6. An explicit -Session bypasses all of it.
#
#    Checked against the analyzer itself, because that is where the bypass
#    lives: the selector is never called at all.
# =========================================================================
{
    $analyzer = Get-Content -LiteralPath (Join-Path $PSScriptRoot "analyze-diagnostic-session.ps1") -Raw

    $selectorInsideElse = $analyzer -match '(?s)if \(\$Session\) \{.*?\} else \{.*?Select-DiagnosticSession'

    Check $selectorInsideElse `
          "the selector is only reached when -Session was not given"

    Check ($analyzer -notmatch 'Sort-Object LastWriteTime') `
          "the analyzer no longer sorts sessions by LastWriteTime anywhere"

    Check ($analyzer -match 'chosen by:') `
          "the analyzer prints how the session was chosen, before analysing it"
}.Invoke()

# =========================================================================
# 7. Nothing to choose from.
# =========================================================================
{
    $empty = New-Scratch "empty"

    $r = Select-DiagnosticSession -Root $empty -GrowthCheckSeconds 0

    Check ($r.Status -eq 'none') "an empty root reports nothing rather than failing"
    Check ($null -eq $r.Path) "and offers no path"

    $missing = Join-Path ([System.IO.Path]::GetTempPath()) "av-session-selection\does-not-exist"
    $r2 = Select-DiagnosticSession -Root $missing -GrowthCheckSeconds 0

    Check ($r2.Status -eq 'none') "a missing root reports nothing"
    Check ($r2.Reason -match 'no diagnostic root') "and says which root it looked in"
}.Invoke()

# --- Clean up ---------------------------------------------------------------

$scratchRoot = Join-Path ([System.IO.Path]::GetTempPath()) "av-session-selection"
if (Test-Path -LiteralPath $scratchRoot) { Remove-Item -Recurse -Force -LiteralPath $scratchRoot }

Write-Host ""
Write-Host "  $script:passed passed, $script:failed failed."

if ($script:failed -gt 0) { exit 1 }
exit 0
