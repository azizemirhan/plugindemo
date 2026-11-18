# Phase 12 Stage 8 — Ambient: addendum 3 (the four decisions; the scan and the root-cause analysis, registered)

Addendum to `Phase12-Stage8-Ambient-Repair-Preregistration.md`. Written
after the decisions of 2026-11-18 on the four questions of the diagnosis
report and **before** the scan and the root-cause analysis are run. No
repair candidate is built in this step.

## 1. The four decisions

1. **Pre-Delay belongs in the mimic gate.** It is one of Hall's visible
   controls; if Hall with Pre-Delay reproduces a candidate, that counts
   against Mode independence. But a uniform time shift must not fool the
   identity metric: both **absolute-time** behaviour and
   **onset-normalised** internal buildup are evaluated. After the first
   meaningful wet-energy onset is aligned or removed, the candidate must
   still show its staged buildup. A uniform shift of Hall is not staged
   growth.
2. **Ambient's identity is staged energy migration / buildup shape.** Not
   tone, not 125 Hz attenuation, not output gain, not Pre-Delay, not a
   longer Decay. At small / short in particular, the shape of energy growth
   *after onset*. A1b's small / short behaviour is the structural reference
   and its Hall-against-A1b buildup separation must be preserved. At medium
   and long, late texture *may* remain an additional dimension. Numeric
   thresholds are derived from the frozen A1b and a Hall baseline, not
   guessed.
3. **Repair continues.** The staged-migration family is not abandoned; A1b
   stays the frozen structural reference. Before another candidate, a
   **diagnostic scan** of earlier Ambient results. Diagnostic only: no old
   candidate is passed, no threshold moves, coloration is not read as
   identity.
4. **Repair architecture**, in order: A root-cause diagnosis stage by
   stage; B broadband stage normalisation; C residence-time compensated
   internal loss; D stage-transfer normalisation; E combination only after
   A – D are measured in isolation. Excluded: output EQ, a direct Hall-like
   path, AT5a-style direct injection as the primary repair, Character
   coloration, relaxed tonal thresholds. Every candidate is compared with
   Hall and with frozen A1b.

**This step:** the scan and step A, then a report. Nothing of B – E.

## 2. Onset, and the onset-normalised measures

**Onset.** The time at which a response's running energy first reaches
0.1 % (−30 dB) of its total. One definition for every structure. It does
not depend on numerical dust and does not need a floor.

**Measured from the onset** (energy before the onset is discarded; all
fractions are of the energy after it):

| measure | what it is |
|---|---|
| t10, t50 | time to 10 % and to 50 % of the energy |
| t10 / t50 | a shape number with no time scale. An instantaneous rise followed by an exponential decay gives 0.152 whatever its decay time; an n-stage migration gives more (0.47 for four equal stages). Neither Decay nor Pre-Delay moves it |
| centroid | energy-weighted mean time |
| early fraction | share of the energy in the first 50 ms |
| trajectory distance | the largest gap between two normalised cumulative-energy curves on the same time axis (a Kolmogorov – Smirnov distance), against Hall at the same knobs |

**The frozen metric, onset-aligned.** The candidate's response with
everything before its onset set to exact zero and moved to Hall's onset,
then the frozen families against Hall at the same knobs: buildup, energy
profile, echo density, late texture. The frozen code is not edited; the
target is prepared before it is read.

**Late texture, where the response exists.** The family's two windows on
the onset-aligned response, and its 200 – 500 ms figure.

## 3. The scan

**Population.** Every Ambient structure of Stages 3, 5 and 6 that the
code still builds, and controls. Each is rebuilt from its own definition;
responses are cheap and are recomputed. **Hall's search is not re-run:**
the recorded gate figures are quoted from the stage reports beside the new
measures.

| stage | structures |
|---|---|
| 3 | Ambient A, B, C, D, E (through-coupled blocks at a fixed angle) |
| 5 | A1a (k 0.25), A1b (k 0.40), A3 (2 → 2 → 4 lines, k 0.30) |
| 6 | AT2 every stage heard; AT3 25 % direct; AT5a 8 % direct; AT6 last two heard; AT7 heard stage of four; D5 stages ascending, stages folded |
| controls | Hall's sibling delay set; Hall with Attack at maximum; **Hall delayed by A1b's onset difference** (a pure shift) |

**Per structure, at the three points:** the measures of section 2; octave
deviation from Hall as mean and standard deviation over twelve delay
perturbations (125 Hz, and the worst band's mean); third-octave roughness;
the source spread of requirement 4; centroid against Hall's (residence).

**Relationships.** Rank correlations (Spearman) across the staged
structures between: buildup shape (t10 / t50), trajectory distance,
residence, the 125 Hz mean, the 125 Hz standard deviation, roughness,
source spread, and — where recorded — Hall's search result. With about a
dozen structures these are descriptive. A correlation is reported as a
relationship, not a cause.

**Hall baseline for thresholds.** 300 random settings of Hall's controls
(Size, Decay 0.3 – 8 s, Early/Late, Early Diffusion, Attack, modulation),
fixed seed: the range of t10 / t50, early fraction and centroid / t50 that
Hall reaches with *any* setting. A1b at the nine grid settings beside it.
The thresholds proposed in the report are taken from the gap between the
two; none is fixed in this addendum.

**Reading rule for the pure-shift control.** If Hall delayed by A1b's
onset difference reads as Hall on every onset-normalised measure (as it
must, by construction) and A1b does not, the measures separate staged
growth from Pre-Delay. If A1b's onset-normalised figures fall inside
Hall's baseline range at small / short, then at that point A1b's
separation from Hall is not buildup shape, and that is reported in those
words.

## 4. Step A — root cause, stage by stage

A1b, at medium and at small / short, damping on and off, twelve delay
perturbations so that what is systematic is separated from what is chance.
The tank's state is read line by line; each stage is its two lines; Hall's
"stage" is all eight.

| | measure | answers |
|---|---|---|
| RC1 | each stage's share of the energy held over the whole response, per octave | where the energy sits |
| RC2 | each stage's octave balance against Hall's, mean over the perturbations | **at which stage the spectral fingerprint appears** — already at the first, or growing along the chain |
| RC3 | the same, standard deviation at 125 and 250 Hz | **at which stage the variance appears** |
| RC4 | the ratio of energy held in stage s + 1 to stage s, per octave | transfer efficiency of each join by band. For a chain with exchange rate r and loss rate λ this ratio is r / (r + λ) at the last join; a band that transfers less shows a lower ratio |
| RC5 | energy-weighted mean time of each stage's energy, per octave | residence by stage and band |
| RC6 | broadband, damping off: the decay rate of the total energy held, against the rate asked for, and against Hall | conservation: unintended accumulation or loss |
| RC7 | the angles, sin²θ, stage loop times and τ at each setting | the coupling law as built |
| RC8 | the output's octave balance against the last stage's held energy | what the readout adds |

**Reading rule.** The fingerprint is "created at" the first stage where
the mean deviation at 125 Hz reaches half of its value at the output. The
variance likewise. If the first stage already carries it, the cause is the
injection or the stage's own modes and not the transfer, and step D is not
where the repair is.

## 5. Not done in this step

No candidate. No extension of the frozen search: adding Pre-Delay to
Hall's search is decided, and how it is implemented (an eighth search
dimension, or an onset-aligned target) is proposed in the report for
approval before any gate is run with it.
