# Phase 8 — closure

Pre-Delay, Low Cut, High Cut. Three controls added on the wet path ahead of
the tank, with the Phase 7 signal path reproduced bit for bit when all three
sit at their neutral endpoints.

Phase 7 remains closed and was not reopened. No Phase 7 default was changed,
no damping was retuned, no modal analysis was revisited.

---

## Commits

| stage | commit |
|---|---|
| Phase 7 production baseline | `44ae4f6` |
| Stage 1 — architecture proposal | `3e73a94` |
| Stage 2 — production DSP | `d858cea` |
| Stage 2.5 — engineering signoff | `bd04907` |
| Stage 3 — listening tooling | `63405bc` |
| Stage 3 — listening set and fixes | `cf1766a` |

---

## Final architecture

```
input
  -> Low Cut
  -> High Cut
  -> Pre-Delay
  -> reverb tank
  -> existing wet trim
  -> Width
  -> Mix
```

Topology A, as proposed and approved. The three new stages act on the wet
buffer only. **The dry path remains structurally untouched** — it is not
routed through either filter or the delay line, so dry timing is preserved by
construction rather than by care taken at each call site.

Low Cut and High Cut are **excitation filters, outside the feedback network.**

### The claim, stated exactly

Cut parameters do not enter the tank's recurrence, feedback or damping
configuration, and therefore **do not directly alter the tank's decay law.**

They do not modify: RT60 · the damping controls · the shelf coefficients ·
the feedback gains · the delay lengths · the modulation parameters · the
recurrence configuration.

They *can* change modal excitation weighting, the energy observed in a band,
what a finite-band RT60 estimator reports, and — the tank being modulated,
hence time-varying — the time-varying weighting of what reaches the output.

**Not claimed:** that a measured RT60 can never change. That is too strong for
a finite, modulated, modal system, and it is not what the architecture needs
to be true.

---

## Parameter contract — preserved unchanged

| parameter | range | skew centre | default |
|---|---|---|---|
| Pre-Delay | 0 – 500 ms | 60 ms | **0 ms** |
| Low Cut | 20 – 1000 Hz | 200 Hz | **20 Hz** — neutral endpoint |
| High Cut | 500 – 20000 Hz | 4000 Hz | **20 kHz** — neutral endpoint |

IDs, ranges, defaults, skew and serialization semantics were all preserved.
All three parameters already existed in `ParameterIDs.h` and
`ParameterLayout.cpp` before Phase 8; the phase added their DSP, not their
contract. APVTS stores normalised values, so changing a range would silently
remap saved sessions — that was a hard constraint and it was met.

---

## Pre-Delay — final design

- **integer-sample** delay
- **single** delay storage
- **dual read taps**
- **20 ms linear** crossfade between taps
- taps **do not move** during a transition
- **no sample-by-sample delay glide** — the derivative of a delay time is the
  pitch shift applied to whatever is passing through it, so there is none
- a pending target is **coalesced** while a fade is active: a single pending
  slot is overwritten, not queued
- an active fade **always completes**
- the latest pending target is then applied immediately
- the delay line is **written continuously even while 0 ms is selected**,
  otherwise the buffer goes stale and the first move away from 0 ms would play
  audio from whenever the control was last touched
- **0 ms is explicit zero delay, not one sample** — `readAt(0)` returns the
  sample just written, and the fractional interpolator is never used

### Measured

| | |
|---|---|
| Timing, 44.1 / 48 / 96 kHz | **exact** |
| Combinations tested | 21 (3 rates × 7 durations) |
| Worst timing error | **0** |
| Worst amplitude error | **0** |
| Stereo alignment | sample-aligned |
| Block-size identity | 4096 = 64 = 1 sample, **bit-exact** |
| 0 ms | bit-exact, 0 of 8192 samples differ |

Rapid automation: 1500 random changes, no NaN or Inf; three mid-fade requests
coalesce to the last; the first fade reaches its own target and the system
comes to rest at the last requested value.

---

## Filters — final design

First-order one-pole, both.

