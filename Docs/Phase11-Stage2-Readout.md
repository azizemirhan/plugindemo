# PHASE 11 — Stage 2: readout-topology research

Research only. **No production DSP was changed.** No Phase 10 constant was
touched — not the ER pattern, occupancy, `envelopeDecay`, `sizeAlpha`, BAL-4,
the Early/Late gain law or its semantics. Phase 10 stays closed.

**Headline: the answer to the question this stage was convened to ask is NO.**
Readout-vector data alone does not fix the blocker, and neither does injection
data fix the pan-retention question. Both limits are structural and both are
now identified rather than suspected. Details in sections 17–20.

---

## 1. Current readout vectors

From `FdnTuning.h`:

```
readoutLeft    -1  -1  -1  +1  +1  +1  +1  -1
readoutRight   +1  -1  -1  +1  -1  -1  +1  +1
```

| | |
|---|---|
| full-vector correlation | **0.000000** — exactly orthogonal |
| sign agreements | 4 |
| sign disagreements | 4 |
| **lines where L + R = 0** | **4** |
| norms | 8.00 / 8.00 |

## 2. The subset-correlation weakness

Worst **mono retention** by active-subset size — the energy a downmix keeps if
only that many lines carry:

| active lines | retention | subset correlation |
|---|---|---|
| 1 | **−∞ (total cancellation)** | −1.0000 |
| 2 | **−∞** | −1.0000 |
| 3 | **−∞** | −1.0000 |
| 4 | **−∞** | −1.0000 |

**The mechanism in one number: four of the eight lines have `L + R` exactly
zero.** Energy on any of those reaches the two outputs in anti-phase and
contributes *nothing* to a mono sum.

Perfect orthogonality is no protection whatever, because orthogonality is a
statement about all eight lines at once and the failure happens when only a
few are carrying — which is what small Size and short Decay produce.

### And it is forced, not a bad choice

Orthogonality of two ±1 vectors of length 8 means the signs agree on exactly
four lines and disagree on exactly four. Every disagreeing line has `L + R = 0`.

Checked by exhaustive enumeration of all 65 536 ordered ±1 pairs:

| | |
|---|---|
| orthogonal pairs found | **1 120** |
| of those, with no cancelling line | **0** |
| best achievable 1-line retention | 0.00 dB (a *correlated* line, not a fix) |

**No orthogonal sign-vector pair avoids the problem.** That settles candidate
family B before it reaches audio — Hadamard rows are sign vectors and inherit
the same four cancelling lines.

## 3. Candidate families

| family | construction |
|---|---|
| **A** | production pair |
| **B** | rows of an 8×8 Hadamard matrix |
| **C** | the production pair **rotated by φ** within the plane it spans: `L' = L cos φ + R sin φ`, `R' = R cos φ − L sin φ`. A rotation preserves both norms and the inner product **exactly**, so orthogonality and energy normalisation hold by construction rather than by correction |
| **D** | quadrature: `L_i = cos θ_i`, `R_i = sin θ_i` with ± angle pairing for orthogonality |

The rotation family has an exact algebra: a line where the two signs agree
gets a mono sum of `2 cos φ`, a disagreeing line gets `−2 sin φ`. At φ = 0 the
disagreeing lines vanish — the blocker. At φ = 45° both are ±√2 and no line
cancels.

## 4. Worst-case subset metrics

| candidate | corr | zero-sum lines | 1 line | 2 lines | 3 lines | norms |
|---|---|---|---|---|---|---|
| A: production | 0.0000 | **4** | −∞ | −∞ | −∞ | 8.00 / 8.00 |
| B: Hadamard 1,2 | 0.0000 | **4** | −∞ | −∞ | −∞ | 8.00 / 8.00 |
| B: Hadamard 3,5 | 0.0000 | **4** | −∞ | −∞ | −∞ | 8.00 / 8.00 |
| C: rotated 15° | 0.0000 | 0 | −11.74 | −11.74 | −11.74 | 8.00 / 8.00 |
| C: rotated 22.5° | 0.0000 | 0 | −8.34 | −8.34 | −8.34 | 8.00 / 8.00 |
| C: rotated 30° | 0.0000 | 0 | −6.02 | −6.02 | −6.02 | 8.00 / 8.00 |
| **C: rotated 45°** | 0.0000 | 0 | **−3.01** | **−3.01** | **−3.01** | 8.00 / 8.00 |
| D: quadrature 15–75 | 0.0000 | 0 | −15.21 | −15.21 | −9.85 | 8.00 / 8.00 |
| D: quadrature 30–60 | 0.0000 | 0 | −21.19 | −21.19 | −15.62 | 8.00 / 8.00 |

