# Phase 12 Stage 3 — structural Mode dimension research

Research only. `Source/` is unchanged. No production Mode system, no Phase 13.

Metric pre-registered at `c18ecce` (`Docs/Phase12-Stage3-Preregistration.md`)
before any lever or candidate was scored. `Tests/Phase12Stage3Metric.h` has not
changed since that commit.

## The result in one paragraph

Twenty-five isolated structural levers and twelve composite candidates were
scored against shipping Hall searching over seven user controls. **No candidate
meets all ten acceptance points. Five Modes are not justified.** Two structural
dimensions are real and knob-irreducible, and each has a serious limit:

- **Channel-split coupled blocks** ("Room" rows). Strongly DISTINCT (4.2–5.4),
  free in CPU, tonally neutral, Decay intact. But the whole identity is how a
  *panned* source's reverb is placed. For a centred or mono source Hall
  reproduces it (0.56–0.78).
- **Dispersion ahead of the tank** ("Plate A"). Holds for centred sources, tone
  and stereo identical to Hall. But marginal (2.58–3.35 depending on the search
  run) and 2.8–4.3 × the wet-path CPU in the unoptimised rig.

Chamber is not justified. Ambient's apparent distinctness was largely a stereo
defect in my own candidate; with the defect removed it is WEAK or REPRODUCED.

## 1. Why Stage 2's metric failed

1. **It did not look.** None of its six families read the late field's fine
   structure, the decay's shape, per-band arrival time, or where a panned
   source's reverb goes.
2. **It averaged.** A structural difference lives in one family; RMS across
   families dilutes it below threshold.
3. **A Stage 2 claim of mine was wrong.** Stage 2 called the tank with no
   mixing matrix "a different family by any definition" and blamed the metric
   alone. With eleven new-style families it *still* scored 1.3–1.8. Its
   density, envelope, decay and broadband stereo really are close to Hall's.
   The one large difference is partial placement (spread 60.8 dB against
   15.5 dB; 51 % of partials hard-placed against 3.3 %).

## 2–3. The augmented metric and its freeze

Thirteen families: Stage 2's six, plus late texture, loop repetition, modal
structure, decay shape, band arrival, lateral spread, partial placement. Each
family is divided by `max (1, its own same-family floor)`. **The verdict is the
worst family.** REPRODUCED ≤ 1.5, DISTINCT ≥ 3.0, WEAK between — Stage 2's
thresholds, unchanged. Frozen at `c18ecce`; the seven things tried and changed
on controls before the freeze are listed in the pre-registration.

Changed after the freeze: the **rig** (a `genericVectors` option, section 17),
the candidate file, and how checks are worded. Not the metric, scales,
thresholds or search.

## 4. Negative controls

| target | residual after Hall's search | |
|---|---|---|
| Hall with its knobs moved (4 settings) | 1.00, 1.11, 0.62, 0.90 | REPRODUCED |
| sibling delay set | 0.73 | REPRODUCED |
| delay set × 0.5 | 0.80 | REPRODUCED |
| modulation × 1.4 | 0.00 | REPRODUCED |
| allpass sections 2 | 0.69 | REPRODUCED |
| Stage 2 Room candidate | 1.12 | REPRODUCED |

Strongest negative: **1.12**.

## 5. Positive controls

| target | medium / medium | small / short | large / long |
|---|---|---|---|
| no mixing matrix | 10.95 | 9.45 | 7.13 |
| ring matrix | 3.79 | 3.74 | **2.90** |
| two uncoupled four-line tanks | 11.91 | 12.18 | 11.72 |
| synthetic: dispersed, 240 sections | 3.35 | | |
| synthetic: double-slope decay | 6.99 | | |
| synthetic: slow left-to-right crossing | 4.30 | | |

Weakest positive: **2.90**, 2.6 × the strongest negative. One trial of twelve
is not DISTINCT (ring, large / long). The threshold was not moved; the check is
kept failing. Consequence, stated before candidates ran: a real topology
difference can score 2.9, so 2.5–3.0 at one point means *not refuted*, neither
rejected nor proven.

## 6. Topology-sensitive measurements added

