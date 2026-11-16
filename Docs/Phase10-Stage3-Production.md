# PHASE 10 - Stage 3, items 4-6: the production Early Reflection path

Status: **implementation complete and technically clean. Perceptual acceptance
NOT given here.** Envelope decay 1.5 remains a production listening candidate.

Scope actually built: the ER engine, the pattern data, the single public
`earlyLate` parameter, the processor integration, the production test suites and
the listening set. Nothing else. No Mode, no Character, no Presets, no GUI.

---

## 1. The component, and the split the brief asked for

Two new files, and the separation is the point.

| File | Holds |
|---|---|
| `Source/DSP/EarlyReflectionTuning.h` | the pattern **data**: event count, window, envelope decay, Size alpha, cell occupancy, the balance law and its calibration |
| `Source/DSP/EarlyReflections.h` | the **engine**: two ring buffers, a tap-list convolver, the Size crossfade - plus `EarlyLateBalance`, the two smoothed branch gains |

A future Mode supplies a different `er::Pattern` and the engine is untouched.
This is the same split `FdnTuning.h` and `DiffusionTuning.h` already use.

Neither file depends on JUCE or on the processor, so both are reachable from the
DSP test target, which is where the rule that `Source/DSP` stays JUCE-free is
enforced by compilation.

## 2. The signal path

```
input -> Low Cut -> High Cut -> Pre-Delay -+-> Attack -> tank -> trim -+-> balance -> Width -> Mix
                                           |                           |
                                           +-> early reflections ------+
```

Four properties follow from **where the split is**, not from care taken at the
call site:

- **Pre-Delay moves both branches by exactly the same amount**, because it is
  upstream of the split. An earlier comment in `PluginProcessor.cpp` predicted
  the split would go *before* the pre-delay; that reasoning was wrong and is
  left visible in the source rather than deleted. A pre-delay delays the
  *source*, and a reflection of a delayed source is delayed with it. Splitting
  earlier would have made Pre-Delay a hidden early/late timing control.
- **Attack can never reach the ER**, because the ER branch reads the signal
  before Attack touches it.
- **Early Diffusion can never reach the ER**, because input diffusion lives
  inside the tank, downstream of the split.
- **Late damping stays a late-feedback property.** The ER layer has no feedback
  and nothing applies the shelves to it.

The **trim is applied to the tank branch only**. It is a decay-aware
compensation for the recurrence; applying it to the ER would make the early
layer's loudness follow Decay, which is a late property the early layer does
not have.

## 3. The tap schedule as it ships

8 events per channel, 16 taps, 80.00 ms span at the default Size, at 48 kHz:

| event | L offset | L time | L weight | R offset | R time | R weight |
|---|---|---|---|---|---|---|
| 0 | 328 | 6.83 ms | -0.7301 | 108 | 2.25 ms | -0.8812 |
| 1 | 755 | 15.73 ms | 0.4469 | 647 | 13.48 ms | -0.3279 |
| 2 | 1346 | 28.04 ms | -0.3020 | 1346 | 28.04 ms | -0.2013 |
| 3 | 1616 | 33.67 ms | -0.2630 | 1753 | 36.52 ms | -0.1644 |
| 4 | 2278 | 47.46 ms | 0.1975 | 2311 | 48.15 ms | 0.1300 |
| 5 | 2527 | 52.65 ms | 0.1796 | 2562 | 53.38 ms | -0.1182 |
| 6 | 3128 | 65.17 ms | 0.1454 | 2975 | 61.98 ms | 0.1021 |
| 7 | 3725 | 77.60 ms | 0.1201 | 3570 | 74.38 ms | -0.0840 |

Each channel's weights are L2-normalised to **exactly 1**, so a Size change
moves *when* the energy lands and not *how much*. No tap sits at offset 0 - a
zero-offset tap would be the direct signal re-added at a different gain, which
is a level change wearing the name of a reflection.

## 4. A real defect found during this stage: the placement law

**The first implementation used PHASE 9's golden-ratio-inside-equal-cells
placement, and the schedule test rejected it.** The merged stereo arrival gaps
came out as

```
177 119 178 297 302 297 | 177 119 178 297 302 297 | 177 297 302
```

- a gap pattern repeating exactly every seven arrivals, from only five distinct
values. The cause is structural: `frac(phi*k)` advances by a constant mod 1, so
one event per equal cell can only ever produce **two** distinct gaps. Changing
phi for another irrational would not have helped.

