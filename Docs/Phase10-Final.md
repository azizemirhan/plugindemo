# PHASE 10 - final production candidate

Status: **TECHNICALLY READY FOR FINAL HUMAN SANITY LISTENING.**

Every hard gate passes. Perceptual acceptance is not given here and is not
ours to give. Phase 10 is **not** closed and Phase 11 is not started.

Human blind result that drove this integration: occupancy 0.20 against 0.40,
two cases, **no meaningful preference on either**. 0.40 selected.

---

## 1. Final production constants

| | value | changed this pass |
|---|---|---|
| events per channel | 8 (16 taps) | no |
| placement | velvet noise, fixed seed | no |
| `windowMs` | 80.0 ms at the default Size | no |
| `sizeAlpha` | 0.25 | no |
| `envelopeDecay` | **1.5** | no - promoted to final candidate |
| `cellOccupancy` | **0.40** | **yes, from 0.70** |
| gain law | **linear in power** | **yes, from trigonometric** |
| calibration | **BAL-4, 5 x 3 table** | **yes, from a single constant** |
| `earlyLate` default | 0 | no |

## 2. Final ER pattern fingerprint

```
5065939454054309816
```

An FNV-1a hash over every offset and weight of the shipping schedule, checked
against a recorded constant. Changing the pattern in any way fails that check
and names the derived figures that must be re-measured. It exists because
Stage 3 carried a 6.02 dB handoff figure into its report that had been
measured with a different placement law; nothing connected the number to the
pattern it came from. Now something does.

The schedule it vouches for, at 48 kHz:

| event | L offset | L time | L weight | R offset | R time | R weight |
|---|---|---|---|---|---|---|
| 0 | 290 | 6.04 ms | -0.7491 | 165 | 3.44 ms | -0.8322 |
| 1 | 740 | 15.42 ms | 0.4337 | 678 | 14.12 ms | -0.3756 |
| 2 | 1284 | 26.75 ms | -0.2996 | 1283 | 26.73 ms | -0.2458 |
| 3 | 1643 | 34.23 ms | -0.2488 | 1722 | 35.88 ms | -0.1966 |
| 4 | 2227 | 46.40 ms | 0.1931 | 2246 | 46.79 ms | 0.1572 |
| 5 | 2575 | 53.65 ms | 0.1691 | 2595 | 54.06 ms | -0.1376 |
| 6 | 3124 | 65.08 ms | 0.1395 | 3037 | 63.27 ms | 0.1178 |
| 7 | 3672 | 76.50 ms | 0.1170 | 3583 | 74.65 ms | -0.0987 |

Sum of squares exactly 1.000000000 per channel. No tap at offset 0. 15
distinct merged gaps out of 15, **0 % repetition at every period**. Per
channel the cell guard holds the gap ratio to 1.68x and 1.73x.

## 3. Final signal path

```
input -> Low Cut -> High Cut -> Pre-Delay -+-> Attack -> tank -> trim -+-> balance -> Width -> Mix
                                           |                           |
                                           +-> early reflections ------+
```

Four properties follow from where the split is, not from care taken at the
call site: Pre-Delay moves both branches equally because it is upstream;
Attack cannot reach the ER because the ER reads the signal first; Early
Diffusion cannot, because input diffusion is inside the tank; late damping
stays a late-feedback property. The decay-aware trim applies to the tank
branch only.

## 4. Final gain law

```
lateGain  = sqrt(1 - t)
earlyGain = sqrt(t) * K
```

Both endpoints written out explicitly - `sqrt(0)` and `sqrt(1)` are exact, so
the branches are redundant, and they are there anyway because an endpoint
correct by coincidence of the library is not the same claim as one correct by
construction. At `t = 0` the processor does not evaluate the gains at all; it
takes the explicit legacy bypass.

**This is constant power, like the trigonometric form it replaces**, but it
splits power *linearly in the control*, which makes total energy a straight
line in `t` for any K. A calibration error becomes a monotone tilt rather
than a bow away from the chord. Measured across 24 dB of deliberate
mis-calibration: trigonometric bows up to 0.189 dB, this stays flat to three
decimals. On a sustained pad the worst sag fell 1.28 -> 0.08 dB.

The exponent family between constant power and linear amplitude was measured
and rejected - sag grows monotonically with the exponent on every source,
because the branches never reach the correlation that would make amplitude
addition the better model.

## 5. Final BAL-4 table

Re-measured on the final 0.40 pattern, not carried over from 0.70.

