# Phase 12 Stage 4 — human structural validation: preparation

Research only. `Source/` is unchanged. No Mode parameter, no production
topology, no new baseline. Hall is the Phase 11 production response.

This document is everything up to the listening. **The listening has not
happened.** Sections 7 and 10 are the instructions; the decision rules in
section 14 are written down before any answer exists.

Metric, scales, thresholds and search are Stage 3's, frozen at `c18ecce`, and
were not touched. Room B and Plate A are the structures Stage 3 scored,
parameter for parameter. Nothing was tuned from the results below.

## Where things stand

| | objective result | what listening must settle |
|---|---|---|
| Hall | reference | — |
| Room B | stable: DISTINCT for a positioned source in 30 of 30 runs, REPRODUCED for a centred source in 30 of 30 | whether anything is left on centred material. By the acceptance rule, if not, it is rejected as Room |
| Plate A | threshold-sensitive: 19 of 30 runs DISTINCT, never REPRODUCED; 1 seed of 10 gives a DISTINCT verdict | whether a score of 2.5–3.5 is a different family to a listener, and worth its cost |
| Chamber | NOT JUSTIFIED BY PHASE 12 STAGES 1–3 | closed |
| Ambient | CURRENT MECHANISMS NOT JUSTIFIED | closed until a different mechanism is proposed |

## 1–2. The impostor search over ten seeds

Stage 3 ran one search per target. Here the same search runs ten times per
target with ten seeds Stage 3 did not use. A residual is how far Hall's best
imitation still is; the search can fail to find a closer one but cannot invent
distance, so **the minimum is the tightest bound**.

| target | point | min | median | max | sd | ≤ 1.5 | between | ≥ 3.0 |
|---|---|---|---|---|---|---|---|---|
| Room B, any source | medium | 4.08 | 4.25 | 4.43 | 0.10 | 0 | 0 | 10 |
| | small / short | 4.39 | 4.43 | 4.50 | 0.03 | 0 | 0 | 10 |
| | large / long | 5.15 | 5.25 | 5.31 | 0.04 | 0 | 0 | 10 |
| Room B, centred source | medium | 0.85 | 0.90 | 0.98 | 0.05 | 10 | 0 | 0 |
| | small / short | 0.52 | 0.56 | 0.58 | 0.01 | 10 | 0 | 0 |
| | large / long | 1.35 | 1.41 | 1.42 | 0.02 | 10 | 0 | 0 |
| **Plate A** | medium | 2.95 | 3.17 | 3.47 | 0.15 | 0 | 1 | 9 |
| | small / short | **2.54** | 2.81 | 3.06 | 0.16 | 0 | 8 | 2 |
| | large / long | 2.78 | 3.12 | 3.29 | 0.15 | 0 | 2 | 8 |
| control: Hall, knobs moved | medium | 0.45 | 0.53 | 1.36 | 0.29 | 10 | 0 | 0 |
| control: ring matrix | large / long | 2.55 | 2.92 | 2.99 | 0.15 | 0 | 10 | 0 |

Stage 3's rule reads the weakest of the three points. Per seed:

| candidate | min | median | max | sd | REPRODUCED | WEAK | DISTINCT |
|---|---|---|---|---|---|---|---|
| Room B, any source | 4.08 | 4.25 | 4.39 | 0.09 | 0 | 0 | 10 |
| Room B, centred source | 0.52 | 0.56 | 0.58 | 0.01 | 10 | 0 | 0 |
| Plate A | 2.54 | 2.80 | 3.03 | 0.15 | 0 | 9 | 1 |

## 3. What the spread means

**Room B is stable in both directions.** No seed moves either verdict. The
positioned-source result and the centred-source result are both facts about
the structure, not about the search.

**Plate A is threshold-sensitive.** Its scatter (sd 0.15) is ordinary; the
problem is where it sits. Its range at every point straddles or touches 3.0.
Under the pre-registered rule it is DISTINCT under one seed in ten and WEAK
under nine. Stage 3's two figures for it, 2.58 and 3.13, were two draws from
this distribution, and the report's "DISTINCT" in the candidate table was the
lucky one.

Three things keep that from being a rejection:

- It is **never reproduced**. The closest Hall got in thirty runs is 2.54,
  against 1.36 for the worst run on a target Hall genuinely can reach.
