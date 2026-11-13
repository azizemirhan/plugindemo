# Phase 8 — Stage 2.5 engineering signoff

Two issues raised against the Stage 2 implementation commit `d858cea`, closed.
No DSP was redesigned. One defect was found and fixed; its fix is six lines.

---

## 1. The commutation claim, and what it actually covers

### What produced 4.2e-16

| | |
|---|---|
| Tank | `avtest::sweep::Tank`, i.e. `FdnCore<8>` with fixed injection/readout vectors |
| Mod Depth control | 0.00 → `mapping::modDepthFor(0.0)` = **0.0** |
| Mod Rate control | 0.50 Hz → rate scale 1.000000 |
| Allpass engine depth | 0.0 |
| Matrix engine depth | 0.0 |
| Modulation | **disabled**, by zero depth in both engines |
| Complete production tank? | **No** — not `MorphingStereoFdn<8>`, so no Size banks, no input diffusion, no Size compensation, no stereo readout |

The shipped product runs Mod Depth **30 %**, not 0.

### Depth 0 really is time-invariant — verified, not asserted

Shift the input by 977 samples; require the output to shift by exactly 977.

| Mod Depth | relative shift-invariance error |
|---|---|
| 0 % | `1.26e-25` |
| 30 % (shipped) | `2.03e-3` |

At 0 % the tank is time invariant to rounding, so *LTI* is the correct word and
commutation is the correct test. At the shipped depth the same tank fails shift
invariance by twelve orders of magnitude, because the delay lengths and the
feedback rotation move under an LFO. **An LTI filter and a time-varying system
do not generally commute**, so the 4.2e-16 figure does not extend to the
default product.

### Classification

> **STATIC / LTI CONTROL TEST.**

The suite is kept and its gate is unchanged. Its title and header now say so,
and it is not cited as evidence about the modulated product.

---

## 2. The corrected product-level claim

**What is claimed.** Low Cut and High Cut are *excitation filters*. They sit
outside the feedback network. Their parameters do not enter any tank control
value — not RT60, not the damping ratios or shelf coefficients, not the
feedback gains, not the delay lengths, not the modulation depth or rate — and
therefore appear nowhere in the tank's recurrence equations. **They cannot
change the decay law.**

**What is not claimed.** That a cut can never change a *measured* RT60. That is
too strong for a finite, modulated, modal system. The cuts can change:

- modal excitation weighting
- the energy observed in a band
- what a finite-band RT60 estimator reports
- the time-varying output weighting of the modulated system

The Stage 2 harness measured the third of those at up to 9 %, and showed by a
post-tank control that the change belonged to the estimator.

---

## 3. Structural verification

On the **complete production tank**, `MorphingStereoFdn<8>`, at the shipped
**Mod Depth 30 %**. Two independent checks.

**Snapshot.** 234 tank control values — delay lengths, feedback gains (target
and current), effective loop delays, RT60, damping ratios and activity, every
shelf spec and normalisation, allpass depth, rate, mode, per-line coefficients,
lane rates and added loop delay, matrix depth and rate, Size, morph position
and phase, compensation gains, both banks — captured across a full sweep of Low
Cut, High Cut and Pre-Delay, with audio actually running.

> Every element identical, in every case.

**Replay.** The signal reaching `tank.processBlock()` is replayed into a tank
that was never told a cut parameter at all.

> Worst difference **0** — bit-identical, at Mod Depth 30 %.

Modulation does not weaken either check, because neither is an algebraic
commutation argument. They are routing facts.

---

## 4. The RT60 harness limitation

Unchanged and still recorded. The Stage 2 harness does not reproduce Phase 7's
analytic expectation: 12 kHz reads ~2.29 s against ~1.82 s analytic, and the
500 Hz – 12 kHz bands compress into 2.15 – 2.29 s.

Therefore: **no quantitative ≤ 1 % RT60 gate was created from it**, and it is
not cited as proof of the damping/cut distinction. The structural routing
guarantee plus the static LTI control test carry that claim.

---

## 5. Bypass state — the defect, and the fix

### What the state did

| phase | behaviour |
|---|---|
| while bypassed | **(B) FROZEN** — `process()` returns before touching it; nothing clears it; the setters recompute coefficients only |
| on re-entry, before Stage 2.5 | re-used, i.e. the first output after a bypass was computed from history from before the bypass |

**Measured, by temporarily reverting the fix.** Same signal, same automation
from the bypass onward, two different histories before it:

| re-entry | worst difference | samples differing |
|---|---|---|
| Low Cut 21 Hz | 0.03110 (6.9 % of peak) | **9600 of 9600** |
| Low Cut 30 Hz | 0.03109 | 9600 of 9600 |
| Low Cut 1000 Hz | 0.02924 | 9600 of 9600 |
| High Cut 19 kHz | 0.00041 | 9600 of 9600 |
| High Cut 500 Hz | 0.00462 | 9600 of 9600 |
| both, 120/9k | 0.02585 | 9600 of 9600 |

A real, avoidable transient: audio from before a two-second bypass was audible
after it, and once the state diverged it stayed diverged for the whole active
period.

### The fix

Six lines. On each bypass → active edge, the first sample of the active period
**primes** the state so that that sample passes through unchanged:

```
high cut   s = x                      (a·s + (1-a)·x = x exactly)
low cut    s = x·(a − 1/k)/a          (solves (x − s′)·k = x)
```

Both depend only on the sample in hand, so nothing can carry across a bypass,
and the first sample is continuous with the bypassed signal rather than
stepping away from it.

