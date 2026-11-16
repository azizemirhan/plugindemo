# PHASE 12 — Mode System · Stage 1: research and architecture

**Research and architecture only. No production DSP was modified.** `Source/`
is byte-identical to the Phase 11 closure, which is byte-identical to the
Phase 10 closure.

**Nothing in this document was measured.** The audit in sections 1–2 is read
off current Source and says where; every candidate figure in sections 9–14 is
a **hypothesis with a stated reason**, not a result. Stage 2 is where they are
measured, and section 29 says how. No test was added, so the regression is
unchanged at 2573 / 2591.

Five decisions in here are the product owner's rather than mine. They are
collected in section 30, before the commit hash.

---

## 1. Configurable engine dimensions

Classified as the brief asks: **A** already configurable, **B** small data/API
exposure, **C** new DSP architecture.

| dimension | class | what Source actually provides |
|---|---|---|
| ER pattern (events, window, envelope, occupancy, floor) | **A** at prepare, **B** at run time | `er::Pattern` is a plain struct and `EarlyReflections::prepare (sampleRate, pattern)` takes one. But it is taken **only at prepare**, which sizes the tap vectors to `pattern.events`. There is no run-time pattern change |
| ER timing distribution | **A** | `windowMs`, `cellOccupancy`, `earliestFraction`, `sizeAlpha`, `envelopeDecay` — all Pattern fields. Capacity: span ≤ `maxSpanMs` 160 ms, events ≤ `maxEvents` 32 |
| ER channel geometry | **B** | L and R positions and signs are four draws per event from one xorshift stream with a **hard-coded seed** inside `buildSchedule`. Not a Pattern field. A Mode can change how many events there are, not how the two channels relate |
| ER level relationship | **B** + measurement | the BAL-4 table (`calibrationTable`, 5 × 3) is a file-scope constant **measured against the shipping pattern and the shipping tank**. It is valid for neither a new pattern nor a new delay set |
| Early/Late behaviour | **A** (law) | `gainsForBalance` is pattern-independent by construction: total energy linear in the control for any K |
| injection vectors | **A** per bank, **B** through the tank | `StereoFdn::setStereoVectors` is public and run-time. `MorphingStereoFdn` does not forward it; it is reachable only via `getBank (i)` |
| readout vectors | **A** / **B** | same call |
| diffuser delay configuration | **B** | `StereoDiffusionNetwork::setDelayPatterns` and `setBasePattern` exist, but the diffuser is a **private member** of `StereoFdn`, and `StereoFdn::prepare` hard-codes `fdnInputPattern`, 4 stages, amount 0.7 |
| input diffusion structure | **B** | `setStageCount` (storage for 8), `setSharedStages`, `setCrossCoupling` (default 0.0), `setCrossCouplingStage` all exist on the network and are unreachable from outside the bank |
| FDN delay set | **B** | `StereoFdn::setDelayPattern` exists and refuses rather than clamps. Two limits: **capacity is fixed at prepare** from the *current* pattern's longest line × `maxSizeScale` (165.84 ms), so a longer set returns `exceedsCapacity`; and it steps a ringing loop, so it may only ever be applied to the silent bank |
| spatial scale | **A** | `sizeAlpha` for the ER; the pattern's own milliseconds for the tank |
| modulation configuration | **A** | depth, rate, shape, schedule, update mode, allpass depth/rate/shape/mode, active lines, sections per line (1 or 2, storage reserved) — all public on `MorphingStereoFdn` |
| damping configuration | **A** | `setDamping (lowRatio, highRatio, …)`; the control-to-ratio constants are file-scope |
| feedback matrix | **A** | `setMatrixType`: hadamard (shipping), householder, identity (tests) |
| in-loop static diffusion | **A**, with a warning | `setStaticDiffusion`, weight 0 by default. Phase 9 **rejected** this as a user control because it raised periodicity at Mod Depth 0. It is available; it is not thereby cleared |
| Size mapping | **A** (universal) / **B** (per-Mode range) | `sizeScaleFor` is geometric over `fdn::minSizeScale … maxSizeScale`, and `StereoFdn::setSizeScale` clamps to the same two globals |
| Size level compensation | **B** + measurement | `sizeComp::tableDb` (10 × 9) is **measured for the shipping delay set**. One instance per bank already exists, which is the right shape; the table itself is global |
| wet trim | **B** + measurement | `previewWetTrim` / `tankPowerGain` are derived for the shipping tank |
| Decay mapping | **A**, and should stay universal | `rt60For` clamps; per-line gains come from `feedbackGainForRt60 (delay, rt60)`, so RT60 holds **by construction for any delay set** |
| Attack interaction | **A** | `AttackInjection::prepare (sampleRate, taps, maximumSpreadMs)`; identical schedule on both channels |
| Width | **A**, and must not vary | mid/side, exact identity at 100 % |
| Pre-Delay | **A**, and must not vary | common tap index |
| **the Mode switch itself** | **B** | `beginMorph (scale)` reconfigures the silent bank, primes it, and equal-power crossfades the outputs. It applies **Size and nothing else**. Everything a Mode needs applied to the incoming bank has to ride the same transaction |

### What the audit changes about the plan

Phase 11's closure said a Mode "can vary these today". That was true **per
bank and at prepare**, which is the weaker claim. Three things are not
reachable through the object the processor actually owns, and one is not
reachable at run time at all. None of them is new DSP; all of them are
exposure. Recorded here because my own closure statement was more generous
than Source supports.

