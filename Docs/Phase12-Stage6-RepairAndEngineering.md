# Phase 12 Stage 6 — Room and Ambient repair, a Plate engineering family, Chamber concepts

Research only. `Source/` is unchanged. No Mode parameter, no production
topology, no Phase 13. Hall is frozen.

Gates and hypotheses were committed before measurement
(`Docs/Phase12-Stage6-Preregistration.md`, `61679d5`), and three addenda were
appended and committed before the runs they describe (`e1df45d`, `0f0f594`,
`a47a762`). The metric and the search are Stage 3's, frozen at `c18ecce`.

The raw output of every run is kept in `Docs/Phase12-Stage6-runs/`, 01 to 05.
Figures below are from log 05 unless a log is named. Stage 6 was stopped once
(a machine shutdown) and resumed; see section 36 for what that means for the
evidence.

## The result

**Stage 6's success definition is not met.** It asked for a Room that passes
its pass condition, an Ambient within 1 dB of Hall's tone that keeps its
identity, the two distinct from each other, a Plate at or under 1.75 × Hall,
and a Chamber or a clear statement that there is none.

| | status after Stage 6 | what stands in the way |
|---|---|---|
| **HALL** | proven, frozen | — |
| **PLATE** | **solved as engineering.** The 240-section reference, rearranged, is the same filter at 1.60–1.72 × Hall (reference: 4.4 ×) and is REFERENCE-EQUIVALENT | the preferred 1.5 × was not reached; Hall-as-target is only NOT REFUTED against Plate's controls (2.61), which the pre-registration reports and does not hold against Plate |
| **ROOM** | **blocked.** Stereo, mono and tone repaired (R5b). Size and ringing not | the Size law that works is reproduced by Hall at large / long (1.90); every construction that removes the ringing is reproduced by Hall |
| **AMBIENT** | **blocked.** Tone repaired by four candidates (0.62–0.73 dB over the grid); all four REJECTED by Hall's search | at small / short the repaired structures sit at 1.9–2.1; A1b itself is 4.00 |
| **CHAMBER** | **no mechanism.** Three new concepts, six constructions, six rejected | fourteen negative constructions across Stages 3, 5 and 6 |

Room and Ambient, in the forms that still stand structurally (R5b and A1b),
are DISTINCT from each other in both directions. Neither of them is a
shippable Mode: each still fails G3.

No threshold was moved. Failed candidates stay failed.

**The two general findings.**

1. *For Room, the ringing is the identity.* Hall's search stops on "modal
   structure" against R4c and R5b. The two constructions that bring modal
   prominence from 42 dB to 23 and 17 dB are reproduced by Hall (1.41, 1.06).
   The one partial reduction that keeps the identity (RR1, 35.1 dB) keeps
   most of the ringing. On this metric a small block that does not ring is
   Hall.
2. *For Ambient, the tone defect and the identity are the same property too.*
   The 125 Hz deficit goes away as soon as any of the input is heard without
   migrating (8 % is enough), and with it goes the distance from Hall at
   small / short.

## Wrong predictions, kept

| prediction | what happened |
|---|---|
| Ambient is off in tone because of loss accumulation | damping off makes it worse (1.82 → 2.04 dB) |
| Ambient's replacement hypothesis (too few modes per stage at low frequency) | not established: D7 is not monotone in Size, and AT7, built on it, is worse (3.69 dB over the grid) |
| RR2 (respaced delays) reduces Room's ringing | 41.8 dB against 42.4 |
| a first pre-registered rule (RS4) for combining Size levers | prevented the combination the two-rate model needed; replaced in addendum 1 with the reason stated |
| a vector rule that looks only at safety (RM3) | 1.23 dB of octave error; composite R5 failed tone and stays failed |
| PV2, two-wide through memory, is faster than scalar | it was slower in the first run |
| PV3 brings Plate to ≤ 1.5 × Hall | 1.60–1.72 × |
| PM multirate, first design | the fit was badly posed (310 equivalents); refitted, it is still more work than the reference |

---

## 1. Stage 5 final baseline

