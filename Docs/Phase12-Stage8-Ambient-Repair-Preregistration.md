# Phase 12 Stage 8 — Ambient: repairing A1b without losing it. Pre-registration

Research only. `Source/` is unchanged. No Mode selector, no Phase 13.
**Written and committed before any Stage 8 measurement.** Nothing below has
been run.

## 0. The decision this starts from (2026-11-18)

The blind evaluation is complete and the key has been opened.

**AMBIENT A1b = STRUCTURAL REFERENCE / REPAIR REQUIRED.**

- A1b has a **structural pass**: Hall's search fails against it at the
  three operating points, the structural metric is strongly separated, the
  blind signal analysis found substantial temporal-energy redistribution on
  real sources, and the fingerprint controls show the tonal coloration is
  separable from the structural behaviour.
- A1b does **not** have a production pass. Its objective failures stand in
  full: 1.82 dB tonal deviation at the defaults, up to 3.02 dB over the
  grid, and a source-dependent level variation at short Decay.
- A1b is not rejected and not replaced as the Ambient reference mechanism.

Not permitted:

- changing the 1 dB tonal threshold;
- re-reading the tonal failure as desirable Character;
- an output equaliser as the shipping solution;
- AT5a's direct-energy bypass, or any path by which input reaches the
  output without migrating — it repairs tone and loses structural
  independence at short Decay (Stage 6: 2.06 at small / short).

Also still in force from earlier stages: no AGC, compressor, dynamic gain,
envelope follower or correlation clamp; no shimmer, freeze, granular, pitch
or special-effect delay; no output decorrelation stage.

**The objective.** Keep A1b's staged energy migration and late texture.
Neutralise the spectral imbalance and the source-dependent energy variation
that the topology itself produces, by means inside the topology.

## 1. A1b, frozen

The structural reference is A1b exactly as Stage 5 left it and Stage 6
rebuilt it: `stage6::ambientRaw (AmbientSpec {})` — four stages of two
lines, Hall's sorted delays paired {0,4} {1,5} {2,6} {3,7}; input into the
first stage only; output from the last stage only; neighbouring stages
exchanged by a rotation whose angle is timed from Decay
(sin²θ = stage loop time / τ, τ = 0.40 × RT60 / 3); decay calibrated per
setting and level matched at the defaults, as in Stage 6.

It is not edited from here on. Every Stage 8 candidate is a separate
structure, compared against Hall **and** against this.

## 2. What a repaired Ambient must do — all six at once

| # | requirement | measure | pass |
|---|---|---|---|
| 1 | Hall mimic resistance | G1 then G2, Hall's free seven-control search, three points, five seeds — unchanged | SURVIVES: no run ≤ 1.5, every median ≥ 3.0 |
| 2 | A1b's temporal identity | frozen metric, candidate against A1b **at the same knobs**, three points | ≤ 1.5 on the worst centred family (the PRESERVED line Stage 6 used for Plate) |
| 3 | tone | octave shares against Hall, 125 Hz – 8 kHz — unchanged | ≤ 1.0 dB at the defaults **and** over Stage 6's 27-point grid |
| 4 | source-to-source consistency | section 3 | section 3 |
| 5 | Size and Decay | Stage 6's G3: RT60 within 5 % over nine settings; t10 monotone in Size | as Stage 6 |
| 6 | stereo and mono | Stage 3's limits against Hall's own row | no defect |

And CPU ≤ 1.5 × Hall.

On requirement 2: a candidate that passes 1 and 3–6 but reads above 1.5
against A1b is **a different Ambient**, not a repaired A1b. It is reported
as such, with the family that separates it, and goes forward only by a
separate decision. It is not called a repair.

On requirement 2's fairness: the Stage 7 control "A1b with the fingerprint
filtered off" is A1b's structure with Hall's tone. Its score against A1b is
measured first (diagnostic DA0). If that control itself reads above 1.5,
requirement 2 cannot be met by *any* tonal repair, and that is reported
before any candidate is judged by it. The line is not moved.

## 3. Requirement 4, defined

Stage 7 measured the gain each structure needs to match Hall on each
source, with the structure levelled once at the defaults. A1b: up to
4.2 dB at short Decay, differing by source.

Two different things are in that figure and they are separated here:

- **A level that depends on the setting** (the same for every source). A
  smooth static level law over Size and Decay removes it. That is a
  calibration, like the decay calibration, and is allowed. It is not gain
  that follows the signal.
- **A level that depends on the source** at one setting. No law removes
  it. This is the defect.

**Measure.** At each of the three operating points, with the structure
level-matched to Hall on its impulse response *at that setting*: the gain
still needed to match Hall on each of the three broadband sources of the
Stage 7 package (vocal, snare, percussion). The **spread** is the largest
minus the smallest of the three. The piano phrase is reported and does not
gate: Stage 7 showed that on a tonal source Hall's own sibling delay set
moves by 3.4 dB, so it does not separate a defect from chance.

**Baselines** to be measured in DA5 before any candidate: A1b, AT5a, and
Hall with its sibling delay set (what an ordinary change of delays does).

**Pass — "materially improved":** at every point where A1b's spread exceeds
1.0 dB, the candidate's spread is at most **half** of A1b's; at no point is
it more than 0.3 dB above A1b's. **Preferred:** ≤ 1.0 dB at every point.