That is a comb with a rhythm in it, which is the one thing an early-reflection
layer must not be, and it is exactly what the irregular placement was supposed
to prevent.

**Replaced with velvet noise:** one event per cell, placed inside the middle
70 % of its cell by a fixed-seed draw. Just as deterministic, genuinely
aperiodic. After the change: 15 distinct gap values out of 15, and **0 %
repetition at every period**. Per channel the cell guard holds the gap ratio to
2.66x and 2.78x.

The test was also rewritten, because the first version asked the wrong question.
It demanded zero *exactly equal* gaps, which integer sample offsets will violate
by coincidence; the criterion is now periodicity, which is the property that
matters.

**A note on PHASE 9, which is NOT changed.** `AttackInjection` uses the original
construction and has the same two-valued gap. It is left alone: it was selected
on mean arrival gap and third-octave ripple, it passed its listening validation,
and its taps are about 8 ms apart where this layer's are 3-12 ms. Changing it
would alter the shipped Attack sound for a property nobody reported hearing. The
measurement is recorded in `EarlyReflectionTuning.h` so the next person finds it
instead of repeating it.

## 5. Size

`span = 80 ms * sizeScaleRatio^0.25`, capped at 160 ms.

| Size | tank scale | ER span | first ER arrival | late first arrival |
|---|---|---|---|---|
| 0 % | 0.250 | 58.56 ms | 4.52 ms | 8.52 ms |
| 20 % | 0.379 | 64.98 ms | 5.02 ms | 11.73 ms |
| 50 % | 0.707 | 75.94 ms | 5.88 ms | 20.35 ms |
| 80 % | 1.320 | 88.77 ms | 6.85 ms | 36.17 ms |
| 100 % | 2.000 | 98.50 ms | 7.60 ms | 54.04 ms |

ER travel **1.68x** against the tank's **8.00x**: the two move in the same
direction without moving as one object. The span reaches past the late field's
own first arrival at every Size, and stays under the 120 ms where the layer
would stop being reflections and become a second late reverb - which is the
check that rejects alpha 0.5 (121.3 ms at Size 100 %).

**Taps never move while being read.** A Size change builds a second schedule and
crossfades over 30 ms, coalescing a pending target - the same pattern as PHASE 8
pre-delay and PHASE 9 Attack, for the same reason: the derivative of a delay time
is a pitch shift applied to whatever is passing through it.

Two spans that round to the same integer sample count produce an identical
schedule, so the comparison is made on that integer and a Size smoother creeping
by a fraction of a sample does not restart the fade. Measured: a full Size sweep
*rebuilt every sample* produces a steepest step of 2.326 against 2.684 for the
same input at a fixed Size - the sweep adds nothing.

## 6. The Early/Late control

One new public parameter, and only one.

| | |
|---|---|
| ID | `earlyLate` |
| Display | `Early / Late` |
| Range | 0-100 %, interval 0.1 |
| Default | **0** |
| Version hint | `versionHintV2` = 2 |

**BAL-B**, the energy-calibrated constant-power law:

```
theta = balance * pi/2    late = cos(theta)    early = sin(theta) * K    K = 0.7187
```

Both endpoints are written out exactly rather than trusted to the library.
`cos 0` is exactly 1 anyway; `cos(pi/2)` is **not** - pi/2 is not representable,
so the late branch at "early only" would sit 324 dB down rather than silent.
That is inaudible and still worth removing, because it is the difference between
a property that can be *measured* and one that can only be approximated.

Measured energy across the travel:

| Early/Late | late gain | early gain | wet energy | vs 0 % |
|---|---|---|---|---|
| 0 % | 1.0000 | 0.0000 | 62.6588 | 0.00 dB |
| 25 % | 0.9239 | 0.2750 | 62.3097 | -0.02 dB |
| 50 % | 0.7071 | 0.5082 | 62.1673 | -0.03 dB |
| 75 % | 0.3827 | 0.6640 | 62.3149 | -0.02 dB |
| 100 % | 0.0000 | 0.7187 | 62.6662 | 0.00 dB |

**0.03 dB across the whole travel at this configuration** - and the qualifier is
load-bearing. See the clarification below.

### What the traversal actually does, and what 6.14 dB is