- The **ring-matrix control** — a different topology by construction — lands in
  the same place: 2.55 to 2.99, WEAK in ten runs of ten. This metric scores
  some real topology differences just under its own line.
- So **3.0 is a pre-registered line, not a physical law**. It was not moved and
  will not be. What the numbers support is: Plate A is at the level of a known
  real difference, and the metric cannot call it either way.

That is exactly the case human listening is for.

A note on the search itself: its scatter is not uniform. The knobs-moved
control has one run at 1.36 among nine near 0.5, so a single search can miss by
a wide margin on an easy target. One search per target, as Stage 3 ran, was too
few for anything within about 0.5 of a threshold.

## 4–5. The Room render set

Six files. Same knobs in both halves, 100 % wet, Early/Late 0.

| source | position | setting | file count |
|---|---|---|---|
| snare, vocal | centred | compact / short (Size 25, Decay 0.8 s) | 2 |
| snare, vocal | centred | medium / normal (Size 60, Decay 2.5 s) | 2 |
| snare | hard left | compact / short | 1 |
| vocal | hard left | medium / normal | 1 |

The centred condition gets four files and the panned one two, because the
acceptance rule turns on the centred condition.

## 6. Room B, objectively, at those settings

Same knobs, no search — the difference as it sits in the files.

| setting | any source | centred source |
|---|---|---|
| compact / short | 8.12 (lateral spread) | 1.91 (modal structure) |
| medium / normal | 12.02 (lateral spread) | 1.60 (modal structure) |

A hard-left impulse, right against left output, dB:

| setting | reverb | 0–50 ms | 50–200 ms | 200–600 ms | whole |
|---|---|---|---|---|---|
| compact / short | Hall | −0.4 | −1.0 | −1.0 | −0.8 |
| | Room B | −20.4 | −6.6 | −4.8 | −10.0 |
| medium / normal | Hall | 0.0 | −1.2 | −1.3 | −1.1 |
| | Room B | nothing arrives | −9.3 | −3.9 | −8.2 |

The rendered hard-left sources, whole file: Hall −0.3 to −1.5 dB, Room B −7.6
to −10.0 dB. Hall's reverb comes back centred; Room B's stays on the source's
side.

On a centred source the same-knob difference is 1.6–1.9, and once Hall is
allowed to move its knobs it closes to 0.5–1.4. **The measurement predicts
that the four centred files will sound like the same reverb.** If they do,
Room B is rejected as Room (section 14).

## 7. Listening instructions

In `renders/phase12-stage4/LISTENING.md`, with an answer sheet. Twelve files,
`s4-01.wav` to `s4-12.wav`. Each file is one comparison: a sound through one
reverb, half a second of silence, the same sound through another. The listener
answers the same five questions for every file and does not need to know which
test a file belongs to:

1. Same or different? (same / slightly / clearly)
2. If different: two different *kinds* of reverb, or one reverb set slightly
   differently?
3. Which words fit which half — smaller and closer; metallic or zingy; softer
   at the start of each hit; wider or to one side?
4. Which would you rather use?
5. Would you want both as separate choices in a plugin?

These carry the brief's questions without naming Room or Plate: "different
kinds" is the family question, "smaller and closer" is the compact-room
question, "metallic" against "softer at the start" is dispersion against blur,
and questions 4 and 5 are the would-you-choose-it question.

Two things are in the pack that the brief did not ask for:

- **A catch trial** (Hall against Hall, identical halves). It shows how often
  this listener reports a difference when there is none, which is what gives
  the other eleven answers their weight.
- **The Room B / Plate A cross-check** is rendered now, not after the results,
  so a second round is not needed. If either candidate fails it is ignored.

The pack is 100 % wet with Early/Late at 0, like the earlier packs and like
the metric. That isolates the late field, which is the only thing the two
structures change. It also means the early-reflection layer, which is
identical in every Mode, is not there to dilute the difference: a difference
heard here is an upper bound on what a mix would show.

## 8. The Plate render set

Four files, centred source.

| source | setting |
|---|---|
| snare, vocal | short plate (Size 50, Decay 1.5 s) |
| snare, vocal | longer plate (Size 60, Decay 2.5 s) |

No tonal change of any kind: same damping, same everything, and the chain is
all-pass (0.00 dB in every octave band, Stage 3). If Plate's identity is not
audible like this, it has none in Phase 12's terms.

## 9. Plate A, objectively, at those settings

