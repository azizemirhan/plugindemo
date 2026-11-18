# Phase 12 Stage 7b — RN1, RN2, RN3: the registered definitions, restated for review

This is a reading aid. **It changes nothing.** The pre-registration is
`Docs/Phase12-Stage7-Room-Preregistration.md` as committed in `9ad81db`,
and it is frozen. Where this page and that document differ in any word, that
document governs.

Nothing has been coded. Nothing will be until individual hypotheses are
approved.

Each hypothesis is set out the same way: what is fixed by the registration,
what the registration leaves to an addendum, what ends it, and the places
where my own text is loose enough that a reviewer should press on it. The
loose places are listed, not repaired: repairing them would be amending a
frozen document.

## 0. What applies to all three

**Gates** (registration, section 2):

- G1, G2, G3 as before. Centred source. Three operating points. Hall's
  search free on all seven controls, Early/Late included. SURVIVES needs no
  run ≤ 1.5 and every median ≥ 3.0.
- Added for Room:
  - modal prominence ≤ Hall + 3 dB (16.8 dB) at all three points **before**
    any G1 or G2 result counts; otherwise "distinct by ringing", rejected;
  - a search that stops on "modal structure" is reported as resting on
    modes;
  - G1 at small / short first; a lowest run ≤ 1.5 there ends the hypothesis;
  - a visibility control before each candidate. If the frozen metric does
    not score the control ≥ 3.0 against Hall's search, the hypothesis is
    *not testable* — not passed, not failed — and is not built further. The
    metric is not extended.
- Size, Decay ± 10 %, stereo and mono, octave bands ≤ 1 dB, CPU ≤ 1.5 ×
  Hall: as Stage 6.
- A survivor is then run against Ambient A1b and Plate PV3, both ways.

**Order** (section 4): the three visibility controls; then, per testable
hypothesis, modal prominence, G1 at small / short, the other points, G2,
G3. One build each. A failed hypothesis is not tuned; a second attempt
needs a second written reason.

**Not yet fixed for any of them:** the numeric parameters. The registration
says they are "to be fixed in an addendum before the build (k, ρ₀, the
crossover, the offsets)". That addendum does not exist.

## 1. RN1 — unfinished mixing without loops

**Claim.** Reflections stay countable while the room decays. Their number
per second grows on a time scale that is a fixed fraction of Decay, so the
texture is still resolving at −20 dB. Nothing returns inside the audible
decay. Breaks Hall's properties 1 and 2 (mixing time set by Size; density
and periodicity one number).

**Fixed by the registration.**

| element | as registered |
|---|---|
| lines | four delay lines, each at least 0.6 × RT60 long, so the first recirculation is ≥ 36 dB down |
| mixing | orthogonal, between the four lines, only to carry the response past the first pass |
| the response inside the first pass | feed-forward: sparse signed taps read along each line |
| tap spacing | drawn at random; no periodic spacing |
| tap density along the line | ρ(t) = ρ₀ · exp(t / (k · Decay)) |
| tap weights | follow the decay |
| channels | independent tap sets for left and right |
| excluded | any allpass; any short loop |
| Size | sets the time of the first reflections and the initial spacing 1 / ρ₀; the line length does not follow Size |

**Left to the addendum.** k; ρ₀ and how it maps from Size; the density
ceiling; the tap-sign rule; the random seed policy; whether the 2 s cap on
line length is used.

**Predictions on record.** Modal prominence at or below Hall's 13.8 dB. t10
ratio across Size 1.3–1.8. Fold-down near −3 dB. Distinct from Hall on late
texture; Hall's attempts to follow show up on buildup, energy profile,
recurrence and loop repetition. CPU 1.3–2.0 × Hall. Lowest scores at
large / long.

**Visibility control.** Hall's lines × 8 with a fixed sparse tap set and no
timed law.

**What ends it.** Modal prominence above 16.8 dB at any point. A lowest run
≤ 1.5 at small / short. Octave bands outside 1 dB. CPU above 1.5 × Hall.
If it passes everything, it goes to listening before anything else, because
the metric cannot tell a room from grain.