Stage 5 is `77f32fe` (hash record `f1d54fb`). DSP `--fast` 2574 / 2592 with
the 18 preserved failures; Stage 5 heavy suite 2 / 11 with 9 FINDING
failures; state 26 / 26; `Source/` unchanged. Standing structures entering
Stage 6: Hall; Plate A (240 sections, NOT REFUTED, 3–4.6 × Hall); Room R4c
(SURVIVES, fails G3 on Size, stereo / mono, and rings); Ambient A1b
(SURVIVES, fails G3 on tone, 1.82 dB); no Chamber.

## Part 1 — Room

### 2. Size-repair candidates

R4c's exchange time is 0.12 × Decay with no Size in it. Criterion: t10 not
falling, t10(90) ≥ 1.25 × t10(10), centroid rising at every step. Hall: × 1.79.

| law | t10 ratio | verdict |
|---|---|---|
| R4c | 1.08, falls | fails |
| RS1 exchange × s | 1.21 | fails |
| RS2 exchange × s² | 1.37 | centroid does not rise |
| RS3 block × s | 1.45 | centroid does not rise |
| RS5 exchange × s, block × s | 1.54 | centroid does not rise |
| RS6 exchange × s², block × s | 1.69 | centroid does not rise |
| **RS7** exchange × s, block × s² | 2.26 | **passes**; Hall at five Sizes 8.55 / 10.58 / 8.29 / 2.27 / 2.35; G1 5.08 / 8.99 / 1.72 |
| **RS8** exchange × s, block × s^1.5 | 1.78 | **passes**; Hall at five Sizes 8.31 / 8.62 / 7.63 / 3.06 / 1.59; G1 5.08 / 8.89 / 2.40 |

The pre-registered choice (highest lowest run) is RS7. Both laws lose most of
their distance from Hall at the top of the Size range: the block grows until
it is no longer a small block.

### 3. Ringing-repair candidates

Modal prominence, worst of three points. Hall 13.8 dB; clean ≤ 16.8;
materially reduced ≤ 27.8.

| candidate | dB | G1 (medium / small / large) |
|---|---|---|
| R4c | 42.4 | — |
| RR1 allpasses in the block | 35.1 | 2.89 / 5.03 / 1.71, passes (record only) |
| RR2 delays respaced | 41.8 | — |
| RR3 two clusters | 44.2 | — |
| RR4 Householder block | 40.8 | — |
| RR5 unequal exchange | 42.6 | — |
| RR6 eight local lines, open tank | 30.6 | 2.48 / 3.52 / 1.41, REJECTED (record only) |
| RR7 eight local + allpasses | 23.1, materially reduced | 1.41 / 1.56 / 2.00, **REJECTED** |
| RR8 sixteen local + allpasses | 17.4, materially reduced | 1.74 / 1.06 / 2.24, **REJECTED** |

**No ringing repair.** Nothing was darkened; tone and damping are untouched in
every candidate. The identity goes somewhere between 35 and 31 dB.

### 4. Stereo / mono-repair candidates

Only the block's injection and readout vectors change; Hall is not touched.

| | L/R | mirror | worst mono tone | octave | G1 lowest |
|---|---|---|---|---|---|
| Hall | 0.4 | 0.3 | −15.0 | 0.00 | — |
| R4c | 1.3 | −1.3 | −23.3 | 0.77 | — |
| RM1 mid ± side | 0.0 | 0.3 | −12.9 | 0.54 | 3.65 |
| RM2 generic | −0.0 | 1.0 | −19.0 | 0.63 | worse than Hall, not taken on |
| RM3 safety only (of 2304) | −0.1 | 0.0 | −3.6 | **1.23, over** | 3.55 |
| **RM4** safety and tone | −0.2 | −0.0 | −4.2 | 0.52 | 3.71 |

RM4: in L +−++ R −−+−, out L +++− R −+++. **Repaired.**

### 5. Best repaired Room

Two composites, each judged against the pass condition.

