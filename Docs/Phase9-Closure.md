# Phase 9 — closure

Late Diffusion, Density, Attack, Bloom were the phase's four candidate
controls. **Phase 9 ships one: Attack.** The other three were rejected on
measurement, before any production code was written for them.

Phase 7 and Phase 8 remain closed and were not reopened. No Phase-7 damping
was retuned, no Phase-8 DSP was changed.

---

## 1. Closure commit

See the commit that carries this document. Stage commits:

| stage | commit |
|---|---|
| Phase 8 closure (baseline) | `cda97e1` |
| Stage 1 — architecture research | `80ae906` |
| Stage 2 — research prototypes | `6e1a0a1` |
| Stage 3 — production candidate | `546bfe0` |
| Stage 4 — listening set | `fc65492` |

---

## 2. Final Phase-9 architecture

```
input
  -> Low Cut                                   (PHASE 8)
  -> High Cut                                  (PHASE 8)
  -> Pre-Delay                                 (PHASE 8)
  -> ATTACK: staggered feedforward injection   (PHASE 9, new)
  -> input diffusion                           <- Early Diffusion
  -> FDN tank                                  (PHASE 4-7)
  -> wet trim
  -> Width
  -> Mix
```

**Attack remains outside the feedback recurrence.** It is a fixed FIR on the
wet signal before the tank, so it cannot see or be seen by the recurrence.

The dry path is untouched, as it has been since Phase 8.

### Where it sits, and why that question has an exact answer

Stage 2's report said the two candidate placements — before and after the
input diffuser — "are not the same sound". **That was wrong, and Stage 3
corrected it.** Attack is one scalar FIR applied identically to both channels,
so as an operator on the stereo pair it is `h(z)·I`, a scalar times the
identity, which commutes with any 2×2 linear operator including the stereo
diffuser. Measured across nine Early-Diffusion × Attack combinations: worst
relative difference **4.31e-16**.

The placement is therefore chosen on practical grounds. Before the diffuser
wins on two: the diffuser stays immediately before the injection vectors where
Phase 4 searched it, and Attack joins the other input-conditioning stages in
the processor rather than being threaded into `StereoFdn`.

---

## 3. Attack parameter contract — preserved unchanged

| | |
|---|---|
| ID | `"attack"` |
| Display name | `Attack` |
| Range | **0 – 100**, linear |
| Step | 0.1 |
| Default | **0.0** |
| Mapping | the existing public linear contract |
| Formatter | `percentToText`, label `%` |
| Type | `juce::AudioParameterFloat` |
| Serialization | unchanged |

**Attack = 0 is the legacy neutral state**, and it already was the default, so
existing sessions restore it and sound unchanged.

The parameter's *smoother* was removed — `Level attack`, a `LinearSmoother`
that was consumed by nothing. Removed for the same fundamental reason
`preDelayMs` went in Phase 8: a smoothed Attack read per sample means
continuously moving tap positions, and the derivative of a delay time is a
pitch shift applied to whatever is passing through it. `AttackInjection` takes
the raw target and owns its own transition.

### Dormant parameters

`lateDiffusion` (default 70 %) and `density` (default 70 %) **remain in the
state and public compatibility structure.** They are not deleted, not renamed,
not repurposed. They are documented as dormant in `PluginProcessor.h`, where
their smoothers still live.

> Their IDs existing is **not** a reason to expose the rejected Phase-9
> behaviour. They are serialised state-compatibility debt and nothing more.

---

## 4. Attack DSP topology

```
y[n] = sum_k  w_k * x[n - d_k],    d_0 = 0,    sum_k w_k^2 = 1
```

| | |
|---|---|
| Taps | **32** — 1 immediate, 31 delayed |
| Maximum internal spread | **250 ms** |
| Timing | deterministic and irregular: the spread is divided into 31 equal cells, one tap per cell |
| Placement within a cell | **golden-ratio** sequence — no common divisor with the cell grid, so no two tap spacings repeat |
| Signs | deterministic ±1 from a fixed-seed xorshift; the immediate tap is always positive |
| Weights | **L2-normalised**, squares sum to 1.0000000000 |
| Immediate component | **25 %** of the injection power |
| Mean tap spacing at full Attack | **8.1 ms** |
| Third-octave ripple | **0.746 dB** |