| | Size 0 % | Size 50 % | Size 100 % |
|---|---|---|---|
| Decay 0.1 s | 0.0963 | 0.0643 | 0.0350 |
| Decay 0.5 s | 0.3049 | 0.4245 | 0.5210 |
| Decay 2.5 s | 0.4156 | 0.6575 | 1.0324 |
| Decay 10 s | 0.4426 | 0.7217 | 1.1937 |
| Decay 60 s | 0.3750 | 0.6169 | 1.0375 |

Knot axes 0.1 / 0.5 / 2.5 / 10 / 60 s and Size scale 0.25 / 0.7071 / 2.0,
checked against `mapping::rt60For` and `mapping::sizeScaleFor` so the literals
cannot drift from the mapping.

**The 0.1 s row runs the other way** - falling with Size where every other row
rises - because the trim's 12 dB safety limit is active there. PHASE 6
recorded that limit as a known unmet target at exactly this decay. It is the
corner that rules out an analytic fit: the best four-coefficient fit leaves
8.71 dB there where the table leaves 1.26 dB.

## 6. Calibration methodology

`K = sqrt(late energy / ER energy)` at each knot, both branches from identical
history, a 10 ms noise burst over a 4 s window, every other control at its
default. Interpolated bilinearly in `log(rt60) x log(sizeScale)`, clamped
outside the knots rather than extrapolated.

**Not derived from the audio.** No level detection, no envelope follower, no
correlation measurement, no source classification, no AGC. K changes when the
user changes Decay or Size and at no other time.

Verified: every knot matches a fresh measurement to **0.007 dB**; the
interpolator reproduces each knot; it clamps rather than extrapolates.

## 7. Multi-source traversal results

Reported as three separate errors, because they have different causes and
different remedies and one combined figure would hide all three.

### A. Parameter-space error - the table against ideal, on broadband material

| setting | table K | ideal K | error |
|---|---|---|---|
| defaults (D 2.5, S 60) | 0.6575 | 0.6587 | -0.02 dB |
| small room (D 0.8, S 30) | 0.4413 | 0.4470 | -0.11 dB |
| large hall (D 4, S 90) | 0.9874 | 1.0124 | -0.22 dB |
| long + large (D 12, S 100) | 1.1768 | 1.2037 | -0.20 dB |
| short + small (D 0.3, S 10) | 0.2157 | 0.2481 | -1.22 dB |
| shortest decay (D 0.1, S 60) | 0.0569 | 0.0658 | -1.26 dB |

**Worst: 1.26 dB.** Target met.

### B. Source-dependent error - unreachable by any parameter-only law

| setting | worst source | spread |
|---|---|---|
| defaults | pad | 3.14 dB |
| small room | piano | 2.55 dB |
| large hall | pad | 2.24 dB |
| long + large | pad | 4.29 dB |
| short + small | piano | 2.00 dB |
| shortest decay | piano | 3.24 dB |

**Worst: 4.29 dB.**

### C. Crossfade-law sag

| source | worst traversal | worst sag |
|---|---|---|
| broadband noise | 1.26 dB | 0.00 dB |
| snare | 1.28 dB | 0.01 dB |
| vocal | 3.05 dB | 0.04 dB |
| piano | 4.90 dB | 4.51 dB |
| pad | 13.44 dB | 12.90 dB |
| sine diagnostic | 10.16 dB | 3.09 dB |

## 8. Source-dependent residual behaviour - THE ONE PRODUCT CONCERN

On broadband and transient material the design does what it was built to do:
**1.26 and 1.28 dB** of traversal with essentially no sag.

On **sustained narrowband material it does not**, and this is an explicit
product concern rather than a rounding error.

The mechanism, measured rather than inferred. Branch correlation on a pad:

| setting | corr(late, ER) | traversal at 0 / 25 / 50 / 75 / 100 % |
|---|---|---|
| D 2.5, S 60 | **+0.6141** | 0.0 / +2.8 / +3.5 / +3.8 / +3.1 dB |
| D 0.3, S 10 | **-0.8566** | 0.0 / -6.7 / **-13.4** / -9.8 / -1.1 dB |

Two layers carrying the same narrow band have a definite phase relationship at
that frequency, and sweeping their relative weights sweeps through
constructive or destructive interference. With a negative correlation that is
a real hole in the middle of the control.

**It is not the occupancy choice.** Checked across the family over 16 cells -
two narrowband sources, eight settings:

| | occ 0.20 | occ 0.40 | occ 0.70 |
|---|---|---|---|
| worst | 8.4 dB | 13.4 dB | 11.3 dB |
| **mean** | **3.8 dB** | **4.9 dB** | **4.8 dB** |
| cells won | 5 | 5 | 6 |

The means sit within 1.1 dB of each other against an effect reaching 13.4 dB,
and the per-cell winner is a tie. So the effect is **architectural**, and
reopening the occupancy choice would chase it around the family rather than
fix it. 0.40's 13.4 dB worst against 0.70's 11.3 dB is the kind of difference
a further setting reshuffles; picking an occupancy on one source at one
setting would be fitting noise.

**It cannot be fixed by K or by the law.** K sets the endpoints and this is in
the middle; the law's sag is the cross term, which no choice of exponent
removes. Fixing it would need either decorrelated branches or signal-aware
gain, and the second is forbidden for good reason - a balance that tracked the
programme material would behave one way on a snare and another on a pad.

**Two intentionally failing gates record it** and are not weakened.

## 9. Handoff measurements, recomputed on the final pattern

| Size | E/L 25 % | E/L 50 % | E/L 75 % | ER span | late first | overlap | valley at |
|---|---|---|---|---|---|---|---|
| 0 % | 0.41 dB | 0.58 | 0.65 | 2.52-56.00 ms | 8.52 ms | 47.48 ms | 10 ms |
| 20 % | 1.34 dB | 1.62 | 1.74 | 2.79-62.12 ms | 11.73 ms | 50.40 ms | 10 ms |
| 50 % | 1.58 dB | 0.26 | 0.25 | 3.25-72.60 ms | 20.35 ms | 52.25 ms | 30 ms |
| **60 %** | **2.50 dB** | 1.73 | 1.39 | 3.44-76.50 ms | 24.38 ms | 52.12 ms | 25 ms |
| 80 % | 3.71 dB | 4.62 | 2.24 | 3.81-84.88 ms | 36.17 ms | 48.71 ms | 35 ms |
| **100 %** | **8.87 dB** | 7.70 | 4.12 | 4.23-94.19 ms | 54.04 ms | 40.15 ms | 40 ms |

**Size 100 % fell from 16.47 dB to 8.87 dB** with the occupancy change. The
overlap stays positive at every Size, so the two layers do overlap; at Size
100 % the valley sits at 40 ms, inside the ER span but before the late field's
54.04 ms onset - a gap in the early layer's own decay rather than a comb.

No value from the 0.70 pattern is carried forward. The fingerprint check is
what enforces that.

## 10. Envelope decay status

**`envelopeDecay = 1.5` promoted to the PHASE 10 final candidate.**

Re-measured on the final pattern: decay 0.6 gives an 11.19 dB valley at Size
100 %, decay 1.5 gives 8.87 dB. Still better by 2.32 dB, with the ER tail at
-17.0 dB. The 0.40 pattern revealed no new audible or structural failure that
would justify reopening the search, and reopening it alongside the occupancy
and law changes would have destroyed the causal attribution.

## 11. Legacy neutrality - HARD GATE

**12 of 12 cases byte-identical to the accepted legacy baseline.**

Baseline is `a1328f0`, the PHASE 10 Stage 3 foundation commit. Its audio path
is identical to PHASE 9 closure `0a81aca` - the only `PluginProcessor.cpp`
hunk between them is in `setStateInformation`, `processInternal` is untouched,
and the parameter set, ranges and defaults are unchanged. Documented in
`Phase10-Stage3-Production.md` section 8.

`earlyGain` at Early/Late 0 is exactly 0.0 and the processor takes its bypass
branch before the gains are consulted, so no calibration law can reach the
bit-exact path. Asserted, not assumed.

## 12. Independence results

At Early/Late 100 % the late gain is exactly zero, so the output is the early
layer alone. Swept across their full ranges and compared **bit for bit**:

Attack, Early Diffusion, Late Diffusion, High Damp, Low Damp, High Damp Freq,
Mod Depth - **all seven byte-identical.** Early-only output peaks at 0.504, so
these are not the identities of silence.

**Decay is deliberately no longer in that list**, and that is a design change
rather than a concession: BAL-4 makes K a function of Decay, so Decay scales
the early branch's gain on purpose. The narrower claim is tested instead and
holds exactly - Decay 0.5 s against 10 s at Early/Late 100 %:

| | |
|---|---|
| samples compared | 11872 |
| measured gain ratio | 1.804657 |
| expected from the table | 1.804657 |
| worst per-sample drift | **0.000000** |

