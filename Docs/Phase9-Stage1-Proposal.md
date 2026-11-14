# Phase 9 — Stage 1 architecture report

Late Diffusion, Density, Attack, Bloom. Research and measurement only: no
production DSP changed, no parameter added, no existing contract touched.

Every number below comes from `Tests/Phase9Research.cpp`, run against the
Phase 8 tank at its shipping defaults. The measures are the ones this project
already owns — normalised echo density from Phase 3, envelope periodicity from
Phase 6, `flattenDecay` from the delay-set search. No new analyser was
written.

---

## 0. The resolution floor — read everything else against this

Before any comparison, the measures were asked what they can resolve. One
configuration, one impulse response, the same statistic at eleven window
positions across the late field. Nothing about the tank changes between these
numbers, so their spread is the floor.

| measure | spread | range |
|---|---|---|
| normalised echo density | **0.077** | 0.932 – 1.009 |
| envelope periodicity | **3.03** | 21.31 – 24.34 |
| excess kurtosis | **0.090** | −0.046 – 0.044 |

**A control that moves one of these by less than its floor has not been shown
to move it.** This suite exists because the first run of this file quoted a
density difference of 0.0066 as though it meant something, without knowing
whether 0.0066 was resolvable. It is not.

---

## 1. The current Phase-8 late-field architecture

```
input
  -> Low Cut -> High Cut -> Pre-Delay                      (PHASE 8, wet only)
  -> StereoDiffusionNetwork: 4 allpass stages per channel   <- Early Diffusion
       left  1.87 / 2.99 / 4.31 / 5.53 ms   total 14.70 ms
       right 1.87 / 2.53 / 4.87 / 5.19 ms   total 14.46 ms
  -> injection vectors b_L, b_R  (orthogonal sign vectors, unit norm)
  -> FdnCore<8>:
         per line:  integer delay read
                 -> AllpassPhaseBank section(s)   <- IN the feedback loop
                 -> LineDamping shelves           (PHASE 7)
                 -> per-line decay gain
                 -> 8x8 orthogonal feedback matrix
                 -> delay write + injection
  -> readout vectors c_L, c_R
  -> Size compensation -> wet trim -> Width -> Mix
```

`MorphingStereoFdn<8>` holds two `StereoFdn<8>` banks for Size morphing; each
bank owns its own input diffuser and `FdnCore`.

### Where density is created

In the **first ~100 ms**, and almost entirely by the input diffuser and the
FDN's own circulation. Normalised echo density in 30 ms windows:

| configuration | 5 ms | 10 ms | 20 ms | 40 ms | 80 ms | 160 ms | 320 ms | 640 ms |
|---|---|---|---|---|---|---|---|---|
| tank alone, no diffuser | 0.022 | 0.024 | 0.044 | 0.186 | 0.359 | 0.847 | 0.985 | 0.994 |
| Early Diffusion 0 % | — | 0.020 | 0.055 | 0.166 | 0.411 | 0.805 | 0.987 | 0.950 |
| **Early Diffusion 70 %** | 0.285 | 0.451 | 0.709 | 0.843 | 0.939 | 0.970 | 0.932 | 1.022 |
| Early Diffusion 100 % | 0.282 | 0.374 | 0.499 | 0.867 | 0.926 | 0.981 | 0.981 | 1.029 |

The tank alone takes about 160 ms to become noise-equivalent. With the input
diffuser at the shipping 70 % it is 70 % of the way there by 20 ms and
essentially there by 80 ms.

### Where diffusion is created

Three places, and only two of them are reachable from a control:

| | mechanism | in the recurrence? |
|---|---|---|
| input diffuser | 4 allpass stages per channel, coefficient = Early Diffusion | no — excitation only |
| in-loop allpass | `AllpassPhaseBank`, 0.61–1.43 ms sections, coefficient fixed at 0.45 | **yes** |
| feedback matrix | 8×8 orthogonal mixing, every line into every line | yes, structurally fixed |

### Early vs late echo buildup

