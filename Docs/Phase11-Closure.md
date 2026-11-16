# PHASE 11 — COMPLETE

Stereo and spatial finalisation. Audited across four stages, accepted by
measurement and by human listening, **with no production DSP change
whatsoever**.

`Source/` is **byte-identical to the Phase 10 closure commit**. Phase 11 added
tests, measurements, documentation and listening material, and changed nothing
that ships.

Phase 12 is **not** started and is not to begin without explicit
authorisation.

---

## 1. Final stereo architecture

Read from current Source, not from design intent.

### Where the channels are independent

| stage | file | behaviour |
|---|---|---|
| input capture | `PluginProcessor.cpp` | a mono host input is **copied** to both channels, so mono and dual-mono are identical thereafter |
| Low Cut, High Cut | `CutFilter.h` | one filter pair per channel |
| Pre-Delay | `PreDelay.h` | two delay lines read at the **same tap index** — a pure common delay, structurally incapable of skewing the image |
| Early Reflections | `EarlyReflections.h` | two ring buffers, **different** tap times and signs per channel — the only deliberately asymmetric stage |
| Attack | `AttackInjection.h` | two rings, **identical** schedule — a scalar operator times the identity |
| input diffuser | `StereoDiffusionNetwork.h` | two fully independent chains, different delay sets, **no cross-channel path** |

### Where they interact

| stage | behaviour |
|---|---|
| **FDN injection** | `injection[i] = injL[i]·l + injR[i]·r` — both input channels drive all eight lines. First cross-channel point |
| **FDN feedback** | an 8×8 orthogonal matrix mixes every line into every other **every sample** |
| **FDN readout** | each output is a different sign combination of all eight line states |
| **Width** | mid/side on the wet signal only |
| Dry/Wet Mix | per channel — **not** a cross-channel point |

Vector geometry, verified: `injectionL · injectionR = 0`,
`readoutL · readoutR = 0`, `injectionL · readoutL ≠ 0`. Both pairs orthogonal,
and the left readout is not aligned with the left injection, so neither output
is a privileged view of its own input.

## 2. Width behaviour — kept unchanged

Mid/side scaler on the wet signal, after the Early/Late combine, before Mix.
Range 0–200 %.

| Width | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| 0 % | 1.0000 | 0.00 dB | −3.0 dB | **−3015 dB** | 0.00 dB |
| 50 % | 0.5947 | −0.26 dB | −4.0 dB | −9.9 dB | −0.98 dB |
| **100 %** | −0.0087 | −0.33 dB | −6.1 dB | −6.0 dB | −3.05 dB |
| 150 % | −0.3922 | −0.30 dB | −8.2 dB | −4.6 dB | −5.17 dB |
| 200 % | −0.6058 | −0.26 dB | −10.1 dB | −4.0 dB | −7.05 dB |

- **0 % is true mono**, not a narrowed signal — side energy at the arithmetic
  zero of the collapse.
- **100 % is exactly neutral** — reconstruction error **1.4e-17**, an algebraic
  identity rather than a calibration.
- Above 100 % correlation goes negative, as a widener must.
- **No centre movement and no gain change**: R−L rms inside 0.33 dB throughout.

## 3. Early-reflection spatial behaviour — kept unchanged

| Early/Late | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| 25 % | −0.0103 | −0.25 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| 50 % | −0.0108 | −0.18 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| 100 % | −0.0103 | 0.00 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| branch isolated | −0.0103 | **0.00 dB** | −6.1 dB | −6.0 dB | −3.06 dB |

Per-event L/R time offset: **mean 0.64 ms, worst 2.60 ms**. A mean near zero
means neither channel systematically leads — a consistent lead is a *position*,
a scatter of leads and lags is a *space*.

The ER layer has **no cross-channel path at all**: a one-sided source stays
one-sided, a centred source gets two decorrelated channels of equal energy.
**Width without an arbitrary pull is exactly that pair of properties.**