| condition | R5b = R4c + RM4 | R6 = R5b + RS7 |
|---|---|---|
| centred distinct, not Hall at any Size | **yes** — SURVIVES 5.78 / 6.37 / 3.71, lowest run 3.44; five Sizes lowest 2.08 | **NO** — REJECTED 5.78 / 8.00 / 1.90, lowest run 1.80; five Sizes lowest 1.66 |
| Size monotonic and meaningful | **NO** (t10 × 1.10, falls) | yes (× 2.29) |
| Decay within 10 % | yes (0.999–1.005) | yes |
| modal prominence materially reduced | **NO** (42.1 dB) | **NO** (42.1 dB) |
| stereo and mono | yes | yes |
| octave bands within 1 dB | yes (0.52) | yes (0.52) |
| CPU ≤ 1.5 × Hall | yes (1.05) | yes (1.03) |

**Room is BLOCKED.** R5b is the Room that goes on to the comparisons; it is
R4c with its stereo, mono and tone repaired and nothing else.

Not measured: a composite with RS8 through G2. The rule picks one law and it
picked RS7. RS8's single G1 run at large / long is 2.40 and its Size-90 search
is 1.59, so there is no reason to expect it to pass, but it was not run with
five seeds and this report does not claim it.

### 6. Room Hall-mimic result

Hall's seven controls against R5b, five seeds: 5.78 / 6.37 / 3.71, no run
under 3.44. Stopping family "modal structure" at all three points — the
ringing. Hall cannot reproduce R5b; what it cannot reproduce is the defect.

## Part 2 — Ambient

### 7. Tonal-deviation root cause

Located, not explained.

- It is one band: 125 Hz is 1.82 dB low; every other octave is within 1 dB.
- Not loss accumulation: damping off, 2.04 dB.
- Not the diffuser (1.43–1.55), the modulation (1.39–1.72) or the stage
  assignment (1.82–2.07).
- It grows through the tail (first 0.3 s 1.25 dB; 0.3–1.2 s 2.63 dB).
- Only 0.6–0.8 % of the energy held in the tank is in the heard stage.
- D7, the test of the second hypothesis (the deficit shrinks as Size grows):
  125 Hz at Size 10 / 30 / 50 / 70 / 90 is −2.43 / +1.67 / −2.19 / −0.87 /
  +0.43 dB. The suite prints "it does". Read plainly, the ends agree with the
  hypothesis and the middle does not; this is weak support. AT7, the repair
  that follows from the hypothesis, made things worse.

What the repairs show: the deficit is a property of *hearing only energy that
has migrated through every stage*. Letting 8 % of the input reach the heard
stage directly removes it. That is the state-energy distribution and the
readout timing together; the mechanism by which low frequencies migrate less
is not established.

### 8. Correction candidates

| | defaults | worst of 27 | Hall's search (medium / small / large) |
|---|---|---|---|
| A1b | 1.82 | 3.02 | SURVIVES 9.77 / 4.00 / 15.33 |
| AT1 damping compensation | not built: the diagnosis does not point there | | |
| AT2 every stage heard | 0.66 | 1.51, over | — |
| AT3 25 % direct | 0.18 | 0.73 | **REJECTED** 4.03 / 1.98 / 5.97 |
| AT4 stage reassignment | not built: under 0.3 dB of effect | | |
| AT5a 8 % direct | 0.24 | 0.62 | **REJECTED** 3.97 / 2.06 / 4.63 |
| AT5b 12 % direct | 0.21 | 0.66 | **REJECTED** 4.01 / 1.89 / 5.26 |
| AT5c 16 % direct | 0.20 | 0.68 | **REJECTED** 4.11 / 1.99 / 5.55 |
| AT6 last two stages heard | 1.35 | 2.45, over | — |
| AT7 heard stage of four lines | 1.30 | 3.69, over | — |

No output equaliser and no Character-style filtering was a candidate. The
four that fix tone pass G3 (Decay within 1.1 %, stereo and mono, CPU
0.94–1.05) and are rejected for a median under 2.5 at small / short.

### 9. Best repaired Ambient

