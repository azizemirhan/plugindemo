# Phase 12 Stage 8 — AS1, the timed and Decay-calibrated 4 → 4 sibling: result, and a stop

Research only. `Source/` is unchanged. No Mode selector, no Phase 13, no
production integration.

**AS1 does not preserve A1b's identity. By the registered rule the work
stops here: no other topology was built.** Every measurement the
pre-registration listed was still made and is reported below.

Order of record: root-cause report (`bb71457`) → AS1 pre-registration,
approved as written (`f8f261f`) → build → measurements. Nothing in the
topology, the calibration point, the convergence region, the gates or the
prediction was changed after a result was seen. Logs:
`Docs/Phase12-Stage8-runs/07-as1-measurements.txt`, `08-as1-searches.txt`.

## 1. The verdict, and exactly which preservation dimensions failed

Gate 4.1, onset-normalised, same knobs, against frozen A1b:

| | t10 | t50 | t10 / t50 | buildup family | energy-profile family |
|---|---|---|---|---|---|
| limit | ± 10 % | ± 10 % | ± 0.03 | ≤ 1.5 | ≤ 1.5 |
| medium | 77.9 → 58.2 ms, **−25 %** | 202.7 → 184.7, −9 % | 0.384 → 0.315, **−0.069** | 0.76 | 0.57 |
| short | 30.8 → 22.5 ms, **−27 %** | 79.7 → 66.8, **−16 %** | 0.387 → 0.337, **−0.050** | 1.16 | **1.52** |
| long | 155.8 → 107.8 ms, **−31 %** | 404.1 → 366.2, −9 % | 0.386 → 0.294, **−0.091** | 0.91 | 0.47 |

Failed:

- **t10 — at all three points**, by 25 – 31 %. AS1's energy starts
  arriving too soon.
- **the shape number — at all three points**, by 0.05 – 0.09. AS1 rises
  more abruptly in proportion to its own length.
- **t50 — at short** (−16 %).
- **the energy-profile family — at short**, 1.52 against 1.5.

Passed: the buildup family at all three points; t50 and the energy-profile
family at medium and long.

At the six other grid settings (reported, not gating) AS1 is inside all
five lines at one — Size 60, Decay 0.8 — and outside at five.

## 2. The calibration did not reach its target

The one calibration was: the k at which AS1's t50 at the medium point
equals A1b's. **No k in the registered range reaches it.** AS1's t50 at
medium runs from 122.5 ms at k = 0.05 to 184.7 ms at k = 3; A1b's is
202.7 ms. By the registered rule AS1 was built at the nearest, **k = 3**.

- At k = 3 the join exchanges 0.4 – 0.8 % of the energy per pass.
- So one join cannot be slowed into A1b's build: past a point, slowing the
  exchange only lowers the level of what arrives, not when half of it has
  arrived. The time scale of a single exchange is bounded by the decay.
- **A correction.** The pre-registration gave A1b's t50 as 204.9 ms. That
  was the mean over the twelve perturbations; the unperturbed A1b's is
  202.7 ms, and that is what the code used. It changes nothing in the
  result. The mismatch between document and code is mine.

## 3. The prediction

Written before the build: AS1 fails the shape gate, t10 comes out early,
shape near 0.30 – 0.33; variance at 125 Hz falls to about 0.4 – 0.5 dB;
source spread about half of A1b's.

| | predicted | measured | |
|---|---|---|---|
| shape | 0.30 – 0.33 | 0.315 / 0.337 / 0.294 | held |
| t10 | early, below −10 % | −25 / −27 / −31 % | held |
| 125 Hz scatter | 0.4 – 0.5 dB | 0.53 / 0.58 / 0.70 | partly: about halved at medium and long, unchanged at short |
| source spread | about half of A1b's | 2.40 / 2.88 / 1.59 against 2.27 / 2.83 / 0.63 | **wrong**: no improvement; worse at long |

I did not predict that the calibration would run out of range.

## 4. Everything else that was registered

### 4.1 Tone and its variance (gate 4.4) — fails

