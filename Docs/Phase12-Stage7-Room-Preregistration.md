# Phase 12 Stage 7 — Room: a new conceptual basis, pre-registered

Research only. `Source/` is unchanged. **Nothing in this document has been
built or measured.** It is written and committed before any of it is coded,
and Stage 7 stops here for a human decision.

The metric, its thirteen families, its thresholds (REPRODUCED ≤ 1.5,
DISTINCT ≥ 3.0) and the seven-control search are Stage 3's, frozen at
`c18ecce`. Stage 6's additions (Size "meaningful", the ringing lines, Hall's
search at five Sizes, the knob-dependent impostor, CPU ≤ 1.5 × Hall) stand as
written in `Phase12-Stage6-Preregistration.md`. No threshold is changed here;
one is added and is stricter (section 2).

## 0. What is closed

By decision after Stage 6: R4c and R5b are research references and controls,
not Room candidates. Their distance from Hall is their modal ringing, and
ringing must not be the identity of the Mode.

A new Room hypothesis may not rest on any of:

- excessive modal ringing, or tonal coloration;
- the Early/Late control, or an early layer heard beside a field (R1c: Hall's
  early layer reproduces it at once);
- shorter Hall delays, lower Size, or more diffusion;
- fixed-angle coupling;
- a local block, in any of its forms (R1–R4, RS1–RS8, RR1–RR8).

Also treated as closed for Room, because the same construction was measured
under another family's name and reproduced by Hall:

| idea | measured as | result |
|---|---|---|
| a fast decay held up by a slower one (EDT ≠ RT, absorption per reflection, anisotropic decay) | Chamber C2a, C2b, C3a–c | "Hall's Decay knob absorbs most of it" |
| reflections tapped from, or fed into, Hall-length loops | Stage 3 feed-forward pattern and multi-tap readout; C1 | reproduced |
| walls as scattering nodes joined by two-way lines; a lattice | Chamber K1, K3 | rejected, collapse at small / short |
| nested recursive diffusion in one loop | Chamber K2 | rejected |
| dispersion inside the loop (a modal density that varies with frequency) | Plate PT1 | reproduced (0.51–0.74) |
| un-mixed short loops ("wall pairs") | R3, C1 | rejected |
| where a panned source's reverb goes | Room B, Stage 3–4 | reproduced on a centred source |

## 1. Re-deriving the question

### 1.1 What Hall is, as a set

Hall is eight delay lines, one orthogonal mixing matrix that is the same at
every frequency, the same decay per second in every line, and a readout that
decorrelates the channels by sign. Its seven controls scale the delays
(Size), set the slope (Decay), add the common early layer (Early/Late),
smear the input (Early Diffusion), shape the onset (Attack), modulate, and
scale the side signal (Width).

Whatever those controls are set to, Hall's response keeps four properties:

1. **Its mixing time is set by Size.** Density is complete after a few loop
   passes, whatever Decay is. Stage 3 measured it: every network is at
   Gaussian density by 80 ms.
2. **Its echo density and its periodicity are one number.** Shorter loops
   are denser sooner and repeat sooner. It cannot be dense at 5 ms and
   free of repetition at 50 ms.
3. **Its mixing is the same at every frequency.**
4. **Its two channels differ by sign and mixture, not by time,** equally at
   every frequency.

### 1.2 Why everything collapsed at small / short

At Size 25, Decay 0.8 s the response is over in a few hundred milliseconds.
Any property with a time scale comparable to the decay is 20 dB down before
it is established. What survived there did so for one of two reasons:

- it lives in the first tens of milliseconds and does not recirculate
  (Plate's dispersion: 3.09 at small / short);
- its time scale is *tied to Decay*, so it shrinks with the response
  (Ambient A1b: 4.00; R4c: 6.38).

Stage 5's general finding was the second. It has a physical reading that the
earlier stages did not draw out, and it is the basis proposed here.

### 1.3 The basis: what sets the mixing time

Count the reflections that happen within the decay: N = RT / (mean free
path time).

| space | mean free path | RT | N | what the listener gets |
|---|---|---|---|---|
| hall | 30–40 ms | 2 s | 50–70 | mixing takes a time set by the room's *size* |
| hard chamber | 6–12 ms | 2–4 s | 300–600 | mixed long before anything has decayed |
| compact absorptive room | 5–10 ms | 0.2–0.5 s | 20–60, each losing 1–3 dB | **the sound is gone before the field has finished mixing** |

A compact room is not a small hall. In a small hall with short decay the
field is fully mixed within a few passes and then decays; in a compact room
the decay and the mixing happen on the *same* time scale, so the reflections
remain countable to the end. Its mixing time is set by its **decay**, not
its size.

This is the law R4c stumbled on (exchange time = 0.12 × Decay) and it is why
R4c was out of Hall's reach. R4c realised it with four short loops, which
ring. **The task is to keep the law and remove the loops.**

Stated as the requirement: *a Room must be unfinished at every Decay —
its texture must still be resolving when it has decayed 20 dB — without any
loop short enough to be heard as pitch or repetition, and without a tone of
its own.*

These three figures (reflection counts, the reading of R4c) are my
derivation, not a measured result.

## 2. Gates

Unchanged: G1 (one search per point), G2 (five seeds; SURVIVES needs no run
≤ 1.5 and every median ≥ 3.0), G3, centred view, three operating points,
Hall free to use all seven controls including Early/Late.

**Added for Room, stricter than Stage 6:**

- **Ringing is a precondition, not a quality flag.** Modal prominence must
  be *clean* — ≤ Hall + 3 dB (16.8 dB) at all three points — before a G1 or
  G2 result counts. "Materially reduced" (≤ Hall + 14) is no longer enough.
  A candidate that is distinct and rings is recorded as *distinct by
  ringing* and rejected.
- **The stopping family is read.** If Hall's search stops on "modal
  structure" at any point, the result is reported as resting on modes,
  whatever the prominence figure says.
- **Small / short first.** G1 is run at small / short before the other two
  points. A lowest run ≤ 1.5 there ends the hypothesis.
- **A visibility control before each candidate** (section 4): a synthetic
  version of the property applied to Hall itself. If the frozen metric does
  not score the control ≥ 3.0 against Hall's search, the hypothesis is not
  testable with this metric; it is reported as *not testable*, not as
  passed or failed, and is not built further. The metric is not extended.

Size, Decay (± 10 %), stereo and mono, octave bands (≤ 1 dB), CPU
(≤ 1.5 × Hall): as Stage 6.

Then, for anything that passes: Room against Ambient A1b and against Plate
PV3 in both directions, as Stage 6 section 4.

## 3. Three hypotheses

They differ in the axis on which Room leaves Hall's set: time (RN1),
frequency (RN2), space between the channels (RN3). They break properties 1–2,
3 and 4 of section 1.1 respectively.

### RN1 — unfinished mixing without loops

**Interpretation.** The room's reflections stay countable while it decays.
Early in the response there are few; the number per second grows on a time
scale that is a fixed fraction of Decay, so at −20 dB the texture is still
resolving. Nothing returns: no path is short enough to repeat within the
audible decay.

**State topology.** Four long delay lines, each at least 0.6 × RT60 long
(the first recirculation is then ≥ 36 dB down), mixed orthogonally only to
carry the response beyond that. The response inside the first pass is
*feed-forward*: sparse signed taps read along each line, their spacing drawn
at random (no periodic spacing), their number per unit length rising along
the line as ρ(t) = ρ₀ · exp(t / (k · Decay)), their weights following the
decay. Independent tap sets for left and right. No allpass, no short loop.

**Why it survives short Decay.** Its only time constants are the first tap
(Size) and k · Decay. At Decay 0.8 s the density law is compressed with the
response, exactly as R4c's exchange was.

**Why Hall cannot reproduce it.** Property 1: Hall is at full density by
80 ms at every setting, so against a field that is still sparse at 200 ms it
must differ on *late texture* (short-time fluctuation, 50–200 and 200–500
ms). To be sparse Hall has only large Size and no diffusion, which moves its
*buildup* and *energy profile*; and its loops then return inside the decay
(*recurrence*, *loop repetition*), where RN1 has nothing. Early/Late does
not help: the early layer ends, and RN1's sparseness does not.

**Expected Size behaviour.** Size sets the time of the first reflections and
the initial spacing ρ₀⁻¹ (the mean free path); the line length does not
follow Size. t10 should rise with Size through the initial gap. This is the
weakest part of the hypothesis: it may read as pre-delay. Predicted t10
ratio 1.3–1.8.

**Expected modal behaviour.** No recirculation inside the decay, so no modes
to stand out: modal prominence at or below Hall's 13.8 dB. This is the
opposite of R4c and is the first thing to be measured.

**Mono / stereo risks.** Independent sparse sequences fold to mono as a
denser sparse sequence: no comb, fold-down near −3 dB. Risk: with few taps
early, channel balance in any short window is uneven (image wander in the
first 50 ms).

**Tone risk.** A sparse random sequence is white on average and uneven over
any short stretch. With too few taps the octave bands will not be within
1 dB. This bounds ρ₀ from below.

**Perceptual risk, stated now.** A field that stays sparse can be heard as
grain or rattle rather than as a room. The metric cannot tell these apart;
if RN1 passes it goes to listening before anything else.

**CPU estimate.** Cost is the number of taps: ∫ρ over the first pass. At
100 → 600 echoes per second over 0.5 s that is about 150 taps per channel,
roughly 1.3–2.0 × Hall. The limit is 1.5 ×; the density ceiling may have to
be set by cost. Memory: four lines of up to 0.6 × the longest RT60 —
megabytes where Hall has 498 KiB. A cap on line length (2 s) brings the
first return back above −36 dB for Decay beyond about 3.3 s; reported if
used.

**Nearest relatives, and what is new.** Stage 3's multi-tap readout and
feed-forward pattern (reproduced) tapped Hall-length loops at a fixed
density. K2 put recursive diffusion in one loop of Hall-scale lines. New
here: no loop shorter than 0.6 × RT60, and a density law timed from Decay.

**Visibility control.** Hall's lines × 8 with a *fixed* sparse tap set (no
timed law). If Hall reproduces that, the long-loop engine alone is not the
identity and RN1 rests on the timed law only; this is reported either way.