| setting | same-knob score | reverb | 250 Hz | 4 kHz | 8 kHz |
|---|---|---|---|---|---|
| short plate | 3.93 (band arrival) | Hall | −0.1 | −2.1 | −2.5 |
| | | Plate A | +7.1 | −13.7 | −18.4 |
| longer plate | 4.11 (band arrival) | Hall | −2.0 | −3.2 | −3.5 |
| | | Plate A | +3.8 | −15.9 | −20.2 |

Band figures are arrival relative to 1 kHz, ms. In Hall every band arrives
within 3.5 ms of every other. In Plate A, 8 kHz leads 250 Hz by about 25 ms.

Density (impulse, normalised echo density in a 20 ms window):

| | 0–20 ms | 10–30 | 20–40 | 40–60 | reaches 0.5 | 0.7 | 0.9 |
|---|---|---|---|---|---|---|---|
| Hall | 0.01 | 0.41 | 0.74 | 0.80 | 14 ms | 18 ms | 50 ms |
| Plate A | 0.09 | 0.64 | 0.85 | 1.07 | 6 ms | 12 ms | 24 ms |

Plate A is dense twice as soon. That is a second, independent part of its
character, and the plate-like one.

## 11. Transient audit

**The chain alone.** A unit click comes out as a 15.2 ms downward chirp, energy
unchanged, peak 12.6 dB lower. Group delay: 28 ms at 125–250 Hz, 25 ms at
1 kHz, 9 ms at 4 kHz, 3 ms at 8 kHz.

**Through the reverb** (one hit, short-plate setting, first 100 ms of the wet
response, Plate level-matched to Hall):

| source | reverb | centroid, ms | t5 | t50 | build (t50 − t5) | peak re Hall |
|---|---|---|---|---|---|---|
| impulse | Hall | 59.9 | 30.8 | 57.8 | 27.0 | — |
| | Plate A | 62.8 | 33.6 | 61.6 | 28.0 | −1.0 dB |
| snare | Hall | 63.6 | 34.7 | 62.2 | 27.5 | — |
| | Plate A | 66.4 | 37.5 | 66.4 | 28.9 | −0.6 dB |
| percussion | Hall | 67.2 | 37.4 | 67.6 | 30.2 | — |
| | Plate A | 68.2 | 38.8 | 68.7 | 29.9 | +0.5 dB |

The 12.6 dB the chain takes off a click does not survive into the wet signal:
the tank spreads a click far more than the chain does, so the wet peak moves
by 1 dB or less and the onset builds 1.4 ms slower at most. The whole wet
onset is 1–3 ms later.

**Dispersion or blur.** A blur delays and smears every band alike; a
dispersion delays each band differently and leaves each band's rise alone.
Arrival / rise per band, ms:

| source | reverb | 250 Hz | 1 kHz | 4 kHz | 8 kHz |
|---|---|---|---|---|---|
| impulse | Hall | 34.3 / 22.0 | 33.7 / 20.5 | 32.4 / 21.6 | 32.1 / 20.5 |
| | Plate A | 60.7 / 13.7 | 50.6 / 21.4 | 39.5 / 21.9 | 35.5 / 20.5 |
| snare | Hall | 38.1 / 26.1 | 38.5 / 19.8 | 36.7 / 22.3 | 36.0 / 21.2 |
| | Plate A | 64.3 / 21.7 | 55.3 / 21.4 | 44.6 / 22.4 | 39.1 / 21.4 |
| percussion | Hall | 40.1 / 24.9 | 40.1 / 25.0 | 39.9 / 25.2 | 39.9 / 24.1 |
| | Plate A | 45.6 / 26.6 | 45.9 / 26.3 | 44.6 / 25.5 | 42.2 / 23.8 |

- On the impulse and the snare it is **dispersion, not blur**: arrivals spread
  over 25 ms across bands while each band's rise is within about 1 ms of
  Hall's (250 Hz excepted, where it is faster).
- On the bright percussion the ordering mostly disappears (3.7 ms across
  bands). That source is a 30 ms noise burst with little below 1 kHz, so there
  is nothing for the chain to separate. **Plate A's signature depends on the
  source having a sharp, broadband onset.** Expect it on the snare files more
  than on the vocal ones.
- The objective checks found no transient damage: peak within 1 dB, onset
  build within 1.4 ms, density sooner not later. Whether the ordered arrival
  reads as plate "zing" or as a softened attack is a listening question the
  measurement cannot answer.

