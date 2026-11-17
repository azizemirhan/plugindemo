# Phase 12 Stage 5 — five-Mode structural completion programme

Research only. `Source/` is unchanged. No Mode parameter, no production
topology, no Phase 13. Hall is frozen.

Gates and hypotheses were committed before measurement
(`Docs/Phase12-Stage5-Preregistration.md`, `0327aa2`), and three addenda were
appended and committed before the runs they describe (`a007233`, `b9daf55`,
`d6fe0c3`). The metric and the search are Stage 3's, frozen at `c18ecce`.

## The result

**Stage 5's success definition is not met.** It asked for a credible
structural mechanism, past preliminary rejection, for each of five families.
After 63 Plate chains and 18 Room, Chamber and Ambient hypotheses:

| | status | what stands in the way |
|---|---|---|
| **HALL** | proven, frozen | — |
| **PLATE** | reference only. Reduction **failed** | no chain under 1.75 × Hall keeps the identity; the 240-section reference is itself only NOT REFUTED and costs 3–4.6 × |
| **ROOM** | a mechanism that is structurally **SURVIVES** on a centred source (R4c: 5.69 / 6.38 / 4.06) and **fails G3** | Size barely acts and is not monotone; 1.3 dB channel imbalance; worst mono tone 8 dB below Hall's; rings |
| **CHAMBER** | **no mechanism.** Seven hypotheses, seven rejected | — |
| **AMBIENT** | a mechanism that is structurally **SURVIVES** (A1b: 9.77 / 4.00 / 15.33) and **fails G3** | tone: octave bands 1.8 dB from Hall (limit 1) |

So: one proven family, two new mechanisms that are genuinely outside Hall's
reach on a centred source and each blocked by named engineering defects, one
candidate blocked on cost, and one family with nothing.

No threshold was moved. R3 missed NOT REFUTED by two thousandths (a median of
2.498 against 2.5) and is rejected.

**The one general finding:** every mechanism that got past the impostor search
sets its energy exchange *from a time tied to Decay*. Every mechanism that set
it as a fixed angle was reproduced by Hall. Stage 3's Ambient A–E, R1 and C2
all failed for that one reason.

## 1. Hall

Unchanged. `git diff --stat -- Source` is empty. A new fast check (12.5.0)
hashes seventeen responses of the research rig — Hall at five settings and
twelve Stage 3 / Stage 4 structures — against values recorded before Stage 5
touched the rig. All seventeen match bit for bit, so the Stage 3 and Stage 4
results still describe the rig and those heavy suites were not re-run.

---

## Part A — Plate

### 2. The reference

Per channel, after the input diffuser and before the tank: 240 identical
first-order allpass sections in series, `y = a·x + s; s = x − a·y`, a = −0.7,
one state each. No delay lines, no feedback around the chain, nothing
time-varying. Magnitude flat by construction.

| | |
|---|---|
| coefficients | all −0.7 |
| delays | none; each section's group delay is 5.67 samples at DC, 0.18 at Nyquist |
| group delay of the chain | 28.3 ms at 125 Hz, 28.1 at 250, 27.4 at 500, 25.0 at 1 k, 18.5 at 2 k, 9.2 at 4 k, 3.2 at 8 k, 1.2 at 16 k |
| density (Stage 4) | echo density 0.9 at 24 ms, against Hall's 50 ms |
| transient (Stage 4) | wet peak within 1 dB of Hall's; onset 1–3 ms later; 25 ms of arrival spread across bands |
| CPU (Stage 4) | chain 74.6 % of the wet path; linear in sections, 4.2 ns each |
| memory | 2 × 240 doubles, 3.75 KiB |
| serial dependency | section k needs section k−1's output of the same sample: 240 multiply-adds in a row per channel per sample, latency-bound |

### 3. Reduction candidates

63 chains, the list fixed in advance: uniform chains at 240 … 32 sections and
three coefficients; chains of sections in z⁻² and z⁻³ ("stretched"); mixed
coefficient strengths; and mixed stretches in thirds and halves.

**A prediction written first:** an allpass of order K carries exactly Kπ of
phase, so the area under its group-delay curve is fixed by its order. About
70 % of the reference's area is below 5 kHz. So no arrangement of ordinary
sections far below ~170 can keep the curve.

- **Confirmed:** the fewest ordinary sections that stay close to the
  reference is 128.
