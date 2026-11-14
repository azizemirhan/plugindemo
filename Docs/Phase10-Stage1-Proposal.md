# Phase 10 — Stage 1 architecture report

Early Reflections and the Early/Late architecture. Research and measurement
only: no production DSP, no parameter added, no existing contract touched, no
GUI.

Every number comes from `Tests/Phase10Research.cpp` against the Phase-9 tank
at its shipping defaults.

> ## 25. Recommendation: **RETAIN EXPLICIT EARLY REFLECTIONS**
>
> The deciding evidence is a single measurement, taken before any candidate
> was designed: **the Phase-9 tank produces zero discrete arrivals in its
> first 80 ms.** At the shipping Early Diffusion the onset is a diffuse wash
> with no peak standing 6 dB clear of its neighbours. An ER layer therefore
> adds a dimension the current architecture does not produce at any setting of
> any existing control — which is exactly the test Density and Bloom failed in
> Phase 9, and this one passes it.
>
> Recommended architecture: **ER-B, decorrelated multitap, split before Attack
> and after the cuts and Pre-Delay.** But see §23 — it needs **two new public
> parameters**, which is a materially different compatibility situation from
> Phases 8 and 9.

---

## 1. The exact Phase-9 signal path

Verified against `PluginProcessor.cpp`, `StereoFdn.h` and `FdnCore.h`:

```
input
  -> capture to the double-precision wet buffer          stage 1
  -> Low Cut -> High Cut                                 stage 1b, flag-bypassed
  -> Pre-Delay                                           integer taps, dual read
  -> Attack                                              32-tap FIR, PHASE 9
  -> [ tank.processBlock ]                                stage 2
       -> StereoDiffusionNetwork, 4 allpass stages        <- Early Diffusion
            left  1.87 / 2.99 / 4.31 / 5.53 ms  = 14.70 ms
            right 1.87 / 2.53 / 4.87 / 5.19 ms  = 14.46 ms
       -> injection vectors b_L, b_R                      orthogonal sign vectors
       -> FdnCore<8>: delay -> AllpassPhaseBank -> damping
                      -> gain -> 8x8 matrix -> write
       -> readout vectors c_L, c_R
  -> Size compensation -> decay-aware wet trim
  -> Width (mid/side)
  -> Mix (linear dry/wet crossfade)
```

| | |
|---|---|
| Input diffusion occurs | **inside `StereoFdn::processSample`**, before the injection vectors |
| Attack sits | in the processor, **before** `tank.processBlock`, so before the diffuser |
| What reaches the tank | the cut, pre-delayed, Attack-spread wet signal |
| Stereo routing before the FDN | the diffuser's differing L/R delay patterns; cross-coupling default **0.0**, so the diffuser is diagonal at shipping settings |
| Line excitation | `injection[i] = b_L[i]·L + b_R[i]·R`, unit-norm orthogonal sign vectors searched in Phase 4 |
| Compensation / trim | per-bank Size compensation inside the morph, then `mapping::previewWetTrim` driven by RT60 and Size |
| Latent ER code | **none.** `DelayLine.h`, `Interpolation.h` and `ScratchArena.h` each mention early reflections in their rationale as an intended future use. No ER class, no ER tap table, no ER parameter |

---

## 2. Early Reflections, operationally

> A finite, feedforward, temporally structured set of spatial reflection
> events occurring before the established late reverberant field.

A valid ER system must produce a controllable impression of initial room size
and distance, width, surface interaction and spatial arrival pattern, **without
becoming the late decay mechanism.**

Measurably, that means: **countable discrete arrivals**, each with its own
time and inter-channel distribution, in a finite window, with no recurrence.

It is **not** another diffuser, not a wet gain envelope, not another
Pre-Delay, and not the first 100 ms of the FDN output relabelled. The next
section is the test that separates those.

---

## 3. Does the tank already do this? No — and that is the phase's licence

The question asked first, before any candidate existed.

| Early Diffusion | first arrival | **significant arrivals** | crest | NED 5 ms | NED 20 ms | NED 40 ms |
|---|---|---|---|---|---|---|
| 0 % | 37.3 ms | **1** | 13.65 | — | — | 0.085 |
| **70 % (shipping)** | 22.9 ms | **0** | 5.31 | — | 0.289 | 0.880 |
| 100 % | 22.9 ms | **0** | 8.61 | — | 0.289 | 0.808 |

