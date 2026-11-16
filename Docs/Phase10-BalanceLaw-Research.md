# PHASE 10 - the crossfade law: final balance research

Research only. **No production DSP constant was changed.** The architecture is
not reopened: Early/Late stays a composition control, BAL-4 stays the
calibration layer, Early/Late 0 stays an explicit bit-exact bypass.

One question: is constant-power the best source-robust crossfade law before
BAL-4 goes to production. The answer is yes, with one refinement that was not
asked for and that the measurements made unavoidable.

---

## 1. Law definitions

A law is a pair of shapes on `t` in [0, 1], before calibration. `early` is
multiplied by K afterwards, never inside the law, so the law and the
calibration stay separable.

| | late(t) | early(t) |
|---|---|---|
| **LAW-A** trigonometric, shipped | `cos(t*pi/2)` | `sin(t*pi/2)` |
| **LAW-C(p)** power family | `(1-t)^p` | `t^p` |
| **LAW-B** linear amplitude | `1-t` | `t` |

LAW-B is LAW-C with p = 1. **LAW-C with p = 0.5 is also exactly constant
power** - its squared gains sum to one, like LAW-A's - and it is included so
the shipped law and the family sit on one axis instead of being compared across
two formulations.

Every power form has exact endpoints for free, since `0^p` is 0 and `1^p` is 1.
LAW-A needed its upper endpoint written out by hand, because `cos(pi/2)` is
6.1e-17 rather than zero.

## 2. Calibration method, identical for every law

K is fixed by the **endpoints**: it is the gain that makes the total at
Early/Late 100 % equal the total at 0 %. Every law here goes from late 1 /
early 0 to late 0 / early 1, so the condition is identical and **one K serves
all of them**. Measured rather than asserted:

| law | late(0) | early(0) | late(1) | early(1) | endpoint total |
|---|---|---|---|---|---|
| all seven | 1.0000 | 0.0000 | 0.0000 | 1.0000 | -0.000 dB |

The table was **not** re-fitted per law, and that measurement is what licenses
not re-fitting it. K is also **not** tuned to minimise mid-travel error: that
would trade an exact endpoint for a better middle, and a user who sets
Early/Late to 100 % is entitled to the same loudness as at 0 %.

The K used throughout is the ideal K measured on the **broadband** source at
each configuration - exactly what BAL-4's table stores. Using the table's
interpolation instead would fold its 1.08 dB rms into every law equally and
obscure the thing being compared.

### Branch correlation, the mechanism

| source | corr(late, ER) | adds as |
|---|---|---|
| broadband noise | 0.0103 | energy |
| snare transient | 0.0065 | energy |
| vocal | 0.0494 | energy |
| piano | 0.1149 | between |
| pad | 0.1633 | between |
| sustained sine | 0.1991 | between |

Decorrelated branches add in energy and want constant power; fully correlated
branches add in amplitude and want a linear law. **Nothing here is remotely
close to correlated.** The most correlated source measures 0.199.

## 3-7. Results by source

Total integrated wet output relative to Early/Late 0, with the same K for every
law. `sag` is the law's own contribution - how far the middle falls below the
straight line joining the two ends - and it is the column that matters.

**`range` and `sag` are different faults and reading them as one is how this
question gets answered wrongly.** If K is wrong for the material the endpoint
is wrong and the whole curve tilts; every law inherits that equally and no law
can fix it. If the law does not match how the branches add, the curve bows away
from its own chord; that is the law's fault and it is what a listener hears as a
dip in the middle of the control.

### Broadband noise

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| **LAW-A trig** | -0.02 | -0.03 | -0.02 | -0.00 | **0.03 dB** | 0.03 |
| **p = 0.50** | -0.03 | -0.03 | -0.03 | -0.00 | **0.03 dB** | 0.03 |
| p = 0.70 | -0.93 | -1.24 | -0.93 | -0.00 | 1.24 dB | 1.24 |
| LAW-B p = 1 | -2.06 | **-3.04** | -2.06 | -0.00 | 3.04 dB | 3.04 |

