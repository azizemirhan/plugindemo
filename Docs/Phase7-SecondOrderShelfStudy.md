# Phase 7 — Second-order monotonic shelf study (TEST-ONLY)

**Verdict: NOT JUSTIFIED FOR PRODUCTION.** The candidate improves Problem A by
4.56 pp but does not reach the 2 % target, and it creates a severe new
Problem B regression — 12 kHz tail mismatch goes from 5.82 % to **29.34 %**.
Cost and stability are not the blockers.

Implementation: `Tests/DampingSecondOrder.cpp`. Run with
`AuroraVerbTests.exe SecondOrder`. Nothing in `Source/` was changed.

---

## The candidate

An RBJ-form shelving biquad per band, shelf slope **S = 1** — the steepest the
form reaches without overshoot. `S > 1` produces a resonant bump before the
plateau, which the attenuation-only requirement forbids, so S is fixed at 1.

Not a cascade of identical first-order sections. That approach (T1/T2) stays
rejected, and for a reason that matters here: a cascade buys steepness by
raising the whole pair to a power, which steepens the plateau approach as much
as the transition and drags the broad mid down with it. That is Problem A
itself, not a fix for it. A second-order shelf steepens the **transition**
while leaving both plateaus exactly where they were.

Three properties the study relies on come out of the form with no fitted
constant, and all three were measured rather than trusted:

| property | measured |
|---|---|
| high shelf exactly unity at DC | 8.33e-12 worst, at the cancellation floor |
| low shelf exactly unity at Nyquist | 4.66e-15 worst |
| corner at half the plateau attenuation in dB | 6.13e-9 dB worst |
| monotonic, no resonant boost | worst non-monotonic step 6.66e-16 |
| zero damping is a bit-exact identity | 0 failures |
| stable across the Phase 7 grid | largest pole radius 0.998150 |
| max normalised magnitude never above one | exactly 1.00000000000000 |
| certified bound never below attained max | gap 7.39e-4, never negative |

Grid: 720 combinations (Decay × Low Damp × High Damp × Low Hz × High Hz) × 8
lines, 360 transparent lines exercised for the identity path.

### Two tolerances in this file are the author's, not quality gates

The DC-unity and corner checks were first set at 1e-12 and 1e-9 dB and both
tripped, at 8.3e-12 and 6.1e-9. The cause is cancellation in the **evaluation**,
not error in the coefficients — `|H(DC)|` is `sum(b)/sum(a)` and the algebra
gives `4A(1-cos w0)` for both sums, so at a low corner each sum is formed from
terms far larger than the result. Measured, not argued:

| corner (Hz) | 1/(1-cos w0) | \|H(DC)\|-1 | ratio to 1e-16 |
|---|---|---|---|
| 20 | 291805 | 1.28e-10 | 1281931 |
| 50 | 46689 | 1.44e-11 | 143799 |
| 200 | 2918 | 9.70e-13 | 9703 |
| 1000 | 116.9 | 5.3e-15 | 53 |
| 6000 | 3.4 | 4e-16 | 4.4 |
| 18000 | 0.6 | 0 | 0 |

The error tracks the cancellation factor and vanishes entirely by 18 kHz. The
bounds were then set from that analysis (2e-11 and 1e-7 dB), not from the
observed numbers. No pre-registered gate was touched.

---

## Fairness of the comparison

Matched to production on everything except transition shape:

- identical requested plateau ratios, from `damping::shelfGainFor` on the same
  per-line loop delay, so the asymptotic low and high plateaus are the same
  gains in both topologies
- identical corner frequencies, with the corner meaning the same thing in both
- Option A maximum normalisation in both. For the candidate the maximum comes
  from its **own** certified bound — the endpoint-product interval method,
  which rests on monotonicity alone — not from production's closed form, which
  is specific to the first-order pole family and is not validated for this one
- High Damp was not weakened to pass Problem A; the amount is held and only
  selectivity is tested
- nothing was retuned after seeing a T20/T30 number

**Harness self-check.** The study's own T0 column must reproduce the
registered production T0 mismatches. The first run failed it at 1.19 pp,
because the excitation was a plain 64-sample burst with an impulse at
realisation 0 instead of the registered 1024-sample Hann-windowed,
RMS-equalised noise burst. With the registered excitation copied exactly the
drift is **0.18 pp** and the comparison stands.

---

## PROBLEM A — the default broad mid

Shipping defaults: Decay 2.5 s, Low Damp 20 %, High Damp 50 %, Low Damp Freq
200 Hz, High Damp Freq 6 kHz.