"0.03 dB traversal" and "6.14 dB cost" read as contradictory on one page. They
are both true and they are not about the same thing, so stating them without
this paragraph invites exactly one misreading: that Early/Late changes the
output level by 6 dB.

**6.14 dB is a branch-gain calibration error**, the dB ratio between the shipped
K and the ideal K at the worst point of a Decay x Size grid. It is not a
total-output level change *at the reference*.

**But it becomes one away from the reference**, and that is the part worth being
precise about. The error sits on the early branch gain, and at Early/Late 100 %
the late branch is exactly zero, so the whole output is that branch. Total
integrated wet energy, same source, same 4 s window, normalised to Early/Late 0:

| Early/Late | ref: D 2.5 s, S 60 % | D 2.5 s, S 80 % | D 2.5 s, S 100 % | D 0.5 s, S 20 % |
|---|---|---|---|---|
| 0 % | 0.00 dB | 0.00 dB | 0.00 dB | 0.00 dB |
| 25 % | -0.02 dB | -0.21 dB | -0.34 dB | +1.66 dB |
| 50 % | -0.03 dB | -0.78 dB | -1.30 dB | +4.10 dB |
| 75 % | -0.02 dB | -1.43 dB | -2.52 dB | +5.65 dB |
| 100 % | 0.00 dB | -1.73 dB | -3.15 dB | **+6.14 dB** |
| **traversal** | **0.03 dB** | **1.73 dB** | **3.15 dB** | **6.14 dB** |

So the honest statement is **not** "the control holds level to 0.03 dB". It is:
the control holds level to 0.03 dB at the reference configuration, and drifts to
6.14 dB at the corners of the Decay and Size ranges, in the same direction and
by the same amount as the calibration error.

**This is a balance-design issue, not an implementation defect.** The code does
exactly what BAL-B specifies; BAL-B is a single-point calibration meeting
configurations it was not calibrated for. A test is written to fail on it and
stays failing until the design is revisited.

### K was re-measured, because Stage 2's figure does not transfer

Stage 2 reported 0.7811, taken against an **untrimmed** late branch. Production
trims the tank and not the ER, so the ratio the balance actually sees is a
different ratio. Re-measured against what ships: 62.659 / 121.322 gives
**K = 0.7187**, and the shipping constant is within 0.001 dB of the measured
ideal.

### The cost of a fixed constant, stated rather than waved at

The late branch's energy moves with Decay and Size and a fixed K cannot follow
it. Computing K from the running signal is forbidden - it is dynamic
normalisation, program-dependent semantics, an AGC in all but name.

| Decay | Size | ideal K | error of the fixed constant |
|---|---|---|---|
| 0.5 s | 20 % | 0.3543 | **+6.14 dB** |
| 0.5 s | 60 % | 0.4505 | +4.06 dB |
| 0.5 s | 100 % | 0.5210 | +2.79 dB |
| 2.5 s | 20 % | 0.5034 | +3.09 dB |
| 2.5 s | 60 % | 0.7187 | 0.00 dB |
| 2.5 s | 100 % | 1.0324 | -3.15 dB |
| 10.0 s | 20 % | 0.5423 | +2.45 dB |
| 10.0 s | 60 % | 0.7917 | -0.84 dB |
| 10.0 s | 100 % | 1.1937 | **-4.41 dB** |

Early/Late is a clean composition control at and near the defaults; at the
extremes of Decay and Size it also changes the level somewhat. **Reported
characteristic, not a defect awaiting a detector.**

**Balance smoothing is on the GAINS.** Two `LinearSmoother`s over 20 ms, matching
the other Level smoothers. Nothing in the balance path can move a reflection in
time, and a test confirms the tap offsets are bit-identical at balance 0, 13, 50,
87 and 100 %.

## 7. The handoff - CORRECTED, and worse than first reported

**The first version of this section quoted 6.02 dB. That number was wrong.** It
was measured before the placement law changed in section 4 and never re-read
afterwards, so a stale figure was carried into the report and into the listening
set's premise. The real figure is **16.47 dB**.

Full handoff grid, worst notch *with a recovery*, measured on the stereo
magnitude `sqrt(l^2 + r^2)`:

| Size | E/L 25 % | E/L 50 % | E/L 75 % |
|---|---|---|---|
| 0 % | 0.83 dB | 0.47 dB | 0.40 dB |
| 20 % | 1.43 dB | 1.27 dB | 1.15 dB |
| 50 % | 2.35 dB | 1.57 dB | 1.27 dB |
| 60 % (default) | 3.56 dB | 3.13 dB | 3.06 dB |
| 80 % | 5.22 dB | 4.84 dB | 1.18 dB |
| 100 % | **16.47 dB** | 14.34 dB | 9.11 dB |

The default-Size row is new, and it is new because the gate that claimed to
bound it **could not fail**: the loop ran 0, 20, 50, 80 and 100 % and then
asserted on "the default Size", which is 60 and was never rendered, so the
variable stayed at zero. Stage 2 caught a test like that once already.

### The envelope is no longer the dominant term

| envelope | ER tail | worst notch, Size 100 % |
|---|---|---|
| decay 0.6 (Stage 2) | -24.5 dB | 18.88 dB |
| **decay 1.5 (candidate)** | -17.3 dB | **16.47 dB** |

1.5 is still better than 0.6, by 2.41 dB. But both sit in a 16-19 dB valley, so
the envelope constant is now a second-order effect on a first-order problem.

### What actually moved it: the placement law

Two things changed in the same step - the placement law and K - so the cause was
separated rather than assumed.

**K is ruled out.** At Size 100 % the valley is 16.47 dB with the shipped K and
16.48 dB with that configuration's own ideal K: a 0.01 dB difference.

**The placement is what moved it**, and `cellOccupancy` is the knob that trades
aperiodicity against arrival density. Sweeping it at Size 100 %, E/L 25 %:

| cellOccupancy | max merged gap | periodicity | distinct gaps | valley |
|---|---|---|---|---|
| 0.05 | 10.15 ms | 7.7 % | 14 / 15 | 6.31 dB |
| 0.20 | 10.67 ms | **0.0 %** | **15 / 15** | **7.26 dB** |
| 0.40 | 11.31 ms | 0.0 % | 15 / 15 | 8.86 dB |
| **0.70 (shipped)** | 12.31 ms | 0.0 % | 15 / 15 | **16.47 dB** |
| 0.95 | 13.12 ms | 0.0 % | 15 / 15 | 42.00 dB |

**Aperiodicity and a clean handoff are not in conflict.** At 0.20 the arrival
sequence is still fully aperiodic - 0 % repetition, 15 distinct gaps out of 15 -
and the valley is less than half the shipped figure. The shipped 0.70 sits just
below a cliff: 0.95 gives 42 dB.

Note that the max-gap column barely moves across the whole sweep (10.15 to
13.12 ms) while the valley moves by 36 dB, so the mechanism is not gap width
alone - it is where a gap falls relative to the late field's 54 ms onset. The
sweep is reported as measured rather than explained beyond what it supports.

**No constant was changed.** `cellOccupancy` stays at 0.70 and `envelopeDecay`
stays at 1.5, because changing either is a production DSP change and this stage
was told not to make one. The evidence that 0.70 is a poor choice is recorded
here for that decision to be made deliberately.

Two measurement corrections from Stage 2 are carried forward and matter here:
the notch requires a **recovery** on the far side (a decay is not an energy
hole), and it is measured on **both channels at once** (per-channel sparseness
read as a 40 dB hole that was not one).

## 8. Legacy neutrality - the hard gate

**At Early/Late 0 the plugin is the PHASE 9 plugin, byte for byte, on 12 of 12
cases.**

### Which baseline, precisely

The comparison was made against **`a1328f0`**, which is the PHASE 10 Stage 3
foundation commit, **not** the PHASE 9 closure commit `0a81aca`. Calling it a
direct comparison against PHASE 9 closure would be wrong, because that binary
was never built.

What makes it equivalent is that `a1328f0` does not touch the audio path.
`git diff 0a81aca a1328f0 -- Source/` is four files:

| file | change | reaches the audio path? |
|---|---|---|
| `PluginProcessor.cpp` | one hunk, at `setStateInformation`: adds `resetParametersAbsentFrom` | no - `processInternal` is untouched |
| `PluginProcessor.h` | declares that method | no |
| `ParameterIDs.h` | adds `versionHintV1/V2/Latest`; `versionHint` kept equal to V1 | no - no parameter added or changed |
| `ParameterLayout.cpp` | `pid()` uses `versionHintV1` (same value, 1); adds an unused `appendedPid` | no - same IDs, ranges and defaults |