```
LP(z) = (1 - a) / (1 - a z^-1),    a = exp(-2 pi f / fs)

High Cut:   y = LP(x)
Low Cut:    y = (x - LP(x)) * (1 + a) / (2 a)
```

### Why the Low Cut normalisation exists

`x - LP(x)` has transfer function `a(1 - z^-1)/(1 - a z^-1)`. Its magnitude
rises monotonically from exactly 0 at DC to **`2a/(1+a)` at Nyquist — not to
1**. Uncorrected, the passband of a filter whose passband is supposed to be
transparent droops: 0.11 dB at a 200 Hz corner, and **0.64 dB at the 1000 Hz
top of the shipped range**. A broadband 0.64 dB is louder than most things
this project measures to three decimals, so it is corrected rather than
tolerated, by one multiply by a precomputed constant.

Because the uncorrected magnitude is monotone in frequency, dividing by its
maximum cannot make the filter clip. For the record: `|H|^2 = a^2(2-2c)/(1-2ac+a^2)`
with `c = cos w`, and `d/dc` of that is `-2(1-a)^2/(...)^2 < 0`, so `|H|`
increases with `w` for every `a` in `(0,1)` and the maximum is at Nyquist,
always. Across the shipped 20–1000 Hz range `k` stays within `[1.000, 1.077]`.

### Preserved

`|H| <= 1` · no resonance · stable coefficients at every sample rate ·
first-order topology.

The impulse-invariant form was kept over the bilinear transform by
measurement, not by preference: `a = exp(-2 pi f / fs)` lies in `(0,1)` for
every `f > 0` at every rate, so there is no coefficient region to exclude and
no pre-warping blow-up near Nyquist. At an 8 kHz corner probed at 16 kHz the
impulse-invariant form spreads **1.43 dB** across sample rates against the
bilinear form's **3.46 dB**; at 4 kHz/16 kHz, 1.53 against 4.43.

### Measured

| | |
|---|---|
| Implementation vs analytic response, worst error | **< 0.05 dB** |
| Low-cut sample-rate consistency, tested regime | **<= 0.0116 dB** |
| Low-cut passband at Nyquist after normalisation | unity to `<= 0.001 dB` |

> The arbitrary `>= 10 dB at 1.5 octaves below cutoff` criterion is
> **rejected and is not to be revived.** It was arithmetically inconsistent
> with a first-order high-pass, which gives about −9.54 dB there, so it would
> have forced a second-order fallback by construction rather than by evidence.
> Filter order is not chosen by an arbitrary slope threshold.

---

## Neutral transparency

Neutral configuration: **Pre-Delay 0 ms · Low Cut 20 Hz · High Cut 20 kHz.**

| | |
|---|---|
| Samples compared | 48 000 |
| Samples different | **0** |
| Worst difference | **0.0** |

### BIT-EXACT PHASE-7 NEUTRAL PATH

Each stage is bypassed on a flag at its neutral endpoint rather than run with
coefficients that happen to be nearly transparent — the house pattern from
`LineDamping`. The bypass decision is taken from the **settled target**, not
the smoothed value: `LogSmoother` recovers its linear value through
`exp(log(x))`, which need not return `x` bit-exactly, so an equality test on
the smoothed value could miss the endpoint for ever and leave a nominally
neutral filter running.

---

## Stage 2.5 — the one genuine Phase 8 defect

Filter state was frozen during bypass and **reused on re-entry**, so audio
from before a bypass was audible after it.

### Before the fix

Measured by running the same signal and the same automation twice, differing
only in what the filter did before a two-second bypass:

| | |
|---|---|
| Low Cut 21 Hz re-entry, worst difference | **0.0311** — about **6.9 % of peak** |
| Samples differing after re-entry | **9600 / 9600** |

Once the state diverged it stayed diverged for the whole active period.

### The fix

Prime the filter state on the first active sample so that that sample passes
through unchanged, from the sample itself:

```
high cut   s = x                  (a s + (1-a) x = x exactly)
low cut    s = x (a - 1/k) / a    (solves (x - s') k = x)
```

Six lines. One branch per block, hoisted out of the sample loop.

### After the fix

