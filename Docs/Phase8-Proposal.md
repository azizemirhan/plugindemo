# Phase 8 — Pre-Delay, Low Cut, High Cut: Stage 1 architecture proposal

**Stage 1 only. No production code has been changed.**

**The headline finding: all three parameters already exist.** `preDelay`,
`lowCut` and `highCut` are declared in `ParameterIDs.h`, fully specified in
`ParameterLayout.cpp` with ranges, skews, defaults and formatters, attached to
`RawHandles`, given smoothers of the right kind in `SmoothedParameters`,
prepared, targeted, snapped and advanced every block — and **never applied to
audio**. `getTailLengthSeconds()` already adds the pre-delay to the reported
tail.

Phase 8 is therefore not "add three parameters". It is **"consume three
parameters that are already wired to the edge of the audio path"**. That
changes the parameter-contract question from a design decision into a
compatibility constraint, and it removes most of the scaffolding work.

---

## 1. The current signal path

`AuroraVerbProcessor::processInternal`, the single templated implementation
behind both `processBlock` overloads. The block is split into chunks of
`maxChunk = wetBuffer.getNumSamples()` — `max(blockSize, 512)` — so **every
stateful stage must be chunk-safe**.

```
                      ┌─ dry (float/double, untouched, in `buffer`) ──────────┐
                      │                                                       │
input buffer ─────────┤                                                       │
                      │                                                       ▼
                      └─► 1. capture into wetBuffer (Real = double, stereo)
                                     │
                                     ▼
                          2. reverb engine
                             tank.setRt60Seconds / setInputDiffusion / setSize
                             tank.setDamping(low, high, lowHz, highHz, snap)   ← PHASE 7
                             architecture::applyWithDepth(tank, …, depth, rate)
                             tank.processBlock(wetL, wetR, count)
                             × previewWetTrim(rt60, sizeScale)                 ← decay-aware
                                     │
                                     ▼
                          3. stereo width, wet only (M/S, per-sample smoother)
                                     │
                                     ▼
                          4. dry/wet crossfade ──────────────────────────────► output
```

Supporting lifecycle:

| | |
|---|---|
| `prepareToPlay` | the **only** allocation point. Sizes `wetBuffer`, prepares smoothers and the tank, sets `needsSmootherReset` |
| `reset()` | clears `wetBuffer`, re-arms `needsSmootherReset`, `tank.reset()`. Has an explicit "the shipping reverb engine reset() goes here" marker |
| first block | `firstBlockAfterPrepare` → `smoothed.snapToTargets()`, and damping is snapped rather than glided |

### What is already in place for Phase 8

| | current state |
|---|---|
| `id::preDelay` | `skewed(0, 500 ms, centre 60), default 0.0, step 0.1`, label "ms" |
| `id::lowCut` | `freqParam(20, 1000 Hz, centre 200), default 20.0` |
| `id::highCut` | `freqParam(500, 20000 Hz, centre 4000), default 20000.0` |
| `smoothed.preDelayMs` | `Glide` = `OnePoleSmoother`, τ = 10 ms |
| `smoothed.lowCut` / `.highCut` | `Ratio` = `LogSmoother`, τ = 30 ms, floors 10 Hz / 100 Hz |
| `advanceUnconsumed()` | **skips all three**, precisely because nothing consumes them |
| tests | **zero** references to any of the three |

---

## 2. Reusable DSP that already exists

Nothing new needs inventing.

**`DSP/DelayLine.h`** — fixed-capacity fractional delay, power-of-two ring,
allocation only in `prepare()`, separate `write()` / `read()`. Two details
decide the pre-delay design:

- `readAt(0)` returns **the sample just written** — genuine zero delay, exactly.
- interpolated `read(double)` is **clamped to ≥ 1 sample**, because a 4-point
  kernel needs a newer neighbour. *A glide toward 0 cannot reach 0 through the
  interpolated path.* This is the single most likely source of a silent
  Phase-8 bug and is called out again in §7.