**Predicted failure.** Large / long. With 5 s of decay Hall is smooth and
free of audible repetition for most of the response, and RN1's taps are
dense for most of it too. I expect the lowest scores there — the reverse of
every earlier candidate.

### RN2 — scattering that depends on frequency

**Interpretation.** A surface scatters diffusely when the wavelength is
small against its irregularities and reflects specularly when it is not. In
a compact room this boundary falls in the middle of the audio band: highs
are diffuse from the first reflection, lows stay as distinct reflections.
The onset of a sound is heard as a few clean low-frequency arrivals under an
immediately dense top.

**State topology.** Hall's eight lines and delays. The constant matrix is
replaced by a *paraunitary* one — orthogonal at every frequency, different
at each. Built from four planar rotations between line pairs, each of the
form R(45°) · diag(1, A(z)) · R(−45°), A(z) an allpass whose phase turns
from 0 to π through a crossover: at low frequency the pair passes straight
through, at high frequency it mixes fully. A second layer pairs the lines
differently. The low-frequency mixing is not zero but timed: sin²θ = loop
time / (k · Decay), Stage 5's rule, below the crossover only. Lossless at
every frequency, so no tone.

**Why it survives short Decay.** The difference is made in the first two or
three passes, 10–60 ms, and the low-frequency coupling is tied to Decay.

**Why Hall cannot reproduce it.** Property 3. Hall has one matrix and a
broadband diffuser. It can be sparse or dense, not sparse below a frequency
and dense above. Expected on *band arrival* (time to 5 % at 250 Hz against
4 and 8 kHz) and *late texture*; possibly *echo density*.

**Expected Size behaviour.** Hall's law, by construction (the delays are
Hall's).

**Expected modal behaviour — the main risk.** Below the crossover the lines
are weakly coupled loops. Their modes are as many as Hall's but they are
eight combs, and combs are periodic: this may read as low-frequency
repetition or ringing. If modal prominence is not clean the hypothesis is
rejected under section 2 whatever its score. I put the chance of this at
about even.

**Mono / stereo risks.** The readout is Hall's. Weakly mixed lows mean the
left and right low-frequency content comes from fewer lines each: channel
balance and mono tone at low frequency are at risk, as they were in R4c.

**Tone risk.** None from the matrix. The octave *shares* can still move if
low and high frequencies reach the readout on different schedules — the
effect that put Ambient 1.8 dB out.

**CPU estimate.** Eight first-order allpasses and eight rotations per sample
over Hall: 1.1–1.25 ×. Memory as Hall.

**Nearest relatives, and what is new.** It uses the timed rule, as R4c and
A1b do. New: the coupling is a function of frequency; there is no block,
and nothing is heard separately. PT1 changed the *delay* with frequency
inside the loop and was reproduced; this changes the *mixing*.