**Places to press.**

1. **The control's rule is not the general rule.** Section 2 says a control
   scoring under 3.0 makes the hypothesis not testable. RN1's own paragraph
   says that if Hall reproduces the control, "RN1 rests on the timed law
   only; this is reported either way". Those are two different consequences
   and the text does not say which applies.
2. **The control tests the engine, not the property.** A fixed sparse tap
   set on long lines shows whether loop-free sparseness is visible. It does
   not show whether the *timed* density law is, and the timed law is the
   claim.
3. **CPU.** The estimate's upper half is over the 1.5 × limit, and the text
   says the density ceiling "may have to be set by cost". A ceiling set by
   cost is a constraint on the property being tested.
4. **Memory.** "Megabytes where Hall has 498 KiB" is stated and no memory
   limit is registered for Room.
5. **The cap.** With a 2 s cap the first return rises above −36 dB for
   Decay beyond about 3.3 s. At the large / long point (5.0 s) the "nothing
   returns" premise is then already compromised — at the point where
   failure is predicted anyway.
6. **Size may be pre-delay.** Registered as "the weakest part". Stage 6's
   definition of a meaningful Size is the test; nothing stricter was added.
7. **"100 → 600 echoes per second"** appears only inside the cost estimate.
   It is an illustration, not a registered value.

## 2. RN2 — scattering that depends on frequency

**Claim.** In a compact room the boundary between specular and diffuse
reflection falls inside the audio band: highs are diffuse from the first
reflection, lows remain distinct arrivals. Breaks Hall's property 3 (mixing
the same at every frequency).

**Fixed by the registration.**

| element | as registered |
|---|---|
| lines and delays | Hall's eight, unchanged |
| matrix | paraunitary: orthogonal at every frequency, different at each |
| construction | four planar rotations between line pairs, each R(45°) · diag(1, A(z)) · R(−45°); A(z) an allpass whose phase turns from 0 to π through a crossover |
| behaviour | below the crossover a pair passes straight through; above it, it mixes fully |
| second layer | pairs the lines differently |
| low-frequency mixing | not zero: sin²θ = loop time / (k · Decay), below the crossover only |
| loss | none at any frequency |
| readout | Hall's |

**Left to the addendum.** The crossover frequency and whether it moves with
Size; the order of A(z) (the cost estimate assumes first order); k; which
lines are paired in each layer.

**Predictions on record.** Distinct on band arrival (time to 5 % at 250 Hz
against 4 and 8 kHz) and late texture, possibly echo density. Size follows
Hall's law by construction. CPU 1.1–1.25 × Hall, memory as Hall. Fails at
medium and large, once the lows have mixed. **About even odds that modal
prominence is not clean.**

**Visibility control.** The same matrix with the crossover moved out of
band both ways (all specular; all diffuse), and the in-band version against
Hall at the same knobs.

**What ends it.** Modal prominence above 16.8 dB at any point — the named
main risk. A lowest run ≤ 1.5 at small / short. Octave bands outside 1 dB
(named risk: lows and highs reaching the readout on different schedules,
the effect that put Ambient 1.8 dB out). Low-frequency mono balance outside
Stage 3's limits.

**Places to press.**

1. **The control's pass condition is ambiguous.** "Both must be reproduced
   by Hall or by the R3 result" does not say what follows if one of the
   out-of-band versions is *not* reproduced, nor what "by the R3 result"
   means operationally.
2. **Two controls, two rules.** The "not testable" condition is stated on
   the in-band version scoring under 3.0 against Hall at the *same knobs*.
   Section 2 states it on the control against Hall's *search*. Same-knob
   and free-search scores are very different numbers.
3. **How the timed coupling enters the matrix is not written out.** The
   construction gives each pair a pass-through below the crossover; the
   timed rule gives a small rotation there. Where that rotation sits in
   R · diag(1, A) · R⁻¹ is not specified.