| family | reads | scale |
|---|---|---|
| late texture | 5 ms energy fluctuation, 50–200 and 200–500 ms | 0.35 dB |
| loop repetition | peak autocorrelation of the flattened tail, 3–150 ms and 150–1000 ms | 0.05 |
| modal structure | lag RMS, modal prominence | 1.5 dB |
| decay shape | EDT and late slope against mid slope | 0.10 |
| band arrival | time to 5 % of band energy at 250 Hz / 4 k / 8 k minus 1 k | 4 / 3 / 3 ms |
| lateral spread | R/L for a left-only impulse, three windows | 1.5 dB |
| partial placement | spread of per-bin mid/side; share beyond 15 dB | 3 dB, 0.05 |

## 7–10. Isolated levers: matrix and diffuser

Shipping Hall with one change. Three figures are the three operating points
after Hall's search; the verdict uses the weakest.

| item | lever | medium | small | large | verdict |
|---|---|---|---|---|---|
| A | Householder | 3.96 | 3.91 | 2.69 | WEAK (placement) |
| A | random dense orthogonal | 1.45 | 1.39 | 1.54 | REPRODUCED |
| A | Hadamard permuted | 1.33 | 1.13 | 1.18 | REPRODUCED |
| A | sparse chain | 1.11 | 1.35 | 1.42 | REPRODUCED |
| A/F | coupled blocks, shipping in/out | 0.70 | 0.69 | 1.06 | REPRODUCED |
| B | 1 stage | 1.95 | 2.35 | 4.80 | WEAK |
| B | 2 stages | 0.88 | 1.25 | 1.57 | REPRODUCED |
| B | 8 stages | 1.93 | 1.62 | 2.05 | WEAK |
| C | no diffuser | 2.12 | 2.56 | 13.42 | WEAK |
| D | clustered ratios | 0.54 | 0.55 | 1.31 | REPRODUCED |
| D | wide ratios | 0.74 | 0.57 | 0.81 | REPRODUCED |
| H | velvet FIR | 1.84 | 1.27 | 0.87 | REPRODUCED |
| E | one line per channel | 1.49 | 0.98 | 2.40 | REPRODUCED |
| E | staggered injection | 1.69 | 0.99 | 2.83 | REPRODUCED |
| **F** | **split channels, coupled blocks** | 6.03 | 5.11 | 7.75 | **DISTINCT** |
| **F** | **inject block A, read block B** | 12.57 | 3.13 | 62.70 | **DISTINCT** (see 17) |
| **F** | **the same, block B lossy** | 13.34 | 4.84 | 295.76 | **DISTINCT** (see 17) |
| G | loop allpass × 1 | 0.70 | 0.76 | 1.05 | REPRODUCED |
| G | loop allpass × 2 | 1.20 | 0.84 | 1.37 | REPRODUCED |
| G | loop dispersion 4 | 1.36 | 0.71 | 1.45 | REPRODUCED |
| G | loop dispersion 12 | 1.59 | 0.62 | 2.19 | REPRODUCED |
| I | multi-tap readout | 0.80 | 1.08 | 1.42 | REPRODUCED |
| I | early pattern feeds the tank | 0.72 | 0.56 | 0.98 | REPRODUCED |
| J | pre-dispersion 64 | 1.09 | 0.61 | 0.94 | REPRODUCED |
| J | pre-dispersion 240 | 3.41 | 2.58 | 3.12 | WEAK, not refuted |

3 DISTINCT, 5 WEAK, 17 REPRODUCED.

**Matrix family (A).** Changing which dense orthogonal matrix mixes eight
lines does not make a Mode. Householder is the only one with any residue, and
it is partial placement at one point. What matters is not the matrix's entries
but whether it *partitions* the network (F).

**Diffuser (B, C, D, H).** Ratios and feed-forward diffusion are reproduced
outright. Stage count has weak leverage, mostly at one operating point; Early
Diffusion and Attack take most of it away.

**In-loop mechanisms (G) and readout (I)** are reproduced.

## 11–12. Room

Lever: two four-line blocks, left input into one and right into the other,
coupled by a small rotation.

