# Phase 12 Stage 8 — Ambient: addendum 5 (decisions on the root cause; the 4 → 4 sibling, registered)

Addendum to `Phase12-Stage8-Ambient-Repair-Preregistration.md`. Written
after the decisions of 2026-11-18 on the scan and root-cause report and
**before the candidate is built or anything below is measured.**

## 1. The decisions

1. **Small / short.** Preserving A1b's staged buildup there is required.
   Strong separation from Hall there is not. Under the nine-point
   Mode-identity criterion Ambient may predeclare the **short-Decay edge**
   as its convergence region against Hall, within the approved limits
   (at most three points, one edge, never the default, the rest under the
   normal gates). The small / short point is not an identity anchor
   against Hall; it is an A1b-preservation point. No earlier result
   changes.
2. **Pre-Delay.** Not an eighth dimension of every frozen search. The
   primary structural search is **onset-normalised**, which removes a
   uniform shift from the comparison on purpose. Separately, a **one-time
   adversarial Hall + Pre-Delay verification** on frozen A1b and on every
   Ambient finalist, read both in absolute time and onset-normalised.
3. **Identity.** Late texture is no longer Ambient's primary identity and
   is not a gate. The primary measurable identity is **staged energy
   migration / post-onset buildup**.
4. **Topology.** The repair may change the number of joins and the lines
   per stage. **One** sibling is authorised: *timed and Decay-calibrated
   4 → 4*. A research candidate, not a production decision. No family of
   variations. It tests one hypothesis: fewer sparse joins keep A1b's
   buildup and reduce the variance.
5. **B and C on A1b.** No candidate campaign. Minimal diagnostic controls
   only if needed.
6. **A1b preservation gates, approved:** t10 within ± 10 %; t50 within
   ± 10 %; t10 / t50 within ± 0.03; buildup-family and energy-profile-
   family distance to A1b ≤ 1.5. Identity preservation, not Hall
   distinctness.
7. **Tone:** ≤ 1.0 dB, unchanged.
8. **Source-dependent level:** no absolute number yet. Quantify the Hall
   control's natural variation, define the candidate's excess over it, and
   register the threshold before the candidate is evaluated against it.
9. **The first experiment** and its report list (section 6).
   If 4 → 4 fails A1b preservation: stop and report why before any other
   topology.

## 2. The distinction decision 2 asks to be written down

- **Hall + Pre-Delay that only moves the whole response later is not an
  Ambient mimic.** A uniform delay changes when the reverb starts and
  nothing about how its energy grows. The onset-normalised comparison is
  blind to it by construction: Hall delayed by any amount reads 0.00 –
  0.03 against Hall on every family (measured, scan).
- **Hall + Pre-Delay + its visible controls that reproduce the
  onset-normalised buildup as well is a mimic.** If, with the start
  aligned, Hall's Size, Decay, Attack, Early/Late, diffusion, modulation
  and Width bring it within 1.5 of the candidate, a user holding Hall can
  make the candidate's sound, and that counts against it.
- So Pre-Delay is allowed to explain *where the response starts* and
  nothing else. What must survive it is the shape of the growth.

## 3. The candidate: AS1, timed and Decay-calibrated 4 → 4

Everything here is fixed now. One build. Nothing is tuned after a
measurement.

| | |
|---|---|
| lines | Hall's eight delays, sorted ascending, indices 0 – 7 |
| stage 1 (fed) | lines 0, 2, 4, 6 |
| stage 2 (heard) | lines 1, 3, 5, 7 |
| within a stage | a 4 × 4 Hadamard mix, as Stage 5's builder gives a four-line block |
| the join | line i of stage 1 rotated against line i of stage 2, one angle θ for all four — Stage 5's builder, unchanged |
| timing | sin²θ = (mean loop time of stage 1) / τ, capped at 0.5; **τ = k × RT60** (one join) |
| input | stage 1 only. No direct share |
| output | stage 2 only |
| input and output vectors | the generic orthonormal quad (`patterns::genericQuad`): rows 0 and 1 into stage 1 for left and right, rows 2 and 3 out of stage 2 |
| diffuser, modulation, damping | Hall's, unchanged |
| decay | calibrated per setting to Hall's RT60, as A1b's is |
| level | matched to Hall at the defaults, as A1b's is |

**Why interleaved lines.** A1b's stages each span the short and the long
delays ({0,4}, {1,5}, …). Interleaving keeps that for two stages of four.

**Why the generic vectors.** Stage 3 found that sign vectors on a
four-line Hadamard block are rows of the block's own matrix, which cost
Ambient A – C 7 – 8 dB of mono fold-down, and Stage 5's A3 (a four-line
heard stage on the builder's sign vectors) failed mono fold-down at
−5.5 dB. Using vectors aligned with nothing is the known remedy and is
chosen in advance for that reason, not after seeing AS1.

**k — a rule, not a tuning.** A1b's k = 0.40 is spread over three joins;
with one join there is no value to inherit. k is set by one calibration,
fixed here: **the single k, between 0.05 and 3, at which AS1's
onset-normalised t50 at the medium point equals frozen A1b's (204.9 ms)
within 1 %,** found by bisection. That k is then used at every setting.
The calibration consumes exactly one of the preservation figures at one
point (t50, medium). t10, the shape number, both families, and everything
at short and long remain tests. If no k in the range reaches A1b's t50,
that is reported and AS1 is built at the nearest.

