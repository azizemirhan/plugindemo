# Phase 12 Stage 9 — Ambient AS2: a sixteen-line, four-stage dense migration network. Pre-registration

Research only. `Source/` is unchanged. No Mode selector, no Phase 13, no
production integration.

**Nothing in this document has been built or measured. It is for review.
AS2 is not coded until it is approved.**

## 0. The decisions this starts from (2026-11-18)

1. **AS1 is closed.** No further ordinary 4 → 4 candidate.
2. **A1b** is no longer a production-viable Ambient candidate. It is
   frozen as the **Ambient structural / sonic research reference** and is
   not modified. It fails final acceptance because two non-convergence core
   points are below the DISTINCT threshold. The nine-point criterion is the
   standard for future Ambient finalists and is not lowered to rescue it.
3. **The Pre-Delay confound** at A1b's default point is resolved once, with
   full-support alignment and no target sample discarded. A methodological
   verification; it does not restore A1b.
4. **Final mimic gate.** For final Mode acceptance Pre-Delay is part of
   Hall's visible-control mimic space. The onset-aligned search remains a
   diagnostic and is not sufficient as the final gate. The search may be
   nested or coarse-to-fine instead of a brute-force eighth dimension.
   **Alignment and search must never discard target samples**; padding is
   used so that a uniform offset is evaluated without truncating the
   target.
5. **AS1's variance ambiguity** — one transition, or unusually weak
   coupling — is recorded as an unresolved diagnostic ambiguity and is not
   pursued.
6. **Next hypothesis: multiple migration stages and denser stages.**
   Sixteen lines, four stages, 4 → 4 → 4 → 4. Chosen over a twelve-line
   three-stage form because that would change stage density and transition
   count together.
7. **Preservation** of A1b's useful temporal behaviour by the frozen
   measures: t10, t50, t10 / t50, the buildup family, the energy-profile
   family. A1b's tonal fingerprint is not to be preserved.
8. **Production gates**, independently: the nine-point criterion; the final
   Hall mimic search including Pre-Delay; tone ≤ 1.0 dB; the
   source-dependent level gate; Size; Decay; stereo and mono. Reported as
   well: CPU against Hall, memory, switching and pre-warm. The research
   candidate is not rejected on CPU alone.
9. Stop after this document, before coding.

## 1. The hypothesis

*A1b's staged buildup needs its three joins. A1b's spectral variance is
added at each join because each stage is sparse — two lines. Keep the three
joins and double the lines in every stage: the buildup should stay and the
variance should fall.*

What is held fixed against A1b: the number of stages (four), the number of
joins (three), the join law, the timing law, where the input goes in and
where the output is taken. What changes: lines per stage (two → four), and
with it the number of lines (eight → sixteen) and the within-stage mix.

## 2. AS2, exactly

One build. Nothing below is tuned after a measurement except the single
calibration of 2.6.

### 2.1 State

Sixteen delay lines in one feedback loop through one 16 × 16 orthogonal
scattering matrix. Per line, as in every structure of this phase: the
decay gain for its own length, the two damping shelves, the modulated
allpass. Built in the research open tank (`stage6::OpenTank`), which takes
any number of lines and applies production's own decay, damping and
modulation classes per line.

### 2.2 Delay allocation

