# Phase 12 — Stage 6: pre-registration

**Research only.** `Source/` is not changed. No Mode parameter, no production
topology, no new baseline. Hall is the frozen Phase-11 engine.

This file is committed **before** any Stage 6 structure is measured. Whatever
is learned afterwards is appended as an addendum; nothing here is edited.

Stage 5 is closed at `77f32fe`. Its final baseline is the starting point:

| family | Stage 5 result |
|---|---|
| Hall | proven, frozen |
| Plate | 240-section reference NOT REFUTED; section-count reduction FAILED; 3–5 × Hall |
| Room | R4c SURVIVES G2 centred (5.69 / 6.38 / 4.06); fails G3: Size flat, modal prominence 42.4 dB (Hall 13.8), worst mono tone −23.3 dB (Hall −15.0), L/R 1.3 dB |
| Chamber | seven hypotheses, seven rejected |
| Ambient | A1b SURVIVES G2 (9.77 / 4.00 / 15.33); fails G3 on tone, 1.82 dB |

## 0. What is frozen

- The metric (13 families, scales, floor rule, worst-family verdict), the
  thresholds REPRODUCED ≤ 1.5 and DISTINCT ≥ 3.0, and the seven-control
  search: as frozen at `c18ecce`.
- Stage 5's gates, unchanged:
  - **G1** — one search per operating point, centred view (lateral family
    ignored in search and verdict). A run ≤ 1.5 anywhere is the end.
  - **G2** — five seeds per point. SURVIVES: no run ≤ 1.5 and median ≥ 3.0 at
    every point. NOT REFUTED: no run ≤ 1.5 and median ≥ 2.5 at every point.
    Otherwise REJECTED.
  - **G3** — Size t10 not falling over five Sizes; RT60 within 10 % of Hall
    over nine settings (Ambient 5 %) with an explicit calibration allowed;
    the Stage 3 stereo limits (worst mono tone ≤ 3 dB deeper than Hall,
    hard-placed ≤ +0.05, balance and mirror within 1 dB, fold-down within
    1.5 dB); octave bands within 1 dB of Hall.
- Operating points: medium / medium (60, 2.5 s), small / short (25, 0.8 s),
  large / long (85, 5.0 s). Seeds: Stage 5's.

A failed candidate stays failed. No threshold moves.

## 1. What Stage 6 adds to the gates

These are new *requirements*, each stricter than or additional to Stage 5.
None replaces a Stage 5 limit.

**1.1 Size must be meaningful, not only non-falling.** G3's Size test is kept.
In addition, for Room: t10 at Size 90 ≥ 1.25 × t10 at Size 10 (Hall's ratio is
1.79), and the temporal centroid of the response rises at every step.