## 4. Late spatial behaviour — kept unchanged

Early/Late 0, broadband, centred:

| setting | corr | R−L rms | mono fold |
|---|---|---|---|
| Size 20 / 60 / 100 % | 0.0077 / −0.0087 / −0.0127 | ≤ 0.33 dB | −2.97 / −3.05 / −3.07 dB |
| Mod Depth 0 / 30 / 100 % | −0.0014 / −0.0087 / −0.0096 | ≤ 0.59 dB | −3.02 / −3.05 / −3.05 dB |
| Decay 0.4 / 2.5 / 20 s | 0.0172 / −0.0087 / −0.0098 | ≤ 0.51 dB | −2.94 / −3.05 / −3.05 dB |

Worst imbalance **0.59 dB**, worst departure from the decorrelated downmix
**0.07 dB**. No periodic stereo movement, **no excessive widening at the
extremes**.

**The stereo field is architectural, not a final widening trick.** At neutral
Width the late branch already carries −6.0 dB of side energy and the ER branch
the same, against a −3.01 dB ideal for equal mid and side. Width 0 % collapses
a genuinely stereo signal rather than revealing a mono one.

## 5. Source-side localisation philosophy — accepted

**ER preserves source-side localisation. The late field is diffuse and
centre-seeking.**

A hard-panned source produces a tail **0.74 dB** off centre. Three positions
were measured and weighed:

| | behaviour | architecturally |
|---|---|---|
| **A. diffuse / centre-seeking** | the late field forgets where the source was | **free** — what the shared tank with a scrambling matrix already does |
| B. moderate retention | a few dB of tail bias | **not reachable through data** (section 10) |
| C. strong true-stereo retention | near-full retention | separate per-channel tanks; roughly doubles tank cost, loses the shared-mode density |

**A is accepted for V1.** B and C are not data changes; the ER layer already
supplies source position wherever Early/Late exceeds 0, which is where
listeners localise; and Phase 12 may revisit it as Mode data.

## 6. Narrow-band mono: KNOWN MEASURED NON-BLOCKING CHARACTERISTIC

### What it is

On sustained narrow-band material the two branches can acquire a definite
phase relationship at the source frequency, and the downmix then loses more
than the 3.01 dB that summing decorrelated channels costs.

Worst measured: **correlation −0.9848**, mono fold-down to **−15.40 dB**,
sustained across all twelve analysis windows. In the **late path**, at
**Early/Late 0** — the ER layer is not even running.

### It is not a small-Size phenomenon

Stage 3 found the worst fold-down is between **−7 and −20 dB at every Size**,
including the largest and densest. Stage 1 found it at Size 15 % because that
is where it swept.

### The fraction of the frequency axis affected

60 tones per cell, 100 Hz – 4 kHz, Mod Depth 0:

| | median | < −6 dB | < −9 dB | < −12 dB |
|---|---|---|---|---|
| Size 0–100 %, Decay 0.6 / 2.5 s | **−2.24 … −3.12 dB** | 10–15 % | 1.7–10 % | 0–3.3 % |

**The median is healthy everywhere.** The original −15.40 dB came from landing
a pure sine on one of the few percent of frequencies that cancel.

### It survives musical material — which is why it needed listening

Stage 3 recommended reclassification on the strength of the healthy median.
**That recommendation was premature and was refused, correctly.** Stage 4
measured musical sources at equal-tempered pitches:

| setting | note | pure tone | pad | bass | **piano** |
|---|---|---|---|---|---|
| **product default** S60 D2.5 | D4 293.7 Hz | −9.60 | −4.88 | −7.23 | **−7.76 dB** |
| product default | C#4 277.2 Hz | −5.50 | −2.78 | −4.80 | −4.93 dB |
| product default (control) | C4 261.6 Hz | −0.26 | −1.30 | −0.66 | −0.77 dB |
| small room S15 D0.6 | A3 220 Hz | −13.67 | −10.51 | −12.08 | **−12.60 dB** |
| small room (control) | B3 123.5 Hz | −0.35 | −1.38 | −0.50 | −0.62 dB |

