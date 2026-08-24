# Phase 5F — FL Studio host regression, Aurora Custom mix law

Session: `2026-08-09_19-05-14_164f3e52`
Module hash verified loaded by FL Studio: `05D43BC52923899CE59644E70668DA0B040226BA578EDB296D4A40180966845B`
48 kHz, prepare block 512, runtime blocks 1–231.

Held fixed for the whole regression: Decay 2.5000 s, Size 0.8706, Mod Rate 0.2000 Hz,
Mod Depth 0.3500, Width 1.0000, compensation −0.638 dB. Mix is the only variable.

---

## Step B0 — Mix 0% — **PASS**

**Measured** (dwell t = 240.904 s → 736.652 s, 4882 timeline rows, 8 min 16 s)

| Check | Result |
|---|---|
| Mix telemetry | min = max = `0.00000000`; 0 rows non-zero |
| Endpoint exactness | exact, not asymptotic — the law pins m ≤ 0 to dry 1 / wet 0 |
| Movement path | 7 events over 187 ms: 0.300000 → 0.293963 → 0.277000 → 0.247218 → 0.183241 → 0.116150 → 0.063391 → 0.000000 |
| Width | 1.0000 constant, single startup event |
| Decay / Size / Mod Rate / Mod Depth | each one startup event only, values unchanged |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |
| Signal genuinely present | output_peak 0.5416, raw_peak 0.2106 — dry-only, not silence |