**`DSP/DampingTuning.h`** — `onePoleCoefficient(f, fs) = exp(-2πf/fs)`, chosen
over a bilinear biquad for reasons that apply verbatim here: `a ∈ (0,1)` for
every `f > 0` at every sample rate, no pre-warping blow-up near Nyquist, and
**a one-pole's coefficient can change discontinuously without ringing because
it has no resonant memory** — click-free cutoff automation by construction
rather than by smoothing hard enough. The corner warps near Nyquist; that is
documented and deliberately uncorrected.

**`DSP/LineDamping.h`** — establishes the house transparency pattern:

> With both shelf gains at 1 the filter is bypassed **on a flag** rather than
> computed with coefficients that happen to be zero, so Damp 0 % is a
> **bit-exact identity** and costs one predictable branch.

Phase 8's zero/bypass requirement should use exactly this pattern.

**`Tests/DspAnalysis.h`** — FFT, band deviation, third-octave bands, impulse
response, correlation, finiteness and subnormal checks. Everything the Phase-8
tests need already exists.

---

## 3. Topology comparison

### A) Input → Low Cut / High Cut → Pre-Delay → Core — **RECOMMENDED**

### B) Input → Pre-Delay → Low Cut / High Cut → Core

**A and B are very nearly the same choice.** A delay and an LTI filter
commute, so for any settled setting their outputs are identical to within
arithmetic. They differ only while the delay time is moving, and only in which
signal the filter state sees. A is preferred on two small, real grounds: the
delay line never stores sub-audio energy that is about to be discarded, and
"input conditioning" reads correctly as the first thing in the chain. There is
no sonic argument for B.

### C) Input → Pre-Delay → Core → Wet Low Cut / High Cut

Materially different, and the only serious alternative.

| | pre-core (A) | post-core (C) |
|---|---|---|
| what it changes | the **excitation** reaching the tank | the **output** of the tank |
| RT60(f) | unchanged | unchanged |
| band energy in the tail | reduced, because it was never injected | reduced, because it is removed on the way out |
| cutoff automation on a ringing tail | affects newly injected signal only; the tail darkens over ≈ one RT60 | the existing tail darkens immediately |
| internal tank level with bass-heavy input | **reduced** — matters, because the output trim is decay-aware | unchanged; low energy circulates and is removed at the end |
| interaction with the Width M/S stage | none, Width is downstream of both | one more ordering decision — before or after Width |

C's one genuine advantage is immediate automation response. A's advantages are
that it does the job the product brief actually describes — the brief's own
words for Low Cut are *"prevent unnecessary sub / bass energy from **feeding**
… the reverb"*, which is an input function — and that it keeps low energy out
of the feedback loop rather than circulating and then discarding it.

A's automation lag is not a defect: a cutoff change takes up to one RT60 to be
fully heard in the tail, which is how a room behaves when the source changes
tone. It should be **measured and documented**, not hidden, and it is
irrelevant in the overwhelmingly common static-setting case.

### D) Split — distinct input conditioning and wet tonal filtering

**Rejected for Phase 8.** This is two filter pairs and two more parameters to
answer for, bought entirely with "we might want it later". The brief says not
to add architecture for theoretical flexibility. If a future phase wants to
shape the tail independently of the excitation, that is a *different control*
with its own justification, and A does not block it — see §8.

### Recommendation

```
input ─► [Low Cut] ─► [High Cut] ─► [Pre-Delay] ─► tank ─► trim ─► width ─► mix
         └──────── wet path only; dry is untouched ────────┘
```

All three stages live inside `processInternal` between the existing **step 1
(capture)** and **step 2 (reverb engine)**, operating on `wetBuffer`. The dry
signal is never touched, so "no change to dry timing" holds structurally
rather than by care.

Filters before the delay so that the delay line stores only signal that will
be used.

---

## 4. The damping distinction, and how to prove it

This is the critical risk the brief names, and it has a clean test.

| | mechanism | measurable signature |
|---|---|---|
| **High Damp** | per-line shelf **inside the feedback loop** → frequency-dependent **decay rate** | `RT60(f)` changes |
| **High Cut** | one-pole on the **excitation**, outside the loop → frequency-dependent **injected amplitude** | `RT60(f)` **unchanged**; band energy changes |

