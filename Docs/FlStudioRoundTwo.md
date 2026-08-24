# AuroraVerb — FL Studio validation, round two

The first session passed with warnings and left four things unmeasured. This
round exists to close them, not to change the DSP.

| Gap from round one | How round two closes it |
|---|---|
| 17 deadline overruns with no evidence about their cause | Every callback is now measured against its own block's period, and every overrun carries the block size, duration, budget, ratio and plugin state |
| 43 minutes of capture, 925 MB per file, no bound | A configurable limit, defaulting to ten minutes, that stops cleanly and says so |
| No automation, transport or bypass exercised | Test A drives all of them and checks each one appears in the telemetry |
| RT60 not measurable from mixed material | Test B is one impulse into silence, at 100% wet, with nothing else playing |

Round one ran at 44.1 kHz. That result stands; this round runs at **48 kHz** so
the two rates are both covered rather than one replacing the other.

---

## Session settings, both tests

Set these in FL Studio before inserting the plugin.

- Sample rate **48 000 Hz** (Options > Audio settings)
- Buffer length **512 samples**
- One AuroraVerb instance, on one mixer track
- Every AuroraVerb parameter at its default
- Diagnostic Preview build, capture enabled

The plugin's own defaults are Decay 2.5 s, Size 60%, Mod Rate 0.5 Hz, Mod Depth
30%, Mix 30%, Width 100%. If any of these are not what the plugin opens with,
stop and say so — it means the build is not the one this document describes.

Capture stops itself after ten minutes. That is not an error and does not end
the session: telemetry keeps running, `summary.txt` records where the audio
stopped, and `events.jsonl` carries a `capture_limit_reached` entry. Test B
needs far less than ten minutes; test A should be kept inside it too, so the
captured audio covers the whole run.

---

## Test A — host behaviour and automation

One session, all of it recorded. Work through the list in order; each step has
something that must appear in the telemetry afterwards.

| # | Action | Must appear in the session |
|---|---|---|
| 1 | Play, then stop | Transport state recorded at both points |
| 2 | Seek the transport while playing | No burst, no NaN, no Inf |
| 3 | Bypass the plugin (its own bypass), then un-bypass | A bypass event each way |
| 4 | Bypass the mixer track, then un-bypass | Silence, then clean resume |
| 5 | Decay: short, then mid, then long | Three parameter events; level does not jump |
| 6 | Size: minimum, default, near maximum | Morph events; no dropouts |
| 7 | Mod Depth: 0, default, high | Depth 0 is the static tank |
| 8 | Mod Rate: low, default, high | No zipper, no phase reset |
| 9 | Automate Decay and Size at the same time | Both event streams present; CPU inside budget |
| 10 | Change Size rapidly, several times in a few seconds | Morphs coalesce rather than piling up |
| 11 | Stop everything and let the tail finish in silence | Tail decays to silence; no stuck level, no denormal stall |

Play the standard sources under this — the same set as round one — so the
material is not silence while the automation runs.

**Every automation move and every bypass has to be visible in the telemetry.**
If a step produced no event, that is a finding about the diagnostic layer, and
it belongs in the report rather than being repeated until it appears.

---

## Test B — impulse response

A **separate, clean session**. Close the test A session first.

- Source: `impulse-dry.wav` only
- Mix: **100% wet**
- Everything else at its defaults
- One impulse, then silence, for at least twelve seconds
- No other source playing, on any track
- **No parameter changes at all during the session**

The point is a recording that can be measured rather than described. Anything
else in the signal path — a second source, a parameter move, a click from the
transport — makes the decay measurement meaningless, and a measurement nobody
can trust is worse than not having one.

What is measured from it afterwards:

- RT60, broadband and by band (low, mid, high)
- T20 and T30, with the R² of each fit
- Energy decay curve linearity
- Late-tail periodicity, and modal persistence
- Peak lifetime and spectral flux
- Stereo correlation, mono downmix loss, worst third-octave cancellation
- True peak

Each is compared against the figure the offline harness produced for the same
settings. A difference is a finding; the offline harness is not automatically
the correct one.

---

## After both sessions

```powershell
.\Scripts\analyze-diagnostic-session.ps1
```

Do not run it while a session is still open — the report is written when the
host releases the plugin, and a folder read before that is incomplete rather
than empty.

---

## What this round does not do

No gain value is changed and no limiter is added. The gain-staging measurement
that accompanies this round reports what the current chain does and splits the
result into the preview trim, the Size compensation and the mix law. Choosing
what to change comes after listening to the A/B renders, not before.