The setting-dependent part is reported separately as the level law the
candidate would need (dB at the nine grid settings), with its range.

"Half" is my reading of "materially improved" and is the number here most
open to challenge. It is fixed now, before A1b's spreads on this exact
measure are known.

## 4. Diagnosis first

Stage 6 located the tonal deviation and did not explain it. A repair aimed
at an unexplained defect is a guess, and Stage 6's guesses (AT1 – AT7)
either failed or cured it by the forbidden route. So the cause is measured
before any candidate is built.

**The question the diagnosis is built to answer.** Two different things
would both show up as "125 Hz is 1.8 dB low":

- **S — a systematic imbalance.** Energy that is heard only after it has
  migrated has been in the tank longer, and has met the frequency-dependent
  loss for longer; or the transfer between stages is itself less efficient
  at some frequencies. S has a sign and a shape that do not depend on the
  particular delay values.
- **V — variance.** A1b is heard through one path: in at two lines, out at
  two lines. Hall is heard through all eight both ways. A single sparse
  path has a rougher frequency response; an octave band's share is then one
  draw from a wide distribution. V has no fixed sign: it changes when the
  delays change slightly. The Stage 6 Size sweep (125 Hz at five Sizes:
  −2.43 / +1.67 / −2.19 / −0.87 / +0.43 dB) looks like V. It was read at
  the time as weak support for a different hypothesis.

They need different repairs. S is answered by loss and transfer
calibration. V is answered by making the heard path less sparse *without*
adding a direct path. V would also explain the source-dependent level,
which S alone would not.

| | diagnostic | reads |
|---|---|---|
| DA0 | "A1b, fingerprint off" (Stage 7's control) against A1b, frozen metric, same knobs, three points | whether requirement 2 is reachable by a tonal repair |
| DA1 | A1b's octave deviation from Hall with every delay perturbed by up to ± 4 % (twelve fixed seeds), at the three points; the same perturbation applied to Hall against unperturbed Hall | per band: the **mean** over seeds (S) and the **standard deviation** (V); Hall's own standard deviation as the floor |
| DA2 | DA1 with damping off | how much of S is loss met during migration |
| DA3 | third-octave shares, 100 Hz – 10 kHz, A1b and Hall's sibling against Hall: the standard deviation across bands | roughness of the response at the scale a source's spectrum samples it |
| DA4 | DA1's V for variants that change only how many lines are heard or fed (heard: last stage / last two / all, with stage weights; fed: first stage / first two) | which sparseness V comes from. These are probes, not candidates |
| DA5 | requirement 4's measure for A1b, AT5a and Hall's sibling | the baselines of section 3 |
| DA6 | per octave: energy-weighted mean arrival time, A1b against Hall | residence time by band, for a loss calibration |

**Reading rule, fixed now.** For a band at a point: S is "present" if
|mean| ≥ 0.5 dB and |mean| ≥ twice its standard error; V is "present" if
A1b's standard deviation is at least twice Hall's and ≥ 0.5 dB. The
candidates of section 5 are built for whichever is present. If neither is,
the diagnosis has failed and that is reported before anything is built.

## 5. Candidate families

Named now; their parameters are fixed in an addendum after the diagnosis
and before any is built. All keep input at the first stage only and output
after migration only.

| | family | answers | mechanism |
|---|---|---|---|
| **AR1** | residence-time loss calibration | S, loss part | each stage's damping scaled by the mean time energy has spent before reaching it, so that the energy *heard* has met Hall's total frequency-dependent loss. Per-stage, static, set from Decay and Size |
| **AR2** | frequency-compensated transfer | S, transfer part | the rotation between stages made a function of frequency (a paraunitary exchange) so that the migration rate is the same in every band. Lossless at every frequency |
| **AR3** | stage normalisation | V | the heard stage, and if needed each stage, widened in *modes* without widening in *paths*: more lines inside a stage, exchanged at the same timed rate, the stage's total loss normalised to its line count. AT7 tried one version of this (three stages, the last of four lines) and was worse; AR3 keeps four stages |
| **AR4** | several migrated readouts | V | more than one fully migrated path heard: the chain read at its far end through two or more decorrelated late stages fed only by migration. No stage is heard that the input can reach in fewer exchanges than A1b's last |
| **AR5** | per-stage loss normalisation | S and level | the loss per second made equal in every stage after the exchange is accounted for, so the energy arriving at the heard stage does not depend on how the bands were distributed on the way |

A family is built only if the diagnosis shows the thing it answers.

**Not candidates:** anything with a non-zero direct share; any filter after
the readout; hearing a stage the input reaches sooner than A1b's last
(AT2, AT6 — Stage 6: over 1 dB over the grid, and they shorten the
migration).

## 6. Order

1. DA0 – DA6.
2. Addendum: the reading, and the parameters of the families the reading
   calls for.
3. Per candidate, cheapest decisive check first: tone at the defaults; tone
   over the grid; requirement 2 at small / short (the point where every
   Stage 6 repair lost its identity); requirement 4; then G1, G2, G3.
4. One build each. A failed candidate is not tuned; a second attempt needs
   a second written reason.

## 7. What would count as failure

If no candidate meets all six: the report says which of the six are
jointly reachable and which are not, with the nearest candidate's figures.
No threshold is lowered to produce a pass, A1b stays the structural
reference with its defects, and the decision returns to you.