**Visibility control.** The same matrix with the crossover moved out of
band both ways (all specular; all diffuse). Both must be reproduced by
Hall or by the R3 result, and the in-band version must not be. If the
metric scores the in-band version under 3.0 with Hall at the *same knobs*,
it does not see band-dependent density and RN2 is not testable.

**Predicted failure.** Medium and large: once the lows have mixed, nothing
is left but Hall.

### RN3 — the two channels differ by time, not by sign

**Interpretation.** Two microphones a hand's width to a metre apart in one
room receive the same reflections at slightly different times. Low
frequencies arrive in phase at both; high frequencies are decorrelated. The
image is solid at the bottom and wide at the top, and it is so from the
first reflection to the last. Hall's channels are decorrelated at every
frequency equally, which is how a very large space sounds. That the
coherent-low image is a cue for a *compact* space is my reading, not an
established result, and is the weakest link here.

**State topology.** Hall's tank, unchanged. Only the readout changes: each
line is read at two points a few samples to a few tens of samples apart
(0.1–0.8 ms, a different offset per line, scaled by Size), one point to
each channel, with the same sign. No sign pattern decorrelates the
channels; time does.

**Why it survives short Decay.** It is a property of every echo. It has no
time scale of its own.

**Why Hall cannot reproduce it.** Property 4. Width scales the side signal
by one number at every frequency. It can make Hall as correlated *on
average* as RN3; it cannot make the correlation fall with frequency.
Expected on *stereo* and *partial placement*.

**Expected Size behaviour.** Hall's law for the field; the offsets scale
with Size.

**Expected modal behaviour.** Hall's exactly.

**Mono / stereo risks — high.** Summing two copies offset by Δt is a comb
with its first notch at 1 / (2Δt): 0.6–5 kHz. Eight lines with eight
different offsets average the notches, not away. Mono will be +3 dB at low
frequency against high. Stage 3's limits are a fold-down within 1.5 dB of
Hall's and a worst mono tone no more than 3 dB deeper. I expect this to be
the gate RN3 fails if it fails.

**Tone risk.** None in stereo.

**CPU estimate.** Eight extra reads: 1.0–1.05 × Hall.

**Nearest relatives, and what is new.** Room B placed a *panned* source's
reverb on its own side and was nothing on a centred one. RN3 is a property
of a centred source's reverb. No earlier candidate changed how the channels
are related.

**Visibility control — expected to decide it.** The control *is* the
candidate. Before G1: RN3 against Hall at the same knobs with Width swept.
If some Width brings the worst family under 1.5, the frozen metric reads
RN3 as a narrower Hall — it measures how correlated the channels are, not
at which frequencies — and RN3 is **not testable** with it. I think this is
more likely than not. The property would then stand or fall only by ear.

**Predicted failure.** Reproduced by Width at every point, uniformly.

## 4. Order of work, when and if this is approved

1. The three visibility controls. No candidate is built until its control
   has been read.
2. For each hypothesis still testable: modal prominence, then G1 at small /
   short, then the other points, then G2, then G3.
3. Room against Ambient and Plate for any survivor.

One build of each, at the parameters to be fixed in an addendum before the
build (k, ρ₀, the crossover, the offsets). A hypothesis that fails is not
tuned; a second attempt needs a second written reason, as in Stages 5 and 6.

## 5. Considered and not registered

| idea | why not |
|---|---|
| modal density rising with frequency, as in a real room (∝ f²) | needs dispersion in the loop (PT1, reproduced) or a warped network (every delay an allpass: far beyond any CPU limit); and sparse low modes are ringing |
| arrival times on an image-source lattice (√ of sums of squares) | a feed-forward early pattern handing over to a tank: the reflection-into-tank family, three times reproduced |
| first reflections losing more than later ones | a double slope: C2, C3 |
| an early field that is coherent between channels and a late one that is not | a time-varying RN3; the metric's stereo family is not time-resolved, and Early/Late may supply it |
| a recirculating fixed reflection pattern | periodic at 30–80 ms: flutter, a repetition defect |

## 6. What would count as the reset having failed

If none of RN1–RN3 is testable and clean and through G2, the statement is:
on a centred source, without ringing or tone, no Room structure outside
Hall's reach has been found by construction in Stages 3, 5, 6 or 7, and the
remaining question is whether the frozen metric's idea of "outside Hall's
reach" is the right definition of a Mode at short Decay. That is a decision
about the standard and is not mine to take.