The render harness exercises `processInternal` and the parameter set. Neither
differs, so `a1328f0` produces the same audio as `0a81aca` by construction, and
a second baseline build was not needed. If that ever stops being true, the
regeneration recipe is in the test source.

Proved the only way it can honestly be proved: `Tests/StateCompatibility/
ProcessorRender.h` was built against `a1328f0` in a detached worktree and its
output hashed; the current build is compared against those hashes. The header is
written to compile against a build that has never heard of `earlyLate` - it
names parameters by string and ignores ids the build does not have.

Cases: impulse, noise, sine, transient train, vocal-like tone, uncorrelated
stereo, pre-delay 80 ms, attack 100 %, Size 100 % with Decay 8 s, Size 0 % with
Decay 0.4 s, cuts and damping engaged, Mix 100 % with Width 200 %.

Reasoning about the bypass branch would not have done. Stage 2 found a defect in
exactly this area that source-code reasoning had missed, and its report said not
to rely on such reasoning again.

The mechanism: `earlyLateActive` is false when the balance reports itself
bypassed - asked of the **target and the smoothing state**, never of the current
value - and the combine step is skipped entirely. Not multiplied by gains that
happen to be 1 and 0. The ER delay line is still written, so the first move away
from 0 reads current history rather than whatever was passing through when the
control was last touched.

## 9. What the early layer cannot see

At Early/Late 100 % the late gain is exactly zero, so the output is the early
layer alone. Every late-path control was then swept across its full range and the
output compared **bit for bit**:

Attack, Early Diffusion, Late Diffusion, High Damp, Low Damp, High Damp Freq,
Decay, Mod Depth - **all eight produce byte-identical output.**

Not a tolerance. An identity. And the early-only output peaks at 0.504, so these
are not the identities of silence.

## 10. Pre-Delay moves both branches equally

Residual after shifting the output back by the pre-delay, relative to peak:

| Early/Late | 20 ms | 80 ms |
|---|---|---|
| 0 % (late only - the control) | 0.014947 | 0.047650 |
| 50 % | 0.000930 | 0.002965 |
| 100 % (early only) | **0.000000** | **0.000000** |

**The obvious version of this test was wrong and is recorded as such.** It first
demanded an exact zero and the composed output gave 4.2e-4, which looks like a
leak and is not one: *the late path alone gives a larger residual*. The tank is
not perfectly time invariant even at Mod Depth 0, so feeding it the same impulse
960 samples later does not produce an exactly shifted response, and no amount of
correct pre-delay behaviour would make it.

With the late path as the control, the result is clean: the ER branch shifts
**exactly**, and composing it in does not worsen the shift.

*Known measurement characteristic:* this gate cannot be tightened to an exact
identity on the composed signal until the tank is exactly time invariant at Mod
Depth 0, which it is not.

## 11. Stereo and mono

| Early/Late | correlation | mono sum | vs the decorrelated expectation |
|---|---|---|---|
| 0 % | -0.0087 | -3.05 dB | -0.04 dB |
| 25 % | 0.0017 | -3.00 dB | +0.01 dB |
| 50 % | 0.0287 | -2.89 dB | +0.12 dB |
| 75 % | 0.0561 | -2.77 dB | +0.24 dB |
| 100 % | 0.0678 | -2.73 dB | +0.28 dB |

Every figure sits close to -3.01 dB, which is **consistent with** the downmix of
two approximately decorrelated channels of similar energy.

**It is not a proof that no cancellation occurs anywhere in the spectrum, and it
is not offered as one.** The ER layer gives each channel its own arrival times
and its own signs, which is exactly the structure that *can* produce narrow-band
cancellation in a sum. Per Stage 1's correction, the layer is **not** described as
mono-neutral. Case D of the listening set is where this gets listened to.

## 12. Automation

Steepest sample-to-sample step, against the same material with the balance held:

| sequence | step | vs held |
|---|---|---|
| 0 -> 100 | 2.023719 | 0.953x |
| 100 -> 0 | 2.360655 | 1.112x |
| 0 -> 25 | 2.023719 | 0.953x |
| 25 -> 75 | 2.072547 | 0.976x |
| 75 -> 10 | 2.324799 | 1.095x |
| 0 -> 100 -> 0 -> 100 | 2.360655 | 1.112x |
| random, 40 steps | 2.095341 | 0.987x |