Early buildup is controlled by the input diffuser's coefficient and by the
delay set's shortest members. Late buildup is controlled by the loop lengths —
i.e. by Size — and by the matrix, which is not a parameter.

### Excitation-only vs recurrence mechanisms

| excitation only | inside the recurrence |
|---|---|
| Low Cut, High Cut, Pre-Delay (Phase 8) | Decay / RT60 |
| Early Diffusion | Low/High Damp and their corners |
| injection vector geometry | Size (delay lengths) |
| | `AllpassPhaseBank` depth, rate, sections, lines |
| | matrix modulation depth and rate |

---

## 2. The exact role of the existing Early Diffusion

It sets the **allpass coefficient** of the four input stages, through
`coefficientForDiffusion` — a linear map onto 0 … 0.75. It changes nothing
else: an allpass has unit magnitude at every coefficient, so the control
cannot change the spectrum, only the phase. It is not a dry/wet blend, and
that is deliberate.

Measured reach, early window (0–30 ms) against late window (300–900 ms):

| Early Diffusion | NED early | NED late | periodicity late | kurtosis late |
|---|---|---|---|---|
| 0 % | — (pre-onset silence) | 0.987 | 22.67 | −0.064 |
| 35 % | 0.101 | 0.943 | 22.11 | −0.029 |
| 70 % | 0.125 | 0.932 | 23.70 | −0.007 |
| 100 % | 0.116 | 0.981 | 20.81 | −0.016 |

Late-window NED spread across the whole control: **0.055 — below the 0.077
floor.** Late periodicity spread: **2.89 — below the 3.03 floor.**

> **Early Diffusion is an excitation control and nothing more.** It owns the
> first ~100 ms of density — at 20 ms it moves NED from 0.044 to 0.709, a
> factor of 16 — and it does not measurably reach the established late field.

That settles half of question 3 in the brief: a Late Diffusion control would
not be a second scaling of the input chain, because the input chain does not
reach the late field. It would have to be a different mechanism in a different
place.

---

## 3. Operational definitions

Stated so that each can be measured, and each measured on the tank.

**Late Diffusion** — the degree to which the *established* late field's
envelope lacks a preferred repetition interval. Measure: `maxEnvelopePeriodicity`
over 300–900 ms, lags 1–150 ms. High value = one lag towers over the rest =
audible metallic repetition. Floor 3.03.

**Density** — the rate at which the response becomes statistically
indistinguishable from noise. Measure: normalised echo density (Abel & Huang)
in 30 ms windows, flattened first, as a function of time. Floor 0.077.

**Attack** — how the late field's *energy* and *density* develop after wet
onset. Measure, and it takes two numbers: (a) the landmark gap, the time
between the short-window energy first reaching −20 dB and −3 dB of its own
peak; and (b) the density-versus-time curve over the same interval. Both are
needed — see §6, mechanism N.

**Bloom** — a second, independent degree of freedom in that same buildup:
onset timing separate from growth rate. Measure: would require two landmarks
that move independently. **No mechanism was found that moves them
independently**, so Bloom has no operational definition on this architecture
yet. That is the finding, not an omission.

---

## 4. Do all four deserve separate controls? No.

| | verdict | the measurement that decides it |
|---|---|---|
| **Late Diffusion** | **candidate, conditional** | in-loop sections move periodicity 23.70 → 12.42, **3.7× the floor** |
| **Density** | **reject** | late NED is 0.93–0.99 in all 16 configurations tested; nothing moves it past the floor. Early density is real but already owned by Early Diffusion, Size and Mod Depth |
| **Attack** | **retain** | Pre-Delay keeps the landmark gap at 13.0 ms while shifting every landmark by exactly 50 ms; an envelope stretches the gap to 20 and 54 ms |
| **Bloom** | **reject for now** | every mechanism tried moves the same single number Attack moves; no second degree of freedom demonstrated |

### Why Density is not a control

Late density is saturated. Across Early Diffusion 0–100 %, Decay 1–6 s, Size
20–100 %, Mod Depth 0–70 %, High Damp 0–100 % and both Late Diffusion
candidates, late NED stays within **0.932 – 1.011** — a range of 0.079,
which is the floor. There is no late-density range to sell, because the tank
already reaches the noise-equivalent limit and cannot exceed it.