*Significant arrival* = a local maximum of the 1 ms envelope standing ≥ 6 dB
above its ±3 ms shoulders and ≥ 3 ms clear of the previous one. 3 ms is
roughly where two clicks stop fusing; 6 dB is enough prominence to be a
distinct event rather than a ripple on a wash.

Density windows here are **10 ms**, chosen short to resolve the early region,
so they are **not comparable** to Phase 9 Stage 1's 30 ms figures where 20 ms
read 0.709. The statistic depends on how much signal it was given.

> At the shipping setting the tank's first 80 ms contains **no countable
> arrivals at all**. At Early Diffusion 0 it contains one — because the input
> chain degenerates to four cascaded pure delays, i.e. a single 14.7 ms
> delayed copy, which is not an ER structure either.
>
> **An explicit ER layer adds something no existing control produces.**

---

## 4. Early Diffusion's mechanism and role

It sets the **allpass coefficient** of the four input stages via
`diffusion::coefficientForDiffusion`, a linear map onto 0 … 0.75. Nothing
else: an allpass has unit magnitude at every coefficient, so the control
changes phase and not spectrum. It is not a dry/wet blend, deliberately.

Phase 9 Stage 1 established its reach: it moves 20 ms normalised echo density
from 0.044 to 0.709 — **8.5× the 0.077 measurement floor** — and does not
measurably reach the established late field (0.055 spread, below the floor).

**Early Diffusion owns early density. It does not own early structure.** That
is the semantic room an ER layer occupies.

---

## 5. Parameter contract audit — the binding constraint

The full list is 19 parameters, `versionHint = 1`, with the documented rule
"never renamed or reordered — only appended to, with a bumped version hint".

| ID | name | range | default | mapping | serialised | consumed by DSP |
|---|---|---|---|---|---|---|
| `mode` | Mode | 13 choices | Large Hall | choice | yes | **no** |
| `color` | Color | 3 choices | Classic | choice | yes | **no** |
| `preDelay` | Pre-Delay | 0–500 ms | 0 ms | skew, centre 60 | yes | yes |
| `decay` | Decay | 0.1–60 s | 2.5 s | skew, centre 2.5 | yes | yes |
| `size` | Size | 0–100 % | 60 % | linear | yes | yes |
| `earlyDiffusion` | Early Diffusion | 0–100 % | 70 % | linear | yes | yes |
| `lateDiffusion` | Late Diffusion | 0–100 % | 70 % | linear | yes | **no — dormant** |
| `attack` | Attack | 0–100 % | 0 % | linear | yes | yes (Phase 9) |
| `modRate` | Mod Rate | 0.01–10 Hz | 0.5 Hz | skew, centre 0.5 | yes | yes |
| `modDepth` | Mod Depth | 0–100 % | 30 % | linear | yes | yes |
| `density` | Density | 0–100 % | 70 % | linear | yes | **no — dormant** |
| `lowDamp` | Low Damp | 0–100 % | 20 % | linear | yes | yes |
| `highDamp` | High Damp | 0–100 % | 45 % | linear | yes | yes |
| `lowDampFreq` | Low Damp Freq | 20–1000 Hz | 200 Hz | skew, centre 200 | yes | yes |
| `highDampFreq` | High Damp Freq | 500–20000 Hz | 10 kHz | skew, centre 4000 | yes | yes |
| `width` | Width | 0–200 % | 100 % | linear | yes | yes |
| `lowCut` | Low Cut | 20–1000 Hz | 20 Hz | skew, centre 200 | yes | yes (Phase 8) |
| `highCut` | High Cut | 500–20000 Hz | 20 kHz | skew, centre 4000 | yes | yes (Phase 8) |
| `mix` | Mix | 0–100 % | 30 % | linear | yes | yes |

> ### There is no reserved ER parameter. None.
>
> No `erLevel`, no `earlyLate`, no `erSize`, no `reflections`. Phases 8 and 9
> both consumed IDs that already existed — `preDelay`, `lowCut`, `highCut`,
> `attack` — so their compatibility story was "the default was already the
> neutral". **Phase 10 cannot borrow that.** It must append, bump
> `versionHint`, and rely on JUCE's behaviour for a parameter absent from an
> older saved state: the parameter keeps its default.
>
> That makes the **default value the entire compatibility mechanism**, and it
> must be the neutral one — see §21.