### Snare transient

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| **LAW-A trig** | 0.01 | 0.03 | 0.06 | 0.07 | **0.07 dB** | 0.00 |
| **p = 0.50** | 0.01 | 0.03 | 0.05 | 0.07 | **0.07 dB** | 0.01 |
| LAW-B p = 1 | -2.04 | -2.98 | -1.98 | 0.07 | 3.05 dB | 3.05 |

### Vocal

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| LAW-A trig | -0.14 | -0.47 | -0.80 | -0.92 | 0.92 dB | 0.11 |
| **p = 0.50** | -0.24 | -0.47 | -0.70 | -0.92 | 0.92 dB | **0.01** |
| LAW-B p = 1 | -2.14 | -3.48 | -2.88 | -0.92 | 3.48 dB | 3.02 |

### Piano

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| LAW-A trig | -0.27 | 0.19 | 0.99 | 1.60 | 1.88 dB | 0.67 |
| **p = 0.50** | -0.19 | 0.19 | 0.72 | 1.60 | 1.80 dB | **0.59** |
| LAW-B p = 1 | -2.32 | -2.82 | -0.92 | 1.60 | 4.43 dB | 3.62 |

### Pad - the case that separates the two constant-power laws

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| LAW-A trig | -1.05 | -3.43 | -6.96 | -7.58 | 7.58 dB | **1.28** |
| **p = 0.50** | -1.69 | -3.43 | -5.76 | -7.58 | 7.58 dB | **0.08** |
| LAW-B p = 1 | -2.81 | -6.44 | -9.56 | -7.58 | 9.56 dB | 3.88 |

The 7.58 dB of range looks damning until the sag column is read: it is **all
tilt**, identical under every law, and it is the source-dependent calibration
error Stage 3.5 measured. The law's own contribution is 1.28 dB for the
trigonometric form and **0.08 dB** for the linear-in-power one.

Note that this real musical pad is a **worse** case than the synthetic sine
diagnostic (7.58 dB against 2.57 dB). The diagnostic extreme was not the
extreme.

### Sustained sine - diagnostic

| law | 25 % | 50 % | 75 % | 100 % | range | sag |
|---|---|---|---|---|---|---|
| LAW-A trig | -1.13 | -1.01 | 0.22 | 1.45 | 2.57 dB | 1.73 |
| p = 0.50 | -1.26 | -1.01 | -0.24 | 1.45 | 2.71 dB | 1.73 |
| LAW-B p = 1 | -3.03 | -4.02 | -1.58 | 1.45 | 5.47 dB | 4.74 |

### Summary

| law | worst sag, normal | worst range, normal | sag, sine |
|---|---|---|---|
| LAW-A trig | 1.28 dB | 7.58 dB | 1.73 dB |
| **p = 0.50** | **0.61 dB** | 7.58 dB | 1.73 dB |
| p = 0.60 | 1.22 dB | 7.58 dB | 2.33 dB |
| p = 0.70 | 1.82 dB | 7.58 dB | 2.94 dB |
| p = 0.80 | 2.58 dB | 8.27 dB | 3.54 dB |
| p = 0.90 | 3.27 dB | 8.95 dB | 4.14 dB |
| LAW-B p = 1.00 | 3.88 dB | 9.56 dB | 4.74 dB |

## 8. Correlation observations, and the refuted hypothesis

**The exponent family is monotonically worse, and the correlation table says
why.** The family was proposed because partly-correlated branches should want
something between energy and amplitude addition. Every step away from constant
power makes every source worse - including the correlated ones - because at a
correlation of 0.2 energy addition is still much the better approximation. So
moving towards linear buys nothing and pays a 3.04 dB mid-travel sag on noise
that was not there before.

**p = 0.60 through 0.90 are rejected. LAW-B is rejected.**

## The result that was not the question asked