Early density is a genuine dimension, and it is already triple-owned:

| control | NED at 20 ms | span | vs floor |
|---|---|---|---|
| Early Diffusion 0 → 70 % | 0.044 → 0.709 | 0.665 | **8.6×** |
| Size 20 → 60 % | 0.956 → 0.709 | 0.247 | **3.2×** |
| Mod Depth 0 → 30 % | 0.501 → 0.709 | 0.208 | **2.7×** |

A Density knob would be a fourth hand on a lever three controls already hold.

---

## 5. Candidate mechanisms evaluated

| | mechanism | result |
|---|---|---|
| A | additional late allpass diffusion, **after** the tank | **measured, rejected** |
| B | variable allpass coefficient strength | **no setter exists**; the in-loop coefficient is fixed at 0.45 by `coefficientCentreFor`. Adding one is small, and is the most promising Stage-2 lever |
| C | variable number of late diffusion stages | **measured via sections per line: the one mechanism that works** |
| D | injection spreading across FDN lines | not measured; structural, and the vectors were chosen by search in Phase 4 — changing them reopens that |
| E | time-staggered injection | not measured; it is Pre-Delay per line, i.e. a Size-like change to effective loop geometry |
| F | feedback-path diffusion | this is what C/B are — the `AllpassPhaseBank` already sits in the feedback path |
| G | extra loop delay / scattering | changes loop length, hence modal spacing and RT60; `loopDelayCorrection` already exists to compensate |
| H | density through delay distribution | **this is Size.** Measured: Size 20 → 60 % moves 20 ms NED by 3.2× the floor |
| I | Attack through late-input envelope | = N in effect, since the field is already dense when it arrives |
| J | Attack through gradual network injection | **not yet distinguishable from N by the landmark metric — see below** |
| K | Bloom through time-varying excitation distribution | not measured; no independent landmark found |
| L | Bloom through a late diffusion ramp | would move periodicity, not the buildup; not a Bloom |
| M | Bloom through multiple paths with different buildup times | the only structurally plausible Bloom, and the most expensive |
| N | wet amplitude envelope | **measured, and the measurement disqualifies it as *the* Attack** |

### A — post-tank allpass diffusion: rejected by measurement

The long 8-stage pattern already in `DiffusionTuning.h` (50.78 ms left,
39.70 ms right) that production does not use, placed after the tank:

| configuration | NED late | periodicity late | RT60 proxy | L/R corr | mono/stereo |
|---|---|---|---|---|---|
| baseline | 0.932 | 23.70 | 2.06 s | −0.027 | −3.13 dB |
| 8 stages @ 35 % | 0.961 | 23.73 | 2.06 s | | |
| 8 stages @ 70 % | 1.011 | 22.41 | 2.06 s | −0.007 | −3.04 dB |
| 8 stages @ 100 % | 0.998 | 24.77 | 2.14 s | +0.022 | −2.91 dB |

Every density and periodicity change is **inside the floor**. The decay is
untouched, as expected — it is outside the loop. And channel correlation
drifts from −0.027 toward +0.022, i.e. slightly *less* decorrelated at full
strength.

> An allpass chain after the tank re-phases a signal that is already
> noise-equivalent. It cannot add density that is already saturated and it
> does not measurably reduce periodicity. It buys nothing and costs ~100
> ns/sample. **Rejected.**

### C — in-loop sections: the one mechanism that works

| configuration | NED late | periodicity late | kurtosis late | RT60 proxy |
|---|---|---|---|---|
| baseline, 1 section | 0.932 | 23.70 | −0.007 | 2.06 s |
| **2 sections per line** | 0.970 | **12.42** | 0.041 | 2.02 s |

Periodicity falls by **11.3, which is 3.7× the floor**, while density stays
inside the floor. **That is the separation the brief's §14 asks for**: in this
topology Late Diffusion and Density are distinguishable, because one moves and
the other has nowhere left to go.