The sixteen delays are **Hall's eight together with the eight of Hall's
sibling set** (`mode12::hallSibling()`: the set found by the Stage 2 delay
search with Hall's own sum and ratio, seed `0x4A11`), sorted ascending and
indexed 0 – 15.

- Both sets exist already and neither is chosen for this candidate.
- Their sums are equal, so the mean line length is Hall's and A1b's.
- **A known risk, not checked:** each set was searched for good behaviour
  within itself, not against the other. Two of the sixteen may fall close
  together. The smallest ratio between neighbouring delays is reported
  with the result. The set is not altered if it is small.

### 2.3 Stage membership

| stage | lines (indices in the sorted sixteen) | role |
|---|---|---|
| 1 | 0, 4, 8, 12 | fed |
| 2 | 1, 5, 9, 13 | |
| 3 | 2, 6, 10, 14 | |
| 4 | 3, 7, 11, 15 | heard |

Each stage spans the whole range of lengths, as A1b's pairs {0,4} {1,5}
{2,6} {3,7} do; later stages are slightly longer, as in A1b.

### 2.4 Scattering

- **Within a stage:** a 4 × 4 Hadamard mix (every line of a stage feeds
  every line of it equally).
- **Between neighbouring stages:** line i of stage s is rotated against
  line i of stage s + 1 by one angle θₛ, for i = 1 … 4. This is A1b's join,
  with four pairs in place of two.
- The 16 × 16 matrix is the block-diagonal of the four Hadamard blocks with
  the three sets of rotations applied in order, stage 1 – 2 first. It is
  orthogonal by construction; the orthogonality error is checked before
  anything is measured (limit 10⁻¹²).
- No path from stage 1 to stage 4 other than through stages 2 and 3.

### 2.5 Input and output vectors

Input into stage 1 only. Output from stage 4 only. **No direct share; no
other stage heard.**

AS1 showed what goes wrong both ways: sign vectors on a Hadamard block are
rows of the block's own matrix (Stage 3: 7 – 8 dB of mono fold-down); the
generic orthonormal quad fixed that and left a 1.3 dB left / right
imbalance. So the vectors are specified here from two requirements:

- left and right have the **same magnitude on every line** — then the two
  channels carry the same energy whatever the lines hold;
- left and right are orthogonal, and neither is a row of the Hadamard
  block.

Both hold for unequal magnitudes whose squares sum to zero with two signs
flipped:

| | line 1 | line 2 | line 3 | line 4 |
|---|---|---|---|---|
| output, left | +√0.4 | +√0.1 | +√0.3 | +√0.2 |
| output, right | +√0.4 | +√0.1 | −√0.3 | −√0.2 |
| input, left | +√0.3 | +√0.2 | +√0.4 | +√0.1 |
| input, right | +√0.3 | +√0.2 | −√0.4 | −√0.1 |

(0.4 + 0.1 − 0.3 − 0.2 = 0.) Unit norm, orthogonal, balanced per line. If
the four lines of the heard stage hold equal energy the mono fold-down is
−3.0 dB. This pair has not been used before and is untested.

### 2.6 Timing, and the one calibration

A1b's law, unchanged:

    sin²θₛ = (mean loop time of stage s, at the current Size) / τ, capped at 0.5
    τ = k × RT60 / 3

**k is the single degree of freedom.** It is set by one calibration, fixed
here: the k between 0.10 and 1.50 at which AS2's onset-normalised t50 at
the medium point equals frozen A1b's (unperturbed A1b: 202.7 ms) within
0.5 %, by bisection; that k at every setting. If no k in the range reaches
it, AS2 is built at the nearest and that is reported first.

As with AS1, this consumes one preservation figure at one point (t50,
medium). t10, the shape number, both families, and everything at short and
long remain tests. A1b's own k is 0.40; with the same number of joins I
expect AS2's near it.

### 2.7 Size

Every delay is multiplied by Hall's Size scale, as in Hall and A1b. The
stage loop times, and so the angles, follow. Nothing else depends on Size.

### 2.8 Decay and level

- Decay: the tank is asked for RT60 × a decay scale fitted per setting so
  that the measured RT60 is Hall's — A1b's procedure, Stage 5's code path.
- Level: matched to Hall at the defaults, once. The static level law over
  Size and Decay that would be needed is reported; none is fitted.
- Diffuser, attack stage, early layer, Width: Hall's, unchanged.

### 2.9 Calibration budget

| what | how many | status |
|---|---|---|
| k | 1 | the calibration of 2.6 |
| decay scale per setting | as A1b | a calibration to Hall's RT60; reported as the surface a product would have to reproduce |
| level at the defaults | 1 | as every structure of this phase |
| everything else | 0 | fixed in this document |

## 3. Before AS2 is believed: instrument checks

AS2 is the first Ambient built in the open tank and not in the eight-line
core. Before any AS2 figure is read:

| check | limit |
|---|---|
| the open tank built as Hall against Hall (exists, Stage 6) | −60 dB |
| **A1b rebuilt in the open tank** — same lines, same matrix, same vectors — against A1b in the eight-line core, frozen metric, same knobs, three points | ≤ 1.5; and t10, t50 within 2 % |
| AS2's matrix | orthogonality error ≤ 10⁻¹² |

If A1b in the open tank is not A1b, the open tank is not a valid place to
compare AS2 with A1b and nothing further is read until that is understood.

## 4. Gates

### 4.1 A1b preservation (approved thresholds, unchanged)

At the three operating points, same knobs, onset-normalised, against
frozen A1b: t10 within ± 10 %; t50 within ± 10 %; t10 / t50 within ± 0.03;
buildup family ≤ 1.5; energy-profile family ≤ 1.5. Reported at the other
six grid settings, where they do not gate. Tone is not compared with
A1b's.

### 4.2 Nine-point Mode identity against Hall

As approved. Declared convergence region: the short-Decay edge. The six
core points, the default among them, DISTINCT (five seeds, no run ≤ 1.5,
median ≥ 3.0). At most three points not DISTINCT in all. The candidate's
signature — 4.1's measures — reported at all nine.

**The search used for this gate is the final mimic search of 4.3**, not
the onset-aligned one; the onset-aligned figures are reported beside it as
a diagnostic.

### 4.3 The final Hall mimic search, including Pre-Delay

Decided in principle; the implementation is proposed here for approval
with the rest.

- **Hall's response is delayed; the target is never touched.** Hall with
  Pre-Delay D is Hall's response with D of zeros in front, observed over
  the same time. No target sample is moved, zeroed above −200 dB, or
  discarded. (Samples more than 200 dB below the response's loudest 5 ms,
  before its first arrival, are set to exact zero in the target, so that
  the late-texture family does not read numerical dust as texture. That is
  the one preparation, and it removes nothing audible.)
- **Nested, coarse to fine.** Outer: D on a grid of 15 ms from 0 to
  150 ms. At each D the inner search is the frozen seven-control search —
  same scatter of starts, same pattern search, same objective, same seed
  rule — run on the delayed Hall. Then D is refined around the best outer
  value in steps of 5 ms and 1 ms with the inner search re-run. Five
  seeds, as G2.
- **It is new code beside the frozen search, not an edit of it.** Its
  first check is that at D = 0 it returns the frozen search's residuals
  exactly, run for run.
- **Reading at a point:** the result is the lowest over D. DISTINCT: no
  run ≤ 1.5 and median ≥ 3.0 at the best D. REPRODUCED: any run ≤ 1.5 at
  any D. Otherwise BETWEEN.
- The **interpretation** stands as decided: Pre-Delay that only moves Hall
  later reproduces nothing, because a delayed Hall is still Hall's buildup;
  Pre-Delay together with Hall's other controls reproducing the candidate
  is a mimic.
- Cost: about eleven outer values and six refinements per point, each a
  five-seed search — some fifteen to twenty times one of today's gates.
  Nine points, two subjects (AS2 and frozen A1b): hours. It is run once
  per finalist.

### 4.4 Tone

Octave shares against Hall ≤ 1.0 dB at the defaults and over Stage 6's 27
settings. Unchanged. With it: mean and standard deviation per octave under
± 4 % and ± 30 % delay perturbations.

### 4.5 Source-dependent level

As registered for AS1 and unchanged: baseline = the largest spread among
Hall's sibling and Hall under twelve ± 4 % perturbations (measured: 0.66 /
0.82 / 0.72 dB); excess ≤ 0.5 dB at every point, and at most half of
A1b's excess where that exceeds 1.0 dB. **An engineering criterion, not a
psychoacoustically validated perceptual threshold.**