φ = 45° gives exactly **−3.01 dB** — the cost of summing a hard-panned signal,
which is not cancellation at all.

**The quadrature family is worse than the production pair** on this metric
despite having no zero-sum lines. A family can be orthogonal,
energy-normalised, free of cancelling lines and still a poorer choice.

**The cost is in the same algebra.** At 45° each readout reads only four of the
eight lines — the others multiply by zero — so every mode is hard-panned. That
trades a mono problem for a placement problem.

## 5–7. Narrow-band stress results

140 cells per candidate: Size {0, 10, 15, 20, 30, 60, 100 %} × Decay {0.1, 0.3,
0.6, 2.5, 10 s} × {220, 440, 1000, 3000 Hz}, sustained tones, Early/Late 0,
Mod Depth 0. **Minima reported, not averages.**

"Normal" excludes the two extreme corners (Size < 10 %, Decay < 0.3 s). The
original failure — Size 15 %, Decay 0.6 s — *is* normal.

| candidate | all cells: min corr | worst fold | **normal cells: min corr** | **worst fold** | worst balance |
|---|---|---|---|---|---|
| A: production | −0.9747 | −8.75 dB | −0.9567 | **−5.92 dB** | 16.4 dB |
| B: Hadamard | −0.9872 | −10.92 dB | −0.9759 | −10.92 dB | 14.2 dB |
| C: rotated 22.5° | −0.9312 | −14.07 dB | −0.9312 | −9.13 dB | 21.0 dB |
| C: rotated 30° | −0.9477 | −11.75 dB | −0.9477 | −11.58 dB | 19.0 dB |
| C: rotated 45° | −0.9607 | −16.54 dB | −0.9586 | **−16.54 dB** | 20.2 dB |
| D: quadrature | −0.9680 | −14.23 dB | −0.9680 | −13.57 dB | 13.6 dB |

### Duration at the original failing point — Size 15 %, Decay 0.6 s, 220 Hz

| candidate | windows < −0.5 | worst window | worst fold |
|---|---|---|---|
| A: production | **9 of 9** | −0.9820 | **−19.43 dB** |
| B: Hadamard | 9 of 9 | −0.9891 | −7.29 dB |
| C: rotated 22.5° | 9 of 9 | −0.9708 | −9.23 dB |
| C: rotated 30° | 9 of 9 | −0.9503 | −6.86 dB |
| **C: rotated 45°** | **0 of 9** | **−0.4645** | **−3.81 dB** |
| D: quadrature | 9 of 9 | −0.8045 | −9.84 dB |

### What this means — and it is not what the subset table predicted

**At the identified failing point, the 45° rotation is a complete fix.** Nine
windows of nine become zero, and the fold-down goes from −19.43 dB to
−3.81 dB.

**Across normal cells it is not a fix at all — it is worse than production.**
Normal-cell worst fold: production −5.92 dB, rotated 45° −16.54 dB. The
rotation **relocates** the failure rather than removing it.

**Why the subset metric over-promised.** It assumed each line carries an
*independent* state. The 8×8 feedback matrix couples them, so two lines read
by opposite outputs can carry *correlated* states and cancel anyway. The
subset metric is **necessary but not sufficient**, and saying so is better
than quoting −3.01 dB as a guarantee.

**It also costs placement.** Worst per-frequency balance rises from 16.4 dB to
20.2 dB — the hard-panning the algebra predicted.

## 8–10. Broadband, transient and tonal regression

Six sources, default settings, Early/Late 0:

| candidate | worst broadband balance | worst fold deviation | side at default |
|---|---|---|---|
| A: production | 2.22 dB | **0.09 dB** | −6.0 dB |
| B: Hadamard | 2.15 dB | 0.48 dB | −5.9 dB |
| C: rotated 22.5° | 1.43 dB | 0.92 dB | −5.9 dB |
| C: rotated 30° | 0.94 dB | 1.12 dB | −5.9 dB |
| C: rotated 45° | **0.17 dB** | 1.25 dB | −5.9 dB |
| D: quadrature | 0.86 dB | 1.49 dB | −6.3 dB |

**No candidate is rejected on broadband grounds.** The rotations actually
*improve* broadband centring markedly (2.22 → 0.17 dB) and cost about 1 dB of
fold-down accuracy. Side energy is essentially unchanged, so none of them
narrows the field.

Tonal material is section 5's subject and is where the candidates separate.

## 11. L/R swap symmetry

Measured on asymmetric inputs only — Stage 1 established that a centred input
makes the sample-level test degenerate. The figure is the disagreement between
the two one-sided magnitudes, which is what a channel preference would look
like.

| candidate | left only | right only | mirror error |
|---|---|---|---|
| A: production | −1.06 dB | +0.42 dB | **0.64 dB** |
| B: Hadamard | −0.17 dB | +2.47 dB | **2.31 dB** |
| C: rotated 22.5° | −0.23 dB | −0.18 dB | **0.05 dB** |
| C: rotated 30° | +0.11 dB | −0.38 dB | 0.27 dB |
| C: rotated 45° | +0.74 dB | −0.68 dB | 0.06 dB |
| D: quadrature | −0.72 dB | −0.88 dB | 0.15 dB |

**The rotations markedly improve L/R symmetry** — 0.64 dB down to 0.05 dB. An
unexpected and genuine secondary benefit. The Hadamard pair is the only
candidate with a *systematic* channel preference (2.31 dB), which under the
brief's rule is a blocker on its own.

## 12. Source-side retention

| candidate | mean tail-side bias |
|---|---|
| A: production | 0.74 dB |
| B: Hadamard | 1.32 dB |
| C: rotated 22.5° | 0.20 dB |
| C: rotated 30° | 0.24 dB |
| C: rotated 45° | 0.71 dB |
| D: quadrature | 0.80 dB |

**The readout choice barely moves it**, and on reflection it could not: by the
time energy reaches the readout it has been through the feedback matrix many
times and carries no memory of which input channel delivered it. Readout
geometry decides how the two outputs *view* the tank; it cannot decide what
the tank *remembers*.

## 13. Injection control experiment — the second negative result

Research only. Injection vectors blended toward their own channel's readout,
renormalised so drive level is unchanged:

| injection | left only | mean bias | corr | mono fold | side |
|---|---|---|---|---|---|
| A: current | −1.06 dB | **0.74 dB** | −0.0087 | −3.05 dB | −6.0 dB |
| B: slight retention | −0.31 dB | **0.30 dB** | 0.0008 | −3.01 dB | −6.0 dB |
| C: moderate retention | −0.64 dB | **0.73 dB** | 0.0371 | −2.85 dB | −6.2 dB |

**Source-side retention is not an injection property either**, which is the
opposite of what this experiment was set up to confirm. There is no
improvement at all, and the intermediate blend is *lower* than the baseline.

**The reason is the feedback matrix, and it applies to both levers at once.**
Side retention would require energy that entered on the left to still be
preferentially visible to the left readout. The 8×8 orthogonal matrix mixes
every line into every other one *every sample*, so after a handful of loop
traversals the state is close to isotropic and carries no direction to retain.
Whatever side information survives comes from the first few milliseconds,
before the mixing has run — a small fraction of the energy at any musical
Decay.

**So neither readout data nor injection data can deliver source-side
retention in a single shared tank with a scrambling matrix. That is an
architectural limit.**

## 14. Decay and RT60 impact

**None, and it is structural rather than measured loosely.** The readout is
*outside the recurrence*: it observes the line states and does not feed them,
so it cannot change RT60, the damping shelves, the loop gains or the
modulation law — the same argument Phase 9 made for Attack. Total tail energy
is preserved across all candidates, which is the rotation's norm invariance
showing up in audio.