`mode` and `color` are also serialised and unconsumed, which matters for §22:
the ER layer's tap pattern is the most obviously mode-dependent thing in the
design.

---

## 6. Recommended split location

Of the four candidates:

```
A)  cuts -> Pre-Delay -> split { ER | Attack -> Early Diffusion -> FDN }
B)  cuts -> split { ER | Pre-Delay -> Attack -> Early Diffusion -> FDN }
C)  cuts -> Pre-Delay -> ER -> late injection derived from ER output
D)  hybrid: ER and Late with separate onset offsets
```

### Recommended: **A**

```
input -> Low Cut -> High Cut -> Pre-Delay
      -> split
           |-> ER taps ------------------------------>|
           |-> Attack -> Early Diffusion -> FDN ----->| -> Early/Late -> trim
                                                                   -> Width -> Mix
```

Why A, in order of weight:

1. **It makes Attack structurally unable to reach ER.** Measured: rendering ER
   at Attack 0 / 50 / 100 % gives **bit-identical** output while the late
   centroid moves 144.1 → 168.9 → 231.5 ms. That is the structural separation
   §13 demands, not an approximation with a tolerance.
2. **Shared filter conditioning before the split**, which §14 prefers, with no
   duplicated filter controls.
3. **One Pre-Delay meaning**, which §5 resolves below.
4. C is rejected because deriving the late injection from the ER output
   couples them permanently: the Early/Late balance would then change the late
   field's excitation, not just its level, and ER tuning would move the late
   field. That is the opposite of separable.
5. D is rejected for Stage 1 because separate onsets are a *feature* that
   needs a reason, and the measurement in §10 shows the two layers already
   overlap without one.

---

## 7. Recommended Pre-Delay semantics: **Model 1 — Pre-Delay moves both**

| | |
|---|---|
| **Model 1** | Pre-Delay moves ER *and* Late — **recommended** |
| Model 2 | ER stays near the source, Pre-Delay moves only Late |
| Model 3 | ER gets a derived smaller delay, Late gets the full amount |

Model 1 is recommended because of the brief's own stated preference — avoid
hidden derived behaviour — and because it is the only model where the control
means one thing. A user who sets Pre-Delay 80 ms and hears the ER layer
arrive at 0 ms has a control that does something it does not say.

Model 3 is the one to argue for, and it is the one to reject hardest: a
derived ER delay is a second, invisible parameter whose ratio nobody chose,
which has to be documented, which will be wrong for some material, and which
cannot be automated. Phase 8 and 9 both took the opposite choice deliberately
— explicit over derived — and this should match.

Model 2's attraction is physical: in a real room the first reflections arrive
close behind the direct sound regardless of how far away the listener is. But
AuroraVerb is not simulating a measured room (§9), and Pre-Delay's established
meaning in this product is already "move the whole wet event later". Changing
that meaning for one layer is a bigger semantic break than keeping it.

**Backward compatibility is unaffected either way** — at the neutral ER level
there is no ER layer to delay.

---

## 8. Recommended Early/Late balance semantics

The four candidates, measured against the Phase-9 wet energy (which balance =
late-only must reproduce):

| balance | A equal-gain | B equal-power | C normalised | D ratio |
|---|---|---|---|---|
| 0.00 (ER only) | −5.11 dB | −5.11 dB | 0.00 dB | −5.11 dB |
| 0.25 | −6.27 dB | −3.18 dB | 0.00 dB | −3.18 dB |
| 0.50 | −4.85 dB | −1.84 dB | 0.00 dB | −1.84 dB |
| 0.75 | −2.35 dB | −0.82 dB | 0.00 dB | −0.82 dB |
| 1.00 (late only) | 0.00 dB | 0.00 dB | 0.00 dB | 0.00 dB |
| **worst deviation** | **6.27 dB** | **5.11 dB** | **0.00 dB** | **5.11 dB** |

Two honest caveats about that table:

- **C reads 0.00 dB by construction, not by measurement.** The policy is
  defined as forcing total energy to the reference. What the column hides is
  the cost: under C the balance is no longer two independent gains, because
  the normalisation couples them — a user asking for "more ER" silently gets
  less late.
- **D coincides with B exactly here**, because the global gain was set so that
  balance = 1 reproduces Phase 9, which is the same normalisation B already
  satisfies at that end. They would separate only if the global gain came from
  something other than the late path.