### 4.6 Size, Decay, stereo and mono

RT60 within 5 % of Hall's over nine settings; t10 monotone in Size; Stage
3's stereo and mono limits against Hall's own row.

### 4.7 Reported, not gating

- CPU against Hall, wet path, with the interleaved-pair timer of the Plate
  tool; on an idle machine if one is available, otherwise marked
  indicative.
- Memory of the tank at the largest Size.
- Switching: the cost of two networks during a crossfade with Hall, and
  the pre-warm AS2 needs at long Decay to be within 1 dB of its settled
  response.

## 5. Order

1. The instrument checks of section 3.
2. The calibration of k.
3. Preservation (4.1). **If it fails: stop and report which dimensions
   failed**, as with AS1 — the remaining cheap measurements (4.4 – 4.7) are
   still made, the long searches are not started without a decision.
4. Tone and variance; source level; Size, Decay, stereo, mono; CPU and
   memory.
5. The final mimic search on AS2 and on frozen A1b (4.2, 4.3).

## 6. Expected cost

| | Hall | A1b | AS2, estimate |
|---|---|---|---|
| lines | 8 | 8 | 16 |
| matrix work per sample | one dense 8 × 8 Hadamard | block form | four 4 × 4 Hadamard blocks and twelve plane rotations: about the arithmetic of the 8 × 8, not twice it |
| per-line work (decay, two shelves, modulated allpass, interpolated read) | 8 | 8 | 16 — this is where the cost doubles |
| **wet path × Hall** | 1.00 | 0.87 – 1.27 | **1.6 – 2.1** |
| tank memory at the largest Size | 498 KiB | 498 KiB | **about 1.0 MiB** |
| two networks during a switch | — | about 2.0 × | about 2.7 – 3.1 × |