**None.** A1b remains the structure that stands, with its 1.82 dB defect.
The best tone is AT5a (0.62 dB over the grid, inside the preferred 0.75); it
is not Ambient at small / short.

### 10. Ambient Hall-mimic result

A1b against Hall's controls (three seeds, log 05 matrix; five seeds in Stage
5): 9.77 / 4.00 / 15.33, lowest run 3.56. Unchanged from Stage 5.

## Part 3

### 11. Room against Ambient, both directions

Five seeds, three points, centred, visible controls only, the impostor
following its own knobs.

| target | searched by | medium | small | large | lowest run |
|---|---|---|---|---|---|
| Room R5b | Ambient A1b's controls | 5.06 | 4.71 | 5.40 | 4.56 |
| Ambient A1b | Room R5b's controls | 126.73 | 4.13 | 270.71 | 4.10 |

**DISTINCT in both directions.**

The separating dimensions, from the family residuals:

- Ambient cannot make Room's *echo density, modal structure and buildup*
  (4.5–5.4 left at every point): a small, sparse, fast block.
- Room cannot make Ambient's *late texture and energy profile* at medium and
  large (two and three orders above the threshold) and its *stereo* at small
  / short (4.10): energy that arrives late because it has had to migrate.

Two cautions. These are the G3-blocked structures, not repaired ones; and
part of Room's side of the separation is the modal structure that section 3
could not remove.

## Part 4 — Plate

### 12. Multirate feasibility

**Not feasible as designed.** The filter bank is sound (reconstruction −331
dB with the chains out; images −86 dB or lower). Fitted for the fewest
sections within 0.25 ms rms:

| | bands | section-equivalents (reference 240) | ripple | extra latency | × Hall |
|---|---|---|---|---|---|
| PM1 | 242 @ −0.25 at rate / 4; 230 @ −0.64 at full rate | 290.5 | 0.44 dB | 6.8 ms | 5.45 |
| PM2 | 264 at rate / 8; 234 at rate / 2; 179 at full rate | 329.0 | 1.06 dB | 26.6 ms | 6.20 |

Both are more work than the reference and are a different response (PM1 1.93
/ 1.91 / 6.07 from the reference). My prediction was about 140 equivalents;
the fit came out at twice that, and the pre-registration says that is to be
reported as my fit, not as a property of multirate. The honest statement:
this attempt failed and does not settle whether a better design exists.

### 13. SIMD / parallel feasibility

**Feasible, and the result of the Stage.** The reference chain is bound by
latency, not arithmetic. Cut into sub-chains with one sample of delay between
them, the same 240 sections run side by side. The transfer function is the
reference's times a pure delay of (sub-chains − 1) samples; on noise the
difference is zero to twelve decimal places for every form.

| form | sub-chains | chain speed against the reference |
|---|---|---|
| PV1 scalar | 4 / 8 / 16 | 2.00 / 2.52 / 2.64 × |
| PV2 two-wide, through memory | 4 / 8 / 16 | 2.36 / 2.92 / 4.97 × |
| PV3 two-wide, in registers | 4 / 8 / 16 | 4.77 / 4.85 / 6.72 × |

These timings are noisier than the ones in logs 03 and 04 (the reference
chain itself read 771–1241 ns in this run; in log 04 PV3 with eight
sub-chains read 6.76 ×, here 4.85 ×). The machine was in use. The ordering is
stable; single figures are not.

### 14. Alternative topology candidates

Dispersion inside the loop (PT1, 2 / 4 / 8 sections per line) and hybrids
with a short chain ahead (PT2). Cheap, 1.09–1.36 × Hall, and **all six
REJECTED at G1** (lowest runs 0.51–1.11). Not a Plate.

### 15. Best Plate engineering candidate

**PV3, two-wide in registers, 16 sub-chains.** 15 samples (0.3 ms) of added
delay; otherwise the 240-section reference.

### 16. Plate-to-reference similarity

0.35 / 0.16 / 0.33 at the same knobs: **PRESERVED** (limit 1.5). Delay by
band is the reference's plus 0.3 ms in every band; click peak and span
identical. Decay, tone, stereo and mono equal the reference's to the printed
digit.