> **WITHDRAWN IN STAGE 2.5 — see `Phase8-Stage2.5-Signoff.md`.** The
> pre-registered test below was built on two things that did not survive
> measurement. The `≤ 1 %` RT60 target is not achievable through this harness
> for reasons that have nothing to do with the tank: a one-octave analysis
> band has finite rejection, so a broadband excitation leaks fast-decaying
> high-frequency energy into every band, and removing the highs removes the
> leak. A post-tank control — the same filter applied where it is
> mathematically incapable of changing a decay — produced the same measured
> change, which settles whose it is. And the harness fails its own sensitivity
> control against Phase 7's analytic table, so its silence would prove nothing
> anyway. **No quantitative RT60 gate was created from it.**
>
> What replaced it is a *routing* guarantee, proved on the complete production
> tank at the shipped Mod Depth: every readable tank control value is
> identical across a full cut sweep, and the signal reaching `processBlock`
> replays bit-identically into a tank that was never told a cut parameter.
> The claim is that a cut cannot change the **decay law**, not that it cannot
> change a **measured** RT60 — the latter is too strong for a finite,
> modulated, modal system.

**Pre-registered separation test.** Measure `RT60(f)` with the certified
fixed-Q ensemble analyser at a fixed damping setting, sweeping High Cut across
its range. The decay time in every band must be invariant while the band's
total energy follows the filter's magnitude response.

- RT60(f) invariance target: **≤ 1 %** across the High Cut sweep, which is
  within the analyser's own repeatability.
- Band energy must track the one-pole magnitude to within **≤ 0.5 dB**.

Same test for Low Cut against Low Damp. If RT60(f) moves with a cut control,
the filter has leaked into the loop and the architecture is wrong — that is
the failure this test exists to catch, and it is a measurement, not an
argument.

Perceptually the two stay distinct and non-redundant: **High Cut is uniformly
darker; High Damp is progressively darker.** A listener hears the difference
as "dark reverb" versus "bright onset that darkens".

---

## 5. Parameter contract — already committed, recommend no change

These are not new parameters. `ParameterIDs.h` states the rule: *"Once a
plugin has shipped they must never be renamed or reordered — only appended
to."* `versionHint` is 1 and all three are in it.

| | id | range | skew centre | default | units |
|---|---|---|---|---|---|
| Pre-Delay | `preDelay` | 0 – 500 ms | 60 ms | **0 ms** | ms, 0.1 step |
| Low Cut | `lowCut` | 20 – 1000 Hz | 200 Hz | **20 Hz** | Hz, 0.1 step |
| High Cut | `highCut` | 500 – 20000 Hz | 4000 Hz | **20 kHz** | Hz, 0.1 step |

**Recommendation: keep all three exactly as committed**, against the brief's
candidate ranges. Reasons:

1. **APVTS stores normalised values.** Narrowing Pre-Delay from 500 to 250 ms
   does not clamp saved sessions, it **silently remaps** them — a session
   saved at normalised 0.5 would reopen at a different millisecond value. Same
   for raising the High Cut floor from 500 Hz to 1 kHz. A range is nearly as
   public as an ID.
2. **0–500 ms is usable because of the skew.** A linear 0–500 ms control would
   be unusable; with the centre at 60 ms the musically busy region occupies
   the first half of the travel and the long end costs nothing. 250 ms would
   remove the long slap and ambient pre-delays for no gain.
3. **500 Hz is a defensible High Cut floor** for dark plate and vintage
   voicings, and a floor is only reachable deliberately.

The three defaults are already the transparent ones the brief asks for.

### One thing worth flagging, not fixing

`lowCut` has the **identical** range and skew centre to `lowDampFreq`
(20–1000, centre 200), and `highCut` identical to `highDampFreq`
(500–20000, centre 4000). The DSP distinction is real and provable (§4), but
on a control surface four frequency knobs with two shared ranges invite
exactly the redundancy confusion the brief warns about. **This is a labelling
and layout concern for the GUI phase, not a reason to change a range now.**