Decay changes how loud the early layer is and nothing whatever about what it
contains. Size is absent for the same reason plus the approved alpha law.

**Pre-Delay** still shifts both branches: the early-only residual after
shifting back is **0.000000** at both 20 and 80 ms. The composed figure is
dominated by the tank's own imperfect time invariance at Mod Depth 0, which
PHASE 10 did not introduce; measured against that control, composing the early
layer in does not worsen the shift.

## 13. Automation results

Early/Late sweep, rapid Early/Late, Decay at nonzero Early/Late, Size at
nonzero Early/Late - all against the steepest step the same material produces
with everything held:

| case | vs held |
|---|---|
| Early/Late sweep | 1.005x |
| Early/Late rapid | 0.707x |
| Decay at E/L 50 | 0.917x |
| Size at E/L 50 | 0.932x |

No clicks, no gain discontinuity, no NaN or Inf, no CPU spike. Seven balance
sequences including a 40-step random one stay within 1.112x.

**ER tap times cannot move during gain smoothing**, and that is structural: a
law computes two gains and has no access to the schedule. Confirmed by test -
tap offsets are bit-identical at balance 0, 13, 50, 87 and 100 %.

**Zero heap allocations** across 200 blocks of simultaneous Size and balance
automation.

## 14. Stereo and mono

| Early/Late | correlation | mono sum | vs the decorrelated expectation |
|---|---|---|---|
| 0 % | -0.0087 | -3.05 dB | -0.04 dB |
| 25 % | -0.0103 | -3.06 dB | -0.05 dB |
| 50 % | -0.0108 | -3.06 dB | -0.05 dB |
| 75 % | -0.0110 | -3.06 dB | -0.05 dB |
| 100 % | -0.0103 | -3.06 dB | -0.05 dB |

Flatter than the 0.70 pattern's, which drifted to -2.73 dB at the ER-heavy
end. Reported, not certified: a figure near -3.01 dB is *consistent with* two
approximately decorrelated channels of similar energy. It is not a proof that
no cancellation occurs, and PHASE 10 has declined to make that claim since
Stage 1.

## 15. CPU

400 blocks of 512 samples at 48 kHz, best of 7, against a same-run baseline:

| stage | time | % of real time | relative to the tank |
|---|---|---|---|
| the tank (legacy full-Late) | 55.14 ms | 1.29 % | 1.00x |
| early reflections, active | 2.24 ms | 0.05 % | **0.041x** |
| ER at Early/Late 0 | 0.29 ms | 0.01 % | **0.005x** |

Once per block, not per sample:

| | time | share of a 512-sample block |
|---|---|---|
| p = 0.50 gain law (`sqrt`) | 3.33 ns | 0.00003 % |
| BAL-4 lookup + interpolation | 76.18 ns | 0.00071 % |

The law uses `sqrt`, not generic `pow`: `pow` measured 2.3x the trigonometric
form and `sqrt` measures a third of it. The lookup is the dearer of the two
because it evaluates eight logarithms; precomputing them is *not* done,
because it would trade a readable table of measured values for a parallel
array of their logarithms that someone would eventually fail to keep in step.
At once per block the cost does not justify the hazard.

## 16. Memory

**2 x 8192 doubles = 128 KiB** at 48 kHz, 512 KiB at 192 kHz. Sized for the
160 ms span ceiling rather than the current span, so a Size change never
allocates. `prepareToPlay` is the only allocation point. The calibration table
is 15 `constexpr` doubles.

## 17. Sample rate and block size

Reflection **times** agree to **18.71 us** from 44.1 kHz to 192 kHz, measured
*across* rates rather than against each rate's own nominal span. Two prepares
produce bit-identical schedules.

Block size, relative to each configuration's own peak, against a 512-sample
reference:

| block | Early/Late 0 | Early/Late 50 |
|---|---|---|
| 32 | 0.007073 | 0.010463 |
| 64 | 0.001102 | 0.001391 |
| 128 | 0.000000 | 0.000000 |
| 480 | 0.000000 | 0.000000 |
| 1024 | 0.000000 | 0.000000 |

The two columns move together, so the dependence below 128 samples belongs to
the tank's control-rate updates, not to the ER layer. **Known engine
characteristic, not a PHASE 10 regression** - and stated rather than left as a
passing test that happened to use a convenient block size.

## 18. Production test totals

`runPhase10ProductionTests`: **18 / 20** sections, with **2 intentionally
failing checks** - both the narrowband concern of section 8.

