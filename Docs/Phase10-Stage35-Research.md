# PHASE 10 - Stage 3.5: balance calibration and ER occupancy research

Research only. **No production constant was changed.** `envelopeDecay` stays
1.5, `cellOccupancy` stays 0.70, `balanceCalibration` stays 0.7187, and none of
them is a shipping constant.

Two issues were handed to this pass. Both now have evidence. One of them turned
out to be two issues, and the second of those cannot be fixed by calibration at
all.

---

## 1. Ideal K across Decay and Size

`ideal K = sqrt(late energy / ER energy)`, both branches from identical history,
10 ms noise burst, 4 s window.

| Decay | S 0 % | S 20 % | S 40 % | S 60 % | S 80 % | S 100 % | spread |
|---|---|---|---|---|---|---|---|
| 0.1 s | 0.0964 | 0.0697 | 0.0674 | 0.0658 | 0.0649 | 0.0350 | 8.81 dB |
| 0.3 s | 0.2351 | 0.2668 | 0.2984 | 0.3143 | 0.3342 | 0.3322 | 3.06 dB |
| 0.7 s | 0.3381 | 0.3978 | 0.4660 | 0.5286 | 0.6113 | 0.6602 | 5.81 dB |
| 1.5 s | 0.3942 | 0.4727 | 0.5642 | 0.6619 | 0.7965 | 0.9132 | 7.30 dB |
| 2.5 s | 0.4161 | 0.5034 | 0.6049 | **0.7187** | 0.8770 | 1.0324 | 7.89 dB |
| 5.0 s | 0.4332 | 0.5282 | 0.6382 | 0.7658 | 0.9446 | 1.1374 | 8.38 dB |
| 10.0 s | 0.4432 | 0.5423 | 0.6574 | 0.7917 | 0.9814 | 1.1937 | 8.61 dB |
| 25.0 s | 0.4381 | 0.5341 | 0.6495 | 0.7816 | 0.9775 | 1.1954 | 8.72 dB |
| 60.0 s | 0.3755 | 0.4561 | 0.5610 | 0.6793 | 0.8456 | 1.0375 | 8.83 dB |

**Total spread: 30.68 dB** (0.0350 to 1.1954). The previously reported 6.14 dB
was measured on a grid whose shortest Decay was 0.5 s; including 0.1 s, which is
the parameter's actual minimum, the fixed constant is wrong by up to **26.26 dB**.

**The ER branch is not the variable.** Its energy moves 0.02 dB over the whole
grid, because the taps are L2-normalised and the span change does not alter
their sum of squares. Every bit of the variation is the late branch, which is
the decay-aware trim's residual.

**Two shapes, not one.** Every row from 0.3 s up rises with Size. The 0.1 s row
*falls* with Size, 0.0964 down to 0.0350. That inversion is the trim's 12 dB
safety limit, which PHASE 6 recorded as a known unmet target at exactly 0.1 s
and attributed to the limit rather than to its table. It matters here because a
smooth surface cannot pass through an inverted corner without bending everywhere
else.

## 2. Sensitivity of ideal K to the other controls

Reference ideal K at the defaults: 0.7187.

| control | setting | ideal K | vs reference |
|---|---|---|---|
| Attack | 50 % | 0.7205 | +0.02 dB |
| Attack | 100 % | 0.7030 | -0.19 dB |
| Early Diffusion | 0 % | 0.7241 | +0.07 dB |
| Early Diffusion | 100 % | 0.7221 | +0.04 dB |
| Low Damp | 0 % | 0.7138 | -0.06 dB |
| Low Damp | 100 % | 0.7235 | +0.06 dB |
| **High Damp** | **0 %** | 0.8404 | **+1.36 dB** |
| **High Damp** | **100 %** | 0.5739 | **-1.95 dB** |
| Mod Depth | 0 % | 0.7264 | +0.09 dB |
| Mod Depth | 100 % | 0.7200 | +0.02 dB |

Attack, Early Diffusion, Low Damp and Mod Depth are all inside 0.2 dB and do
not earn a term. **High Damp reaches 1.95 dB** and is the only other candidate.

It is **not** recommended for the model, and the reason is scope rather than
size: 1.95 dB is the full travel of a control users rarely take to its
endpoints, against the 10 dB-plus that Decay and Size produce in ordinary use.
Adding a third axis triples the table for the smallest of the three effects. It
is recorded here so that if the two-axis model's residual ever needs explaining,
the next person knows where 2 dB of it lives.