## 2. Dimensions requiring new DSP

| | class | status |
|---|---|---|
| per-channel tanks (true-stereo late field) | **C** | not proposed. Roughly doubles the tank, gives up shared-mode density |
| cross-channel coupling as a parameter | **C** | the coupling *is* the 8×8 matrix plus the vectors. Not proposed |
| ER cross-feed | **C** | the ER layer has no cross-channel path. Not proposed |
| FDN order other than 8 | **C** | `fdn::order` is a compile-time constant behind every vector and table. Not proposed |
| true plate dispersion (frequency-dependent arrival, the chirp) | **C** | needs long allpass cascades. **Rejected for V1**, section 20 |
| nested or series tank topologies | **C** | not proposed |

**No candidate in this document requires class C.** That is a constraint I
held the candidates to, and it is also the main risk: section 29 names the
results that would force the question back open.

## 3. Mode / Character boundary

> **MODE is geometry. CHARACTER is transfer.**

| | MODE owns | CHARACTER owns (Phase 13) |
|---|---|---|
| kind of data | **times, counts, signs, selections** | **gain versus frequency, nonlinearity, modulation style** |
| concretely | ER pattern · delay set · diffuser delay set and stage count · sections per line · Size-to-geometry law · per-Mode calibration tables | damping voicing and ranges · modulation waveform and rate family · saturation · bandwidth · noise or texture |
| answers | *what shape is this space, and how does energy arrive in it?* | *what is it made of, and which decade built it?* |

Three quantities are wanted by both, and the rule for each is fixed now:

| contested | rule |
|---|---|
| **modulation depth** | composes multiplicatively. A Mode may carry a **bounded** factor (proposed 0.6–1.4×) justified only by its geometry — a delay excursion of fixed size is a larger pitch excursion on a short line. Waveform, rate family and any pitch-cents ceiling are Character's |
| **diffusion** | stage count and delay set are Mode. The coefficient remains the user's Early Diffusion. Character may not change stage count |
| **brightness** | **Mode does not touch damping at all.** No candidate below changes a damping constant. Plate is not made bright here |

The last row is a deliberate restriction and it costs something: a Plate that
is not brighter than a Hall is less immediately "a plate". It is also what
makes *"Hall with brighter damping"* impossible **by construction** rather than
by good intentions, and it leaves Phase 13 an untouched axis. If Stage 2 shows
a Mode cannot be identified without a tonal cue, that is a finding to bring
back, not a licence to add one quietly.

## 4. ROOM — target

| | |
|---|---|
| perceptual | compact, believable, strong early spatial identity, fast buildup, short perceived dimensions, controlled tail |
| DSP signature | early energy arrives inside ~40 ms and dominates the first 80 ms · short centre time · echo density reaches saturation fastest of the natural-space Modes · late field starts early enough that there is no handoff valley to hear |
| must not be | Hall with shorter Decay — **tested directly**, section 17 D-2 |
| distinguishing axis | **early-field structure and buildup speed**, not tail length |

## 5. HALL — target

| | |
|---|---|
| perceptual | large, smooth, spacious, slower buildup, broad late field, long decay capability, elegant rather than metallic |
| DSP signature | longest loop delays of the natural-space Modes · later centre time · a clear but bridged gap between early and late · smooth, saturated late density |
| must not be | Room with longer delays |
| distinguishing axis | **scale and the pace of buildup** |

**Hall is proposed as the baseline Mode: bit for bit the current engine.** The
argument is in section 23, because it is a state-compatibility argument more
than an acoustic one.

## 6. CHAMBER — target

| | |
|---|---|
| perceptual | dense, lively, reflective, more energetic than Hall, clear early-to-late interaction, medium scale |
| DSP signature | the **highest early-field energy density** of the five · an early layer still sounding when the late field arrives, so the two overlap rather than hand off · a tightly clustered delay set |
| must not be | Room or Plate |
| distinguishing axis | **early/late overlap and energy density**: Room hands off quickly, Hall hands off across a gap, Chamber does not hand off at all |

Chamber is the Mode I am least sure is a distinct family rather than a point
between two others. Section 17's impostor test is aimed at it specifically.

## 7. PLATE — target

| | |
|---|---|
| perceptual | artificial by design, fast dense onset, minimal room impression, smooth dense tail, weak geometric ER identity |
| DSP signature | **no resolvable early reflections** · echo density at or near saturation almost immediately · negligible L/R time structure in the first 30 ms · narrowest delay spread |
| must not be | Hall with brighter damping — excluded by section 3 |
| distinguishing axis | **absence of geometry** |

## 8. AMBIENT — target

| | |
|---|---|
| perceptual | spacious, enveloping, soft buildup, broad stereo, no obvious physical room, long atmospheric tails, still musical and controllable |
| DSP signature | **latest centre time** · no identifiable first reflection · the slowest rise · the widest delay spread |
| must not be | shimmer, Supermassive, freeze. Section 21 |
| distinguishing axis | **onset softness** — the only Mode where the attack of the reverb is itself smeared |

## 9. First configuration family for each Mode

**Hypotheses.** Relative to the baseline: delay set 26.27–82.92 ms, sum
388.50 ms, max/min ratio 3.16; ER 8 events over 80 ms; input diffuser 4
stages, about 14.6 ms.