### Internal mapping

```
spread = 250 ms * u^2
```

where `u` is the normalised Attack control. This gives greater useful
resolution in the lower musical portion: **0 – 100 ms occupies the first
~63 %** of the control's travel, against 40 % for a linear map. Not changed
during closure.

### How the design was chosen — and a correction

The tap count was chosen on **temporal spacing and envelope crest behaviour,
not on magnitude ripple**. That distinction matters, because the first version
of the Stage-3 suite did choose on ripple and reached a wrong answer:

| taps | mean gap | envelope crest | third-octave ripple |
|---|---|---|---|
| 6 | 50.0 ms | 6.43 | **0.330 dB** |
| 8 | 35.7 ms | 5.57 | 0.535 dB |
| 16 | 16.7 ms | 3.94 | 3.491 dB |
| 24 | 10.9 ms | 3.21 | 1.651 dB |
| **32** | **8.1 ms** | **2.78** | **0.746 dB** |
| 48 | 5.3 ms | 2.27 | 1.903 dB |

> **Do not read the low-tap-count rows as better.** Six taps over 250 ms
> measure better on ripple than any dense configuration while being five
> audible echoes 50 ms apart. The measure is blind to them because a 50 ms
> spacing puts the comb's teeth 20 Hz apart, far finer than a third-octave
> band, so the smoothing averages the whole comb away. **Sparse low-tap-count
> designs produce perceptually dangerous discrete echo spacing.**

Ripple is kept as a coloration check. The ordering to trust is the mean gap —
arrivals separate perceptually somewhere above roughly 10 ms — and the
envelope crest factor, which falls monotonically with tap count and directly
measures whether individual taps stand out.

Against the regular-spaced all-positive alternative at the same tap count:
**11.247 dB** of ripple. The irregular placement and the sign pattern buy
10.5 dB.

### Automation

**No sample-by-sample delay-time smoothing.** Two fixed tap schedules read
from one delay line, crossfaded over **30 ms**, measured rather than inherited
from Phase 8's 20 ms pre-delay crossfade. A fade always runs to completion; a
request arriving mid-fade overwrites a single pending slot and is applied the
moment the fade ends — coalesced, not queued.

Validation: five explicit transitions (0 → low, low → high, high → 0, 0 → max,
max → 0) plus **450 random changes**.

| | |
|---|---|
| NaN / Inf | none |
| Abnormal spike | none |
| Worst transition ratio | **≤ 1.000** against the settled output |

The delay line is **written unconditionally including while Attack = 0**, so a
move off zero reads real recent signal rather than a buffer frozen from
whenever the control was last touched.

---

## 5. Neutral compatibility

### BIT-EXACT PHASE-8 COMPATIBLE

| signal | samples | differing |
|---|---|---|
| impulse | 48000 | **0** |
| white noise | 48000 | **0** |
| sine 440 | 48000 | **0** |
| transient train | 48000 | **0** |
| DC 0.25 | 48000 | **0** |
| silence | 48000 | **0** |

Attack 0 takes an explicit bypass branch, not a tap set that happens to sum to
unity — and the distinction is arithmetic, not pedantic: the 32 weights sum to
**−0.732320**, not 1.0, while their squares sum to 1.0000000000. Collapsing
the taps to offset zero would be both a gain change and a different
computation.

Existing sessions at the default Attack 0 therefore preserve the Phase-8
sound.

---

## 6. Attack against Pre-Delay

| | onset | buildup |
|---|---|---|
| Pre-Delay 50 ms | **+50 ms** | **+0 ms** |
| Attack, Stage-2 research at 150 ms | **+0 ms** | **+58 ms** |
| Attack, production at high settings | +0 ms | **up to +144 ms** |