Held reference 2.122654. Nothing the automation produces is a step the material
does not already contain. Steps land on arbitrary sample indices, not on block
boundaries.

## 13. CPU and memory

400 blocks of 512 samples at 48 kHz, best of 7, against a same-run tank baseline,
with a `volatile` sink so the accumulator survives:

| stage | time | % of real time | relative to the tank |
|---|---|---|---|
| the tank | 130.45 ms | 3.06 % | 1.00x |
| early reflections | 5.99 ms | 0.14 % | **0.046x** |
| ER at Early/Late 0 | 0.84 ms | 0.02 % | **0.006x** |

Memory: 2 x 8192 doubles = **128 KiB** at 48 kHz (512 KiB at 192 kHz), sized for
the 160 ms ceiling rather than the current span, so a Size change never
allocates. `prepareToPlay` is the only allocation point.

**Zero heap allocations** across 200 blocks of simultaneous Size and balance
automation, counted by the suite's replaced `operator new`.

## 14. Sample rate and determinism

Two prepares produce bit-identical schedules. Reflection **times** agree to
better than **20.7 us** from 44.1 kHz to 192 kHz - measured *across* rates, not
against each rate's own nominal span, which is the mistake PHASE 9 made (it
reported a 6.79 ms error that was really 0.0116 ms).

## 15. The `earlyLate` parameter and saved state

- Exists, displays as `Early / Late`, range 0-100, default 0, hint 2 - all
  checked.
- All nineteen historical parameters are **still at generation-1 hint**, checked
  **by name**. The old check counted them and said "every parameter is at V1",
  which became the wrong statement the moment PHASE 10 added one. Naming them is
  a *tightening*: it now also catches a new parameter wrongly given the
  originals' hint, which is the actual mistake the hint exists to prevent and
  which a count could never see.
- A PHASE 9-era state (19 children, no `earlyLate`) loaded into an instance where
  the control has been moved restores it to **0**, via
  `resetParametersAbsentFrom`.
- `er::referenceSizeControl` is checked against the Size parameter's actual
  default, because the DSP headers deliberately do not include the parameter
  layout and a repeat that nothing checks is a repeat that drifts.

### Scope of the absent-parameter reset - stated so it is not inherited by accident

`resetParametersAbsentFrom()` implements the contract of a **full plugin state**:
the blob the host saves describes the entire instance, so a parameter it does not
mention is one the saving build did not have, and the default is the only honest
thing to restore.

**That reasoning does not transfer to a partial preset.** A factory or user preset
format that deliberately stores only some parameters - a "tone only" preset, say
- means the *opposite* by omission: leave the rest as they are. If such a format
is added later it must carry its own loader and must not reuse this path. Nothing
in PHASE 10 decides that question, and nothing here should be read as having
decided it.

## 16. Test inventory

| Suite | Target | Result |
|---|---|---|
| `runPhase10ProductionTests` (10 sections) | `AuroraVerbTests` | **30 / 30** |
| `AuroraVerbStateTests` (incl. 5 new PHASE 10 sections) | separate JUCE-linking exe | **22 / 22** |

The division is deliberate. Component characterisation - schedule, Size law,
balance, handoff, stereo, automation, CPU, memory - is in the DSP target, which
does not link JUCE. The processor's own topology claims - legacy bit-exactness,
Pre-Delay co-shift, what the early layer cannot see - are tested against the
**real assembled processor**, because testing them against a hand-built chain
would be testing the hand-built chain.

## 17. Defects found and fixed in this stage

1. **The placement law produced a periodic arrival sequence** (section 4). Found
   by the schedule test, not by reading the code. Fixed by replacing
   golden-ratio placement with a fixed-seed draw inside each cell.
2. **The balance calibration constant was wrong** - the placeholder 0.5634 was
   2.115 dB off, and Stage 2's 0.7811 does not transfer because production trims
   the late branch. Re-measured to 0.7187.
3. **`cos(pi/2)` is not zero**, so "early only" was not actually early only. Both
   endpoints are now exact.
