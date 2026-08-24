# Phase 7 — Damping: plan

**Written 2026-08-24. No DSP has been implemented. Nothing was built, rendered
or measured for this document — every number below is derived analytically from
the code and is marked as an estimate until measured.**

## Scope

In: **Low Damp · Low Damp Freq · High Damp · High Damp Freq**, and the
frequency-dependent decay system behind them.

Out, and staying in the feature-parity backlog: Pre-Delay · Low Cut · High Cut ·
Late Diffusion · Density · Attack · Mode · Color · Early Reflections.

Parameter IDs, ranges and defaults do not change.

---

# 1. Current DSP seam audit

## 1.1 Position in the loop

[`FdnCore::processSample`](../Source/DSP/FdnCore.h#L288), one sample:

| Step | Line | What |
|---|---|---|
| 0 | [294](../Source/DSP/FdnCore.h#L294) | `allpass.advance()` — once for the whole bank, before any line, so every section sees the same modulation instant |
| 1 | [298](../Source/DSP/FdnCore.h#L298) | `lines[i].read()` — integer delay read |
| 2 | [307](../Source/DSP/FdnCore.h#L307) | `allpass.process(delayed, i)` — PHASE 5B time-varying allpass |
| **3** | **[308](../Source/DSP/FdnCore.h#L308)** | **`processLineState(phased, i)` — the PHASE 7 damping slot, currently identity** |
| 4 | [310](../Source/DSP/FdnCore.h#L310) | `× gains[i].getNextValue()` — per-line decay gain |
| 5 | [311](../Source/DSP/FdnCore.h#L311) | `stateOut[i] = v[i]` — what the stereo readout vectors see |
| 6 | [314](../Source/DSP/FdnCore.h#L314) | `matrix.process(v)` — orthogonal mixing |
| 7 | [316](../Source/DSP/FdnCore.h#L316) | `lines[i].write(v[i] + injection[i] + guards[i].next())` |

The order is stated in a comment at
[FdnCore.h:300–306](../Source/DSP/FdnCore.h#L300) and matches the code exactly.

**Damping sits before the decay gain and before the matrix.** Both facts matter
and both are favourable — see 4.1 and 5.1.

## 1.2 Per-line state that exists today

| State | Where | Per line? |
|---|---|---|
| `lines[N]` — `FdnDelayLine` | [392](../Source/DSP/FdnCore.h#L392) | yes |
| `gains[N]` — `LinearSmoother<double>`, 20 ms ramp | [393](../Source/DSP/FdnCore.h#L393) | yes |
| `guards[N]` — `AlternatingGuard`, anti-denormal | [394](../Source/DSP/FdnCore.h#L394) | yes |
| `allpass.sections[N × maxSections]` | AllpassPhaseBank.h:338 | yes |
| `allpass.lanes[N]`, `engagement[N]`, `currentCoefficient[N]` | 339–341 | yes |
| `matrix` | [396](../Source/DSP/FdnCore.h#L396) | **no — shared** |

**There is no per-line filter state at all.** Phase 7 introduces the first.

## 1.3 The seam cannot hold state as written — first thing to fix

```cpp
/** Seam for PHASE 7 per-line damping. Compiles to nothing today. */
[[nodiscard]] static double processLineState (double x, int) noexcept { return x; }
```
— [FdnCore.h:349](../Source/DSP/FdnCore.h#L349)

It is **`static`**. A static member function cannot touch per-line filter state.
The seam was shaped for a stateless transform, and damping is not one.

The fix is a signature change, not a redesign: drop `static`, keep the name,
arguments and call site. The call at
[line 308](../Source/DSP/FdnCore.h#L308) does not change. It stays non-virtual
and inlinable, so the "costs nothing" property is preserved when damping is
inactive.

This is the smallest possible edit to the seam and it is the correct one. **The
seam is not being replaced or moved.**

## 1.4 Relationship to the current RT60 gain

Gains come from [`applyGains`](../Source/DSP/FdnCore.h#L364) →
[`fdn::feedbackGainForRt60`](../Source/DSP/FdnTuning.h#L120):

```
g_i = 10^(-3 · d_i / RT60)      clamped to maxFeedbackGain = 0.9995
```

with `d_i` the **effective** loop delay —
[`getEffectiveLoopDelaySamples`](../Source/DSP/FdnCore.h#L241) — which already
adds the allpass group delay. That correction exists because ignoring it made
measured RT60 about **1.7 % long**
([FdnCore.h:353–363](../Source/DSP/FdnCore.h#L353)). The same class of error
applies to damping filters, which also add group delay. See risk 6.

Because damping is applied *before* the gain, the two simply multiply. Per pass
through line *i* at frequency ω the signal is scaled by

```
G_i(ω) = g_i · |H_i(ω)|
```

which gives the equation the whole phase rests on:

```
   1              1          -log10 |H_i(ω)|
--------  =  ----------  +  -----------------
RT60(ω)      RT60_mid            3 · d_i
```

Two consequences follow immediately:

- **If `|H_i(ω)| ≤ 1` for all ω, then `RT60(ω) ≤ RT60_mid` for all ω.** Damping
  can only ever shorten a band. It can never add energy, at any setting.
- For the tail to keep decaying as *one* thing rather than as eight, the
  quantity `log|H_i(ω)| / d_i` must be **the same for every line**. A single
  identical filter on all eight lines would give eight different high-band
  decay rates. **The filter must be derived per line from that line's delay
  length**, exactly as the decay gain already is.

That second point is the single most important design constraint in the phase,
and it is easy to get wrong.

## 1.5 Safest insertion point

**Step 3, in place, with `processLineState` made non-static.** Nothing else
moves.

Why this point and not another:

- **Before the gain** — the gain stays the only place broadband decay is set,
  so `feedbackGainForRt60` keeps its meaning and `getMaxGain()` keeps being the
  stability quantity it claims to be.
- **After the allpass** — the allpass is energy-preserving by construction;
  putting damping before it would make the allpass operate on an
  already-coloured signal for no benefit.
- **Before the matrix** — the matrix is orthogonal, and the RT60 derivation
  ([FdnTuning.h:103–118](../Source/DSP/FdnTuning.h#L103)) explicitly assumes
  energy does not leak between lines at a rate the formula cannot see. Damping
  after the matrix would break that assumption. Damping before it is per-line
  and the formula survives.
- **`stateOut` is written after damping**, so the stereo readout sees the damped
  signal. Correct: the output should be damped, not just the recirculating copy.

---

# 2. Damping topology options

Three were considered against Aurora's actual topology. None is copied from any
product; the choice is driven by the equation in 1.4 and by the four parameters
that already exist.

## A) Per-line one-pole lowpass + one-pole highpass

The classic minimal absorptive FDN. One lowpass for High Damp, one highpass for
Low Damp.

| | |
|---|---|
| RT60 controllability | Poor. **A lowpass has no unity plateau** — its magnitude is already below 1 far below the cutoff, so mid-band RT60 sags the moment High Damp is raised. |
| Phase | Gentle, first order. Fine. |
| CPU | Lowest — ~2 filters × 8 lines. |
| Stability | Excellent, `|H| ≤ 1` trivially. |
| Automation | Excellent, one-pole coefficient changes cause no ringing. |
| Musicality | Good tail, but the control lies: "High Damp" darkens *and* shortens everything. |
| Mapping | Awkward — the four parameters do not map onto it cleanly. |
| **Verdict** | **Rejected.** Fails the mid-band preservation requirement structurally, not by tuning. |

## B) Per-line cascaded first-order low-shelf + high-shelf — **SELECTED**

One low-shelf and one high-shelf per line. Mid-band gain is unity **by
construction**; the shelves pull down only outside their corners.

| | |
|---|---|
| RT60 controllability | Excellent. Mid plateau is unity, so `feedbackGainForRt60` stays exactly correct in the mid band with no compensation. Low and high bands are set independently. |
| Phase | First order, gentle, minimum phase. No pre-ring. |
| CPU | ~2 one-poles + 4 multiplies per line per sample. See §7. |
| Stability | Provable, see §5. |
| Automation | One-pole coefficients; no transient on coefficient change. |
| Musicality | Strong — and specifically strong *because* it is inside a feedback loop. See below. |
| Mapping | **Exact.** The four parameters *are* the two shelf gains and the two corner frequencies. |
| Stereo | Both banks derive from the same per-line rule; no L/R asymmetry introduced. |
| FDN interaction | Per-line, before the matrix, orthogonality assumption intact. |

**Why first order is enough — and better.** The filter is applied once per pass.
After *k* circulations the effective response is `|H(ω)|^k`, so a gentle 6 dB/oct
shelf becomes progressively steeper *with time*. A steep per-pass filter is
unnecessary and would add ringing inside the loop. This is a property of
feedback topology, not a compromise.

## C) Split-band — crossover into three bands, per-band gain, recombine

| | |
|---|---|
| RT60 controllability | Excellent, arguably the most direct. |
| Phase | **Problematic.** Crossover networks are magnitude-flat only in the *sum*; the FDN reinjects the recombined signal, so crossover phase error accumulates over passes. |
| CPU | Highest — 3× state, 2 crossovers per line. |
| Stability | Needs a bound on the recombined response, which is harder to prove than a shelf cascade. |
| Musicality | Would allow a mid band that differs from unity — not asked for. |
| **Verdict** | **Rejected.** Everything it offers over B is something the four parameters do not expose, and it pays for it in phase behaviour inside a feedback loop. |

## Selected form

Per line *i*, cascade:

```
high shelf:   H_hi(z) = G_hi,i + (1 - G_hi,i) · LP(z, f_high)
low  shelf:   H_lo(z) = G_lo,i + (1 - G_lo,i) · (1 - LP(z, f_low))

one-pole:     LP(z, f) = (1 - a) / (1 - a·z⁻¹),   a = exp(-2π f / fs)
```

`H_hi` is exactly 1 at DC and exactly `G_hi` at Nyquist. `H_lo` is exactly
`G_lo` at DC and approaches 1 above its corner.

Per line, per sample: 2 one-pole states, ~8 multiplies, ~4 adds.

---

# 3. Parameter semantics

Parameter IDs, ranges, defaults: **unchanged**.

| Parameter | Range | Default | Skew centre |
|---|---|---|---|
| Low Damp | 0–100 % | **20 %** | linear |
| High Damp | 0–100 % | **50 %** | linear |
| Low Damp Freq | 20–1000 Hz | **200 Hz** | 200 Hz |
| High Damp Freq | 500–20000 Hz | **6000 Hz** | 4000 Hz |

## 3.1 What Low Damp and High Damp mean

Three candidates were on the table: attenuation amount, RT60 ratio, or raw
damping coefficient.

**Selected: RT60 ratio.**

> **High Damp is the ratio of the high-band decay time to the mid-band decay
> time. Low Damp is the same for the low band.**

Rejected alternatives, with reasons:

- **Attenuation amount (dB per pass).** Meaningless to a user, and the audible
  result depends on delay length, so the same number does different things on
  different lines and at different Size settings.
- **Raw damping coefficient.** A filter coefficient is not a musical quantity,
  and its perceptual effect changes with sample rate.

RT60 ratio is the only one of the three that is **a time the user can hear**,
and it is stable under Size, Decay and sample-rate changes by construction.

## 3.2 Percentage → ratio mapping

Decay times are perceived multiplicatively — the same argument the project
already makes for `LogSmoother`
([SmoothedParameter.h:33–37](../Source/DSP/SmoothedParameter.h#L33)). So the
mapping is exponential, not linear:

```
ratio(p) = r_min ^ p          p = damp% / 100
```

- `p = 0` → ratio 1.0 — no damping, exactly. **A zeroed control is exactly
  transparent**, which is a property worth having.
- `p = 1` → ratio `r_min`.
- Monotone, smooth, and equal control steps give equal *musical* steps.

Proposed starting values, **to be confirmed by listening in Phase 7 and then
recorded in a tuning header with the measurement that chose them**, as every
other tuning constant in this project is:

| | `r_min` | ratio at default | band RT60 at Decay 2.5 s |
|---|---|---|---|
| High Damp | 0.125 | 0.354 (at 50 %) | **0.88 s** |
| Low Damp | 0.25 | 0.758 (at 20 %) | **1.90 s** |

Defaults therefore give a mildly tightened low end and a natural darkening
above 6 kHz, with the mid band untouched.

## 3.3 Worked example — why per-line derivation is mandatory

At Decay 2.5 s, High Damp 50 % (target high-band RT60 0.88 s), the required
per-pass high-shelf gain from the equation in 1.4:

| line | delay | required `G_hi` | per pass |
|---|---|---|---|
| shortest | 26.27 ms | 0.876 | −1.15 dB |
| longest | 82.92 ms | 0.658 | −3.6 dB |

**A factor of three between the shortest and longest line.** A single shared
filter would leave the eight lines decaying at eight different rates in the high
band — an audibly uneven, phasey tail. This is the concrete form of the
constraint in 1.4.

## 3.4 A deliberate limitation, stated plainly

**Damping is subtractive only.** `|H| ≤ 1` means a band can be shortened, never
lengthened. There is no setting that makes the low end ring *longer* than the
mid band.

This is the price of unconditional stability and it is worth paying. If a
"long lows" character is wanted later, it has to come from shortening mid and
high instead — which is a different control, not this one.

---

# 4. RT60 model

## 4.1 Mid band is preserved structurally, not by correction

Because the shelf cascade has unity mid-band gain, `g_i` continues to mean
exactly what it means today, and the existing calibration is untouched.

**This is the most important property of topology B.** The requirement "default
damping must not disturb the mid-band decay target" is met by the shape of the
filter, not by a compensation term that could be wrong.

## 4.2 The residual, and how it is removed

The low shelf does not reach *exactly* unity at Nyquist: its plateau is

```
P_i = G_lo,i + (1 - G_lo,i) · 2a / (1 + a)
```

At the default 200 Hz corner (`a = 0.9742` at 48 kHz), estimated worst case
across the delay set is **P ≈ 0.9985, i.e. −0.013 dB per pass**. Compounded over
12 passes per second on the longest line against a 24 dB/s baseline, that is an
estimated **~0.7 % short** in the mid band. Small, but not zero, and this
project has corrected a 1.7 % error before.

**Removal is free.** `P_i` is available in closed form, so:

```
g_i = min(maxFeedbackGain,  feedbackGainForRt60(d_i, RT60) / P_i)
```

Since `P_i ≤ 1` this raises `g_i` slightly, and the maximum of the combined
per-pass response returns to exactly `g_i` — so **the stability bound is
unchanged** (§5.1) while the mid band becomes exact rather than approximate.

This reuses [`applyGains`](../Source/DSP/FdnCore.h#L364) as it stands. It also
means **`applyGains` must be called whenever a damping parameter changes**,
exactly as `setAllpassDepth` already calls it
([FdnCore.h:196–200](../Source/DSP/FdnCore.h#L196)). The precedent and the
mechanism both already exist.

## 4.3 Measurement plan

Baseline: **Decay 2.5 s**, the existing default and the value the skew is
centred on.

| Band | Method | Target |
|---|---|---|
| Low | Schroeder backward integration on a band-limited impulse response, band centred on `lowDampFreq / 2` | within tolerance of `ratio_low × RT60_mid` |
| Mid | same, band centred at 1 kHz | **within 2 % of the Decay parameter, at every damping setting** |
| High | same, band centred on `2 × highDampFreq` | within tolerance of `ratio_high × RT60_mid` |

Bands are measured well away from the corners so the shelf transition is not
inside the measurement band. Crossover behaviour is measured separately, by
sweeping the corner and confirming the band RT60 moves monotonically with it.

Mid-band tolerance is the strict one and is the phase's central claim. Low and
high band tolerances should be set once the mapping constants in 3.2 are fixed,
and set from measured repeatability — not guessed.

Reuse: the Phase 6 harness already does exact-frequency sliding-window analysis
with a contamination guard, and the modal decay estimator is in `Tests/`. This
is not new instrumentation.

## 4.4 Crossover overlap semantics — normative

The parameter ranges overlap: `lowDampFreq` reaches 1000 Hz and `highDampFreq`
starts at 500 Hz. Two regions therefore exist and they are **not** governed by
the same acceptance criterion.

### Normal ordering — `lowDampFreq < highDampFreq`, with a meaningful gap

A real mid plateau exists between the two corners.

- **Decay is the mid-band RT60 anchor.**
- **Mid-band RT60 tolerance is ±2 %.**
- The RT60-ratio interpretation of Low Damp and High Damp (§3.1) is **exact**.

This is the region the exit criterion in §11 refers to.

### Overlapping or reversed ordering

**No parameter is clamped. No range is changed. The user's controls are never
silently reordered.** A control that quietly rewrites itself is worse than one
that does something unusual, and the ranges are frozen.

Documented semantics for this region:

> **Two subtractive shelves acting simultaneously.**

Both shelves still only attenuate, and they overlap rather than leaving a
plateau. Consequently there is **no physical mid plateau**, and "the mid-band
RT60" is not a quantity the tank possesses there.

Mandatory in this region:

- Stability.
- `max |H(ω)| ≤ 1`.
- Monotonic parameter response — more damping never produces less damping.
- No NaN, no Inf, no clicks.

**Not applicable in this region:** the ±2 % mid-band RT60 exit criterion. It is
withheld because the physical structure it measures does not exist, not because
the result would be inconvenient.

The exact RT60-ratio interpretation likewise holds only where the plateau
exists. In the overlap region the two shelves compose, and the resulting decay
is what the composition gives.

**Extreme overlap is measured and reported separately**, as its own result — not
folded into the normal-ordering measurements and not omitted.

## 4.5 Compensation decision

**No RT60 compensation term is planned beyond the `P_i` division in 4.2.** The
mid band is preserved by construction; the low and high bands are *supposed* to
change — that is the feature. If the measurements in 4.3 show mid-band error
outside 2 % after the `P_i` division, the cause is a modelling error to be found,
**not** a compensation constant to be tuned. Stated in advance, deliberately.

---

# 5. Stability strategy

## 5.1 The proof

Today: `ρ(A·G) ≤ ‖A·G‖₂ = max(g_i) < 1`, with A orthogonal
([FdnCore.h:34](../Source/DSP/FdnCore.h#L34)).

With damping the per-line operator becomes `g_i · H_i(z)`. If `|H_i(ω)| ≤ 1` for
all ω then

```
‖A · G · H‖₂  ≤  max(g_i) · max|H_i(ω)|  ≤  max(g_i)  ≤  0.9995  <  1
```

so **the existing stability argument carries over unchanged**. No new margin, no
new clamp, no new failure mode.

## 5.2 Why `|H| ≤ 1` holds by construction

Both shelves have the form `H = G + (1 - G)·F`, with `G ∈ [0, 1]` and `F` either
the one-pole lowpass or its complement. By the triangle inequality, since `G`
and `(1 - G)` are non-negative reals:

```
|H(ω)| ≤ G + (1 - G)·|F(ω)| ≤ G + (1 - G) = 1
```

given `|F(ω)| ≤ 1`. For the one-pole lowpass `|LP| ≤ 1` with its maximum at DC.
For the complement, `|1 - LP|` has its maximum at Nyquist, `2a/(1+a)`, which is
`≤ 1` for all `a ≤ 1`.

**This holds for every parameter value, every sample rate and every combination
— no coefficient region needs to be excluded.** It is not a tuning result.

The proof must still be **verified numerically over a dense grid** of both
amounts × both frequencies × supported sample rates. A proof and a test are not
substitutes for each other.

## 5.3 Edge cases

| Case | Behaviour | Why safe |
|---|---|---|
| Damp = 0 % | `G = 1` → `H(z) = 1` exactly | Bit-exact identity. Zeroed controls are exactly transparent. |
| Damp = 100 % | `G = r_min^1` derived per line; still `> 0` | `|H| ≤ 1` unchanged. |
| DC | `H_hi = 1`, `H_lo = G_lo` | Bounded. |
| Nyquist | `H_hi = G_hi`, `H_lo ≈ 1` | Bounded. |
| `f_c` → 20 Hz | `a → 0.9974` at 48 kHz | `a < 1` always; never unstable. |
| `f_c` → 20 kHz at 44.1 kHz | `a ≈ 0.058` | Valid, though the corner is warped — documented, not corrected. |
| High sample rates (96/192 kHz) | `a = exp(-2πf/fs)` recomputed | Impulse-invariant one-pole: `a ∈ (0,1)` for any `f > 0`. **No pre-warping blow-up, unlike a bilinear biquad.** |
| Decay 100 s (max) | `g_i` → 0.9982, headroom to the clamp is 0.18 % | The `P_i` division raises `g_i`; **the `min(maxFeedbackGain, …)` clamp must be applied after it.** See risk 8. |
| Denormals | Filter state is new recirculating state | `ScopedFtz` covers it; the `AlternatingGuard` at the delay write feeds the filter, so its input never decays into the subnormal range. Filter state must be cleared in `reset()` and initialised in `prepare()`. |

## 5.4 Explicit requirement

At **every** combination of Low Damp and High Damp at their minimum and maximum,
across Decay 0.1 s to 100 s and all supported sample rates, the measured maximum
of `g_i · |H_i(ω)|` over a dense frequency sweep must stay **below
`maxFeedbackGain`**. Automated, exhaustive, not sampled.

---

# 6. Automation and smoothing strategy

The architecture already prescribes the answer. From
[SmoothedParameter.h:16–45](../Source/DSP/SmoothedParameter.h#L16):

| Quantity | Smoother | Rate | The docs already say so |
|---|---|---|---|
| Low Damp, High Damp (0..1) | `LinearSmoother` | audio | *"normalised 0..1 coefficients such as diffusion or **damping amount**"* |
| Low Damp Freq, High Damp Freq | `LogSmoother` | control | *"Frequencies and times are perceived multiplicatively… equal time means equal ratio"* |

**Smoothing domain for the cutoffs: log-frequency**, recomputing `a` from the
smoothed Hz at each control tick. Not linear Hz (a sweep would crawl at the top
and lurch at the bottom), and not direct interpolation of `a` (which is neither
musical nor cheaper).

## 6.1 The control grid, and a correctness requirement

Control-rate updates use `modulation::controlRatePeriod = 32`
([ModulationTuning.h:216](../Source/DSP/ModulationTuning.h#L216)) — 0.67 ms at
48 kHz.

**The counter must be a free-running sample index, not a block boundary.** This
is stated as a correctness requirement, not an optimisation:

> *"the same automation must render identically at any block size"* —
> [ModulationTuning.h:210–214](../Source/DSP/ModulationTuning.h#L210)

[`ModulatedFeedbackMatrix.h:269`](../Source/DSP/ModulatedFeedbackMatrix.h#L269)
already does exactly this and is the pattern to follow. Note that the processor
currently pushes tank settings once per *chunk*, and the chunk is the host block
([PluginProcessor.cpp:372–376](../Source/PluginProcessor.cpp#L372)) — which can
be 2048 samples, 42 ms. **Damping coefficients must not be updated on that
grid.**

## 6.2 Block-size independence — scope, normative

The requirement applies at the **core** level and is deliberately weaker at the
**plugin** level. Conflating the two would demand something of a host that a
host does not offer.

### Core — bit-identical, mandatory

> Given the **same sample-accurate parameter target schedule**, processing it in
> chunks of **64 / 128 / 512 / 2048** samples must produce **bit-identical**
> output.

This is a correctness test of `FdnCore` and of the smoothing, and nothing else.
It is what the free-running control-rate counter in §6.1 exists to guarantee: a
hard equality check, no tolerance. If it fails, a smoother is keyed to block
boundaries and that is a bug.

### Plugin / host — not bit-identical

Host automation targets arrive **on block boundaries**, so a different host
block size delivers a genuinely different target schedule. Requiring
bit-identical renders across host block sizes would be requiring the host to
behave in a way it does not.

**Bit-identical rendering across host block sizes is therefore not declared a
requirement.** What is required at plugin level:

- No clicks or crackles.
- No unstable coefficient at any block size.
- Correct final value once automation settles.
- A musically continuous sweep.
- No pathological block-size dependence — no size at which behaviour changes in
  kind rather than in the small.

## 6.3 Why clicks are structurally unlikely here

- A **one-pole's** coefficient can change discontinuously without ringing — its
  state is continuous and there is no resonant memory. A biquad shelf would not
  have this property. It is one of the reasons B was chosen over a biquad form.
- The shelf **gains** enter the audio path through `LinearSmoother` at audio
  rate.
- The **decay gains**, which move when damping changes via §4.2, already ramp
  over 20 ms (`gainSmoothingSeconds = 0.02`,
  [FdnCore.h:83](../Source/DSP/FdnCore.h#L83)).

Zipper noise, coefficient discontinuity and burst are each addressed by a
different mechanism, all three of which already exist. Nothing new is required —
but all three must be **tested**, per §10-C.

---

# 7. Expected CPU cost

**Estimate. Not measured. Must be measured after implementation.**

Per line, per sample: 2 one-pole filters + 2 shelf mixes ≈ **8 multiplies,
4 adds**. Across 8 lines: **~96 flops/sample**, against an existing tank cost
estimated at 250–350 flops/sample (the 8×8 matrix alone is ~120).

| | |
|---|---|
| Current measured baseline | **~1.05 %** |
| Estimated after Phase 7 | **~1.25 – 1.45 %** |
| **During a Size morph** | **roughly double the damping cost** |

That last row is easy to miss: `MorphingStereoFdn` runs **two banks** during a
Size transition, so both carry damping filters. The transition is bounded
(`morphPrimeMaxSeconds = 1.5` plus a 120 ms fade,
[FdnTuning.h:289–295](../Source/DSP/FdnTuning.h#L289)) but the peak is real and
must be measured under a Size automation sweep, not only at rest.

Arithmetic is also unlikely to dominate: the delay lines are the memory-bound
part, and the filters add no memory traffic. The estimate above is deliberately
pessimistic.

---

# 8. Quality benchmark mapping

Quality targets, mapped to
[`Docs/ReverbBenchmarkAndFeatureParity.md`](ReverbBenchmarkAndFeatureParity.md).
No feature is copied from any product; these are acceptance criteria.

| Target | How this design addresses it | Benchmark row |
|---|---|---|
| Natural high-frequency decay | Per-line derivation (§1.4, §3.3) — all eight lines reach the same high-band RT60 | Smooth decay; Frequency-dependent decay correctness |
| No muffled resonant tail at dark settings | Mid plateau is unity; darkening removes high energy without lifting a resonance. First-order shelf has no Q | Low modal ringing |
| No harsh ringing at bright settings | `|H| ≤ 1` — no band is ever boosted, at any setting | Controlled metallicity |
| Musically smooth damping sweep | Exponential %→ratio (§3.2) + log-domain cutoff smoothing (§6) | Automation safety |
| Controllable frequency-dependent decay | Parameter is a decay *ratio*, invariant under Size and Decay | Frequency-dependent decay correctness |
| Stable at extremes | §5.1 proof + exhaustive §5.4 sweep | No NaN/Inf; automation safety |

**Frequency-dependent decay correctness** is currently the single NOT STARTED
row in the quality table. Phase 7 is what closes it.

---

# 9. Default character preservation

FU-1's verdict — **CHARACTER ACCEPTABLE** — was established on the **undamped**
tank. Enabling default damping changes the default sound. That is the point of
the phase, and it must be judged rather than assumed.

**The likely direction is favourable.** FU-1's metallic character lives in the
high band, which is precisely where default High Damp acts. Phase 6 already
recorded that the high-band character *should be re-checked in the Phase 7
damping work*, and named Round 2c's nine full-band renders as the ready-made
comparison set
([Phase6-FU1-Listening.md](Phase6-FU1-Listening.md)).

Planned before/after comparison — **nothing rendered yet**:

| Measure | Method | Expectation |
|---|---|---|
| RT60 by band | §4.3 | Mid unchanged within 2 %; low and high move as designed |
| Spectrum | Long-window average of the tail | High band lower; mid unchanged |
| Peak / crest | True peak and crest on identical sources | No increase — damping only removes energy |
| Stereo correlation | Inter-channel correlation on the tail | **Unchanged** — damping is per line and identical in structure across lines; any change indicates a bug |
| Metallicity | Blind listening reusing the Round 2c apparatus — withheld key, pre-registered analysis, content-free render checks | Equal or better |
| Mix 30 % balance | Host listening at the production default | Perceptually equivalent |

Two rules for this comparison, set in advance:

1. **Stereo correlation is a bug detector here, not a taste question.** Damping
   applies the same rule to all lines, so a correlation shift means the per-line
   derivation is wrong.
2. If the listening comparison finds the damped default *better*, that is a
   result to record — **not** a licence to re-open FU-1's closed verdict.

---

# 10. Test matrix

Defined before implementation, deliberately.

## A. Unit / synthetic

- Coefficient stability across a dense grid: both amounts × both frequencies ×
  {44.1, 48, 88.2, 96, 192} kHz.
- **`max|H(ω)| ≤ 1` over a dense frequency sweep, on the whole grid** (§5.4).
- Frequency response: shelf corners land where the parameter says, plateau is
  unity within tolerance, asymptotes hit `G` exactly.
- Extreme values: 0 % (must be **bit-exact identity**), 100 %, both corners at
  both ends, and the overlap region (risk 3).
- Per-line derivation: `log|H_i| / d_i` equal across all eight lines.
- `reset()` clears filter state; `prepare()` initialises it.

## B. RT60

- Decay **0.5 / 2.5 / 5 / 10 s** × {damping off, default, low-heavy,
  high-heavy}.
- Mid band within **2 %** of the Decay parameter in **every** cell — the phase's
  central claim.
- Low and high bands track the ratio mapping.
- Crossover sweep: band RT60 moves monotonically with corner frequency.

## C. Automation

- Low Damp sweep 0→100 %, High Damp sweep 0→100 %, both crossovers swept full
  range, at several speeds.
- No zipper, no discontinuity, no burst.
- **Block-size independence**, scoped per §6.2. **Core:** the same
  sample-accurate target schedule chunked at 64 / 128 / 512 / 2048 must be
  bit-identical — a hard equality check, not a tolerance. **Plugin/host:** not
  bit-identical; no click, no unstable coefficient, correct settled value,
  musically continuous sweep, no pathological block-size dependence.

## D. Regression — must be unchanged

Size · Mod Depth · Width · Mix · decay baseline · stereo correlation ·
NaN/Inf · zero audio-thread allocation · CPU (at rest **and during a Size
morph**) · full existing suite (2121 checks) · pluginval strictness 10.

## E. Host

Short FL Studio regression **after every automated test passes**, on a
production-profile artefact installed through
`Scripts/install-production-vst3.ps1`. Damping-focused, plus the eight checks
from the shipping/build hardening regression.

---

# 11. Phase 7 exit criteria

Declared COMPLETE only when **all** hold:

1. All four damping parameters genuinely connected to DSP.
2. Parameter automation works on all four.
3. Frequency-dependent RT60 measured across low, mid and high bands.
4. **Mid-band Decay target preserved within 2 % at every damping setting** — in
   the normal-ordering region, where a mid plateau physically exists (§4.4). In
   the overlap/reversed region the ±2 % criterion does not apply; stability,
   `max|H| ≤ 1`, monotonicity and freedom from NaN/Inf/clicks do, and the
   behaviour is measured and reported separately.
5. No instability: §5.4 exhaustive sweep passes.
6. No clicks, crackles or zipper on any of the four.
7. CPU acceptable and measured, at rest and during a Size morph.
8. pluginval strictness 10 PASS.
9. Full test suite PASS.
10. FU-1 default character regression judged acceptable against the Round 2c
    baseline.
11. FL Studio host regression PASS.

---

# 12. Open technical risks

| # | Risk | Severity | Response |
|---|---|---|---|
| 1 | `processLineState` is `static` and cannot hold state (§1.3) | Low | Known, first edit, signature-only. |
| 2 | Mid-band exactness depends on the `P_i` term (§4.2) | Medium | Measured, not assumed. Error outside 2 % is a modelling bug, not a constant to tune. |
| 3 | **Crossover overlap.** `lowDampFreq` reaches 1000 Hz and `highDampFreq` starts at 500 Hz, so the two shelves can overlap and there is then **no true mid plateau** | Medium | Parameter ranges are frozen, so this cannot be designed away. Accept, measure the worst case, document. **Do not silently clamp** — that would make the control lie. |
| 4 | **Default character changes.** FU-1's verdict was on the undamped tank | Medium | §9. Likely favourable, but judged by blind listening, not assumed. |
| 5 | Damping is subtractive only — no band can be lengthened (§3.4) | Low | Deliberate; the price of §5.1. Documented as a limitation, not a defect. |
| 6 | Shelf **group delay** shifts effective loop length — the same class of error as the 1.7 % allpass overshoot | Medium | Estimated < 0.1 %, but that is an estimate. Measure; extend `getEffectiveLoopDelaySamples` if it exceeds 0.5 %. |
| 7 | **Dual-bank Size morph** doubles damping cost and needs both banks' filters consistent, or a morph crossfades between differently-damped tanks | Medium | Explicit test: Size automation sweep with damping active, measuring CPU peak and listening for a character step mid-morph. |
| 8 | At Decay 100 s, headroom under `maxFeedbackGain` is only **0.18 %**, and the `P_i` division *raises* `g_i` | **High** | The `min(maxFeedbackGain, …)` clamp must be applied **after** the division. If the clamp engages, mid-band RT60 silently drifts. Must be tested at the extreme, and the clamp made observable. |
| 9 | FU-1 regression baseline was rendered by the harness, and a harness/plugin Early Diffusion parity item is open | Low | Equivalent at shipping defaults, which is the comparison condition. Re-confirm default-equivalence before trusting the comparison. |
| 10 | Corner-frequency warping near Nyquist at 44.1 kHz (§5.3) | Low | Inherent to the impulse-invariant one-pole. Document; do not correct — correcting it would cost the unconditional stability that form provides. |

**Risk 8 is the one to watch.** It is the only one that can silently produce a
wrong decay time rather than an audible or measurable failure.
