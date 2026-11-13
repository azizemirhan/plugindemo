# AuroraVerb

An original professional algorithmic reverb (VST3 / Standalone, Windows x64).

The product name is a placeholder and can be changed in one place — the
`AV_PRODUCT_NAME` / `AV_COMPANY_NAME` / `AV_BUNDLE_ID` / `AV_PLUGIN_CODE`
variables at the top of `CMakeLists.txt`. No source file hard-codes it.

> **Scope note.** This project uses commercially available reverbs only as
> *listening benchmarks* for sound quality, density, stereo width, decay
> behaviour and modulation quality. All DSP algorithms and all software
> architecture here are original work.

---

## Current status — PHASE 7 complete, shipping build hardened

| Area | State |
|---|---|
| `FeedbackMatrix` — orthogonal Hadamard / Householder, verified against a reference multiply | done |
| `FdnDelayLine` — integer-delay ring buffer with explicit capacity | done |
| `FdnCore` — 8×8 network, RT60 decay maths, damping slot reserved | done |
| `StereoFdn` — searched injection/readout vectors, short input diffuser | done |
| `MorphingStereoFdn` — dual-bank Size changes, level-triggered priming | done |
| `AllpassDiffuser` — Schroeder section, exactly allpass, click-free automation | done |
| `DiffusionStage` — allpass plus click-free engagement crossfade | done |
| `DiffusionNetwork` — 2–8 series stages, searched delay set, Diffusion mapping | done |
| `StereoDiffusionNetwork` — structural decorrelation, no M/S widening | done |
| CMake + JUCE 8 build (VST3 + Standalone) | done |
| Build profiles — `production` / `development` / `validation`, every identity-deciding flag stated explicitly | done |
| Shipping / build hardening — installed artefact hash-verified, host regression PASS | done |
| C++20, static MSVC runtime, x64 | done |
| Full parameter layout (19 parameters, APVTS) | done |
| Parameter smoothing — three laws, chosen per parameter kind | done |
| Denormal protection (`ScopedFtz`, additive guard, `flushDenormal`) | done |
| Mono-or-stereo in → stereo out bus handling | done |
| Double-precision internal signal path | done |
| Zero audio-thread allocation, proven by a heap-allocation detector | done |
| State save/load (XML via APVTS) | done |
| Development GUI (generic controls) | done |
| `DelayLine` — power-of-two ring buffer, fractional reads, multi-tap | done |
| `Interpolation` — linear / Catmull-Rom / 3rd-order Lagrange | done |
| `Lfo` — sine / triangle / seeded smooth random | done |
| `ModulatedDelay` — click-free delay changes, pitch-deviation budget | done |
| `ScratchArena` — pre-allocated bump allocator for the audio thread | done |
| `GivensRotation` / `ModulatedFeedbackMatrix` — time-varying orthogonal matrix | done |
| `TimeVaryingAllpass` — energy-preserving rotation form, Thiran, Schroeder reference | done |
| `AllpassPhaseBank` — per-line phase modulation, true bypass at depth 0 | done |
| `ModulationArchitecture` — the seven candidates and the selected default | done |
| RT60 loop-delay correction — decay hits its target with modulation on | done |
| `SizeCompensation` — measured 2D surface, per-bank through a morph | done |
| `ParameterMapping` — perceptual curves, JUCE-free and tested | done |
| Diagnostic layer — lock-free telemetry, session reports, audio capture | done |
| `DiagnosticAnalyzer` — offline analysis reusing the PHASE 3–5C measurements | done |
| FL Studio test tooling — watcher, launcher, session analyser, checklist | done |
| Frequency-dependent decay (PHASE 7) — per-line low/high shelves, Option A composite normalisation | done |
| Damping default operating point — swept to interior optima and adopted | done, with accepted limitations |
| Certified modal analysis — 18657 / 18657 poles, 0 duplicates, 0 unresolved | done, frozen |
| Offline DSP test harness — 2350 checks | done |
| **Algorithm bank** | **not yet — the wet path is an identity placeholder** |

**PHASE 7 closed with four documented accepted limitations** — see
[`Docs/Phase7-Closure.md`](Docs/Phase7-Closure.md). In short: the ~2 % Problem A
research target was slightly missed at ~2.3 % (from 7.90 %), High Damp 100 %
keeps a known extreme tail-curvature diagnostic, six of 2350 checks
intentionally stay red as historical or rejected-topology records, and blind
listening found source-dependent voicing preferences but no consistent
perceptual defect. Phase 8 has not started.

The plugin loads in a DAW, shows all parameters, automates them without zipper
noise, saves/restores state, and passes audio through. The DSP core, the
diffusion network and the FDN tank are complete and measured, but not yet wired
into the shipping audio path.