## 3-6. The candidate models

Fitted over Decay >= 0.3 s, for the reason in section 1, then **scored over the
whole grid including 0.1 s** so the cost of the exclusion is visible rather than
hidden.

| model | worst (D>=0.3) | rms (D>=0.3) | worst (whole grid) | rms (whole grid) | form |
|---|---|---|---|---|---|
| **BAL-1** | 9.71 dB | 4.02 dB | 26.26 dB | 8.02 dB | fixed K = 0.7187 |
| **BAL-2** | 6.72 dB | 3.09 dB | 20.48 dB | 5.93 dB | Decay only |
| **BAL-3** | 5.77 dB | 1.81 dB | 24.15 dB | 5.61 dB | Decay + Size |
| **BAL-3b** | 3.13 dB | 0.75 dB | 17.14 dB | 3.28 dB | + log(rt60)^2 |
| **BAL-4** | - | - | **3.54 dB** | **1.08 dB** | 5 x 3 table, bilinear |

```
BAL-2   log K = -0.7113 + 0.1235*log(rt60)
BAL-3   log K = -0.5704 + 0.1235*log(rt60) + 0.4065*log(sizeScale)
BAL-3b  log K = -0.5704 + 0.1235*log(rt60) + 0.4065*log(sizeScale) - 0.0711*log(rt60)^2
BAL-4   15 measured values, interpolated bilinearly in log(rt60) x log(sizeScale)
```

Size earns its term (6.72 -> 5.77 dB worst) and the saturation term earns its
place (5.77 -> 3.13 dB). **Why the quadratic is needed:** K saturates in Decay.
It rises 4.06 dB from 0.5 to 2.5 s, only 0.84 dB from 2.5 to 10 s, and turns
back down above 25 s. A straight line in log(rt60) cannot be right in the middle
and at both ends.

BAL-4 is scored **off its own knots** - the whole 9 x 6 grid, of which it sampled
15 points. Scoring a lookup on its knots reports zero and means nothing.

## 7. Recommended model: BAL-4

**The 5 x 3 table, interpolated bilinearly in log(rt60) x log(sizeScale).**
Fifteen numbers.

It is recommended over BAL-3b, which has four coefficients and is tempting, for
one measured reason: **BAL-3b leaves 8.71 dB of traversal at Decay 0.1 s** while
BAL-4 leaves 1.26 dB. The analytic form is comparable in the musical range and
fails at the bottom of the Decay parameter, which is a setting users can reach.

The brief asked for the smallest useful model and preferred a compact analytic
fit "if its error is comparable to a lookup table". It is not comparable at the
short-decay end, so the table wins on the brief's own criterion.

Bilinear in the log domain also keeps the surface monotone between knots, which
is what "audible predictability" means for a control: turning Decay up never
makes the early layer quieter on one side of a knot and louder on the other.

## 8. Representative traversal variation

Worst traversal of **total integrated wet output** across Early/Late 0 to 100,
measured on the composed signal, not on branch energies.

Broadband material (noise burst and percussive hit):

| setting | BAL-1 | BAL-2 | BAL-3 | BAL-3b | **BAL-4** | ideal K |
|---|---|---|---|---|---|---|
| defaults (D 2.5, S 60) | 0.03 dB | 2.33 dB | 1.59 dB | 0.05 dB | **0.03 dB** | 0.03 dB |
| small room (D 0.8, S 30) | 4.13 dB | 0.58 dB | 0.89 dB | 0.90 dB | **0.60 dB** | 0.04 dB |
| large hall (D 4, S 90) | 2.98 dB | 4.80 dB | 1.86 dB | 0.12 dB | **0.22 dB** | 0.00 dB |
| long + large (D 12, S 100) | 4.48 dB | 5.12 dB | 1.45 dB | 0.36 dB | **0.20 dB** | 0.00 dB |

**BAL-4 worst on representative broadband settings: 0.60 dB.** The 1.0 dB
preferred target is met.

## 9. Worst tested traversal variation

Extreme combinations, broadband:

| setting | BAL-1 | BAL-3b | **BAL-4** |
|---|---|---|---|
| short + small (D 0.3, S 10) | 9.24 dB | 0.94 dB | **1.20 dB** |
| very long + small (D 40, S 15) | 3.71 dB | 0.47 dB | **0.31 dB** |
| shortest decay (D 0.1, S 60) | **20.76 dB** | 8.71 dB | **1.26 dB** |

