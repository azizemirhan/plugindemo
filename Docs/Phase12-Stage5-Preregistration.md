# Phase 12 Stage 5 — pre-registration

**Written and committed before any Stage 5 candidate was measured.** In this
commit `Tests/Phase12Stage5.cpp` is an empty function.

Research only. `Source/` is unchanged. Hall is frozen.

Order of work: **Plate, then Room, then Chamber, then Ambient.** Each part is
run and read before the next is run. A part's hypotheses may be *added to*
after an earlier part is read; an addition is appended to this file and
committed before it is measured. Nothing already written here is edited.

## What is not changed

The metric, its thirteen families, every scale, the per-family floor, the
worst-family verdict, the thresholds (REPRODUCED ≤ 1.5, DISTINCT ≥ 3.0) and
the seven-control impostor search are Stage 3's, frozen at `c18ecce`.

A failed candidate stays failed. A new attempt is a new mechanism with a new
name.

## The gates, for every part

**Centred source.** Every score below is taken with the lateral family (the
only one measured with a one-sided source) left out of both the search and
the verdict. A candidate is judged on what a centred source hears.

**Level.** Every structure is energy-matched to Hall at the defaults.

| gate | what runs | outcome |
|---|---|---|
| **G1 quick** | one search per operating point, three points | REPRODUCED at any point → **rejected at once**, no further time spent |
| **G2 serious** | five seeds per point, three points | see below |
| **G3 semantics** | survivors of G2 only | see below |

G2 outcomes, read on the fifteen runs:

- **SURVIVES** — no run ≤ 1.5, and the median is ≥ 3.0 at every point.
- **NOT REFUTED** — no run ≤ 1.5, and the median is ≥ 2.5 at every point. This
  is Plate A's class: at the level of a known real difference (the ring
  control, 2.55–2.99), not provable by this metric. It needs listening.
- **REJECTED** — anything else.

"Survived preliminary structural rejection", the Stage 5 success term, means
SURVIVES or NOT REFUTED at G2 **and** no failure at G3. NOT REFUTED is
reported as NOT REFUTED, never as proven.

G3, each measured against Hall at the same knobs:

| | limit |
|---|---|
| Size | time to 10 % of the energy does not fall as Size rises (5 settings) |
| Decay | measured RT60 within 10 % of Hall's over nine settings; **Ambient 5 %**. An explicit `decayScale` calibration is allowed and is reported as such |
| stereo / mono | the Phase 11 checks of Stage 3 section 12.3.9, with its limits: worst tone no more than 3 dB deeper than Hall's, hard-placed partials no more than 5 points more, balance and mirror within 1 dB, broadband fold-down within 1.5 dB |
| tone | no octave band (125 Hz–8 kHz) more than 1 dB from Hall |
| CPU | Plate ≤ 1.75 × Hall's wet path, preferred ≤ 1.5 ×. Others: reported; above 1.5 × is a finding |

## Part A — Plate reduction

**Reference:** Plate A, 240 first-order sections per channel, a = −0.7, ahead
of the tank. Not changed.

**Hypothesis.** The identity is the frequency-ordered arrival (8 kHz leads
250 Hz by about 25 ms) and the early density that follows from it.

**A prediction made before measuring.** An allpass of order K carries exactly
Kπ of phase, so the area under its group-delay curve from 0 to Nyquist is
fixed by its order and by nothing else. About 70 % of the reference's area
lies below 5 kHz. Therefore **no arrangement of ordinary first-order sections
with much fewer than ~170 of them can keep the reference curve below 5 kHz**:
changing coefficients only moves delay from one frequency to another. The
only cheap way out is a section that carries more phase for the same
arithmetic: a section in z⁻ᴹ carries M times the phase and costs the same,
at the price of a group delay that repeats every rate / M (for M = 2 the
image is at 24 kHz, for M = 3 at 16 kHz and 32 kHz).

So the expectation is: uniform and mixed-coefficient reductions fail on
similarity to the reference; stretched chains may pass.

**The grid, fixed here (63 candidates):**

| family | sections per channel | coefficients |
|---|---|---|
| U — uniform, stretch 1 | 240, 160, 128, 96, 80, 64, 48, 32 | −0.7, −0.8, −0.9 |
| S2 — stretch 2 | 120, 96, 80, 64, 48, 32 | −0.5, −0.6, −0.7 |
| S3 — stretch 3 | 80, 64, 48, 32 | −0.4, −0.5, −0.6 |
| M — mixed strength, stretch 1 | 96, 64, 48 | spread evenly in low-frequency delay from −0.5 to −0.9 |
| G3 — thirds of stretch 1 / 2 / 3 | 96, 72, 48 | −0.7 / −0.6 / −0.5 |
| G2 — halves of stretch 2 / 3 | 80, 64, 48 | −0.6 / −0.5 |

