# Phase 12 Stage 7 — Ambient listening package, Room and Chamber reset, Plate validation plan

> **Correction, 2026-11-19.** The frozen-metric scores in this document were read without the same-family baseline and are unscaled: buildup is overstated by 25 %, energy profile by 83 %, echo density by 25 %; the other families are correct. Timing, tone, level and CPU figures are unaffected. Every affected figure, before and after, is in `Phase12-Stage9-Metric-Baseline-Correction.md`, which governs where the two differ. This document is otherwise left as written.

Research only. `Source/` is unchanged. No Mode selector, no production DSP,
no Phase 13. Hall is frozen.

**Stage 7 built one thing — a listening package — and measured nothing new
against a gate.** Everything else here is analysis and plan. It stops for a
human decision.

## 0. Decisions carried in from Stage 6 (2026-11-18)

1. The V1 target remains five Modes: Room, Hall, Chamber, Plate, Ambient.
2. Hall is frozen and accepted.
3. **Plate:** the 16-way pipelined chain (PV3) is the current
   production-viable candidate. 1.60–1.72 × Hall is accepted provisionally.
   No redesign unless later validation disproves it. CPU to be re-measured
   on an idle machine with interleaved Hall / Plate runs.
4. **Room:** R4c / R5b are not a shipping Room. Their distinction is tied to
   modal ringing, and ringing must not be the identity. Kept as research
   references only. Room returns to architectural research.
5. **Ambient:** A1b is a live candidate and has **not** passed. The 1 dB
   tonal gate is unchanged and **A1b fails it (1.82 dB).** A blind listening
   package decides whether the deviation is objectionable.
6. **Chamber:** fourteen negative structures are sufficient; no incremental
   variants. New work starts from a new conceptual basis.
7. No Mode selector. No Phase 13.

## 1. Status

| stop condition | state |
|---|---|
| 1. Ambient blind listening package ready | **ready**: `renders/phase12-stage7-ambient/`, 27 files, `LISTENING.md`, `KEY.txt` |
| 2. Room: new conceptual families documented and pre-registered | **done**: `Docs/Phase12-Stage7-Room-Preregistration.md`, three hypotheses, none built |
| 3. Chamber: new conceptual research directions documented | **done**, section 4 — with a finding the decision should weigh |
| 4. Plate candidate validation plan documented | **done**, section 5 |
| 5. No production DSP changed | `git diff -- Source` empty |

Three things for the decision, ahead of the detail:

- **Ambient has a second objective issue the gate does not measure.** A1b's
  level is set once, at the defaults. At short settings it then comes out
  1.4–4.2 dB hotter than Hall depending on the source (section 2.3).
- **Room and Chamber may be asking for the same thing.** Under the
  constraints now in force — centred source, short Decay, no ringing, no
  tone, no Early/Late — the analysis finds a small number of properties
  left outside Hall's reach, and physics does not divide them between a
  room and a chamber. Section 4.3.
- **One Room hypothesis (RN3) will probably prove untestable with the frozen
  metric**, and I have said so in its pre-registration rather than leave it
  out.

## 2. Track A — the Ambient listening package

### 2.1 What it is

`Scripts/export-phase12-stage7-ambient-pack.ps1 -Config Release` renders it
from `Tests/Phase12Stage7Listening.cpp`. The renders folder is not tracked;
the package is reproducible from any checkout.

Twenty-seven files, `s7-01.wav` to `s7-27.wav`. Each is one comparison: a
source through one reverb, half a second of silence, the same source through
another. 100 % wet, 48 kHz, Early/Late 0, every visible control at its
default and equal for both halves; the second reverb level-matched to the
first by integrated energy; file numbers and orders fixed and patternless.
**Which file is which is only in `KEY.txt`**, and is not repeated here.

| group | files | what it answers |
|---|---|---|
| Hall against A1b, centred | 10 | a different family? — vocal, piano and snare at short / medium / long, percussion at short |
| the same, folded to mono | 3 | does it survive mono |
| the same, hard-left source | 2 | a positioned source |
| A1b against AT5a | 4 | does the tonally repaired Ambient sound like the same reverb, better or worse |
| **tone only**: Hall against Hall carrying A1b's octave fingerprint | 3 | is the deviation, by itself, audible, and as what |
| **structure only**: Hall against A1b with the fingerprint filtered off | 3 | is A1b still a different family when its tone is Hall's |
| catch trials, identical halves | 2 | how often this listener reports a difference that is not there |