**BAL-4 worst including extremes: 1.26 dB**, inside the 1.5-2.0 dB allowance.

### And the result nobody asked for: tonal material is a different problem

| setting | BAL-1 | BAL-4 | **ideal K** |
|---|---|---|---|
| defaults | 6.10 dB | 6.08 dB | **1.41 dB** |
| small room | 8.94 dB | 4.21 dB | 0.26 dB |
| large hall | 4.54 dB | 1.78 dB | 0.61 dB |
| long + large | 6.42 dB | 2.28 dB | 0.65 dB |

On a sustained harmonic tone **no calibration reaches the target**, and two
separate things are responsible.

**First, the ideal K is itself source-dependent.** At the defaults it is 0.7187
for noise and 1.4499 for a tone - a **6.10 dB** spread, the same order as the
error this pass was convened to remove:

| setting | noise | hit | tone | spread |
|---|---|---|---|---|
| D 2.5, S 60 | 0.7187 | 0.7210 | 1.4499 | 6.10 dB |
| D 0.8, S 30 | 0.4467 | 0.4413 | 0.2567 | 4.81 dB |
| D 12, S 100 | 1.2037 | 1.1995 | 1.5042 | 1.97 dB |

A parameter-only law is a function of settings, so it cannot track this. Closing
it would require measuring the incoming audio, which the brief forbids - and
rightly: a balance whose calibration followed the programme material would
behave differently on a snare and on a pad, which is the unpredictability the
fixed design existed to avoid.

**Second, and deeper: the ideal-K column is not zero either.** That column is the
traversal achieved by the K that perfectly equalises the two branch energies for
that very render, and on a tone at the defaults it still leaves 1.41 dB.
Constant-power crossfading assumes the branches add incoherently. For broadband
material they nearly do. For a sustained tone the ER layer and the late field
carry the same narrow band, their cross term is not negligible, and
`cos^2 + sin^2 = 1` stops describing the sum.

**That is a property of BAL-B's law, not of its calibration, and no choice of K
removes it.** A second intentionally failing gate records it.

So the honest summary of part A: **a parameter-aware K solves the part of the
problem that depends on settings** - the part a user can predict and automate -
**and leaves the part that depends on the music.** That is the right split. It is
a split, not a solution.

## 10. Automation implications

Steepest sample-to-sample step in the composed output, Decay swept across its
whole range with K recomputed every 64 samples from the model:

| | step | vs held |
|---|---|---|
| K held constant | 2.400387 | 1.000x |
| Decay swept, K per block | 2.256488 | **0.940x** |
| the same sweep eight times faster | 2.194270 | **0.914x** |

**No new machinery is needed, and that is structural rather than lucky.** K
reaches the audio only through `earlyGain`, a 20 ms `LinearSmoother` read per
sample. A new K is a new target for that smoother, exactly as a new balance
position is. Nothing recomputes a tap time, so there is no path by which moving
Decay or Size could move a reflection.

## 11. Legacy endpoint

**Unchanged and still bit-exact: 12 of 12 cases byte-identical to the PHASE 9
build.** The full state suite is 22 / 22.

K multiplies the **early** gain only. At Early/Late 0 that gain is exactly 0, the
processor takes its explicit bypass branch and never evaluates the product, so no
calibration law - this one or any other - can reach the bit-exact path. A check
asserts the exact zero rather than trusting it.

## 12. The occupancy family: structure

All metrics are computed from the engine at each occupancy, never restated from
an earlier run.

| occ | first | last | centroid | min gap | max gap | unique | periodicity |
|---|---|---|---|---|---|---|---|
| 0.15 | 4.42 ms | 75.56 ms | 14.27 ms | 0.00 ms | 10.48 ms | **13/15** | **15.4 %** |
| **0.20** | 4.21 ms | 75.75 ms | 14.17 ms | 0.00 ms | 10.67 ms | 15/15 | 0.0 % |
| 0.30 | 3.83 ms | 76.12 ms | 14.01 ms | 0.00 ms | 11.00 ms | **14/15** | **7.7 %** |
| 0.40 | 3.44 ms | 76.50 ms | 13.77 ms | 0.02 ms | 11.31 ms | 15/15 | 0.0 % |
| 0.55 | 2.85 ms | 77.04 ms | 13.39 ms | 0.00 ms | 11.81 ms | 15/15 | 0.0 % |
| 0.70 | 2.25 ms | 77.60 ms | 12.88 ms | 0.00 ms | 12.31 ms | 15/15 | 0.0 % |