To hear the tank in a host, configure with `-DAV_DEV_DIFFUSION_PREVIEW=ON`.
That routes the wet path through input diffusion into the 8×8 stereo FDN and
maps Decay, Size and Early Diffusion onto it, with a fixed −12 dB trim so a
full-scale input cannot clip. Add `-DAV_DEV_ALLPASS_CANDIDATE=n` to choose which
tank modulation engine Mod Depth drives — see
[PHASE 5B](#tank-allpass-phase-modulation-phase-5b). The options are off by
default and the default build is unaffected; every variant passes `pluginval` at
strictness 10.

### Verification

- Release build: **0 errors, 0 warnings** (`/W4`, MSVC 19.44, x64).
- Offline DSP tests: **1077 / 1077 pass** across 150 suites.
- `pluginval --strictness-level 10` on the built VST3: **SUCCESS**, including
  editor tests, editor automation, plugin state restoration, background-thread
  state, parameter thread safety, bus enumeration and parameter fuzzing, across
  44.1 / 48 / 96 kHz at block sizes 64–1024.

```powershell
.\Scripts\build.ps1 -Tests      # build and run the DSP suites
.\Scripts\run-tests.ps1         # run the suites against an existing build
```

### Build variants

| Configure options | What it is |
|---|---|
| *(none)* | Default build. Identity wet path, no diagnostics, no telemetry code at all. |
| `-DAV_DEV_DIFFUSION_PREVIEW=ON` | Preview build. The reverb engine is audible; diagnostics still absent. |
| `+ -DAV_DEV_DIAGNOSTICS=ON` | Diagnostic preview. Telemetry active, session reports written. |
| `+ -DAV_DEV_AUDIO_CAPTURE=ON` | Adds input/output WAV capture. |

`-DAV_DEV_ALLPASS_CANDIDATE=0..3` selects which modulation engine the preview's
Mod Depth drives. All variants pass `pluginval` at strictness 10.

### Known issue: Smart App Control

This machine has Windows Smart App Control enabled
(`HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState = 1`).
It refuses to start locally-built, unsigned executables, so
`AuroraVerbTests.exe` is blocked with "Application Control policy blocked this
file". It does **not** block unsigned DLLs from being loaded, which is why the
VST3 still validates.

The test target therefore also builds `AuroraVerbTests.dll`, exporting the same
suites. `Scripts\run-tests.ps1` tries the executable first and falls back to the
DLL automatically, so the suites run without weakening the setting. Turning
Smart App Control off would also work, but it cannot be turned back on without
reinstalling Windows.

It has shown up in four forms so far, and the blocks are per file hash, so they
are intermittent — the same source can build fine one day and be refused the next:

1. **The test runner.** Handled by the DLL fallback above.
2. **A freshly linked VST3.** `pluginval` reports *"No types found. This usually
   means the plugin binary is missing or damaged"*, and any LV2 `manifest.ttl`
   errors printed alongside it are unrelated probe noise. Deleting the `.vst3`
   and rebuilding gives the binary a new hash, which gets through — though not
   always on the first attempt. During round two the preview configuration was
   refused twice and accepted on the third link, while the default and
   diagnostic configurations built minutes earlier from the same source were
   never touched.

   pluginval's own message is misleading here: the binary is neither missing nor
   damaged. To tell a block from a real load failure, call `LoadLibraryW` on
   `Contents\x86_64-win\AuroraVerb.vst3` and read the error. **4551** is
   *"Application Control policy has blocked this file"* and means the build is
   fine; anything else is a genuine defect and rebuilding will not fix it.
   `Scripts\run-pluginval.ps1` performs this check and reports which one it is.
3. **`vst3_helper.exe` during the build.** Handled by `VST3_AUTO_MANIFEST FALSE`
   — `moduleinfo.json` is optional host-scan metadata, and the plugin passes
   `pluginval` at strictness 10 without it.
4. **`juceaide` during a fresh configure.** JUCE builds this helper and then runs
   it, so a blocked binary fails configure outright with *"Testing juceaide
   failed"*. Deleting only the JUCE build tree does not help, because the rebuilt
   binary is byte-identical and hashes the same. Copying an already-accepted
   `juceaide.exe` from another build tree into
   `_deps/juce-build/tools/extras/Build/juceaide/juceaide_artefacts/Custom/`
   and re-running configure does. This is the reason to avoid wiping a working
   build directory unnecessarily.

---

## Toolchain

Already installed and verified on this machine:

- **Visual Studio 2022 Build Tools 17.14** (MSVC 19.44, Windows SDK 10.0.26100)
- **CMake 4.4.1** at `C:\Program Files\CMake\bin`
- **JUCE 8.0.15** checked out at `External/JUCE` (git-ignored)

To reproduce on another machine:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Kitware.CMake
```

JUCE does not have to be checked out by hand — if `AV_JUCE_PATH` is empty,
CMake fetches the pinned tag (`8.0.15`) on first configure.

---

## Building

```powershell
.\Scripts\build.ps1                 # Release VST3 + Standalone
.\Scripts\build.ps1 -Config Debug
.\Scripts\build.ps1 -Tests          # also build and run the offline DSP tests
.\Scripts\build.ps1 -Clean
```

The VST3 is installed after each build into
`%LOCALAPPDATA%\Programs\Common\VST3` — the per-user VST3 folder, so builds
never need an elevated shell. Override with `-DAV_VST3_COPY_DIR=...`, or turn
the copy off with `-DAV_COPY_PLUGIN_AFTER_BUILD=OFF`.

### Known issue: non-ASCII project path

This project currently lives under `...\Demirhan Özdemir\Desktop\Müzik\...`.
JUCE's build helper `juceaide` crashes (`Unhandled exception`) when the **build
directory** contains non-ASCII characters — it cannot open its own output file.
MSVC itself is fine with such paths, and so is the JUCE checkout.

`Scripts\build.ps1` detects this and redirects the build tree to
`C:\JuceBuilds\<project-name>` automatically, printing a warning. Everything
works, but artefacts land outside the project folder.

The permanent fix is to move the project to an ASCII-only path such as
`C:\Dev\valhalla-reverb`, after which `build\` is used normally.

---

## Layout

```
CMakeLists.txt              build, product identity, JUCE acquisition
Scripts/build.ps1           one-command build + test
Source/
  PluginProcessor.h/.cpp    audio processor, bus handling, render loop, state
  PluginEditor.h/.cpp       development GUI (replaced in PHASE 20)
  Parameters/
    ParameterIDs.h          parameter IDs, Mode and Color enums
    ParameterLayout.h/.cpp  APVTS layout, ranges, value formatting, raw handles
  DSP/                      JUCE-free, header-only, independently testable
    DspConfig.h             project-wide conventions, rate scaling
    Denormal.h              ScopedFtz, additive guard, subnormal helpers
    Interpolation.h         fractional-delay kernels + why each was chosen
    DelayLine.h             ring buffer, multi-tap fractional reads
    Lfo.h                   sine / triangle / seeded smooth random
    ModulatedDelay.h        delay + LFO + smoothing, pitch-deviation budget
    SmoothedParameter.h     Linear / OnePole / Log smoothing laws
    ScratchArena.h          pre-allocated bump allocator for the audio thread
    AllpassDiffuser.h       Schroeder allpass section + the equation behind it
    DiffusionStage.h        allpass plus click-free engagement crossfade
    DiffusionTuning.h       delay sets, Diffusion mapping, stereo constants
    DiffusionNetwork.h      series chain of 2-8 stages
    StereoDiffusionNetwork.h  two chains, structural decorrelation
    ConfigResult.h          explicit outcome for capacity-bounded changes
    FeedbackMatrix.h        orthogonal Hadamard / Householder mixing
    FdnDelayLine.h          integer-delay line with explicit capacity
    FdnTuning.h             FDN delays, vectors, RT60 maths, morph constants
    FdnCore.h               the 8x8 network itself
    StereoFdn.h             injection, readout and the input diffuser
    MorphingStereoFdn.h     dual-bank Size changes
Tests/                      offline measurement harness, no JUCE dependency
External/JUCE               JUCE 8.0.15 checkout (git-ignored)
```

---

## DSP core (PHASE 2)

Everything under `Source/DSP` is header-only, free of JUCE, and exercised by
`Tests` before it is allowed near the audio path. Three decisions are worth
reading the source comments for, because they were made against measurements
rather than convention:

**Interpolation.** 3rd-order Lagrange is the default, not Catmull-Rom. Hermite
was the original choice for its C1 continuity across integer boundaries — the
property a moving delay tap seems to want, since the slope of a delay is a pitch
shift. Sweeping a tap across eight knots with a 5 kHz source showed Lagrange
ahead on both accuracy (-53.5 vs -50.5 dB RMS) and error curvature (0.0017 vs
0.0021), while also being cheaper. Hermite stays available.

**Smoothing.** One law does not fit every parameter. Levels use a fixed-duration
linear ramp; delay times use a one-pole, because a ramp's corners are heard as
two ticks of detune on a moving tap (measured: 500x more curvature than the
one-pole); frequencies and durations use a log-domain glide at control rate, so
equal time means equal ratio and a 100 Hz → 10 kHz sweep passes through 1 kHz at
the halfway point rather than 5 kHz.

**Modulation depth.** `ModulatedDelay::peakPitchDeviationCents()` converts depth
and rate into the detune they actually produce, so the algorithms in PHASE 9
onwards can budget against a number. The prediction matches measurement to
within 0.01 %.

---

## Diffusion network (PHASE 3)

`AllpassDiffuser` → `DiffusionStage` → `DiffusionNetwork` → `StereoDiffusionNetwork`.
Four findings drove the design, each of them against an initial assumption:

**The loop delay is rounded to a whole sample.** Reading a Schroeder allpass at
a fractional position puts an interpolation filter inside the recursion, which
destroys the allpass property outright — measured at M = 100.37, g = 0.7 the
impulse loses 21.8 % of its energy and the response notches by 38 dB. Rounding
restores it exactly (energy error 1.1e-16, response flat to 3.3e-13 dB). A
one-sample delay step moves the output by exactly the signal's own
sample-to-sample difference, so a rounded glide is click-free by construction.

**Metallic ringing is not visible in the magnitude spectrum.** With rounded
delays the network is exactly allpass, so spectral flatness is 1.00000 no matter
how periodic the echo pattern is. The metrics that do discriminate are energy
concentration and envelope periodicity:

| delay set | kurtosis | periodicity | flatness |
|---|---|---|---|
| chosen | 35.1 | 26.7 | 1.00000 |
| harmonic (2, 4, 6 … ms) | 1343.2 | 158.6 | 0.99999 |
| eight identical (5 ms) | 6319.6 | 296.4 | 1.00000 |

**The Diffusion mapping is linear.** A sub-linear power law was expected to be
needed. Measuring density at 0/25/50/75/100 % for exponents 1.0, 0.7, 0.5 and
0.35 showed the plain linear map has the most evenly spaced steps, and two
independent density metrics agree on it.

**Shared stereo stages do not behave monotonically.** Sharing more leading
stages was expected to trade spread for mono compatibility smoothly. Measured,
the worst third-octave downmix dip goes 5.08 → 5.08 → 17.17 → 17.04 → 3.19 dB
for 0…4 shared stages. Which delays are left differing matters more than how
many.

Measured behaviour at the shipped tuning (six stages, Diffusion 0.8, mono in):
L/R correlation **0.008**, mono downmix level **-3.07 dB**, worst third-octave
downmix dip **5.08 dB**, each channel individually flat to **±0.000 dB**.

CPU, Release/x64, share of one core for one instance:

| configuration | 48 kHz | 96 kHz |
|---|---|---|
| 2-stage mono | 0.055 % | 0.119 % |
| 4-stage mono | 0.122 % | 0.470 % |
| 8-stage mono | 0.228 % | 1.166 % |
| 4-stage stereo | 0.263 % | 1.362 % |
| 8-stage stereo | 0.477 % | 1.791 % |

**Denormal guard.** The additive guard alternates sign every sample, so its
long-term mean is exactly zero and a recursion cannot integrate it into a DC
offset. A fixed-sign guard settles a g = 0.8 loop at +5e-25; the alternating one
leaves more than a thousand times less behind, at -500 dBFS.

---

## Feedback delay network (PHASE 4)

`FeedbackMatrix` → `FdnDelayLine` → `FdnCore` → `StereoFdn` → `MorphingStereoFdn`.

**Decay.** A signal in line *i* passes through it once every *d_i* seconds and is
multiplied by *g_i* each time, so `g_i = 10^(-3·d_i/RT60)` puts every line at
−60 dB simultaneously. Measured against the target from 0.1 s to 100 s, the
worst error is a few percent, and T20 agrees with T30 throughout.

**Stability.** With an orthogonal matrix the loop operator's 2-norm is exactly
`max(g_i)`, verified numerically by power iteration rather than asserted. Every
decay setting keeps it strictly below 1.

**Integer delays.** No fractional interpolation anywhere in the loop. PHASE 3
measured what an interpolator does inside a feedback path — 21.8 % energy lost,
38 dB notches — and inside an FDN it would additionally make the decay
frequency-dependent, which is a damping filter nobody asked for and would
corrupt the RT60 measurements this phase exists to establish.

**Stereo geometry.** Four unit-norm sign vectors chosen by measurement. The
first attempt required each readout to be orthogonal to its own injection; that
made the left output blind to the left input and collapsed hard-panned
separation below 1 dB. Same-side overlap carries the image; the *cross* terms
are what must be orthogonal. The measured ceiling on separation across every
valid quadruple is 1.40 dB — a shared tank gives channel identity as a bias, not
as isolation.

**Size morphing.** Two independent banks; only their outputs are crossfaded,
never their feedback loops. A fresh bank is empty, so fading straight into it
dropped the level 9.8 dB. Fixed priming times do not fix it either, because the
fill rate depends on the decay:

| priming | level variation |
|---|---|
| none | 9.76 dB |
| fixed 120 ms | 5.98 dB |
| fixed 500 ms | 3.62 dB |
| level trigger at 75 % | reaches the steady difference |

So the fade is triggered by level: the two banks' envelopes are tracked and the
crossfade starts once the incoming one has caught up. What remains is not an
artefact — at a fixed RT60 a larger tank is genuinely quieter (measured −2.86 dB
from Size 1.0 to 1.8), which is what a bigger room with the same decay time
does. The transition never drops more than 0.64 dB below either steady state.

---

## Tank matrix modulation (PHASE 5A)

`GivensRotation` → `ModulatedFeedbackMatrix`, replacing the static matrix inside
`FdnCore`. The delay lengths never change; what moves is how energy is shared
between the lines.

**A(t) = R₁(t)·R₂(t)·…·R_k(t)·H.** Every factor is a Givens rotation, orthogonal
for any angle, so the product is orthogonal by construction rather than by
maintenance. The coefficients are rebuilt from the current angles every update,
never nudged from their previous values, so there is nothing to drift. Measured
over 60 s at maximum depth and rate with eight rotations: worst |AᵀA − I| entry
**1.3e-15**, row and column norms exact, |det| exact, condition number 1.0, and
no energy change over two million applications.

**The honest result.** Matrix modulation at inaudible depths does very little:

| depth | angle | modal persistence | AM depth | pitch |
|---|---|---|---|---|
| 0.00 | 0° | 0.7944 | 0.03 dB | 0.00 ct |
| 0.25 | 5° | 0.7889 | 0.69 dB | 0.03 ct |
| 0.50 | 10° | 0.7806 | 1.31 dB | 0.06 ct |
| 1.00 | 20° | 0.6917 | 2.50 dB | 0.13 ct |

Persistence only moves meaningfully at 20°, and by then a sustained tone carries
2.5 dB of level wobble. Pitch stays at a tenth of a cent throughout, as expected
— nothing changes a delay length — so what this modulation costs is amplitude,
not detune. Rate is bounded by spectral concentration: clean to 0.4 Hz, 5 dB
down by 0.8 Hz, and 33 dB down by 1.2 Hz, which is chorus rather than movement.

RT60 is untouched at every setting (within 0.4 %) and long-run energy drift stays
under 0.05 dB when measured across whole modulation cycles.

**Capacity.** `setDelayPattern` and `setSizeScale` return a `ConfigResult` and
refuse anything beyond what `prepare()` reserved, leaving the previous
configuration intact. This closed the PHASE 3 limitation, and immediately caught
a real bug in the test suite: a control delay set that exceeded capacity was
being refused, so the "harmonic" comparison was silently measuring the shipped
set twice.

---

## Tank allpass phase modulation (PHASE 5B)

`TimeVaryingAllpass` → `AllpassPhaseBank`, inserted into the `FdnCore` line-state
path. The signal order inside the loop is now

```
integer FDN delay read → short time-varying allpass → damping slot (inactive)
                       → per-line decay gain → orthogonal matrix → delay write
```

The integer FDN delay lengths are untouched. What moves is the phase of each line.

### The energy-preserving realisation

A Schroeder allpass modulated by rewriting `g` in the direct form is not
energy-preserving while `g` is moving: the stored state was written under the old
coefficient and is read back under the new one, and the mismatch is real energy
appearing or disappearing inside a feedback loop.

The same transfer function factors into a plane rotation. With `c² + s² = 1`:

```
y = −s·x + c·d        d = delay output
w =  c·x + s·d        w = delay input
```

Substituting `W = cX + s·z⁻ᴹW` gives `W = cX/(1 − s·z⁻ᴹ)` and
`Y = (−s + z⁻ᴹ)·X/(1 − s·z⁻ᴹ)` — the Schroeder allpass with `g = s`. But
`(x,d) → (y,w)` is a rotation of a plane, so `x² + d² = y² + w²` holds **exactly,
per sample, for any coefficient trajectory whatsoever**. Energy preservation stops
being a property of slow modulation and becomes a property of the structure.

Measured worst per-sample energy residual with a fresh random coefficient every
single sample: **1.3e-15**. Through a 40 Hz coefficient sweep the rotation form
passes **−0.001 dB**; the direct-form reference passes **+0.041 dB**.

### Candidates

| candidate | persistence | periodicity | movement | RT60 | drift | pitch |
|---|---|---|---|---|---|---|
| PHASE 4 static | 0.7944 | 43.2 | 0.078 | 1.9924 | −0.024 dB | 0.00 ct |
| PHASE 5A matrix | 0.7528 | 45.2 | 0.058 | 1.9851 | −0.029 dB | 0.19 ct |
| **A — energy-preserving** | **0.6556** | **20.30** | **0.106** | 2.0338 | 0.003 dB | 0.12 ct |
| B — Thiran | 0.7333 | 21.37 | 0.073 | 1.9925 | −0.030 dB | 0.02 ct |
| C — Schroeder reference | 0.6556 | 20.27 | 0.106 | 2.0338 | 0.002 dB | 0.12 ct |

Candidate A is the production candidate: it meets both modal targets
(persistence ≤ 0.70, periodicity ≤ 30) and puts movement 1.8× above matrix
modulation, at an eighth of a cent of pitch.

**Candidate C is reported honestly rather than patched.** At the shipped 0.20 Hz
it is indistinguishable from candidate A — its energy error only becomes
measurable when the coefficient moves fast. No gain normalisation was bolted onto
it; a reference with a correction fitted is no longer a reference. What the
comparison actually shows is that the rotation form's advantage at reverb
modulation rates is *structural and guaranteed*, not large. It matters because it
holds under automation, Size morphs and any future rate, not because it sounds
different at the default.

Candidate B is genuinely gentler — a first-order Thiran section is a fraction of a
sample of delay, not a full allpass loop, so it barely reaches the modal targets.

### The blind-spot finding

The pitch estimator sweep initially probed a 1 ms section with a 1 kHz tone and
measured **0.000 cents at every depth** while still passing, because zero
satisfies an upper bound.

For a section of `M` samples, `H(z) = (−g + z⁻ᴹ)/(1 − g·z⁻ᴹ)`. At `Mω = 0` this is
`(1−g)/(1−g) = 1` and at `Mω = π` it is `−(1+g)/(1+g) = −1`. At every frequency
`f = k·fs/(2M)` the section is **exactly transparent to its own coefficient** — the
numerator and denominator carry the same `g`-dependence and it cancels. A 1 ms
section at 48 kHz has `M = 48`, so 1 kHz is precisely such a blind spot.

Measured, at swing 0.30 and 1 Hz:

| probe | Mω | measured |
|---|---|---|
| fs/(4M) = 250 Hz | π/2 | 3.430 ct |
| fs/(2M) = 500 Hz | π | 0.000 ct |
| 3fs/(4M) = 750 Hz | 3π/2 | 1.142 ct |
| fs/M = 1000 Hz | 2π | 0.000 ct |

The sweep is now anchored to the section length and probes at `fs/(4M)`, where
sensitivity is greatest, and asserts a **non-zero** measurement as well as the
bound. The blind-spot table stays in the suite so the trap cannot come back.

PHASE 2's `peakPitchDeviationCents()` was checked for reuse and rejected: it
models a directly modulated delay length, whereas here a coefficient moves and the
delay excursion is a nonlinear function of it. The separate estimator uses the
group delay `τ(ω) = M(1−g²)/(1 + g² − 2g·cos Mω)`, whose `g`-sensitivity peaks at
DC at `2M/(1−g)²`. That makes it a genuine upper bound but a conservative one —
between 3× and 9× above measurement — which is the safe direction for a budget
check. The shipped default predicts **1.41 ct** against a 3 ct subtle budget, and
measures **0.12 ct** in the tank.

### Chosen defaults, and why

- **Depth 0.35** (coefficient swing 0.105). Depth 0.10 already meets both modal
  targets, but its movement of 0.062 is barely above matrix modulation's 0.058.
  0.35 is the lowest sweep point that puts movement *clearly* above it (0.106,
  1.8×) while leaving spectral concentration untouched at 70.3 dB against the
  static tank's 69.9 dB.
- **Rate 0.20 Hz.** Concentration is intact through 0.40 Hz, drops to 57.7 dB at
  0.40 and collapses to 22.2 dB by 0.80 Hz — that is chorus, not movement.
- **8 lines, 1 section.** 2 lines is ineffective (persistence 0.7694, periodicity
  44.1 — essentially the static tank). Cascading two sections buys periodicity
  (15.39) but costs persistence (0.6833), triples pitch (0.40 ct) and pushes RT60
  to 2.0846 s, 4.2 % off target and outside the 3 % band the single section holds.

RT60 sits ~1.7 % above target at every allpass setting: each section adds its own
group delay to the loop, lengthening the effective delay at unchanged feedback
gain. This is arithmetic, not drift, and it stays inside the 3 % band.

**Depth 0 is a real bypass**, not a zero-amplitude modulation: the bank reports
itself bypassed, the LFO lanes advance by `skipSample()` so re-engaging never
jumps in phase, and the output is **bit-identical** to the PHASE 4 static FDN.

### Cost

At 48 kHz, worst case, measured in the same run as the PHASE 5A figures:

| configuration | ns/sample | of one core |
|---|---|---|
| static FDN | 123.1 | 0.591 % |
| PHASE 5A matrix, shipped | 218.8 | 1.050 % |
| allpass, 8 lines × 1 (shipped) | 249.7 | 1.198 % |
| allpass, 8 × 2, max rate/depth | 288.9 | 1.387 % |
| dual-bank morph + allpass | 276.2 | 1.326 % |

Reducing the active line count barely reduces cost — 2 lines is within a few per
cent of 8 — because `advance()` ticks all eight LFO lanes every sample regardless
of how many lines are engaged, so that a lane which stopped and restarted could
not jump in phase. The active line count is a voicing control, not a CPU control.

### Preview

`-DAV_DEV_DIFFUSION_PREVIEW=ON -DAV_DEV_ALLPASS_CANDIDATE=n` selects what the
preview build's Mod Depth control drives: `0` matrix modulation only (PHASE 5A),
`1` energy-preserving, `2` Thiran, `3` Schroeder reference. Exactly one engine is
driven at a time — deciding how they combine is PHASE 5C's job, and a preview that
already mixed them would prejudge it. All four variants build with 0 errors and
0 warnings and pass `pluginval` at strictness 10 — including candidate 3, the
reference that is known to gain energy under fast coefficient movement — and the
default build is unaffected by any of them.

---

## Final modulation, RT60 correction and Size compensation (PHASE 5C)

Three questions closed here: which modulation architecture the reverb uses, why
its decay came out long, and why Size doubled as a volume control.

### Choosing the architecture

Seven candidates, measured identically: static, matrix-only (PHASE 5A as
shipped), allpass-only (PHASE 5B), three hybrids at 3°, 5° and 7° of matrix on
top of the allpass engine, and Thiran-only.

| candidate | persist | period | lifetime | moveHz | fast AM | pitch | score |
|---|---|---|---|---|---|---|---|
| A static | 0.7944 | 43.21 | 1.254 s | 0.078 | 0.00 dB | 0.023 ct | 0.398 |
| B matrix-only | 0.6417 | 42.13 | 0.840 s | 0.153 | 0.23 dB | 0.291 ct | 0.661 |
| **C allpass-only** | **0.6583** | **20.45** | **0.874 s** | **0.106** | **0.04 dB** | **0.145 ct** | **0.681** |
| D light hybrid 3° | 0.6250 | 20.13 | 0.812 s | 0.112 | 0.16 dB | 0.309 ct | 0.669 |
| E medium hybrid 5° | 0.6111 | 19.46 | 0.796 s | 0.132 | 0.41 dB | 0.451 ct | 0.654 |
| F hybrid 7° | 0.5944 | 19.69 | 0.776 s | 0.162 | 0.94 dB | 0.633 ct | 0.600 |
| G Thiran-only | 0.7167 | 32.09 | 1.008 s | 0.083 | 0.01 dB | 0.053 ct | 0.559 |

**Allpass-only is the default.** Every candidate clears the hard constraints, so
the choice came down to the ranking, and two things had to be got right before
the ranking meant anything.

**The single AM figure was misleading.** Splitting amplitude modulation by rate
changed the picture completely: allpass-only's 4.46 dB is almost entirely below
1 Hz (5.01 dB slow against 0.04 dB fast, dominant component at 0.37 Hz), which is
a slow swell rather than a tremolo. Matrix-only's total is lower at 2.53 dB but
carries six times as much of it above 1 Hz. Ranking on the combined number
penalises whichever engine happens to modulate more slowly, for a reason that has
nothing to do with how it sounds.

**The weighting is stated, not buried.** Objectives are grouped by phenomenon —
modal stasis, echo periodicity, spectral change — because persistence and peak
lifetime are two views of the same thing and counting them separately weights it
twice. The winner is then reported as a function of how much echo periodicity
carries:

| echo weight | winner |
|---|---|
| 0.10, 0.20 | B matrix-only |
| 0.33 and above | C allpass-only |

Matrix-only wins only where echo periodicity counts for less than a fifth of
movement quality. It is not less: late-tail periodicity is the metallic-ringing
metric, and PHASE 3 established that it is invisible to every magnitude-domain
measure because an allpass cascade is flat by construction. A tank whose modes
drift but whose echo pattern still repeats on a fixed interval still sounds
metallic. Matrix-only leaves it at 42 against the static tank's 43; allpass-only
halves it.

No candidate is dominated by another on all eight raw objectives — the whole set
is on the Pareto front — so the ranking is a judgement, and it is recorded with
the evidence that would overturn it.

### What the matrix layer contributes to a hybrid

| hybrid | persistence gain | periodicity gain | fast AM cost |
|---|---|---|---|
| D 3° | 22.4 % | 4.9 % | +0.13 dB |
| E 5° | 38.8 % | 8.5 % | +0.37 dB |
| F 7° | 40.8 % | 8.6 % | +0.75 dB |

Percentages are of what the allpass engine bought over the static tank. Stated
precisely: the matrix layer **does** improve modal persistence meaningfully. It
does **not** improve echo periodicity. So it is not useless, and the removal rule
does not fire on its own terms — what decides it is that the persistence it buys
does not cover the amplitude modulation, pitch and concentration it spends, and
that the two engines are redundant exactly where a listener hears the problem.

Matrix modulation stays in the codebase, fully tested and reachable, as an
optional character component for a later era or algorithm.

### Two corrections to earlier reasoning

**PHASE 5A said matrix modulation "costs amplitude, not detune."** The first half
holds. The second does not. Rotating energy between lines of different lengths
changes the effective group delay of the composite path a signal takes, and a
changing effective delay is frequency modulation whether or not any single delay
line moved. Measured with the analytic signal, matrix-only produces **more**
frequency modulation than the allpass engine — 0.188 against 0.145 cents RMS.
Both are tiny; what changes is that there is no pitch-budget argument for
preferring matrix modulation.

**The sideband analysis was contaminated.** Sideband *order* is only defined for
one modulation rate, and the bank runs eight lanes at eight rates, so energy at
2 × 0.20 Hz may be another lane's first-order sideband. Measured strictly, with
one lane engaged, first-order dominates as expected. Measured for the shipped
eight-lane bank by distance from the carrier instead — which needs no
single-rate assumption — 88.75 % stays on the carrier, 11.17 % lands within one
lane rate, and 0.07 % goes further. The "9.34 % higher-order" the first version
reported was entirely the other seven lanes.

The PHASE 5B blind-frequency finding is kept and extended to a six-point grid.
Blind points still read exactly zero, as the algebra requires, and non-blind
points read up to 6.216 cents at the research maximum.

### RT60 loop-delay correction

PHASE 5B's decay ran about 1.7 % long. Not drift — the allpass sections sit
inside the feedback path, so the loop is longer than its delay line and the gain
was being computed for the wrong length.

The extra length is exactly the section's **pole count**, because the group delay
of a stable allpass integrates over frequency to its number of poles. That makes
the correction independent of the coefficient, and therefore of modulation depth
— verified directly: 29.0000 samples added on line 0 at depth 0.10, 0.35, 0.70
and 1.00 alike. Correcting from the DC group delay instead would have been
depth-dependent and frequency-blind; τ(0) = M(1+g)/(1−g) is the maximum over
frequency, not the average.

| target | no correction | analytic | calibrated scalar |
|---|---|---|---|
| 0.3 s | +3.21 % | +1.30 % | +1.55 % |
| 0.5 s | +1.77 % | −0.40 % | −0.16 % |
| 1.0 s | +2.29 % | +0.14 % | +0.36 % |
| 2.0 s | +1.93 % | −0.09 % | +0.07 % |
| 4.0 s | +1.93 % | −0.20 % | −0.01 % |
| 10.0 s | +1.70 % | −0.31 % | −0.13 % |

A calibrated scalar matches it, and a LUT would too — but both need re-measuring
whenever the section lengths, the sample rate or the Size range change, and the
analytic model needs no calibration data at all. Worst error across the full
8 × 5 decay-by-depth matrix (0.3 s to 100 s, depth 0 to 1) is under 5 %, with
decay linearity above 0.98 everywhere.

It does not buy the broadband figure by pulling the bands apart. Low/mid/high
RT60 spread is 0.236 s on the static tank, 0.081 s corrected at the default
depth, and 0.021 s at full depth — the modulation narrows it.

### Size level compensation

A larger tank at fixed RT60 is genuinely quieter (PHASE 4). The closed form says
level ∝ √(RT60/d), so the correction should be −10 log10(s) and, because the RT60
term cancels out of the ratio, should not depend on Decay at all.

**Measurement said otherwise.** The linearisation of 1 − g² stops holding at
short decays, where g moves well away from 1. The correction Size 0.25 needs
moves by **2.46 dB** between a half-second decay and a ten-second one — five
times the 0.5 dB threshold for keeping the table one-dimensional. So the shipped
table is a 9 × 5 surface in Size and Decay, bilinear in log on both axes.

A second dependence: engaging the allpass bank shifts the correction by up to
0.58 dB, and the shift is on Size, not depth — depth 0.35 and 1.00 agree to
0.06 dB. That is the loop-delay model again: a fixed sample count added to every
loop is a larger proportion of a short line. It is modelled as one additive term
switched by engagement, not as a third table axis.

| | uncompensated | compensated |
|---|---|---|
| Size 0.25 | −20.57 dB | −26.51 dB |
| Size 1.00 | −26.76 dB | −26.78 dB |
| Size 2.00 | −30.40 dB | −26.70 dB |
| **spread** | **9.83 dB** | **0.27 dB** |

Applied outside the feedback loop, after the stereo readout, before the wet
output. Nothing touches a feedback gain, the tank state, or the injection and
readout vectors. Bounded at ±12 dB; the real range asks for at most 6.0 dB.

**Crossfade ordering was measured, not assumed.** During a morph the two banks
are different sizes and need different gains:

| ordering | worst transient excursion |
|---|---|
| A per-bank correction, then crossfade | 0.62 dB |
| B crossfade, then target correction | 6.99 dB |

A is shipped. B saves one multiply per sample and applies the incoming Size's
correction to a sum that is still mostly the outgoing bank.

### Preview

Functional order, as the user sees it:

```
input → short stereo input diffusion → 8×8 FDN → selected modulation
      → stereo readout → Size level compensation → safe wet output
```

The allpass modulation is not literally a stage after the FDN — it lives inside
the line-state feedback path where PHASE 5B put it.

Parameter mappings live in `ParameterMapping.h`, JUCE-free and testable. Each
curve is chosen so the control's **existing default position** produces the
setting the measurements chose: Mod Depth is `u^0.8719`, which sends the 30 %
default to depth 0.35 while still reaching the full research range at 100 %; Mod
Rate is a scale on the measured default rather than a raw frequency, clamped to
the 0.02–2.0 Hz the engine was measured over; Size is geometric, so equal travel
is equal ratio.

**Gain staging, not limiting.** Sustained full-scale noise at a 20 s decay
reached a peak of 2.18 with the old fixed −12 dB trim, and full-scale DC reached
8.01 — an 18 dB span no fixed trim covers without making normal settings far too
quiet. The trim is now the reciprocal square root of the tank's predicted
steady-state build-up, normalised to unity at the reference setting, so Decay
controls decay rather than doubling as a volume control. It depends only on
parameters, never on the signal, so it is exactly reversible and cannot hide a
peak.

| case | headroom |
|---|---|
| full-scale impulse | 47.25 dB |
| impulse, 60 s decay | 51.84 dB |
| sustained noise, default decay | 4.50 dB |
| sustained noise, 20 s decay | 2.88 dB |
| sustained noise, largest Size | −1.13 dB |
| full-scale DC | −8.89 dB |

The last two still exceed full scale. They are stress cases rather than signals —
a reverb integrates energy by definition — and they are reported rather than
flattened. There is no limiter anywhere in this path.

### Cost

At 48 kHz, worst case, all measured in one run:

| configuration | ns/sample | of one core |
|---|---|---|
| static FDN | 124.7 | 0.599 % |
| matrix-only | 229.6 | 1.102 % |
| **allpass-only (shipped)** | **241.9** | **1.161 %** |
| allpass-only + Size compensation | 255.7 | 1.227 % |
| light hybrid | 349.5 | 1.677 % |
| dual-bank morph + modulation + compensation | 290.4 | 1.394 % |

The shipped architecture is 1.89× the static tank; Size compensation is inside
the run-to-run noise. The worst configuration anywhere in the matrix — morph plus
modulation plus compensation at 192 kHz — is 8.73 % of one core.

CPU thresholds are asserted as a **ratio to the static tank measured in the same
run**, not as absolute nanoseconds. A stray build process tripled every figure in
one run of this suite, and an absolute threshold caught it as a DSP regression.

### Developer signal export

`Scripts\export-signals.ps1` renders nine deterministic sources — impulse, snare,
clap, piano chord, vocal, synth pad, pluck, noise burst, decorrelated stereo —
dry and wet at three presets, as 32-bit float WAV. Synthesised from source, so
the repository carries no audio and the renders are reproducible.

It lives only in the test target behind its own DLL entry point, never runs as
part of a test run, and adds no file-writing code to the VST3. This is not
diagnostic telemetry — nothing observes a running plugin — which is PHASE 5D.

---

## Diagnostic layer (PHASE 5D)

Observing the plugin in a real DAW without disturbing it. Compiled out entirely
by default: with `AV_DEV_DIAGNOSTICS` off there is no collector object, no
thread, no queue and no branch anywhere in the audio path — the cost is absent
rather than small.

### Extended Size calibration

The first thing this phase did was re-measure the PHASE 5C surface. It had been
calibrated over 0.5–10 s while the Decay parameter runs 0.1–60 s, so most of the
range was being clamped and its accuracy was an assumption.

Measuring the clamped region showed the extension had to go further than
planned: clamping 0.1 s to the 0.3 s row is wrong by **6.96 dB**, and 0.2 s by
1.70 dB. The grid now starts at the parameter's own minimum. At the top it stops
at 100 s, because the surface has stopped moving there — clamping 150 s costs
0.19 dB and 300 s costs 0.03 dB.

| decay | uncompensated spread | compensated spread | worst error |
|---|---|---|---|
| 0.1 s | 33.10 dB | 9.10 dB | **5.27 dB** |
| 0.2 s | 20.58 dB | 0.23 dB | 0.19 dB |
| 0.3 s | 16.52 dB | 0.22 dB | 0.16 dB |
| 0.5 s | 13.32 dB | 0.17 dB | 0.11 dB |
| 1 s | 10.92 dB | 0.10 dB | 0.06 dB |
| 2 s | 9.72 dB | 0.04 dB | 0.03 dB |
| 4 s | 9.13 dB | 0.09 dB | 0.08 dB |
| 10 s | 8.73 dB | 0.18 dB | 0.12 dB |
| 30 s | 8.64 dB | 0.32 dB | 0.20 dB |
| 100 s | 8.55 dB | 0.36 dB | 0.19 dB |

**The ±0.5 dB target is met from 0.2 s upwards — worst error 0.20 dB — and is
not met at 0.1 s.** That is reported rather than clamped away. The cause is not
the table, which matches measurement there; it is the 12 dB safety limit. At
0.1 s with Size 2.0 the tank asks for **+17.23 dB**, and applying that was
judged worse than not: the longest delay line at that setting is 166 ms, longer
than the whole RT60, so the tank has stopped circulating and what would be
boosted is a sparse near-dry response.

The choice was between a bounded compensation with a known 5.27 dB error in one
corner and an unbounded one that applies 17 dB of gain to an artefact. The
bounded one ships, `sizeComp::correctionIsClamped()` reports the corner, and the
diagnostic layer raises `uncalibrated_region` when a session enters it.

Interpolation between grid nodes is accurate to 0.202 dB; sample rate moves the
correction by under 0.5 dB across 44.1–192 kHz.

### What the audio thread is allowed to do

Update atomic counters, fill a preallocated struct, push to a lock-free queue,
copy samples into a preallocated ring, read a monotonic clock. Nothing else — no
allocation, no lock, no string, no file, no formatting.

**Snapshots reach the consumer through an SPSC queue and through nothing else.**
The audio thread owns its snapshot outright; no other thread reads it. At
control rate — every 10 ms — it copies that struct into a queue slot and
publishes the slot with a release store. The consumer pops slots it then owns
exclusively and keeps the newest in its own state. At no point do two threads
touch the same non-atomic bytes.

This replaced a sequence lock, and the reason is worth recording. The seqlock
published by bumping a counter around a plain struct copy and let the reader
retry when it saw the counter move. It measured 1.8 million consistent reads and
zero tears — and was still wrong. The payload copy was a non-atomic read racing
a non-atomic write, which is a data race, and a data race is undefined
behaviour: the standard lets the compiler assume it cannot happen and optimise
on that basis. A test that does not trigger it shows the optimiser has not yet
chosen to exploit it here, not that the code is correct. **Measurements cannot
establish an absence that the memory model does not grant.**

Ownership, stated plainly: `working` is audio-thread-only; each queue slot
belongs to the producer until its release store and to the consumer after its
acquire load; `latest` in the writer belongs to the writer thread. The watcher
and the report builder read only the writer's copy.

Overflow policy: the queue is 64 slots, and the producer publishes every 10 ms
against a consumer that polls every 100 ms, so it holds about half a second of
slack. When it is full the **newest** snapshot is dropped and counted — never
the oldest, because taking the oldest would mean the producer popping, and only
the consumer may pop or the single-producer/single-consumer argument collapses.
The producer returns immediately either way.

The drop count travels on a separate pair of atomic counters rather than inside
a snapshot. It has to: the counters exist to say "the queue overflowed", and the
queue is precisely the channel that cannot carry that message once it is full.
The first version had exactly that hole — a four-slot queue overflowed several
hundred times and reported a drop count of nought.

The SPSC queue leaves one slot empty so full and empty are distinguishable from
two indices, and pads them onto separate cache lines. A full queue refuses the
newest event and counts it; it never blocks and never drops the oldest, because
dropping the oldest means extra work exactly when the system is already
struggling. Measured: a rejected push on a full queue costs the same as a
successful one, and across 200 000 concurrent pushes every item was either
consumed or counted — none vanished, none arrived out of order.

### What the tools can and cannot show

`-DAV_TEST_ASAN=ON` builds the test target with AddressSanitizer, which is worth
running over hand-written index arithmetic in a ring buffer.

**ThreadSanitizer is not available for MSVC on Windows.** The tool that would
actually search for data races cannot be run here, and that is stated rather
than worked around. The argument for the snapshot transport's correctness is
therefore structural — no non-atomic object is reachable from two threads — and
not "a sanitizer did not complain". That is the stronger claim of the two in any
case: TSan only reports races it observes on the paths it happens to execute.

ASan installs its own allocator, so the allocation counter cannot be linked in
at the same time. In a sanitizer build the suites that assert "this allocated
nothing" report themselves as **not measured** instead of passing on a counter
that is guaranteed to stay at zero. The allocation figures quoted below come
from the ordinary build.

### Telemetry levels

| Level | Cost | Contents |
|---|---|---|
| 0 off | one relaxed atomic load, then return | nothing |
| 1 lightweight | control-rate snapshots | CPU, peaks, RMS, NaN/Inf, clip, denormal, block size, parameters, morph, compensation |
| 2 full | adds capture | input/output WAV, event history, stereo metrics, timelines |

Switching level allocates nothing — everything either level needs is reserved in
`prepare()`.

### Live versus offline

The audio callback does no FFT, no RT60, no modal tracking and no true peak.
Those are far too heavy for a real-time thread, and a version cheap enough to run
there would not be worth having. They happen afterwards, in `DiagnosticAnalyzer`,
over the captured WAV — **calling the same PHASE 3–5C functions the DSP phases
were validated with, not a second copy of any formula.**

The analyser refuses to measure what cannot be measured. RT60 from a sustained
pad is a confident number that means nothing, so a non-impulsive source reports
`not measurable from this source` and leaves the decay fields empty.

Measured through a real capture of the shipped chain, against the same figures
the offline suites produce directly:

| | through a capture |
|---|---|
| RT60 broadband | within 6 % of the requested decay |
| decay linearity R² | > 0.98 |
| late-tail periodicity | ≤ 30 |
| modal persistence | ≤ 0.75 |
| worst third-octave dip | ≤ 4 dB |
| true peak, full-scale impulse | ≤ 1.0 |

### Session output

```
AuroraVerbDiagnostics/YYYY-MM-DD_HH-mm-ss_instance-id/
├── session.json     versioned schema, locale-independent decimals
├── summary.txt      the same for a human, ending in a verdict
├── timeline.csv     one row per poll — what the live monitor reads
├── events.jsonl     one JSON object per line, with timestamps
├── input.wav        32-bit float
├── output.wav
├── analysis.json    written later by the session analyser
├── analysis.txt
└── environment.txt
```

The root comes from `AV_DIAGNOSTIC_ROOT`, then `USERPROFILE`, then `HOME` — never
a hard-coded path or user name — and is printed in every report. Writability is
proved by writing a probe file, not by inspecting permissions, so a read-only
location or a full disk fails at `start()` rather than halfway through a session.
A session that cannot start is refused cleanly and the plugin carries on.

### Verdict rules

Every threshold lives in `av::diag::thresholds` with the reason it has its value.

**FAIL**: any NaN, any Inf, callbacks that ran with no audio observed.

**WARNING**: output over full scale, clipped samples, raw internal peak over
unity, CPU p99 over 20 % of the block budget, deadline exceeded, event queue
overflow, capture overflow, DC above 1 % of RMS, subnormals, more than a quarter
of morphs timing out, decay outside the calibrated region.

The verdict is technical. Nothing decides whether the reverb sounds good.

Two warnings are expected and are not faults: `raw_internal_over_unity` reports
the 2.79 peak PHASE 5C measured on full-scale DC, and `uncalibrated_region`
reports the 0.1 s corner above. Both exist because there is no limiter anywhere
in this path — hiding them would make every measurement describe the limiter.

### Tools

```powershell
.\Scripts\prepare-fl-test.ps1            # render the nine test sources + reference
.\Scripts\start-fl-diagnostic.ps1        # find FL Studio, start the monitor, launch
.\Scripts\watch-diagnostics.ps1          # live terminal view
.\Scripts\analyze-diagnostic-session.ps1 # offline analysis, exit code 0/1/2/3
```

`start-fl-diagnostic.ps1` searches the registry and every fixed drive rather than
one hard-coded path, launches with ordinary user rights, and does not modify,
patch or automate FL Studio in any way. Scanning, inserting and listening are the
user's steps — a script that drove the host would be testing the script.

The watcher reads files, never the plugin, and follows `timeline.csv`
incrementally rather than re-reading it each tick.

### Cost

The question this has to answer is whether the instrumentation changes what it
measures. Measured against the same DSP with telemetry off, at 48/96/192 kHz:
lightweight telemetry stays under 10 % of the DSP cost, and full telemetry with
capture under 35 %. The timer's own cost is measured separately against an empty
loop, and reported.

---

## Engineering rules

These hold for every module added from PHASE 2 onwards:

1. No heap allocation, locking, or I/O on the audio thread. Buffers are sized in
   `prepare()` and only reused in `process()`.
2. Internal processing is `double` unless a module documents why `float` is
   sufficient there.
3. Times are specified in seconds and converted in `prepare()`. Hard-coded delay
   lengths are authored at 48 kHz and scaled via `av::scaleToRate`, so the
   reverb character survives a sample-rate change.
4. Every feedback path is denormal-protected.
5. Every DSP module is independently constructible and testable from the
   `Tests` target. Nothing under `Source/DSP` may include JUCE or depend on
   `AuroraVerbProcessor`; the test target does not link JUCE, so a violation
   fails the build rather than going unnoticed.
6. Parameter IDs are permanent. Append only, and bump `av::params::versionHint`.
7. A DSP choice that can be measured is measured. Where a comment claims one
   approach beats another, there is a test printing the numbers behind it.

---

## Parameters

| Parameter | Range | Default |
|---|---|---|
| Mode | 13 algorithms | Large Hall |
| Color | Classic / Retro / Modern | Classic |
| Pre-Delay | 0 – 500 ms | 0 ms |
| Decay | 0.1 – 60 s | 2.5 s |
| Size | 0 – 100 % | 60 % |
| Early Diffusion | 0 – 100 % | 70 % |
| Late Diffusion | 0 – 100 % | 70 % |
| Density | 0 – 100 % | 70 % |
| Attack | 0 – 100 % | 0 % |
| Mod Rate | 0.01 – 10 Hz | 0.5 Hz |
| Mod Depth | 0 – 100 % | 30 % |
| Low Damp | 0 – 100 % | 20 % |
| High Damp | 0 – 100 % | 50 % |
| Low Damp Freq | 20 – 1000 Hz | 200 Hz |
| High Damp Freq | 500 Hz – 20 kHz | 6 kHz |
| Width | 0 – 200 % | 100 % |
| Low Cut | 20 – 1000 Hz | 20 Hz |
| High Cut | 500 Hz – 20 kHz | 20 kHz |
| Mix | 0 – 100 % | 30 % |

Parameters are intentionally allowed to mean different things per algorithm —
`Attack` becomes gate/reverse length in the Nonlinear family, `Density` is a
primary control on Plate and a secondary one on Room, and so on.

---

## Aurora Sound — voicing and character (PHASE 6)

### FU-1 — modal / bell-like tonal character: **COMPLETE**

**Verdict: CHARACTER ACCEPTABLE.** An audible metallic / bell-like character
exists in the tank. The evidence does not show it to be an objectionable voicing
defect in shipping use, so **no production DSP change was made under FU-1**: the
modal response is not flattened, no mode is targeted, Mod Depth is unchanged,
and the Size-based localisation experiment will not be run.

What was built and kept, in the test harness only — nothing in `Source/` was
touched:

| | |
|---|---|
| Full-spectrum modal map | 60 detailed modes, 2814 peaks above a 10 dB prominence floor |
| Size / Decay / Mod Depth sweeps | the seven planned measurements, five delivered in full and two in part |
| q-space Size tracker | replaced a tracker that lost mode identity on the Size axis; validated synthetically, cross-validated leave-one-out and against fresh off-grid renders |
| Modal decay estimator | sliding-window exact-frequency DFT with a contamination guard |
| A/B/C/D structural classification | rule unchanged; regenerated from corrected Size evidence |
| Four blind listening rounds | 1, 1b, 1c, 2, 2b, 2c — pre-registered analysis, blind keys, content-free render checks |

Two findings recorded rather than worked around: **per-mode modal decay is
infeasible for 60 of 60 modes** at the tank's modal density, which is a physical
identifiability limit; and **class C is empty** — nothing measures as
size-insensitive once the Size axis is tracked correctly.

The modal map, the tracker and the v4 mode table are kept as a **regression
baseline**, not a to-do list. **The high-band character should be re-checked in
the damping phase**, which acts directly on the region these rounds probed; the
nine Round 2c renders are the ready-made comparison set.

Detail: [`Docs/Phase6-FU1-AnalysisPlan.md`](Docs/Phase6-FU1-AnalysisPlan.md) ·
[`Docs/Phase6-FU1-Interpretation.md`](Docs/Phase6-FU1-Interpretation.md) ·
[`Docs/Phase6-FU1-Listening.md`](Docs/Phase6-FU1-Listening.md).

**Known limitation, carried:** all listening used synthetic sources, and the most
metallic file in the final round was a dry source with no reverb in it. FU-1 was
never validated against real recordings.

---

## Shipping / build hardening — **COMPLETE**

The reverb engine used to be switched on by a development flag that was off by
default, whose value came from an existing CMake cache rather than from the
build script. Every artefact validated in a host had it on; a default build
produced a plugin with no reverb in it and said nothing. The flag is gone, the
engine is part of every build, and each identity-deciding flag is now stated
explicitly on every configure.

Builds go through `Scripts/build.ps1 -Profile production|development|validation`
or the matching `CMakePresets.json` preset. No build installs anything as a side
effect; installing is a separate gated step,
`Scripts/install-production-vst3.ps1`, run from an elevated shell.

| | |
|---|---|
| Installed production artefact | `2B451890E229F7FDC4A202898AB0384BCAAEF88E2A877FD520D5464DC9E02BD8` |
| Rollback archive, do not re-install | `A49D477DD353058287FA7FC75D4422E865497A91FC4051EB84D5E6A395622464` |
| FL Studio host regression | PASS, eight checks |

A production build carries no telemetry code at all, so it reports no runtime
`build_profile`. That is the design, not a fault: its identity comes from the
preset, the configure flags and the artefact hash.

Detail, including the rollback procedure:
[`Docs/ShippingBuildHardening.md`](Docs/ShippingBuildHardening.md).

---

## Roadmap

> **Numbering, resolved.** This list once placed *early reflections* at 6, which
> collided with the voicing and character work already documented as **Phase 6 —
> Aurora Sound**. Resolved in favour of the two anchors that were already load
> bearing: Phase 6 is Aurora Sound, which is baked into three document
> filenames, and Phase 7 is damping, which is carved into `FdnCore.h` as a
> reserved processing seam. Early reflections gave up the number — it had a
> roadmap slot and no code — and moved to the unnumbered backlog below. Nothing
> from 8 onwards shifted.
>
> **Rule from here on:** a phase number is assigned when the work *starts*, never
> reserved in advance. Reserving 6 for work that never began is what produced
> the collision.

Phase 1 skeleton · 2 DSP core · 3 diffusion network · 4 FDN ·
5A matrix modulation · 5B allpass phase modulation ·
5C final modulation, RT60 correction, Size compensation ·
5D diagnostic layer and FL Studio test infrastructure ·
5F host regression and gain staging ·
**6 Aurora Sound — voicing and character** · **7 damping** ·
8 Large Hall · 9 Room ·
10 Plate · 11 Chamber · 12 Random/Chorus Space · 13 Ambient/Cathedral ·
14 Nonlinear · 15 Chaotic · 16 Color engine · 17 Stereo engine ·
18 Presets · 19 GUI · 20 CPU optimisation · 21 Automated DSP testing ·
22 Final VST3 build.

### Upcoming DSP work / backlog — unnumbered

Real work with no phase number yet. Each takes the next free number on the day
it starts.

| | |
|---|---|
| **Early reflections** | Had the number 6 and never had any code. A distinct early-reflection stage with its own early/late balance. Required for v1.0. |
| Harness / plugin Early Diffusion parity off-default | Equivalent at shipping defaults; diverges only away from them. Not a blocker. |

Scope and priority for both, and for everything else v1.0 needs, are tracked in
[`Docs/ReverbBenchmarkAndFeatureParity.md`](Docs/ReverbBenchmarkAndFeatureParity.md).