| topology | band | RT60 | predicted | deviation | \|T20-T30\| | R^2 | K spread |
|---|---|---|---|---|---|---|---|
| T0 | 500 Hz | 2.4773 | 2.4678 | 0.91 % | 0.92 % | 0.99988 | 0.12 % |
| T0 | 1 kHz | 2.4934 | 2.4902 | 0.26 % | 0.67 % | 0.99983 | 0.18 % |
| T0 | 2 kHz | 2.2981 | 2.3297 | **8.08 %** | 0.54 % | 0.99990 | 1.50 % |
| T0 | 4 kHz | 1.9754 | 1.9216 | 20.98 % | 1.21 % | 0.99993 | 3.85 % |
| T0 | 6 kHz | 1.7302 | 1.6379 | 30.79 % | 1.73 % | 0.99991 | 4.59 % |
| T0 | 12 kHz | 1.3918 | 1.3136 | 44.33 % | 1.20 % | 0.99983 | 3.72 % |
| S2 | 500 Hz | 2.4895 | 2.4838 | 0.42 % | 1.38 % | 0.99988 | 0.00 % |
| S2 | 1 kHz | 2.5198 | 2.4997 | 0.79 % | 0.91 % | 0.99984 | 0.00 % |
| S2 | 2 kHz | 2.4122 | 2.4578 | **3.51 %** | 0.41 % | 0.99992 | 0.04 % |
| S2 | 4 kHz | 2.1145 | 1.9642 | 15.42 % | 1.79 % | 0.99979 | 0.33 % |
| S2 | 6 kHz | 1.7795 | 1.3071 | 28.82 % | 5.10 % | 0.99853 | 0.00 % |
| S2 | 12 kHz | 1.0866 | 0.9012 | 56.54 % | 6.88 % | 0.99731 | 0.05 % |

Worst 500 Hz – 2 kHz deviation from base Decay:

```
T0       8.08 %
S2       3.51 %
target   2.00 %   (pre-registered, unchanged)
```

**S2 improves the broad mid by 4.56 pp and still misses the target by 1.51 pp.**
The improvement is real and it is concentrated exactly where the problem is —
at 2 kHz, where T0 loses 8 % of the requested decay and S2 loses 3.5 %. Per-line
coherence at 2 kHz also improves by a factor of 37, from 1.50 % to 0.04 %.

---

## PROBLEM B — extreme High Damp tail curvature

Static M0: Mod Depth 0, Decay 2.5 s, Low Damp 0, High Damp 100 %, High Damp
Freq 6 kHz. Validated fixed-Q ensemble analyser, 32 realisations.

| topology | band | RT60 | EDT | T20 | T30 | R^2 | \|T20-T30\| | K spread |
|---|---|---|---|---|---|---|---|---|
| T0 | 1 kHz | 2.3331 | 2.4939 | 2.2969 | 2.3261 | 0.99975 | 1.26 % | 3.76 % |
| T0 | 3 kHz | 1.7495 | 1.8417 | 1.6869 | 1.7304 | 0.99965 | 2.52 % | 17.85 % |
| T0 | 6 kHz | 1.2482 | 1.2503 | 1.1876 | 1.2388 | 0.99815 | 4.13 % | 25.28 % |
| T0 | 12 kHz | 0.8160 | 0.9391 | 0.7695 | 0.8171 | 0.99741 | 5.82 % | 22.75 % |
| S2 | 1 kHz | 2.4944 | 2.6526 | 2.4969 | 2.4813 | 0.99983 | 0.63 % | 0.19 % |
| S2 | 3 kHz | 2.0525 | 2.0215 | 1.9704 | 2.0400 | 0.99944 | 3.41 % | 8.21 % |
| S2 | 6 kHz | 1.5089 | 1.1178 | 1.3071 | 1.4632 | 0.99329 | **10.67 %** | 0.00 % |
| S2 | 12 kHz | 0.8360 | 0.5875 | 0.5479 | 0.7753 | **0.95514** | **29.34 %** | 0.99 % |

| band | T0 here | T0 registered | S2 here | S2 - T0 |
|---|---|---|---|---|
| 1 kHz | 1.26 % | 1.27 % | 0.63 % | -0.63 pp |
| 3 kHz | 2.52 % | 2.55 % | 3.41 % | +0.90 pp |
| 6 kHz | 4.13 % | 4.22 % | 10.67 % | **+6.53 pp** |
| 12 kHz | 5.82 % | 6.00 % | 29.34 % | **+23.51 pp** |

Investigation gate 5.0 %, unchanged. S2 fails it at 6 kHz and 12 kHz, and the
12 kHz fit itself stops being sound (R² 0.955 against the 0.995 requirement,
EDT collapsing from 0.939 s to 0.588 s).

---

## Why — and it falsifies the coherence hypothesis

This is the part worth keeping. **Per-line K spread improved dramatically and
the tail curvature got much worse.**

```
worst per-line K spread    T0 25.28 %    S2 9.66 %
12 kHz tail mismatch       T0  5.82 %    S2 29.34 %
```

So per-line coherence is **not** what drives the tail mismatch. The earlier
coherence work moved spread from 25.28 % to 0.99 % and the mismatch did not
follow; this study moves it the same way again, by a different mechanism, and
the mismatch moves hard in the wrong direction.

What does drive it is the steepness of the aggregate RT60(f) gradient:

| freq (Hz) | T0 RT60 | S2 RT60 | S2 - T0 | T0 spread | S2 spread |
|---|---|---|---|---|---|
| 250 | 2.4887 | 2.4999 | +0.0112 | 0.26 % | 0.00 % |
| 500 | 2.4556 | 2.4992 | +0.0436 | 1.02 % | 0.01 % |
| 1000 | 2.3339 | 2.4871 | +0.1533 | 3.76 % | 0.19 % |
| 2000 | 1.9708 | 2.3087 | +0.3379 | 11.32 % | 2.70 % |
| 3000 | 1.6090 | 1.7804 | +0.1713 | 17.85 % | 8.21 % |
| 4000 | 1.3268 | 1.1680 | -0.1588 | 22.03 % | 9.66 % |
| 6000 | 0.9792 | 0.5556 | -0.4236 | 25.28 % | 0.00 % |
| 8000 | 0.8001 | 0.3889 | -0.4112 | 25.24 % | 3.47 % |
| 12000 | 0.6417 | 0.3218 | -0.3198 | 22.75 % | 0.99 % |
| 16000 | 0.5804 | 0.3136 | -0.2669 | 20.46 % | 0.12 % |

S2 holds 2.31 s at 2 kHz and falls to 0.56 s at 6 kHz. T0 goes 1.97 s to
0.98 s over the same span. S2's gradient through the corner is roughly four
times steeper — which is exactly what "more selective" means and exactly what
fixed Problem A.

A one-octave analysis band at 6 kHz spans 4.2 to 8.5 kHz. Inside that single
band S2 holds RT60 from about 0.39 s to 1.17 s, a 3.0:1 range; T0 holds 0.80 s
to 1.33 s, 1.7:1. A sum of exponentials with a 3:1 spread of time constants is
visibly curved on an energy decay curve, and T20 and T30 read different slopes
off it. **The mismatch is a consequence of the selectivity, not an accident of
it.**

### The structural conclusion, bounded to what was tested

**Within the tested family — a single-corner monotonic shelf — increasing
transition selectivity is a direct tradeoff:**

```
more selectivity  ->  less broad-mid damage          (Problem A improves)
                  ->  steeper in-band RT60(f) gradient
                  ->  more tail curvature            (Problem B worsens)
```

Higher-order sharpness alone is therefore not a justified solution. Two points
on that family were measured — first order (T0) and second order with unit
slope (S2) — and the tradeoff runs the same way across both.

This claim is deliberately narrower than the evidence might invite. It is a
statement about one corner made sharper, not about filter order in general.
Nothing here rules out a different damping structure that lowers mid-band
damage by other means: a transition spread over more than one knee, a shaped
rather than monotone-shelf response, or a structure that varies per line.
Those were not tested and this study says nothing about them.

This does explain the two earlier dead ends in one sentence each. The T1/T2
cascade failed because it steepened the plateau approach along with the
transition, hurting Problem A directly. Residue equalisation failed because the
mismatch does not come from residue weighting at all — it comes from the spread
of RT60 inside the measurement band, which is set by the RT60(f) gradient.
---

## Cost and stability — not the blockers

| topology | one damper (ns/sample) | whole tank (ns/sample) |
|---|---|---|
| T0 | 4.891 | 102.868 |
| S2 | 5.869 | 112.447 |

```
damper cost ratio   S2 / T0   1.200x
tank cost ratio     S2 / T0   1.093x
```

A 9 % whole-tank cost for a second-order shelf pair per line is affordable, and
stability is comfortable across the whole grid (largest pole radius 0.998150,
`max|Hn|` exactly 1). If Problem B did not regress, cost would not stand in the
way.

Morphing cost was not measured: the candidate's coefficients are a function of
the same four controls and would smooth the same way, but a biquad cannot be
cross-faded by interpolating coefficients without checking the intermediate
states, and that question only matters if the topology is adopted.

---

## What this means for the next step

S2 is not justified for production as a drop-in replacement: it trades a 4.56 pp
Problem A gain for a 23.51 pp Problem B loss, and it does not reach the 2 %
Problem A target even so.

But the Problem A result is strong and the mechanism is now understood, which
points at what a working fix has to look like: it must reduce mid-band damage
**without** steepening the RT60(f) gradient inside any one analysis band.
Sharpening the single corner does the opposite, as measured. Untested
directions that could, in principle:

1. **Move the default corner rather than sharpen it.** T0 at a higher High Damp
   Freq default gives back the broad mid with no gradient change at all. This
   is a defaults question, not a topology question, and it is the cheapest
   thing left untested.
2. **Spread the transition over more than one corner** — a gentler gradient
   held over a wider span, which is the opposite of selectivity and would
   improve Problem B as well as Problem A.
3. **Accept the 6 % at High Damp 100 %** as the character of a maximum-damping
   setting, and gate on a less extreme operating point.

Options 1 and 3 need no DSP change at all.

No production Source change, no install, no FL Studio, no quality-gate change,
and Phase 7 is not complete.