| | defaults | worst of 27 settings |
|---|---|---|
| A1b | 1.82 dB | 3.02 dB |
| AS1 | **2.05 dB** | **5.01 dB** |

Standard deviation of the octave deviation over delay perturbations:

| | 125 Hz, ± 4 % | 125 Hz, ± 30 % | 250 Hz, ± 30 % | 500 Hz, ± 30 % |
|---|---|---|---|---|
| A1b, medium / short / long | 1.14 / 0.57 / 1.10 | 1.30 / 0.88 / 1.22 | 0.72 / 1.05 / 0.95 | 0.81 / 0.72 / 0.79 |
| AS1 | 0.53 / 0.58 / 0.70 | 0.73 / 1.07 / 0.70 | 0.80 / 1.00 / 0.55 | 0.50 / 0.70 / 0.29 |

- At medium and long AS1's low-frequency scatter is roughly 55 – 60 % of
  A1b's. At short it is not lower.
- The means are much the same as A1b's: mid bands +0.4 to +0.6, 8 kHz
  −0.5 to −0.7 — the loss met while waiting, as before.
- The tone gate is failed by more than A1b fails it. The worst of the 27
  is 5.01 dB; I have not looked at which setting.

### 4.2 Source-dependent level (gate 4.5) — fails

The Hall baseline, measured before AS1 (largest spread among Hall's
sibling delay set and Hall under twelve ± 4 % perturbations): 0.66 / 0.82
/ 0.72 dB. The registered limit, an engineering criterion and not a
perceptually validated threshold: excess ≤ 0.5 dB, and at most half of
A1b's excess where that exceeds 1.0 dB.

| | baseline | A1b spread | A1b excess | AS1 spread | AS1 excess |
|---|---|---|---|---|---|
| medium | 0.66 | 2.27 | 1.61 | 2.40 | **1.74** |
| short | 0.82 | 2.83 | 2.01 | 2.88 | **2.07** |
| long | 0.72 | 0.63 | 0.00 | 1.59 | **0.87** |

Outside at all three. Third-octave roughness is likewise unchanged: 1.35 /
1.97 / 0.74 dB against A1b's 1.61 / 1.94 / 0.72.

One thing did improve and is not part of any gate: the static level law
AS1 would need runs from −0.9 to +3.8 dB over the nine grid settings,
against −5.8 to +5.7 for A1b.

### 4.3 Size, Decay, stereo and mono, CPU (gate 4.6) — fails on stereo

| | A1b | AS1 | |
|---|---|---|---|
| RT60 / Hall, nine settings | 1.000 – 1.001 | 1.000 – 1.002 | passes |
| t10 at Size 10 / 30 / 50 / 70 / 90 | 111 … 182 ms, monotone | 90 … 148 ms, monotone | passes |
| late correlation | −0.12 | −0.04 | |
| mono fold-down | −3.9 dB | −3.2 dB (Hall −3.0) | passes |
| **left / right balance** | 0.7 dB | **1.3 dB** | **fails** (limit 1.0) |
| worst mono tone | −17.2 dB | −8.4 dB (Hall −15.0) | passes; shallower than Hall |
| CPU | 0.87 × | 0.94 × | passes; machine in use, indicative only |

The generic vectors did what they were chosen for — mono fold-down is
within 0.2 dB of Hall's — and cost a 1.3 dB channel imbalance, which is
over the limit.

### 4.4 Nine-point Mode identity against Hall, onset-normalised (gate 4.2) — fails

The approved criterion: declared convergence region the short-Decay edge;
the six core points must be DISTINCT (no run ≤ 1.5, median ≥ 3.0). Hall's
frozen search, five seeds; the target zeroed before its onset and moved to
Hall's.