| | ROOM | HALL | CHAMBER | PLATE | AMBIENT |
|---|---|---|---|---|---|
| late delay sum vs baseline | 0.45–0.60× | **1.00×** | 0.70–0.85× | 0.50–0.65× | 1.25–1.50× |
| late spread (max/min) | ~3.2, irregular | **3.16** | ~2.0, clustered | ~1.5–1.8 | ≥ 4 |
| ER events | 8–12 | **8** | 12–16 | 24–32 | 12–16 |
| ER window | 35–45 ms | **80 ms** | 50–60 ms | 20–30 ms | 120–150 ms |
| ER envelope decay | ~0.8 (front-loaded) | **1.5** | ~2.5 (sustained) | flat | flat |
| ER earliest fraction | 0 | **0** | 0 | 0 | ~0.15 |
| ER Size alpha | ~0.35 | **0.25** | 0.25 | ~0.1 | 0.25 |
| input diffuser stages | 4, delays scaled down | **4** | 6 | 8 | 8, long set |
| allpass sections per line | 1 | **1** | 1 | 2 | 1 |
| modulation factor | 0.6–0.8× | **1.0×** | ~0.9× | 0.8–1.0× | 1.2–1.4×, slower |
| stereo vectors | baseline | **baseline** | baseline | baseline | baseline |
| matrix | hadamard | **hadamard** | hadamard | hadamard | hadamard |
| damping | untouched | **untouched** | untouched | untouched | untouched |
| Attack, Width, Pre-Delay | untouched | **untouched** | untouched | untouched | untouched |

The last four rows are constant on purpose. **A row that does not vary is a
decision, not an omission**, and each has its reason in the sections below.

## 10. Mode-specific ER strategy

| Mode | philosophy | why |
|---|---|---|
| ROOM | few, early, front-loaded, strongly different between channels | a small room is identified by its first reflections. The geometry *is* the identity |
| HALL | the shipping pattern | accepted through four listening rounds in Phase 10 |
| CHAMBER | more events, sustained envelope, window reaching past the late onset | "clear early-to-late interaction" needs the two layers audible at once |
| PLATE | very many events in a very short window, flat envelope | section 20 |
| AMBIENT | spread wide, no early bias, first event held off | a first reflection is a room cue. Removing the cue is the point |

Two limits found in the audit apply here.

- **Channel geometry is not data.** Room wants strong L/R difference and Plate
  wants almost none. Today both would get the same seeded draws. Needs the
  class-B exposure in section 26, with defaults that reproduce the current
  pattern fingerprint `5065939454054309816` exactly.
- **K is per Mode.** Every non-Hall Mode needs its own BAL-4 table, measured
  the way Phase 10's was. Fifteen measurements per Mode, and not optional:
  Phase 10's first placeholder K was 2.1 dB out.

## 11. Mode-specific Late strategy

Order 8, Hadamard, the shipping vectors and the shipping decay law are
**constant across all five**. What varies is the delay set: its scale and its
spread.

| Mode | set | reasoning |
|---|---|---|
| ROOM | short, irregular | short loops recirculate early — the tank's own first passes land inside the ER window and become part of the early field |
| HALL | shipping | — |
| CHAMBER | medium, **clustered** | lines of similar length return energy in bunches rather than evenly: dense and lively rather than smooth |
| PLATE | short-to-medium, **narrowest** | minimises audible structure in the onset. The cost is in section 20 |
| AMBIENT | long, **widest** | late first return and maximally uneven recirculation: slow, soft, no pulse |

**Every non-Hall set must come from a directed search**, the way the shipping
set and both diffuser sets did. With eight delays, hand-picking creates
coincidences faster than it removes them; `DiffusionTuning.h` already records
that lesson. Stage 2 searches; it does not guess.

**Phase 11 constrains expectations here.** Stage 3 tested four delay sets
against the narrow-band mono condition and none improved on production. A new
delay set is being asked to change **buildup and texture**. It is not being
asked to fix the mono characteristic, and it is expected to inherit it.

## 12. Mode-specific diffusion strategy

| Mode | stages | set | reasoning |
|---|---|---|---|
| ROOM | 4 | shipping input set, scaled down | less pre-smear, so discrete early arrivals stay discrete |
| HALL | 4 | shipping | — |
| CHAMBER | 6 | shipping set, six stages | denser onset without Plate's total smear |
| PLATE | 8 | shipping set, all eight | density has to exist before the first loop return |
| AMBIENT | 8 | a **long** set | this is what softens the attack |

`DiffusionTuning.h` states the long Phase 3 set was "kept for the large and
ambient algorithms in later phases". It also states why it was **not** used in
front of the tank: 50.78 ms left against 39.70 ms right, so the onset arrives
11 ms later in one ear — "a localisation cue, not a reverb". **Ambient cannot
use it as it stands.** It needs a long set searched under the same
balanced-total constraint the short one was. That is a search, not new DSP.

Early Diffusion remains the user's control of the coefficient in every Mode.

## 13. Mode-specific spatial strategy

**The injection and readout vectors do not vary by Mode.** This is the row I
expected to use and am proposing to leave alone.

Phase 11 measured it three ways. Every orthogonal ±1 readout pair cancels four
lines in mono — 1120 pairs, none exempt. Rotated readouts relocate the
narrow-band failure rather than remove it. Injection blending produced no
source-side retention at all, because the matrix scrambles direction within a
few traversals. **There is no demonstrated perceptual effect to assign to a
Mode**, and the brief's rule is that a dimension without one is not varied.

Spatial identity therefore comes from where Phase 11 found it actually lives:

| lever | carried by |
|---|---|
| source-side localisation and early width | **the ER pattern's channel geometry** — strong in Room, weak in Plate |
| onset decorrelation | the two diffuser chains' differing delay sets |
| perceived scale | delay-set scale and ER window |

One exception is held open. Rotated readouts measurably improved swap symmetry
(0.64 → 0.05 dB) and broadband centring (2.22 → 0.17 dB). If Stage 2 finds
Ambient benefits from that and the mono cost is no worse than production's, it
is a candidate for Ambient only.

## 14. Mode-specific modulation tendency

| Mode | factor | reasoning |
|---|---|---|
| ROOM | 0.6–0.8× | the same delay excursion is a larger pitch excursion on a short line, and a short tail leaves nothing to hide it behind |
| HALL | 1.0× | — |
| CHAMBER | ~0.9× | — |
| PLATE | 0.8–1.0× | a real plate does not move, but an eight-line tank without motion rings. The factor stays near 1 for that reason, not for authenticity |
| AMBIENT | 1.2–1.4×, slower rate | long tails expose stationary modes; slower keeps it from becoming chorus |

Two measured facts bound this. **Modulation is 46.5 % of the tank's CPU**, so
the factor changes depth, never the number of modulated elements. And
modulation made the narrow-band mono condition **mildly worse** (17.5 % →
22.5 % of tones affected), so raising it in Ambient has to be re-measured
against Phase 11's gate rather than assumed harmless.

## 15. Size semantics

**Size means larger, monotonically, in every Mode.**

- The control law stays the universal geometric one: equal ratio per unit of
  travel, over the same 8× range.
- A Mode supplies the delay set that range is applied to. Size 20 % in Room is
  smaller than Size 80 % in Room. It is not promised to equal Size 20 % in
  Hall, and should not — a small hall is not a small room.
- The ER span follows through each Mode's own `sizeAlpha`.

**One open question, not decided here.** Room's short set at the bottom of an
8× range puts loop delays under 10 ms, which is repetition pitch rather than
reverberation (section 19). A narrower range for Room would fix that and is
class B. It would also mean Size's travel differs by Mode. Stage 2 measures
whether the universal range survives before anything is narrowed.

Gate proposed: per Mode, centre time and early centroid are **monotone
non-decreasing in Size** across the full travel.

## 16. Decay semantics

**Decay is RT60 in seconds and is not Mode data.**

The per-line gain is derived from each line's own length, so the decay rate
holds for any delay set by construction. No Mode gets a Decay law, a Decay
range or a Decay multiplier.

"Where technically intended" needs one honest qualifier. At Decay 0.1 s a
150 ms line loses about 90 dB per pass, which is one echo, not a 0.1 s reverb.
Long-set Modes at the very bottom of the Decay range will be physically
degenerate the way the baseline already is at large Size. That is to be
**reported per Mode as a usable-range floor**, not hidden by clamping.

Gate proposed: per Mode, mid-band T30 against the Decay control within a
stated tolerance over the usable range, using the Phase 7 analyser. **Not** the
Phase 8 RT60 sensitivity harness, which is known-bad and stays out of gates.

## 17. Distinctness metrics

### The features

All taken from the wet path after energy matching (section 18), so level is
not a feature.

| | metric | expected to separate |
|---|---|---|
| F1 | time to normalised echo density 0.9 | Plate from everything; Room from Hall |
| F2 | early temporal centroid, 0–100 ms | Room / Chamber / Ambient |
| F3 | clarity-style early/late ratio at 50 and 80 ms | Room and Chamber from Hall and Ambient |
| F4 | centre time, and time to 50 % / 90 % cumulative energy | Hall and Ambient from the rest |
| F5 | handoff: depth of the energy valley between early and late | Chamber (none) from Hall (present) |
| F6 | short-time L/R correlation at 0–20, 20–80, 80–300 ms, and late | Room (early structure) from Plate (none) |
| F7 | side/mid energy development over the same windows | Ambient |
| F8 | onset periodicity and gap repetition | flutter guard; Chamber's clustering |
| F9 | smoothed spectral ripple and fine modal prominence | Plate smoothness; ringing guard |
| — | octave-band RT60 normalised to 1 kHz | **should not separate the Modes at all** |

The last row is a control rather than a feature. If band RT60 differs by Mode
at identical damping settings, a Mode has acquired a tonal signature, and
section 3 says that belongs to Character.

### The criteria

**D-1 — pairwise separation.** All ten pairs differ by more than threshold in
at least three feature families.

**D-2 — the impostor test.** *This is the one that answers the brief's
question.* For each ordered pair (A, B): hold A at the Mode-neutral settings,
then search B's **user controls** — Size, Decay, Early Diffusion, Early/Late,
Attack, damping — for the setting that brings B closest to A.

> If some knob position in B reproduces A, then A is a preset of B.

"Room must not sound like Hall with shorter Decay" stops being a hope and
becomes twenty optimisations with a residual each. A family is distinct only
if **no** user-reachable setting of another Mode collapses onto it.

**D-3 — robustness.** A Mode stays itself across Size and Decay travel, and
under Character-range perturbation of damping and modulation style (section
25).

**D-4 — monotone controls**, sections 15 and 16.

### Thresholds are fixed before candidates are scored

The threshold for "different" is derived from within-Mode variation across
sources, and from published just-noticeable differences where they exist, and
**written down before any candidate is evaluated**. A threshold chosen after
seeing the numbers measures how much I wanted the candidates to pass.

