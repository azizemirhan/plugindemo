# AuroraVerb — FL Studio test checklist

The first session in a real host. Everything before this was measured offline;
this is where the plugin meets a DAW's actual behaviour — its block sizes, its
transport, its scanning, its automation, and its habit of doing things the
offline harness never does.

Nothing here is a listening test. Whether the reverb sounds good is a separate
question, and the diagnostic layer deliberately refuses to answer it.

---

## Before starting

```powershell
# 1. Build the diagnostic preview
cmake -S . -B build -DAV_DEV_DIFFUSION_PREVIEW=ON -DAV_DEV_ALLPASS_CANDIDATE=1 `
                    -DAV_DEV_DIAGNOSTICS=ON -DAV_DEV_AUDIO_CAPTURE=ON -DAV_BUILD_TESTS=ON
cmake --build build --config Release

# 2. Render the test sources
.\Scripts\prepare-fl-test.ps1

# 3. Start the monitor and FL Studio
.\Scripts\start-fl-diagnostic.ps1
```

The plugin installs to `%LOCALAPPDATA%\Programs\Common\VST3`.

**Smart App Control**: a freshly linked VST3 is sometimes refused, and FL Studio
reports it as a plugin that failed to load. Delete the `.vst3` and rebuild — the
new binary hashes differently and gets through. See the README.

---

## What is being recorded

Each run writes a session folder under `%USERPROFILE%\AuroraVerbDiagnostics`:

| File | Contents |
|---|---|
| `session.json` | Every counter, machine-readable, versioned schema |
| `summary.txt` | The same thing for a human, ending in a technical verdict |
| `timeline.csv` | One row per poll — what the live monitor reads |
| `events.jsonl` | Parameter changes, morphs, prepares, warnings, with timestamps |
| `input.wav` / `output.wav` | What went in and what came out |
| `analysis.json` / `analysis.txt` | Written later by the session analyser |
| `environment.txt` | Build, host and configuration |

The report is written when the host **releases** the plugin. Close FL Studio, or
remove the plugin from the mixer, before looking for it.

---

## Sample rates

Run the whole matrix below at each rate the machine's driver offers. Change it
in **Options > Audio settings**.

- [ ] 44.1 kHz
- [ ] 48 kHz
- [ ] 96 kHz

## Buffer sizes

At each sample rate, repeat the core scenarios at three buffer lengths.

- [ ] Low (whatever the driver's shortest stable setting is)
- [ ] Medium (a typical working value)
- [ ] High (the driver's longest)

Record the actual values used — FL Studio's slider is in samples or milliseconds
depending on the driver, and `session.json` records what the plugin was told.

---

## Scenarios

Tick each one, and note the session folder it produced.

| # | Scenario | What to watch |
|---|---|---|
| 1 | Plugin scan and load | Appears in the database; no scan crash |
| 2 | Editor open and close, several times | No leak, no crash, monitor keeps updating |
| 3 | Play and stop | Transport state appears in `session.json` |
| 4 | Loop playback over a boundary | No click at the loop point |
| 5 | Mixer track bypass | Silence, then clean resume |
| 6 | Plugin bypass (the plugin's own) | Dry passes through |
| 7 | Decay automation | Level does not jump; `events.jsonl` shows the changes |
| 8 | Size automation | Morph events appear; no dropouts |
| 9 | Mod Rate automation | No zipper, no phase reset |
| 10 | Mod Depth automation, including to 0 | Depth 0 is the static tank |
| 11 | All four automated at once | CPU p99 stays inside budget |
| 12 | Mono source into the plugin | Stereo output, correlation below 1 |
| 13 | Stereo source | Correlation near 0, mono downmix loss small |
| 14 | Silence after a tail | Tail decays; no denormal stall, no stuck level |
| 15 | Long decay (Decay at maximum) | No runaway; peak stays bounded |
| 16 | Rapid Size changes | Morphs coalesce; no pile-up |
| 17 | Transport seek while playing | No burst, no NaN |
| 18 | Change sample rate mid-session | `prepare_sample_rate` event; clean restart |
| 19 | Change buffer size mid-session | Block-size range widens in the report |
| 20 | Offline WAV export (render) | Offline flag set; render completes |
| 21 | Save the project | State written |
| 22 | Restore state in the same session | `state_restore` event; sound matches |
| 23 | Remove and re-insert the plugin | Report written on removal; new session on insert |
| 24 | Two AuroraVerb instances at once | Two session folders, independent counters |
| 25 | Close FL Studio | Final reports written for every instance |

**FL Studio trial**: a trial licence cannot reopen a saved project. Run the whole
matrix inside one session. The diagnostic folders and any WAV exports survive
independently of the project file, so nothing is lost.

---

## After each session

```powershell
.\Scripts\analyze-diagnostic-session.ps1
```

Runs the offline analysis over the captured audio and merges its result with the
live verdict. Exit code: `0` pass, `1` pass with warnings, `2` fail, `3` the
session was incomplete.

### Reading the result

The verdict is technical. `PASS` means: no NaN, no Inf, no audio-thread
allocation, no queue or capture drops, no clipping, CPU inside budget, mono
cancellation inside 4 dB.

Two things will legitimately raise warnings and are **not** faults:

- **`raw_internal_over_unity`** — the internal peak before the output trim. A
  reverb integrates energy; PHASE 5C measured 2.79 on full-scale DC and 1.14 on
  sustained full-scale noise at the largest Size. These are reported rather than
  hidden behind a limiter, because there is no limiter anywhere in this path.
- **`uncalibrated_region`** — Decay below 0.1 s or above 100 s, or the corner at
  0.1 s with a large Size where the level compensation hits its 12 dB limit.
  Known, measured, and documented in the README.

A `FAIL` is a real defect. Keep the session folder and the project.

---

## Recording the outcome

For each run, note:

- sample rate, buffer size, FL Studio version
- session folder name
- verdict, and any warnings beyond the two above
- anything heard that the numbers did not catch

The last line matters most. The diagnostic layer measures what it was told to
measure; a listener notices what nobody thought to instrument.