---

## 6. Filter topology: first-order one-pole, both

Matching `DampingTuning.h` rather than introducing a second filter philosophy.

| | one-pole (proposed) | biquad Butterworth |
|---|---|---|
| slope | 6 dB/oct | 12 dB/oct |
| resonance | none, structurally | Q = 0.707 overshoot |
| coefficient-change behaviour | **no transient — no resonant memory** | can ring on a coefficient step |
| near Nyquist | `a ∈ (0,1)` always; corner warps, documented | needs pre-warping; blows up near Nyquist |
| cost | 1 multiply-add per sample per channel | ~5 |
| automation safety | safe by construction | needs coefficient smoothing |

**Is 6 dB/oct enough for a Low Cut?** Measured against the first-order
response `|H| = f / √(f² + fc²)`:

| Low Cut | attenuation at 60 Hz | at 100 Hz |
|---|---|---|
| 200 Hz | **−10.8 dB** | −7.0 dB |
| 400 Hz | −16.6 dB | −12.3 dB |

−10.8 dB of 60 Hz at a 200 Hz setting is a working low cut for a reverb send.
First order is sufficient and no escalation is proposed.

**Pre-registered escalation criterion, so this is a decision and not an
assumption:** if measurement shows less than 10 dB of attenuation one and a
half octaves below the corner, a second cascaded one-pole (12 dB/oct, Q = 0.5,
still no resonance, still unconditionally stable) is added to Low Cut only.
The arithmetic above says this will not trigger; the criterion exists so the
outcome is recorded either way.

High Cut stays first order regardless — a gentle HF rolloff *is* the "silky"
target, and a steeper one sounds abrupt.

---

## 7. Smoothing and transparency

Three parameters, three different policies. The existing smoothers are right
for two of them and wrong for one.

### Low Cut / High Cut — keep `LogSmoother`, control rate

Already correct and already configured: τ = 30 ms, log-domain so equal time
means equal ratio. `LineDamping` establishes that a one-pole corner needs no
audio-rate ramp, because a coefficient step produces no transient. Update the
coefficient once per chunk from `advance(count)`.

### Pre-Delay — **the existing `Glide` smoother is the wrong tool**

`OnePoleSmoother` with τ = 10 ms applied to a delay *time* produces Doppler.
The derivative of delay time **is** the pitch shift: a 0 → 100 ms move with
τ = 10 ms starts at a rate of change near 10, which is not a glide but a
destroyed signal. The brief anticipates this exactly.

**Proposed: dual-tap crossfade on one delay line.**

- One write pointer, two read taps: the current delay and the pending one.
- A parameter change starts a **linear crossfade over 20 ms** from the old tap
  to the new, then the old tap is retired.
- A change arriving mid-fade retargets from the current blend.
- No pitch shift at all. The only artifact is a brief overlap of two delay
  times during the fade, which is what every professional stepped delay
  control does, and it is 20 ms long.
- Cost: one extra `readAt` and a linear blend, during the fade only.

Consequence to handle deliberately: **`preDelayMs` leaves the per-sample path**
and the raw target drives the crossfade state machine instead. It must then be
removed from `advanceUnconsumed()` — see §9.

### Zero / bypass transparency — bit-identical, by flag

Following the `LineDamping` precedent, each stage is bypassed **on a flag** at
its neutral value rather than computed with neutral coefficients:

| control | neutral | behaviour |
|---|---|---|
| Pre-Delay | `0.0 ms`, settled | stage skipped entirely — no write, no read |
| Low Cut | `20.0 Hz` | filter skipped |
| High Cut | `20000.0 Hz` | filter skipped |

At the Phase-7 default settings this makes the Phase-8 build **bit-identical**
to Phase 7, which is the strict reference case the brief asks for, and it costs
three predictable branches per chunk.

Two honest consequences to measure rather than assume:

1. **A step at each boundary.** Leaving bypass at Low Cut 20.1 Hz engages a
   one-pole whose attenuation is 3 dB at 20 Hz and under 1 dB above 50 Hz. The
   step must be measured and reported; it is expected to be inaudible, but
   that is a prediction until it is a number.