Two of these laws are both exactly constant power and **they do not perform the
same**. p = 0.50 has 0.61 dB of worst sag against the trigonometric law's
1.28 dB, and on the pad 0.08 dB against 1.28 dB.

They differ in shape, not in power:

```
trig      late^2 = cos^2(t*pi/2)      early^2 = sin^2(t*pi/2)
p = 0.50  late^2 = 1 - t              early^2 = t
```

The second is **linear in the control**, which has a consequence the first does
not. With branches that add in energy, the total is

```
total / Le  =  (1 - t)  +  t * (K^2 Ee / Le)
```

a straight line in `t` for **any** K, right or wrong. A calibration error turns
into a smooth monotone tilt and nothing else. The trigonometric law also sums to
one, but its split is not linear in `t`, so a mismatched endpoint makes the curve
bow away from its chord - which is exactly the "obvious bump or drop in the
middle of the control" the brief rules out.

Tested directly on broadband material with K mis-calibrated on purpose:

| K error | trig sag | p = 0.50 sag | range, both |
|---|---|---|---|
| -12 dB | 0.000 dB | 0.000 dB | 12.00 dB |
| -6 dB | 0.000 dB | 0.000 dB | 6.00 dB |
| -3 dB | **0.189 dB** | 0.000 dB | 3.00 dB |
| 0 dB | 0.034 dB | 0.034 dB | 0.03 dB |
| +3 dB | **0.189 dB** | 0.000 dB | 3.00 dB |
| +6 dB | 0.000 dB | 0.000 dB | 6.00 dB |
| +12 dB | 0.000 dB | 0.000 dB | 12.00 dB |

The 0.034 dB both show at zero error is the decorrelation floor, not a sag - the
branches measure 0.0103 correlation rather than exactly zero.

**This matters because the calibration will be imperfect.** Stage 3.5 put the
source-dependent floor at 6-7.6 dB and no parameter-only law can close it. A law
that degrades gracefully under calibration error is therefore worth more than one
that is merely equally correct when the calibration is exact.

## 9. Worst normal-musical traversal

**Sag** - the law's own contribution: **0.61 dB** for p = 0.50, 1.28 dB for
LAW-A.

**Range** - including the calibration tilt no law can fix: **7.58 dB** for
both, on the pad.

## 10. Worst diagnostic traversal

Sustained sine: 1.73 dB of sag for both constant-power laws, 2.57 / 2.71 dB of
range. The sine is **not** the worst case; the pad is.

## 11. Automation

Both constant-power laws, four cases each, gains driven per block:

| law | case | steepest step | vs held | finite |
|---|---|---|---|---|
| LAW-A trig | Early/Late sweep | 2.409517 | 1.006x | yes |
| LAW-A trig | Early/Late rapid | 1.693953 | 0.707x | yes |
| LAW-A trig | Decay at E/L 50 | 2.197314 | 0.917x | yes |
| LAW-A trig | Size at E/L 50 | 2.230934 | 0.932x | yes |
| p = 0.50 | Early/Late sweep | 2.406880 | 1.005x | yes |
| p = 0.50 | Early/Late rapid | 1.693953 | 0.707x | yes |
| p = 0.50 | Decay at E/L 50 | 2.197314 | 0.917x | yes |
| p = 0.50 | Size at E/L 50 | 2.230934 | 0.932x | yes |

No click, no gain discontinuity, no NaN or Inf. **ER tap times cannot move under
either law:** a law computes two gains and has no access to the schedule.

## 12. Legacy neutrality

**Unchanged: 12 of 12 cases byte-identical to the PHASE 9 build, state suite
22 / 22.** `early(0)` is exactly 0.0 in every law, and the processor takes its
explicit bypass branch before the gains are consulted at all - so no law, this
one or any other, can reach the bit-exact path. Asserted, not assumed.

## 13. CPU difference between the laws

| law | per evaluation | relative |
|---|---|---|
| trig | 8.1 ns | 1.00x |
| p = 0.50, via `pow` | 19.1 ns | 2.34x |
| p = 0.70, via `pow` | 19.5 ns | 2.39x |
| **p = 0.50, via `sqrt`** | **2.7 ns** | **0.33x** |