A 0.00 ms minimum gap is a left and a right arrival on the same sample - one
centre-panned reflection, which rooms do. It is not a doublet; the guard that
prevents those is per channel.

**CPU is not measured, and that is a decision rather than an omission.**
Occupancy moves tap positions inside their cells; the tap count, the ring size
and the per-sample work are identical at every value. Six numbers differing only
by timing noise would invite someone to read a trend into them. The invariant is
asserted instead.

## 13. Handoff valley by occupancy and Size

Worst notch with a recovery, stereo magnitude, Early/Late 25 %. **The default
Size is in this sweep**, because Stage 3 shipped a gate that omitted it and
therefore could not fail.

| occ | Size 60 % | Size 80 % | Size 100 % | worst |
|---|---|---|---|---|
| 0.15 | 2.13 dB | 3.79 dB | **7.13 dB** | 7.13 dB |
| **0.20** | 1.93 dB | 3.67 dB | 7.26 dB | 7.26 dB |
| 0.30 | 1.97 dB | 3.59 dB | 7.90 dB | 7.90 dB |
| 0.40 | 1.98 dB | 3.61 dB | 8.86 dB | 8.86 dB |
| 0.55 | 1.94 dB | 3.95 dB | 11.05 dB | 11.05 dB |
| **0.70 (shipped)** | 3.56 dB | 5.22 dB | **16.47 dB** | 16.47 dB |

Monotone in occupancy, and the shipped value is the worst point in the family by
a factor of more than two.

## 14-15. Temporal gaps, periodicity and uniqueness

The gap metrics are in section 12. The finding that matters is the one at the
**bottom** of the range, and it is the counterweight to section 13:

**0.15 and 0.30 are not aperiodic.** 0.15 shows 15.4 % gap repetition and only
13 distinct gaps out of 15; 0.30 shows 7.7 % and 14 of 15. Only 0.20, 0.40, 0.55
and 0.70 are fully clean.

So the best valley in the family (0.15, at 7.13 dB) is **not** an acceptable
candidate, and the structural argument that rejected golden-ratio placement
rejects it for the same reason. **0.20 is the lowest occupancy that is both fully
aperiodic and near the best valley.**

## 16. Spectral ripple

Measured on the ER layer's own impulse response, at two resolutions.

| occ | fine peak above median | 1/3-octave ripple |
|---|---|---|
| 0.15 | 7.19 dB | 5.01 dB |
| **0.20** | 7.11 dB | **5.18 dB** |
| 0.30 | 7.86 dB | 5.03 dB |
| 0.40 | 7.48 dB | 3.76 dB |
| **0.55** | 7.50 dB | **0.78 dB** |
| **0.70 (shipped)** | 7.44 dB | **1.56 dB** |

**This is the trade-off, and it runs the opposite way to the valley.** The fine
figure is flat across the family - there is no fine-comb difference - but
third-octave ripple, which is what a listener hears as tone, is **3.3 dB worse at
0.20 than at 0.70**.

So the two measurements disagree about which end of the range is better: the
handoff favours low occupancy, tonal flatness favours high. Neither one decides.

*A note on the fine metric:* it is a peak above the median, not peak-to-trough.
Peak-to-trough was tried first and was useless - at 2.9 Hz resolution a sparse
tap train has single bins 40 dB down, so the figure reported the deepest null and
swung 20 dB between occupancies for nothing a listener could hear. PHASE 9
learned the converse lesson, that third-octave alone is blind to fine combs, so
both resolutions are reported and the disagreement would have been the finding.

## 17. Mono and stereo

| occ | correlation | mono sum |
|---|---|---|
| 0.15 | 0.0480 | -2.81 dB |
| 0.20 | 0.0519 | -2.79 dB |
| 0.30 | 0.0287 | -2.89 dB |
| 0.40 | -0.0108 | -3.06 dB |
| 0.55 | 0.0358 | -2.86 dB |
| 0.70 | 0.0287 | -2.89 dB |

**No meaningful difference across the family.** Correlation stays under 0.052 and
the downmix within 0.22 dB of -3.01 dB everywhere. Occupancy is not a stereo
parameter.

Reported, not certified: a figure near -3.01 dB is *consistent with* two
approximately decorrelated channels of similar energy. It is not a proof that no
cancellation occurs, and PHASE 10 has declined to make that claim since Stage 1.