Settings: short (Size 25, Decay 0.8 s), medium (60, 2.5 s), long (85, 5.0 s)
— the three operating points of every gate.

A1b and AT5a are Stage 6's structures, built by Stage 6's builders and
calibrated as Stage 6 calibrated them. Nothing was tuned.

**The two filtered groups are listening controls, not candidates.** The
fingerprint is put on, or taken off, by a 4097-tap linear-phase filter
fitted until the octave measure itself reads the target (section 2.2). An
output equaliser is excluded as an Ambient repair by Stage 6's
pre-registration and stays excluded; a filtered A1b is not being proposed.
Together the two groups separate the question the gate cannot: whether the
1.8 dB is heard as *tone*, and whether the *family* difference needs it.

**A source was changed.** Earlier stages' "piano" is four held sine waves.
A reverb's level on four sines is largely chance (section 2.3). The pack
uses a struck phrase instead — seven notes and chords from G2 to A4, a
dozen partials each — which also puts energy in the 125 Hz octave, where
the question is. It is synthesised, like every source in this repository;
it is piano-like, not a piano.

### 2.2 Objective notes: tone

Octave-band energy share against Hall's, dB, on the impulse response at the
same knobs.

| setting | | 125 | 250 | 500 | 1k | 2k | 4k | 8k | worst |
|---|---|---|---|---|---|---|---|---|---|
| medium | A1b | −1.82 | −0.10 | +0.12 | +0.78 | +0.99 | +0.12 | −0.83 | **1.82** |
| | AT5a | −0.24 | +0.22 | +0.21 | +0.05 | +0.03 | +0.07 | −0.11 | 0.24 |
| | Hall + fingerprint | −1.77 | −0.14 | +0.13 | +0.78 | +0.98 | +0.12 | −0.84 | 1.77 |
| | A1b, fingerprint off | −0.04 | +0.04 | −0.01 | 0.00 | 0.00 | 0.00 | 0.00 | 0.04 |
| short | A1b | −2.26 | −0.93 | +1.47 | +0.93 | +0.77 | +0.31 | −1.02 | **2.26** |
| | AT5a | +0.22 | −0.19 | +0.22 | +0.24 | +0.32 | +0.06 | −0.26 | 0.32 |
| | Hall + fingerprint | −2.34 | −0.83 | +1.44 | +0.95 | +0.76 | +0.30 | −1.02 | 2.34 |
| | A1b, fingerprint off | +0.06 | −0.07 | +0.05 | −0.02 | 0.00 | 0.00 | 0.00 | 0.07 |
| long | A1b | +0.44 | +0.20 | +0.09 | +0.17 | +0.71 | +0.15 | −0.56 | 0.71 |
| | AT5a | +0.32 | +0.23 | −0.09 | −0.03 | +0.11 | +0.04 | −0.08 | 0.32 |
| | Hall + fingerprint | +0.44 | +0.20 | +0.06 | +0.21 | +0.69 | +0.16 | −0.56 | 0.69 |
| | A1b, fingerprint off | 0.00 | 0.00 | +0.03 | −0.04 | +0.02 | 0.00 | 0.00 | 0.04 |

