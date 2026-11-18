# Phase 12 Stage 8 — the Pre-Delay confound at A1b's default point, resolved once

Research only. `Source/` is unchanged. A methodological verification, as
decided on 2026-11-18. **It restores nothing: A1b's status is unchanged.**

Log: `Docs/Phase12-Stage8-runs/09-predelay-full-support.txt`.

## The question

The one-time Hall + Pre-Delay verification read A1b at the default point
as 1.93 (lowest run 1.88). That figure was obtained by moving A1b *earlier*
by 63 ms, which discarded about 8 ms after its first arrival — under 0.1 %
of its energy. Was the low figure produced by the discarded samples?

## The method

- **Full support.** The target is A1b's whole response. Nothing is moved
  and nothing is removed. Hall is delayed instead, by zeros in front,
  observed over the same time.
- The one preparation of the target: samples more than 200 dB below its
  loudest 5 ms, before its first arrival, are set to exact zero, so the
  late-texture family does not read numerical dust. No sample above
  −200 dB is changed. The reading with the target exactly as it is, is
  given too.
- **Start:** the Hall solution found earlier — Size 3, Decay 2.37 s,
  Early/Late 1, Early Diffusion 72, Attack 59, modulation 21, Width 145,
  Pre-Delay 63 ms — and the same with Pre-Delay at D\* + 0, 10, 30 and
  40 ms (D\* = 43.1 ms).
- **Refinement:** a pattern search over the seven controls and Pre-Delay
  from each start. The earlier method (target advanced) is refined the
  same way, for comparison.

## The result

At the start itself, from the rounded knob values as printed in the log:

| method | worst family |
|---|---|
| target advanced (the earlier method) | 3.30 |
| full support, dust zeroed | 5.60 |
| full support, target exactly as it is | 153.25 (the late-texture artefact) |

The printed knobs are rounded to whole percent, and at these settings that
is enough to move the score from 1.88 to 3.30: the minimum is narrow.

After refinement:

| started at Pre-Delay | full support | target advanced |
|---|---|---|
| D\* + 0 | **1.72** | 2.32 |
| D\* + 10 | 3.84 | 2.09 |
| D\* + 20 | 2.62 | 1.86 |
| D\* + 30 | 2.16 | 1.88 |
| D\* + 40 | 2.05 | 1.88 |
| **lowest** | **1.72** | 1.86 |

The lowest full-support figure is **1.72**: Hall at Size 19, Decay
2.31 s, Early/Late 0, Early Diffusion 66, Attack 70, modulation 0, Width
162, **Pre-Delay 53.6 ms**. What is left at that point: stereo 1.71, loop
repetition 1.72, late texture 1.64, decay 1.50 — no buildup or
energy-profile residue above 1.0.

## Reading

- **The earlier reading was not an artefact of the discarded samples.**
  With nothing discarded, Hall with Pre-Delay and its visible controls
  comes to 1.72 of A1b at the default point — a little closer than the
  1.93 found before.
- It is **WEAK** (between 1.5 and 3.0), not a mimic: no solution at or
  under 1.5 was found.
- It is not a uniform shift doing this. The Hall that gets there has
  Attack at 70 % and Size at 19; Pre-Delay supplies 54 ms of start. Hall's
  own controls match A1b's buildup and energy profile to within 1.0.
- The limits of this verification: it is a local search from one known
  solution and its neighbours, not five seeds of the frozen scatter at
  each Pre-Delay. **A lower figure may exist.** The final mimic search
  proposed in the AS2 pre-registration (section 4.3) is the instrument
  that would settle it.

## What follows

Nothing changes in A1b's classification: a structural and sonic research
reference, not production-viable, failing final acceptance at two core
points of the nine. This adds that at the default point too, Hall with
Pre-Delay reaches within 1.72 with no target sample discarded.

The discarded-samples question is closed. The full-support method is the
one used from here on, by decision.