| | |
|---|---|
| History dependence | **bit-zero** — 0 of 9600 samples differ, all six cases |

**No bypass crossfade was required**, and none was added.

### Preserved

Bit-exact neutral path (0 of 48 000) · first-order topology · parameter
contract · commutation at exactly `4.161784462e-16` · all 40 Stage 2 Phase 8
checks.

---

## Endpoint automation

| | |
|---|---|
| Low Cut neutral endpoint | 20 Hz |
| High Cut neutral endpoint | 20 kHz |

**30 endpoint-transition movements** tested across a sine, noise and a
music-like signal, with the transition index swept across a full probe period
so no result depends on where in the waveform it landed.

| | |
|---|---|
| Worst excess transition | **0**, within one floating-point residue (`1.6e-14 %` of peak) |

Excess is measured against `max(ideal switch, the signal's own step)` — the
step an instantaneous change made by an already-settled filter would take, or
the step the input already had, whichever is larger. Every move leaving an
active filter for OFF steps exactly the ideal step, which is the unavoidable
timbral discontinuity; every move entering a filter steps exactly the input's
own step.

Continuous endpoint-touching automation also passed: a 3 Hz triangle onto the
endpoint with the corner applied every 64 samples gives output/input step
ratios of 0.88 – 0.996, all at or below 1. A one-pole cannot make its input
steeper and the sweep passes through OFF on every cycle, so a ratio at or
below 1 means the repeated switching adds no step of its own.

**No bypass morphing was required.**

---

## Damping / cut distinction — the wording, corrected permanently

The `4.2e-16` commutation result was obtained with:

- `FdnCore<8>` — the Phase 7 sweep harness, **not** the complete production
  `MorphingStereoFdn<8>`
- **Mod Depth 0**, mapping to exactly 0.0 in both modulation engines
- **modulation disabled**

### It is a STATIC / LTI CONTROL TEST

It must **not** be generalised to the shipping modulated tank. Depth 0 was
verified time-invariant rather than assumed — shift the input by 977 samples
and the output shifts by exactly 977, to `1.26e-25` — and the same tank at the
shipped Mod Depth 30 % fails that test at `2.03e-3`, twelve orders of
magnitude worse, because the delay lengths and the feedback rotation move
under an LFO. An LTI filter and a time-varying system do not generally
commute.

### Production structural verification, at Mod Depth 30 %

On the complete production tank:

| | |
|---|---|
| Tank control/state configuration values compared across a full cut sweep | **234, all unchanged** |
| Replay of the same post-cut input into a tank that never saw a cut parameter | **bit-identical** |

The snapshot covers both Size banks: delay lengths, feedback gains target and
current, effective loop delays, RT60, damping ratios and activity, every shelf
spec and normalisation, allpass depth, rate, mode, per-line coefficients, lane
rates and added loop delay, matrix depth and rate, Size, morph position and
phase, and the compensation gains. Audio was actually run, so a parameter
reaching the tank through a shared buffer or a stale pointer would have shown
up.

Neither check is an algebraic commutation argument, so modulation does not
weaken either. They are routing facts.

---

## RT60 harness limitation — recorded openly

The Stage 2 RT60 sensitivity harness compressed the expected spectral
differences and did not reproduce the Phase 7 analytic absolute values.

| band | measured | prior analytic expectation |
|---|---|---|
| 12 kHz | ~**2.29 s** | ~**1.82 s** |

All bands from 500 Hz to 12 kHz landed within 2.15 – 2.29 s.

Separately, a post-tank control settled whose the measured drift was: applying
the same cut *after* the tank, where it is mathematically incapable of
changing any mode's decay, produced the same measured change — so the drift
belongs to the estimator. A one-octave analysis band has finite rejection, so
a bright excitation leaks fast-decaying high-frequency energy into every band,
and removing the highs removes the leak.

**Therefore no `<= 1 %` quantitative Phase 8 RT60 gate was created from that
harness**, and it is not cited as proof of the damping/cut distinction. The
structural routing guarantee plus the static LTI control test carry that
claim.

Phase 7 conclusions are unaltered — those studies compared settings through
the same analyser, so a common bias cancels. The analysis was not reopened
during closure.