**Limit, stated now:** these metrics can establish that five responses differ.
They cannot establish that the difference is the *intended* one, or that it is
heard as a family. That is the listening protocol's job, in section 29.

## 18. Loudness-normalisation strategy

Two separate things, kept separate.

**In the product: static per-Mode calibration.** Each Mode gets its own Size
level compensation table, wet-trim reference and BAL-4 table, measured offline
and stored as constants. Target: integrated wet energy at each Decay × Size
point matches Hall's within a stated tolerance. **This is data, not
processing** — the kind the engine already ships for Size.

It is **not** AGC, real-time energy normalisation or any detector-driven gain.
Those stay forbidden.

**In evaluation: matched diagnostic renders.** Measure wet RMS, integrated
energy, early energy (0–80 ms) and late energy (80 ms on) per Mode per source.
Report all four, because matching one does not match the others: a Plate and a
Hall of equal integrated energy still differ in early energy, and that
difference *is* the Mode.

So the rule for comparisons: match **integrated wet energy**, report the
early/late split as a finding, and never match early and late separately —
doing so would erase what is being compared.

The residual after calibration is itself a gate: Mode-to-Mode integrated
energy within tolerance at the Mode-neutral settings, across the source set.

## 19. ROOM small-space risk analysis

Phase 11 found its hardest behaviour at small Size and short Decay. Room lives
there by definition.

| risk | mechanism | expectation |
|---|---|---|
| **repetition pitch** | a loop delay under ~20 ms recirculates above 50 Hz and is heard as pitch. The baseline already reaches 6.6–20.7 ms at minimum Size; a Room set at half the length reaches ~3–10 ms | **the main risk, and it gets worse** |
| metallic ringing | resolved modes. Modal overlap scales as total loop delay ÷ RT60, so short Decay *helps* | less severe than at Hall settings, counter to intuition |
| flutter / repetition | few lines returning at regular intervals on a transient | depends entirely on the searched set |
| mono fold-down | Phase 11's narrow-band condition, found at **every** Size | **inherited, not created.** Reopened only if Room is perceptually worse than the accepted case |
| ER audibility | Room's identity rests on the ER layer, and Early/Late defaults to 0 | **a design problem, not a DSP one** — below |
| buildup speed | should be the fastest natural Mode | follows from the short set |

### The ER default problem

Room's target is "strong early spatial identity". The ER layer is off at
Early/Late 0, and 0 is the parameter's default and the legacy-neutral value.
So at default settings Room has no ER layer at all.

Three ways out, and I reject two:

| | | verdict |
|---|---|---|
| a per-Mode Early/Late floor | 0 would mean "some early" in Room | **rejected** — 0 stops meaning full Late, which breaks the brief's section 4 |
| Mode moves the Early/Late knob | selecting Room sets the control | **rejected** — a parameter that moves other parameters is unpredictable under automation |
| **Room's late engine carries early identity by itself** | short loops put the tank's own first passes inside the early window | **proposed** |

The third is why Room's delay set is short, and it is a real constraint on the
design: **Room must be identifiable at Early/Late 0.** Stage 2 tests exactly
that. If it fails, the question of what Mode-neutral settings are comes back
to you (decision 3).

### Required Room validation, from the brief

Small Size · short Decay · dense transient input · mono fold-down · ER
audibility · buildup speed · metallic ringing · flutter and repetition — each
as a measurement against Hall at the same settings, with the Phase 11 gates as
the reference for "no worse".

## 20. PLATE architecture analysis

### What a plate is, and what this engine can do about it

A plate has no geometry: no walls, so no discrete reflections. It is dense
from the first instant. And it is **dispersive** — high frequencies travel
faster, so an impulse arrives as a downward chirp.

| property | reachable? |
|---|---|
| no discrete reflections | **yes** |
| immediate density | **yes**, with a cost |
| dispersion | **no** — class C. Rejected for V1 |

Dispersion needs long allpass cascades. It is new DSP whose only purpose is
authenticity in one Mode, and it fails the product principle's fourth test.
**Plate here is plate-like in density and geometry, not a plate model.** That
should be said plainly in the product rather than discovered by a reviewer.

### The ER branch: the four options the brief lists

| option | assessment |
|---|---|
| heavily reduced | a level change. Leaves a Hall's reflection pattern underneath, just quieter. **Kept only as the Stage 2 control arm** |
| **replaced by a plate-specific pattern** | **proposed.** 24–32 events in 20–30 ms with a flat envelope is not a set of reflections; it is a burst of density. The layer stops describing a room and starts supplying the onset |
| delayed relative to Late | `earliestFraction` exists. But a gap before early energy is a room cue, the opposite of the target. **Rejected** |
| removed for Plate | Early/Late would do nothing in one Mode. **Rejected** — a dead control is worse than a subtle one |

**Early/Late keeps its meaning in Plate**: 0 is the tank alone, 100 is the
onset burst alone, and total energy stays linear in between because the law
does not depend on the pattern. What changes is what "early" sounds like.

The ER architecture is not removed, globally or for Plate.

### The cost

Fast onset wants short loops. Smooth, dense tails want long total loop delay.
With eight lines those pull against each other, and Plate's narrow, shortish
set sits at the low-modal-density end. Three things are stacked to compensate
— eight diffuser stages, two allpass sections per line, near-baseline
modulation — and **I do not know that they are enough.** Plate is the Mode
most likely to come back from Stage 2 as "not achievable in class A/B".