Harmonics do not rescue it, and adjacent semitones differ by about 4 dB. On
measurement alone this looked like a shipping blocker, and it was carried as
one until a human heard it.

### Broadband material is unaffected

Across impulse, noise, snare, vocal and piano the late path sits within
**0.09 dB** of the decorrelated downmix ideal and inside 0.5 dB of L/R balance.

## 7. Human listening result

Six-file pack, `renders/phase11-mono-pack/`, each file playing control →
failing → control, mono folds **not** renormalised.

| | result |
|---|---|
| stereo-to-mono fold-down | a **small audible reduction** in reverb level |
| does the reverb disappear? | **no** |
| default-like settings, Mix ≈ 60 % | the problem note did **not** collapse or lose its reverb in an obviously abnormal way |
| severe small-Size / short-Decay case | mono sounded somewhat less reverberant, but **the tail remained clearly present** and the effect was **not judged a practical product failure** |

**Classification: KNOWN MEASURED NON-BLOCKING CHARACTERISTIC.**

The measurements leaned the other way and are preserved in full. The listening
settled it, which is the right order and the reason the pack existed.

## 8. Bell-like tonal tail: KNOWN MILD NON-BLOCKING TONAL CHARACTERISTIC

A human heard a very subtle bell-like tonal ring in the tail on a vocal at
about 60 % Mix.

### Measurement — with two limits of the analysis recorded

**The Q figures are not measurements.** Every bandwidth read 2.93 Hz, which at
a 32768-point FFT is exactly two bins — the resolution floor. The true mode
bandwidth at RT60 2.5 s is about 0.88 Hz, below what the transform resolves.

**The pad and vocal rows measure the source, not the tail.** A pad at 220 Hz
has its own harmonic peaks; only impulse and noise say anything about the
tail's own colour.

On that basis, the dominant peak sits at **1356 Hz** with **18.02 dB**
prominence against the median of a third-octave window — but only **0.80 dB**
of third-octave prominence.

### Subsystem isolation — one change at a time

| moves the peak frequency | leaves it unmoved |
|---|---|
| input diffusion (1356 → 3102 Hz) | Mod Rate |
| allpass bank (→ 646 Hz) | damping |
| Mod Depth 0 (→ 1159 Hz) | Decay |
| Early Diffusion (→ 2026 / 633 Hz) | |
| Size (→ 6864 / 1365 Hz) | |

**No configuration removes it**: prominence stays 14–23 dB throughout and
third-octave prominence stays under **1.32 dB**. High Damp merely attenuates
it (18.02 → 17.23 dB); it does not alter its formation.

**Where in the chain:** probing a single FDN line state through a unit readout
vector — public API, no production change — shows the peak **already present
there** (line 0 19.26 dB, line 3 16.64, line 7 14.08). It forms in the
recurrence, not in the readout combination.

**Persistence** is governed by Decay: −113 dB at 0.8 s against −11.5 dB at 8 s.

**L/R:** the dominant peak is at **different frequencies** in the two channels
— 1356 and 1491 Hz — within 2.01 dB of each other in prominence.

### Why B and not C

- narrow high-resolution modal peaks are measurable
- perceptually smoothed prominence remains low (< 1.32 dB everywhere)
- **no single defective subsystem was isolated** — the peak moves with five of
  them and none removes it
- the peaks are intrinsic to the recursive modal structure: eight lines give
  resolvable poles, and a pole is a narrow peak
- the human observation is subtle rather than disruptive
- **no justified production fix has been identified.** There is no smallest
  responsible mechanism to name, so there is nothing minimal to propose

No blind A/B pair was prepared, and the reason is measurement rather than
omission: the brief authorised one *if a subsystem strongly changed the peak*.
None did. Asking a listener to compare two renders would have required first
producing a difference.