---

## Automated regression

### Final Stage 2.5 focused regression: 990 / 992

The two failures are pre-existing Phase 7 diagnostics, at values **bit-identical**
to the prior failures:

```
0.06022872179
19.34162509
```

| | |
|---|---|
| **New regressions** | **0** |
| Phase 8 suites | **55 / 55 PASS** |

No historical gate was weakened, and no expected value was silently updated.

---

## CPU — incremental cost

These are the **added** cost of the Phase 8 stages. They are **not** total
plugin CPU numbers.

| | ns/sample | relative to the tank |
|---|---|---|
| Phase 7 tank baseline | ~199.923 | — |
| Phase 8 **added**, active | ~**7.845** | ~**3.92 %** |
| Phase 8 **added**, neutral | ~**2.368** | ~**1.18 %** |

Stage 2.5 remeasurement after the priming fix: ~**4.13 %** active overhead
(tank 206.443, added active 8.528, added neutral 2.638 — 1.28 %). The absolute
figures move with the machine; the ratio does not. Priming adds one branch per
block and sits below the noise floor of this measurement.

The engaged figure is two one-poles and a normalising multiply per channel
plus two ring-buffer writes and two reads — roughly twenty operations, against
a tank carrying eight delay lines, an 8×8 matrix and sixteen damping shelves.
The suite's gate is a pathology detector sized from that operation count, not
a budget: it exists to catch an accidental allocation or a denormal storm,
which would be orders of magnitude.

---

## Listening validation

### Validation artifact

```
C:\JuceBuilds\plugindemo\AuroraVerb_artefacts\Release\VST3
  \AuroraVerb.vst3\Contents\x86_64-win\AuroraVerb.vst3
```

| | |
|---|---|
| SHA256 | `BE4807868E2DC7A5FE67BA12C272D922B4D32B0CCFAA43572FD6E0109D73644C` |
| Profile | validation — diagnostics ON, capture OFF, candidate 1 |
| `AV_COPY_PLUGIN_AFTER_BUILD` | OFF |

Built from `63405bc`. `git diff --name-only bd04907 -- Source/ CMakeLists.txt`
returns zero files, so this binary is the Stage 2.5 plugin.

### Listening render folder

`renders\phase8-stage3` — 39 files, 48 kHz, 32-bit float. Four cases over four
sources, rendered through the production `StereoCutFilters` and `PreDelay` in
the processor's own order. Regenerate with
`.\Scripts\export-phase8-listening.ps1`. See
[`Docs/Phase8-Stage3-Listening.md`](Phase8-Stage3-Listening.md).

### LISTENING VERDICT: PASS

The user listened to the prepared set and reported **no audible problem.**

The listener did not identify:

- click / pop
- stale-state burst
- pitch glide
- abnormal pre-delay behaviour
- dry-path timing shift
- ringing
- metallic filter artifact
- unusable Low Cut behaviour
- unusable High Cut behaviour
- obvious product defect

No subjective preference claim is recorded beyond this.

---

## Old FL validation junction — a live hazard

A Phase 7 system VST3 junction **still exists** and does **not** point at the
Phase 8 validation artifact:

```
link    C:\Program Files\Common Files\VST3\AuroraVerb.vst3
target  C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
SHA256  4B25281AA97209E54073DB679AD6A564ED8A0CCAB6697E8D0D6803BEEE9DC7DE
dated   2026-11-13 15:42
```

So **FL Studio currently loads that older binary, not the Phase 8 build.**
Anything heard in FL right now is pre-Phase-8 and should not be reported as a
Phase 8 observation.

It was left exactly as it was. Repointing or removing it needs elevation under
Program Files, which is a separate deliberate act, and Phase 8's listening
validation needed no host. To use the Phase 8 artifact in FL, repoint the
junction at the path above with `Scripts\prepare-fl-validation.ps1`, which
verifies the source hash before creating the link and re-hashes the binary
*through* the link afterwards.

---

## Accepted limitations