| candidate | any source | centred source |
|---|---|---|
| Room A — split, angle 0.15 | 5.42 DISTINCT | **0.78 REPRODUCED** |
| Room B — split, angle 0.30 | 4.24 DISTINCT | **0.56 REPRODUCED** |
| Room C — split 0.15, 1 diffuser stage | 4.75 DISTINCT | 1.65 WEAK |
| Room D — split 0.15, generic vectors | 5.29 DISTINCT | 1.29 REPRODUCED |

A left-only source's reverb stays left: −30 dB on the far side in the first
50 ms, −3.9 to −9.9 dB at 200–600 ms, against Hall's 0.0 and −1.3 dB. No user
control does that; Width cannot. It is also the thing Phase 11 recorded as
missing from Hall ("a one-sided source produces a one-sided tail — it does
not").

**The limit.** Remove the lateral family from the search and verdict — that is,
feed it a centred source — and Hall reproduces Room A, B and D. On a mono send
this Mode *is* Hall. Acceptance point 3 ("cannot be reproduced by normal user
controls") holds only for sources with stereo position.

Room B is the cleanest row: no stereo flag, RT60 within 1 % of Hall, tone
within 0.11 dB, CPU 0.97 ×. Room A and C are 1.3 dB out of left/right mirror
symmetry. Room D (generic vectors) is worse, not better: 3.2 dB out of balance
and RT60 down to 0.887 × Hall. Rejected.

Whether "Room" is the right name for a source-position-preserving reverb is a
listening question, not mine.

## 13–14. Plate

Lever: a cascade of first-order allpass sections ahead of the tank, so high
frequencies arrive before low ones.

| candidate | medium | small | large | any | centred |
|---|---|---|---|---|---|
| Plate A — 240 sections, a −0.7 | 3.35 | 3.13 | 3.19 | DISTINCT | 3.16 DISTINCT |
| Plate B — 120 sections, a −0.85, 8 stages | 2.74 | 2.86 | 2.84 | WEAK | WEAK |

Plate A is the only candidate whose identity survives a centred source. Its
magnitude response, stereo image, mono fold-down and Decay are Hall's (band
deviation 0.00 dB, RT60 1.003–1.032 ×).

**Two limits.**

- *It is marginal.* The same structure is in the lever table, where the search
  found a closer imitation at small / short: **2.58**, not 3.13. The search is
  stochastic and two runs on one target differ by 0.3–0.55 (also visible on
  Room A, 5.11 / 5.42, and Ambient A, 3.13 / 3.51). The lower figure is the
  bound. By the pre-registered rule Plate A is DISTINCT at two points and *not
  refuted* at the third.
- *It is expensive.* 2.82 × Hall's wet path in this run, 4.25 × in an earlier
  one. A scalar, unoptimised cascade, so an upper bound — but it is a serial
  recurrence and does not vectorise along its length. Halving it (Plate B)
  drops the score to WEAK.

## 15–16. Chamber

One candidate: 8 diffuser stages, two in-loop allpasses per line, multi-tap
readout. None of its three levers had leverage alone; it was built anyway so
that Chamber was tested rather than assumed away.

**1.85 / 1.69 / 2.28 — WEAK at every point.** Also the largest tonal shift
(1.07 dB) and a narrowed image (correlation 0.31, Width 100 reads −2.9 dB).

**CHAMBER NOT JUSTIFIED.** No structural dimension was found that places it
anywhere Hall cannot go.

## 17–18. Ambient, and a defect in my candidates

Lever: inject into block A, read only from block B, so every reflection must
cross the coupling before it is heard — a slow swell with no direct onset.

| candidate | medium | small | large | verdict |
|---|---|---|---|---|
| Ambient A — through, 0.15 | 12.32 | 3.51 | 58.58 | DISTINCT |
| Ambient B — through 0.30, 8 stages | 19.87 | 3.09 | 22.85 | DISTINCT |
| Ambient C — through 0.15, block B lossy | 13.99 | 4.81 | 61.45 | DISTINCT |
| Ambient D — through 0.15, generic vectors | 10.74 | **2.02** | 14.07 | WEAK |
| Ambient E — through 0.30, generic vectors | 9.02 | **1.21** | 18.46 | REPRODUCED |

**A, B and C are defective.** Their injection and readout sign vectors were
rows of the block's own 4-point Hadamard matrix, so energy alternates between
them coherently. Result: late L/R correlation −0.56 to −0.63 and a mono
fold-down of −7 to −8 dB against Hall's −3. Part of their DISTINCT verdict was
that defect, and Hall's search answered it by going to Width 200.

Rows D and E replace the vectors with generic orthogonal ones. The stereo
checks then match Hall (correlation −0.03, fold −3.1 dB) — and the distinctness
at small / short goes with it: 2.02 and 1.21.

What remains is real but partial: at medium and large sizes the swell is far
outside Hall's reach (9–18). At small / short it is not. And every through
candidate **redefines Decay** (RT60 up to 1.25 × Hall, section 20).

**AMBIENT NOT PROVEN.** The three original rows and the two lever rows marked
"see 17" are kept in the tables; their DISTINCT verdicts should not be read as
support. I have not diagnosed why partial placement reaches 14 at large / long
in every through candidate, including D and E.

No shimmer, freeze, pitch, granular or delay-effect mechanism was used.

## Candidates against each other

Room A, Plate A and Ambient A, each searched over its own seven controls to
imitate each of the others: all eighteen trials DISTINCT, weakest 3.34. (Run
with the defective Ambient A; not repeated with D.)

## 19. Size

Time to 10 % of the energy rises monotonically with Size in every structure.
The check also uses an early centroid and fails for the five Ambient rows:
that centroid is computed over a fixed 100 ms window, which their swell
outgrows at Size 90. A flaw in my check, left as it ran rather than adjusted
after the fact.

## 20. Decay

Measured RT60 over nine settings as a ratio to Hall's at the same knobs:

| | min .. max | |
|---|---|---|
| Room A / B / C | 0.985 .. 1.010 | intact |
| Plate A | 1.003 .. 1.032 | intact |
| Plate B | 1.001 .. 1.069 | intact |
| Chamber | 0.990 .. 1.047 | intact |
| Room D | 0.887 .. 1.011 | redefined |
| Ambient A / B / D / E | 0.973 .. 1.253 | redefined |
| Ambient C (lossy) | 0.754 .. 1.006 | redefined |

Hall's own RT60 reads 0.80–0.88 of the request with this estimator; the ratios
above are relative to Hall, which is what "Mode must not redefine Decay" asks.

## 21. Stereo and mono (Phase 11 checks, at the defaults)

| | late corr | fold, dB | L/R, dB | cross 0–50 / 200–600, dB | worst tone fold, dB |
|---|---|---|---|---|---|
| Hall | −0.03 | −3.0 | 0.4 | 0.0 / −1.3 | −15.0 |
| Room A | −0.02 | −3.1 | 1.3 | −30 / −8.9 | −12.7 |
| Room B | −0.04 | −3.1 | 0.9 | −30 / −3.9 | −8.5 |
| Room C | −0.03 | −3.0 | 1.3 | −30 / −8.6 | −12.0 |
| Room D | 0.00 | −3.1 | 3.2 | −30 / −9.9 | −14.7 |
| Plate A | −0.03 | −3.0 | 0.4 | 0.1 / −1.3 | −15.0 |
| Plate B | −0.03 | −3.0 | 0.3 | −0.4 / −1.4 | −21.4 |
| Chamber | 0.31 | −1.8 | 0.2 | 0.2 / −0.6 | −16.8 |
| Ambient A / B / C | −0.56 … −0.63 | −6.9 … −8.1 | ≤ 0.4 | ≈ 0 | −14.3 … −21.2 |
| Ambient D / E | −0.03 | −3.1 | 0.4 | −6.6 / −0.2 | −14.2 / −9.8 |

Seven rows are worse than Hall on at least one check. Clean: Room B, Plate A,
Chamber (narrow rather than unsafe), Ambient D, Ambient E. Room B's worst
narrow-band tone folds 6.5 dB *better* than Hall's.

## 22. Tonal neutrality

Largest octave-band deviation from Hall: Room A–C ≤ 0.11 dB; Plate A 0.00;
Plate B 0.26; Room D 0.64; Ambient B 0.52, E 0.68; **Chamber 1.07, Ambient A
and D 1.05, Ambient C 1.01** (over the 1 dB line). Band RT60 within 10 % of
Hall for every lossless structure.

## 23. CPU and memory

Whole wet path, single thread, unoptimised research rig (upper bounds).

| | × Hall | added state |
|---|---|---|
| Room A–D | 0.88–1.01 | none |
| Ambient A–E | 0.92–1.03 | none |
| Chamber | 1.22 | in-loop allpass buffers |
| Plate B | 1.91 | 240 allpass states, 1.9 KiB |
| **Plate A** | **2.82** (4.25 in an earlier run) | 480 allpass states, 3.8 KiB |

The coupled-block matrix is two 4-point butterflies and four rotations: cheaper
than the shipping 8-point butterfly, branch-free, SIMD-friendly.

## 24. Mode switching

Between different topologies no state can be carried over. A network started
from silence, primed on live input for 400 ms, then crossfaded over 120 ms
equal-power stays within 0.12–0.51 dB of where it would have been. Unprimed,
the dip is −3 to −15 dB. So switching needs two networks alive for about half
a second, as Size morphing already does. Not implemented.

## 25. Phase 13 compatibility

Both surviving mechanisms are spectrally flat (≤ 0.11 dB and 0.00 dB) and leave
Decay alone, so a Character layer (MODERN / CLASSIC / VINTAGE) that owns tone
would stack on either without the two fighting. Dispersion does smear
transients; a Character voicing that relies on a sharp onset would interact
with it. Not measured.

## 26–28. Survivors, rejections, and the five-Mode question

| | status |
|---|---|
| Hall | reference, unchanged |
| channel-split coupled blocks (Room B) | objective survivor **for positioned sources only** |
| pre-dispersion (Plate A) | marginal: not refuted, expensive |
| through-coupled blocks (Ambient D) | not proven: WEAK, redefines Decay |
| Chamber | NOT JUSTIFIED |
| dense matrix variants, diffuser ratios, velvet, injection variants, in-loop allpass / dispersion, multi-tap, ER-feeds-tank | rejected: REPRODUCED |
| Room D, Ambient A / B / C / E, Plate B | rejected |

**Five Modes are not justified.** No candidate passes all ten acceptance
points. The honest inventory is Hall plus at most two conditional candidates.

## 29. Worth human listening

Only two, and each to answer one question:

- **Room B against Hall**, with a panned source and with a centred one. Is the
  lateral dimension worth a Mode when it vanishes in mono?
- **Plate A against Hall**, centred source. Is a 2.6–3.4 score audible, and
  worth the CPU?

No pack has been rendered.

## 30. Recommended Stage 4

1. A compact blind pack: Hall, Room B, Plate A × two sources × two operating
   points, about a dozen files.
2. In parallel, decide whether V1 needs five Modes at all. The evidence
   supports one to three.
3. If Plate survives listening: research a cheaper dispersion before anything
   else, since halving this one loses the effect.
4. Ambient needs a different mechanism or should be dropped; Chamber should be
   dropped.

## 31. Research tests added

`Tests/Phase12ResearchCore.h`, `Phase12Stage3Rig.h`, `Phase12Stage3Metric.h`,
`Phase12Structural.h/.cpp`, `Phase12StructuralCandidates.cpp` — sections
12.3.1 to 12.3.12. The Stage 3 suite takes **57 minutes** (3441 s) and now
dominates the regression. `AV_S3_QUICK` and `AV_S3_SKIP_LEVERS` skip the
control and lever batches for local work.

## 32. Production source

`git diff --stat -- Source` is empty. No new parameter, no production baseline.

## 33. Regression

DSP suite: **2609 / 2649**, 40 failures.

- 18 preserved from Phases 8–11, unchanged.
- 14 Stage 2 findings, unchanged.
- 8 new, all Stage 3 findings about research candidates: ring at 2.90; Room
  not distinct for a centred source; Chamber not distinct; Size centroid
  (section 19); Decay redefined (6 rows); stereo worse than Hall (7 rows);
  octave band over 1 dB; a candidate doubles CPU.

No previously passing check fails. State suite: 26 / 26.

## 34. Commit

Results committed at `dfd1a95`.