**Operator auditory report** (user's own judgement, recorded verbatim in substance)

- The sound became distinctly drier.
- No reverb tail or sense of extension remained.
- No click, pop or zipper.
- No sudden volume jump.

Conclusion: the 0% endpoint is correct by measurement and correct by ear.

---

## Open items raised during this step

1. **Capture limit reached at t = 600.000 s** (`capture_limit_reached`, `capture_limited = 1`).
   The WAV capture stopped there by design. Timeline and event telemetry continue
   unaffected, so every check above and every check in the remaining steps is still
   available — only recorded audio is not. Accepted by the operator: the regression
   continues on telemetry plus live listening, in this same session. No new session.

2. **Mix movement at t ≈ 736.65 → 762.67 s — resolved, not an anomaly.** Mix was
   swept 0 → 1.000000 over ~450 ms, held at full wet for ~25 s, then swept back to
   0.000000 over ~190 ms. The operator confirms this was a manual exploratory move
   made to hear the difference, outside the planned sequence — not automation and
   not host-originated. Health over the window is clean (nan/inf/subnormal/clipped
   and all drop counters 0), so it is recorded as operator activity and excluded
   from the regression result.

3. Deadline overruns: 23 cumulative at t = 1057.7 s, all on small blocks, consistent
   with the Phase 5E pattern (host delivering 1-sample blocks while idle).

---

## Step B1 — Mix ≈ 1% — **PASS**

The knob was moved by hand, so the landing point is not exactly 1%. The event stream
records the settled control value as **0.009727** (0.97%). The timeline's `mix` column
prints four decimals and therefore shows `0.0100`; that is a rounded display of
0.009727, not an exact 0.010000. Every figure below is stated for the value actually
reached.

**Movement path** — 5 events over 1.25 s, t = 1338.400 → 1339.648 s:

```
0.000000 → 0.005109 → 0.014704 → 0.021000 → 0.015000 → 0.009727
```

An overshoot to 2.1% and a settle back down — the shape of a hand-turned knob, not a
fault.

**Settled window** t = 1341.1 → 1402.3 s, 609 rows:

| Check | Result |
|---|---|
| Mix telemetry | single value throughout the window, no drift |
| Coefficients at 0.009727 | dry 0.999883, wet 0.024615 |
| Against the old linear law | linear wet would be 0.009727 — the Aurora wet is 2.53× |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each; no events since t = 1250 |
| Non-mix parameter events since t = 1250 | 0 |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |

**Deadline** — 34 cumulative. Block-size distribution of every overrun in the session:

| block | overruns |
|---|---|
| 1 | 28 |
| 2 | 5 |
| 3 | 1 |
| 5 | 1 |
| 22 | 1 |

Zero overruns on any block of 32 samples or more. Identical in character to the
Phase 5E finding — the cause is the host delivering 1-sample callbacks, not the
mix law. **Not worse than Phase 5E.**

**Operator auditory report**

- Reverb is audible, very faintly.
- No click, zipper or crackle.
- No sudden level jump.

This is the step where the new curve differs most from the old one, and it passed
both by measurement and by ear.

---

## Step B2 — Mix 4.9% — **PASS**

Settled control value **0.049000**, exact in both the event stream and the timeline
column, so there is no rounding ambiguity at this point.

**Movement path** — 8 events, t = 1973.358 → 1975.906 s:

```
0.009727 → 0.015562 → 0.025417 → 0.030500 → 0.036000
         → 0.044000 → 0.049000 → 0.054053 → 0.049000
```

Approached, overshot to 5.4%, settled back to 4.9%. Hand-turned knob.

**Production mapping at m = 0.049000**

| | dry | wet | wet/dry |
|---|---|---|---|
| Aurora | 0.997039 | 0.089676 | 0.089942 |
| old linear | 0.951000 | 0.049000 | 0.051525 |

The Aurora wet is 1.83× the linear wet. The dry branch recovers 0.41 dB — real, but
far too small to hear on its own. The operator's note that the snare's body survives
is mostly a consequence of the mix being low, not of the 0.41 dB.

**Settled window** t = 1977.0 → 2045.2 s, 679 rows:

| Check | Result |
|---|---|
| Mix telemetry | min = max = 0.0490, no drift |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each |
| Non-mix parameter events since t = 1402 | 0 |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |

**Deadline** — 50 cumulative. Block sizes: 1 (×41), 2 (×6), 3, 5, 22. Still zero
overruns on any block of 32 samples or more. Unchanged in character.

**Operator auditory report**

- Reverb clearly more present than at 1%.
- The snare's main hit and body are preserved.
- No click, zipper or crackle.
- No sudden level jump.

---

## Deferred observation — level at Mix 100%

During the unplanned exploratory move at t ≈ 736–762 s the operator heard the level
drop markedly at full wet. Recorded here, **not** counted against the mix-law
regression: at Mix 100% the law gives dry 0 / wet 1 exactly, so nothing the crossfade
does can explain a level change — whatever is happening is on the wet path itself.

What the telemetry can say, from block RMS over comparable windows of the same
looping source:

| window | mix | block RMS |
|---|---|---|
| 300 – 730 s | 0.000 | −46.08 dB |
| 1977 – 2045 s | 0.049 | −46.89 dB |
| 738 – 762 s | 1.000 | −47.92 dB |

Full wet sits **1.85 dB** below dry-only in RMS terms.

Two cautions on that number:

- `output_peak`, `input_peak` and `raw_peak` are cumulative session maxima, not
  per-interval, so all three windows report the same −5.33 dB and the peak column
  contributes nothing to this comparison.
- 1.85 dB is a real drop but a modest one, and smaller than the operator's
  description suggests. Either the perception is driven by something RMS does not
  capture — the loss of the direct signal and its transients — or the windows are
  not as comparable as they look. Unresolved.

The likeliest mechanism is the preview wet trim (`previewBaseTrim` 0.25, scaled by
the decay-aware factor), which exists to stop a full-scale input clipping the tank
and which attenuates the wet path unconditionally. That is deliberately out of scope
here: the trim is not to be touched during this regression. To be taken up in the
Phase 5F preview-trim / wet gain-staging stage.

---

## Step B3 — Mix 10.0% — **PASS**

Settled control value **0.100000** — exactly the nominal point this time, in both the
event stream and the timeline column.

**Movement path** — 7 events, t = 2148.820 → 2150.091 s:

```
0.049000 → 0.062105 → 0.080195 → 0.086267 → 0.092000
         → 0.100000 → 0.095000 → 0.100000
```

**Production mapping at m = 0.100000**

| | dry | wet | wet/dry |
|---|---|---|---|
| Aurora | 0.987688 | 0.158634 | 0.160611 |
| old linear | 0.900000 | 0.100000 | 0.111111 |

Aurora wet is 1.59× linear; the dry branch recovers 0.81 dB. The gap between the two
laws keeps narrowing as mix rises — 2.53× at 1%, 1.83× at 4.9%, 1.59× here — which is
what an exponent below 1 does.

**Settled window** t = 2152.1 → 2201.5 s, 492 rows:

| Check | Result |
|---|---|
| Mix telemetry | min = max = 0.1000, no drift |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each |
| Non-mix parameter events since t = 2045 | 0 |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |

**Deadline** — 57 cumulative. Block sizes: 1 (×46), 2 (×8), 3, 5 (×2), 22. Zero
overruns at 32 samples or more, unchanged.

**Operator auditory report**

- Reverb more present than at 5%.
- The snare's main hit and body are preserved.
- No click, zipper or crackle.
- No sudden level jump.

---

## Step B4 — Mix 30.5% — **PASS**

Settled control value **0.305000**, exact in both the event stream and the timeline
column. Not the nominal 30%, so the mapping below is stated for 0.305 and the nominal
point is given alongside for comparison.

**Movement path** — 32 events, t = 2296.677 → 2304.956 s. A long, deliberate turn
with several small corrections around the target (0.303 → 0.295933 → 0.303 → 0.295 →
0.305). No discontinuity anywhere in the sequence.

**Production mapping**

| | dry | wet | wet/dry |
|---|---|---|---|
| Aurora at 0.305 | 0.887413 | 0.386940 | 0.436032 |
| linear at 0.305 | 0.695000 | 0.305000 | 0.438849 |
| Aurora at 0.300 | 0.891007 | 0.381860 | **0.428571** |
| linear at 0.300 | 0.700000 | 0.300000 | **0.428571** |

This is the step the exponent was derived for. At the nominal 30% the two laws agree
on wet/dry to the last digit shown — that identity is exactly what `k = 0.799604`
buys. At the 0.305 the knob actually reached, the two ratios are 0.436032 and
0.438849: they no longer coincide exactly, because the identity is pinned at 0.30 and
only approximately preserved either side of it. The gap is 0.6% of the ratio.

The dry branch recovers **+2.12 dB** against the old linear law at 0.305 (+2.10 dB at
the nominal 0.300). That figure is the level collapse the blind test objected to.

**Settled window** t = 2307.1 → 2431.7 s, 1238 rows:

| Check | Result |
|---|---|
| Mix telemetry | min = max = 0.3050, no drift |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each |
| Non-mix parameter events since t = 2201 | 0 |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |
| morph_active | 0 rows — no morph in flight during the window |

**Deadline** — 63 cumulative, same small-block pattern.

**Operator auditory report**

- No drop or collapse in overall level.
- Snare still full and forward.
- Reverb clearly increased over 10%.
- The transition was clean; no click, zipper or crackle; no volume jump.

The level collapse is gone by ear as well as by arithmetic.

---

## Follow-up FU-1 — bell/chime-like ringing in the tail (Phase 6, Voicing & Character)

**Observation (operator, subjective).** At Mix 30.5%, after a snare hit, a tonal
"bell or chime-like" sound is audible behind the reverb tail.

**Not a mix-law failure, and the reason is structural rather than a judgement call.**
The mix stage is two scalar multiplies applied to the dry and wet buses. It cannot
create spectral content that is not already in the wet bus — it can only make what is
there louder or quieter. So the ringing exists in the tank output at every mix
setting; raising the wet gain is what brought it above the threshold of notice.

What the new law changes here is only how much wet reaches the output. At 30% the
Aurora wet coefficient is 0.381860 against linear's 0.300000 — **1.27×**, or +2.1 dB.
That is enough to lift an already-present resonance into audibility, which is a
plausible reason the observation surfaces now and did not before. It is not evidence
that the law introduced it.

**This session cannot be used to measure it.** The capture stopped at its 600 s limit;
`output.wav` covers 0 – 600 s only, and the observation is at t ≈ 2300 s. There is no
recorded audio of the event.

**Suggested method when Phase 6 takes this up**, using tooling that already exists:

- A dedicated impulse capture at Mix 100%, well inside the capture limit, so the tail
  is isolated from source material.
- `AnalyseAuroraVerbCapture` with `impulsive = 1`, which is the entry point added for
  exactly this case.
- Modal peak persistence to locate the ringing frequencies. Note the existing ±4 Hz
  tolerance in `modalPeakPersistence`: it is wide enough that a modulated peak still
  counts as surviving, so it will not by itself distinguish a fixed resonance from a
  modulated one.
- The candidates worth examining are FDN delay-line lengths and their common factors,
  the damping filter settings, and modulation depth — a ringing tone usually means
  either a mode that is too isolated or a delay set with too much shared structure.

No change is proposed here. Voicing is explicitly out of scope for Phase 5F.

---

## Step B5 — Mix 50.0% — **PASS**

Settled control value **0.500000**, exact in event stream and timeline column.

**Movement path** — 70 events, t = 2599.310 → 2601.382 s, ending 0.493037 → 0.500000.
Several corrections in the approach (0.410 → 0.403 → 0.395 → 0.403 → …). No
discontinuity.

**Production mapping at m = 0.500000**

| | dry | wet | wet/dry |
|---|---|---|---|
| Aurora | 0.707107 | 0.574507 | 0.812475 |
| old linear | 0.500000 | 0.500000 | 1.000000 |

Note what this says about the level complaint: at 50% the Aurora law is **louder than
linear on both branches** — dry +3.01 dB, wet +1.21 dB. The law cannot be attenuating
anything here.

**Settled window** t = 2604.0 → 5738.3 s, 31 128 rows (52 minutes):

| Check | Result |
|---|---|
| Mix telemetry | min = max = 0.5000, no drift over 52 minutes |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each |
| Non-mix parameter events since t = 2431 | 0 (all 70 were mix) |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |
| morph_active | 0 rows |

**Deadline** — 73 cumulative, same small-block pattern.

**Operator auditory report**

- Reverb clearly increased over 30%.
- A very slight decrease in overall level — not objectionable, not a collapse.
- No click, zipper, crackle or other artefact in the transition.
- The bell-like tonal tail became very slightly more noticeable, but sounds good
  rather than bad at this level.

### The slight level decrease — measured, and it does not agree with the perception

Block RMS over active rows (rows where the source was silent are excluded, since
transport stopped and started four times in this stretch):

| mix | active rows | block RMS |
|---|---|---|
| 0.049 | 676 | −46.89 dB |
| 0.100 | 486 | −47.04 dB |
| 0.305 | 1231 | −46.02 dB |
| 0.500 | 1251 | −45.39 dB |

From 30.5% to 50% the measured level **rises by 0.63 dB**. It does not fall. So the
mix law is not attenuating the signal, and this is not marked against it.

The perception is not dismissed — it is unexplained. Three candidates, none
established:

1. RMS is a poor proxy for loudness here. Raising mix replaces direct signal and its
   transients with diffuse tail. Perceived loudness and clarity can fall while RMS
   rises, because the ear weights onsets and spectral distribution in a way RMS does
   not. This is the most plausible of the three.
2. The wet path is quieter than the dry path it displaces, so the *balance* shifts
   even though the sum does not. This is the same mechanism as the Mix 100% note
   above, and points at the same place: the preview wet trim. See FU-2.
3. The windows may not be comparable — the operator may have been playing different
   material at different points in a 52-minute stretch. Not controlled for.

The correct instrument is gated LUFS (BS.1770-4), not block RMS, and that tooling
already exists offline. It cannot be applied here: the capture stopped at 600 s and
all of these windows are past it. Deferred to FU-2 with a fresh, bounded capture.

---

## FU-1 update — tonal tail, observed at 30.5% and 50%

Revised from the operator's reports at both settings:

- The tonal, bell/chime-like component in the tail is **present**.
- It becomes **slightly more audible as Mix rises** — very slightly between 30.5% and
  50%, not proportionally.
- At its current level it is **not objectionable**. The operator's judgement at 50% is
  that it sounds good rather than bad.

That it tracks Mix upward is consistent with the structural argument already recorded:
the mix stage scales the wet bus, so raising Mix raises anything already in it. It
remains no evidence that the law created the resonance.

Status: open, Phase 6 Voicing & Character. Priority is low on current evidence —
the operator does not find it a defect. It should still be characterised rather than
left to taste, because a resonance that is pleasant on a snare at 2.5 s decay may not
be at other decay times, sizes or sources.

## FU-2 — wet-path level and the preview trim (Phase 5F, preview/wet gain staging)

Collects two observations that point at the same place:

- **Mix 100%** (t ≈ 736–762 s): marked level drop by ear; block RMS 1.85 dB below
  dry-only. Recorded above.
- **Mix 50%**: very slight level drop by ear, while block RMS *rises* 0.63 dB.

At both settings the Aurora law's coefficients are at or above the old linear law's,
so the crossfade is not the attenuator. What both have in common is that raising Mix
substitutes wet for dry, which makes the level of the wet path itself the thing under
test.

The prime suspect remains `previewBaseTrim` (0.25) scaled by the decay-aware factor —
an unconditional attenuation of the wet bus, present to stop a full-scale input
clipping the tank.

Method when this is taken up:

- A fresh capture session, bounded well inside the 600 s limit, so there is recorded
  audio to measure.
- Gated LUFS per BS.1770-4 rather than block RMS, using the meter in `Tests/GainStaging`
  that was validated against the published table.
- Measure dry-only, wet-only and the mix points, so the wet path's level is separated
  from the crossfade's.

Not to be acted on during the mix-law regression. The preview trim is explicitly not
to be changed here.

---

## Step B6 — Mix 100% — **PASS**

Settled control value **1.000000**, exact in event stream and timeline column.

**Movement path** — 74 events across two attempts, t = 5938.272 → 5950.240 s.

The first sweep stopped at **0.995000**, short of the stop. It was then backed off to
0.601583 and swept up again, reaching exactly 1.000000 on the second attempt. That
near-miss is worth keeping, because it shows what the pinned endpoint is protecting:

| | dry | wet |
|---|---|---|
| m = 0.995000 (first pass) | 0.00785390 | 0.99599997 |
| m = 1.000000 (second pass) | 0.00000000 | 1.00000000 |

At 0.995 the dry branch is still passing 0.0079 — small, but not zero. Only the true
endpoint gives silence on the dry bus.

**What is and is not measured here.** The telemetry records the *control* value, not
the coefficients; the plugin does not log dry and wet. So the chain is: telemetry
proves the control reached exactly 1.000000; the law pins `m >= 1.0` to `{0.0, 1.0}`
without arithmetic; the unit suite checks that pinning exactly. The operator's report
that the dry snare character disappears completely is independent corroboration that
the dry bus really is silent, and it is their observation, not a measurement.

**Settled window** t = 5953.1 → 6106.8 s, 1527 rows:

| Check | Result |
|---|---|
| Mix telemetry | min = max = 1.0000, no drift |
| Width | 1.0000, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth | 0 off-nominal rows each |
| Non-mix parameter events since t = 5738 | 0 |
| NaN / Inf / subnormal / clipped | 0 rows |
| Event / snapshot / capture drops | 0 |
| morph_active | 0 rows |

**Deadline** — 80 cumulative. Block sizes across the whole session: 1 (×60), 2 (×11),
3 (×2), 4, 5 (×2), 6, 21, 22, 24. **Zero overruns on any block of 32 samples or
larger**, over 6100 seconds and every mix setting. Identical in character to Phase 5E.

**Operator auditory report**

- Transition clean; no click, zipper or crackle.
- Level clearly low.
- The dry snare character disappears completely.
- The wet tail turns distinctly bell/chime-like — pleasant and light at 50%, but at
  100% it dominates and becomes nearly the main thing heard.

### Level at 100%, measured

| mix | block RMS | vs 100% |
|---|---|---|
| 0.305 | −46.02 dB | |
| 0.500 | −45.39 dB | |
| 1.000 | **−48.57 dB** | −2.54 dB vs 30.5%, −3.17 dB vs 50% |

Here measurement and perception agree: at full wet the level really does fall, by
about 3 dB against 50%. Since the law gives dry 0 / wet 1 exactly, none of that drop
can come from the crossfade. It is the wet path's own level. This is the strongest
evidence yet for FU-2.

---

# VERDICT — Aurora Custom mix law, FL Studio host regression: **PASS**

Session `2026-08-09_19-05-14_164f3e52`, 6107 seconds, 48 kHz, runtime blocks 1–231,
module hash verified as the one FL Studio actually loaded.

## What was required, and what was found

| Requirement | Result |
|---|---|
| Mix telemetry correct at every point | Yes — 7 settled points, each a single value with no drift |
| Endpoints exact | Yes — 0.000000 and 1.000000 both reached exactly; the 0.995 near-miss confirms the pinning is doing work |
| No click or zipper at 0→1% and 0→5% | Confirmed by the operator at both, and at every other step |
| Smoother healthy | 271 mix events across 7 moves, every path continuous, no discontinuity |
| NaN / Inf / subnormal / clipped = 0 | Zero rows, all 6107 seconds |
| Event / snapshot / capture drops = 0 | Zero |
| Width unchanged | 1.0000 throughout, 0 off-nominal rows |
| Decay / Size / Mod Rate / Mod Depth unchanged | 0 off-nominal rows each; not one non-mix parameter event after startup |
| Deadline pattern not worse than Phase 5E | Same: 80 overruns, all on blocks ≤ 24 samples, zero at ≥ 32 |

## Measured coefficients at every settled point

| step | control | dry | wet | wet/dry | wet vs linear |
|---|---|---|---|---|---|
| B0 | 0.000000 | 1.000000 | 0.000000 | — | — |
| B1 | 0.009727 | 0.999883 | 0.024615 | 0.024618 | 2.53× |
| B2 | 0.049000 | 0.997039 | 0.089676 | 0.089942 | 1.83× |
| B3 | 0.100000 | 0.987688 | 0.158634 | 0.160611 | 1.59× |
| B4 | 0.305000 | 0.887413 | 0.386940 | 0.436032 | 1.27× |
| B5 | 0.500000 | 0.707107 | 0.574507 | 0.812475 | 1.15× |
| B6 | 1.000000 | 0.000000 | 1.000000 | — | 1.00× |

## The complaint this change existed to fix

Resolved. At the 30% default the dry branch recovers **2.10 dB** against the old
linear law while the wet/dry ratio is preserved exactly, and the operator reports no
drop or collapse at that setting. That was the whole object of the exercise.

## What is still open

The planned sweep has one step left: **back to 30%**. It has not been run. It is worth
running, because coming down is a different test from going up — it checks that the
law and its smoother return to the same coefficients from the other direction rather
than showing hysteresis.

Two follow-ups are open and are not held against this verdict:

- **FU-2** (Phase 5F, preview/wet gain staging) — the wet path's own level. Strongest
  evidence: at Mix 100% the law contributes dry 0 / wet 1 exactly, yet the level falls
  ~3 dB against 50%. The crossfade cannot cause that.
- **FU-1** (Phase 6, Voicing & Character) — bell/chime-like modal ringing in the tail,
  rising with Mix and dominant at 100% wet. The mix stage scales the wet bus; it
  cannot create spectral content, so the resonance is in the tank at every setting.

Neither is a defect of the mix law. Both were surfaced by it, because raising the wet
gain is what made them audible.

---

## Correction — the event stream is not the authority for a settled value

Found while reconciling the final step: the last mix event reads 0.278175, while the
timeline reads 0.2820. Both are correct, and the reason is in the code.

Parameter events are **coalesced with a threshold** — `Source/PluginProcessor.cpp`,
the `report` lambda, called for mix with `0.005f`:

```cpp
if (last < 0.0f || std::abs (current - last) > threshold)
```

A control that finishes its travel with a move smaller than 0.005 therefore leaves no
event at all. The event stream records the *path*; the timeline snapshot records the
*state*. For a settled value the timeline is the authority, at its four decimal places.

**This changes one previously reported figure.** At step B1 the event stream read
0.009727 and the timeline read 0.0100, and that gap was attributed to four-decimal
rounding. That explanation was wrong: 0.009727 rounds to 0.0097, not 0.0100. The
settled control value was **0.0100**, and the last sub-threshold move went unrecorded.

| B1 | dry | wet |
|---|---|---|
| as reported, at 0.009727 | 0.999883 | 0.024615 |
| **corrected, at 0.0100** | **0.999877** | **0.025165** |

The wet figure was 2.2% low. Nothing about the pass/fail changes, and no other step is
affected: at B2–B6 the event value and the timeline value agree exactly (0.049000,
0.100000, 0.305000, 0.500000, 1.000000).

---

## Step B7 — return to 30% — **PASS**

The operator went well beyond the planned single return. From Mix 100% the control was
swept down, and then through **three further full excursions to exact zero and back**,
before settling. 454 mix events in the session, 240 of them in this stretch.

Settled control value **0.2820** (timeline; the last event reads 0.278175, which is the
coalescing above). Four decimal places means the value is known to ±0.00005 — the
resulting uncertainty in the coefficients is in the sixth decimal:

| m | dry | wet | wet/dry |
|---|---|---|---|
| 0.28195 | 0.903519 | 0.363375 | 0.402178 |
| **0.28200** | **0.903485** | **0.363427** | **0.402250** |
| 0.28205 | 0.903451 | 0.363478 | 0.402322 |

### 1. Consistent with the earlier 30% point?

Yes, allowing for the control landing at 28.2% rather than 30.5%. Both sit on the same
curve; there is no second branch.

| point | control | dry | wet | wet/dry |
|---|---|---|---|---|
| B4, ascending | 0.3050 | 0.887413 | 0.386940 | 0.436032 |
| B7, descending | 0.2820 | 0.903485 | 0.363427 | 0.402250 |

### 2. Is the smoother continuous in the downward direction?

Yes. The descent from 1.0 took 22 events over 610 ms with no step larger than 0.077 in
the control, and the three subsequent excursions to zero and back are equally smooth.
No discontinuity anywhere, and the operator heard no click or zipper in any of them.

### 3. Hysteresis or state-dependence?

**None is possible, and the reason is structural rather than statistical.**
`mixCoefficientsFor` is a pure function of its argument:

```cpp
[[nodiscard]] inline MixCoefficients mixCoefficientsFor (double mix) noexcept
{
    const double m = std::clamp (mix, 0.0, 1.0);
    if (m <= 0.0) return { 1.0, 0.0 };
    if (m >= 1.0) return { 0.0, 1.0 };
    return { std::cos (m * 1.5707963267948966), std::pow (m, mixWetExponent) };
}
```

No member state, no static, no branch on direction, no dependence on anything but `m`.
The same control value gives the same coefficients whichever way it was approached.

The session happens to contain the empirical case as well: the timeline shows the
control at exactly 0.3000 twice in this stretch — once descending from 1.0
(t = 6264.9–6267.3 s), once ascending from 0 (t = 6272.4–6278.4 s). Block RMS differs
by 0.23 dB between them. **That number proves nothing**: the two windows are 24 and 59
rows of a live musical source a few seconds apart, which is far too small and too
uncontrolled to resolve a difference of that size. It is reported for completeness, not
as evidence. The structural argument is the answer.

### 4. Width and the other parameters

Constant. Over the whole return stretch, 2976 timeline rows: width 1.0000, decay
2.5000, size 0.8706, mod_rate 0.2000, mod_depth 0.3500 — zero off-nominal rows each,
and zero non-mix parameter events.

### 5. Health

Clean. Across those 2976 rows: NaN 0, Inf 0, subnormal 0, clipped 0, event drops 0,
snapshot drops 0, capture drops 0, morph_active 0. Deadline 90 cumulative, same
small-block pattern.

**Operator auditory report**

- Transition clean; no click, zipper or crackle.
- Snare full and forward again.
- Reverb balance feels the same as the earlier 30% test.
- The level collapse did not return.
- The dominant bell-like character at full wet receded into the background.

---

# FINAL VERDICT — Aurora Custom Mix Law, FL Studio host regression: **FINAL PASS**

Session `2026-08-09_19-05-14_164f3e52`. 6379 seconds — 1 hour 46 minutes — at 48 kHz,
runtime blocks 1 to 231, on the module hash verified as the one FL Studio loaded.
Eight settled points, 454 mix events, 12 963 timeline rows.

## Settled points, corrected

| step | control | dry | wet | wet/dry | wet vs linear |
|---|---|---|---|---|---|
| B0 | 0.0000 | 1.000000 | 0.000000 | — | — |
| B1 | 0.0100 | 0.999877 | 0.025165 | 0.025168 | 2.52× |
| B2 | 0.0490 | 0.997039 | 0.089676 | 0.089942 | 1.83× |
| B3 | 0.1000 | 0.987688 | 0.158634 | 0.160611 | 1.59× |
| B4 | 0.3050 | 0.887413 | 0.386940 | 0.436032 | 1.27× |
| B5 | 0.5000 | 0.707107 | 0.574507 | 0.812475 | 1.15× |
| B6 | 1.0000 | 0.000000 | 1.000000 | — | 1.00× |
| B7 | 0.2820 | 0.903485 | 0.363427 | 0.402250 | 1.29× |

## Every requirement, met

| Requirement | Result |
|---|---|
| Mix telemetry correct at every point | 8 settled points, each a single value with no drift, longest held 52 minutes |
| Endpoints exact | 0.000000 and 1.000000 both reached exactly, four times and once respectively |
| No click or zipper at 0→1% and 0→5% | Confirmed, and at all seven other transitions including three extra 0↔30% round trips |
| Smoother healthy, both directions | 454 events, every path continuous ascending and descending |
| No hysteresis | Impossible by construction — the law is a pure function of the control value |
| NaN / Inf / subnormal / clipped = 0 | Zero rows, all 12 963 |
| Event / snapshot / capture drops = 0 | Zero |
| Width unchanged | 1.0000 throughout |
| Decay / Size / Mod Rate / Mod Depth unchanged | Zero off-nominal rows; not one non-mix parameter event after startup |
| Deadline not worse than Phase 5E | 90 overruns, all on blocks ≤ 24 samples, zero at ≥ 32 |

## The complaint this change existed to fix

Resolved, and confirmed twice from opposite directions. At the 30% default the dry
branch recovers 2.10 dB against the old linear law while the wet/dry ratio is preserved
exactly. The operator reports no level drop at 30% either on the way up or on the way
back down from full wet.

## Carried forward

- **FU-2** — preview / wet gain staging. Next stage of Phase 5F.
- **FU-1** — bell/chime-like modal ringing. Phase 6, Voicing & Character. Untouched.

The mix law is closed.

---

## FU-1 update — tonal content measured in the FU-2 candidate renders

The operator heard a flat, continuous, hum-like tonal sound behind all four blind
candidates at Mix 100%, and asked that it not be attributed to the candidate gains.
That instruction was correct, and the measurement confirms why.

### The four candidates are one render, scaled

Measured on the mixed / Mix 100% set, sample by sample against the loudest file:

- gain ratios, as an unordered set: **0.250000, 0.405000, 0.674499**
- worst relative deviation from an exact scalar multiple: **6.6e-8**

6.6e-8 is float32 storage precision, not a difference. The four files are the same
wet render multiplied by four constants, so no gain choice can create or remove
spectral content. (No letter is named here; naming which file carried which ratio
would be reading the blind key.)

### The tonal structure is identical across all four

Goertzel bins over a 3 s window from the sustained middle of the render, each
spectrum normalised to its own 220 Hz fundamental:

**spread across the four candidates: 0.0000 dB at every frequency measured.**

So the hum is one signal heard at four levels.

### Most of the hum is in the dry source

The `mixed` source is not neutral for this question. `makeGainStagingSource` builds a
**sustained fifth at 220 Hz and 330 Hz** under a repeating transient, and that is what
dominates both spectra: 220 Hz is the maximum in the dry *and* in the wet, with
330 Hz at -3.10 dB dry.

A reverb of a sustained tone is a sustained tone. Much of what was heard as a hum is
the source's own held interval, reverberated — and at Mix 100% there is no dry signal
left to mask it.

### But not all of it: some frequencies are boosted well beyond the dry

Relative to the 220 Hz fundamental, wet minus dry:

| freq Hz | dry | wet | wet - dry | |
|---|---|---|---|---|
| 55 | -70.45 | -55.86 | **+14.59** | boosted |
| 110 | -55.60 | -57.98 | -2.38 | |
| 165 | -59.84 | -55.26 | +4.58 | |
| 220 | 0.00 | 0.00 | 0.00 | fundamental |
| 254 | -62.83 | -50.88 | **+11.95** | boosted |
| 275 | -70.43 | -58.75 | **+11.68** | boosted |
| 330 | -3.10 | -7.90 | -4.80 | |
| 385 | -61.13 | -61.12 | +0.01 | |
| 440 | -58.74 | -62.45 | -3.71 | |
| 495 | -65.91 | -69.17 | -3.26 | |
| 550 | -59.63 | -53.72 | +5.91 | |
| 660 | -57.24 | -41.64 | **+15.60** | boosted |
| 880 | -62.63 | -56.11 | +6.52 | |
| 1100 | -66.49 | -81.58 | -15.09 | suppressed |
| 1320 | -58.80 | -64.28 | -5.48 | |
| 2032 | -62.69 | -75.59 | -12.90 | suppressed |
| 3225 | -56.91 | -47.23 | **+9.68** | boosted |

Four bins gain 10 to 16 dB relative to the fundamental, and two lose 13 to 15 dB.
A flat tank would move all of them together. **This is modal colouration, and it is
in the wet path at a level the dry does not have.** It answers question 4: yes, there
is tonal content in the wet that is not in the dry, and it belongs to FU-1.

### Three limits on the above, stated before it is used

1. **Seventeen hand-picked frequencies, not a spectrum.** Chosen around the source's
   harmonics and the tone scan's worst points. A stronger mode could sit between them.
   A proper analysis needs a full spectrum, not targeted bins.
2. **The boosts are lower bounds.** A 3 s Goertzel bin is about 0.33 Hz wide, and the
   modulation moves modes by more than that, so energy that belongs to a mode is
   smeared out of the bin measuring it.
3. **254 Hz and 3225 Hz were also the two worst points in the sustained tone scan — but
   at Size 100%, and these renders are Size 60%.** Different Size means different delay
   lengths and therefore different modal frequencies. The coincidence is a lead worth
   following, not a confirmation, and should not be written down as one.

### Status

FU-1 remains open for Phase 6, now with a first measurement rather than only a
description. The suggested method there is unchanged and still the right one: a
dedicated impulse capture at Mix 100%, analysed with `AnalyseAuroraVerbCapture` at
`impulsive = 1`, so the tail is isolated from source material — which is exactly the
confound that made this measurement need three caveats.

No voicing change is proposed. Nothing in the DSP was touched.