## 9. Rejected readout alternatives

| candidate | reason |
|---|---|
| **every orthogonal ±1 pair** | **ruled out by exhaustive enumeration.** All 65 536 pairs were checked: 1 120 are orthogonal, and **zero** avoid a fully cancelling line. Orthogonality of two ±1 vectors of length 8 forces four sign agreements and four disagreements, and every disagreement is a line whose `L + R` is exactly zero and which therefore vanishes in mono |
| Hadamard rows | sign vectors, so they inherit the same four cancelling lines — predicted by the enumeration and confirmed in audio (min corr −0.9872). Also the only candidate with a **systematic 2.31 dB channel preference** |
| rotated 15° / 22.5° / 30° | improve the subset metric monotonically but do not clear the failing point: all three still show 9 of 9 windows below −0.5 correlation |
| **rotated 45°** | fixes the identified cell decisively (9/9 windows → 0/9, −19.43 → −3.81 dB) and makes the worst **normal** cell **worse** (−5.92 → −16.54 dB). It relocates the failure. Also raises per-frequency balance scatter 16.4 → 20.2 dB |
| quadrature (non-binary) | worse than production on the subset metric (−15 to −21 dB) and in audio (normal-cell fold −13.57 dB against −5.92 dB). Having no zero-sum lines is not sufficient |

**Why the subset metric over-promised:** it assumed each line carries an
*independent* state. The feedback matrix couples them, so two lines read by
opposite outputs can carry *correlated* states and cancel anyway. The metric is
**necessary but not sufficient**.

Noted for the record: the rotations did markedly **improve** L/R swap symmetry
(0.64 → 0.05 dB) and broadband centring (2.22 → 0.17 dB). Real benefits, simply
not a fix.

## 10. Rejected injection alternatives

| injection | left only | mean bias | corr | mono fold |
|---|---|---|---|---|
| current | −1.06 dB | **0.74 dB** | −0.0087 | −3.05 dB |
| slight retention | −0.31 dB | **0.30 dB** | 0.0008 | −3.01 dB |
| moderate retention | −0.64 dB | **0.73 dB** | 0.0371 | −2.85 dB |

**No improvement at all**, and the intermediate blend is *lower* than the
baseline. The experiment was set up expecting to confirm injection as the
lever; it refuted it.

**The reason is the feedback matrix, and it applies to readout and injection
alike.** Side retention would require energy that entered on the left to remain
preferentially visible to the left readout. The matrix mixes every line into
every other every sample, so after a few loop traversals the state is close to
isotropic and carries no direction to retain. Whatever side information
survives comes from the first few milliseconds, a small fraction of the energy
at any musical Decay. **An architectural limit, not a tuning question.**

## 11. Rejected delay-set alternatives

80 tones per candidate, pooled over two Sizes so none can win by relocating the
problem:

| candidate | worst GCD | median | **< −6 dB** | worst |
|---|---|---|---|---|
| **production** | 15 | −2.72 dB | **17.5 %** | −17.71 dB |
| golden spacing | 72 | −3.40 dB | 23.8 % | −15.09 dB |
| wide spread | 310 | −3.00 dB | 23.8 % | −18.54 dB |
| coprime at default | **1** | −2.72 dB | 21.2 % | −14.76 dB |

**Production is the best of the four.** SET-E (non-uniform per-line scaling)
was not built, because a law attacking modal density cannot fix a failure that
occurs at every density — building it would have been work invented to fill the
stage.

### Three mechanisms tested, three refuted

| mechanism | verdict |
|---|---|
| sample quantisation | **ruled out** — 0.121 % worst error |
| scale-dependent GCD collapse | **ruled out** — r = 0.094. At Size 0 % two lines land on 365 and 511 samples (5×73 and 7×73), yet Size 40 % with a low GCD has the worst fold and Size 100 % with a high GCD the best. Refuted a second time by the coprime set performing worse |
| sparse modal density | **10–23 % of the variance.** Real but minor, with clear counter-examples: the densest cell gives the *best* fold-down |