The upper CPU figure is over the 1.5 × limit earlier stages used. By
decision that does not reject the research candidate. These are estimates
from line counts; nothing has been timed.

## 7. Predictions and expected failure modes — written before the build

I am asked for expected failure modes. I think AS2 is the right experiment
and I expect it to fail two or three gates. Each of these can be wrong.

**P1. Preservation: probably passes, narrowly on shape.** Same stage
count, same join law, k calibrated to t50. I expect t10 within 10 % and
the shape number within 0.03 of A1b's 0.38. The Hadamard mix inside a
four-line stage spreads energy across the stage faster than A1b's
two-line rotation, which may make each stage hand on slightly sooner:
if it fails, I expect t10 early.

**P2. Variance: improves, and not enough.** Step A found about 0.3 – 0.5 dB
of scatter added per join at 125 Hz with two-line stages. Doubling the
modes per stage should cut each by about √2. I expect the 125 Hz standard
deviation near **0.8 dB** where A1b's is 1.14 (medium), not near Hall's
0.36.

**P3. Tone gate: fails over the grid.** With 0.8 dB of scatter the worst
of 27 settings is expected around 2 dB. The mean should be A1b's: mid
bands +0.4 to +0.6 and 8 kHz −0.5 to −0.7 from loss met while migrating,
which nothing in AS2 addresses. **I expect AS2 to fail ≤ 1.0 dB.** The
arithmetic of the root-cause report said the scatter per join has to fall
to about 0.2 dB, which is some ten times the modes per stage, not twice.

**P4. Source-dependent level: improves, still outside.** Excess of about
1.0 – 1.4 dB at medium and short against A1b's 1.6 and 2.0; limit 0.5.

**P5. Nine-point identity: fails where A1b fails.** This is the one I
would most want read before the build. A1b is BETWEEN at Size 25 and
Size 85 with Decay 2.5 s (2.43 and 2.71) under the onset-aligned search,
and those searches stop on modal structure and stereo, not on buildup —
Hall's Attack control had already matched the buildup. **A candidate that
preserves A1b's buildup inherits A1b's buildup-based separation from
Hall, and that separation is what fell short.** Denser stages should make
AS2 *smoother*, that is more like Hall on the families those searches
stopped on. I therefore expect AS2 to be no more distinct than A1b at
those two points, and possibly less.

**P6. Final mimic search with Pre-Delay: weak at the default.** A1b read
1.93 there, and 1.72 in the full-support verification with no target
sample discarded (`Phase12-Stage8-PreDelay-Verification.md`). Same
reasoning as P5.

**P7. Stereo and mono: pass.** The balanced vectors are built for it. If
the lines of the heard stage hold unequal energy the correlation moves
away from zero; I expect it inside the limits.

**P8. Decay and Size: pass**, as A1b.