2. **Pre-Delay 0 cannot be reached by the interpolated path** — `read(double)`
   clamps at 1 sample. The crossfade must target the integer `readAt(0)` /
   bypass path when the new value is 0, not glide into a 1-sample floor. This
   is the likeliest silent bug in the whole phase.

---

## 8. Future-phase compatibility

Architecture A does not block the roadmap, and in one respect protects it.

```
                       ┌──────────────► (future Early Reflections) ──┐
input ─► [LowCut/HighCut] ─► [Pre-Delay] ─► late tank ───────────────┴─► merge ─► width ─► mix
         └ conditioning ┘    └ late only ┘
```

Putting the filters **before** the future Early/Late split means one filter
pair conditions both paths — the right place for input conditioning. Putting
Pre-Delay **after** that split point keeps it on the late path only, which is
what a pre-delay classically is: early reflections should start early. Had
Pre-Delay gone before the split, adding Early Reflections later would have
required moving it, and moving it would change the sound of existing sessions.

Unaffected by this proposal: Late Diffusion, Density, Attack, Bloom,
Early/Late balance, stereo controls, Algorithm/Mode selection, Colour
families. None is implemented now.

---

## 9. Architectural risks

Ordered by how quietly each one would fail.

| # | risk | response |
|---|---|---|
| 1 | **`advanceUnconsumed()` double-advance.** It currently skips `preDelayMs`, `lowCut`, `highCut` because nothing consumes them. The moment the audio path consumes them they must be removed from that list, or every smoother runs at twice the rate — a wrong but *plausible-looking* automation speed, with nothing failing | explicit test: a smoother's value after one block must equal its value after the same span consumed sample-by-sample |
| 2 | **Fractional read cannot reach 0** (§7) | integer / bypass path at 0 ms, with a test asserting bit-exact identity at 0 |
| 3 | **Chunking.** `processInternal` splits blocks at `max(blockSize, 512)`. New state must persist across chunks and must not re-snap per chunk | block-size identity test: one 4096-sample block must equal eight 512-sample blocks, bit for bit |
| 4 | **Range change would silently remap saved sessions** (§5) | recommend no range change; if one is ever wanted it needs a `versionHint` bump and a migration |
| 5 | **Pre-delay buffer size.** 500 ms at 192 kHz = 96 000 samples → 131 072 after the power-of-two round, ×2 channels ×8 bytes ≈ **2 MB**, allocated in `prepareToPlay` only | sized from `maxDelaySeconds` and the actual rate; never resized in `process` |
| 6 | **Stereo divergence.** Two independent delay lines with independent smoothers would drift and smear the image | one shared time and one shared crossfade state driving both channels; test asserts sample-aligned taps |
| 7 | **`reset()` must clear the new state** — a stale pre-delay buffer would replay old audio after transport stop | `reset()` clears the delay lines and filter memory; test asserts silence-in → silence-out immediately after reset |
| 8 | High Cut at 20 kHz with a 44.1 kHz rate sits near Nyquist, where the one-pole corner warps | documented, inherited from `DampingTuning.h`; the default is bypassed so it does not arise at the default |

---

## 10. Proposed test plan

New suite `Tests/Phase8Filters.cpp` (+ `runPhase8Tests` in `Suites.h`,
`RunAll.cpp`, `Tests/CMakeLists.txt`), reusing `DspAnalysis.h` and the
certified ensemble analyser.

**Pre-Delay**
- exact delay timing at 44.1 / 48 / 88.2 / 96 / 192 kHz, measured as impulse
  arrival in samples against `round(ms × fs / 1000)`
- `0 ms` **bit-exact** identity against the Phase-7 path
- maximum value, and the buffer is large enough for it at every rate
- stereo alignment — L and R taps sample-identical
- block-size identity: 4096 in one block == eight 512-sample blocks, bit for bit
- automation transition: no click, no discontinuity above a stated threshold,
  **no pitch glide** — measured as instantaneous frequency of a sine through
  the transition