## 21. AMBIENT scope confirmation

Ambient is built from: a long, widely spread delay set · a long, balanced
diffuser set · a late-biased ER pattern with no first reflection · somewhat
deeper, slower modulation.

| excluded | status |
|---|---|
| shimmer / pitch shifting | none |
| granular processing | none |
| large feedback-delay special FX | none — the same eight-line tank |
| freeze | none |
| infinite reverb | none — Decay's ceiling is the same `maxRt60Seconds` and `maxFeedbackGain` as every Mode |

**No new mechanism.** Every item is configuration of a stage that already
exists. Ambient's long tails come from the Decay control the user already has,
which is why it stays a general-purpose reverb.

The one thing to watch: "deeper, slower modulation" is the first step toward a
chorus effect. The factor is capped at 1.4× for that reason.

## 22. Mode switching strategy

### The existing infrastructure is the right one, and it is not yet sufficient

`MorphingStereoFdn` already does a program change properly: the silent bank is
reset and reconfigured, primed with the live input until its level settles,
then equal-power crossfaded at the outputs. The two feedback loops never
touch. A request arriving mid-transition overwrites one pending slot. There
are never more than two banks.

Each of the brief's requirements maps onto a property that already holds for
Size:

| requirement | how |
|---|---|
| no click | outputs are crossfaded; delays never move while read |
| no explosion | the incoming loop starts empty and is only ever fed input |
| no invalid state | configuration **refuses** rather than clamps (`ConfigResult`); on refusal the engine stays on the current bank |
| predictable automation | coalesced, last value wins, bounded by `getWorstCaseTransitionSamples` — about 1.62 s |

### What is missing

1. **`beginMorph` applies Size and nothing else.** A Mode transaction must
   apply the delay set, diffuser set and stage count, sections per line and
   compensation table to the **incoming bank only**.
2. **Several setters write both banks at once** — modulation, sections per
   line, static diffusion. Mid-transition the two banks are different Modes
   and need different values.
3. **Three clocks.** The tank fades over 120 ms after priming; the ER layer
   crossfades schedules over 30 ms; K ramps over 20 ms. A Mode change must
   move all three together, or the early layer of the new Mode plays over the
   tail of the old one.
4. **The ER layer cannot change pattern at run time** (section 1).
5. **Capacity** must be the maximum over all five Modes at prepare.

All class B. **None is new DSP**: it is the Size transaction, generalised from
one number to a configuration.

### One behaviour to decide rather than inherit

A Size morph cuts the old tail off in 120 ms. For a Size change that is right.
For a Mode change on a 6 s tail it may not be — the alternative is to stop
feeding the old bank and let it ring out. That is nicer, and it loses the
"never more than two banks" bound unless rapid switching is coalesced harder.
**Stage 2 renders both.** Default until then: the existing behaviour.

## 23. Parameter and state strategy

### What is in the repository today

`ParameterLayout.cpp` already registers a **`mode`** parameter: an
`AudioParameterChoice` with **thirteen** entries, default `largeHall`,
`versionHintV1`. It is serialised and automatable. **No DSP reads it.** Its
enum comment says each entry is "an independent DSP topology, not a preset of
a shared engine", which the V1 scope has since superseded.

### Two options

| | A — append a new five-entry parameter | B — give the existing `mode` meaning |
|---|---|---|
| parameter | new ID, five choices | the thirteen-entry `mode` |
| historical IDs / hints | **untouched** | untouched |
| old session, `mode` at default | Hall ≡ baseline. **Identical** | Large Hall ≡ baseline. Identical |
| old session, `mode` touched | **still identical** — the old parameter stays inert | **changes sound on reload** — a control that did nothing now does something |
| host automation list | five names | thirteen names, eight of them aliases or dead |
| cost | a second "Mode" in host lists | thirteen entries for five families, permanently |

**Recommended: A.** It is the only option where the guarantee is unconditional
— *no existing session changes sound* — rather than holding only for sessions
that never touched a dead control.

### Proposed design, under A

| | |
|---|---|
| type | `AudioParameterChoice`, five entries |
| order | Room, Hall, Chamber, Plate, Ambient — append-only from here |
| **default** | **Hall** |
| ID | new, appended after `earlyLate`. Name to be confirmed |
| versionHint | a **new** `versionHintV3`. V1 and V2 never change |
| smoothing | none; discrete. A change starts one coalesced transaction |
| legacy `mode` | stays registered, serialised and inert, exactly as today |

### Why Hall is the default, and why Hall is the baseline

A session saved before Phase 12 has no value for the new parameter, so it
loads the default. **If the default Mode is bit-identical to the current
engine, every old session renders as it did**, and Phase 10's 12-of-12 legacy
byte identity survives Phase 12 **without a re-baseline**. The whole legacy
guarantee reduces to one property of one Mode.

It is also the honest acoustic reading. The shipping delay set runs 23–72 ms
at the default Size, the default Decay is 2.5 s, and the historical default
enumerator is `largeHall`. The current engine is a hall.

### Two traps found while reading

- **`versionHintLatest` would silently re-hint `earlyLate`.** `appendedPid`
  uses `versionHintLatest`, so moving that constant to V3 would move
  `earlyLate` with it. The state suite already pins `earlyLate` to
  `versionHintV2` and would catch it, but the right order is to pin
  `earlyLate` to V2 explicitly **before** adding V3.