| Size, Decay | region | A1b median (lowest) | A1b | AS1 median (lowest) | AS1 |
|---|---|---|---|---|---|
| 25, 0.8 | convergence | 1.77 (1.75) | between | 1.31 (1.25) | REPRODUCED |
| 60, 0.8 | convergence | 4.76 (4.58) | distinct | 1.41 (1.15) | REPRODUCED |
| 85, 0.8 | convergence | 3.07 (2.89) | distinct | 2.62 (2.54) | between |
| 25, 2.5 | **core** | **2.43 (2.26)** | **between** | 1.59 (1.26) | **REPRODUCED** |
| 60, 2.5 (default) | **core** | 5.13 (4.85) | distinct | 1.67 (1.54) | **between** |
| 85, 2.5 | **core** | **2.71 (2.50)** | **between** | 3.43 (2.77) | distinct |
| 25, 5.0 | **core** | 3.82 (3.47) | distinct | 2.14 (1.95) | **between** |
| 60, 5.0 | **core** | 4.14 (4.06) | distinct | 2.24 (1.94) | **between** |
| 85, 5.0 | **core** | 3.75 (3.67) | distinct | 3.09 (2.99) | distinct |

- **AS1: 2 of 9 DISTINCT, 3 REPRODUCED, one of them a core point.** At the
  default it reads 1.67. Hall with Attack at 70 – 86 % reproduces it or
  nearly does across the small and medium Sizes. NOT DISTINCT.
- **Frozen A1b: 6 of 9 DISTINCT, none REPRODUCED — and two core points are
  BETWEEN** (Size 25 and Size 85 at Decay 2.5 s: 2.43 and 2.71). The
  default is DISTINCT at 5.13. Read strictly, **A1b itself does not meet
  the approved nine-point criterion**: it fails at two core points, not by
  being reproduced but by falling short of 3.0.

The second line is a new measurement on A1b. It changes no recorded
verdict, and A1b's status is yours to set. But the criterion AS1 was to be
held to is one the reference does not pass, and that has to be said before
any other candidate is measured against it.

### 4.5 The one-time Hall + Pre-Delay verification (4.3)