### 17. Plate-to-Hall distinctness

Hall's search, five seeds: **3.38 / 3.09 / 3.13, lowest run 2.72 — SURVIVES**,
and by Stage 5's rule (no median under the reference's own ten-seed minimum
2.95 / 2.54 / 2.78) **REFERENCE-EQUIVALENT**. The margin over 3.0 is small.
In the other direction, Hall as target against Plate's controls is NOT
REFUTED (2.71 / 2.61 / 3.08); the pre-registration reports that and does not
hold it against Plate.

### 18. Plate CPU

| measurement in log 05 | PV3, 16 | reference |
|---|---|---|
| candidate table | 1.60 | 4.35 |
| G3 table | 1.61 | 4.40 |
| engineering table | 1.72 | — |

**Inside the 1.75 × acceptable limit in all three; the preferred 1.5 × is not
met.** The highest reading leaves three hundredths of margin, on a machine
that was in use. PV1 (scalar, eight sub-chains) read 1.71 here and 1.77–1.87
in log 03. G3 passes at 1.75.

## Part 5 — Chamber

Results from log 03. Part C was not run again (instruction of 2026-11-18).

### 19. Three pre-registered concept families

K1 a scattering delay network (walls and the paths between them); K2 nested
recursive diffusion in a single loop; K3 a scattering lattice. Two fixed
variants each. All built in an open tank whose control holds: built as Hall
it is Hall (−310 dB) and Hall's search returns 0.00.

### 20. Concept results

| | G1 (medium / small / large) | G2 medians | lowest run | verdict |
|---|---|---|---|---|
| K1a wall network, 4 walls | 1.62 / 1.32 / 3.44 | — | 1.32 | REJECTED at G1 |
| K1b wall network, 5 walls | 2.30 / 1.19 / 2.54 | — | 1.19 | REJECTED at G1 |
| K2a nested ring, 4 | passes | 1.84 / 2.12 / 3.10 | 1.75 | REJECTED |
| K2b nested ring, 6 | passes | 2.10 / 1.56 / 5.22 | 1.28 | REJECTED |
| K3a lattice, edge to edge | 2.84 / 1.48 / 2.50 | — | 1.48 | REJECTED at G1 |
| K3b lattice, to corners | passes | 2.50 / 1.68 / 3.66 | 1.39 | REJECTED |

The pattern is the same in all three: some distance at large / long, collapse
at small / short.

### 21. Best Chamber candidate

**None.**

### 22. Chamber cross-Mode mimic result

Not run: there is no Chamber to put in the matrix.

## Across the surviving families

### 23. Cross-mimic matrix

Hall, Room R5b, Ambient A1b, Plate PV3. Three seeds, three points, centred.
Each cell: lowest run / lowest median.

| target \ impostor | Hall | Room | Ambient | Plate |
|---|---|---|---|---|
| Hall | — | 2.08 / 2.15 | 3.85 / 3.99 | 2.56 / 2.61 |
| Room | 3.66 / 3.71 | — | 4.56 / 4.67 | 3.69 / 3.71 |
| Ambient | 3.56 / 4.00 | 4.13 / 4.13 | — | 3.54 / 3.58 |
| Plate | 3.09 / 3.13 | 3.14 / 3.18 | 3.44 / 3.52 | — |

No family other than Hall is reproduced by another's controls; every
non-Hall row is DISTINCT (medians ≥ 3.0). The Hall row is the stated
exception: Room's controls reach 2.15 of Hall at small / short (REJECTED as a
target, by the numbers) and Plate's 2.61 (NOT REFUTED). Reported, not held
against them.

### 24. Size semantics

| | t10 at Size 10 … 90, ms | |
|---|---|---|
| Hall | 36.2 … 64.9 | × 1.79 |
| Plate PV3 | 41.2 … 70.5 | Hall's law, pass |
| Ambient A1b and AT5 | 37.5 … 81 | monotone, pass |
| Room R5b | 27.1, 26.9, 27.0, 27.3, 29.8 | **not monotone, × 1.10: fail** |
| Room R6 | 23.6 … 54.2 | passes, and is not a separate Mode |