**Modulation makes it mildly worse rather than better** — 17.5 % affected at
Mod Depth 0 against 22.5 % at the shipping default — because moving the delays
moves the modes. The deepest nulls do get shallower (−17.71 → −14.68 dB). The
brief's test is still passed: the geometry stands on its own at Mod Depth 0,
where production is the best of four.

## 12. Why no decorrelation DSP was added

Four reasons, in order of weight.

1. **The human listening found the condition acceptable.** A new mechanism
   would be solving a problem a listener judged not to be a practical product
   failure.
2. **It would be the first thing in this engine added to hide a measurement
   rather than to serve a musical purpose.** The brief ruled out correlation
   clamps, dynamic mono protection and post-Width manipulation from the start,
   and the reasoning extends to a decorrelating allpass: all four treat a
   symptom whose cause is understood.
3. **It would cost Phase 10's byte identity.** Any change to the late path
   breaks the 12/12 legacy comparison and would require an explicit Phase 11
   re-baseline and a full re-run of every Phase 10 behavioural gate. That price
   is payable for a real defect; it is not payable for a characteristic.
4. **The cause is not in any one place to fix.** Three stages tried three
   independent data levers — readout vectors, injection vectors, delay sets —
   and none moves it, because the condition is narrow-band excitation of a
   modal field read by two fixed linear combinations.

## 13. Phase 10 compatibility status

**Fully intact. `Source/` is byte-identical to the Phase 10 closure commit.**

| | |
|---|---|
| legacy neutrality | **12 of 12 cases byte-identical** to the accepted baseline |
| state suite | **26 / 26** |
| Phase 10 accepted constants | all unchanged — `cellOccupancy` 0.40, `envelopeDecay` 1.5, `sizeAlpha` 0.25, BAL-4 table, the linear-in-power gain law, `earlyLate` default 0 |
| Phase 10 diagnostic gates | all four preserved and still failing on purpose |

**No Phase 11 re-baseline is needed**, because no late waveform changed. Had a
readout or delay-set change been adopted, the re-baseline procedure is
documented in `Phase11-Stage2-Readout.md`.

## 14. Sample-rate and block-size status

ER spatial geometry is specified in milliseconds, so it is the same field at
every rate:

| rate | ER L/R offset, mean | worst |
|---|---|---|
| 44.1 kHz | 0.643 ms | 2.630 ms |
| 48.0 kHz | 0.641 ms | 2.604 ms |
| 96.0 kHz | 0.639 ms | 2.604 ms |
| 192.0 kHz | 0.643 ms | 2.615 ms |

Block size, relative to each configuration's own peak, against a 512-sample
reference:

| block | Early/Late 0 | Early/Late 50 |
|---|---|---|
| 32 | 0.007073 | 0.010463 |
| 64 | 0.001102 | 0.001391 |
| 128 / 480 / 1024 | **0.000000** | **0.000000** |

The two columns move together, so the dependence below 128 samples belongs to
the tank's control-rate updates, **not** to the ER layer. **Known engine
characteristic, no new spatial defect.**

Size continuity: the largest **relative** single-line step across a
5 %-per-step sweep is **11.0 %**, exactly what the geometric mapping predicts,
constant at every Size. Delay changes ride the existing dual-bank morph, so
taps never move while being read.

## 15. CPU and memory status

300 blocks of 512 samples, best of 5:

| stage | time | % of real time |
|---|---|---|
| late stereo engine (tank) | 41.95 ms | 1.311 % |
| the same tank, Mod Depth 0 | 22.46 ms | 0.702 % |
| ER stereo layer | 1.69 ms | 0.053 % |
| Width mid/side | 0.07 ms | **0.002 %** |