Second-order sections placed on a log-frequency grid are not on the list. By
the area argument they cannot beat first-order sections per unit of phase,
and each costs about twice the arithmetic. Stated, not measured.

**Measured for every candidate, same knobs, three points:** the score against
Hall; the score against Plate A (same thirteen families, so arrival time,
buildup, density, texture, modal structure and stereo are all in it); the
wet-path cost against Hall.

**PRESERVED** means the score against Plate A is ≤ 1.5 at all three points —
the reference and the candidate are as close as an imitation has to be to be
called REPRODUCED.

**Shortlist rule.** Of the candidates that are PRESERVED and cost ≤ 1.75 ×,
the three cheapest. If none is PRESERVED under 1.75 ×, the three closest to
Plate A under 1.75 ×, and the report says the identity was not preserved.

**The shortlist then goes through G2** against Hall. A reduced Plate is
*reference-equivalent* if it has no run ≤ 1.5 and its median at each point is
at least the reference's own ten-seed minimum there (2.95, 2.54, 2.78). Its
absolute class (SURVIVES or NOT REFUTED) is read by the table above, exactly
as the reference's is. A reduction cannot be asked to be more distinct than
what it reduces.

**Failure modes:** the image of a stretched section arriving late and audible
as high-frequency sizzle (read in the 8 kHz arrival and in the score against
Plate A); density lost with the phase; identity kept only on impulses.

## Part B — Room

Room B is closed. Target: distinct from Hall on a centred source with
Early/Late at 0.

All Room, Chamber and Ambient candidates use one new construction, so that
each differs from the next in one thing: the eight lines are partitioned into
blocks; each block mixes internally; adjacent blocks are joined by a rotation
of angle θ between paired lines; each block's delays are scaled as a group.
The whole matrix stays orthogonal. Vectors are equal-magnitude sign vectors
that are not rows of any block's matrix (the defect of Stage 3's Ambient
A–C), so that the channels are balanced by construction.

| | hypothesis | construction | expected family |
|---|---|---|---|
| **R1a** local block | a small fast block that is both driven and heard, losing its energy to a larger field, is a compact enclosure: dense immediately, then a quieter tail | A = 4 shortest lines × 0.25, B = 4 lines × 1; θ 0.25; in A, out A | decay shape, energy profile |
| **R1b** | the same, less extreme | A × 0.40; θ 0.25; in A, out A | |
| **R1c** | the same, heard in both blocks | A × 0.25; θ 0.25; in A, out A + B | |
| **R2** multi-region | three regions of rising size, driven at the small end | 4 × 0.2 / 2 × 0.45 / 2 × 1; θ 0.30 each; in first, out all | energy profile, buildup |
| **R3** wall pairs | four un-mixed short loops (pairs of walls) leaking into a diffuse field | A = 4 lines × 0.30, identity inside A; θ 0.20; in A, out A + B | loop repetition, modal structure |

**Failure modes, each tested on anything that passes G1:** flutter and
metallic repetition (loop-repetition family raw value against Hall's; R3 is
expected to fail here and is on the list to show what that failure looks
like); ringing (modal prominence); comb-like colour (third-octave deviation);
narrow-band mono collapse; a short Decay that does not hold (Decay 0.3 s:
finite, RT60 within the Decay limit); stereo bias.

**The specific adversary** is Hall with Early/Late raised: the early layer is
a user control and is in the search.

## Part C — Chamber

Not a size between Room and Hall. Target: a reflective enclosure whose
reflections and late field keep exchanging energy.

| | hypothesis | construction | expected family |
|---|---|---|---|
| **C1a** recirculating reflections | two un-mixed reflection paths in strong exchange with a dense field re-imprint a discrete pattern on every pass | A = 2 lines, identity; B = 6 lines; θ 0.60; in all, out all | late texture, recurrence |
| **C1b** | four paths in a ring | A = 4 lines, ring; B = 4; θ 0.45; in all, out all | |
| **C2a** coupled volumes | a lossy reflective block weakly coupled to a long-lived one decays fast, then is held up by what comes back: the double slope of a chamber coupled to a room | A = 4 lines × 0.6, RT 0.45 ×; B = 4 lines × 1; θ 0.12; in A, out A | decay shape (late slope), energy profile |
| **C2b** | stronger | A RT 0.30 ×; θ 0.20 | |

