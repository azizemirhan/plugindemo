# Phase 12 Stage 3 — pre-registration of the augmented metric

**Written and committed before any structural lever or candidate was scored.**
The candidate file (`Tests/Phase12StructuralCandidates.cpp`) is an empty
function in this commit.

Research only. `Source/` is unchanged.

## What is frozen by this commit

`Tests/Phase12Stage3Metric.h`: the thirteen families, every scale, the
per-family floor rule, the worst-family verdict, and the thresholds.

| | |
|---|---|
| REPRODUCED | worst family ≤ **1.5** |
| DISTINCT | worst family ≥ **3.0** |
| WEAK | in between |

The thresholds are Stage 2's, unchanged. What changed is what is measured and
how it is added up.

| # | family | new? | what it asks |
|---|---|---|---|
| 0–5 | buildup, energy profile, echo density, stereo, decay, recurrence | Stage 2, unchanged | — |
| 6 | late texture | new | does the late field's short-time energy fluctuate more than noise does? |
| 7 | loop repetition | new | does the tail repeat itself, at loop lags (3–150 ms) or long lags (150–1000 ms)? |
| 8 | modal structure | new | do single modes stand out? |
| 9 | decay shape | new | is the decay one exponential? (EDT and late slope against the mid slope) |
| 10 | band arrival | new | do all frequencies arrive together? |
| 11 | lateral spread | new | where does a hard-left source's reverb go, and how fast does it cross? |
| 12 | partial placement | new | is each partial placed between the channels, or hard centre / hard out of phase? |

**Scoring.** A family is the RMS of its features in JND or proxy units, divided
by `max (1, that family's own same-family floor)`. **The verdict is the worst
family, not an average.** The impostor search minimises that same number.

## Why Stage 2's metric failed — the short version

1. It did not look. Nothing in six families read the fine structure of the late
   field, the shape of the decay, when each frequency arrives, or how a panned
   source's reverb is placed.
2. It averaged. A structural difference lives in one family; RMS over families
   dilutes it.

The full analysis goes in the Stage 3 report.

## What was tried on the controls and changed, before the freeze

Everything below was done **on controls only**. Recorded so the freeze is not
mistaken for a first attempt.

| attempt | what happened | what changed |
|---|---|---|
| late echo density at 80/160/320 ms | every network, including one with no mixing matrix, is at Gaussian density by 80 ms | removed; it could only dilute |
| band arrival as an energy centroid | a 19 ms dispersion moved it < 3 ms | replaced by time-to-5 % per band |
| loop repetition to 150 ms only | a ring network's return (0.42 autocorrelation at ~300 ms) was outside the window | long-lag feature added, 150–1000 ms |
| one "fine structure" family of three features | RMS inside the family diluted the one feature that moved | split into loop repetition and modal structure |
| eleven families | **the tank with no mixing matrix scored 1.3–1.8: still reachable by Hall** | partial placement added (below) |
| search to a 0.02 step, two refinements | could not recover Hall's own moved knobs (2.15, 1.62); 0.02 on the Decay axis is 11 %, two JNDs wide | step to 0.005, three refinements, oblique moves, half the starts seeded at the target's measured RT60 |
| synthetic dispersion of 160 sections (4 kHz leads 1 kHz by 10 ms) | 2.64, WEAK | control restated at 240 sections (16 ms), plate magnitude; 2.64 is recorded as this family's resolution |

**The no-mixing-matrix control deserves its own line.** Stage 2 asserted it was
"a different family by any definition" and blamed the metric when it scored
0.25. With eleven families and worst-family scoring it *still* scored only
1.3–1.8. Its echo density, envelope, decay, band arrival and broadband stereo
really are close to Hall's — the input diffuser and the in-loop allpasses see
to that. What is different is one thing, and it is large: with no mixing every
mode lives in one delay line, and the readout sign vectors then place it either
dead centre or dead out of phase.

| | spread of mid/side across partials | share of partials > 15 dB one way |
|---|---|---|
| Hall | 15.5 dB | 3.3 % |
| Hall, sibling delay set | 16.1 dB | 3.2 % |
| ring matrix | 17.9 dB | 4.7 % |
| **no mixing matrix** | **60.8 dB** | **51 %** |

That is the mechanism behind Phase 11's narrow-band mono finding, at full
strength. It is a real, audible property (mono fold-down, narrow-band sources)
and no user control touches it: Width shifts every partial by the same amount
and leaves the spread alone.

## Control results at the freeze

Same-family floor, per family (Hall against two sibling delay sets and against
itself with Size ± 2, at three operating points):