- **A five-entry choice is not freely appendable.** Hosts store automation
  normalised as index ÷ (count − 1). A sixth Mode later would move every
  existing lane. Saved *state* survives; saved *automation* does not.
  Reserving slots now avoids that, at the price of visible dead entries.
  **Decision 2.**

### If Hall cannot be the baseline

If Stage 2 shows the Hall target needs different data from the current engine,
the fallback is the one the brief describes: establish an explicit Phase 12
baseline, rerun the Phase 10 behavioural gates and the Phase 11 spatial gates,
and record that waveform identity ended deliberately. Not silently.

## 24. CPU and memory impact

**Estimates from Source and from Phase 11's measured stage costs. Not
benchmarked.**

| change | where | effect |
|---|---|---|
| different delay set | all non-Hall | **none per sample** — same eight lines |
| ER events 8 → up to 32 | Plate | 16 → 64 multiply-accumulates. ER measured at 0.053 % of real time, so roughly 0.2 % |
| diffuser stages 4 → 8 | Plate, Ambient | doubles the diffuser. Storage already reserves eight. **Its share of tank cost has not been isolated** |
| allpass sections 1 → 2 | Plate | doubles the in-loop allpass work. Modulation is 46.5 % of the tank, so this is the largest single item. **To be benchmarked first** |
| modulation factor | all | **none** — depth, not count |
| Mode transition | all | two banks for at most ~1.62 s. Same as a Size change |

Against the brief's four questions:

| | |
|---|---|
| additional delay memory | **yes, for Ambient.** Capacity is global, so the longest Mode sets it for all. Today each line reserves 165.84 ms. A 1.5× set needs about 249 ms, which may cross an allocation-rounding boundary and as much as **double** tank memory for every Mode. To be read off, not estimated. Capping Ambient near 1.2× may avoid it |
| additional allpasses | **no new ones.** Plate enables a second section per line that is already reserved |
| additional feedback paths | **none** |
| extra per-sample work | Plate (ER taps, diffuser stages, second section) and Ambient (diffuser stages). Room and Chamber should be within noise of Hall |

**Mode changes configuration, not the number of processing stages** — with the
exceptions listed, each of which is an existing stage switched on or lengthened
rather than a new one.

## 25. Phase 13 Character compatibility

*Can Modern / Classic / Vintage be applied cleanly on top?*

| Mode | answer | why |
|---|---|---|
| ROOM | **yes** | identity is ER geometry and loop scale. Untouched by tone |
| HALL | **yes** | the baseline; Character is free to voice it |
| CHAMBER | **yes** | identity is overlap and clustering |
| PLATE | **yes, with the largest caveat** | "vintage plate" is the strongest association in the whole matrix. Because Plate here carries no tonal data, Vintage Plate is entirely Phase 13's to build — which is the point, and also means Plate may feel unfinished until then |
| AMBIENT | **yes, watch modulation** | Ambient's factor (≤ 1.4×) times a modulation-heavy Character could leave the general-purpose range. Phase 13 needs a cap on the **product**, not on each factor |

The boundary holds because no Mode owns damping, saturation, modulation
waveform or bandwidth. The brief said to redesign the boundary now if
responsibilities overlapped too strongly; they overlap in exactly one place,
modulation depth, and section 3 fixes the composition rule.

Section 17's D-3 is the test: perturb each Mode by a Character-sized change in
damping and modulation style, and confirm it is still classified as itself. A
Mode that only survives at neutral Character is not a Mode.

## 26. Recommended implementation order

For the production stage, after Stage 2. Each step is separately gateable.

| | step | gate |
|---|---|---|
| 1 | pin `earlyLate` to `versionHintV2` explicitly | state suite 26/26, unchanged |
| 2 | ER: fixed-capacity tap storage, run-time pattern change through the existing schedule crossfade; seed and channel spread as Pattern fields | fingerprint `5065939454054309816` unchanged; legacy 12/12 |
| 3 | tank: capacity from the maximum over Modes; diffuser configuration reachable through the bank | legacy 12/12; allocation gate |
| 4 | tank: generalise the Size transaction to a configuration transaction | every existing morph test unchanged; legacy 12/12 |
| 5 | a single `ModeConfig` data record, with **Hall = the current constants** | legacy 12/12 — the proof that Hall is the baseline |
| 6 | append the Mode parameter, default Hall, wired to Hall only | old sessions byte-identical |
| 7 | add Modes one at a time, each with its own calibration tables | per-Mode gates from section 28 |
| 8 | Mode switching coordination across tank, ER and K | click, explosion and automation gates |

Steps 1–6 add a Mode system containing one Mode, and **change no sound**. That
ordering is the point: the infrastructure is proved neutral before any new
behaviour depends on it.

## 27. Rejected ideas

| idea | why |
|---|---|
| Modes as parameter presets | the thing the brief forbids, and what D-2 exists to detect |
| a per-Mode Decay law or range | Decay 1 s must mean 1 s |
| a per-Mode Early/Late floor | 0 must mean full Late |
| Mode moving other parameters | unpredictable under automation |
| per-Mode damping or tonal tilt | consumes Phase 13's design space |
| per-Mode stereo vectors | no demonstrated perceptual effect, on Phase 11's own evidence |
| per-Mode Width behaviour | Width must remain Width |
| true plate dispersion | class C, one Mode, authenticity only |
| separate left and right tanks for a "wide" Mode | class C; doubles the tank |
| static in-loop diffusion as Mode data | rejected in Phase 9 for raising periodicity at Mod Depth 0. A Mode default does not change that finding |
| a different FDN order per Mode | class C |
| reusing the thirteen-entry `mode` | changes the sound of sessions that touched a dead control |
| deleting the legacy `mode` parameter | breaks sessions and automation that reference it |
| output decorrelation for Ambient width | Phase 11's decision stands |
| shimmer, freeze, granular, infinite | out of V1 scope |
| matching early and late energy separately across Modes | erases the difference under test |
| AGC or detector-driven loudness matching | forbidden, and unnecessary: static tables suffice |