1. **The RT60 estimator cannot measure the cut/damping distinction.** Recorded
   above with the control experiment that establishes it is the estimator and
   not the tank. The structural claim is proved by routing instead, and the
   `<= 1 %` gate the Stage 1 proposal pre-registered was withdrawn rather than
   weakened.
2. **The commutation result is static-only.** It stands as a control test at
   Mod Depth 0 and is not evidence about the shipping modulated tank.
3. **There is a step at each endpoint, because the endpoint means OFF.** It is
   measured and reported rather than morphed away: worst low-cut step at or
   above 30 Hz is 1.61 dB, worst high-cut step below 20 kHz is 1.21 dB. No
   avoidable excess remains.
4. **Priming does not remove settling.** A low cut entering on a bass-heavy
   signal still takes its time constant to remove the lows. That is the filter
   doing its job, it is smooth by construction, and it is what a tonal change
   across OFF sounds like.
5. **At the shipping Mix, a 10 kHz High Cut is marginal on an already-bright
   source** — 0.20 dB, flagged as a null trial in the Stage 3 manifest. This
   is arithmetic, not a defect: at 30 % wet a change confined to the reverb
   arrives scaled by 0.3 under 70 % of something unchanged.

---

## Test infrastructure debt — unchanged, still deferred

Carried forward from
[`Docs/Phase7-TestInfrastructureDebt.md`](Phase7-TestInfrastructureDebt.md),
untouched during Phase 8 because none of it blocked the work:

- the centralised shipping-default test contract
- stale manually duplicated historical defaults — `Tests/Phase8Listening.cpp`
  is now one more site that duplicates the shipping defaults, and it says so
- the README reference to the removed `AV_DEV_DIFFUSION_PREVIEW` flag

---

## Research trail

Preserved, including the parts that failed. Nothing was rewritten to hide a
wrong turn.

### Test-design errors made and corrected

Stage 2 — five: peak-sample magnitude measurement under-reading by up to
3 dB at four samples per cycle; a block-size test applying its control once
per chunk, so the single-chunk run never requested the move; a truncated
excitation discarding the filter's own output tail and breaking commutation at
`3.8e-4`; an excitation pre-filtered with the analysis band, double-filtering
until the measurement could not see High Damp at all; and a single-run CPU
timing that reported the bypassed pre-delay as slower than the engaged one.

Stage 2.5 — four: a wrong click reference, twice (first the destination run's
own steepness, then the ideal switch alone, both reading passing behaviour as
failure); one transition index at sample 4800, which for a 1 kHz tone at
48 kHz is exactly a zero crossing; a periodic stale-state signal whose period
divided the bypass length, making two different histories agree by accident;
and conflating legitimate settling with staleness.

Stage 3 — four: Case 4 matched on a quantity High Damp cannot reach, which was
itself the finding; a whole-render centroid diluted by the source's own span;
a late window at −95 dB, matching on inaudible residue; and two tilt measures
saturating on one-sided sources, one of them split exactly on the corner it
was measuring. One source choice was overturned by measurement — Case 3's
vocal holds 12 dB less energy above 3 kHz than below and moved 0.05 dB at a
10 kHz cut.

### Gates replaced with justified ones

The endpoint step gate (1 dB → the filter's own response, 0.05 dB) · the
sample-rate spread gate (0.75 → 0.25 → split into two physical regimes) · the
click gate (`max(ideal, input)`) · the continuous-automation reference (the
input's own steepness, which a one-pole provably cannot exceed) · and the
withdrawn `>= 10 dB / 1.5 octave` filter-order criterion, which is recorded in
the test output as withdrawn rather than deleted.

---

## Not done, deliberately

- Renaming AuroraVerb / ACTIVE VERB
- GUI redesign
- Early Reflections, Early/Late split, Attack / Bloom / Density
- Modulation architecture changes
- Any Phase 7 retuning
- Unrelated technical-debt cleanup
- Phase 9

---

## PHASE 8 COMPLETE

Pre-Delay, Low Cut and High Cut ship. The neutral path is bit-identical to
Phase 7, the one defect found during validation was fixed and its fix proved
bit-exact, the automated regression carries no new failure, and the prepared
listening set found no audible problem.