**P9. CPU 1.6 – 2.1 × Hall; memory about 1 MiB.**

**What would make AS2 a success** is P1 holding and P2 – P4 being wrong in
the good direction — the scatter falling to Hall's floor with only twice
the lines. **What it cannot fix by construction** is P5 and P6: if they
hold, preservation of A1b and the nine-point criterion are pulling in
opposite directions, and no amount of density resolves that. The
experiment would then have answered the question it was built for — is
the variance a density problem — and left the identity question where the
AS1 report left it.

## 8. Not in AS2

No direct path. No output filter. No hearing of a stage before the last.
No per-stage loss compensation (step C) and no level law (step B): either
could be added to a candidate that passes 4.1, 4.2 and 4.3, after it is
measured without them. No second variant.

## 9. Stop

This document is for review. Nothing is coded until it is approved.

---

# Addendum 1 (2026-11-19) — approval, clarifications, and the vectors replaced

Written before AS2 is measured. No AS2 response, score, tone or level
figure existed when any of this was decided; the only AS2 output seen was
the algebraic design check that found the fault below.

## A1. Approval and frozen clarifications

AS2 is approved to be built and measured once, with these clarifications:

- A1b failing the nine-point criterion does not invalidate AS2 in advance.
  AS2 is not a clone of A1b: denser stages may add structural dimensions
  A1b lacks. That has to be shown, not assumed either way.
- **Preserving A1b is not sufficient.** The A1b-preservation measures
  (t10, t50, shape, buildup and energy-profile families) are reported as
  **temporal-continuity diagnostics, not as proof of Mode validity.** AS2
  must independently pass every production gate: the nine-point criterion;
  the final Hall mimic search including Pre-Delay; tone ≤ 1.0 dB; the
  source-dependent level gate; Size and Decay; stereo and mono.
- A1b is a temporal / sonic research reference only. At its default point
  Hall with Pre-Delay reaches 1.28 (corrected): REPRODUCED.
- The final search is a genuine adversarial Hall search with Pre-Delay
  jointly optimised, coarse to fine, full support, no target sample
  discarded (section 4.3). The onset-aligned search is a diagnostic.
- If AS2 preserves A1b's temporal identity and still fails the same core
  points: stop. No AS3 automatically.
- Section 5's order is superseded on one point: **the full result is
  measured in one cycle**, the final search included, whatever the
  preservation diagnostics read, because preservation is no longer the
  gate that decides whether the rest is worth measuring.
- The fast regression is run after the AS2 cycle.

## A2. Section 2.5 is withdrawn: the first vectors were wrong

Section 2.5 specified unequal-magnitude vectors of one sign pattern and
said that neither was a row of the stage's Hadamard mix. Literally true
and in substance false: **they were 0.972 coherent with rows of the mix**
— the left vector with (+,+,+,+)/2, the right with (+,+,−,−)/2 — so about
94 % of a vector's energy would land on one line after one mix. That is
Stage 3's defect again, and it violates the approval condition that the
vectors not be aligned with the scattering basis. Found by the design
check before any AS2 measurement. The error was mine: I did not compute
the coherence when I wrote 2.5.

## A3. The replacement, chosen by algebra alone

**The mixing matrix is not changed.** The vectors are chosen by the
following method, fixed here, using no audio, mimic, tone, buildup or
quality result.

**Requirements** (as set on 2026-11-19): unit norm; left and right
injection orthogonal; left and right readout orthogonal; balanced
per-line energy (left and right equal in magnitude on every line); no
left / right bias for a centred or mono input; coherence with every row
of the stage's Hadamard mix ≤ 0.70, ≤ 0.60 preferred; after one mix no
line holds more than 50 % of a vector's energy; every |input · output|
≤ 0.50; no output vector a ± copy of an input vector.

**Objective.** Minimise the largest coherence of any of the four vectors
with any row of the mix.

**The solution set, derived.**

- For a unit vector v the four coherences are the entries of H v, and
  their squares sum to 1. So the largest is at least 0.5, with equality
  only when all four are ± 0.5.