And the RT60 proxy moves 2.06 → 2.02 s, under 2 %, because
`loopDelayCorrection` is already enabled and already folds the allpass group
delay into the per-line gain.

### The disqualifying coupling

| configuration | periodicity late |
|---|---|
| Mod Depth 0 %, 1 section | 16.72 |
| Mod Depth 0 %, **2 sections** | 16.72 |

Identical. The `AllpassPhaseBank` **fully disengages at Mod Depth 0** — that
is Phase 5B's true-bypass property and it is deliberate and correct for
modulation. But it means a Late Diffusion control built on that bank would do
**nothing at all** when Mod Depth is 0.

> Two user controls where one silently disables the other is not a parameter
> set. This must be resolved in Stage 2 before Late Diffusion can be
> authorised.

### N — the wet envelope, and why the metric was not enough

| configuration | −20 dB | −10 dB | −3 dB | landmark gap |
|---|---|---|---|---|
| baseline | 16.0 ms | 21.0 ms | 29.0 ms | 13.0 ms |
| Pre-Delay 50 ms | 66.0 ms | 71.0 ms | 79.0 ms | **13.0 ms** |
| N: envelope 50 ms | 18.0 ms | 25.0 ms | 38.0 ms | 20.0 ms |
| N: envelope 150 ms | 21.0 ms | 30.0 ms | 75.0 ms | 54.0 ms |

Pre-Delay shifts all three landmarks by exactly 50 ms and leaves the gap at
13.0 ms. The envelope stretches the gap. **§12 is satisfied: a 50 ms Attack is
measurably not +50 ms of Pre-Delay.**

But the landmark metric alone cannot tell N from J, because both stretch the
gap. So the density-versus-time curve was measured under N:

| | 10 ms | 20 ms | 40 ms | 80 ms | 160 ms |
|---|---|---|---|---|---|
| baseline | 0.451 | 0.709 | 0.843 | 0.939 | 0.970 |
| N: envelope 150 ms | 0.453 | 0.722 | 0.886 | 0.937 | 0.967 |

Worst difference **0.044 — below the 0.077 floor.**

> N stretches the landmark gap by 41 ms while leaving the density-versus-time
> curve statistically where it was. It is not building a late field slowly; it
> is turning down a late field that is already fully built. The landmark
> metric is necessary and not sufficient, and any Attack mechanism must be
> judged on both.

---

## 6. Rejected mechanisms, and why

| | why |
|---|---|
| A, post-tank allpass | no measurable density or periodicity change; mild stereo cost; ~100 ns/sample |
| N, as *the* Attack | passes the landmark test but leaves density-vs-time unchanged — a fader, not a formation control. Keep as the reference architecture only |
| L, Bloom via a diffusion ramp | moves periodicity, which is Late Diffusion's axis, not a buildup axis |
| Density as a user control | the quantity is saturated late and triple-owned early |
| Bloom as a user control | no second independent buildup landmark found |

Not rejected but not yet evaluated: D, E, K, M. D and E both amount to
changing the injection geometry or the effective loop lengths, which reopens
the Phase 4 vector search and the Phase 7 decay work respectively. M is the
only structurally plausible Bloom and is the most expensive option on the
list.

---

## 7. Recommended architecture

**Two controls, one of them conditional.**

```
Late Diffusion  ->  AllpassPhaseBank, in the line feedback path
                    coefficient strength and/or sections per line
                    CONDITIONAL on resolving the Mod Depth 0 coupling

Attack          ->  mechanism J (gradual network injection) measured against
                    mechanism N (wet envelope) as the control architecture,
                    judged on landmark gap AND density-versus-time
```

Nothing else. Density and Bloom are not recommended as controls.

The two parameters `density` and `attack` already exist in
`ParameterLayout.cpp` (70 % and 0 %) and are smoothed in the processor but
consumed by no DSP — exactly the state `preDelay`, `lowCut` and `highCut` were
in before Phase 8. `lateDiffusion` likewise exists at 70 %. There is no
`bloom` parameter. **So the recommendation to drop Density needs a decision
about an existing parameter ID, not merely about a knob**, and that decision
belongs to the product owner: an ID can stay reserved and unconsumed, or be
repurposed, but APVTS stores normalised values and removing it changes what
old sessions restore.