The driver of all of it is one number: at unit gain the ER layer carries
**0.358** of energy against the late path's **1.163** — a **−5.11 dB**
mismatch. The two layers do not arrive comparably loud, so any policy that
treats the balance as a plain crossfade is also a level change.

### Recommended: **D — a ratio around a fixed global wet gain**, with the
### global gain calibrated so that balance = fully-late reproduces Phase 9

It keeps `Mix` as the only dry-versus-wet control and makes Early/Late purely
compositional, which is the distinction §6 of the brief insists on. Its 5.11 dB
excursion is real and is confined to the ER-heavy end of the travel, where a
level change is expected and arguably wanted — a room heard mostly through its
early reflections *is* quieter than one heard mostly through its tail.

C is rejected as the default despite its perfect number, because perfect
energy constancy is bought by making the two gains secretly dependent, and the
brief warns specifically against over-normalising away meaningful spatial
differences.

---

## 9. Energy accounting strategy

1. Calibrate the ER layer's unit-gain energy against the late path's **once,
   at prepare**, from the tap weights — the −5.11 dB above is a property of
   the tap set, not of the signal.
2. Apply policy D: `wet = g · ((1−b)·er + b·late)` with `g` set so that
   `b = 1` is bit-identically Phase 9.
3. Gate the result: integrated wet energy across the balance sweep must stay
   inside a stated budget, and the budget must be **argued, not assumed**.
   Phase 9's Attack held 0.096 dB; ER cannot, and should not have to.
4. Do **not** touch `Mix` or the decay-aware wet trim to compensate. Both are
   measured, gated and closed.

---

## 10. The early-to-late handoff

ER and late rendered separately on a **10 ms noise burst** — not an impulse,
where a sparse tap train leaves most analysis windows empty — then their
envelopes compared under an equal-power mixture.

| | |
|---|---|
| ER-to-late crossover | **35 ms** |
| Maxima in the smoothed sum, 10–120 ms | 4 |
| **Deepest notch with a recovery** | **1.14 dB at 25 ms** |

> **The handoff is clean.** No energy hole, no double peak.

The reason is visible in §11: the late field's own first arrival is around
**23 ms**, well inside the 80 ms ER window, so the two layers **overlap**
rather than meeting end to end.

That overlap is currently **an accident of the tank's delay set rather than a
designed property**, and that is worth stating plainly: it would stop being
true if the ER window were shortened, or if Size were raised enough to push
the late onset past the ER tail. Size 100 % already moves the late first
arrival to **52.5 ms**.

---

## 11. Size interaction

| Size | longest loop delay | late first arrival | late centroid | NED 20 ms |
|---|---|---|---|---|
| 0 % | 20.73 ms | **6.6 ms** | 90.2 ms | 0.939 |
| 20 % | 31.42 ms | 10.0 ms | 92.3 ms | 0.972 |
| **60 %** | 72.19 ms | **22.9 ms** | 101.4 ms | 0.289 |
| 100 % | 165.84 ms | **52.5 ms** | 120.0 ms | — |

The late field's first arrival moves **8× across the Size range**. An ER
pattern with fixed timing would describe a fixed-size space while the tank
describes a variable one, and at Size 100 % the ER layer would finish 28 ms
before the late field even started.

### Recommendation: **scale ER timing with Size, and add no ER Size parameter**

Reuse the existing meaning, as §10 of the brief prefers. The scaling should be
**weaker than the late path's**, because Size already moves the late onset by
8× and matching that would make the two layers move as one rigid object — the
"excessive correlated movement" the brief warns about. A square-root or
similar compression of the Size scalar is the obvious candidate and is a
Stage-2 measurement, not a Stage-1 assertion.

Existing Size semantics for the late path are **not changed**.

---

## 12. Early Diffusion interaction

| Early Diffusion | late NED 20 ms | ER-C arrivals | ER-C crest |
|---|---|---|---|
| 0 % | — | **12** | 69.71 |
| 35 % | 0.249 | 11 | 52.39 |
| 70 % | 0.289 | **0** | 19.19 |
| 100 % | 0.289 | **0** | 22.06 |

This is the measurement that decides the candidate, and it fires exactly where
it was aimed: feeding the ER taps through the input-diffuser pattern at the
shipping Early Diffusion **destroys the arrival structure completely** — 12
arrivals become 0.