The audit's "share of energy in the first 10 / 20 ms" columns are zero for
both reverbs: at Early/Late 0 nothing arrives before about 30 ms. They are in
the test output and say nothing.

## 12–13. Plate CPU

A profile only. Nothing was optimised. Single thread, best of seven,
unoptimised research rig.

| part of Plate A | ns / sample | share |
|---|---|---|
| dispersion chain (2 × 240) | 936.5 | **74.6 %** |
| tank, without modulation | 101.9 | 8.1 % |
| modulation | 129.9 | 10.3 % |
| input diffusion | 47.3 | 3.8 % |
| other (attack, level, early, I/O) | 40.3 | 3.2 % |

Hall 319.5 ns, Plate A 1255.9 ns: **3.93 ×** in this run. Stage 3 reported
2.82 × and 4.25 × for the same code; the ratio moves with machine state
because the two parts are bound by different things. Read it as "about three
to four".

Cross-check: the chain timed alone is 871 ns, by subtraction 937. "Modulation"
is what Mod Depth 0 saves, a lower bound on that part.

**Section count.**

| sections per channel | ns / sample | × Hall |
|---|---|---|
| 0 | 319.5 | 1.00 |
| 30 | 406.6 | 1.27 |
| 60 | 511.5 | 1.60 |
| 120 | 743.7 | 2.33 |
| 240 | 1255.9 | 3.93 |
| 480 | 2320.2 | 7.26 |

Linear: 273 + 4.21 ns per section, r² = 0.998. One section on one channel
costs 2.1 ns per sample. 76 sections cost one extra Hall.

**SIMD.** Each section is `y = a·x + s; s = x − a·y`. Section k needs section
k−1's output from the same sample and its own state from the previous one.

- Along the cascade: not vectorisable as written. The loop is bound by the
  latency of one multiply-add after another, not by the amount of arithmetic.
- Across the two channels: possible, at most × 2, and an out-of-order core
  already overlaps the two chains.
- Across time: also a recurrence.
- In blocks along anti-diagonals (section k at sample n with section k−1 at
  sample n+1) the work is independent, so SIMD is possible in principle. Not
  built, not measured.

So the serial recurrence blocks the obvious vectorisation and leaves only a
restructured one.

**Plate A is a reference upper bound.** If it passes listening, the
240-section chain is the sonic reference and not the shipping design. Stage 3
already showed the naive economy fails: 120 sections with a stronger
coefficient scored WEAK. The next stage's question is the cheapest mechanism
that keeps the two measured properties — ordered arrival and early density.

## 14. Cross-check and decision rules

Objectively Room B and Plate A are not two ends of one dimension. Room B moves
lateral spread and leaves band arrival alone (0.3–0.5 in that family, Stage
3); Plate A moves band arrival and leaves lateral spread alone (0.2–0.5).
Stage 3's mutual search, run with Room A (the same structure at a smaller
angle) and one seed, had the pair DISTINCT from each other at every point
(4.55–7.48). It was not repeated with Room B. File `s4-03` puts both on one hard-left snare.

**Written before any listening result:**

| | PASS | CONDITIONAL | FAIL |
|---|---|---|---|
| **Room B** | centred files heard as a different kind of reverb, described as smaller or closer, and wanted as a separate choice | centred files heard as slightly different but not as a different kind | centred files heard as the same or essentially Hall — **whatever the panned files sound like** |
| **Plate A** | heard as a different kind on snare and vocal, the difference described as character and not as damage, and wanted as a separate choice | audibly different but "same kind", or only on the snare, or preferred by nobody | heard as the same, or the difference described only as a blurred attack |

If Room B's panned files are clearly different and its centred files are not,
the classification is, verbatim:

> ROOM B — STRUCTURAL SPATIAL BEHAVIOR, NOT SUFFICIENT ROOM FAMILY

and it is rejected as the production Room Mode. The measurement predicts this
outcome. The lateral behaviour would remain a documented property that could
be offered some other way; that is not a Phase 12 decision.

If the catch trial is reported as "clearly different", every other answer in
that sitting is suspect and the sitting should be repeated.

Valid outcomes: Hall only; Hall + Plate; Hall + Room; Hall + Room + Plate.

## 15. Chamber