- That happens exactly when v = ½ (± r₁ ± r₂ ± r₃ ± r₄) for the rows rᵢ:
  sixteen vectors. Eight are single lines (± eᵢ); eight have entries
  ± 0.5 with an **odd** number of minus signs.
- A single-line vector cannot be balanced against an orthogonal partner.
  So the minimum under the balance requirement is reached exactly by the
  odd-parity sign vectors, of which there are four up to overall sign:

      m₁ = (−,+,+,+)/2   m₂ = (+,−,+,+)/2   m₃ = (+,+,−,+)/2   m₄ = (+,+,+,−)/2

- These four are mutually orthogonal. Using two for the input and the
  other two for the output makes every input · output exactly zero.
- Every value of the objective is then the same for all assignments of
  the four to (input left, input right, output left, output right) and all
  overall signs: 384 ties.

**Tie-break, fixed here.** Overall sign: three entries positive. Order: by
the line that carries the minus sign, ascending — input left, input right,
output left, output right.

| | line 1 | line 2 | line 3 | line 4 |
|---|---|---|---|---|
| input, left | −0.5 | +0.5 | +0.5 | +0.5 |
| input, right | +0.5 | −0.5 | +0.5 | +0.5 |
| output, left | +0.5 | +0.5 | −0.5 | +0.5 |
| output, right | +0.5 | +0.5 | +0.5 | −0.5 |

**What they give, by algebra:**

| property | value | limit |
|---|---|---|
| norm | 1 | 1 |
| left · right, input and output | 0 | 0 |
| magnitude per line, left against right | equal (0.5) | equal |
| coherence with any row of the mix | **0.50** | ≤ 0.70; ≤ 0.60 preferred; 0.50 is the least possible |
| largest share on one line after one mix | **25 %** | ≤ 50 % |
| largest \|input · output\| | **0** | ≤ 0.50 |
| centred input, L + R | (0, 0, 1, 1): lines 3 and 4 of stage 1, energy 2 | no bias |
| side input, L − R | (−1, 1, 0, 0): lines 1 and 2, energy 2 | no bias |
| mono fold of the output | lines 1 and 2 of stage 4; −3.0 dB for equal line energies | |

The fallback (changing a column sign of the mix) is not needed and is not
used. **Approved 2026-11-19; the mixing matrix is not to be modified.**

One thing the algebra does not settle and the measurement will: a centred
source feeds two of stage 1's four lines and the mono fold hears two of
stage 4's. After one mix each vector is spread evenly over all four lines,
so this should not matter; it is the stereo and mono gate's business.

## A4. A statement in section 2.4 corrected

Section 2.4 said there is no path from stage 1 to stage 4 other than
through stages 2 and 3. Over the response that is so. **Within a single
pass of the matrix it is not exactly so:** the three sets of rotations are
applied in sequence, so a small part of stage 1's signal crosses all three
joins in one pass — an entry of about 0.021 in amplitude at the medium
point (the product of the three sines and the Hadamard's 0.5), about
0.05 % in energy per entry.

This is a consequence of the frozen join rule, which is A1b's and is not
changed. **A1b has the same property.** It is recorded as a known
structural consequence and is not treated as a defect of AS2 unless a
measurement shows a problem.

## A5. Design conditions on the delay allocation, checked

Algebraic only; from the design check, log
`Docs/Phase12-Stage9-runs/10-as2-design-check.txt`.

| condition | result |
|---|---|
| delays unique, non-degenerate | closest pair 41.58 and 42.58 ms, 2.4 % apart |
| near-equal delays not clustered in one stage | inside every stage the lines are at least 29 % apart; the closest pairs of the sixteen fall in different stages |
| no systematic slope by delay length alone | stage means 42.8 / 45.8 / 51.4 / 54.3 ms; last over first 1.27, against A1b's 1.71 |
| Size scales all lines consistently | every delay is multiplied by the same Size scale |
| a genuine four-stage network | block-diagonal mix with joins between neighbours only; the one-pass leak of A4 is the only exception |
| matrix orthogonal | error 0.3 × 10⁻¹⁵ |

The slope is not zero: later stages are about 9 % longer each. It is
smaller than A1b's and is a property of the registered allocation; it is
not adjusted.