---

## 8. Independence matrix

`L` low · `M` medium · `H` high · `R` likely redundant. Measured
classifications are **bold**; the rest are structural.

| | EarlyD | LateD | Dens | Atk | Bloom | Decay | Size | PreDly | ModDpth |
|---|---|---|---|---|---|---|---|---|---|
| **Early Diffusion** | — | **L** | **R** | M | — | **L** | **M** | L | **M** |
| **Late Diffusion** | **L** | — | **L** | L | — | **H** | M | L | **H** |
| **Density** | **R** | **L** | — | M | — | L | **H** | L | **M** |
| **Attack** | M | L | M | — | **R** | **L** | M | **L** | L |
| **Bloom** | — | — | — | **R** | — | **L** | — | L | — |
| **Decay** | **L** | **H** | L | **L** | **L** | — | M | L | L |
| **Size** | **M** | M | **H** | M | — | M | — | L | M |
| **Pre-Delay** | L | L | L | **L** | L | L | L | — | L |
| **Mod Depth** | **M** | **H** | **M** | L | — | L | M | L | — |

The classifications that decide the parameter count:

- **Early Diffusion ↔ Density = LIKELY REDUNDANT.** Early Diffusion moves
  20 ms NED by 8.6× the floor. That is the dimension Density would claim.
- **Late Diffusion ↔ Mod Depth = HIGH.** The only working mechanism is
  disabled at Mod Depth 0. This is the blocking interaction.
- **Late Diffusion ↔ Decay = HIGH.** Decay 1 → 6 s moves late periodicity
  16.16 → 26.29, a span of 10.1, **3.3× the floor** — comparable to what the
  mechanism itself achieves.
- **Density ↔ Size = HIGH.** Size 20 → 60 % moves 20 ms NED by 3.2× the floor.
- **Attack ↔ Pre-Delay = LOW.** Proved: identical 13.0 ms gap, landmarks
  shifted by exactly 50 ms.
- **Attack ↔ Bloom = LIKELY REDUNDANT.** Both read the same landmark gap.
- **Bloom ↔ Decay = LOW**, and this is the one encouraging Bloom result:
  Decay 1 → 6 s moves the landmark gap only 12 → 14 ms, while the envelope
  moves it 13 → 54 ms. The buildup shape is **not** Decay's. Bloom fails on
  independence from *Attack*, not from Decay.
- **Late Diffusion ↔ High Damp.** Not in the brief's list but measured and
  worth recording: High Damp 45 → 100 % moves late periodicity 23.70 → 12.57,
  a span of 11.1 — **as large as the Late Diffusion mechanism's own effect.**
  A Late Diffusion control will be partly indistinguishable from High Damp on
  this measure, which is a warning about the measure as much as about the
  control.

---

## 9. Metrics for the retained controls

| control | metric | tool | floor |
|---|---|---|---|
| Late Diffusion | envelope periodicity, 300–900 ms, lags 1–150 ms | `maxEnvelopePeriodicity` — **pass the window raw** | 3.03 |
| Attack | landmark gap (−20 dB to −3 dB of short-window peak) **and** density-versus-time | `envelopeDb` + `normalisedEchoDensity` on `flattenDecay`'d windows | 1 ms step; 0.077 |
| (guard) | late NED must not fall | `normalisedEchoDensity` | 0.077 |
| (guard) | L/R correlation and mono downmix | `correlation` | — |
| (guard) | RT60 and RT60(f) | the Phase 7 certified ensemble analyser | Phase 7's own gates |

Two preparation rules, both learned the hard way in this stage:

1. **Echo density needs `flattenDecay`; periodicity does not.**
   `maxEnvelopePeriodicity` detrends its own envelope internally with the same
   0.002 coefficient, so pre-flattening detrends twice and the numbers
   describe the double-detrending.