**Modulation is 46.5 % of the tank's cost** — the largest single
stereo-related item, worth knowing before Phase 12 varies it.

Memory: the stereo-specific storage is the ER layer's two ring buffers
(**128 KiB** at 48 kHz), the pre-delay's two lines, and the two independent
diffuser chains. **The tank's eight lines are shared by both channels by
construction** — there is no per-channel tank — so stereo costs nothing there.

## 16. Mode-readiness for Phase 12

### Already data — a Mode can vary these today

| attribute | reachable through |
|---|---|
| ER pattern | `er::Pattern`; `EarlyReflections::prepare` already takes one |
| injection vectors | `StereoFdn::setStereoVectors`, a public runtime call — asserted by test |
| readout vectors | the same call |
| diffuser patterns | `StereoDiffusionNetwork::setDelayPatterns`, which replaces both chains and refuses as a whole |
| delay set | `StereoFdn::setDelayPattern` |
| spatial scale | Pattern data for the ER, a mapping for the tank |

### Not data — would need code

| attribute | why |
|---|---|
| cross-channel coupling | there is no coupling parameter. The coupling *is* the 8×8 feedback matrix plus the injection/readout geometry. A Mode can change the stereo **view**; the mixing itself is not parameterised |
| per-channel tank | one shared eight-line tank. Separate left and right tanks would be new DSP |
| ER cross-feed | the ER layer has no cross-channel path at all |

**Phase 12 may intentionally vary spatial configuration data** — ER pattern,
injection and readout geometry, diffuser delays, delay set, spatial scale — and
the architecture supports that through existing public calls without touching
core stereo processing.

**Phase 11 itself is complete.** Nothing here is a Phase 12 commitment, and the
one attribute that is not data — the degree of cross-channel mixing — is also
where this phase found its hardest limit. Those two facts are related and
should be weighed together if a Mode ever wants a tighter, more positional
character.

## 17. Preserved intentional diagnostic failures

**All eighteen preserved. None weakened, none deleted.** They are the standing
record of everything above, and they are what will fail loudly if a future
change makes any of it worse.

### Phase 7 — six pre-existing diagnostics

Verified during Phase 10 by rebuilding `a1328f0` in a detached worktree and
running it on the same machine — identical values, identical order:

```
0.06022872179   T20/T30 agreement
19.34162509     stimulus-to-stimulus repeatability
0.05998214192   ensemble T20/T30 agreement
0.2154187833    the analytic decay law vs the tank
                candidate fit at 6000 Hz
                candidate fit at 12000 Hz
```

### Phase 10 — four intentional gates

```
C: the shipping law contributes under 1 dB of sag
the traversal target is met on normal musical material
the traversal target is met on tonal material too - IT IS NOT, and no value of K can fix it
the source-dependent floor is small next to the error being fixed - IT IS NOT
```

### Phase 11 Stage 1 — four finding-gates

```
a centred mono source produces no material L/R energy imbalance
a one-sided source produces a one-sided tail - IT DOES NOT, the late field collapses to the centre
no setting produces severe downmix cancellation
no setting produces near-perfect anti-correlation - IT DOES
```

### Phase 11 Stage 2 — two

```
and improves the worst NORMAL operating cell over production
injection blending increases source-side retention - IT DOES NOT; the matrix is the limit
```

### Phase 11 Stage 3 — two

```
some delay set materially reduces the affected fraction
the shipping modulation default does not make the narrow-band condition worse - IT MAKES IT MILDLY WORSE
```

## 18. Final regression totals

| suite | result |
|---|---|
| **full DSP regression** | **2573 / 2591** — 18 intentional/pre-existing failures, **0 new** |
| `AuroraVerbStateTests` | **26 / 26**, including legacy **12 / 12 byte-identical** |
| `runPhase11StereoAuditTests` | 27 / 31 — 4 finding-gates |
| `runPhase11ReadoutTests` | 14 / 16 — 2 finding-gates |
| `runPhase11DelaySetTests` | 7 / 9 — 2 finding-gates |
| `runPhase11Stage4Tests` | 5 / 5 |