**Pre-Delay controls wet onset timing. Attack controls late-field
establishment.** Pre-Delay moves the onset by exactly its own value at every
Attack setting and leaves the buildup untouched; Attack moves the buildup at
every Pre-Delay setting and leaves the onset untouched.

Listening validation confirmed the distinction is perceptible, **though subtle
at normal Mix settings** — see §13.

---

## 7. Attack against Early Diffusion

Buildup range measured across the Early Diffusion positions:

| Early Diffusion | Attack buildup span |
|---|---|
| 0 % | **143 ms** |
| 70 % (shipping) | **143 ms** |
| 100 % | **144 ms** |

**Attack's range is effectively independent of Early Diffusion** — within 1 ms
across the whole travel of the other control.

The two own different things. Early Diffusion primarily controls early
scattering and early density: its normalised-echo-density span at 20 ms is
0.055 → 0.709, **8.5× the 0.077 measurement floor**, while Attack leaves that
quantity inside the floor. Attack primarily controls temporal late-field
buildup.

---

## 8. Modulation interaction

Buildup range across Mod Depth:

| Mod Depth | Attack buildup span |
|---|---|
| 0 % | **142 ms** |
| 30 % (shipping) | **143 ms** |
| 100 % | **142 ms** |

The control direction and range survive modulation.

### The known diagnostic, recorded rather than hidden

At **Mod Depth 0 and high Attack**, late-field envelope periodicity rises:

```
16.7  ->  37.5          (measurement floor 3.03)
```

At the shipping Mod Depth 30 % the picture is smaller and non-monotone:
23.70 → 30.44 → 21.67. The mechanism is not mysterious — 32 irregular taps
imprint their own autocorrelation structure on the excitation, and at Mod
Depth 0 there is nothing to smear it.

Targeted blind listening (Stage 4, Case A) **did not demonstrate an
objectionable perceptual artifact**.

> ### KNOWN MEASUREMENT CHARACTERISTIC — NOT A SHIPPING BLOCKER

---

## 9. Energy

| | |
|---|---|
| Integrated wet energy variation across the Attack sweep | **≈ 0.096 dB** |
| Output normalization used | **none** |

The L2-normalised tap weights control the injected energy intrinsically.
Neither Mix nor the wet trim was touched to compensate. **Attack is not
functioning primarily as a wet-level control.**

The peak falls as Attack rises, which is correct rather than a defect: the
same energy spread over more time has a lower peak.

---

## 10. Stereo and mono

| | |
|---|---|
| Deviation from baseline, L/R correlation and mono downmix | **≤ ~0.1 dB** |

No meaningful stereo regression. The schedule is channel-identical, so it
cannot alter the stereo geometry — the image comes from the diffuser's
differing patterns and the injection and readout vectors, all downstream of
this stage.

A per-channel schedule was measured and the expected mono penalty **did not
appear** (−2.97 dB against −2.90 dB for channel-identical, correlation
marginally closer to zero). That argument for channel-identical is therefore
withdrawn; the choice rests on the commutation property and on simplicity.

---

## 11. Decay and damping

Attack is feedforward and outside the tank recurrence. **Structurally it does
not modify** Decay · the feedback gains · the loop lengths · the damping
filter coefficients · Size · Mod Depth · Mod Rate. None of those can see it.

The RT60 proxy moved 2.060 → 2.040 s across the whole Attack sweep, which is
under 1 % and within the proxy's own 20 ms quantisation.

> **No new Phase-7 RT60 claim is made from the known-limited Phase-8
> sensitivity harness.** That harness's limitation is recorded in
> `Docs/Phase8-Closure.md` and it is not used as a gate here.

---

## 12. CPU — incremental cost

| | relative to the current tank |
|---|---|
| Attack **neutral** | **≈ +0.70 %** |
| Attack **active** | **≈ +14.70 %** |

Absolute figures from the Stage-3 run: tank 241.319 ns/sample, Attack neutral
1.688, Attack active 35.462. Delay memory: two buffers of 32768 doubles =
512 KiB, sized from the 400 ms allocation limit and rounded to a power of two
for index masking.

