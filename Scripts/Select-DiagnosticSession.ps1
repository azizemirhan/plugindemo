<#
    Chooses which diagnostic session folder to work on.

    Kept in its own file, and free of any analysis, so that the choice can be
    tested on its own. It is the part that was wrong, and it was wrong in a way
    no analysis test would have caught.

    WHY NOT LastWriteTime
    ---------------------
    Windows does not update a directory's timestamp while a file inside it is
    open. A session that is being written therefore looks OLDER than one that
    finished hours ago, so "the newest folder by LastWriteTime" reliably picks
    the wrong one - and picks a finished session over the live one precisely
    when a live one exists. The watcher had this same defect and was fixed;
    this had it too.

    Folder names are YYYY-MM-DD_HH-mm-ss_instanceid, so sorting by NAME is a
    real chronological sort that no file system behaviour can distort.

    THE RULES
    ---------
      1. A session with no summary.txt has not been finalised, so it is live.
      2. Exactly one live session: that is the answer.
      3. Several live: only one of them should actually be growing. Measure
         timeline.csv twice, a moment apart, and take the one that grew.
      4. Several growing, or several live and none growing: do not guess. Ask
         for an explicit -Session and stop.
      5. No live session at all: the newest FINISHED one, by folder name.

    Kept to plain ASCII, like the other scripts here: Windows PowerShell reads a
    BOM-less script using the machine's ANSI code page, and a stray UTF-8 dash
    has silently broken a script in this repository before.
#>

function Select-DiagnosticSession {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Root,

        # How long to wait between the two timeline.csv measurements. Only used
        # when more than one session looks live.
        [double] $GrowthCheckSeconds = 3.0,

        # Returns the current size of a session's timeline.csv. Injectable so
        # the growth rule can be tested without a second process writing files
        # in real time.
        [scriptblock] $SizeProbe = $null
    )

    $result = [pscustomobject]@{
        Status    = 'none'      # selected | ambiguous | none
        Path      = $null
        Reason    = ''
        Live      = @()
        Growing   = @()
    }

    if (-not (Test-Path -LiteralPath $Root)) {
        $result.Reason = "no diagnostic root at $Root"
        return $result
    }

    $folders = @(Get-ChildItem -LiteralPath $Root -Directory -ErrorAction SilentlyContinue)

    if ($folders.Count -eq 0) {
        $result.Reason = "no sessions under $Root"
        return $result
    }

    if (-not $SizeProbe) {
        $SizeProbe = {
            param($path)
            $timeline = Join-Path $path "timeline.csv"
            if (Test-Path -LiteralPath $timeline) { (Get-Item -LiteralPath $timeline).Length } else { 0 }
        }
    }

    # --- 1. Which of them have not been finalised ---------------------------
    #
    # summary.txt is written when the host releases the plugin, so its absence
    # is the definition of "still running" that does not depend on any
    # timestamp.
    $live = @($folders | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $_.FullName "summary.txt"))
    })

    $result.Live = @($live | ForEach-Object { $_.Name })

    if ($live.Count -eq 1) {
        $result.Status = 'selected'
        $result.Path   = $live[0].FullName
        $result.Reason = 'the only session without a summary.txt, so the only live one'
        return $result
    }

    if ($live.Count -gt 1) {
        # --- 2. Several look live. Only one should be growing. --------------
        #
        # A host scan instantiates the plugin and drops it again, which can
        # leave a folder that never got a summary. Those are indistinguishable
        # from the real session until you watch whether anything is still being
        # written to them.
        $before = @{}
        foreach ($f in $live) { $before[$f.FullName] = & $SizeProbe $f.FullName }

        if ($GrowthCheckSeconds -gt 0) { Start-Sleep -Seconds $GrowthCheckSeconds }

        $growing = @($live | Where-Object {
            (& $SizeProbe $_.FullName) -gt $before[$_.FullName]
        })

        $result.Growing = @($growing | ForEach-Object { $_.Name })

        if ($growing.Count -eq 1) {
            $result.Status = 'selected'
            $result.Path   = $growing[0].FullName
            $result.Reason = "the only one of $($live.Count) live sessions whose timeline is still growing"
            return $result
        }

        # Guessing here would analyse one live session and silently ignore
        # another, and the report would look exactly as confident either way.
        $result.Status = 'ambiguous'
        $result.Reason = if ($growing.Count -gt 1) {
            "$($growing.Count) sessions are being written at once"
        } else {
            "$($live.Count) sessions have no summary.txt and none of them is growing"
        }

        return $result
    }

    # --- 3. Nothing live. The newest finished one, by name. -----------------
    $newest = $folders | Sort-Object Name -Descending | Select-Object -First 1

    $result.Status = 'selected'
    $result.Path   = $newest.FullName
    $result.Reason = 'no live session; the newest finished one by folder timestamp'

    return $result
}