Priming does **not** remove the settling that follows — a low cut entering on a
bass-heavy signal still takes its time constant to remove the lows. That is the
filter doing its job, it is smooth by construction, and it is what a tonal
change across OFF sounds like.

Cost: one branch per block, hoisted out of the sample loop. It cannot reach the
neutral path, which returns before the loop exists.

### What this preserved

- exact neutral-path bit identity — 0 of 48000 samples differ
- the parameter contract — untouched
- the one-pole topology — untouched
- commutation — still exactly `4.161784462e-16`, because the registered
  excitation is Hann-windowed and starts at zero
- all 40 Stage 2 Phase 8 checks

No bypass crossfade or morph was added. Measurement did not call for one.

---

## 6. Endpoint-transition measurements

Measured against the right reference, after two wrong ones were discarded —
see §8.

```
excess = actualStep − max(idealStep, inputStep)
```

where *idealStep* is the step an instantaneous switch made by an
already-settled filter would take, and *inputStep* is the signal's own step.
The transition index is swept across 48 samples (one full period of the 1 kHz
probe), on sine, noise and a musical signal.

**Low Cut** — 20 → 21, 20 → 30, 30 → 20, 20 → 1000, 1000 → 20
**High Cut** — 20k → 19.9k, 20k → 19k, 19k → 20k, 20k → 500, 500 → 20k

> Worst excess, both controls, all 30 combinations: **0**, to within one
> floating-point rounding residual (`1.6e-14 %` of peak).

Every move that leaves an active filter for OFF steps exactly the ideal step —
the unavoidable timbral discontinuity. Every move that enters a filter steps
exactly the input's own step. Neither is avoidable; nothing exceeds them.

**Continuous automation touching the endpoints**, 3 Hz triangle onto the
endpoint, corner applied every 64 samples, over 2 seconds:

| control | worst output step ÷ input step |
|---|---|
| Low Cut 20 ↔ 30 | 0.9962 |
| Low Cut 20 ↔ 1000 | 0.9261 |
| High Cut 20k ↔ 19k | 0.9035 |
| High Cut 20k ↔ 500 | 0.8804 |

A one-pole cannot make its input steeper, and the sweep passes through OFF on
every cycle. A ratio at or below 1 therefore means the repeated switching adds
no step of its own.

---

## 7. Stale state

The decisive test, and it needs no tolerance. Two runs get the same signal and
the same automation from the bypass onward, and differ only in what the filter
did *before* a two-second bypass: one charged at a corner holding real energy,
the other bypassed throughout.

| re-entry | charged at | worst difference | samples differing |
|---|---|---|---|
| Low Cut 21 Hz | 1000 Hz | 0 | 0 of 9600 |
| Low Cut 30 Hz | 1000 Hz | 0 | 0 of 9600 |
| Low Cut 1000 Hz | 1000 Hz | 0 | 0 of 9600 |
| High Cut 19 kHz | 500 Hz | 0 | 0 of 9600 |
| High Cut 500 Hz | 500 Hz | 0 | 0 of 9600 |
| both, 120/9k | 300/2000 | 0 | 0 of 9600 |

Bit for bit. The audio after a bypass does not depend on the audio before it.

Noise is used deliberately: the first version of this test used a musical
signal whose period divided the bypass length, which made two different
histories agree by accident and reported a clean result for the wrong reason.

Acceptance, item by item: no isolated click (§6), no stale-state burst (above),
no NaN or Inf (every suite), no abnormal single-sample spike (§6), no
discontinuity exceeding the underlying signal transition (§6).

---

## 8. Test-design errors made and corrected in this stage

Recorded because three Stage 2 failures were also test defects, and the pattern
matters more than any single number.

1. **Wrong click reference, twice.** First "the largest step the destination
   run makes anyway" — which makes any entry into a smoothing filter look
   enormous and lets a real artifact hide under a steep waveform. Then "the
   ideal switch alone" — which penalises passing the current sample through,
   although that steps no further than the input already did. Both read passing
   behaviour as failure. The reference is now `max(ideal, input)`.
2. **One transition index.** Every transition was measured at sample 4800,
   which for a 1 kHz tone at 48 kHz is exactly a zero crossing — the quietest
   instant available, and the one place a switching artifact is smallest. Now
   swept across a full period.
3. **A periodic stale-state signal.** The musical test signal's period divided
   the bypass length, so the charged and cold histories coincided and the test
   reported 0.0000 for a filter that genuinely carried state. Replaced with
   noise.
4. **Conflating settling with staleness.** "Peak difference from a settled run"
   was labelled a stale-state burst; most of it is legitimate settling. The two
   are now measured separately, and only the one that must be zero is gated.

Two gates were replaced with justified ones: the click gate (above), and the
continuous-automation reference (the input's own steepness, which a one-pole
provably cannot exceed).

---

## 9. Pre-delay

Unchanged. No regression found, none looked for beyond the focused regression
run. Stage 2's accepted results stand.

## 10. CPU, phrased as incremental Phase 8 overhead

These are **added** costs, not total plugin cost.

| | ns/sample | relative to the tank |
|---|---|---|
| Phase 7 tank | 199.923 | — |
| Phase 8 **added**, active | 7.845 | 3.92 % |
| Phase 8 **added**, neutral | 2.368 | 1.18 % |

Re-measured after the priming change, in a separate run: tank 206.443, added
active 8.528 (4.13 %), added neutral 2.638 (1.28 %). The absolute figures move
with the machine; the ratio does not. Priming adds one branch per block and is
below the noise floor of this measurement.