## 15. Spectral impact

Spectral flatness over 200 Hz – 10 kHz is unchanged across candidates. A
rotation redistributes the *same* line states between two outputs; it does not
alter what the lines contain.

## 16. CPU and memory impact

| | |
|---|---|
| per-sample CPU | **unchanged** |
| memory | **unchanged** — two arrays of eight doubles |
| SIMD suitability | **unchanged** |
| branch count | **unchanged** |

The readout is eight multiply-accumulates per channel either way; only the
*coefficients* differ. `StereoFdn.h` does not special-case ±1 anywhere — the
loop multiplies — so non-binary weights cost exactly the same.

**A pure data change with no runtime cost**, which is what the brief hoped for.

## 17. Best readout candidate

**None of them solves the blocker, so there is no winner to propose.**

The most defensible *partial* candidate is **C: rotated 45°**, and its ledger
is mixed rather than good:

| | |
|---|---|
| the identified failure | **fixed** — 9/9 windows → 0/9, −19.43 → −3.81 dB |
| worst normal cell | **worse** — −5.92 → −16.54 dB |
| broadband centring | **better** — 2.22 → 0.17 dB |
| L/R swap symmetry | **better** — 0.64 → 0.06 dB |
| per-frequency balance scatter | **worse** — 16.4 → 20.2 dB |
| decay, spectrum, CPU, memory | unchanged |
| source-side retention | unchanged |

**Recommending it on the strength of the one cell it fixes would be fitting to
a single measurement** — the mistake this project has caught four times
already. It is not recommended.

## 18. Rejected candidates and reasons

| candidate | reason |
|---|---|
| **B: Hadamard rows** | sign vectors, so they inherit all four cancelling lines — proven by enumeration before being rendered, and confirmed in audio (min corr −0.9872). Also the only candidate with a systematic 2.31 dB channel preference, which is a blocker on its own. |
| **D: quadrature** | worse than production on the subset metric (−15 to −21 dB) and worse in audio (normal-cell fold −13.57 dB against −5.92 dB). Having no zero-sum lines is not sufficient. |
| **C: rotated 15°, 22.5°, 30°** | improve the subset metric monotonically but do not clear the failing point: all three still show 9 of 9 windows below −0.5 correlation at Size 15 %, Decay 0.6 s, 220 Hz. |
| **C: rotated 45°** | not rejected outright, but not recommended — see section 17. |
| any ±1 pair whatsoever | ruled out by exhaustive enumeration, section 2. |

## 19. Recommended pan-retention philosophy

The three positions the brief asked for:

### A. Diffuse / centre-seeking tail — *current behaviour, 0.74 dB*

The late field forgets where the source was and places everything centrally.
**Audibly:** a wide, even, enveloping tail; a hard-panned source gets a tail
that surrounds rather than one that sits beside it. **Architecturally:** free —
it is what the shared tank with a scrambling matrix already does.

### B. Moderate source-position preservation

A hard-panned source would keep a few dB of tail bias. **Audibly:** a sense
that the reverb "belongs to" the source position, which many engineers expect
from a true-stereo reverb. **Architecturally: not reachable through data.**
Section 13 shows neither injection nor readout achieves it. It would need the
matrix to mix less, or separate per-channel tanks, or a parallel short
true-stereo path — all new DSP, all needing their own neutrality gates.

### C. Strong true-stereo source-position preservation

Near-full retention. **Audibly:** essentially two reverbs, closest to a pair of
mono reverbs. **Architecturally:** genuinely separate left and right tanks.
Roughly doubles the tank cost, loses the shared-mode character that gives this
engine its density, and would be a different product.

### Recommendation for ACTIVE Reverb V1: **A**

Three reasons, in order of weight.

1. **B and C are not data changes.** V1 does not need new DSP for a behaviour
   nobody has reported missing, and Stage 1 explicitly declined to call
   0.74 dB a defect.
2. **The ER layer already provides what B offers, where it matters.** It has
   no cross-channel path at all and preserves side fully, so any Early/Late
   above 0 already gives source-position information in the early field —
   which is where listeners localise.