**CHAMBER — NOT JUSTIFIED BY PHASE 12 STAGES 1–3.** Removed from active V1
Mode research. No Stage 4 effort was spent on it.

## 16. Ambient

**AMBIENT — CURRENT MECHANISMS NOT JUSTIFIED.** Candidates A–E are closed and
were not tuned further. This does not ban an Ambient Mode: a future candidate
needs a fundamentally different structural mechanism.

## Mode count: what each choice would cost

No decision here, and none before the listening results. These are my
reading of the consequences, not measurements.

| | 3 Modes | 4 Modes | 5 Modes |
|---|---|---|---|
| **what it means today** | Hall + both candidates pass | 3 + one mechanism not yet found | 3 + two not yet found |
| **UI** | three-way switch or segmented control | selector | selector or menu |
| **parameter** | see below | see below | the five-choice parameter already decided |
| **presets** | each Mode can carry a real set | thinner per Mode | thinnest; weakest Mode's presets expose it |
| **Phase 13 Character** | 3 × 3 = 9 voicings to validate | 12 | 15 |
| **claims** | "three reverb families", each provable | one claim resting on research not done | two such claims |

The parameter is the part that cannot be changed later. A choice parameter's
automation value is its index divided by (count − 1), so adding a choice after
release moves every existing choice and breaks saved automation. The earlier
locked decision is a new five-choice Mode parameter beside the untouched
legacy `mode`. If V1 ships fewer than five real Modes, the options are to
declare five slots and expose fewer (stable later, but hidden values must map
somewhere defined), or to declare the true count and accept that it is final.
That choice should be made with the count, not before it.

Stage 2 showed what a Mode without a structural dimension is: a preset. A
five-Mode claim with two such Modes is the weaker product and the weaker
claim.

## 17. Fast and heavy test suites — a proposal

Not implemented. The full run is now roughly two and a half hours, of which
Stage 3 is 57 minutes and Stage 4 is 30.

| | FAST REGRESSION | HEAVY STRUCTURAL RESEARCH |
|---|---|---|
| contents | every suite through Phase 11; Phase 12's cheap sections (core identity, floor, semantics, stereo, tone, switching, Room and Plate objective sections, transient audit) | every stochastic impostor search: 12.2.5, 12.3.3–12.3.7, 12.4.1; and the CPU profiles |
| when | every change | final Mode acceptance; any major topology change; Phase 12 closure; release validation |
| how | a `--fast` switch that skips suites tagged heavy and **prints what it skipped** | the default, argument-free run, exactly as today |

Rules that keep this from weakening anything:

- **The default stays the whole thing.** CI and `run-tests.ps1` run everything
  unless told otherwise, so the gate is unchanged. Fast is opt-in.
- A fast run reports "N heavy suites not run" in its summary and cannot print
  "ALL TESTS PASSED" without that line.
- Heavy suites stay deterministic: fixed seeds, as now. Two runs of 12.4.1
  must agree to the last digit.
- No test is deleted or loosened. The split is by cost only.
- The CPU profiles move to heavy for a different reason: their numbers are
  only meaningful on a quiet machine.

## 18. Production source

`git diff --stat -- Source` is empty. No Mode parameter, no new topology, no
baseline.

Changed in `Tests/`: `Phase12Stage4.cpp/.h` (new), a `structural::ensureFloor()`
so a filtered run can measure the floor without printing section 12.3.2
(same pairs, same numbers), registration in `CMakeLists.txt`, `Suites.h`,
`RunAll.cpp`, and one export entry in `TestExports.cpp`. New script:
`Scripts/export-phase12-stage4-pack.ps1`.

## 19. Regression

**The full regression was not completed for this stage.** It was started and
then stopped by me when the Stage 5 brief arrived, because Stage 5 needs the
machine and ends with its own regression, which covers this code. At the
point it was stopped it had reached the Phase 7 suites with 2241 checks
passed and 4 failures, all four among the 18 preserved ones (Phase 7
decay-law checks); nothing new.

What did run to completion:

- The Stage 4 suite on its own: **12 / 14**. The two failures are findings:
  Plate A is not DISTINCT in every run (19 of 30), and Room B is not DISTINCT
  from Hall on a centred source at the same knobs (1.91).
- State suite: 26 / 26.

Stage 4 changed no existing test's arithmetic: `ensureFloor()` shares the
floor computation with section 12.3.2 and gives the same numbers.


## 20. Commit

Committed at `27eff87`.