### Recommendation: **A — Early Diffusion affects only the late path's input
### diffusion. It must not be shared with ER.**

Sharing one coefficient across both systems would mean that turning Early
Diffusion up silently dissolves the ER layer into a wash — one parameter
unpredictably controlling two unrelated systems, which §12 of the brief
forbids. The architectural separation is recommended **even though no new ER
diffusion parameter is proposed in Stage 1**: ER scattering, if any is wanted,
should be a fixed internal property of the chosen tap pattern until evidence
justifies exposing it.

---

## 13. Attack interaction

| Attack | ER output | late centroid | late first arrival |
|---|---|---|---|
| 0 % | **bit-identical** | 144.1 ms | 22.9 ms |
| 50 % | **bit-identical** | 168.9 ms | 22.9 ms |
| 100 % | **bit-identical** | 231.5 ms | 22.9 ms |

### Attack must remain a late-field control, and placement A makes that
### structural

Attack moves the late centroid by 87 ms and leaves ER **bit-identical** — not
approximately, exactly — because the split precedes it. No tolerance, no
gate threshold, nothing to drift.

---

## 14. Low Cut / High Cut routing

### Recommendation: **shared conditioning before the split**

Both layers get the same band limits, as §14 of the brief prefers: simple
semantics, no duplicated filter controls, and the cuts stay exactly where
Phase 8 measured, gated and closed them.

No reason was found for separate ER bandwidth. The argument for one would be
that real early reflections are brighter than a late tail, since they have hit
fewer surfaces — but that tonal difference belongs in the ER structure and in
the future Color/Mode layer (§15, §22), not in a second pair of filter
controls the user has to understand.

---

## 15. Damping relationship

`Low Damp` and `High Damp` belong to the **late feedback system** — they are
per-line shelves inside the recurrence, normalised by Phase 7's Option A
composite maximum. They must **not** be applied to explicit ER by default.

ER tonal response should come from the shared input filtering, from the ER
structure's own gain distribution, and from the future Color/Mode layer.

> Applying the damping controls to a feedforward ER layer would turn them into
> a global wet EQ, which is both a semantic break and a Phase-7 reopening.
> Neither is authorised.

---

## 16. ER measurement metrics

The smallest set that mapped to audible behaviour, with each one's limitation
stated:

| metric | what it answers | known limitation |
|---|---|---|
| first arrival time | where the layer starts | none |
| **significant arrivals** | is this ER or a wash | **fragile at 3–5 ms tap spacing** — see below |
| **separated arrivals** | discrete-echo risk | arithmetic on the schedule; no failure mode |
| reflection centroid | where the layer's weight sits | none |
| crest factor | sparseness | absolute value is an artifact of impulse input; ordering is valid |
| normalised echo density | how wash-like | window-length dependent; never compare across window lengths |
| L/R correlation | width | none |
| mono downmix energy | mono safety | none |
| third-octave ripple | coloration | **blind to combs finer than a third-octave band** |

Two of those limitations were found the hard way and both matter:

- **The envelope arrival counter is fragile where the decision is being
  made.** At 18 taps over 80 ms — a 4.4 ms mean gap — it reported 1 arrival
  against 12 at twelve taps and 9 at twenty-four, because its prominence test
  looks 3 ms either side and adjacent taps fall inside that window. The
  **separated** count was added because it is arithmetic on the tap schedule
  and has no such failure mode. The envelope counter is kept beside it only
  because it is the measure that correctly returns **zero for the tank**,
  which is the comparison the whole phase rests on.
- **Ripple is never the selection criterion for a sparse pattern.** Phase 9
  established it and this phase reconfirms it.

---

## 17. Early/Late transition metric

```
ER-dominated    : ER envelope above the late envelope
transition      : the crossover
late-dominated  : late envelope above
notch depth     : min(max before, max after) - envelope, within 10-120 ms
```

The notch definition matters and it took two attempts. Measuring "how far the
envelope falls below its running maximum" up to 250 ms reported **10.87 dB at
245 ms** — which is the reverb tail decaying, exactly as it should. **A decay
is not an energy hole.** A hole has a recovery on the far side, so the measure
has to require one. Corrected, the same configuration reads **1.14 dB**.

---

## 18. ER candidate architectures evaluated