## 18. Tests added and corrected

**Added** - `Tests/Phase10Stage35.cpp`, 10 sections, 17 checks:
ideal K across Decay x Size; sensitivity to five other controls; four candidate
models with least-squares fits; BAL-4 scored off its knots; traversal on three
sources at seven settings; the source-dependence floor; automation of a
parameter-dependent K; and the occupancy family's structure, handoff, spectrum
and stereo.

**Added** - the regression coverage the brief asked for, in
`Tests/Phase10Production.cpp`:

1. **A pattern fingerprint.** An FNV-1a hash over every offset and weight of the
   shipping schedule, checked against a recorded constant, with the derived
   figures it vouches for listed beside it. Changing the pattern in any way
   fails it and names what has to be re-measured. This is the guard against the
   exact mistake Stage 3 made - a 6.02 dB figure surviving a placement change
   that invalidated it.
2. **The handoff sweep is a named list, and a check asserts it contains the
   default Size.** A brace initialiser inside a `for` is not something a test can
   inspect, which is why the original omission was invisible.

**Corrected in this pass:** the fine-ripple metric (peak-to-trough to peak above
median, section 16); the fit range (the inverted 0.1 s row excluded from fitting
but kept in scoring, section 1); the traversal aggregation (split by source class,
because broadband tests the calibration and tonal tests the law).

**Two intentionally failing gates, and they are different gates:**

- Stage 3's: the fixed K does not generalise across Decay and Size.
- Stage 3.5's new one: on tonal material the traversal target is not met by
  **any** K, because BAL-B's law assumes incoherent branches. It stays failing
  until the law is revisited, and a replacement has to beat the measured
  ideal-K floor, which no calibration can.

Neither was weakened.

## 19. Regressions

**None.** `runPhase10ProductionTests` 35 / 36 (the one deliberate balance gate),
`AuroraVerbStateTests` 22 / 22, `runPhase10Stage35Tests` 15 / 17 (two deliberate
gates). The legacy bit-exact comparison is unchanged at 12 / 12.

No production constant was touched, so no audio changed. The pattern fingerprint
asserts that.

## 20. Recommendation, pending the human A/B

### Balance

**Adopt BAL-4** - the 5 x 3 table in log(rt60) x log(sizeScale) - when
authorised. It meets the 1.0 dB target on broadband material at representative
settings (0.60 dB worst) and the 2.0 dB allowance at the extremes (1.26 dB), it
needs no new smoothing, and it cannot reach the legacy endpoint.

Do **not** add High Damp as a third axis on this evidence.

Carry forward as an open item, not as solved: **tonal material**, where the
limit is BAL-B's constant-power law rather than its calibration.

### Occupancy

On measurement alone the candidate set reduces to **0.20** and **0.40**:

| | 0.15 | **0.20** | 0.30 | **0.40** | 0.55 | 0.70 |
|---|---|---|---|---|---|---|
| aperiodic | **no** | yes | **no** | yes | yes | yes |
| valley, Size 100 | 7.13 | **7.26** | 7.90 | **8.86** | 11.05 | 16.47 |
| 1/3-oct ripple | 5.01 | **5.18** | 5.03 | **3.76** | 0.78 | 1.56 |

0.15 and 0.30 are rejected on periodicity. 0.55 and 0.70 are rejected on the
handoff. Between the two survivors the measurements point in opposite
directions - 0.20 has the better handoff, 0.40 the better tonal flatness - and
**that is where measurement stops being able to decide.**

**The human A/B now running compares 0.70 against 0.20.** Its result is required
before any occupancy becomes a shipping candidate, and this pass does not
anticipate it. Two outcomes are worth planning for:

- **If 0.20 wins**, the structural evidence agrees and 0.20 becomes the
  candidate. Cases C and D must be re-rendered with it, because the existing
  renders use 0.70 and a final acceptance pack has to test what is actually
  proposed.
- **If 0.70 wins**, the valley is audibly less important than the measurement
  suggests, and the right follow-up is a second A/B between 0.20 and 0.40 rather
  than keeping 0.70 - because 0.70 is the worst handoff in the family by more
  than a factor of two and that should not survive on a single comparison it
  happened to win.

`envelopeDecay` stays at 1.5 and is not reopened. Nothing in this pass gives a
reason to, and reopening it alongside occupancy would destroy the causal
attribution the whole pass was structured to preserve.

## 21. Commit

Recorded in the commit that carries this document; the hash is reported with it.