A: Hall's search in absolute time. B: the target moved earlier by five
values of D around D\* (its onset minus Hall's); each D is Hall with that
Pre-Delay; the reading is the lowest median over D.

| | D\* | A, absolute: median (lowest) | B, lowest median (lowest run) | at | reading |
|---|---|---|---|---|---|
| **A1b**, medium | 43 ms | 5.81 (5.21) | **1.93 (1.88)** | D\* + 20 | **WEAK** |
| A1b, short | 21 ms | 3.86 (3.73) | 1.46 (1.38) | D\* + 20 | mimic — in the convergence region |
| A1b, long | 70 ms | 11.89 (10.94) | 3.65 (3.47) | D\* − 10 | not a mimic |
| **AS1**, medium | 33 ms | 4.19 (4.01) | **1.47 (1.34)** | D\* + 10 | **MIMIC** |
| AS1, short | 17 ms | 2.05 (1.60) | 1.27 (1.17) | D\* | mimic — in the convergence region |
| AS1, long | 51 ms | 9.19 (9.15) | 1.59 (1.57) | D\* + 10 | WEAK |

- **AS1: Hall with Pre-Delay and its visible controls reproduces AS1's
  onset-normalised buildup at the default** (a run at 1.34), and comes
  within 1.6 at long. By the registered reading that is a mimic.
- **A1b at the default: 1.93, weak.** In absolute time it stands at 5.81.
  With the start aligned exactly it stands at 5.22. With 20 ms more
  Pre-Delay than that, Hall at Size 3 with Attack 59 comes to 1.93. So the
  onset-aligned search at D\* — the primary search — understates what Hall
  with Pre-Delay can do, at this point by a factor of 2.7.
- At short A1b is reproduced outright (1.38), inside the region where
  convergence was declared.
- At long A1b is not a mimic at any D tried.

**The distinction the decision asked to be kept**, applied: in none of
these is Hall merely moved later. In every "weak" or "mimic" row Hall's
other controls are doing the work — small Size and Attack at 60 – 100 % —
and Pre-Delay supplies only the start. That is the case that counts.

A caution on the row that matters most: at D\* + 20 the target is moved
earlier than its own measured onset by 20 ms. A1b's first energy above
−200 dB is at 55 ms and its 0.1 % onset at 68 ms, so advancing by 63 ms
discards the first 8 ms after first arrival, which holds less than 0.1 %
of the energy. I do not think that explains a fall from 5.2 to 1.9, but it
is a property of how B was built and I have not separated it out.

## 5. What this says about the hypothesis

The question was: can reducing the number of sparse transitions preserve
A1b-like staged buildup while materially reducing spectral variance and
source-dependent level instability?

**On this one experiment: no to the first half, and not shown for the
second.**

- **The buildup needs the joins.** One exchange cannot be made slow enough
  to give A1b's t50, and what it does give rises too abruptly: shape 0.29
  – 0.34 against 0.38 – 0.39. This is the clear part of the result.
- **The variance half is confounded.** The calibration rule drove AS1 to
  the edge of its range, where the join passes under 1 % per pass. Coupling
  that weak is itself more selective about which modes exchange. So AS1
  tests "one join at extremely weak coupling", not "one join". Low-
  frequency scatter still roughly halved at medium and long, which is in
  the direction the hypothesis predicts; the source spread did not move at
  all, which is not. I cannot say from this which of the two effects the
  flat source spread belongs to.
- **Buildup and distinctness went together again.** AS1 has less staged
  buildup than A1b and is reproduced by Hall's Attack control where A1b is
  not. Nothing here contradicts the scan's ordering.

## 6. What I am not doing

- Not building a second 4 → 4 at another k, a three-stage form, or a
  larger network. Any of them is another topology.
- Not changing AS1's k, its vectors or its line assignment in the light of
  the result.
- Not building B or C controls: no result above needs one to be read.
- Not altering A1b's recorded status or any gate.

## 7. Questions the result raises — yours to decide

1. **A1b under the criterion it is the reference for.** It is BETWEEN at
   two core points of the nine and WEAK at the default against Hall with
   Pre-Delay searched. Is the nine-point criterion as approved the standard
   for Ambient, in which case A1b's status needs a ruling; or does the
   reference stand on the six points and the absolute-time result?
2. **Which search is primary.** The onset-aligned search at D\* reads A1b
   at 5.22 at the default; a 20 ms search around it reads 1.93. If the
   one-time verification is what counts for mimicry, it should probably be
   run on every finalist over a range of D, not only at D\*. That is a
   change to the method approved in decision 2 and I have not made it.
3. **Whether to resolve the confound.** One more build of 4 → 4 at a
   moderate coupling (not calibrated to t50, which is unreachable) would
   say whether one join at ordinary coupling has low variance. It would
   say nothing new about preservation, which has failed. Authorised only
   if you say so.
4. **Whether the two requirements can be met together at all.** Staged
   buildup like A1b's needs several joins; several sparse joins compound
   the roughness. With eight lines there is no form with many joins and
   dense stages. The remaining route is more lines, at a CPU cost, and it
   is unexplored.

## 8. Tests and tooling

Test code only: two on-request suites in `Tests/Phase12Stage8Ambient.cpp`
(`runPhase12Stage8AmbientSibling`, `…SiblingSearch`) and their
registration lines. They run only when named exactly and are outside every
regression. Their "FINDING" checks fail where a gate failed; those are
results. No existing test, rig, gate or metric was edited. A1b is built by
Stage 6's builder, unchanged. The Stage 7 listening package was not
touched.

## 9. Regression

Run on the build that contains the AS1 suites, with nothing else building
or measuring.

| check | result |
|---|---|
| Fast regression (`AuroraVerbTests.exe --fast`) | 2593 / 2611 passed, 18 failures — the same totals as before |
| the 18 failures | compared line by line with the previous run: no difference |
| State suite | 26 / 26 passed |
| `git diff --stat 77f32fe -- Source`, `git status -- Source` | empty |

No check was added to the regression. Not covered: the heavy suites were
not re-run; the on-request suites are outside every regression by design.

## 10. Stop

Waiting for decisions on section 7. Nothing further is built for Ambient
until then.

Open from earlier rounds, unchanged: individual approval of RN1 – RN3;
three Plate closure runs on an idle machine; the two Plate findings; an
audit of earlier results for the late-texture artefact.