- **The gate result is unchanged: A1b FAILS** (1.82 dB at the defaults,
  3.02 dB over Stage 6's 27-point grid; limit 1.0).
- The deviation depends on the setting: worst at short (2.26 dB, and 500 Hz
  is 1.5 dB *up* there), inside 1 dB at long. It is not one curve.
- It is more than the 125 Hz band: at medium and short the 1–2 kHz octaves
  are 0.8–1.0 dB up and 8 kHz 0.8–1.0 dB down. Stage 6 reported the worst
  band; the shape is a low cut with a presence lift.
- The control filters reach their targets within 0.1 dB.

### 2.3 Objective notes: structure, level, mono

**Structure at the same knobs** (centred; ≥ 3.0 reads as a different
structure). Not the gate — the gate is Hall's search with its controls free.

| | Hall vs A1b | Hall vs AT5a | A1b vs AT5a |
|---|---|---|---|
| medium | 155 (late texture) | 9.7 (echo density) | 148 (late texture) |
| short | 10.3 (buildup) | 3.7 (echo density) | 8.0 (energy profile) |
| long | 298 (late texture) | 13.8 (late texture) | 286 (late texture) |

AT5a is far closer to Hall than to A1b at every point. Under Hall's search
A1b is 9.77 / 4.00 / 15.33 (SURVIVES) and AT5a 3.97 / 2.06 / 4.63
(REJECTED at short). The A1b-against-AT5a files let a listener hear what
that costs.

**Level by source.** Every structure is levelled once, on its impulse
response at the defaults. The gain then still needed to match Hall on each
source, dB:

| source | setting | Hall's sibling delay set | A1b | AT5a |
|---|---|---|---|---|
| vocal | medium / short / long | −0.1 / −0.2 / +0.7 | −1.4 / **−4.2** / +0.8 | −0.5 / −0.1 / +0.5 |
| piano phrase | | +3.4 / −0.2 / −0.4 | +0.7 / −1.7 / −1.6 | +3.4 / −1.4 / −1.5 |
| snare | | 0.0 / 0.0 / +0.2 | 0.0 / **−2.5** / +0.5 | −0.1 / +0.1 / +0.1 |
| percussion | | 0.0 / +0.1 / +0.2 | +0.9 / **−1.4** / +1.2 | +0.1 / +0.2 / +0.1 |
| four held sines | | +1.6 / +0.3 / +3.3 | +8.9 / 0.0 / −5.4 | +2.7 / −0.3 / −6.4 |

- On broadband sources Hall's sibling stays within 0.2 dB (0.7 on the vocal
  at long). **A1b at short is 1.4–4.2 dB louder than Hall**, by an amount
  that depends on the source. AT5a is within 0.2 dB there.
- This is not a gate and no gate is proposed for it here. It says that A1b
  needs a level law over Size and Decay that Stage 6 did not give it, and
  that part of the difference is source-dependent and so cannot be removed
  by a law. In the pack it is matched out file by file.
