# Phase 12 Stage 8 — Ambient: the scan of earlier results and the root-cause analysis

Research only. `Source/` is unchanged. No Mode selector, no Phase 13.
**No repair candidate was built and no repair topology is committed to.**
This is the report asked for before one is.

A1b remains the frozen structural reference. No earlier candidate is
passed, no threshold is moved, and coloration is not read as identity
anywhere below.

Order of record: the four decisions and the definitions (addendum 3,
`832a91f`) → scan and step A → addendum 4 (`a5e4df6`) → DA12, DA13 → this
report. Logs: `Docs/Phase12-Stage8-runs/04`, `05`, `06`.

## 1. In five lines

1. **The variance is made at every join, not at the readout.** At 125 Hz
   the scatter of each stage's balance grows 0.07 → 0.29 → 0.65 → 1.14 dB
   along the chain. Each exchange between two sparse stages multiplies in
   its own roughness.
2. **The low-frequency "deficit" is mostly not systematic.** When the
   delays are moved enough to shift the lowest modes, the mean at 125 Hz is
   not separable from variance at medium and long. At short about 0.7 dB of
   it is real. Five failed explanations were explaining a draw.
3. **Across fifteen earlier structures, staged buildup and the defects
   rise together** (rank correlation 0.85 – 0.98 at medium and long). Every
   structure that fixed tone did it by losing the buildup. None has both.
4. **Measured from the onset, A1b's buildup is real and very stable — and
   at small / short it is a slower Hall, not a differently shaped one.**
   Its shape number is 0.39 against Hall's 0.33 there; its landmarks are
   about twice Hall's.
5. **Steps B and C can remove what is systematic. They cannot touch the
   variance, and the variance is what fails the tone gate and the source
   consistency.** That needs fewer joins or denser stages — a change of
   topology, which is why I am stopping here.

## 2. Step A — where the defects are created

A1b, the tank's state read line by line, twelve delay perturbations.

### 2.1 The coupling as built (RC7)

| | τ | sin²θ at the three joins | stage loop times |
|---|---|---|---|
| medium | 333 ms | 0.096, 0.110, 0.137 | 32, 37, 46, 55 ms |
| short | 107 ms | 0.145, 0.167, 0.207 | 15, 18, 22, 26 ms |
| long | 667 ms | 0.081, 0.093, 0.115 | 54, 62, 77, 92 ms |

Weak coupling: 8 – 21 % of the energy exchanged per pass.

### 2.2 Where the energy is (RC1, RC5)

- The first stage holds 83 – 87 % of all the energy the tank ever holds;
  the heard stage **0.6 – 1.3 %**. What is heard is a hundredth of what is
  stored.
- Mean time of the energy by stage, medium: 171 / 232 / 287 / 341 ms. Each
  join adds about 55 – 60 ms (20 – 30 ms at short). With the damping off
  this is the same in every band to within 3 %: **residence time is not
  frequency dependent.** With the damping on, 125 Hz and 8 kHz arrive 5 –
  10 % sooner because they are damped more — the ordinary effect of the
  two shelves, not a property of the topology.

### 2.3 Conservation (RC6)

Damping off, the total energy held decays at −27.2 / −80.3 / −13.4 dB per
second against −24.0 / −75.0 / −12.0 asked for; Hall holds the asked rate
to 0.2 %. A1b's tank is deliberately asked for a shorter time (decay scale
0.88 – 0.93) so that the *heard* response has the right RT60, and the
total follows that. There is no unintended accumulation or loss of
broadband energy. **Step B has nothing to correct in the decay.** What it
does have is the level: see 4.3.

### 2.4 The systematic part (RC2, RC4, RC8, DA12)

Each stage's octave balance against Hall's, mean over the perturbations,
medium:

| | 125 | 250 | 500 | 1k | 2k | 4k | 8k |
|---|---|---|---|---|---|---|---|
| stage 1 | +0.34 | +0.06 | −0.08 | −0.10 | −0.04 | −0.01 | +0.04 |
| stage 2 | −0.07 | +0.55 | +0.12 | +0.12 | +0.17 | +0.01 | −0.17 |
| stage 3 | −0.11 | +0.33 | +0.30 | +0.41 | +0.33 | +0.13 | −0.46 |
| stage 4 | −0.37 | +0.60 | +0.42 | +0.48 | +0.39 | +0.21 | −0.62 |
| the output | −0.95 | +0.53 | +0.46 | +0.46 | +0.36 | +0.28 | −0.64 |

- **The mid-band lift and the 8 kHz loss grow stage by stage** (+0.1 →
  +0.4; −0.17 → −0.62) and **vanish with the damping off** (all within
  ± 0.1). This is loss met while migrating: the energy that is heard has
  been in the tank 1.7 times as long as Hall's and has passed the shelves
  that many more times. It is systematic, it has a known cause, and it is
  what step C is for.
- **The 125 Hz figure is a different thing.** Under the ± 4 % perturbation
  it looked systematic (−0.95 at medium, −1.50 at short). Under ± 30 %,
  which actually moves the lowest modes (DA12):

| | A1b mean at 125 Hz | its standard error | Hall, perturbed the same way | reading |
|---|---|---|---|---|
| medium | −0.83 | 0.27 | −0.69 | not separable from variance |
| short | −0.66 | 0.18 | +0.17 | **systematic**, about 0.7 – 0.8 dB |
| long | −0.17 | 0.25 | +0.18 | not separable from variance |

  At medium Hall's own delays, perturbed, read −0.69: the reference delay
  set happens to sit high at 125 Hz. A1b is not low there; Hall's set is
  slightly high.
- At short there is a real deficit of about 0.7 dB. Step A places it: the
  first two joins pass 125 Hz 1.0 and 0.7 dB less well than 1 – 4 kHz, and
  the third hardly differs. At medium the three joins are 0.6, 0.2 and
  0.3 dB down. The cause is not loss, not residence and not modulation;
  with coupling this weak and loops this short there are only two to four
  modes per stage in the octave, and exchange between two sparse sets of
  modes is less complete than between dense ones. **I can say where it
  happens and that it scales with sparseness; I have not proved the
  mechanism.**