**No new regression. No gate weakened.** Run undisturbed on an idle machine —
two earlier runs in this project failed CPU benchmarks while a build ran in
parallel, and `MatrixModulationCpuBenchmark`'s own source already records that
threshold firing twice on machine state rather than on a code change.

### Measurement errors of mine, found and corrected across Phase 11

Recorded because the project's convention is to keep the trail, and because a
report that lists only its successes is not evidence.

1. Mono fold-down compared against 0 dB instead of −3.01 dB, so the expected
   answer failed.
2. Swap symmetry tested on a centred source, where swap is a no-op — it
   measured output side energy and read 134 %.
3. The centre-pull conclusion drawn from twelve tones, where the mean was under
   1.5σ. Widened to 32: an apparent +2.37 dB lean became −0.01 dB with a 16/16
   split.
4. A sample-rate reference read before the loop had reached it.
5. A table header printed above a table with different columns.
6. The subset metric quoted as if it bounded real behaviour; it assumes
   independent line states.
7. The injection section's commentary asserting a result before its measurement
   came back.
8. A GCD section headed "the real mechanism" before its correlation came back
   at 0.094.
9. A modulation claim written before its measurement — it makes the condition
   mildly worse, not better.
10. A Size-continuity check bounding the absolute sample jump, which a
    geometric law cannot satisfy.
11. Q figures quoted from a bandwidth that was two FFT bins — the resolution
    floor, not a measurement.
12. Pad and vocal rows read as tail coloration when they were measuring the
    source's own harmonics.
13. **A premature reclassification recommendation in Stage 3**, made on a
    healthy median before anyone had heard the failure. Stage 4's musical
    measurements contradicted it, and the human listening then agreed with the
    original recommendation after all — by a different route and for better
    reasons.

## 19. Production artifact

**None new.** `Source/` is byte-identical to the Phase 10 closure commit, so
the shipping artefact is unchanged and carries the Phase 10 constants:

```
cellOccupancy     0.40
envelopeDecay     1.5
sizeAlpha         0.25
gain law          late = sqrt(1-t), early = sqrt(t) * K
BAL-4             5 x 3 Decay x Size table
earlyLate         default 0, versionHint 2
readout vectors   -1 -1 -1 +1 +1 +1 +1 -1  /  +1 -1 -1 +1 -1 -1 +1 +1
injection vectors +1 +1 +1 +1 -1 -1 -1 -1  /  +1 +1 -1 -1 +1 +1 -1 -1
delay set         26.27 30.38 38.10 42.58 47.06 54.20 66.99 82.92 ms
Width             mid/side, 0-200 %, exact at 100 %
```

ER pattern fingerprint: **`5065939454054309816`**, unchanged.

## 20. Closure commit

`9f60d04` — *Phase 11 closure: stereo and spatial finalisation complete*

Phase history: `af2d7d7` Stages 1–2 · `0ea3a91` Stage 3 · `0ebc4d4` Stage 4 ·
this commit.

---

# PHASE 11 — COMPLETE

The stereo and spatial architecture is accepted as it stands. **No production
DSP change was made in this phase**, and `Source/` is byte-identical to the
Phase 10 closure.

Two characteristics are carried with their full evidence and their failing
diagnostic gates:

- **narrow-band mono cancellation** — KNOWN MEASURED NON-BLOCKING
  CHARACTERISTIC, after a human found the effect audible but not a practical
  product failure
- **bell-like tonal tail** — KNOWN MILD NON-BLOCKING TONAL CHARACTERISTIC, with
  no single defective subsystem and no justified fix

**Phase 12 Mode work may intentionally vary spatial configuration data. Phase 12
is not started and is not to begin without explicit authorisation.**
