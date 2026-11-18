# Phase 12 Stage 8 — Ambient: what the diagnosis found. Report, and a stop

Research only. `Source/` is unchanged. No Mode selector, no Phase 13.
**No repair candidate was built.** The diagnosis that was to come before
the repair found something about A1b itself that the repair cannot go
ahead without a decision on. This reports it and stops.

Nothing here reclassifies A1b. Its status is the decision of 2026-11-18:
STRUCTURAL REFERENCE / REPAIR REQUIRED.

Order of record: pre-registration (`d68021a`) → DA0 – DA6 → addendum 1
(`d5fd886`) → DA7 – DA10 → addendum 2 (`82fa86d`) → DA11 → this report.
Each set of diagnostics was registered before it was run. Logs:
`Docs/Phase12-Stage8-runs/01`, `02`, `03`.

## 1. The three findings, in order of consequence

### 1.1 With Pre-Delay, Hall comes close to A1b at small / short

Hall's frozen seven-control search, five seeds, against A1b:

| target | medium | small / short | large / long | lowest run |
|---|---|---|---|---|
| A1b as recorded (Stage 5, 6) | 9.77 | 4.00 | 15.33 | 3.56 |
| A1b, pre-arrival dust set to exact zero (DA11b) | 5.81 | 3.86 | 11.89 | 3.73 |
| **A1b, arrival removed — Hall with Pre-Delay (DA11c)** | **5.06** | **2.09** | **3.61** | **1.97** |

- The frozen search has seven controls. **Pre-Delay is not one of them,
  and the product has a Pre-Delay control.**
- A1b starts later than Hall at the same knobs: 35 ms at medium, 15 ms at
  short, 65 ms at long (DA7). DA11c moves A1b earlier by exactly that,
  which is the same comparison as giving Hall that much Pre-Delay.
- At medium and long A1b is still distinct (5.06 and 3.61, no run under
  3.55). **At small / short it is not: median 2.09, every run between 1.97
  and 2.14.** Under the rule Stage 6 applied to Ambient repairs — a median
  under 2.5 is REJECTED — that would be a rejection at small / short. No
  run reached 1.5.
- AT5a, the 8 % direct repair, was rejected at small / short at 2.06. That
  is now explained: a direct path removes the late start, and the late
  start was most of what separated A1b from Hall there.
- One pre-delay value per point was tried, fixed in advance. A search over
  Pre-Delay could only lower these figures.

This is a diagnostic reading under the label "A1b, arrival removed". It is
not G2 and it changes no verdict. But it bears directly on the first of the
four reasons given for A1b's structural pass ("Hall's mimic search still
fails against A1b at the three main operating points"): **that holds for
the seven controls searched. With the eighth it holds at two points and is
weak at the third.**

### 1.2 A1b has no late texture of its own; the family was reading its late start

At the same knobs A1b's late-texture distance from Hall was 155 (medium)
and 298 (long). Those numbers are an artefact.

- The family measures the spread, in dB, of 5 ms energy bins from 30 ms
  on, and skips a bin only when its energy is exactly zero.
- A1b's response arrives at 55 ms (medium) and 100 ms (long), abruptly:
  one bin takes it from below −200 dB to above −60 dB. Before that there
  are non-zero samples more than 200 dB down. The family counts them: its
  "fluctuation" for A1b is **77 dB and 148 dB** against Hall's 0.5.
- A filter that changes A1b's response by 2.5 × 10⁻¹⁴ of its peak moves the
  score by 58 and 75.
- With the dust set to exact zero, late texture falls from 155 to 7.3 and
  from 298 to 4.4; with the arrival also removed, to nothing at medium and
  2.8 at long.
- **In 200 – 500 ms, where the response exists, A1b's texture is Hall's:**
  0.31 / 0.35 / 0.35 dB against 0.33 / 0.42 / 0.46.

What A1b does have, robustly, at the same knobs and after the arrival is
removed:

| | buildup | energy profile | modal structure | stereo | decay |
|---|---|---|---|---|---|
| medium | 10.3 | 6.0 | 6.8 | 9.4 | 3.9 |
| short | 7.4 | 5.1 | 3.2 | 2.7 | 2.7 |
| long | 13.0 | 5.5 | 6.6 | 6.7 | 4.2 |

So A1b's measurable temporal identity is **a slow, late build of energy**
(its loudest 5 ms is at 215 ms where Hall's is at 50 ms), not a texture.
The phrase "late texture" in the Stage 5 – 7 reports, in the Room-against-
Ambient result ("two and three orders above the threshold") and in the
objective for this stage described a number that is not a property of the
sound.

The same defect in the family affects any structure with a late arrival,
and Hall itself when anything non-zero precedes its arrival (a −120 dB
noise floor makes Hall read 3.1 and 7.4 against itself). **I have not
audited earlier results for it.** That is a separate piece of work and
should be done before any earlier late-texture figure is relied on.

### 1.3 The tone defect is half systematic and half chance, and the chance half is the level problem

Octave deviation from Hall over twelve small perturbations of the delays
(± 4 %), which move the modes and change nothing else:

| | 125 Hz, mean | 125 Hz, standard deviation | Hall's own |
|---|---|---|---|
| medium | −0.95 dB | 1.14 dB | 0.36 |
| short | −1.50 dB | 0.57 dB | 0.26 |
| long | +0.47 dB | 1.10 dB | 0.14 |

- **The recorded 1.82 dB is a 0.95 dB systematic deficit plus this delay
  set's place in a distribution 1.14 dB wide.** Stage 6's Size sweep, which
  changed sign four times, was that distribution.
- **The systematic part has two causes.** The mid-band lift and the 8 kHz
  loss vanish with damping off: loss met while migrating. The
  low-frequency deficit does not (−0.95 → −1.12): it is not loss, not
  residence time (the same in every band, DA6) and not modulation (DA10:
  unchanged at depth 0, default and double). **Its cause is not
  established.** Five hypotheses have now failed: loss, stage assignment,
  modes per stage, residence time, modulation.
- **The chance part comes from hearing one two-line stage** (DA4). It falls
  to Hall's floor only when every stage is heard or the input is let
  through directly — the two excluded routes.
- **The chance part is also the source-dependent level.** Third-octave
  roughness 1.6 / 1.9 / 0.7 dB against 0.3 / 0.3 / 0.2 for Hall's sibling;
  spread over vocal, snare and percussion **2.27 / 2.83 / 0.63 dB** against
  0.08 / 0.28 / 0.55.
- **A separate, larger level problem:** the static level law A1b needs
  runs from −5.8 to +5.7 dB over the nine grid settings. Stage 7's "1.4 –
  4.2 dB louder at short" was mostly this. A law fixes it; none exists.

**A lower bound on any repair.** The tone gate is 1.0 dB at the defaults
and over 27 settings. With a 1.1 dB standard deviation at 125 Hz the worst
of 27 is expected near 2.5 – 3 dB whatever the mean — A1b's recorded 3.02.
Passing needs the deviation near 0.35 dB, Hall's own floor. So: **a repair
that only corrects the systematic part cannot pass the grid; the heard
path has to stop being sparse.** This is arithmetic on the measured
figures, not itself a measurement.

## 2. Why no repair was built

The objective was: preserve A1b's staged energy migration and late-texture
topology; neutralise the spectral imbalance and the source-dependent
variation; no direct path; no output filter. Requirement 2 asked a repair
to read within 1.5 of A1b.

1. **Requirement 2 is not measurable as written.** The metric reads A1b
   58 – 75 away from itself after a 10⁻¹⁴ change. What a repair must
   preserve has to be restated on something real — the build of energy —
   and that restatement is a change to an approved requirement.
2. **What is to be preserved is in question at small / short.** The late
   start is most of A1b's margin there. A repair that keeps it keeps
   something Pre-Delay gives Hall; a repair that loses it (as AT5a did)
   falls to 2.1. Either way the small / short case does not stand on
   structure alone.
3. **The two defects pull opposite ways from the identity.** The variance
   comes from the sparse, fully migrated readout. That readout is also what
   makes the energy arrive late and gradually. The measured cures for the
   variance are the ones that shorten the migration. I have not shown that
   no structure escapes this; I have five probes that do not.
4. **The low-frequency deficit has no known cause**, so a topology-internal
   compensation for it would be a fit, not a repair.

Building AR1 – AR5 now would mean choosing answers to 1 and 2 myself.

## 3. What I would put to you

Not decisions — the questions the evidence raises, with what I would
measure for each.

**A. Does Pre-Delay belong among Hall's controls in the gate?** It is a
visible control. If yes, every structure with a late arrival needs
re-reading, A1b first, and the frozen search has to be extended — by
decision, not by me. If no, the reason should be recorded, because the
gate's stated purpose is that a Mode must not be another Mode with its
visible controls moved.

**B. What is Ambient's identity, measurably?** On the evidence: energy that
builds slowly and peaks late, at every setting, without a late start being
the whole of it. Whether that is enough for a Mode at small / short —
where, with the start aligned, Hall gets to 2.1 — is the product question.
Under the approved Mode-identity criterion, small / short could be a
declared convergence point for Ambient against Hall; that criterion was
written for the acoustic-space Modes and applying it to Ambient would be a
new decision.

**C. Should the late-texture family be repaired or retired?** It skips
exact zeros and counts dust. The metric is frozen; changing it re-opens
every result that used it. Not changing it leaves a family that reads
arrival time as texture. An audit of which recorded results depend on it
would say how much is at stake, and costs nothing but time.

**D. For the repair itself**, if it goes ahead: the level law is
straightforward and independent of everything above; the loss part of the
tilt is a per-stage damping calibration; the variance needs a denser heard
path, and the honest expectation is more lines (cost) or less migration
(identity). The low-frequency deficit needs a cause first.

## 4. What was built

Test code only.

- `Tests/Phase12Stage8Ambient.h`, `.cpp` — three on-request suites
  (`runPhase12Stage8AmbientDiagnosis`, `…Diagnosis2`, `…Diagnosis3`): they
  run only when named exactly and are not part of any regression.
- Registration lines in `Tests/RunAll.cpp`, `Tests/Suites.h`,
  `Tests/CMakeLists.txt`.
- No existing test, rig, gate or metric was edited. A1b is built by Stage
  6's builder, unchanged. The Stage 7 listening package was not touched.

The diagnostic suites contain three "FINDING" checks. Two fail, each
reporting a registered expectation that did not hold (DA0: A1b with Hall's
tone reads as A1b; DA10: the modulation hypothesis). They are results, not
regressions, and are outside the regression by construction.

## 5. Corrections to my own earlier statements

- Stage 5 – 7 reports and the Stage 7 package notes describe A1b's
  difference from Hall and from Room as "late texture", with figures of
  155 – 298. Those figures are the artefact of 1.2. The documents are left
  as written; this section is the correction.
- Addendum 1 predicted DA10 would hold. It did not.
- Addendum 2 predicted small / short would stand in DA11c. It did not.
- Stage 6 called the tone defect "one band, located, not explained". It is
  a mean and a variance, and only the mean's loss part is explained.

## 6. Regression

Run on the final build, with nothing else building or measuring.

| check | result |
|---|---|
| Fast regression (`AuroraVerbTests.exe --fast`) | 2593 / 2611 passed, 18 failures — the same totals as Stage 7b |
| the 18 failures | compared line by line with Stage 7b's run: no difference |
| State suite | 26 / 26 passed |
| `git diff --stat 77f32fe -- Source`, `git status -- Source` | empty |

Stage 8 added no check to the regression. Not covered: the heavy suites
were not re-run; the on-request diagnostic and Plate suites are outside
every regression by design.

## 7. Stop

Waiting for decisions on A – D of section 3. Nothing further is built for
Ambient until then.

Still open from earlier rounds, unchanged: individual approval of RN1 –
RN3; three Plate closure runs on an idle machine; the two Plate findings
(the 0.5 ms allowance; the 192 kHz total).