Measured alone, not through the tank, on a 12-tap 80 ms window:

| | candidate | taps | first | arrivals | centroid | crest | ripple | L/R corr | mono |
|---|---|---|---|---|---|---|---|---|---|
| **ER-A** | multitap, identical L/R timing, gain pan | 12 | 4.1 ms | **12** | 10.6 ms | 69.71 | 9.78 | **0.949** | −0.12 dB |
| **ER-B** | decorrelated, different tap time per channel | 24 | 4.1 ms | **12** | 9.7 ms | 66.20 | 10.90 | **−0.001** | **−3.01 dB** |
| **ER-C** | ER-A through the 4-stage diffuser pattern | 12 | 4.1 ms | **0** | 25.3 ms | 19.19 | 9.78 | 0.023 | −2.91 dB |
| **ER-D** | three clusters instead of a uniform spread | 12 | 8.3 ms | **4** | 17.2 ms | 56.57 | 7.66 | 0.730 | −0.68 dB |

### Tap count against discrete-echo risk

| taps | mean gap | separated | env count | crest | ripple |
|---|---|---|---|---|---|
| 6 | 13.33 ms | **3** | 6 | 76.65 | 4.44 |
| 8 | 10.00 ms | **4** | 8 | 73.84 | 6.14 |
| **12** | 6.67 ms | **5** | 12 | 69.71 | 9.78 |
| 18 | 4.44 ms | **1** | 1 | 65.87 | 11.39 |
| 24 | 3.33 ms | **1** | 9 | 63.29 | 10.13 |

An 80 ms window is short, so the risk runs the opposite way from Phase 9's
Attack: there the 250 ms spread made *sparse* counts dangerous, here low
counts give a slap pattern and high counts dissolve the layer into a wash.
12 taps giving five arrivals more than 10 ms clear of their predecessor is the
shape of a room with a few distinguishable surfaces.

---

## 19. Rejected candidates

| | why |
|---|---|
| **ER-C** — taps into short diffusers | **Rejected on the measurement it was included to provoke.** At the shipping Early Diffusion the arrival count goes 12 → **0**. ER-C *is* Early Diffusion: it collapses the structure that defines an ER layer. It was worth building precisely to establish that this intuitively appealing design is the one failure mode the phase most needed to rule out |
| **ER-A** — identical timing, gain panning | Not rejected outright, but **it is nearly mono**: L/R correlation **0.949**, downmix −0.12 dB. It is maximally mono-safe and spatially narrow, which is the wrong trade for a layer whose job is spatial impression. Retained as the conservative fallback |
| **ER-D** — clustered groups | Not rejected; **not chosen in Stage 1.** Four arrivals and a 0.730 correlation make it the most "three surfaces" of the set and the most obviously mode-dependent. Its later first arrival (8.3 ms against 4.1) is a real character difference. It is the strongest Stage-2 alternative and is the natural shape for a future Room mode |
| **ER-E** — per-FDN-line early injection | **Rejected structurally, without measurement.** It would make ER and Late share an excitation path, so the Early/Late balance would change the late field's excitation rather than its level, and the Phase-4 injection vector search would be reopened. The brief names the risk itself; the measurement that would settle it is not worth taking before the simpler candidates have been listened to |

---

## 20. Recommended ER architecture

### ER-B — decorrelated deterministic multitap

| | |
|---|---|
| Taps | **12 per channel**, with independent timing |
| Window | ~80 ms at Size 60 %, scaled by a compressed Size law (§11) |
| Placement | golden-ratio position inside equal cells — irregular, no repeating interval, the same principle as Phase 9's Attack |
| Gains | `exp(-t/0.6W)/sqrt(t)`, monotone and parameter-free |
| Stereo | a different tap **time** per channel, from a different phase of the same sequence |
| Determinism | fully deterministic; same build, same rate, same taps; nothing seeded from the clock |

### Why ER-B over ER-A

| | ER-A | ER-B |
|---|---|---|
| L/R correlation | 0.949 — nearly mono | **−0.001** |
| Mono downmix | −0.12 dB | **−3.01 dB** |
| Arrivals | 12 | 12 |

ER-B is **both** maximally decorrelated and exactly mono-neutral: −3.01 dB is
the textbook figure for summing two uncorrelated signals, meaning the downmix
loses only the energy it must and suffers **no cancellation**. The usual worry
about decorrelated stereo — that it collapses in mono — **does not appear
here**, and the first-arrival time is identical to ER-A's.