- **And a second prediction that was wrong:** I expected stretched sections to
  be the way out, because a section in z⁻ᴹ carries M times the phase for the
  same arithmetic. It does, and the extra phase goes into the images at
  rate / M, not into the band where the identity is. A stretch-2 chain needs
  120 sections where an ordinary one needs 128.

### 4–6. Similarity to the reference, distance from Hall, cost

"vs Plate A" and "vs Hall" are centred scores at the same knobs (medium /
small / large). PRESERVED means ≤ 1.5 against Plate A at all three.

| chain | sections | vs Hall | vs Plate A | × Hall | |
|---|---|---|---|---|---|
| Plate A | 240 | 4.11 / 3.76 / 4.14 | 0 | 3.0–4.6 | reference |
| U 160 @ −0.8 | 160 | 3.85 / 3.62 / 3.97 | 0.91 / 0.70 / 0.87 | 3.5 | PRESERVED |
| U 128 @ −0.8 | 128 | 3.31 / 2.90 / 3.29 | 0.96 / 1.03 / 1.04 | 3.0–3.3 | PRESERVED |
| S2 120 @ −0.7 | 120 | 3.44 / 3.10 / 3.54 | 1.31 / 1.30 / 1.34 | 3.1–3.3 | PRESERVED |
| S2 120 @ −0.6 | 120 | 2.86 / 2.52 / 2.91 | 1.28 / 1.28 / 1.26 | 2.8–2.9 | PRESERVED |
| U 96 @ −0.8 | 96 | 2.56 / 2.22 / 2.63 | 1.59 / 1.61 / 1.54 | — | lost |
| M 96 mixed | 96 | 2.64 / 2.31 / 2.75 | 1.73 / 1.78 / 1.68 | — | lost |
| U 64 @ −0.9 | 64 | 2.55 / 2.37 / 2.69 | 2.39 / 2.40 / 2.36 | — | lost |
| U 48 @ −0.9 | 48 | 1.95 / 1.71 / 2.06 | 2.65 / 2.58 / 2.68 | 1.7–1.9 | lost |
| S3 48 @ −0.6 | 48 | 1.71 / 1.24 / 1.48 | 2.72 / 2.66 / 2.70 | 1.7 | lost |
| M 48 mixed | 48 | 1.66 / 1.18 / 1.32 | 2.83 / 2.69 / 2.87 | 1.7–1.8 | lost |
| U 32 @ −0.7 | 32 | 1.58 / 0.91 / 1.28 | 3.67 / 3.22 / 3.79 | — | lost |

The full grid is in the test output. Distance from Hall falls smoothly with
section count; there is no knee to exploit.

**Through Hall's search** (five seeds per point, medians):