4. **Six tests or reported numbers were wrong before the code was**: the
   gap-equality criterion (section 4), the Pre-Delay exact-zero criterion
   (section 10), the version-hint count (section 15), an allocation gate that
   read its counter twice so that building its own "0 allocations" message
   tripped it, a handoff gate on "the default Size" that never rendered the
   default Size and so could not fail (section 7), and a stale handoff figure
   quoted in the first version of this report (6.02 dB against the real
   16.47 dB, section 7). Each is recorded in the test source with what it got
   wrong, because Stage 2's report asked for the research trail to be preserved.

## 17b. Open design questions this stage did NOT decide

Both are recorded with evidence and neither was acted on, because changing
either is a production DSP change and this stage was told not to make one.

1. **`cellOccupancy = 0.70` is a poor operating point** (section 7). 0.20 gives
   the same aperiodicity with a 7.26 dB valley instead of 16.47 dB, and 0.70
   sits just below a cliff.
2. **BAL-B's single-point calibration drifts to a 6.14 dB traversal at the
   corners** (section 6). Correcting it inside the current design is not
   possible without a Decay- and Size-aware K, which is the dynamic
   normalisation the brief rules out.

## 18. Regressions

**No new regression. Measured against a build of the PHASE 9 commit, not
inferred.**

The last recorded FULL run was PHASE 9 closure at 1500 / 1502, and the suite has
grown by about a thousand checks since - so "two failures then, six now" could
not be resolved by reading. `a1328f0` was rebuilt in a detached worktree and run
on the same machine:

| | baseline `a1328f0` | this stage |
|---|---|---|
| checks | 2452 / 2458 | 2481 / 2488 |
| failures | 6 | 6 |

**The same six, at identical values**, in the same order:

```
0.06022872179   T20/T30 agreement
19.34162509     stimulus-to-stimulus repeatability
0.05998214192   ensemble T20/T30 agreement
0.2154187833    the analytic decay law vs the tank
                candidate fit at 6000 Hz
                candidate fit at 12000 Hz
```

The first two are the diagnostics PHASE 7 closure classified and PHASE 8 and 9
carried forward. The other four were already failing at `a1328f0`; PHASE 9
closure recorded only two because its run covered a smaller suite set, which is
why a line-count comparison was not evidence and a rebuild was.

A seventh failure appeared in the first full run and was **mine**: the new
allocation gate read its counter twice, so building its own report tripped it.
Fixed; that suite is 30 / 30.

Also from the first full run: `MatrixModulationCpuBenchmark` failed its two CPU
gates. Re-measured on an idle machine, **both pass**. The first run had a build
running in parallel, and the table was internally inconsistent - 3373 ns at
48 kHz against 509 ns at 96 kHz for the same work. That benchmark's own source
already records this threshold firing twice on machine state rather than on a
code change.

The known pre-existing items are unchanged and were not weakened to make this
pass:

- The PHASE 8 RT60 sensitivity harness remains known-bad and is still not used as
  an absolute gate.
- Size compensation still misses its +/-0.5 dB target at Decay 0.1 s (worst
  5.27 dB), caused by the 12 dB safety limit, exactly as recorded in PHASE 6.
- The tank is not exactly time invariant at Mod Depth 0 (section 10).

No existing gate was loosened. One was tightened (section 15) and two were
corrected to measure what they claimed to measure (sections 4 and 10).

## 19. Known characteristics that are not shipping blockers

- The handoff valley at Size 100 % is **16.47 dB**, and it is **not** a
  shipping-ready figure. Section 7 shows it is dominated by `cellOccupancy`
  rather than by the envelope, and that a different value of that one data
  constant more than halves it. This is a design decision outstanding, not a
  characteristic to be accepted.
- The balance traversal reaching 6.14 dB at the corners (section 6) is likewise
  **an open design issue**, not an accepted characteristic.
- The composed Pre-Delay shift residual cannot be tightened to an identity while
  the tank is not exactly LTI (section 10).
- The mono downmix figure is consistent with decorrelated channels and is **not**
  a proof of no cancellation (section 11).

## 20. The listening set

`renders/phase10-stage3-ab/` - **eight WAVs, four cases**, plus `LISTENING.md`
and `KEY.txt`. Rendered by `Scripts\export-phase10-listening.ps1`.

### Why the primary comparison is not the envelope

The set was first built to ask whether a 6.02 dB valley reads as a hole. That
figure was stale (section 7) and the real one is 16.47 dB. Re-measuring also
separated the cause, and the two axes are not close:

| axis | range | effect on the Size-100 valley |
|---|---|---|
| envelope decay | 0.6 -> 1.5 | 18.88 -> 16.47 dB |
| `cellOccupancy` | 0.70 -> 0.20 | 16.47 -> **7.26 dB** |

Both envelope candidates sit inside the same structural valley, so a listener
asked to choose between them would be choosing between two versions of the same
problem. `cellOccupancy` is the axis that decides whether the problem exists,
and 0.20 is just as aperiodic as 0.70 - 0 % repetition at every period, fifteen
distinct gaps out of fifteen.

So cases A and B are a blind A/B between those two values.

| Case | Files | Material | Setting | What it tests |
|---|---|---|---|---|
| **A** | `caseA-1/2.wav`, blind | snare | Size 100, E/L 25, 100 % wet | handoff valley: 0.70 vs 0.20 |
| **B** | `caseB-1/2.wav`, blind | vocal | Size 100, E/L 50, 100 % wet | the same, on musical material |
| **C** | `caseC-snare-el050/el000.wav` | snare | Size 80, 100 % wet | slap, rhythm, metallic repetition, combing - with the no-ER render as the reference that lets a listener attribute what they hear |
| **D** | `caseD-vocal-stereo/mono.wav` | vocal | Size 80, E/L 50, Mix 30 % | stereo pull, unstable centre, coloration, mono behaviour |

**Nothing is shipped on this basis yet.** `cellOccupancy` stays at 0.70 in
production; 0.20 exists only as render-time Pattern data. Which value becomes
the shipping candidate is decided after listening.

**Blinding.** The A/B mapping is randomised, differs between case A and case B
so an order learned on one does not carry, appears in no filename, and lives in
`KEY.txt`. Within each pair the source, gain, sample rate and length are
identical.

**Level was checked rather than assumed.** The case B pair differs by 0.76 dB in
peak, which looks like a loudness cue; measured on integrated level the pairs
differ by **0.00 dB and 0.08 dB**, so there is none. Peak is not what a listener
judges loudness by, and a blind pair has to be checked on the quantity that
could actually give the answer away. `LISTENING.md` states the figures and asks
the listener to judge behaviour in time rather than level.

### The superseded 26-file set

`renders/phase10-stage3/` - 26 files, rendered by
`Scripts\export-phase10-listening.ps1`. Everything except Size and Early/Late is
at the product's own defaults in every render. **Nothing is level-normalised**,
because the balance holds the wet energy to 0.03 dB and matching would only hide
whatever is really there.

| Case | Material | What it asks |
|---|---|---|
| **A** | snare, pad - Size 100 %, E/L 0/25/50, 100 % wet; plus a **blind** Size 80 vs 100 pair at E/L 25 | **the primary question:** does the large-Size valley read as separation or as a hole? |
| **B** | snare - Size 80 %, E/L 15/50/75, 100 % wet | discrete slap, flam, repeating rhythm |
| **C** | vocal, piano - Size 80 %, E/L 0 and 50, **shipping Mix 30 %** | metallic repetition, comb coloration, imposed pitch |
| **D** | snare - Size 80 %, E/L 75 %, stereo vs its own mono downmix, with the E/L 0 downmix as reference | stereo pull, unstable centre, mono collapse |

The case A Size pair is blind and its permutation differs per source, because a
listener who knows which file the measurement complained about will hear what the
measurement predicted. The mapping is in `KEY.txt`.

## 21. Status and what happens next

**Done:** the ER engine, the pattern data, the `earlyLate` parameter, the
processor integration, 52 passing checks across two targets, the listening set.

**Waiting on a human:** which of two `cellOccupancy` values becomes the shipping
candidate. The eight-file blind set exists and is the thing that answers it. No
value is accepted before those answers.

**Open and not decided here:** BAL-B's single-point calibration (section 17b).
It is reported, a test fails on it deliberately, and no constant was touched.

**Corrected in this revision:** the handoff figure (6.02 dB was stale; the real
value is 16.47 dB), the claim that the traversal is 0.03 dB everywhere (it is
0.03 dB at the reference and up to 6.14 dB at the corners), and the description
of the legacy baseline (`a1328f0`, with evidence that its audio path matches
PHASE 9 closure, rather than a comparison against `0a81aca` that was never
built).

**Not started, per the brief:** Mode, Character, Presets, GUI, or any other V1
feature.