### Stereo strategy

Different deterministic L/R tap times, with the **first arrival held near
centre** so the image has an anchor and transients do not wander. No
mid/side processing, no ping-pong, no polarity tricks: the width comes from
arrival-time differences, which is what a pair of ears in a room actually
receives.

---

## 21. Neutral and Phase-9 compatibility

**This is the hard requirement and the one place Phase 10 is weaker than its
predecessors.**

Phases 8 and 9 consumed reserved IDs whose defaults were already neutral.
Phase 10 has no reserved ID (§5), so it must append — and the **default value
becomes the entire compatibility mechanism**, because JUCE gives an absent
parameter its default when restoring an older state.

Required:

1. The ER level's default must be the value at which the layer is **absent**,
   and that must be an **explicit bypass branch** reproducing Phase 9 **bit
   for bit** — not an ER gain of zero summed in, which is a different
   computation and a different arithmetic path. Phase 8's flag bypass and
   Phase 9's explicit Attack-0 branch are the pattern; both were measured at
   0 of 48000 samples.
2. The Early/Late balance's default must sit at **fully late**, where policy D
   is calibrated to be bit-identically Phase 9.
3. `versionHint` is bumped. IDs are appended, never inserted.
4. `lateDiffusion` and `density` stay dormant and untouched.

> A default that adds ER to existing sessions would change every project
> anyone has saved. The gate is bit-identity, and it is testable before any
> listening.

---

## 22. Future Mode and Color compatibility

`mode` (13 families) and `color` (3) are already serialised and consumed by
nothing. The ER layer is the most obviously mode-dependent thing in the
design, so structure and data should be separated now even though no mode is
implemented:

| should become mode-dependent | should stay structural |
|---|---|
| tap timing pattern (table) | the tap engine itself |
| tap gain pattern (table) | the gain envelope's form |
| window length / Size law | the Size scaling mechanism |
| stereo distribution (table) | decorrelated-timing strategy |
| ER/Late ratio default | the balance law |
| ER internal scattering amount | whether scattering exists at all |

That is the same split `DiffusionTuning.h` and `FdnTuning.h` already use:
tables in a tuning header, mechanism in the DSP class. A Room mode wanting
ER-D's clusters and a Hall wanting ER-B's spread should be two tables, not two
code paths.

---

## 23. Major technical risks

1. **Two new public parameters, with no reserved IDs.** ER level and
   Early/Late balance. Appending is routine; getting the defaults wrong
   silently changes every existing session. This is the phase's biggest risk
   and it is a *contract* risk, not a DSP one.
2. **The clean handoff is currently an accident.** It works because the late
   field's first arrival (23 ms) falls inside the ER window (80 ms). At Size
   100 % the late onset moves to 52.5 ms, and a shorter ER window or a
   stronger Size law would break the overlap. It should be made a designed
   property with a gate.
3. **The Early/Late balance is a level change at the ER-heavy end** — 5.11 dB
   under the recommended policy. Defensible, but it must be stated in the
   manual rather than discovered by users.
4. **CPU accumulation.** Phase 9's Attack is already ~14.7 % active. ER at 12
   taps measures **7.08 %**; at 24 taps **11.41 %**. Both active together
   approach a quarter of the tank.
5. **The arrival counter is fragile in the 3–5 ms spacing region**, which is
   where tap-count decisions live. Mitigated by the schedule-based separated
   count, but any Stage-2 tap-pattern search must use both.
6. **ER-D is undertested.** It may well be the better sound and Stage 1 chose
   on measurable spatial properties, which is not the same thing.

---

## 24. CPU and memory estimate

| | ns/sample | share of tank |
|---|---|---|
| Phase 7–9 tank | 267.831 | — |
| ER 12 taps, stereo | **18.971** | **7.08 %** |
| ER 24 taps, stereo | **30.562** | **11.41 %** |

Measured with the accumulator escaping. The first version discarded the sum
with a `(void)` cast and reported 12 and 24 taps at the same 3.19 ns/sample —
which is what dead code costs.

Delay memory for an 80 ms window at 96 kHz is 7680 samples per channel,
rounded to 8192 for index masking: **128 KiB** for the pair, against Phase 9
Attack's 512 KiB for 400 ms. No allpasses in the recommended candidate.