| build | profile | density | stereo | decay | recur | texture | repeat | modal | shape | arrival | lateral | placement |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1.25 | 1.83 | 1.25 | 0.41 | 0.13 | 0.72 | 0.75 | 0.78 | 0.89 | 0.18 | 0.73 | 0.53 | 0.23 |

Level invariance: the same response 11.4 dB louder scores 5 × 10⁻¹².

**Negative controls** — same topology; shipping Hall searched to imitate each.

| target | residual, worst over points | verdict |
|---|---|---|
| Hall with its knobs moved, 4 settings, search started at the defaults | 1.00, 1.11, 0.62, 0.90 | REPRODUCED |
| sibling delay set | 0.73 | REPRODUCED |
| delay set × 0.5 | 0.80 | REPRODUCED |
| modulation × 1.4 | 0.00 | REPRODUCED |
| allpass sections 2 | 0.69 | REPRODUCED |
| Stage 2 Room candidate | 1.12 | REPRODUCED |

**Positive controls** — different topology; shipping Hall searched to imitate
each.

| target | medium / medium | small / short | large / long |
|---|---|---|---|
| no mixing matrix | 10.95 | 9.45 | 7.13 |
| ring matrix (one loop) | 3.79 | 3.74 | **2.90** |
| two uncoupled four-line tanks | 11.91 | 12.18 | 11.72 |
| synthetic: dispersed (240 sections) | 3.35 | | |
| synthetic: double-slope decay | 6.99 | | |
| synthetic: slow left-to-right crossing | 4.30 | | |

Strongest negative **1.12**. Weakest positive **2.90**.

**One positive trial of twelve is not DISTINCT**: the ring matrix at large /
long, 2.90 against a threshold of 3.00. It is not REPRODUCED and the threshold
is not moved to rescue it. It is kept as a failing check and as a stated
property of this metric:

> A genuinely different topology can score as low as 2.9 at one operating
> point. A candidate that is DISTINCT at two points and between 2.5 and 3.0 at
> the third will be reported as *not refuted at that point*, not as rejected —
> and not as proven either.

## What is fixed for the candidates

- **Impostor:** shipping Hall, over seven user controls (Size, Decay,
  Early/Late, Early Diffusion, Attack, Mod Depth, Width). Survivors are then
  also searched against each other.
- **Targets:** each structure at three operating points — Size 60 / Decay
  2.5 s, Size 25 / 0.8 s, Size 85 / 5.0 s — with Early/Late at 0, so nothing
  depends on the early layer.
- **Level:** each structure's late path is scaled so its integrated wet energy
  at the product defaults equals Hall's. Every feature is level-invariant in
  any case.
- **Survives objectively:** DISTINCT at all three points. **Rejected:**
  REPRODUCED at any point. Anything else is WEAK and is reported as WEAK.

## The isolated levers, fixed in advance

Each is shipping Hall with **one** thing changed. Parameters are written here
before any is scored, so the list cannot be pruned to the ones that worked.

| brief item | lever | parameters |
|---|---|---|
| A matrix | Householder | — |
| A | random dense orthogonal | seed 1 |
| A | Hadamard, rows signed and columns permuted | fixed permutation |
| A | sparse chain | 2 non-zeros per row |
| A/F | coupled blocks, shipping injection and readout | angle 0.20 rad |
| B stage count | 1, 2, 8 stages | shipping pattern |
| C topology | no input diffuser | — |
| D ratios | clustered (1.8 : 1), wide (2.7 : 1 progression) | 4 stages |
| H feed-forward | velvet FIR diffuser | 16 taps, 30 ms |
| E injection | one line per channel | — |
| E | staggered, each line through its own delay | 30 ms span |
| F connectivity | split channels, coupled blocks | angle 0.15 rad |
| F | inject block A, read block B | angle 0.15 rad |
| F, controlled loss | the same, block B decaying faster | ratio 0.35 |
| G feedback path | static in-loop allpasses | 1 and 2 per line, g 0.5 |
| G | in-loop dispersion | 4 and 12 first-order sections, a −0.7 |
| I ER/late | multi-tap readout inside the lines | 2 taps per line |
| I | early pattern feeds the tank | shipping pattern |
| J | dispersion ahead of the tank | 64 and 240 sections, a −0.7 |

Item C of the brief also names diffuser *order*. Moving the series diffuser
from before the tank to after it is not on the list because for a linear
time-invariant chain it is the identity, and the only time-varying part
(modulation) sits inside the tank either way. That is stated rather than
measured.

Composite candidates for Room, Plate, Chamber and Ambient are built **only**
from levers that show leverage alone, and are scored by the same rule.