`runPhase10Stage35Tests`: 15 / 17, two intentional.
`runPhase10BalanceLawTests`: 13 / 13.

## 19. State test totals

`AuroraVerbStateTests`: **26 / 26. ALL TESTS PASSED.**

Includes legacy 12/12, the seven independence identities, the Decay-scaling
property, Pre-Delay co-shift, block-size independence, the `earlyLate`
parameter contract and the absent-parameter state reset.

## 20. Full regression

**2520 / 2530.** Ten failures, every one accounted for:

**Six pre-existing**, verified last pass by rebuilding `a1328f0` in a detached
worktree and running it on the same machine - identical values, identical
order:

```
0.06022872179   T20/T30 agreement
19.34162509     stimulus-to-stimulus repeatability
0.05998214192   ensemble T20/T30 agreement
0.2154187833    the analytic decay law vs the tank
                candidate fit at 6000 Hz
                candidate fit at 12000 Hz
```

**Four intentional PHASE 10 gates**, all recording section 8:

```
C: the shipping law contributes under 1 dB of sag
the traversal target is met on normal musical material
the traversal target is met on tonal material too - IT IS NOT
the source-dependent floor is small next to the error being fixed - IT IS NOT
```

**No new regression. No gate weakened.** One gate was *replaced* - the old
"fixed K does not generalise" gate, because the design it indicted no longer
exists - and replaced with a stricter criterion on the new design.

The CPU benchmarks pass on an idle machine. Two earlier runs failed them while
a build ran in parallel; `MatrixModulationCpuBenchmark`'s own source already
records that threshold firing twice on machine state rather than on a code
change.

## 21. Known non-blocking characteristics

1. **Narrowband sustained material** (section 8) - up to 13.4 dB of traversal,
   architectural, present at every ER pattern measured. **The one item
   requiring a product decision.**
2. **Source-dependent calibration residual** - 4.29 dB worst. Closing it needs
   the signal analysis the brief forbids.
3. **Block-size dependence below 128 samples** - the tank's, not the ER's
   (section 17).
4. **Composed Pre-Delay shift residual** cannot be tightened to an identity
   while the tank is not exactly LTI at Mod Depth 0.
5. **Size compensation** still misses its +/-0.5 dB target at Decay 0.1 s
   (worst 5.27 dB), from the 12 dB safety limit, as PHASE 6 recorded - and it
   is the same limit that produces the inverted 0.1 s row in the BAL-4 table.
6. **PHASE 8 RT60 sensitivity harness** remains known-bad and is still not
   used as an absolute gate.
7. **The mono downmix figure** is consistent with decorrelated channels and is
   not a proof of no cancellation.

## 22. Final sanity render paths

`renders/phase10-final-sanity/` - four WAVs plus `LISTENING.md`:

```
final-snare-sweep.wav
final-vocal-sweep.wav
final-piano-sweep.wav
final-pad-sweep.wav
```

Rendered by `Scripts\export-phase10-final-sanity.ps1` on the complete
production candidate - occupancy 0.40, envelope decay 1.5, the p = 0.50 law,
the recalibrated BAL-4 table - at Size 80 %, Mix 30 %, everything else
default.

**Each file sweeps Early/Late from 0 to 100 % across its length.** That is a
decision worth stating: a static render cannot show unnatural level movement,
which is one of the six things these files exist to check. A sweep shows it
directly and walks through the handoff, the slap region and the ER-heavy end
in one pass, driving the real automation path.

`LISTENING.md` lists the six checks and states the section 8 behaviour up
front, so the pad's level trend is recognised as a known measured
characteristic rather than discovered as a surprise. **What the listening
decides is whether it is objectionable in use, not whether it exists.**

Superseded packs, kept for the trail: `renders/phase10-stage3-ab/` (eight
files, 0.70 vs 0.20) and `renders/phase10-occupancy-ab/` (four files, 0.20 vs
0.40, the comparison that returned no meaningful preference).

## 23. Commit

`a716b0d` - "Phase 10: final production candidate".

---

## Verdict

**TECHNICALLY READY FOR FINAL HUMAN SANITY LISTENING.**

All hard gates pass: legacy byte-identical 12/12, independence exact,
automation clean, no new regression, CPU and memory negligible.

**Perceptual acceptance is not declared here.** One product concern stands
open and is the reason the four renders exist: on sustained narrowband
material the two layers interfere and the control shows a level trend of
several dB. It is architectural, measured, and recorded; whether it is
acceptable in use is a listening judgement.

**Phase 10 is not closed. Phase 11 is not started.**