- `reset()` leaves no stale audio; `prepareToPlay` at a new rate re-sizes
- no NaN / Inf / subnormals

**Low Cut**
- magnitude response against the analytic one-pole, several corners
- `20 Hz` bit-exact bypass
- attenuation at 60 Hz and 100 Hz against §6's table, and against the
  pre-registered 10 dB escalation criterion
- sample-rate consistency: the same corner gives the same response at every rate
- rapid automation sweep: no instability, no NaN/Inf
- the bypass-boundary step, measured and reported

**High Cut**
- magnitude response against the analytic one-pole
- `20 kHz` bit-exact bypass
- Nyquist-safe at 44.1 kHz: coefficient in range, response monotone, no blow-up
- sample-rate consistency, automation safety, no NaN/Inf
- the bypass-boundary step, measured and reported

**System**
- **Phase-7 zero-setting regression: bit-identical** output at Pre-Delay 0 /
  Low Cut 20 / High Cut 20 k, against the Phase-7 path — the strict reference case
- ~~**the damping separation test of §4** — RT60(f) invariant under both cut
  controls while band energy follows the filter~~ — **withdrawn in Stage 2.5**;
  replaced by the structural routing verification, see §4's banner
- interaction matrix: each cut control against Low Damp, Low Damp Freq, High
  Damp, High Damp Freq, Decay, Size, Mod Depth
- Decay still hits its target with the cuts engaged
- stereo image and mono downmix unchanged by the cuts
- deterministic block-size behaviour for the whole chain
- `reset()` / state restore / parameter serialisation round-trip
- CPU against the Phase-7 baseline

### CPU expectation

Phase-7 baseline from the current suite: whole tank **≈ 57 ns/sample**, 8-stage
stereo path **93.18 ns/sample** (0.895 % of one core). Phase 8 adds two
one-poles and one delay tap per channel — on the order of **1–2 ns/sample**,
so **under 3 %**. Anything materially above that is a defect to investigate,
not a cost to accept. Baseline, result and ratio to be recorded.

---

## 11. Listening validation

Deliberately small, per the brief. Build it **after** the measurements, and
only expand it if a measurement reveals something.

The existing `Tests/DampingListening.cpp` harness already renders blind,
level-matched, permuted A/B sets with a manifest and a key, and already
carries the six sources — `mixed`, `pad`, `snare`, `piano`, `vocal`, `hats`.
It is the natural place to add Phase-8 cases.

One lesson from Phase 7 to apply from the start: **report per-pair measured
difference before asking anyone to listen.** Nine of twelve pairs in the
Phase-7 set were near-identical, and saying so up front is what stopped the
listener hunting for a difference that was not physically there.

Cases: Pre-Delay 0 / moderate / long · Low Cut off vs a practical vocal
setting · High Cut off vs darker · High Cut with High Damp · Low Cut with Low
Damp · at Mix 30 % and at 100 % wet.

---

## 12. Scope boundaries

Not touched by Phase 8: Phase-7 damping defaults, Option A normalisation,
modal analysis, the modulation architecture, existing parameter IDs, the GUI,
Phase 9 work, `AV_COPY_PLUGIN_AFTER_BUILD`.

Known debt left alone, as instructed: the centralised shipping-default test
contract, the stale duplicated defaults, and the README's reference to the
removed `AV_DEV_DIFFUSION_PREVIEW`. None of the three blocks Phase 8.

---

## Stage 2, pending review

Tests and harness scaffolding first, then minimal DSP, focused regression,
measurement, listening, production integration, documentation, closure.

**Decisions wanted before Stage 2 begins:**

1. Topology **A** (filters → pre-delay → core) — confirm, or choose C.
2. Keep the three committed ranges and defaults — confirm, or accept the
   session-remapping cost of changing them.
3. Dual-tap **crossfade** for pre-delay rather than the existing one-pole
   glide, and the consequent removal of `preDelayMs` from the per-sample path.
4. **First-order** one-pole for both cuts, with the 10 dB escalation criterion
   pre-registered.
5. Bit-exact **flag bypass** at the three neutral values, with the boundary
   step measured and reported.