**1.2 Ringing.** The measure is the frozen metric's modal prominence, worst of
the three points (Stage 5's quality table). Two lines, both written now:
*clean* ≤ Hall + 3 dB (Stage 5's flag); *materially reduced* ≤ Hall + 14 dB,
which is half of R4c's present excess of 28.6 dB. A repaired Room must be at
least materially reduced; it is reported against both.

**1.3 Not Hall again, at every Size.** A Room Size law is also searched by
Hall at Size 10 / 30 / 50 / 70 / 90 (Decay 2.5 s, one search each, centred).
A run ≤ 1.5 at any Size rejects the law.

**1.4 CPU.** *Practical* for Room, Ambient and Chamber is ≤ 1.5 × Hall on the
wet path, measured alternately with Hall. Plate's tiers are the brief's:
≤ 1.5 preferred, ≤ 1.75 acceptable, ≤ 2.0 only with exceptional evidence,
above 2.0 not a shipping target.

**1.5 Searches with a knob-dependent impostor.** Stage 5's adversarial gate
searched with a *fixed* structure as the impostor, so an impostor whose
matrix follows Size and Decay could not follow its own knobs (addendum 3 of
Stage 5 said so). Stage 6 searches with the impostor rebuilt at every
evaluation. The search algorithm is the frozen one, copied statement for
statement; a test holds the copy to the original, result for result, on a
fixed structure. An impostor's decay calibration is a smooth law fitted once
on a 4 × 4 grid of Size and Decay and interpolated — what a product would
ship — not a fit per evaluation.

**1.6 Order of work inside a repair.** A repair candidate is first measured
on the defect it is meant to repair. Only candidates that repair it go to G1,
and only those that pass G1 go to G2. This is to save searches, not to hide
anything: every candidate's defect measurement is printed.

## 2. Part 1 — Room: repairing R4c

R4c: the four shortest lines × 0.25 form a local block that is driven and
heard; the four longest form the field; the two exchange with
sin²θ = (mean local loop time) / (0.12 × Decay).

### 2.1 Why Size does nothing — the hypothesis

The local block's loop is a few milliseconds; what is heard early is how fast
energy leaves it, and that rate is 1 / (0.12 × Decay): it has no Size in it.
Size changes the local delays and the per-pass angle together and they
cancel.

| | change to R4c | |
|---|---|---|
| **RS1** | exchange time × (s / s₀) | s = size scale, s₀ its value at Size 60 |
| **RS2** | exchange time × (s / s₀)² | |
| **RS3** | local block scale × (s / s₀); exchange time unchanged | the block itself grows faster than the field |
| **RS4** | RS3 with the better of RS1 / RS2 | built only if both levers move Size on their own |

Measured for each, at Size 10 / 30 / 50 / 70 / 90: t10, temporal centroid,
buildup (time to 50 % of the energy), the local delay scale, a compactness
proxy (share of the energy in the first 80 ms), and Hall's search (1.3).

**Prediction:** RS1 makes Size move t10 and stays distinct at small and
medium; at Size 90 its exchange slows towards a plain two-stage bloom and may
come close to Ambient rather than Hall. RS3 alone moves little.

### 2.2 Ringing

Four short lines carry few modes. All candidates keep the damping and the
tone untouched.

| | change to R4c |
|---|---|
| **RR1** | two static allpasses inside each local line (37 % and 23 % of the line, g 0.5): more state in the block, same topology; the exchange is timed from the loop time including them |
| **RR2** | the local delays respaced: same mean, ratios 1 : 1.31 : 1.73 : 2.27 instead of Hall's own ratios |
| **RR3** | two local clusters of two lines (× 0.18 and × 0.36) on either side of the field, each with its own timed exchange; both driven, both heard |
| **RR4** | Householder scattering in the local block instead of Hadamard |
| **RR5** | the four exchange angles unequal (rates × 0.5, 0.8, 1.25, 2.0; same mean rate) |
| **RR6** | *conditional:* sixteen lines, eight local and eight field, in the open tank of section 6. Built only if no RR1–RR5 is materially reduced, because it leaves R4c's engine |

**Prediction:** RR1 and RR2 reduce it most; RR4 does nothing (at four lines
the two matrices have the same magnitudes). I do not expect *clean* from any
single one.

### 2.3 Mono and stereo

The 4-line sign vectors send a centred source into lines 0–1 and take the
mono sum from lines 2–3: one path through the block's matrix.

| | change to R4c |
|---|---|
| **RM1** | vectors built as mid ± side: mid {1,1,1,−1}, side {1,−1,1,1} in; mid {1,1,−1,1}, side {−1,1,1,1} out |
| **RM2** | four generic orthonormal vectors (Stage 3's), two in and two out |
| **RM3** | every orthogonal pair of ±1 vectors, in and out, scored on **safety only**: among those with balance and mirror within 0.5 dB, the one whose worst third-octave mono fold-down is shallowest. The distinctness metric plays no part in the choice |

Phase-11 Hall behaviour is not touched; only the block's vectors change.
Each is then gated, because the vectors are part of the identity.

### 2.4 The repaired Room and its pass condition

The best of each group is combined into one structure. It is a **Room
candidate** only if all of these hold:

- G2 SURVIVES, centred, and 1.3 at five Sizes;
- Size: G3 and 1.1;
- RT60 within 10 % of Hall;
- modal prominence at least materially reduced (1.2);
- G3's stereo and mono limits;
- octave bands within 1 dB;
- CPU ≤ 1.5 × Hall.

If the composite loses what a single repair had, that is reported and the
composite is not rescued by re-tuning against the gate it failed.

## 3. Part 2 — Ambient: why A1b is 1.82 dB off, and repairing it

### 3.1 Diagnosis, measured before any repair

| | measurement | separates |
|---|---|---|
| D1 | octave shares of A1b against Hall with Low Damp and High Damp both at 0 | **loss accumulation**: if the deviation vanishes, it is the damping acting on energy that is still migrating |
| D2 | octave shares in three time windows (0–0.3 s, 0.3–1.2 s, after 1.2 s), and RT60 per octave | **readout timing**: a deviation that grows or shrinks through the tail |
| D3 | D1 again with the input diffuser off | **diffusion topology** |
| D4 | octave shares held by each stage's lines (state energy, not output) | **state-energy distribution** |
| D5 | D1 with the stages reassigned ({0,1},{2,3},{4,5},{6,7} and {0,7},{1,6},{2,5},{3,4}) | **delay-time distribution** |
| D6 | D1 with Mod Depth 0 | the modulation allpass |

**Hypothesis, written now:** loss accumulation. Energy is heard only after
it has migrated through the stages, about 0.4 × Decay; the bands the damping
shortens lose more on the way than they do in Hall, where everything is heard
at once. D1 should remove most of the deviation.

### 3.2 Repairs

| | change to A1b | allowed when |
|---|---|---|
| **AT1** | the damping ratios asked of the tank raised to a power γ < 1 (one γ per shelf), fitted so the octave shares match Hall's at the defaults: a calibration of the damping law, the same kind of thing as the decay calibration | D1 points to loss accumulation |
| **AT2** | every stage heard, weights rising toward the last | always; identity is at risk and G2 is re-run |
| **AT3** | a share of the input sent straight into the last stage | always; same risk |
| **AT4** | a different stage assignment (D5's) | D5 shows the delay distribution matters |

**Not a candidate:** a static equaliser or tilt on the output. That is tone
applied from outside the structure and belongs to Character.

**Pass:** octave bands within 1.0 dB of Hall (preferred 0.75) at the defaults
*and* over Size 25 / 60 / 85 × Decay 0.8 / 2.5 / 5.0 and three damping
settings (default; both 0; High Damp 80 %); G2 still SURVIVES; RT60 within
5 %; G3 otherwise.

## 4. Part 3 — Room against Ambient

Both exist by the same rule, an exchange timed from Decay. That is exactly
why this test is mandatory.

- The pair is whatever stands after Parts 1 and 2: the repaired structure
  where a repair passed G2, the Stage 5 structure (R4c, A1b) where it did
  not.
- Both directions, knob-dependent impostor (1.5), all seven controls, three
  operating points, five seeds, centred source.
- **A run ≤ 1.5 in either direction: they are not justified as two Modes.**
  Medians ≥ 3.0 at every point in both directions: DISTINCT. Anything
  between: NOT REFUTED, reported as such and not claimed.
- If distinct, the separating dimension is reported: the worst family at the
  best residual, the full family table at that point, and the impostor's
  knobs there.

**Prediction:** distinct in both directions on buildup and energy profile
(Room is heard at once and thins; Ambient is heard late), with the smallest
margin at small / short where both exchange times are shortest.

## 5. Part 4 — Plate: a different engineering family

Section-count reduction is closed. The reference is 240 first-order sections
at −0.7 per channel ahead of the tank.

**Why the reference costs what it does — the hypothesis.** Each section's
output is the next section's input, so the processor cannot overlap them: the
chain is bound by latency, not by the amount of arithmetic. 4.2 ns a section
is about what one multiply-add dependency costs. If that is right, the same
arithmetic rearranged so sections do not wait for each other is several
times cheaper *with the same transfer function*.

| | candidate | transfer function |
|---|---|---|
| **PV1** | the 240 sections cut into C sub-chains (C = 4, 8, 16), each fed the previous one's output from one sample earlier, evaluated side by side | reference × z^−(C−1): identical dispersion, at most 0.3 ms of delay |
| **PV2** | PV1 with left and right in one two-wide vector | same |
| **PM1** | multirate, two bands: below 3 kHz at a quarter of the rate, the rest at full rate; linear-phase split, complementary reconstruction | fitted to the reference's group delay |
| **PM2** | multirate, three bands (1.5 kHz, 6 kHz), at ⅛, ½ and full rate | fitted |
| **PT1** | dispersion inside the loop only: 2, 4, 8 sections per line | a different dispersion: it accumulates per pass |
| **PT2** | hybrid: 48–96 sections ahead plus 2–4 per line in the loop | different |

Second-order and lattice allpass sections are not built: a section of order
*n* carries *n*π of phase whatever its form, and Stage 5 measured that the
phase is what cannot be given away. This is stated as an argument, not as a
measurement.

**Predictions.**
- PV: reference-equivalent to the metric's resolution, and the chain's cost
  falls by at least 3 ×, which puts Plate at or under 1.75 × Hall.
- PM: a band sampled with a safe margin (twice its width) needs the phase
  that lies in it, and 46 % of the reference's phase lies above 3 kHz. I
  expect PM1 near 2.5–3 × and PM2 near 2 ×, with aliasing to account for;
  neither should beat PV.
- PT: cheap, and not the reference. Whether it is a *Plate* at all is what
  the comparison with the reference says.

**Measured for every candidate:** distance from the reference at the same
knobs (three points); distance from Hall; the group-delay curve against the
reference; the click (peak, length) through the chain; for PM, reconstruction
error with the chains bypassed and the alias level on a sine set; wet-path
cost. **Reference-equivalent** is Stage 5's rule: within 1.5 of the reference
at the same knobs at every point, and through Hall's G2 with no run ≤ 1.5 and
no median below the reference's own lowest. The best candidate is the
cheapest reference-equivalent one; if there is none, the cheapest that
passes G2 against Hall is reported as *a different Plate*, not as the
reference.

## 6. Part 5 — Chamber: three new concepts

Not built: reflections fed to the tank, coupled volumes, reflection rings,
fixed-angle exchange, or timed variants of C1–C3.

None of these fits in eight lines with one mixing matrix, so they are built
in a second research tank, the **open tank**: any number of lines, a sparse
scattering matrix, static and nested allpasses inside a line, delayed feeds
and delayed or interior readouts. Decay gain, damping shelves and modulation
are production's own classes applied per line as the research core applies
them.

**The control that reads the open tank.** Hall's own eight lines, matrix and
vectors, built in the open tank. Hall's search must REPRODUCE it (median
≤ 1.5 at every point). If it does not, the open tank differs from the
research core in some way that is not structure, and no Chamber result from
it may be called SURVIVES; it is reported as CONFOUNDED.

### K1 — a scattering delay network

- **Hypothesis.** A room is walls. Sound goes source → wall → wall → … →
  listener; each wall scatters what arrives to every other wall. The early
  reflections and the late field are then one network, and the first
  reflections arrive at times fixed by geometry instead of all lines being
  driven at once.
- **Topology.** Four wall nodes, twelve directed lines (one per ordered pair
  of walls). Each node scatters its three arrivals with (2/3)·11ᵀ − I. Each
  input channel reaches each node through its own delay; each output channel
  hears each node through its own delay. K1b: five nodes, twenty lines.
- **Why Hall's controls cannot reproduce it.** Hall drives eight lines at the
  same instant and mixes all of them every pass. Here a line feeds only
  three others and the network is entered and left at eight different times.
- **Why not Room.** No local block, no exchange timed from Decay; one time
  scale, set by Size.
- **Why not Plate.** No dispersion.
- **Expected failure.** Hall's Early/Late and its reflection pattern supply
  the staggered arrivals, the late field is statistically the same, and
  Hall reproduces it: reflections-into-a-tank a fourth time, in disguise.
- **Cost.** 12 lines, 36 multiplies of scattering: about 1.2–1.5 × Hall.
  K1b about 2 ×.

### K2 — nested recursive diffusion in a single loop

- **Hypothesis.** A chamber's liveliness is density that is *made by the
  recursion itself*: every trip round passes through diffusers again, so
  density multiplies per trip instead of being supplied once at the input.
- **Topology.** No parallel lines and no mixing matrix. Four delay segments
  in one ring (a figure of eight: left enters one side, right the other),
  each segment preceded by an allpass that has another allpass nested inside
  its delay. Outputs are signed taps inside the segments. K2b: six segments.
- **Why Hall's controls cannot reproduce it.** Early Diffusion acts once,
  before the tank. Hall has no control that puts diffusion in the loop or
  turns eight parallel lines into one.
- **Why not Room.** No second time scale and no exchange.
- **Why not Plate.** The allpasses are long and sparse, not a dispersion
  curve; arrival does not depend smoothly on frequency.
- **Expected failure.** One loop has one period. It is audible as
  repetition (the quality table's flag), so it is distinct *and defective*;
  or Hall at Early Diffusion 100 with a matching Size reproduces it.
- **Cost.** Four lines, twelve allpasses: under Hall.

### K3 — a scattering lattice

- **Hypothesis.** In K1 every wall sees every other wall. In a lattice a
  junction sees only its neighbours, so energy has to *travel*: density
  grows as the reached area grows, not exponentially, and two listening
  points hear the same event at different times. Medium scale, dense, and
  still interacting late.
- **Topology.** 3 × 3 junctions, links between neighbours, 24 directed
  lines of unequal length. A junction of degree d scatters with
  (2/d)·11ᵀ − I. Left and right enter at two junctions on one edge and are
  heard at junctions on the opposite edge. K3b: entered at the middle of two
  opposite edges, heard at the four corners.
- **Why Hall's controls cannot reproduce it.** Hall has no distance inside
  it: every line is one mixing step from every other.
- **Why not Room.** Room is two domains with one timed valve. This is one
  medium with transport.
- **Why not Plate.** No dispersion curve.
- **Why this is the risky one for Ambient.** Ambient is also "heard after it
  has travelled". The difference claimed is that Ambient's travel time is
  Decay's and this one's is Size's. The cross-mode matrix tests it; a
  collapse there rejects it.
- **Expected failure.** Reproduced by Ambient's controls, or by Hall with
  Attack raised.
- **Cost.** 24 lines: about 2 × Hall. Over the practical limit unless the
  per-line cost is much lower than I expect.

**Gates for Chamber:** the control; G1; G2; then, for whatever passes, the
other families' own searches (section 7) — a run ≤ 1.5 by any of them
rejects it; then G3 and 1.4.

## 7. Part 6 — the cross-mode matrix

Every family that stands after Parts 1–5 (Hall, Room, Ambient, Plate,
Chamber where there is one), each as target and each as impostor: every
ordered pair, three operating points, three seeds, centred, knob-dependent
impostor. The Hall-as-impostor row is G2 itself (five seeds).

A run ≤ 1.5 anywhere in a pair means the target is not a separate Mode from
the impostor. The pair is DISTINCT with every median ≥ 3.0, otherwise NOT
REFUTED. One exception is stated in advance: *Hall as a target reproduced by
another family's controls* is reported but does not reject that family — a
Mode able to sound like Hall is not a defect; a Mode Hall can sound like is.

## 8. Parts 7–9

- **CPU and memory.** Wet-path cost against Hall for every standing
  structure; delay and state memory in KiB at the largest Size; the time to
  configure the structure from nothing; and the cost of running two
  networks at once during a switch.
- **Switching.** The reference stays 400 ms of pre-warm and a 120 ms
  crossfade. Measured for every standing structure: the level of a network
  started 400 ms before the switch, against the same network that had been
  running throughout, on continuous noise, over the crossfade. More than
  1 dB short means 400 ms is not enough for that structure, and the time
  that would be is reported. Nothing is productionised.
- **Character.** No candidate here changes brightness, adds saturation, or
  filters for an era. AT1 is the only one that touches a damping law, and
  only to return the structure to Hall's octave balance.

## 9. What counts as done

Stage 6 succeeds only with: a Room that passes 2.4; an Ambient that passes
3.2; Room and Ambient distinct from each other; a Plate at ≤ 1.75 × Hall
that is the reference or is honestly a different Plate; and either a
Chamber through every gate or a clear statement that there is none. Anything
less is reported as what it is.

---

## Addendum 1 — after Part 1 (Room) was read once, before Room is run again

Appended; nothing above is edited.

**What the first run found.**

- **The search copy** (1.5) returns the frozen search's result knob for knob.
- **Size:** no law passes 1.1. RS2 and RS3 make t10 rise (× 1.37 and × 1.45,
  not falling) and fail on the centroid, which does not rise. RS4 was not
  built: my rule required each lever to pass on its own.
- **Ringing:** nothing reaches *materially reduced* (≤ 27.8 dB). R4c 42.4;
  RR1 35.1; RR2 41.8; RR3 44.2; RR4 40.8; RR5 42.6; RR6 (sixteen lines) 31.0.
  My prediction that RR1 and RR2 would reduce it most was half right: RR2 does
  nothing.
- **Stereo and mono:** solved. RM3 (in L ++−+ R +−++, out L +++− R −+++):
  worst mono tone −3.6 dB (R4c −23.3, Hall −15.0), balance −0.1 dB, mirror
  0.0 dB, and inside the limits at all three operating points. G2 SURVIVES
  (5.79 / 6.41 / 3.73, lowest run 3.42).
- **The composite R5** (= R4c + RM3) fails 2.4 on Size, on ringing, and on
  **tone: 1.23 dB**, where R4c had 0.77. The vectors that fix mono move the
  octave balance. R5 is not rescued; it stays failed.

**Something the first run makes plain and I had not written down.** In all
fifteen of R5's runs but one, the family Hall stops on is *modal structure*.
The measure that says "this is not Hall" and the measure that says "this
rings" are largely the same measure. A repair that removes the ringing may
remove the identity with it. That is now the main risk of this part, and it
is why every ringing candidate below goes through G1 and G2 before anything
is said about it.

### New Size hypothesis

A two-rate model, written before measuring. Energy leaves the heard block at
rate 1/τ and comes back from the field at rate 1/(τ·R), R being the ratio of
the field's loop time to the block's. With total decay T, the heard centroid
is lowest (≈ 1/(1/τ + 1/T)) when escape is fast and return is slow, and
highest (≈ T) when the block keeps its energy. In R4c neither τ nor R depends
on Size, which is the defect. RS1/RS2 change τ only, so at small Size both
escape *and* return speed up and the centroid goes back up: not monotone, as
measured. RS3 changes R only.

For a centroid that rises with Size, small Size must mean *fast escape and
slow return* and large Size *slow escape*: τ ∝ sᵖ **and** block scale ∝ s^q
together, which is the combination my rule declined to build.

| | exchange time | local block scale |
|---|---|---|
| **RS5** | × (s/s₀) | × (s/s₀) |
| **RS6** | × (s/s₀)² | × (s/s₀) |
| **RS7** | × (s/s₀) | × (s/s₀)² |

Criteria 1.1 and 1.3 unchanged. **Risk stated now:** at large Size the block
holds its energy and becomes a four-line comb — more ringing, not less — and
at small Size the response is a short burst that Hall with a short Decay may
reproduce.

### New ringing hypotheses

RR1 and RR6 are the two levers that moved the measure (−7.3 and −11.4 dB).
Both add state to the heard block; nothing else did anything.

| | |
|---|---|
| **RR7** | RR6 (eight local lines, eight field, open tank) with RR1's two allpasses in every local line |
| **RR8** | sixteen local lines with the allpasses, eight field: 24 lines |

Lines unchanged: *materially reduced* ≤ Hall + 14, *clean* ≤ Hall + 3.
RR8's cost is expected to break 1.5 × Hall; it is built to learn how much
state the measure needs, and is reported with its cost.

### Vectors, again, with tone in the rule

RM3's rule looked at balance, mirror and mono tone, and the structure it
chose fails the octave limit. Tone is a safety measure of the same kind, not
the distinctness metric, so it joins the rule:

**RM4** — over the same 2304 combinations: among those with balance and
mirror within 0.5 dB **and octave bands within 0.75 dB of Hall at the
defaults**, the one whose worst mono tone is shallowest.

For an open-tank block (RR6–RR8) the same 2304 four-line patterns are tiled
across the block's groups of four, the right channel's sign alternating from
group to group (Stage 5's rule for eight lines), and chosen by the same RM4
rule: **RM5**.

### The composite

**R6** = the Size law, ringing repair and vector set chosen by this
addendum's runs, by the same selection rules as 2.1–2.3, and judged by 2.4
with nothing changed. If the chosen ringing repair is an open-tank one, the
Size law is applied to it as the same two exponents and re-measured there,
because it was chosen on the eight-line engine.

---

## Addendum 2 — after Room's second run and Ambient's first, before either is run again

Appended; nothing above is edited.

### Room, second run

- **Size.** One law of six passes 1.1: **RS7** (exchange × s, block × s²):
  t10 × 2.26, centroid rising, not Hall at any of five Sizes (lowest 2.27).
  Its G1 at large / long is 1.72.
- **Ringing.** RR7 reaches 23.1 dB and RR8 17.4 dB — both *materially
  reduced* — and **both are REJECTED at G1** (lowest runs 1.41 and 1.06). The
  risk named in addendum 1 is the result: with the ringing gone, Hall
  reproduces the structure.
- **Vectors.** RM4 (in L +−++ R −−+−, out L +++− R −+++): worst mono tone
  −4.2 dB, balance −0.2 dB, mirror 0.0 dB, octave bands 0.52 dB, G1 lowest
  run 3.71. Inside every limit.
- **The composite R6** (R4c + RS7 + RM4) passes Size, Decay, stereo, tone and
  CPU and is **REJECTED** by Hall's search at large / long (median 1.90,
  lowest run 1.80). The risk named for the Size laws — the block growing
  until it is no longer a small block — is what happened. Its ringing is
  R4c's (42.1 dB).

**What is and is not attempted next for Room.**

1. *One* more Size law, between the one that failed the centroid (RS5, block
   × s) and the one that failed Hall's search at large Size (RS7, block ×
   s²): **RS8**, exchange × s, block × s^1.5. This is a point on a line
   between two measured points, not a new mechanism, and it is the last such
   point I will take: if it fails either way, the result is that this
   construction has no Size law that is both meaningful and distinct.
2. **Ringing: no further candidate.** Seven constructions were measured. The
   two that reduce the measure lose the identity; the five that keep the
   identity do not reduce it. For the record, and not as candidates (they
   cannot be chosen: they are not materially reduced), RR1 (35.1 dB) and RR6
   (30.6 dB) are put through G1, to show where along the way from 42 dB to
   23 dB the identity goes.
3. **The composite is built both ways**: R5b = R4c + RM4 (vectors only), and
   R6 = R5b with a Size law, if one is chosen. Each is judged by 2.4. The
   Room that goes on to Parts 3 and 6 is R6 if Hall's search does not reject
   it, otherwise R5b.

### Ambient, first run

**My hypothesis was wrong.** With the damping off the deviation is *larger*
(2.04 dB against 1.82), not smaller. What the diagnosis shows instead:

- the deviation is one band: 125 Hz is 1.8 dB low; every other octave is
  within 1 dB;
- it does not depend on the damping, on the diffuser (1.43–1.55), on the
  modulation (1.39–1.72) or on which lines form which stage (1.82–2.07, always
  the same sign);
- it grows through the tail: after 1.2 s, 125, 250 and 500 Hz are all low
  (−1.8, −1.9, −1.0);
- only 0.6–0.8 % of the energy held in the tank is in the heard stage.

**New hypothesis.** The timing rule treats the transfer between stages as a
transfer of *power*, a fraction sin²θ per pass. That is right where a
stage's modes are dense. A stage here is two lines, and in the 125 Hz octave
it has a handful of narrow modes; two weakly coupled resonators that are not
tuned to each other exchange only part of their energy. Low frequencies
migrate less completely than the rule assumes, so less of them reaches the
stage that is heard. It is the delay-time distribution and the state-energy
distribution together: *too few modes per stage at low frequency*.

**A test of that, D7, before any repair:** the 125 Hz deviation with the
damping off at Size 10 / 30 / 50 / 70 / 90. If the hypothesis is right it
shrinks as Size grows (longer lines, denser modes).

**Repairs.** AT3 (25 % of the input straight into the last stage) fixes the
tone — 0.18 dB at the defaults, 0.73 over the grid — and is REJECTED by
Hall's search at small / short (median 1.98; A1b has 4.00): too much is
heard at once. AT2 (every stage heard) reaches 0.66 at the defaults and 1.51
over the grid.

| | change to A1b |
|---|---|
| **AT5a / b / c** | 8 %, 12 %, 16 % of the input into the last stage |
| **AT6** | only the last two stages heard, weights 0.5 and 1 |
| **AT7** | stages of 2, 2 and 4 lines with k 0.40: the heard stage has four lines and twice the modes (A3's layout, which measured 1.36 dB in Stage 5 at k 0.30) |

Pass condition unchanged (3.2). AT5 is a sweep of one number between a
measured failure on tone (0 %) and a measured failure on identity (25 %); it
is reported as that.