2. **A statistic computed on near-silence must be refused, not printed.** At
   Early Diffusion 0 the input chain becomes four cascaded pure delays, so the
   5 ms window held only pre-onset silence; flattening amplified the floor
   into structure and the density came out 1.1227 — higher than noise, from a
   window with no signal in it.

---

## 10. Expected interaction with the established work

**Decay.** HIGH for Late Diffusion on the periodicity measure (span 10.1 vs a
3.03 floor). The in-loop mechanism changes effective loop length, which is
exactly what Phase 7's `loopDelayCorrection` was built to compensate — and it
measures as working: 2 sections moves the RT60 proxy 2.06 → 2.02 s, under 2 %.
**Flagged as required: any in-loop Late Diffusion must be re-measured against
the Phase 7 certified analyser, not against this stage's proxy.**

**Size.** HIGH for Density (3.2× floor at 20 ms) — one of the reasons Density
is rejected. MEDIUM for Late Diffusion: Size changes modal spacing, so a fixed
in-loop section length is a different fraction of the loop at each Size.

**Damping.** High Damp 45 → 100 % moves late periodicity by 11.1, as much as
the mechanism itself. Phase 7 damping must not be retuned for Phase 9, so this
is a constraint on how Late Diffusion is *measured and voiced*, not licence to
touch damping.

**Modulation.** HIGH and blocking, as above. Also: Mod Depth 0 → 30 % moves
20 ms density by 2.7× the floor, so modulation is already a density
contributor.

**Pre-Delay.** LOW throughout, and proved so for Attack.

---

## 11. CPU and complexity

Anchored on this project's own benchmarks, not estimated from nothing.

| mechanism | measured anchor | class |
|---|---|---|
| extra in-loop section (1 → 2 per line) | 507 → 655 ns/sample at 48 kHz, **+29 %** | **MEDIUM** |
| post-tank 8-stage stereo allpass | +~100 ns/sample on a ~150–200 ns tank | **HIGH** |
| in-loop coefficient change only (B) | no new state, no new delay | **LOW** |
| dynamic stage participation | `DiffusionStage` keeps running while disengaged, so cost is constant by design | **LOW** |
| wet amplitude envelope (N) | one multiply per sample | **LOW** |
| multiple internal late paths (M) | a second tank or a second allpass bank | **HIGH** |
| time-varying diffusion | already paid for — the bank is already time-varying | **LOW** |

The cheapest mechanism that measurably works is **B — a coefficient setter on
the existing in-loop bank** — and it is LOW. That is the strongest argument
for the recommended architecture.

---

## 12. Neutral / default compatibility

**Late Diffusion** can reproduce Phase 8 exactly. At 1 section per line and
coefficient 0.45 the bank is bit-identically what Phase 8 ships, and both are
already the defaults. A neutral position therefore exists without any new
bypass machinery. The existing parameter default is 70 %, which would have to
be re-pointed at whatever setting equals today's tank, or the default changed
— and changing a default changes what old sessions restore.

**Attack** can reproduce Phase 8 exactly at its existing default of 0 %, for
either mechanism, provided 0 % is an explicit structural bypass on the Phase 8
flag pattern rather than a nominally-neutral envelope left running.

**Density** — no compatibility question, since it is not recommended.

> Architectural cost: **none for the recommended set.** Both retained controls
> have a reachable neutral that is the current tank.

---

## 13. Major technical risks

1. **The Mod Depth coupling is blocking.** Late Diffusion's only working
   mechanism is switched off by another user control. Either the bank gains an
   engagement path independent of modulation depth — which means revisiting
   Phase 5B's true-bypass — or Late Diffusion needs a different home.
2. **Periodicity is not a clean axis.** Four existing controls move it by more
   than the floor, two of them (Decay, High Damp) by as much as the mechanism
   does. A user may not be able to tell Late Diffusion from Decay on this
   measure, and the measure is the best one available.
3. **The Attack mechanism is undecided.** The metric pair can now tell a fader
   from a formation control, but mechanism J has not been built or measured.