C1 differs from Stage 3's rejected forms (a feed-forward early pattern into
the tank; extra readout taps) in being inside the recursion. If C1 is
reproduced that will be the third reflection-into-tank form to fail.

C2 changes what the decay *is*. Its RT60 by the standard estimator (−5 to
−20 dB) is held to the limit by an explicit `decayScale`, and the late tail is
then longer than requested by design. **That is a Decay-semantics question
for a human, flagged here in advance; passing G3 by calibration does not
settle it.**

**Adversarial gate.** The best Chamber is the target of impostor searches by
Hall, by the best Room and by the recommended Plate, three seeds per point. A
run ≤ 1.5 by any of them is a collapse and rejects it.

## Part D — Ambient

Ambient A–E are closed. Their swell was a fixed angle, so its duration fell
with Size until Hall's Attack (a 32-tap spread of up to 250 ms) could imitate
it; and they moved Decay by up to 25 %.

**Hypothesis.** Energy that has to migrate through several stages before it
is heard rises as a higher-order curve that a tap spread cannot draw, **if**
the migration time is tied to the Decay time and not to the loop length.

**The rule that is new:** the per-pass transfer of each stage is set from a
time, sin²θ = (mean loop time of the stage) / τ, with the total migration
time T = k × Decay split evenly over the stages. The topology therefore
depends on Size and Decay by a stated law.

| | construction | k |
|---|---|---|
| **A1a** four stages | 2 → 2 → 2 → 2 lines; in first, out last | 0.25 |
| **A1b** | the same | 0.40 |
| **A3** three stages | 2 → 2 → 4 lines; in first, out last (four lines heard) | 0.30 |

**Decay.** Each candidate's `decayScale` is fitted per operating point so
that its measured RT60 equals Hall's, then verified. The fitted values are
reported: they are the calibration a production law would have to reproduce.

**Failure modes:** reproduced at small / short as before; a swell so slow it
is an effect and not a reverb; two heard lines too few for a dense field
(A1); a late field narrower or less safe in mono than Hall's.

No shimmer, pitch, granular, freeze or delay-effect mechanism.

## What Stage 5 can and cannot conclude

The gates say whether a mechanism is *structurally* outside Hall's reach on a
centred source. They do not say that it sounds like a room, a chamber or an
ambience. That is a listening question and is not answered in Stage 5.

---

## Addendum 1 — after Part A's first run, before its second

Appended; nothing above is edited.

**A measurement of mine was wrong.** The grid's cost column was a
best-of-five per chain, taken minutes apart from Hall's own. Several chains
came out *cheaper than Hall* (0.82–0.97 ×), which is impossible for a
structure that only adds work: the machine's clock speed had moved between
the two measurements. The shortlist rule filters on cost, so it was applied
to noise and picked 96- and 64-section chains that are not under 1.75 ×.
Cost is now measured as a ratio, Hall and the chain alternating, nine rounds,
minimum against minimum. Part A is re-run in full. The first run's output is
not used for anything.

**A prediction of mine was half wrong.** "A stretched section carries M times
the phase for the same arithmetic" is true and does not help: the extra phase
goes into the images at rate / M, not into the band below 5 kHz where the
identity is. In the first run a stretch-2 chain needed as many sections as an
ordinary one to stay PRESERVED (120 against 128). This is recorded here
because the re-run will show the same thing and it should not read as if it
had been expected.

**One section is added to Part A,** because the shortlist rule only asks what
survives under 1.75 × and the next stage needs the other answer too:

- *The floor, whatever it costs.* The three PRESERVED chains with the fewest
  sections go through the same G2 and the same reference-equivalence test.
  This cannot produce a Plate recommendation — the cost limit stands — it
  reports the cheapest chain that is still Plate A.

No threshold, family, scale or search setting is changed.

---

## Addendum 2 — after Parts A and B were read, before Room is run again

Appended; nothing above is edited.

**Part A, as re-run:** no chain under 1.75 × Hall preserves the reference; the
three closest are REJECTED outright (lowest runs 0.65–0.95). The three
cheapest chains that *are* PRESERVED (120–128 sections, about 3 × Hall) are
not reference-equivalent either (medians 1.8–2.7). Plate reduction has
failed within this mechanism. That result stands and is not revisited here.

**Part B, as run:** all five Room hypotheses are rejected.