## 28. Tests to add

None were added in this stage.

**Stage 2, research harness only**

- candidate delay-set and diffuser-set searches, with the scoring recorded
- the nine features of section 17 per candidate per source
- D-1 pairwise separation and the **D-2 impostor** search
- D-3 robustness under Character-range perturbation
- energy tables for calibration; residual Mode-to-Mode loudness
- Room validation set (section 19) against Hall at equal settings
- Plate: ER-replaced against the ER-reduced control arm
- every Phase 11 spatial gate rerun per candidate, reported as *no worse* or *worse*

**Production stage**

- Hall ≡ baseline: legacy 12/12 through the Mode path
- a missing Mode parameter restores Hall; legacy `mode` at any value changes nothing
- version hints: nineteen at V1, `earlyLate` at V2, Mode at V3
- per Mode: Size monotonicity, Decay accuracy over the usable range, band-RT60 Mode invariance
- per Mode: BAL-4 traversal and Size-compensation residuals
- Mode switch: click, energy bound, no invalid state on refusal, coalescing, bounded transition time
- allocation gate across Mode changes at every sample rate
- sample-rate and block-size independence per Mode
- CPU per Mode against a stated ceiling

**Carried forward unchanged**: all eighteen intentional diagnostic failures.

## 29. Proposed Stage 2 research plan

Research only. No production change.

1. **Fix thresholds first.** Derive the distinctness thresholds and write them
   down before any candidate exists.
2. **Measure Hall.** All nine features on the baseline. Everything else is
   relative to it.
3. **Search, do not guess.** Delay sets for Room, Chamber, Plate and Ambient;
   a balanced long diffuser set for Ambient.
4. **Score in research-only harnesses**, using the per-bank public calls that
   already exist. Nothing enters `Source/`.
5. **Run D-1, D-2 and D-3.**
6. **Room and Plate special validation**, sections 19 and 20.
7. **Phase 11 gates per candidate.**
8. **Cost**: benchmark Plate's second allpass section and the eight-stage
   diffuser before relying on them; read off real memory for Ambient.
9. **Listening pack** prepared, not judged.
10. **Report and stop.**

### Results that would send this back

| if Stage 2 finds | then |
|---|---|
| Chamber fails D-2 against Room or Hall | Chamber is not a family in this architecture. Either it gains a class-C mechanism or the product ships four |
| Plate's onset is not dense enough in class A/B | the dispersion question reopens, with a cost attached |
| Room is not identifiable at Early/Late 0 | decision 3 returns to you |
| Hall's target needs non-baseline data | explicit Phase 12 baseline, section 23 |
| any Mode is perceptually worse on mono than the accepted Phase 11 case | Phase 11's diagnostic reopens for that Mode only |

The first row is stated without hedging because the V1 scope already says it:
**the mode count is not a target.**

### Human evaluation plan

For the eventual production stage. Not conducted here.

- **Blind identification.** Five unlabelled renders of one source at identical
  user settings, energy-matched. The listener assigns the five names. Chance
  is 20 %; the output is a confusion matrix, which says *which* Modes are
  confused and not only how many.
- **Attribute questions**, one per Mode: does Room feel compact · does Hall
  feel larger and smoother · does Chamber feel denser and more energetic ·
  does Plate feel artificial and fast · does Ambient feel broad and
  atmospheric.
- **Sources**: vocal, snare, piano, an acoustic or percussive source, pad,
  impulse. The existing deterministic generators cover most of these; whether
  a suitable acoustic source already exists is to be checked in Stage 2.
  Synthetic sources are adequate for metrics and thin for identity judgements,
  so the pack should also accept your own recordings.
- **Two settings**: the Mode-neutral defaults, and one short, small setting
  where Room should be at its best and Hall at its worst.
- Identical settings across Modes in every comparison. No per-Mode tweaking.

### Mode-neutral defaults

No factory presets are decided here. The Mode-neutral settings are simply
**the existing parameter defaults**, unchanged, in every Mode: Decay 2.5 s,
Size 60 %, Early/Late 0, and the rest as shipped. A Mode demonstrates its
identity there or it does not; the settings are not adjusted to help it.

## 30. Decisions for the product owner, and commit

| | decision | recommendation |
|---|---|---|
| 1 | a new five-entry parameter, or reuse the thirteen-entry `mode` | **new parameter**; legacy `mode` stays inert |
| 2 | exactly five choices, or reserved slots for later Modes | **exactly five**, accepting that a sixth would move automation lanes |
| 3 | must Room be identifiable at Early/Late 0 | **yes** — as the design constraint, tested in Stage 2 |
| 4 | Hall ≡ the current engine, bit for bit | **yes**, unless Stage 2 shows the Hall target needs otherwise |
| 5 | Mode carries no tonal data at all | **yes**, leaving brightness wholly to Phase 13 |

Commit: `52a23b4`

---

**Stage 1 ends here.** No production DSP was modified. Phase 13 is not started.