4. **In-loop diffusion touches the recurrence.** `loopDelayCorrection` appears
   to hold it (under 2 % on a proxy), but that must be confirmed against the
   Phase 7 certified analyser before any authorisation. A 2 % proxy is not a
   Phase 7 gate.
5. **`density` and `attack` are live parameter IDs.** Recommending against a
   Density control is a decision about an existing serialised ID.
6. **The late field may simply have no headroom.** The strongest single finding
   of this stage is that late density is saturated at 0.93–0.99 everywhere. If
   late *diffusion* turns out to be similarly saturated once the Mod Depth
   coupling is removed, Phase 9 reduces to one control.

---

## 14. Proposed Stage-2 research experiments

1. **Resolve the Mod Depth coupling.** Add a research-only engagement path for
   the in-loop bank that is independent of modulation depth, and re-measure
   periodicity at Mod Depth 0. This is the gate on everything else.
2. **Add a coefficient setter to `AllpassPhaseBank`** (research-only) and sweep
   0.2 … 0.7 at 1 and 2 sections. Mechanism B is the cheapest candidate and
   has never been measured because the setter does not exist.
3. **Confirm the decay law against the Phase 7 certified analyser**, not a
   proxy, for every in-loop configuration that reaches a shortlist.
4. **Build mechanism J** — gradual network injection — and measure it against
   N on both Attack metrics. Prediction to test: J moves the density-versus-time
   curve, N does not.
5. **Hunt for a second buildup degree of freedom.** If J moves density-vs-time
   while an envelope moves only the landmark gap, then *those two together*
   might be the Attack/Bloom pair the brief hoped for. That is the only route
   to Bloom this stage can see, and it is worth one experiment before Bloom is
   closed.
6. **Listening set, short transients first.** Snare, clap, pluck, sparse
   percussion, impulse, short vocal — density and diffusion differences are
   easiest to hear on transients. Research variants only, not wired to
   parameters.
7. **Stereo and mono guard** on every shortlisted configuration: `correlation`
   and downmix energy, against the baseline's −0.027 and −3.13 dB.

---

## 15. Benchmarking

Premium algorithmic reverbs are used as functional and perceptual references
only: what range of control a professional expects, and what a dense late
field should sound like. No algorithm, topology, parameter name, UI or mode
design is copied. Nothing in this stage was derived from another product.

---

## 16. Explicit recommendation

> ### REDUCE TO 2
>
> **Late Diffusion** and **Attack**.
>
> **Drop Density.** Late echo density is saturated — 0.93 to 0.99 across all
> sixteen configurations measured, a range equal to the measurement floor.
> Early density is real but already owned by Early Diffusion (8.6× floor),
> Size (3.2×) and Mod Depth (2.7×). A Density knob would move a number that
> is either at its ceiling or already under three other hands.
>
> **Drop Bloom.** It fails independence against Attack, not against Decay.
> Every mechanism tried moves the same landmark gap Attack moves, and no
> second buildup landmark was found that moves on its own. Decay, for its
> part, leaves the buildup alone — 12 to 14 ms across a 6× Decay change — so
> the dimension Bloom wants is genuinely vacant; there is simply no mechanism
> for it yet. Experiment 5 is the one chance to revisit that.
>
> **Late Diffusion is conditional.** It is the only one of the four with a
> mechanism that measurably works — in-loop sections move periodicity by 3.7×
> the floor while leaving density alone, which is exactly the Density/Late
> Diffusion separation §14 asked for. But that mechanism is switched off
> entirely at Mod Depth 0. **If Stage 2 cannot decouple it, Phase 9 reduces to
> one control: Attack.**
>
> Four controls were in the roadmap. The measurements support two, and one of
> those two is conditional. The roadmap is not evidence.

---

## STOP

Stage 1 ends here. No production DSP was changed, no parameter added, no
existing contract touched, no GUI work started, and Phase 8 was not reopened.

Research suite: `Tests/Phase9Research.cpp`, `runPhase9ResearchTests`, 7 / 7.