| | medium | small | large | lowest run | |
|---|---|---|---|---|---|
| R1a local block 0.25 | 3.08 | 5.11 | **1.68** | 1.58 | REJECTED |
| R1b local block 0.40 | 1.81 | 4.73 | 1.34 | 1.34 | REJECTED at G1 |
| R1c local, hear both | 1.30 | 2.26 | 1.17 | 1.17 | REJECTED at G1 |
| R2 multi-region | 2.65 | 5.66 | 1.32 | 1.32 | REJECTED at G1 |
| R3 wall pairs | 2.50 | 3.83 | 2.75 | 2.42 | REJECTED (medium median 2.498 against 2.5) |

R3 misses NOT REFUTED by two thousandths. It stays rejected; and it carries
the repetition and ringing flags it was put on the list to demonstrate.

**What was learned.** A small block that is driven *and is the only thing
heard* is far outside Hall's reach at small and medium settings (3.1–5.1).
Heard together with the field (R1c) it is reproduced at once: Hall's early
layer does that. It fails at large / long, and it rings (modal prominence 35
against Hall's 14) and shifts a short Decay (0.84 ×).

**New hypothesis, R4 — a different mechanism, not R1a re-tuned to pass.** In
R1a the exchange between block and field is a fixed angle, so its duration
grows with Size while the Decay it is judged against grows faster. R4 sets
it from a time, as Part D does: sin²θ = (mean loop time of the block) /
(k × Decay). Its RT60 is calibrated explicitly (`decayScale`).

| | block | k |
|---|---|---|
| **R4a** | 4 shortest lines × 0.25 | 0.06 |
| **R4b** | 4 shortest lines × 0.40 | 0.06 |
| **R4c** | 4 shortest lines × 0.25 | 0.12 |

Gates unchanged. **Expected failure, stated now:** the ringing flag is a
property of hearing four short lines and the timing rule does nothing about
it. If R4 passes G1–G3 it passes with that flag on it, and the flag goes to a
human, not into a footnote.

The gate tables now also print the family and Hall's knobs of the lowest run
at each point. That is reporting only.

---

## Addendum 3 — after Parts B, C and D were read, before Chamber is run again

Appended; nothing above is edited.

**Part B, with R4:** R4c (local block × 0.25, k 0.12) SURVIVES G2 on a centred
source (medians 5.69 / 6.38 / 4.06, lowest run 3.66) and **fails G3**: Size
barely moves it and not monotonically, its channels are 1.3 dB out of balance,
its worst mono tone is 8 dB deeper than Hall's. It also carries the ringing
flag predicted in addendum 2. R4a is NOT REFUTED and fails G3 the same way.
Room has a structural mechanism and no survivor.

**Part C:** all four Chamber hypotheses are REJECTED at G1 (lowest runs
1.14–1.46). Reflections inside the recursion (C1) is now the third
reflection-into-tank form to be reproduced. Coupled volumes at a fixed angle
(C2) are reproduced too.

**Part D:** A1b and A3 SURVIVE G2 (lowest medians 4.00 and 4.12); Decay is
within 0.1 % of Hall's by calibration. Both **fail G3 on tone** (octave bands
1.82 and 1.36 dB from Hall; limit 1), and A3 also on mono fold-down. Ambient
has a structural mechanism and no survivor.

**What Parts B and D have in common** is the timing rule: the two mechanisms
that got past G2 are the two whose energy exchange is set from Decay. The two
families of fixed-angle exchanges (R1, C2) did not.

**New Chamber hypothesis, C3 — coupled volumes with a timed exchange.** C2's
construction (a lossy block that is driven and heard, coupled to a long-lived
one) with sin²θ = (mean loop time of the block) / (k × Decay), RT60
calibrated.

| | heard block | its RT | k |
|---|---|---|---|
| **C3a** | 4 lines × 0.6 | 0.45 × | 0.12 |
| **C3b** | 4 lines × 0.6 | 0.30 × | 0.12 |
| **C3c** | 4 lines × 1.0 | 0.45 × | 0.12 |

**The risk stated now:** C3 is R4's construction plus loss. If it passes G2,
the adversarial gate against Room (R4c) is the test of whether it is a second
family or the same one with a knob moved; a collapse there rejects it. The
impostor in that gate is R4c as built at the default knobs, because the
search takes one fixed structure — so R4c's own timing does not follow its
Decay knob during the search. That makes the gate somewhat *easier* for
Chamber to pass than a full test would be, and a pass must be read with that
in mind.

Gates unchanged.