4. **Sparse lows may be the excluded thing by another route.** Eight weakly
   coupled combs at low frequency are close kin to "un-mixed short loops
   (wall pairs)", listed as closed. The registration says the difference is
   that the coupling is frequency-dependent and timed. A reviewer may judge
   that insufficiently new.
5. **Even odds on ringing** is my estimate with nothing behind it but the
   R4c experience.

## 3. RN3 — the channels differ by time, not by sign

**Claim.** Two pickups a short distance apart receive the same reflections
at slightly different times: lows in phase, highs decorrelated, on every
echo from first to last. Breaks Hall's property 4 (channels differ by sign
and mixture, equally at every frequency).

**Fixed by the registration.**

| element | as registered |
|---|---|
| tank | Hall's, unchanged |
| readout | each line read at two points, one to each channel, **same sign** |
| offset between the two points | 0.1–0.8 ms, a different offset per line, scaled by Size |
| decorrelation | by time only; no sign pattern |

**Left to the addendum.** The eight offsets; how they scale with Size; what
Width does in this Mode.

**Predictions on record.** Distinct on stereo and partial placement. Size
and modal behaviour exactly Hall's. No tone in stereo. CPU 1.0–1.05 × Hall.
Mono: a comb with first notch between 0.6 and 5 kHz, and about +3 dB at low
frequency against high; **expected to be the gate it fails if it fails.**
Predicted failure: reproduced by Width at every point.

**Visibility control.** The control is the candidate: RN3 against Hall at
the same knobs with Width swept. If any Width brings the worst family under
1.5, RN3 is *not testable* with the frozen metric. Registered expectation:
more likely than not.

**What ends it.** The Width sweep reproducing it (not testable). Mono
fold-down more than 1.5 dB from Hall's, or a worst mono tone more than 3 dB
deeper.

**Places to press.**

1. **It may not be a Room property.** The registration says that reading a
   coherent-low image as a cue for a *compact* space "is my reading, not an
   established result, and is the weakest link here". Stage 7's Chamber
   study then claims the same property for Chamber (CH-B). Only one Mode
   can own it, and nothing registered says which.
2. **Its control uses a third threshold.** Not-testable is triggered at
   1.5 here, where section 2 says a control must reach 3.0. A sweep result
   between 1.5 and 3.0 is not covered by either sentence.
3. **A not-testable outcome leaves it open, not closed.** The registration
   says the property "would then stand or fall only by ear". No listening
   procedure is registered for that case.
4. **Width.** If time, not sign, decorrelates the channels, the Width
   control as Hall implements it no longer does what it does elsewhere.
   The registration does not say what Width becomes.
5. **The mono prediction is close to a predicted failure.** Reviewing it
   means deciding whether to spend the visibility control on a hypothesis
   its author expects to fail twice.

## 4. Across the three

- **Shared dependency.** RN1 and RN2 both rest on Stage 5's timing rule
  (a time constant tied to Decay). If that rule's success in R4c came from
  the short loops and not from the timing, both fall together.
- **Shared engine with Chamber.** RN1's loop-free field is the engine
  Chamber direction CH-A needs with the opposite density law; RN3 is CH-B.
  Approving an RN hypothesis for Room has a consequence for Chamber.
- **The control rules are inconsistent between the general section and the
  three hypotheses** (items 1.1, 2.2, 3.2 above). This is a defect in my
  drafting. It should be settled by a written ruling before any control is
  run, and the ruling recorded as an addendum, not as an edit.
- **My own ranking, for what it is worth:** RN2 is the cheapest to test and
  the most likely to be killed quickly by ringing. RN1 is the one most
  faithful to the basis and the most expensive. RN3 I expect to be
  untestable. I would not call any of them likely to succeed.

## 5. What review is asked to decide

1. For each of RN1, RN2, RN3: approve as registered, reject, or approve
   with a ruling on the loose points above.
2. One rule for visibility controls: which score, against which search,
   means "not testable".
3. Whether a memory limit applies to Room.
4. Which Mode owns the pickup-coherence property if it proves visible.