- On tonal sources every structure moves, the sibling included (3.4 dB on
  the piano phrase at medium, the same figure as AT5a's). On four held
  sines the spread reaches 14 dB. That row is why the source was changed.

**Mono.** Fold-down, mono against stereo, in the three mono files: Hall
−3.08 / −2.93 / −3.03 dB, A1b −3.20 / −3.65 / −3.37 dB. A1b loses up to
0.7 dB more than Hall (snare, short); Stage 3's limit is 1.5 dB.

### 2.4 What the listening is asked

`LISTENING.md` has six questions per file (same or different; a different
*kind*, a different *tone*, or a different *setting*; which words fit;
whether the tone of either half is objectionable; preference; whether both
are wanted) and four for the end:

1. Does A1b sound like a genuinely different reverb family from Hall?
2. Is its low-frequency difference from Hall objectionable?
3. Does it sound like unintended EQ, or like the way this reverb is?
4. Would that tone get in the way of a Modern / Classic / Vintage Character
   laid over it later?

How the groups bear on them:

| if the listener … | it means |
|---|---|
| hears "different kinds" in the structure-only files | the family does not depend on the tone |
| hears "same kind, different tone" in the tone-only files, and nothing objectionable | the fingerprint is audible and benign |
| marks the tone-only files as filtered or objectionable | the deviation is a defect by ear as well as by gate |
| hears no difference in the tone-only files | 1.8–2.3 dB over an octave is under this listener's threshold on this material |
| prefers AT5a to A1b and calls them the same kind | the repair costs nothing audible; the gate result at short then carries the decision |
| reports differences in the catch trials | the other answers are weighted down accordingly |

Question 4 has an objective half that can be stated now: the fingerprint is
a broad tilt of 1–2 dB that *changes with Size and Decay*. A Character
layer that sets brightness would sit on a base that moves by that much
between settings in Ambient and does not in the other Modes.

**I have not listened and make no perceptual claim.** The product decision
is not mine.

### 2.5 Limits of the package

- 100 % wet, no early layer: a difference heard here is an upper bound on
  what a mix would show.
- Synthetic sources.
- One listener is one listener. The catch trials measure that listener's
  false-alarm rate; they do not make the result general.
- Level matching by integrated energy hides the level differences of 2.3.

## 3. Track B — Room

`Docs/Phase12-Stage7-Room-Preregistration.md`. Nothing is built.

**The basis.** A compact room is not a small hall. Count the reflections
inside the decay: in a hall the field has finished mixing long before it
has decayed, and its mixing time is set by its size. In a compact
absorptive room the sound is gone before the field has finished mixing: its
mixing time is set by its *decay*. That is the law R4c found by accident
(exchange time = 0.12 × Decay) and the reason Hall could not follow it.
R4c realised it with four short loops, which ring. The requirement becomes:
**keep the law, remove the loops.**

Hall, whatever its controls do, keeps four properties: its mixing time is
set by Size; its density and its periodicity are one number; its mixing is
the same at every frequency; its channels differ by sign, not by time. The
three hypotheses each break a different one.

| | hypothesis | axis | the risk named in advance |
|---|---|---|---|
| **RN1** | unfinished mixing without loops: a feed-forward sparse field whose density grows on the Decay time scale; no path shorter than 0.6 × RT60 | time | heard as grain; cost 1.3–2.0 × Hall; Size may read as pre-delay; fails at large / long |
| **RN2** | scattering that depends on frequency: a paraunitary matrix, specular (and Decay-timed) below a crossover, diffuse above | frequency | low-frequency repetition — about even odds that it is not clean |
| **RN3** | the channels differ by time, not sign: coherent lows, decorrelated highs, on every echo | space | mono comb and tilt; and the frozen metric probably reads it as a narrower Hall, in which case it is *not testable* |

Stricter than Stage 6, added for Room: modal prominence must be **clean**
(≤ Hall + 3 dB) before any distinctness result counts; a search that stops
on "modal structure" is reported as resting on modes; small / short is run
first; and each hypothesis has a visibility control that is read before the
candidate is built.

**Are they genuinely new?** RN1 and RN2 both use Stage 5's timing rule,
which is the one thing known to work; what is new is where it is applied
(to density in a loop-free field; to mixing as a function of frequency).
Their nearest failed relatives are named in the document. RN3 is new in
kind and is the least likely to be a *Room* property specifically. I would
not call any of the three safe. I have not coded them.

## 4. Track C — Chamber

A conceptual study. No prototype, no variant of an earlier family. The
acoustics and the product history below are from memory and general
knowledge; **no source was consulted or verified in this stage**, and the
figures are orders of magnitude.

### 4.1 What a chamber is

An echo chamber is a small, very hard, irregular room — tens to a couple of
hundred cubic metres of plaster or tile — with a loudspeaker and one or two
microphones. Physically it is defined by one decoupling: **a small room's
mean free path with a hall's decay time.**

| | mean free path | RT | reflections within the decay | Schroeder frequency |
|---|---|---|---|---|
| hall | 30–40 ms | 2 s | 50–70 | 20–30 Hz |
| compact room | 5–10 ms | 0.2–0.5 s | 20–60 | 150–250 Hz |
| chamber | 6–12 ms | 2–4 s | 300–600 | 300–450 Hz |
| plate | — (two-dimensional, dispersive) | 1–5 s | dense at once | — |

What follows for the sound:

1. **It is mixed before it has decayed at all.** Dense within a few
   milliseconds, and then statistically the same from the first instant to
   the last: no build, no evolution, no sense of a size being revealed.
   This is what the classic digital "Chamber" programs were described as —
   even, dimensionless, little change as the sound decays.
2. **There is no direct sound and no distinct early pattern**: the
   loudspeaker faces a wall or a corner.
3. **Below a few hundred hertz it is strongly modal**: few modes, ringing
   for seconds. The "boxiness" engineers filter out of a chamber return.
4. **It is bright**: hard walls, little air.

### 4.2 Against the other three, and what is left

| against | the difference | usable? |
|---|---|---|
| Hall | no size, no build; dense at once | in a delay network this is *Hall at small Size and long Decay*. Hall's own knobs go there. **This is why fourteen structures failed: as a late field, a chamber is a point in Hall's Size–Decay plane.** |
| Plate | no dispersion: all frequencies arrive together | already what separates Plate from everything |
| Room | many more reflections within the decay: fully mixed where a room is unfinished | only while the decay is long |
| any | low-frequency modes; brightness | excluded: ringing and coloration (and brightness is Phase 13's) |

**At short Decay a chamber is a room.** Damp a chamber to 0.8 s and it is a
small absorptive room: same mean free path, same reflection count. Physics
supplies no late-field difference between a Chamber and a Room at short
Decay. Whatever separates them there has to be a *convention*, and the
record of Stages 3–6 is that Hall's controls absorb conventions.

That is the result of the study, and it is uncomfortable for a five-Mode
plan in which Room and Chamber must each stand at small / short. It does
not say Chamber is impossible. It says what kind of thing a Chamber
mechanism would have to be.

### 4.3 The frame

Each family, by what sets the time its field takes to mix:

| | mixing time is set by |
|---|---|
| Hall | Size |
| Room | Decay: never finished (R4c's law; RN1, RN2) |
| Ambient | Decay: migration before it is heard (A1b) |
| Plate | — its identity is dispersion ahead of the field |
| **Chamber** | **nothing: finished at once, at every Size and every Decay** |

This is my synthesis of the results, not a measurement. It gives Chamber a
definition that is not a matter of degree: *stationary from the first
instant.* And it makes plain where the conflict is. A field that is dense
at once **and** never repeats is outside Hall's reach by section 1.1 of the
Room document (Hall's density and periodicity are one number) — and it is
the same engine RN1 needs for Room, run with the opposite density law.

### 4.4 Research directions

Directions, not designs. None is a variant of reflection-into-tank,
coupled volumes, timed coupling, a wall network, a nested ring or a lattice.

**CH-A — stationarity.** A field at full density from its first
millisecond, with no path short enough to return inside the audible decay,
at every Size and Decay. The property is the *absence* of evolution and of
periodicity together: buildup nil, echo density at its ceiling at once,
recurrence and loop repetition nil, modal prominence at or below Hall's.
Hall approaches it only by shortening its loops, which brings repetition.
*What it needs first:* a measurement, not a prototype — how close Hall at
Size 0 with full diffusion comes to a synthetic stationary field (shaped
noise) at the three points, under the frozen search. If Hall reproduces
shaped noise at small / short, stationarity is not a structure the metric
can hold and CH-A is closed before it starts. *Cost:* dense and loop-free
is expensive in taps; this is the direction most likely to exceed 1.5 ×.
*Conflict:* shares its engine with Room RN1. They would then have to be
told apart by density law alone, and proven so by the Room-against-Chamber
search.

**CH-B — the pickup, not the field.** A chamber is heard through two
microphones in a small diffuse field: coherent at low frequency, falling
with frequency as the spacing dictates. The identity is in how the two
channels are related, not in the tail. *Conflict:* it is the same property
as Room RN3, with the same doubt about whether the frozen metric can see
it. Only one family can own it. *What it needs first:* RN3's visibility
control; one measurement serves both.

**CH-C — Size that does nothing, on purpose.** "Dimensionless" as a control
law: in Chamber the Size control would move something other than apparent
size. This is a product-semantics direction, not a structure, and it runs
against Stage 6's requirement that Size be monotone and meaningful in
every Mode. Listed because the study points at it; it needs a decision
about the requirement, not research.

**Not a direction, and why:** a modal density that rises with frequency (a
warped network costs far more than Plate's chain did; and sparse low modes
are ringing); the transducers and the send chain (tone and era: Phase 13);
anything whose difference from Hall is a matter of degree.

### 4.5 What I conclude

- No mechanism family emerged that is at once specific to a chamber, alive
  at short Decay, free of coloration, and different from what Room needs.
- Two *properties* emerged — stationarity, and the pickup's coherence law —
  and both are claimed by a Room hypothesis as well.
- The cheapest next step for Chamber is two measurements on controls
  (shaped noise against Hall's search; RN3's Width sweep), not a design.
- Whether Room and Chamber can both be held at small / short is the open
  question. I am not answering it by lowering the standard, and the Mode
  count is not my decision.

## 5. Plate — validation plan for PV3

The candidate: the 240-section reference chain as 16 sub-chains, two-wide,
values kept in registers. Same transfer function, 15 samples later.
Measured in Stage 6: PRESERVED (0.35 / 0.16 / 0.33 from the reference),
G2 3.38 / 3.09 / 3.13 with lowest run 2.72 (SURVIVES, reference-equivalent),
1.60 / 1.61 / 1.72 × Hall on a machine that was in use.

Nothing below has been run.

**5.1 CPU, on an idle machine.**

- Machine idle: no other applications, mains power, a fixed performance
  plan, the process at high priority and pinned to one core; thermal state
  settled before the first reading.
- Hall and Plate **interleaved**, block by block (H P H P …), at least 31
  pairs, each block at least 2 s of audio; the ratio taken *per pair*.
- Reported: the median ratio, its 95 % interval, and the ratio of the two
  minima. Three separate runs, one after a restart.
- At the three operating points, at Size 0 and 100, with modulation at 0
  and at its default, and into silence (denormal conditions).
- **Pass: the upper end of the 95 % interval ≤ 1.75 in every run.**
  Preferred, reported, not required: median ≤ 1.5.
- Also the scalar eight-way form (PV1) as the fallback: 1.71 here,
  1.77–1.87 in log 03. If only PV3 passes, Plate depends on the two-wide
  path being available.

**5.2 The things Stage 6 did not look at.**

| | question | why it matters |
|---|---|---|
| sample rate | the chain is 240 sections at −0.7 *at 48 kHz*. At 44.1, 88.2, 96 and 192 kHz the same sections give a different dispersion in milliseconds | Plate has to be *defined* at other rates — sections scaled, coefficient re-fitted, or the chain run at a fixed internal rate — and each choice has a cost and must be shown to be the same Plate. Every Plate result so far is at 48 kHz only |
| the 15 samples | the wet path is 0.3 ms late against Hall's | harmless in a reverb; must be the same at every rate or scale with it, and must be known to Mode switching |
| other processors | "two-wide in registers" is SSE2 on x64 and NEON on ARM | to be measured on each target and each compiler; PV1 is the portable form |
| real-time safety | no allocation, no denormal stall in a 240-section tail | standard checks, on the chain specifically |

**5.3 The margin against Hall.** PV3's medians (3.09–3.38) are close to
3.0, and Stage 4 showed the reference itself is threshold-sensitive (ten
seeds: minima 2.95 / 2.54 / 2.78). Stage 6's rule — no median under the
reference's own ten-seed minimum — is what makes PV3
"reference-equivalent", and it stands. To be added, without changing the
rule: PV3's own ten-seed distribution at the three points, so its spread is
known and not inferred from the reference's.

**5.4 Listening.** PV3 is the reference to twelve decimal places, so the
Plate files of Stage 4's pack (`renders/phase12-stage4/`) are PV3's files.
Whether Plate is a different family *by ear* is the question that pack
asks; this plan adds nothing to it.

**5.5 Switching.** Two networks at once cost 2.68–2.74 × Hall when one is
Plate, for the length of the pre-warm and crossfade; and 400 ms of pre-warm
is 1.1 dB short at long Decay (800 ms is enough). Both belong to the
switching design, not to Plate.

**5.6 What would reopen Plate.** Any of: the upper bound above 1.75 on an
idle machine; a ten-seed median under 2.5 or any run ≤ 1.5; no definition
at other sample rates that is the same Plate inside the limit; a listener
who cannot tell it from Hall.

## 6. Tests and tooling added

- `Tests/Phase12Stage7Listening.h`, `.cpp` — the pack. No suite, no check:
  it renders and writes files, through the test DLL like the earlier packs.
- `Scripts/export-phase12-stage7-ambient-pack.ps1`.
- One export entry in `Tests/TestExports.cpp`; one line in
  `Tests/CMakeLists.txt`.

No existing test, rig or gate was edited.

## 7. Regression

Run on the final build, with nothing else building or measuring.

| Check | Result |
|---|---|
| Fast regression (`AuroraVerbTests.exe --fast`) | 2583 / 2601 passed, 18 failures in 16 suites |
| State suite (`AuroraVerbStateTests.exe`) | 26 / 26 passed |
| `git diff --stat 77f32fe -- Source` | empty |
| `git status --short -- Source` | empty |

The 18 failures match the Stage 6 figure in count and in suite count
(2583 / 2601, 16 suites). They were compared by count, not name by name.

What this does not cover:

- The heavy (non-fast) suites were not re-run in Stage 7.
- Stage 7 added no test. The listening pack is a render, not a gate, and
  nothing in the regression exercises it.
- An earlier regression attempt in this stage overlapped a rebuild and was
  discarded; the figures above are from the clean re-run.

## 8. Stop

Waiting for:

1. the listening results on the Ambient package;
2. approval, amendment or rejection of the three Room hypotheses before any
   is built;
3. a decision on how Room and Chamber are to share — or not share — the
   properties of section 4.3;
4. a go-ahead for the Plate validation of section 5.

No Mode selector has been implemented. Phase 13 has not been started.
