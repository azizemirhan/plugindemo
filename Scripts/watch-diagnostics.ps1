<#
    Live monitor for an AuroraVerb diagnostic session.

    Finds the newest session folder, follows timeline.csv, and redraws a fixed
    status view. It never talks to the plugin, let alone to its audio thread -
    it reads files that the plugin's background writer produced, which is the
    only safe way to observe a real-time process.

    Kept to plain ASCII on purpose. Windows PowerShell reads a script without a
    byte-order mark using the machine's ANSI code page, and on this machine a
    UTF-8 em dash came back as bytes that include a quote character - which
    silently turned the rest of the file into one unterminated string.

    Reads incrementally: the file position is remembered between polls, so a
    session that runs for an hour does not get re-read from the start every
    second.

    Usage:
        .\Scripts\watch-diagnostics.ps1
        .\Scripts\watch-diagnostics.ps1 -Root D:\Diagnostics -IntervalSeconds 2
#>

[CmdletBinding()]
param(
    [string] $Root = "",
    [double] $IntervalSeconds = 1.0,
    [int]    $IdleTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

if (-not $Root) {
    $base = if ($env:AV_DIAGNOSTIC_ROOT) { $env:AV_DIAGNOSTIC_ROOT }
            elseif ($env:USERPROFILE)    { $env:USERPROFILE }
            else                         { "." }

    $Root = Join-Path $base "AuroraVerbDiagnostics"
}

Write-Host "Watching for sessions under: $Root"

if (-not (Test-Path -LiteralPath $Root)) {
    Write-Host ""
    Write-Host "No diagnostic root yet. It is created when a diagnostic build first runs."
    Write-Host "Build with -DAV_DEV_DIAGNOSTICS=ON and load the plugin in a host."
    return
}

function Get-NewestSession {
    param([string] $root)

    # Sorted by folder NAME, not by LastWriteTime.
    #
    # A folder whose files are still open does not get its directory timestamp
    # updated on Windows, so a live session looks older than a finished one and
    # LastWriteTime picks the wrong folder. That is not hypothetical: FL Studio
    # instantiates the plugin once to scan it and again for the mixer insert,
    # both folders were stamped the same second, and the watcher followed the
    # scan instance, saw its summary.txt and exited within a second while the
    # real session went on writing ten thousand rows.
    #
    # The folder name starts with a fixed-width timestamp, so it sorts
    # chronologically on its own.
    $all = Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
           Sort-Object Name -Descending

    if (-not $all) { return $null }

    # A session with no summary.txt has not been closed by the host yet, so it
    # is the one worth watching. Finished sessions are only a fallback.
    $live = $all | Where-Object { -not (Test-Path -LiteralPath (Join-Path $_.FullName "summary.txt")) }

    if ($live) { return $live | Select-Object -First 1 }

    return $all | Select-Object -First 1
}

function Format-Db {
    param([double] $linear)

    if ($linear -le 0) { return "   -inf dBFS" }
    return ("{0,7:N2} dBFS" -f (20.0 * [Math]::Log10($linear)))
}

function Format-Duration {
    param([double] $seconds)

    $span = [TimeSpan]::FromSeconds($seconds)
    return ("{0:00}:{1:00}:{2:00}" -f $span.Hours, $span.Minutes, $span.Seconds)
}

$session = $null

while (-not $session) {
    $session = Get-NewestSession -root $Root

    if (-not $session) {
        Write-Host "Waiting for a session to appear... (Ctrl+C to stop)"
        Start-Sleep -Seconds 2
    }
}

$sessionPath = $session.FullName
$timelinePath = Join-Path $sessionPath "timeline.csv"

Write-Host "Session: $sessionPath"
Write-Host ""

$position = 0
$header = $null
$latest = $null
$lastChange = Get-Date

while ($true) {
    if (Test-Path -LiteralPath $timelinePath) {
        $stream = $null

        try {
            $stream = [System.IO.File]::Open($timelinePath, 'Open', 'Read', 'ReadWrite')

            if ($stream.Length -lt $position) {
                # The file was replaced - a new session in the same folder.
                $position = 0
                $header = $null
            }

            if ($stream.Length -gt $position) {
                $null = $stream.Seek($position, 'Begin')
                $reader = New-Object System.IO.StreamReader($stream)

                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()

                    if (-not $line) { continue }

                    if (-not $header) {
                        $header = $line.Split(',')
                    } else {
                        $fields = $line.Split(',')

                        if ($fields.Count -eq $header.Count) {
                            $row = @{}
                            for ($i = 0; $i -lt $header.Count; $i++) {
                                $row[$header[$i]] = $fields[$i]
                            }
                            $latest = $row
                            $lastChange = Get-Date
                        }
                    }
                }

                $position = $stream.Position
                $reader.Dispose()
            }
        } catch {
            # The writer holds the file; try again next tick rather than dying.
        } finally {
            if ($stream) { $stream.Dispose() }
        }
    }

    Clear-Host
    Write-Host "[AuroraVerb Diagnostic Monitor]"
    Write-Host ""

    if (-not $latest) {
        Write-Host "  Session:  $($session.Name)"
        Write-Host "  Waiting for the first timeline row..."
    } else {
        $num = { param($k, $d = 0.0) if ($latest.ContainsKey($k) -and $latest[$k]) { [double]$latest[$k] } else { $d } }

        $seconds   = & $num 'seconds'
        $meanUs    = & $num 'mean_us'
        $worstUs   = & $num 'worst_us'
        $budget    = & $num 'budget_fraction'
        $inPeak    = & $num 'input_peak'
        $outPeak   = & $num 'output_peak'
        $rawPeak   = & $num 'raw_peak'
        $clipped   = & $num 'clipped'
        $nan       = & $num 'nan'
        $inf       = & $num 'inf'
        $decay     = & $num 'decay'
        $size      = & $num 'size'
        $modRate   = & $num 'mod_rate'
        $modDepth  = & $num 'mod_depth'
        $comp      = & $num 'compensation_db'
        $corr      = & $num 'correlation'
        $morph     = & $num 'morph_active'
        $drops     = & $num 'events_dropped'
        $capDrops  = & $num 'capture_dropped'

        # Round two. The first session showed 17 overruns and nothing about
        # what caused them, because the budget came from the block size the
        # host announced rather than the one it delivered. These columns are
        # the answer to that, so they are on screen while the test runs.
        $blockSize = & $num 'block_size'
        $blockMin  = & $num 'block_min'
        $blockMax  = & $num 'block_max'
        $blockRatio = & $num 'block_ratio'
        $deadlines = & $num 'deadline_count'
        $worstRatio = & $num 'worst_ratio'
        $worstBlock = & $num 'worst_ratio_block'
        $capLimited = & $num 'capture_limited'

        Write-Host ("  Session:      {0}" -f $session.Name)
        Write-Host ("  Elapsed:      {0}" -f (Format-Duration $seconds))
        Write-Host ""
        Write-Host ("  CPU Mean:     {0,8:N2} us" -f $meanUs)
        Write-Host ("  CPU Max:      {0,8:N2} us" -f $worstUs)
        Write-Host ("  Of budget:    {0,8:N2} %" -f ($budget * 100.0))
        Write-Host ""
        Write-Host ("  Block size:   {0,8:N0}  (seen {1:N0} to {2:N0})" -f $blockSize, $blockMin, $blockMax)
        Write-Host ("  This block:   {0,8:N2} x its own budget" -f $blockRatio)

        if ($deadlines -gt 0) {
            Write-Host ("  Deadlines:    {0,8:N0}  worst {1:N2}x on a {2:N0}-sample block" -f $deadlines, $worstRatio, $worstBlock) -ForegroundColor Yellow
        } else {
            Write-Host ("  Deadlines:    {0,8:N0}" -f $deadlines)
        }

        Write-Host ""
        Write-Host ("  Input Peak:  {0}" -f (Format-Db $inPeak))
        Write-Host ("  Output Peak: {0}" -f (Format-Db $outPeak))
        Write-Host ("  Raw Peak:     {0,8:N4}" -f $rawPeak)
        Write-Host ("  Clipped:      {0,8:N0}" -f $clipped)
        Write-Host ("  NaN / Inf:    {0,3:N0} / {1:N0}" -f $nan, $inf)
        Write-Host ""
        Write-Host ("  Decay:        {0,8:N2} s" -f $decay)
        Write-Host ("  Size:         {0,8:N2}" -f $size)
        Write-Host ("  Mod Rate:     {0,8:N2} Hz" -f $modRate)
        Write-Host ("  Mod Depth:    {0,8:N2}" -f $modDepth)
        Write-Host ("  Compensation: {0,8:N2} dB" -f $comp)
        Write-Host ""
        Write-Host ("  Correlation:  {0,8:N3}" -f $corr)
        Write-Host ("  Morph:        {0}" -f $(if ($morph -ge 1) { "Active" } else { "Idle" }))
        Write-Host ("  Queue drops:  {0,8:N0}" -f $drops)
        Write-Host ("  Capture drops:{0,8:N0}" -f $capDrops)

        if ($capLimited -ge 1) {
            Write-Host "  Capture:      stopped at its configured limit - telemetry continuing" -ForegroundColor Yellow
        }

        Write-Host ""

        # Live status uses the same technical rules the report does. It is a
        # provisional view: the authoritative verdict is in summary.txt.
        $status = "PASS"
        if ($drops -gt 0 -or $capDrops -gt 0 -or $clipped -gt 0 -or $outPeak -gt 1.0) {
            $status = "PASS WITH WARNINGS"
        }
        if ($nan -gt 0 -or $inf -gt 0) { $status = "FAIL" }

        $colour = switch ($status) {
            "PASS"               { "Green" }
            "PASS WITH WARNINGS" { "Yellow" }
            default              { "Red" }
        }

        Write-Host "  Status: " -NoNewline
        Write-Host $status -ForegroundColor $colour
    }

    Write-Host ""
    Write-Host "  (Ctrl+C to stop watching. The plugin keeps running either way.)"

    $summaryPath = Join-Path $sessionPath "summary.txt"

    if (Test-Path -LiteralPath $summaryPath) {
        Write-Host ""
        $jsonPath = Join-Path $sessionPath "session.json"

        Write-Host "  This session has been closed by the host. Final report:" -ForegroundColor Cyan
        Write-Host "    $summaryPath"
        Write-Host "    $jsonPath"

        $verdictLine = Select-String -LiteralPath $summaryPath -Pattern "Overall technical result:" |
                       Select-Object -First 1

        if ($verdictLine) {
            Write-Host "    $($verdictLine.Line.Trim())"
        }

        # Not the end of the watch. A test run loads and unloads the plugin many
        # times - a scan, a mixer insert, a bypass, a reinsert - and each one is
        # its own session. Exiting on the first one that closes is what sent this
        # window back to the prompt while the real session was still running.
        $next = Get-NewestSession -root $Root

        if ($next -and $next.FullName -ne $sessionPath) {
            Write-Host ""
            Write-Host "  Following the newer session: $($next.Name)" -ForegroundColor Cyan

            $session = $next
            $sessionPath = $next.FullName
            $timelinePath = Join-Path $sessionPath "timeline.csv"
            $position = 0
            $header = $null
            $latest = $null
            $lastChange = Get-Date
        } else {
            Write-Host ""
            Write-Host "  Waiting for the next session. Load AuroraVerb again, or press Ctrl+C."
        }

        Start-Sleep -Seconds $IntervalSeconds
        continue
    }

    # Idle is reported, not fatal. Between takes the user is clicking around in
    # the host with no audio running, and a watcher that quits after thirty
    # seconds of that is useless for the job it exists to do.
    $idleSeconds = ((Get-Date) - $lastChange).TotalSeconds

    if ($idleSeconds -gt $IdleTimeoutSeconds) {
        Write-Host ""
        Write-Host ("  No new telemetry for {0:N0} s - the plugin is loaded but not processing." -f $idleSeconds) -ForegroundColor DarkGray
        Write-Host "  This is normal while the transport is stopped. Still watching."

        # A newer session may have appeared while this one went quiet.
        $next = Get-NewestSession -root $Root

        if ($next -and $next.FullName -ne $sessionPath) {
            Write-Host "  A newer session has started: $($next.Name)" -ForegroundColor Cyan

            $session = $next
            $sessionPath = $next.FullName
            $timelinePath = Join-Path $sessionPath "timeline.csv"
            $position = 0
            $header = $null
            $latest = $null
            $lastChange = Get-Date
        }
    }

    Start-Sleep -Seconds $IntervalSeconds
}