### 25. Decay semantics

Measured RT60 against Hall's over nine settings: Room R5b 0.999–1.005; Plate
1.004–1.024; Ambient repairs 0.998–1.011. All inside their limits.

### 26. Stereo / mono safety

Room R5b at the three points: balance within 0.2 dB, mirror 0.0, fold-down
−3.0, worst mono tone −4.2 / −10.1 / −3.7 dB (Hall −15.0 / −10.9 / −9.3).
Plate equals Hall. Ambient: correlation −0.10, fold-down −3.7, worst mono
tone −11.7. All inside Stage 3's limits.

### 27. Tonal neutrality

Largest octave-band deviation from Hall at the defaults: Plate 0.00; Room R5b
0.52; Ambient A1b **1.82** (3.02 over the grid); AT5a 0.24 (0.62 over the
grid) but rejected.

### 28. CPU / memory

| | wet path × Hall | memory KiB | configure × Hall |
|---|---|---|---|
| Hall | 1.00 | 498 | 0.96 |
| Room R5b | 0.96 | 498 | 1.34 |
| Ambient A1b | 1.03 | 498 | 1.26 |
| Plate PV3 | 1.72 | 501 | 1.36 |

Two networks at once during a switch: 1.96–2.03 × Hall without Plate,
2.68–2.74 × with it.

### 29. Switching implications

Level of a network pre-warmed for a given time, against the same network
running all along, over the 120 ms crossfade:

| | setting | 0.4 s | 0.8 s | 1.6 s |
|---|---|---|---|---|
| Hall | 60, 2.5 s | −0.2 | 0.0 | 0.0 |
| Hall | 85, 5.0 s | **−1.1** | −0.2 | 0.0 |
| Room | 85, 5.0 s | −0.8 | −0.3 | 0.0 |
| Ambient | 60, 2.5 s | −0.5 | 0.0 | 0.0 |
| Ambient | 85, 5.0 s | **−3.9** | −0.7 | −0.2 |
| Plate | 85, 5.0 s | **−1.1** | −0.2 | 0.0 |

**400 ms of pre-warm is not enough at long Decay** for Hall, Plate and
Ambient; 800 ms is enough for every structure measured. The reference
(400 ms + 120 ms) should become Decay-dependent or 800 ms. Nothing was
productionised.

### 30. Phase-13 compatibility

Nothing here changes brightness, saturates, or filters for an era; no
candidate touched a damping law (AT1 was not built). Character remains free
to act on any of these structures. PV3 changes no sound at all.

### 31. Surviving Mode status

| | G2 (structure) | G3 | counts as a survivor |
|---|---|---|---|
| Hall | — | — | **yes** |
| Plate PV3 | SURVIVES, reference-equivalent | pass at 1.75 × | **yes, at the acceptable CPU tier** |
| Room R5b | SURVIVES | fails Size; rings | no |
| Ambient A1b | SURVIVES | fails tone | no |
| Chamber | — | — | no mechanism |

Two of five. Final perceptual acceptance is not mine to give for any of them.

### 32. Unresolved blockers

1. **Room: ringing and identity are one measurement.** No construction
   separates them.
2. **Room: Size.** The laws that make Size meaningful move the structure to
   Hall at large / long.
3. **Ambient: tone against identity at small / short.** Any directly heard
   input repairs the tone and collapses the distance.
4. **Ambient: the root cause of the 125 Hz deficit is not established.**
5. **Chamber: nothing.** Fourteen negative constructions.
6. **Plate: 1.60–1.72 ×, not 1.5 ×**, measured in the research rig with
   noisy timing; and its margin over Hall is thin (medians 3.09–3.38).
7. **Switching: 400 ms is short at long Decay.**

### 33. Recommended Stage 7 action

A decision before more construction, because two of the blockers are not
engineering defects but findings about the metric and the families:

1. **Listen.** R5b's 42 dB of modal prominence is either a small room's
   colour or a defect; the metric cannot say which, and the Room identity
   depends on it. The same for A1b's −1.8 dB at 125 Hz against AT5a. These
   are human judgements (Stage 4's protocol).
2. **Decide the Mode count honestly.** The evidence supports Hall and Plate;
   Room and Ambient as distinct from each other and from Hall but each with
   an unrepaired defect; no Chamber. Five Modes is not supported.
3. If Room continues: a mechanism whose identity does not rest on a few
   prominent modes — the small-block family appears exhausted. One
   unmeasured item is cheap: the RS8 composite through G2.
4. If Ambient continues: work at small / short specifically, where every
   repair fails and A1b itself has least margin (4.00).
5. Plate: confirm the cost of PV3 in a quiet, production-like measurement
   before relying on 1.75 ×.
6. Switching: re-specify the pre-warm as a function of Decay.

Not recommended: more Chamber concepts on the same metric without a new idea
of what a Chamber is that Hall's controls cannot reach at small / short.

### 34. Tests added

- `Tests/Phase12Stage6Rig.h` — the open tank, the pipelined chain, the
  multirate bank and its fit.
- `Tests/Phase12Stage6.h`, `Phase12Stage6.cpp`, `Phase12Stage6Parts.cpp` — the
  heavy suite `runPhase12Stage6Tests`, sections 12.6.1–12.6.15;
  `AV_S6_PART` (any of `R A X P C M E`) runs parts of it.
- `Tests/Phase12Stage6Fast.cpp` — 12.6.0, nine checks: the builders are
  Stage 5's R4c and A1b; every pipelined form is the plain chain delayed;
  every open-tank matrix is orthogonal and the open tank built as Hall is
  Hall; the multirate bank with its chains out is a pure delay.
- Inert-by-default options in `Phase12Stage3Rig.h`, `Phase12ResearchCore.h`
  and Stage 5's headers; 12.5.0's fingerprints hold them to that.

### 35. Production Source status

Unchanged. No Mode parameter, no topology, no baseline; the legacy `mode`
parameter is untouched; `AV_COPY_PLUGIN_AFTER_BUILD` is OFF.

### 36. Regression status

Final validation on the code as committed:

| run | result |
|---|---|
| DSP, `--fast` | **2583 / 2601**, 18 failures — the 18 preserved ones (Phase 7, Phase 10 and Phase 11 documented findings), in the same 16 suites as at Stage 5. The nine new checks are 12.6.0's. **No new regression.** |
| Stage 6 heavy suite, parts R A X P M E in one process (log 05) | 12 / 17, **5 FINDING failures**: no ringing repair; no Room through 2.4; the first Ambient hypothesis; no Ambient repair through G2; 400 ms of pre-warm |
| Stage 6 heavy suite, part C (log 03) | Chamber: control passes, **1 FINDING failure** (no concept through G2) |
| State, `AuroraVerbStateTests` | **26 / 26** |
| `git diff 77f32fe -- Source` | empty |

**What was not done, stated plainly.**

- **Stage 6 was not run as one process.** Chamber's result is from log 03,
  run before the checkpoint and not repeated, by instruction. Everything
  else is from one process (log 05, 7218 s). Because Chamber was absent,
  section 12.6.15 (the status checks) did not print; the status table in
  section 31 is assembled by hand from the two logs.
- The Stage 2, 3, 4 and 5 heavy suites were **not** re-run after the rig
  edits. The evidence that the edits did not change them is 12.5.0: the
  fingerprints of the structures those suites use, taken before Stage 5,
  still match. That is strong evidence and not the same as re-running them.
- Timing figures in log 05 were taken on a machine that was in use; see
  sections 13 and 18.

**Reproducibility across the interruption.** Every search residual that
appears in both the earlier logs and log 05 is equal to the printed digit
(R4c, RS7, RR7, RR8, RM4, R6, AT3 at G1, PV1's G2). AT3's G2 medians read
4.03 / 1.98 / 5.97 in both.

### 37. Commit

Stage 6 is commit `41a2d3e`; this line was added by the commit after it.