The `pow` rows are an artefact of the harness evaluating every law through the
same call. A production p = 0.50 is `std::sqrt`, which makes it **the cheapest of
the three**.

And a law is evaluated **once per block** to set two smoother targets, not per
sample, so at 48 kHz with a 512-sample block this is under a nanosecond per block
for any of them. **CPU does not select a law here**, and presenting it as a
tiebreaker would be dressing a preference as a measurement.

## 14. Recommended law

**LAW-C with p = 0.50 - the linear-in-power constant-power crossfade:**

```
lateGain  = sqrt(1 - t)
earlyGain = sqrt(t) * K          K from the BAL-4 table
```

It is constant power, so it keeps everything LAW-A earns on decorrelated
material (0.03 dB on noise, 0.07 dB on a transient). It halves the worst sag on
normal musical material, 1.28 -> 0.61 dB, and removes it almost entirely on the
pad, 1.28 -> 0.08 dB. It is flat under any calibration error, which is the
condition that actually obtains. Its endpoints are exact without a special case.
It is the cheapest of the candidates as `sqrt`. And it needs no new machinery -
same two smoothers, same per-block target update.

**This is a change to the law's SHAPE, not to the architecture or to any
constant,** and it is a recommendation. Nothing was implemented.

## 15. Why the alternatives are rejected

- **LAW-A trigonometric** - correct on decorrelated material and measurably
  worse under calibration error, which is the normal condition rather than the
  exception: 1.28 dB of sag on a pad where p = 0.50 has 0.08 dB. Not wrong,
  simply dominated.
- **LAW-C p = 0.60 to 0.90** - the compromise hypothesis is refuted. Sag grows
  monotonically with the exponent on every source including the correlated ones,
  because no source's branches are correlated enough for amplitude addition to
  be the better model.
- **LAW-B linear amplitude** - worst of the family everywhere. 3.04 dB of
  mid-travel sag on broadband material, where constant power has 0.03 dB. It
  would turn Early/Late into something with an audible hole in the middle.

## 16. The 0.20 vs 0.40 listening pack

`renders/phase10-occupancy-ab/` - **four WAVs, two blind cases**, rendered by
`Scripts\export-phase10-occupancy-ab.ps1`.

| Case | Files | Material | Setting |
|---|---|---|---|
| **O1** | `caseO1-1.wav`, `caseO1-2.wav` | snare | Size 100, E/L 25, 100 % wet |
| **O2** | `caseO2-1.wav`, `caseO2-2.wav` | vocal | Size 100, E/L 50, 100 % wet |

Plus `LISTENING.md` with the five questions per case, and `KEY.txt` held
separately.

**Mapping randomised independently per case** and in no filename. **Level was
checked, not assumed:** the pairs differ by **0.00 dB** in integrated level, so
there is no loudness cue; peak differs slightly and that is the effect itself.

**O2 uses 100 % wet rather than the shipping Mix,** as a deliberate reading of
"the most revealing sensible Mix value": the question is about the reverb's
internal structure, and at Mix 30 % seventy per cent of what reaches the listener
is a dry vocal identical in both files. Burying the thing under test under
unchanged material makes the test less sensitive, not more realistic.

**This replaces the earlier eight-file set** in `renders/phase10-stage3-ab/`,
whose primary comparison was 0.70 against 0.20. 0.70 is no longer a structural
candidate.

Production `cellOccupancy` is untouched at 0.70. Neither 0.20 nor 0.40 is a
shipping candidate until these answers exist.

## 17. Commit

`71f6c1e` - "Phase 10: crossfade law research; final occupancy A/B pack".

---

**PHASE 10 IS NOT CLOSED.** Human occupancy listening remains the final
perceptual gate, and two recommendations are outstanding review: BAL-4 as the
calibration layer and p = 0.50 as the law shape. Neither is implemented.