The active cost is **materially larger than any Phase-8 control**, and it is
accepted for this production candidate because Attack is bypassed cheaply at
0, because the control establishes a genuinely independent temporal dimension,
and because listening validation passed.

No sonic-changing CPU optimisation was performed during closure. A 24-tap
variant would save about a quarter of the active cost at 1.651 dB ripple and a
10.9 ms mean gap; that trade is **recorded as possible future work, not
taken.**

---

## 13. Listening verdict

### LISTENING VERDICT: PASS

Stage-4 artifact:

```
C:\JuceBuilds\plugindemo\AuroraVerb_artefacts\Release\VST3
  \AuroraVerb.vst3\Contents\x86_64-win\AuroraVerb.vst3

SHA256  CF60813A9578498F81877D5AB1CA0962DF91B2DD5C2834B2C66B2A84B1C660D7
```

Render folder `renders\phase9-stage4` — 32 files, 48 kHz, 32-bit float.

### Case A — primary periodicity risk: PASS

High-Attack blind comparisons evaluated across snare, piano and hats.
**Selections did not systematically identify Mod Depth 0 as worse.**

The listener reported differences between versions, but **not** objectionable
periodic repetition, metallic ringing, flutter, comb-like hollowness, a
discrete echo sequence or an unnatural rhythmic pulse.

> The measured high-Attack / Mod-Depth-0 periodicity increase does not
> currently correspond to a demonstrated perceptual product defect.

### Case B — normal Attack range: PASS

At the shipping Mod Depth 30 %, Attack 0 / 25 / 50 / 75 / 100 was evaluated.
No new discrete multitap or rhythmic echo artifact was identified. High Attack
settings remained clean.

A transient "tak" sound was noticed, but it was **already present at Attack
0** and was therefore not attributed to the Attack mechanism.

### Case C — Attack against Pre-Delay: PASS

Initial listening found the difference subtle. On more careful listening the
distinction became perceptible:

- **Pre-Delay** moves the wet event later.
- **Attack** keeps the onset substantially present while changing and slowing
  how the reverberant field develops.

The listener also noticed the bell-like reverb component behind the snare
becoming slightly more subdued or muffled as Attack increased. This was **not**
reported as metallic, broken, resonant, echo-like or objectionable, and **it is
not classified as a defect.**

The additional 100 % wet Case-C diagnostic that had been proposed was **not
created**: the listener re-evaluated the existing set carefully and confirmed
the distinction is audible, so it is no longer necessary.

---

## 14. Rejected Phase-9 controls

These are **evidence-based product-scope reductions, not failed implementation
work.** No production code was written for any of them. Each was rejected
before integration, on measurement.

### Density — REJECTED

Established late-field density was already near saturation. Measured late
normalised echo density stayed within approximately **0.93 – 0.99** across
every tested configuration — a range equal to the 0.077 measurement floor, so
there was no late-density range to sell.

Meaningful early-density behaviour is already controlled by **Early Diffusion**
(8.5× the floor at 20 ms), **Size** (3.2×) and **Mod Depth** (2.7×). A
separate late Density control was not justified.

### Bloom — REJECTED

The tested Bloom mechanisms did not establish a measurable or perceptual
dimension sufficiently independent from Attack. Every mechanism tried moved
the same single buildup landmark Attack moves, and no second independent
landmark was found.

Decay *was* independently distinguishable — a 6× Decay change moved the
buildup landmark by only 12 → 14 ms — so the dimension Bloom wanted is
genuinely vacant. **Bloom failed the more important Attack-independence
requirement.**

### Late Diffusion — REJECTED

The research candidate showed useful periodicity changes at Mod Depth 30 % and
100 %, where it was monotone: 23.70 → 9.27 and 21.37 → 8.37.

It failed at **Mod Depth 0 %**, where low and static diffusion settings
*increased* periodicity rather than reducing it:

| setting | periodicity |
|---|---|
| neutral | **16.724** |
| coefficient 0.10 | **32.528** |
| coefficient 0.20 | **32.854** |
| coefficient 0.60 | 14.996 |

That is up to **+16.13 against a 3.03 floor**, with two direction reversals
across the travel, and the only sub-neutral point is below the floor. **This
was not estimator noise.**

The cause is structural: at the coefficients where the section disperses least
it behaves as a pure delay and adds a repeat to the loop, and **the mechanism
depended on modulation to blur its own recurrence structure.** A control whose
benefit is not separable from another control's being on is not a control.

> **Late Diffusion was rejected because of behaviour, not CPU.** Everything
> else about it passed: bit-exact neutral across six configurations, no stereo
> cost, 0.12 dB of coloration, +3.00 % CPU and no new delay memory.

---

## 15. Accepted limitations

1. **High Attack at Mod Depth 0 produces a measurable periodicity rise**
   (16.7 → 37.5, 6.9× the floor), but targeted blind listening found no
   objectionable defect. Recorded as a known measurement characteristic.
2. **Attack's audible distinction from Pre-Delay may be subtle at normal Mix
   settings**, though careful listening confirmed the difference.
3. **Active Attack CPU overhead is ≈ +14.7 %** relative to the current tank.
4. **`lateDiffusion` and `density` remain compatibility and state debt** as
   dormant serialised parameters.

None of these is a Phase-9 blocker.

Carried forward unchanged from earlier phases: the test-infrastructure debt in
`Docs/Phase7-TestInfrastructureDebt.md`, and the Phase-7 FL junction hazard in
`Docs/Phase8-Closure.md`.

---

## Automated regression

**1500 / 1502.** The two failures are the pre-existing Phase-7 diagnostics, at
values unchanged:

```
0.06022872179
19.34162509
```

| | |
|---|---|
| **New regressions** | **0** |
| Phase-9 Stage-3 suites | **20 / 20 PASS** |

All historical gates preserved. No expected value was silently updated.

---

## Research trail

Preserved, including every wrong turn. Nothing was rewritten to look cleaner.

### Measurement corrections, by stage

**Stage 1** — periodicity was being pre-flattened although
`maxEnvelopePeriodicity` detrends internally with the same coefficient, so the
numbers described the double-detrending; and a density window on pre-onset
silence returned 1.1227, higher than noise, from a window with no signal in
it. The resolution-floor suite was also measuring 300 ms windows and comparing
them against 600 ms ones.

**Stage 2** — per-event landmarks in a continuous render reported onset 0 ms
for events 2 and 3, because the window opens inside the previous tail;
replaced with a superposition test. That test was then run at the shipping Mod
Depth 30 % and the *baseline* failed at 9.95 %, because a modulated tank is
time-varying by design and the test was blaming the mechanism for Phase 5's
accepted character; moved to Mod Depth 0.

**Stage 3** — six corrections. Tap count chosen on third-octave ripple, which
is blind to sparse taps. An envelope crest measure that was structurally a
constant. **The buildup landmark took three failed attempts** — two threshold
definitions that moved with their own reference, and one that collapsed into
the envelope's fast initial jump — before being replaced with T50 minus onset,
an integral with no threshold to be fragile about. And both click references
were measured against the input, where a 32-tap FIR is intrinsically steeper
than its own input — the same mistake Phase 8 Stage 2.5 made.

**Also corrected:** Stage 2's claim that the two Attack placements "are not
the same sound", which was wrong — they commute at 4.31e-16.

### Scope, as it actually moved

Four controls were in the roadmap. Stage 1's measurements supported two, one
of them conditional. Stage 2 rejected the conditional one. Phase 9 ships one.
The roadmap was not evidence.

---

## PHASE 9 COMPLETE

Attack ships. The neutral path is bit-identical to Phase 8 across six signal
classes, every transient receives the same deterministic Attack with no onset
detector, the control is orthogonal to Pre-Delay and to Early Diffusion on the
metrics each of them owns, the automated regression carries no new failure,
and targeted blind listening found no audible defect.