**Prediction, so it can be wrong.** I expect AS1 to **fail the shape
gate**. A two-stage cascade rises more abruptly than a four-stage one: for
equal stages without loss the shape number is about 0.32 for two against
0.47 for four, and A1b measures 0.38. With t50 matched, I expect AS1's
t10 to come out early (below −10 %) and its shape near 0.30 – 0.33,
outside ± 0.03. I expect the variance to fall to roughly Stage 3 Ambient
B's (0.4 – 0.5 dB at 125 Hz) and the source spread to about half of
A1b's. If the prediction holds, the hypothesis is half right — the
variance does come from the joins — and the buildup needs them.

## 4. Gates

**4.1 A1b preservation** — at the three operating points, same knobs,
onset-normalised:

| | pass |
|---|---|
| t10 | within ± 10 % of A1b's |
| t50 | within ± 10 % |
| t10 / t50 | within ± 0.03 |
| buildup family, AS1 against A1b, onsets aligned | ≤ 1.5 |
| energy-profile family, the same | ≤ 1.5 |

All five at all three points. Reported also at the six other grid
settings, where they do not gate.

**4.2 Mode identity, nine points, against Hall.** The approved criterion.

- Grid: Size 25 / 60 / 85 × Decay 0.8 / 2.5 / 5.0 s.
- **Declared convergence region against Hall: the short-Decay edge** —
  (25, 0.8), (60, 0.8), (85, 0.8). Reason: at short Decay the migration
  time and the loop times are of the same order, the build is compressed,
  and the scan found A1b's shape number at or below Hall's there.
- Core: the other six, including the default. Each must be DISTINCT: five
  seeds, no run ≤ 1.5, median ≥ 3.0.
- **The search is onset-normalised:** the target is the candidate's
  response with everything before its onset set to exact zero and moved to
  Hall's onset at the same knobs; Hall's frozen seven-control search runs
  unchanged.
- The same nine-point search is run on frozen A1b, for comparison. That is
  a new measurement on A1b; it changes no recorded verdict.
- Plate and Room as references are not part of this first experiment.

**4.3 The one-time Hall + Pre-Delay verification**, on frozen A1b and on
AS1, at the three operating points, five seeds:

- **A, absolute time:** Hall's search against the response as it is, with
  only the sub −200 dB pre-arrival dust set to exact zero.
- **B, with Pre-Delay searched:** the target moved earlier by D for
  D = D\* − 20, D\* − 10, D\*, D\* + 10, D\* + 20 ms (not below zero),
  where D\* is the candidate's onset minus Hall's at the same knobs. Each
  D is Hall with that Pre-Delay. The reading at a point is the **lowest**
  median over the five D.
- **Reading:** a run ≤ 1.5 at any D → *Hall + Pre-Delay + visible controls
  reproduce the onset-normalised buildup here: a mimic.* Median ≥ 3.0 at
  every D → *not a mimic*. Between → weak, with the figures. At the three
  declared convergence points this is reported and does not gate; none of
  the three operating points other than small / short is in that region.

**4.4 Tone.** Octave shares against Hall ≤ 1.0 dB at the defaults and
over Stage 6's 27 settings (three Sizes × three Decays × three damping
settings). Unchanged. Reported with it: the mean and standard deviation
per octave under the ± 4 % and the ± 30 % perturbations.

**4.5 Source-dependent level.**

- *Baseline, measured before AS1:* the source spread (largest minus
  smallest gain over vocal, snare and percussion, structure level-matched
  on its impulse response at the setting) of **Hall's sibling delay set
  and of Hall under each of the twelve ± 4 % perturbations**, at the three
  points. The baseline at a point is the **largest** of those thirteen.
- *Excess:* the candidate's spread minus the baseline at that point; not
  below zero.
- **Pass, fixed now, before the baseline or the candidate is measured:**
  excess ≤ 0.5 dB at every point; and where A1b's excess is above 1.0 dB,
  the candidate's is at most half of A1b's.
- 0.5 dB is my choice. **It is an engineering criterion, not a
  psychoacoustically validated perceptual threshold**: no listening test
  stands behind it. (Labelled so by decision, 2026-11-18, when this
  pre-registration was approved as written.)

**4.6 Size, Decay, stereo and mono, CPU.** Stage 5's G3 as applied to
A1b: RT60 within 5 % of Hall's over nine settings; t10 monotone in Size;
Stage 3's stereo and mono limits; CPU ≤ 1.5 × Hall.

## 5. Verdicts

- Fails 4.1 → **does not preserve A1b.** Everything else is still measured
  and reported, because the report was asked for in full; then stop. No
  other topology is built.
- Passes 4.1 and fixes the variance (4.4 and 4.5) and passes 4.2, 4.3,
  4.6 → the new Ambient production candidate, by decision 9.
- Anything between is reported as measured, gate by gate.

## 6. What the report will contain

t10 and t50; the shape number; buildup and energy-profile distance to
A1b; the nine-point Hall result; the one-time Hall + Pre-Delay result;
octave mean deviation; octave variance; source-dependent level error; Size
and Decay semantics; stereo and mono; CPU; every figure beside frozen
A1b's.

## 6a. Approval

**Approved as written, 2026-11-18.** The topology, the calibration point,
the convergence region, the preservation gates and the prediction are not
to be changed after results are seen. The only edit made after approval
is the labelling of the 0.5 dB figure in 4.5, as instructed. If AS1 fails
A1b identity preservation: stop before any other topology and report
exactly which preservation dimensions failed.

## 7. Not done

No B or C control is built unless a result here cannot be read without
one. No second topology. No Mode selector. No Phase 13.