3. **Phase 12 can revisit it as Mode data** if a Room or Chamber mode wants a
   tighter, more positional character. The injection and readout vectors are
   already runtime-settable; what is missing is a coupling parameter, and that
   is a Phase 12 conversation.

## 20. Proposed production change

**None. No production change is proposed from this stage.**

Readout-vector data cannot fix the blocker, which was the hypothesis Stage 2
existed to test. The negative result is the deliverable.

### What the evidence points to instead

The root cause is not the readout geometry but **sparse modal density**: the
failure needs few active lines, which is what small Size and short Decay
produce. Three directions, in the order I would take them:

1. **Delay-set research at small Size.** Modal density is set by the delay
   lengths, and the delay set is *data*. A set chosen to keep modal density
   adequate at the smallest Sizes attacks the cause rather than the symptom,
   and stays within the "data before mechanism" discipline this stage was
   asked to follow. **Recommended first.**
2. **A readout rotation combined with (1).** The rotation's benefits —
   broadband centring, L/R symmetry — are real and free; they are simply not a
   fix on their own. It may become the right choice *once the modal density
   question is settled*, and should be re-measured then rather than now.
3. **A decorrelating element between the two outputs.** New DSP, and it would
   need its own Phase-10-style neutrality gate. Last resort, and the brief
   already rules out the related shortcuts — correlation clamps, dynamic mono
   protection, post-Width manipulation — for sound reasons.

### If a readout change is ever authorised

The Phase-10 legacy byte identity will no longer hold across the Phase-11
boundary, because the late waveform changes. **That must not be hidden.** The
procedure would be: establish an explicit Phase-11 re-baseline, re-run every
Phase-10 behavioural gate, and prove Phase-10 *semantics* remain correct even
though the exact waveform differs.

## 21. Tests added

`Tests/Phase11Readout.cpp`, 8 sections, **16 checks**, registered in
`RunAll.cpp`. Two fail deliberately and record the negative results:

```
the 45 degree rotation removes the identified failure at its own operating point   PASS
and improves the worst NORMAL operating cell over production                       FAIL
injection blending increases source-side retention - IT DOES NOT                   FAIL
```

Includes exhaustive enumeration of all 65 536 ±1 pairs, exhaustive subset
analysis over all 2⁸ subsets up to size 4, a 140-cell narrow-band stress grid
per candidate, broadband regression over six sources, swap symmetry, the
injection control experiment, and decay/spectrum/cost.

### Two of my own errors, corrected

1. The subset metric was initially quoted as if it bounded real behaviour. It
   assumes independent line states; the matrix correlates them. Now stated as
   necessary-not-sufficient, which is what the audio showed.
2. The injection section's commentary asserted that blending raises side
   retention before the measurement came back. It does not. The prose was
   rewritten to the measured result and the check now fails on the claim.

## 22. Regression result

**2547 / 2561**, run undisturbed. Fourteen failures, all accounted for:

- **six** pre-existing Phase 7 diagnostics, previously verified against a
  rebuilt `a1328f0`
- **four** intentional Phase 10 gates
- **four** Phase 11 Stage 1 finding-gates

**No new regression. No gate weakened.** No production code changed in Stage 1
or Stage 2, so no audio changed.

## 23. Commit

`af2d7d7` — "Phase 11 Stage 1 and 2: stereo audit and readout-topology research".

---

## Stage 2 verdict

**The question was whether the blocker and the pan-retention question can be
improved through stereo topology data before introducing new DSP. The answer
is no, for both, and both reasons are now identified:**

- The blocker is not caused by the readout *choice*. Orthogonal ±1 pairs
  necessarily cancel four lines — proven exhaustively — and escaping the ±1
  family improves the subset metric without fixing the audio, because line
  states are coupled by the matrix.
- Pan retention is not reachable from either the readout or the injection
  vectors, because the matrix scrambles direction within a few loop
  traversals.

**Recommended next step: Stage 3 on delay-set / modal-density research at
small Size**, which attacks the blocker's actual cause and stays within data.

**No production change is authorised or proposed. Phase 10 remains closed.**