- **The readout adds a further −0.6 dB at 125 Hz at medium** (output
  against the last stage's held energy, relative to Hall), and −0.7 dB at
  250 Hz at short. Under the ± 4 % perturbation only; by the argument above
  it may also be this delay set's alignment. Not resolved.

By the registered reading rule: the systematic deviation at 125 Hz reaches
half of the output's at **stage 2** at short, and at no single stage at
medium (it is spread over the joins and the readout).

### 2.5 The variance (RC3)

Standard deviation of each stage's balance over the perturbations:

| | 125 Hz, medium | 250 Hz, medium | 125 Hz, short | 250 Hz, short |
|---|---|---|---|---|
| stage 1 | 0.07 | 0.07 | 0.12 | 0.24 |
| stage 2 | 0.29 | 0.30 | 0.54 | 0.34 |
| stage 3 | 0.65 | 0.44 | 0.40 | 0.47 |
| stage 4 = the output | **1.14** | **0.83** | **0.57** | **0.79** |
| Hall | 0.36 | 0.19 | 0.26 | 0.25 |

- **The first stage is as smooth as Hall. The roughness is added at each
  join**, about 0.3 – 0.5 dB each at 125 Hz at medium. Three joins: 1.14.
- The readout adds nothing: the output's scatter is the last stage's.
- Under ± 30 % it is larger still and reaches further up: 1.30 / 0.72 /
  0.81 / 0.62 dB at 125 / 250 / 500 / 1k (medium).
- **So the variance is not a property of hearing two lines. It is a
  property of passing energy through a series of sparse resonant stages**:
  each exchange has a frequency response as rough as the two stages' modes
  allow, and the roughness of a cascade compounds.

This is the root cause of the two defects that matter most:

- **Tone over the grid.** With 1.1 – 1.3 dB of scatter at 125 Hz the worst
  of 27 settings is expected near 3 dB whatever the mean. A1b's recorded
  figure is 3.02.
- **Source-dependent level.** The same roughness at third-octave scale
  (1.6 – 1.9 dB against 0.3 for Hall's sibling) is what makes one source
  come out 2 – 3 dB hotter than another.

## 3. The scan

Fifteen Ambient structures from Stages 3, 5 and 6, rebuilt from their own
definitions, and three controls. Hall's search was not re-run; the gate
figures quoted are the stage reports'.

### 3.1 Buildup, measured from the onset

Onset: the running energy first reaches 0.1 % of the total. Everything
after is measured from it, so Pre-Delay and a late start count for
nothing.

Medium (Hall: t10 23 ms, t50 106 ms, t10 / t50 0.217, early fraction 0.25):

| | onset | t10 | t50 | t10 / t50 | early | buildup\* | energy profile\* | recorded search |
|---|---|---|---|---|---|---|---|---|
| **A1b (reference)** | 68 | 78 | 203 | 0.384 | 0.05 | 9.4 | 5.1 | 9.77 / 4.00 / 15.33 |
| A1a k 0.25 | 67 | 65 | 179 | 0.363 | 0.06 | 7.1 | 4.2 | 7.77 / 2.90 / 16.73 |
| A3 2-2-4 | 63 | 55 | 181 | 0.306 | 0.08 | 8.1 | 3.2 | 7.76 / 4.12 / 35.93 |
| AT6 last two heard | 65 | 70 | 196 | 0.359 | 0.06 | 8.3 | 4.6 | — |
| AT7 2-2-4 k 0.40 | 63 | 59 | 193 | 0.305 | 0.07 | 8.3 | 3.5 | — |
| Stage 3 Ambient B | 95 | 70 | 182 | 0.381 | 0.05 | 9.0 | 5.4 | 19.87 / 3.09 / 22.85 |
| Stage 3 Ambient A | 73 | 51 | 183 | 0.277 | 0.10 | 6.0 | 2.7 | 12.32 / 3.51 / 58.58 |
| Stage 3 Ambient E | 72 | 40 | 147 | 0.271 | 0.14 | 4.9 | 2.0 | 9.02 / 1.21 / 18.46 |
| AT2 all heard | 25 | 32 | 150 | 0.216 | 0.16 | 2.5 | 1.4 | — |
| AT5a 8 % direct | 38 | 14 | 92 | 0.148 | 0.30 | 1.3 | 1.4 | 3.97 / 2.06 / 4.63 |
| AT3 25 % direct | 38 | 13 | 88 | 0.147 | 0.32 | 1.4 | 1.6 | 4.03 / 1.98 / 5.97 |
| *control:* Hall's sibling | 25 | 22 | 105 | 0.214 | 0.26 | 1.0 | 0.1 | |
| *control:* Hall, pure shift | 68 | 23 | 106 | 0.217 | 0.25 | 0.0 | 0.0 | |
| *control:* Hall, Attack max | 29 | 68 | 251 | 0.270 | 0.07 | 14.4 | 3.4 | |

\* the frozen families against Hall at the same knobs with the onsets
aligned.

- **The pure-shift control reads exactly as Hall** (0.00 – 0.03 on every
  family). The onset-normalised measures do what decision 1 asks: a
  uniform delay is invisible to them.
- **A1b's staged buildup survives the normalisation**: t10 is 78 ms after
  its own onset against Hall's 23; half its energy is in by 203 ms against
  106; 5 % of it in the first 50 ms against 25 %.
- **The two direct-path repairs are Hall** on every buildup measure
  (buildup 1.3 – 1.4, shape 0.15). That is what they lost.
- **Hall's own Attack control makes a slow build** (t10 68 ms at medium).
  It is a visible control and the search has it.

### 3.2 A1b's buildup is stable and has a nearly constant shape (DA13)

Over the twelve perturbations A1b's landmarks move by about 1 %:

| | t10 | t50 | early fraction | t10 / t50 | Hall's t10 / t50 |
|---|---|---|---|---|---|
| medium | 78.3 ± 0.9 ms | 204.9 ± 2.7 | 0.045 ± 0.001 | 0.382 ± 0.002 | 0.217 |
| short | 29.8 ± 0.6 | 76.0 ± 1.1 | 0.263 ± 0.008 | 0.392 ± 0.004 | 0.334 |
| long | 153.2 ± 1.7 | 398.7 ± 4.6 | 0.014 ± 0.000 | 0.384 ± 0.002 | 0.179 |

The shape number is **0.38 – 0.39 at all three points**. That is the
measurable form of "staged migration": the same proportion of build
whatever the decay. Hall's moves from 0.18 to 0.33 because its rise is
fixed by Size while its decay follows Decay.

### 3.3 At small / short the shape is not what separates A1b from Hall

- At the short point A1b's shape number is 0.39 and Hall's is 0.33. A1b's
  landmarks are about twice Hall's (t10 30 against 14 ms, t50 76 against
  43). **It is a slower Hall there, not a differently shaped one.**
- Over the nine grid settings, at Decay 0.8 s: Size 25: 0.387 against
  0.334; Size 60: **0.296 against 0.348**; Size 85: **0.215 against
  0.325**. At short Decay and medium or large Size A1b's shape number is
  *below* Hall's.
- Hall's reach over 300 random settings: t10 from 0 to 112 ms; the shape
  number up to 0.29 at the 95th percentile and far higher in a few sparse
  cases (large Size, very short Decay).
- This agrees with the earlier result that Hall's search, given A1b with
  its start aligned, reaches 2.09 at small / short.

**So the thing decision 2 asks to be preserved — the Hall-against-A1b
buildup separation at small / short — exists as a difference of time
scale, and Hall has controls for time scale.** After onset normalisation
it is a separation of about 2 under Hall's free search, not of 4. I am
reporting that, not acting on it.

### 3.4 Late texture, where the response exists

Onset-aligned, the family's figures against Hall:

| | medium | short | long |
|---|---|---|---|
| A1b, late-texture family | 0.34 | 0.14 | 2.49 |
| A1b, spread in 200 – 500 ms | 0.29 dB | 0.36 | 0.28 |
| Hall, spread in 200 – 500 ms | 0.33 dB | 0.42 | 0.46 |

**Nothing at medium or short. At long, 2.5** — under the 3.0 line — from
the first 200 ms after the onset, where few reflections have yet reached
the heard stage. It is graininess at the start of the build, and the
variance of section 2.5 seen in time. As "an additional identity
dimension at medium and long" it is absent at medium and weak at long.

### 3.5 Relationships across the family

Rank correlation over the fifteen structures (descriptive; fifteen is few
and a correlation is not a cause):

| | medium | short | long |
|---|---|---|---|
| buildup shape ↔ 125 Hz scatter | +0.85 | +0.43 | +0.89 |
| trajectory distance from Hall ↔ 125 Hz scatter | +0.88 | +0.35 | +0.90 |
| 125 Hz scatter ↔ third-octave roughness | +0.98 | +0.58 | +0.84 |
| roughness ↔ source spread | +0.71 | +0.89 | +0.63 |
| buildup ↔ residence | +0.89 | +0.86 | +0.95 |
| buildup ↔ source spread | +0.59 | +0.49 | +0.50 |

- **The more a structure builds like A1b, the rougher it is**, and the
  rougher it is the more its level depends on the source. At medium and
  long this is nearly one ordering of the fifteen.
- The three that are smooth (AT2, AT3, AT5a: scatter 0.11 – 0.15 dB,
  spread 0.3 – 0.9 dB) are the three with Hall's buildup.
- None of the fifteen has both A1b's buildup and Hall's smoothness.

**The one partial exception, and it is a lead, not a candidate.** Stage
3's Ambient B — two blocks of four lines, **one** join, a longer input
diffuser:

| | buildup\* | shape | 125 Hz scatter | roughness | source spread |
|---|---|---|---|---|---|
| A1b, medium / short / long | 9.4 / 6.3 / 12.7 | 0.38 / 0.39 / 0.39 | 1.14 / 0.57 / 1.10 | 1.61 / 1.94 / 0.72 | 2.27 / 2.83 / 0.63 |
| Ambient B | 9.0 / 9.5 / 7.2 | 0.38 / 0.49 / 0.33 | 0.52 / 0.40 / 0.37 | 0.78 / 0.62 / 0.38 | 1.52 / 1.19 / 0.44 |

Comparable buildup with a third to a half of the scatter. It fits the
root cause exactly: one join in place of three, four lines a stage in
place of two. **It was rejected in Stage 3 and stays rejected**: its
coupling is a fixed angle (the swell shrinks with Size), its decay drifted
up to 25 % from the control, and its mono fold-down was 7 – 8 dB down. Part
of its buildup is its eight-stage diffuser. Stage 5's timing rule and
decay calibration were invented after it and were never applied to a
two-stage, four-line form. That combination has not been built.

## 4. What this means for steps B – E

### 4.1 What each step can and cannot do

| step | acts on | can remove | cannot remove |
|---|---|---|---|
| B broadband stage normalisation | level | the setting-dependent level (4.3) | anything spectral; any variance |
| C residence-time compensated loss | the two damping shelves, per stage | the mid-band lift and the 8 kHz loss (0.4 – 0.6 dB), which have a known cause | the variance; the 125 Hz draw |
| D stage-transfer normalisation | the coupling law | possibly the 0.7 dB low-frequency transfer deficit at short | the variance, unless it changes how many joins there are or how dense the stages are |
| E combination | | B + C: a correct mean | the scatter around it |

**B and C are worth doing and I expect them to work on what they address.
Neither touches the defect that fails the gate.** After a perfect B and C,
A1b would have the right average tone and a scatter of 1.1 – 1.3 dB at
125 Hz around it — still about 3 dB at the worst of 27 settings, and still
2 – 3 dB between sources.

### 4.2 What the variance needs

From step A and the scan together: fewer joins, or denser stages, or both.
With eight lines the choices are few:

| form | joins | lines per stage | status |
|---|---|---|---|
| 2 → 2 → 2 → 2 (A1b) | 3 | 2 | the reference; scatter 1.14 |
| 2 → 2 → 4 (A3, AT7) | 2 | 2, 2, 4 | measured: scatter 0.8, buildup a little less |
| 4 → 4, timed and calibrated | 1 | 4 | **not built**; Ambient B is its untimed ancestor: scatter 0.4 – 0.5 |
| more than eight lines | | | costs CPU; not explored |

Each step down in joins also shortens the migration, and the migration is
the identity. Whether one timed join between two four-line stages still
builds like A1b (shape 0.38, t10 78 ms at medium) is the open question,
and it is a question about a **new topology** — which is why I have not
built it.

### 4.3 The level law

The static level A1b needs against Hall runs from −5.8 to +5.7 dB over the
nine grid settings (−2.4 at small / short). This is independent of
everything above, has no bearing on identity, and is a smooth function of
Size and Decay to fit. It is step B's real content.

## 5. Thresholds, derived and proposed — none adopted

From the frozen A1b and the Hall baseline, as decision 2 asks. For review.

**Preservation against A1b** (same knobs, three points, onsets aligned):

| measure | A1b's own spread | proposed line |
|---|---|---|
| t10 | ± 1.1 – 2.0 % | within 10 % of A1b's |
| t50 | ± 1.2 – 1.4 % | within 10 % |
| t10 / t50 | 0.382 – 0.392, ± 0.004 | within 0.03 of A1b's |
| early fraction | ± 2 – 3 % | within 15 % |
| frozen buildup and energy-profile families against A1b | — | each ≤ 1.5 |

A1b moves by about 1 % on its own, so 10 % is several times its natural
spread and still far from Hall (whose t10 is 52 – 77 % below A1b's). The
multiples are my judgement.

**Separation from Hall** — I do not propose a number, because the
measurements disagree with the premise:

- at medium and long the onset-normalised separation is large and any
  reasonable line holds (t10 3.4 – 4.4 × Hall's; shape 0.38 against 0.18 –
  0.22);
- at small / short it is a factor of two in time scale with nearly the
  same shape, and Hall's search with the start aligned reaches 2.09.

A threshold "outside Hall's reach" cannot be set at small / short on these
measures: A1b is not outside Hall's reach there. If the requirement is
"keep A1b's small / short buildup", the preservation table above does that.
If it is "be distinct from Hall at small / short under the gate with
Pre-Delay", A1b itself does not meet it.

## 6. Pre-Delay in the gate: a proposal for approval

Decided: Pre-Delay counts. How, is not yet fixed. Two ways:

1. **An eighth search dimension.** Faithful, but it changes the frozen
   search and every structure has to be re-searched to be comparable.
2. **Onset-aligned targets.** The target is prepared — zeroed before its
   onset and moved to Hall's onset at the same knobs — and the frozen
   search runs unchanged. This is what DA11c did. It is cheap, leaves the
   frozen code untouched, and is slightly generous to the candidate (one
   alignment, not a search over delays).

I recommend 2 as the gate and 1 as a one-time check that 2 does not
understate Hall, on A1b only. Both evaluations decision 1 asks for then
exist: the absolute-time search (the recorded G2) and the onset-normalised
one. Nothing is run under either until you approve the method.

## 7. Corrections to my own earlier statements

- The Stage 8 diagnosis report said the low-frequency deficit was
  "systematic" with "cause not established" after five hypotheses. At
  medium and long it is not systematic. The instrument (± 4 %) could not
  move the low modes; I should have seen that before reading its mean.
- Addendum 4 predicted "not separable" at medium and long and "do not
  know" at short. That held.
- The same report said the variance "comes from hearing one two-line
  stage". Step A shows it is built up at every join; the readout adds
  none.
- Addendum 1 gave requirement 4's baselines as "≤ 1.14 at medium and
  ≤ 1.42 at short". Those stand.

## 8. Tests and tooling

Test code only: three more on-request suites in
`Tests/Phase12Stage8Ambient.cpp` (`runPhase12Stage8AmbientScan`,
`…RootCause`, `…Wide`) and their registration lines. They run only when
named exactly and are outside every regression. No existing test, rig,
gate or metric was edited. The Stage 7 listening package was not touched.

## 9. Regression

Run on the build that contains the scan, root-cause and wide-perturbation
suites, with nothing else building or measuring.

| check | result |
|---|---|
| Fast regression (`AuroraVerbTests.exe --fast`) | 2593 / 2611 passed, 18 failures — the same totals as before |
| the 18 failures | compared line by line with the previous run: no difference |
| State suite | 26 / 26 passed |
| `git diff --stat 77f32fe -- Source`, `git status -- Source` | empty |

No check was added to the regression. Not covered: the heavy suites were
not re-run; the on-request suites are outside every regression by design.

## 10. Stop — what I need before a repair topology

1. **Which requirement governs at small / short** (section 5): preserve
   A1b's buildup there, or be distinct from Hall there with Pre-Delay in
   the gate. A1b satisfies the first and not the second.
2. **Approval of the Pre-Delay method** (section 6).
3. **Whether the repair may change the number of joins and the lines per
   stage.** B and C alone will give a correct mean and leave the scatter;
   I can build and measure them as they are, in that order, and report
   exactly that. Reducing the scatter means a 4 → 4 timed form or more
   lines, which is no longer "A1b repaired" but a sibling of it judged
   against A1b's buildup.
4. **The proposed preservation lines** (section 5), or others.

Open from earlier rounds, unchanged: individual approval of RN1 – RN3;
three Plate closure runs on an idle machine; the two Plate findings; an
audit of earlier results for the late-texture artefact.