**Classification: LOW to MEDIUM** at 12 taps, which is the brief's target.

---

## 25. Stage-2 research plan

1. **Settle the Size law.** Sweep the compression exponent and gate that the
   ER window still overlaps the late first arrival at every Size — turning
   §10's accident into a property.
2. **ER-B against ER-D on listening**, research-only, on snare, clap, a vocal
   phrase and an impulse. Stage 1 chose ER-B on spatial measurements; the
   cluster structure may simply sound more like a room.
3. **Tap count and window length search**, scored on the separated count, the
   mean gap, mono downmix and ripple together — never on one of them.
4. **Build the bit-exact bypass and gate it** across signal classes, before
   anything else is tuned. Phases 8 and 9 both did this first and both
   benefited.
5. **Calibrate and gate policy D's energy budget**, with the budget argued
   rather than inherited from Attack's 0.096 dB.
6. **Mono and width matrix** across ER level, Early/Late balance and Width,
   since Width is a mid/side stage *after* the sum and will interact.
7. **Prove the Attack and Early Diffusion separations again** on the
   production split, as structural bit-identity rather than as statistics.

---

## 26. Candidate decision matrix

Scored qualitatively, with measured dimensions in **bold**:

| | ER-A | **ER-B** | ER-C | ER-D | ER-E |
|---|---|---|---|---|---|
| Naturalness | medium | medium-high | low (a wash) | **high** | unknown |
| **Discrete-echo risk** | medium | medium | **none** | medium-high | unknown |
| **Coloration** | 9.78 dB | 10.90 dB | 9.78 dB | **7.66 dB** | unknown |
| **Stereo quality** | **poor, 0.949** | **excellent, −0.001** | good | medium, 0.730 | potentially high |
| **Mono safety** | **excellent** | **excellent, −3.01 dB** | good | good | risky |
| **Early Diffusion independence** | **full** | **full** | **none** | **full** | partial |
| **Attack independence** | **structural** | **structural** | structural | structural | **none** |
| Size interaction | simple | simple | simple | simple | entangled |
| **CPU** | **7.08 %** | **7.08 %** | +diffuser | **7.08 %** | low |
| Implementation complexity | low | low | low | medium | **high** |
| Neutral bypass | trivial | trivial | trivial | trivial | hard |
| Future Mode compatibility | good | good | poor | **excellent** | poor |

No single row decides it. ER-C loses on one row that is disqualifying;
ER-E loses on two that are structural; ER-A loses on the row the layer exists
to serve. ER-B wins on stereo and mono together while matching ER-A
everywhere else, and ER-D is the alternative most likely to win a listening
test.

---

## 27. Recommendation

> ### RETAIN EXPLICIT EARLY REFLECTIONS
>
> **Evidence.** The Phase-9 tank produces **zero countable arrivals** in its
> first 80 ms at the shipping setting, and one at Early Diffusion 0 — which is
> a single delayed copy, not a structure. No existing control produces early
> *structure*: Early Diffusion owns early *density* (8.5× the floor) and
> nothing owns arrivals. An ER layer therefore adds a genuinely new dimension,
> which is precisely the test Density and Bloom failed in Phase 9.
>
> It also survives every independence check a Phase-9-style rejection would
> have caught it on: **bit-identical** under Attack, structurally separate from
> Early Diffusion, a clean 1.14 dB handoff, **−3.01 dB** mono downmix with
> **−0.001** correlation, and **7.08 %** CPU at 12 taps.
>
> **Recommended architecture:** ER-B decorrelated multitap, split after the
> cuts and Pre-Delay and before Attack, Pre-Delay Model 1, Early/Late policy D,
> ER timing scaled by a compressed Size law, Early Diffusion **not** shared,
> damping **not** applied, cuts shared before the split.
>
> **The condition on all of it** is §21: two new public parameters with no
> reserved IDs, whose defaults are the only thing standing between this phase
> and every saved session changing. Build the bit-exact bypass first.

---

## STOP

Stage 1 ends here. No production DSP, no new parameter, no contract change, no
GUI, Phase 9 untouched, Phase 11 not started.

Research suite: `Tests/Phase10Research.cpp`, `runPhase10ResearchTests`, 6 / 6.
Phase 8 + 9 + 10 suites: 98 / 98.