| chain | medium | small | large | lowest run | |
|---|---|---|---|---|---|
| U 48 @ −0.9 | 1.62 | 1.22 | 1.49 | 0.95 | REJECTED |
| S3 48 @ −0.6 | 1.38 | 1.02 | 1.35 | 0.89 | REJECTED |
| M 48 mixed | 1.26 | 0.94 | 1.13 | 0.65 | REJECTED |
| S2 48 @ −0.7 *(final run's shortlist)* | 1.41 | 0.95 | 1.19 | 0.86 | REJECTED |
| S2 120 @ −0.6 | 2.29 | 1.81 | 2.21 | 1.66 | not reference-equivalent |
| S2 120 @ −0.7 | 2.68 | 2.43 | 2.71 | 2.18 | not reference-equivalent |
| U 128 @ −0.8 | 2.60 | 2.22 | 2.45 | 2.15 | not reference-equivalent |
| *Plate A, Stage 4, ten seeds* | *3.17* | *2.81* | *3.12* | *2.54* | *NOT REFUTED* |

- Under 1.75 × Hall the three closest chains are simply Hall.
- The three cheapest chains that *look* like Plate A at the same knobs (about
  3 × Hall) are not reference-equivalent once Hall searches: 2.2–2.7 against
  the reference's 2.8–3.2. The reference sits *on* the threshold, not above
  it, so there is nothing to give away.

Decay (within 1.3 %), tone (0.00 dB) and every stereo and mono check are
Hall's for all of them. Cost is the only thing wrong with this mechanism, and
it is decisive.

**A measurement of mine was wrong on the first run** and is recorded in
addendum 1: the cost column was measured minutes apart from Hall's and put
several chains *below* Hall. Cost is now a ratio taken alternately with Hall.
It still moves between runs (the reference read 2.97 × in the grid and 4.62 ×
twenty minutes later), so the ordering is trustworthy and the absolute ratio
is "about three to four and a half".

**The shortlist itself is not deterministic, and the final run shows it.** The
rule "the three chains closest to Plate A under 1.75 × Hall" depends on the
measured cost. The part run shortlisted U 48 @ −0.9, S3 48 @ −0.6 and M 48
mixed; the final validation run measured S2 48 @ −0.7 at 1.71 × and
shortlisted it in place of M 48 mixed. The search residuals of the chains
common to both runs are identical to the last digit. All four 48-section
chains are REJECTED, so the conclusion does not depend on which three were
picked. The reference read 4.92 × in the final run's grid and 3.82 × in its
G3 table: "three to five" is the honest range.

### 7. Recommended Plate candidate

**None.** No reduction is recommended. Plate A, 240 sections, stays the sonic
reference, with its status unchanged: NOT REFUTED, threshold-sensitive,
3–4.6 × Hall.

What this rules out for good: getting to ≤ 1.75 × by removing, re-tuning,
re-spacing or stretching sections of a scalar allpass chain ahead of the
tank. What it leaves open is engineering that was not built here (section 31).

---

## Part B — Room

### 8. Hypotheses tested

All on a centred source, Early/Late at 0, Hall free to raise Early/Late
against them. All use one construction: the eight lines partitioned into
blocks, each mixing internally, joined by a rotation, the matrix orthogonal
throughout.

| | hypothesis |
|---|---|
| R1a, R1b | a small fast block that is driven and is the only thing heard, losing energy to a larger field |
| R1c | the same, heard together with the field |
| R2 | three regions of rising size |
| R3 | four un-mixed short loops ("wall pairs") leaking into a diffuse field |
| R4a–c (addendum 2) | R1's block with the exchange timed from Decay, RT60 calibrated |

### 9. Rejected

| | medium | small | large | lowest run | |
|---|---|---|---|---|---|
| R1a | 3.08 | 5.11 | 1.68 | 1.58 | REJECTED |
| R1b | 1.81 | 4.73 | 1.34 | 1.34 | REJECTED at G1 |
| R1c | 1.30 | 2.26 | 1.17 | 1.17 | REJECTED at G1 |
| R2 | 2.65 | 5.66 | 1.32 | 1.32 | REJECTED at G1 |
| R3 | 2.50 | 3.83 | 2.75 | 2.42 | REJECTED (median 2.498) |
| R4b | 1.68 | 2.49 | 1.59 | 1.14 | REJECTED |

R1c is the instructive one: the local block heard *together with* the field
is reproduced at once. Hall's early layer does that. The identity is in
hearing the block alone.

### 10–12. Best Room candidate, centred source, Hall's search

**R4c** — the four shortest lines × 0.25 as a block that is driven and heard;
the exchange with the other four set by sin²θ = (block's mean loop time) /
(0.12 × Decay).

| | medium | small | large | lowest run | G2 |
|---|---|---|---|---|---|
| **R4c** | 5.69 | 6.38 | 4.06 | 3.66 | **SURVIVES** |
| R4a | 3.49 | 4.76 | 2.60 | 2.42 | NOT REFUTED |

In every one of R4c's fifteen runs Hall drove Size to 0 and still stopped
3.7 or more away, on modal structure, echo density or recurrence. This is the
first Phase 12 candidate that is DISTINCT at every point on a centred source.

On centred musical sources at the same knobs, the 20 ms level envelope differs
from Hall's by 1.6 dB (vocal), 2.9 (snare), 3.0 (piano), 3.0 (percussion);
Hall against a sibling of itself shows 1.0 / 0.3 / 1.8 / 0.3.

**It fails G3:**

| | R4c | Hall | limit |
|---|---|---|---|
| Size → time to 10 % energy, Size 10 … 90 | 28.2, 28.0, 28.5, 29.0, 30.4 ms | 36 … 65 ms | must not fall |
| channel balance | 1.3 dB | 0.4 | 1 |
| mirror symmetry | −1.3 dB | 0.3 | 1 |
| worst mono tone | −23.3 dB | −15.0 | 3 dB below Hall |
| RT60 / Hall | 1.000 … 1.006 (calibrated) | | 10 % |
| octave bands | 0.77 dB | | 1 |
| CPU | 1.01 × | | |

And the quality flags: modal prominence 42 dB against Hall's 14 (**ringing**),
repetition 0.37 against 0.19 at long lags. The ringing was predicted in
addendum 2 — it is what hearing four short lines sounds like — and I have no
evidence it is acceptable. A real small room is modal too; whether this is
that, or a metallic artefact, is for a listener.

**Room has no survivor.** It has a mechanism with three named defects.

---

## Part C — Chamber

### 13–16. Seven hypotheses, seven rejected

| | hypothesis | medium | small | large | |
|---|---|---|---|---|---|
| C1a | two un-mixed reflection paths in strong exchange with a dense field | 1.25 | 1.14 | 2.62 | REJECTED at G1 |
| C1b | four paths in a ring | 2.03 | 1.38 | 2.60 | REJECTED at G1 |
| C2a | a lossy heard block coupled to a long-lived one (double slope) | 1.15 | 2.17 | 1.88 | REJECTED at G1 |
| C2b | the same, stronger | 1.46 | 1.93 | 1.98 | REJECTED at G1 |
| C3a | C2 with the exchange timed from Decay (addendum 3) | 1.78 | 1.29 | 1.99 | REJECTED at G1 |
| C3b | lossier | 1.51 | 1.65 | 2.12 | REJECTED (lowest run 1.46) |
| C3c | full-size block | 1.97 | 1.41 | 2.65 | REJECTED at G1 |

- Reflections inside the recursion (C1) is the third reflection-into-tank form
  to be reproduced, after Stage 3's feed-forward pattern and multi-tap readout.
- The double slope does not help even when timed. Hall's Decay knob absorbs
  most of it once the RT60 is calibrated to match.

**There is no best Chamber candidate** and so no cross-Mode search was run:
the adversarial gate is for something that has passed Hall first.

Counting Stage 3, eight Chamber constructions have now failed. What they have
in common is that each asks for *more interaction* between reflections and
field; the metric and Hall's knobs treat that as a matter of degree. I do not
have a ninth idea of a different kind, and I would rather say so than produce
one.

---

## Part D — Ambient

### 17–20. Hypotheses, rejections, best candidate

**New mechanism:** energy migrates through several stages before it is heard,
and the per-pass transfer of each stage is set from a time (k × Decay split
over the stages), so the swell does not shrink with Size. `decayScale` is
fitted per setting.

| | stages | k | medium | small | large | lowest run | G2 |
|---|---|---|---|---|---|---|---|
| **A1b** | 2 → 2 → 2 → 2 lines | 0.40 | 9.77 | 4.00 | 15.33 | 3.56 | **SURVIVES** |
| **A3** | 2 → 2 → 4 lines | 0.30 | 7.76 | 4.12 | 35.93 | 3.88 | **SURVIVES** |
| A1a | 2 → 2 → 2 → 2 | 0.25 | 7.77 | 2.90 | 16.73 | 2.47 | NOT REFUTED |

Nothing was rejected at G2. Both defects of Ambient A–E are gone: small /
short is now 4.0 where it was 1.2–2.0, and Decay is within 0.1 % of Hall's
where it drifted 25 %.

**It fails G3 on tone:**

| | A1b | A3 | Hall | limit |
|---|---|---|---|---|
| RT60 / Hall | 1.000 … 1.001 | 1.000 … 1.001 | | **5 %** |
| Size, time to 10 % | 111 … 182 ms, monotone | 95 … 150 ms, monotone | 36 … 65 | |
| **octave bands** | **1.82 dB** | **1.36 dB** | | 1 |
| late correlation | −0.12 | −0.27 | −0.03 | |
| mono fold-down | −3.9 dB | **−5.5 dB** | −3.0 | 1.5 below Hall |
| worst mono tone | −17.2 dB | −13.2 dB | −15.0 | |
| CPU | 1.27 × | 0.92 × | | |

A1b fails on tone only. A3 fails on tone and on mono fold-down. A1b also
carries flags: 24-band colour 2.7 dB, modal prominence 21 against 14,
short-lag repetition 0.49 against 0.32.

The tonal shift is a consequence of the mechanism, not a voicing: energy
spends longer in the damped loops before it is heard. Phase 12 does not own
tone, so this is a defect to remove, not an identity to keep.

**The calibration that was fitted** (`decayScale`; Size 25 / 60 / 85 by Decay
0.8 / 2.5 / 5.0 s):

| | | | | | | | | | |
|---|---|---|---|---|---|---|---|---|---|
| A1b | 0.930 | 0.880 | 0.856 | 0.959 | 0.880 | 0.835 | 0.952 | 0.920 | 0.899 |
| A3 | 0.922 | 0.926 | 0.889 | 0.944 | 0.952 | 0.916 | 0.915 | 0.937 | 0.949 |

It was fitted point by point. A production law would have to reproduce this
surface from Size and Decay; none has been written.

One figure needs a caveat. At large / long one seed in five returned 270–294
for all three candidates where the others returned 15–36. That is the search
failing badly from one start, not the structure changing; medians are
unaffected.

---

## Across the candidates

### 21. Size

| | time to 10 % of the energy, Size 10 / 30 / 50 / 70 / 90, ms | |
|---|---|---|
| Hall | 36.2, 39.6, 44.9, 52.8, 64.9 | |
| Plate A | 40.9, 44.0, 49.1, 57.7, 70.2 | monotone |
| R4c | 28.2, 28.0, 28.5, 29.0, 30.4 | **not monotone, and nearly flat** |
| A1b | 110.7, 124.8, 144.8, 156.6, 181.6 | monotone |
| A3 | 94.9, 100.3, 111.9, 126.3, 150.4 | monotone |

### 22. Decay

RT60 against Hall over nine settings: Plate A 1.004–1.024 with no
calibration; R4c 1.000–1.006, A1b and A3 1.000–1.001, each with an explicit
per-setting `decayScale`. The calibration is real work that production would
have to carry; it is not hidden.

### 23. Stereo and mono

Plate A and every reduced chain: identical to Hall. R4c: worse (balance,
mirror, mono tone). A1b: within limits (fold-down −3.9 against −3.0; late
correlation −0.12). A3: worse (fold-down −5.5).

### 24. Tone

Plate: 0.00 dB. R4c: 0.77 dB. **A1b 1.82, A3 1.36 dB — over the limit.**

### 25. CPU and memory

The block mechanisms cost nothing: the same eight lines with a different
static matrix and different vectors (R4c 1.01 ×, A3 0.92 ×, A1b 1.27 × in one
noisy reading; no added state). Plate A: 3–4.6 ×, 3.75 KiB.

### 26. Mode switching

Not re-measured. Stage 3 showed that a 400 ms primed crossfade between
networks of different topology stays within 0.5 dB, and that nothing can be
carried across. Two things are new and make it harder:

- R4c, A1b and A3 **change their own matrix with Decay** (the timing rule).
  Moving Decay inside such a Mode is itself a topology change and would need
  smoothing of the angle or a crossfade of its own.
- Ambient's swell is 100–180 ms to a tenth of its energy, so a 400 ms prime
  may be too short for it. Not measured.

### 27. Phase 13

Plate is spectrally flat and would sit under any Character voicing. R4c's
0.77 dB is inside the limit. **Ambient's 1.4–1.8 dB is not**, and until it is
removed a Character layer would be voicing on top of an unintended tilt.

### 28. Fast and heavy suites

Implemented.

| | |
|---|---|
| `AuroraVerbTests.exe` | everything, as before. **The gate.** |
| `AuroraVerbTests.exe --fast` | everything except the heavy suites |

Heavy: `runPhase12ModeFamilyTests` (Stage 2), `runPhase12StructuralTests`
(Stage 3), `runPhase12Stage4Tests`, `runPhase12Stage5Tests`. A fast run prints
a banner first, lists every suite it skipped, and ends on "This result is not
a full gate." It combines with substring filters. Heavy suites are
deterministic and are required for final Mode acceptance, any major topology
change, Phase 12 closure and release validation.

The fast run includes 12.5.0, the rig fingerprint check, which is what makes
it safe to skip Stages 3 and 4 after an edit to the research rig.

`AV_S5_PART=A|B|C|D` runs one part of Stage 5.

### 29. Five-Mode status

| | G2 (structure) | G3 (semantics, safety, cost) | counts as a survivor |
|---|---|---|---|
| Hall | — | — | **yes** |
| Plate A | NOT REFUTED | fails CPU | no |
| Room R4c | SURVIVES | fails Size, stereo / mono | no |
| Chamber | nothing passed G1 | — | no |
| Ambient A1b | SURVIVES | fails tone | no |

Five Modes are the product target. The evidence today supports one, with two
more that have a real structural core and specific defects.

### 30. Unresolved blockers

1. **Chamber has no mechanism**, and I have no further idea of a different
   kind. This is the only blocker that is not engineering.
2. **Plate's cost.** Ruled out within scalar allpass chains.
3. **Plate's own status** is NOT REFUTED, not DISTINCT. The Stage 4 listening
   pack was prepared to settle exactly that; **no listening result has been
   reported to me**, and nothing in Stage 5 changes it.
4. **Room R4c:** Size semantics; channel balance and mono tone; ringing.
5. **Ambient A1b:** tonal shift; the calibration surface has no law.
6. **Room and Ambient are one construction** with the energy flowing in
   opposite directions (heard where it is driven; heard at the far end). They
   were not searched against each other. Until they are, "two families" is
   my description and not a result.
7. **None of this says a mechanism sounds like its name.** The gates test
   whether Hall can imitate it, not whether R4c is a room.
8. The timing rule makes the matrix a function of Decay: see section 26.

### 31. Recommended Stage 6

In this order, each finished before the next:

1. **Listen first.** A short blind pack of R4c and A1b against Hall on centred
   sources, with the Stage 4 Plate files. If R4c's ringing is heard as
   metallic, or A1b is heard as an effect, the engineering below is not worth
   starting.
2. **Room R4c, three defects.** Balanced readout vectors for a four-line
   block; a Size law that actually moves the block (its delay scale is fixed
   at 0.25 here); more lines heard, to reduce ringing without losing the
   block's isolation. Each one alone, then G2 again, because any of them may
   cost the distinctness.
3. **Ambient A1b, tone.** A flat correction derived from the migration time,
   then G3 again; then a law for the calibration surface.
4. **Room against Ambient**, each with its own seven controls.
5. **Plate cost,** by routes not open to this stage: the chain run at a
   quarter of the rate on the band below ~5 kHz, where 70 % of its phase
   lives (a real saving, unlike stretching, because the images are filtered
   away); or the anti-diagonal SIMD restructure of Stage 4. Either must be
   measured against the reference by the same two scores.
6. **Chamber:** do not start a ninth hypothesis without a new idea. If Stage 6
   produces a Room and an Ambient, ask then whether a structure *between*
   them in the block construction is a Chamber or a preset. I expect the
   adversarial gate to say preset, and would want that tested, not assumed.

### 32. Research tests added

- `Tests/Phase12Stage5.h`, `Phase12Stage5.cpp` — the block construction,
  gates G1–G3, level and decay calibration, cost ratio.
- `Tests/Phase12Stage5Parts.cpp` — sections 12.5.1 to 12.5.7.
- `Tests/Phase12Stage5Fast.cpp` — section 12.5.0, the rig fingerprints.
- `Tests/Phase12Stage3Rig.h` — five options, inert at their defaults: a
  custom matrix, custom injection and readout vectors, per-line decay, a
  stretched dispersion chain, a decay calibration.
- `Tests/RunAll.cpp` — `--fast` and `AV_RUN_HEAVY`.

### 33. Production source

Unchanged. No Mode parameter, no topology, no baseline.

### 34. Regression

Final validation, run once on the committed code, nothing else running:

| run | result |
|---|---|
| DSP, `--fast` | **2574 / 2592**, 18 failures — the 18 preserved ones (Phase 7 decay-law checks and the Phase 11 documented findings), unchanged in number and identity. **No new regression.** |
| Stage 5 heavy suite, `runPhase12Stage5Tests`, all four parts in one run | 2 / 11, **9 FINDING failures**, all of them the results of this report: no chain within 1.75 × preserves the reference; no reduced Plate is reference-equivalent; Room, Chamber and Ambient each have no hypothesis through every gate; and the four status checks of 12.5.9 |
| State, `AuroraVerbStateTests` | **26 / 26** |
| `git diff -- Source` | empty |

**What was not re-run, stated plainly.** The fast run skips the four heavy
suites and says so in its last lines. Of those, Stage 5 was then run in full.
The Stage 2, Stage 3 and Stage 4 heavy suites (`runPhase12ModeFamilyTests`,
`runPhase12StructuralTests`, `runPhase12Stage4Tests`) were **not** re-run
after the rig edit. Their last full results stand as recorded in their own
reports (14 + 8 + 2 FINDING failures). The evidence that the rig edit did not
change them is section 12.5.0: seventeen impulse-response fingerprints of the
structures those suites use, recorded before the edit, all still equal. That
is strong evidence and not the same thing as re-running them.

**Reproducibility.** Every search residual in the single full run equals the
one from the separate part runs that this report was written from (Room,
Chamber and Ambient tables to the last printed digit). The only thing that
moved is cost, and with it the Plate shortlist; see section 6.

### 35. Commit

Stage 5 is commit `77f32fe`; this line was added by the commit after it.
