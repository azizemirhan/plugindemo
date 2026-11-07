# Phase 7 — Default Low Damp Freq sweep (TEST-ONLY)

**Answer: NO. Lowering Low Damp Freq does not remove the remaining Problem A
error — it increases it.** The shipping 200 Hz is already the optimum, and the
sweep proves it is a genuine **interior** minimum rather than the edge of the
range. No frequency reaches the ~2 % target, so per the study rules this stops
here and Low Damp **amount** is not touched.

Implementation: `Tests/DampingLowCornerSweep.cpp` with the shared
`Tests/DampingSweepHarness.h`. Run with `AuroraVerbTests.exe LowCorner`.
8 / 8 checks pass. No production default, Source, mapping or gate was changed.
High Damp Freq 10 kHz is a provisional study candidate throughout, not a
shipped value.

---

## Two controls that had to pass first

**The extracted harness is faithful.** The harness was moved into a shared
header for this study, so the shipping baseline point was measured again and
compared against what the corner sweep recorded for the identical
configuration:

| band | here | corner sweep | drift |
|---|---|---|---|
| 500 Hz | 2.25 % | 2.25 % | 0.004 pp |
| 1 kHz | 0.56 % | 0.56 % | 0.003 pp |
| 2 kHz | 2.46 % | 2.46 % | 0.004 pp |

**Low Damp Freq cannot touch the Problem B test — proved exactly, not within a
tolerance.** The registered Problem B test uses Low Damp 0, and
`shelfGainFor` returns exactly 1.0 for `ratio >= 1`, so `gainLow` is exactly 1,
the low shelf is an identity and `coeffLow` never reaches the output.

```
samples compared                    96000
worst sample difference             0.000000000000000000
gainLow at Low Damp 0               1.000000000000000000
```

The 80 Hz and 200 Hz low corners give a **bit-identical** render at Low Damp 0.
The certified 10 kHz Problem B result — worst measured mismatch 5.51 % at
14254 Hz, interior to the valid one-octave range — is therefore reused for
every candidate instead of re-running the suite ten times. The 5 %
investigation gate is unchanged and still not passed.

---

## PROBLEM A — the sweep runs the wrong way

Decay 2.5 s, Low Damp 20 %, High Damp 50 %, High Damp Freq 10 kHz, Mod Depth
30 %, Mod Rate 0.5 Hz. Low Damp amount unchanged.

RT60 in seconds, base Decay 2.50 s:

| low corner | 60 Hz | 80 Hz | 100 Hz | 125 Hz | 160 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 8 kHz | 12 kHz | 16 kHz |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 80 Hz | 2.1826 | 2.2385 | 2.2824 | 2.3235 | 2.3649 | 2.4265 | 2.4855 | 2.4800 | 2.4173 | 2.2419 | 1.9255 | 1.7406 | 1.6445 |
| 100 Hz | 2.1313 | 2.1853 | 2.2297 | 2.2771 | 2.3272 | 2.4060 | 2.4810 | 2.4823 | 2.4212 | 2.2459 | 1.9285 | 1.7431 | 1.6467 |
| 125 Hz | 2.0885 | 2.1347 | 2.1800 | 2.2295 | 2.2828 | 2.3792 | 2.4736 | 2.4844 | 2.4259 | 2.2507 | 1.9322 | 1.7461 | 1.6494 |
| 150 Hz | 2.0599 | 2.0987 | 2.1415 | 2.1915 | 2.2442 | 2.3515 | 2.4647 | 2.4857 | 2.4303 | 2.2555 | 1.9359 | 1.7492 | 1.6521 |
| 175 Hz | 2.0410 | 2.0724 | 2.1121 | 2.1625 | 2.2116 | 2.3238 | 2.4548 | 2.4862 | 2.4345 | 2.2601 | 1.9396 | 1.7522 | 1.6549 |
| 200 Hz | 2.0286 | 2.0531 | 2.0883 | 2.1379 | 2.1851 | 2.2968 | 2.4439 | 2.4861 | 2.4384 | 2.2646 | 1.9432 | 1.7552 | 1.6575 |

Deviation from base Decay, per cent:

| low corner | 250 Hz | 500 Hz | 1 kHz | 2 kHz | **worst 500 Hz – 2 kHz** |
|---|---|---|---|---|---|
| 80 Hz | 2.94 | 0.58 | 0.80 | 3.31 | **3.31 %** |
| 100 Hz | 3.76 | 0.76 | 0.71 | 3.15 | **3.15 %** |
| 125 Hz | 4.83 | 1.06 | 0.62 | 2.97 | **2.97 %** |
| 150 Hz | 5.94 | 1.41 | 0.57 | 2.79 | **2.79 %** |
| 175 Hz | 7.05 | 1.81 | 0.55 | 2.62 | **2.62 %** |
| **200 Hz** *(shipping)* | 8.13 | 2.25 | 0.56 | 2.46 | **2.46 %** ← best |
| 225 Hz | 9.12 | 2.71 | 0.59 | 2.32 | **2.71 %** |
| 250 Hz | 10.03 | 3.18 | 0.64 | 2.18 | **3.18 %** |
| 300 Hz | 11.67 | 4.14 | 0.81 | 1.93 | **4.14 %** |
| 400 Hz | 13.95 | 5.98 | 1.30 | 1.53 | **5.98 %** |

Target 2.0 %, pre-registered and unchanged.

### The minimum is interior, and it is where we already are

The first pass ran 80 to 200 Hz and found the best figure at 200 Hz — the
**top** of the range, improving monotonically toward it. That is an
edge-pinned optimum and cannot be reported as an optimum; it is the same fault
the High Damp sweep caught, where candidates' worst bands sat on the last
centre of the ladder. The range was therefore extended upward until the figure
turned around.

It turns around immediately: 175 Hz 2.62 %, **200 Hz 2.46 %**, 225 Hz 2.71 %.
The minimum is interior and it sits on the shipping value.

The reason is a crossover inside the measurement window. At 200 Hz the worst
band is 2 kHz (2.46 % against 500 Hz's 2.25 %); at 225 Hz it is 500 Hz
(2.71 % against 2 kHz's 2.32 %). The worst-of-window metric is minimised where
the two are level, which is between 200 and 225 Hz and within measurement
distance of 200 Hz itself.

### Why lowering the corner hurts 2 kHz — the two corners are coupled

Lowering the low corner does what it should at the bottom: 60 Hz RT60 goes from
2.0286 s to 2.1826 s, less low damping, and 500 Hz deviation falls from 2.25 %
to 0.58 %.

But it makes **everything above 500 Hz slightly shorter**: 2 kHz 2.4384 →
2.4173 s, 16 kHz 1.6575 → 1.6445 s. A global shift of about 0.9 %, in the band
where the metric is measured, which is why the worst figure rises.

That is Option A's composite-maximum normalisation. Each line is normalised by
a **single scalar**, `1 / max|H_pair|`, and moving either corner changes where
and how high that maximum sits — so it changes the level of the entire
mid and high region. The analytic `max|Hn|` confirms the mechanism is active
and bounded: it runs 0.999999998 to 0.999999999 across the sweep.

This is the mirror image of the High Damp Freq finding, where raising the high
corner pulled 500 Hz **down** from 1.57 % to 2.49 %. **Neither corner can be
moved without shifting the other end of the band.** The structural statement,
bounded to what was tested: under Option A composite-maximum normalisation with
a single-corner shelf per band, the low and high corner defaults are coupled
through the normalisation scalar, so the 500 Hz – 2 kHz window cannot be
optimised one corner at a time.

---

## Low-frequency damping retention

Reference RT60 with Low Damp 0, so the low shelf is an identity:

```
60 Hz 2.530    80 Hz 2.493    100 Hz 2.469    125 Hz 2.462    160 Hz 2.462    250 Hz 2.467
```

Fraction of the shipping 200 Hz low-damping effect retained:

| low corner | 60 Hz | 80 Hz | 100 Hz | 125 Hz | 160 Hz | 250 Hz | mean |
|---|---|---|---|---|---|---|---|
| 80 Hz | 69.3 % | 57.9 % | 49.0 % | 42.8 % | 35.0 % | 23.6 % | **46.2 %** |
| 100 Hz | 79.5 % | 70.0 % | 62.8 % | 57.1 % | 48.6 % | 35.7 % | 59.0 % |
| 125 Hz | 88.1 % | 81.5 % | 75.9 % | 71.8 % | 64.7 % | 51.5 % | 72.2 % |
| 150 Hz | 93.8 % | 89.7 % | 86.0 % | 83.5 % | 78.6 % | 67.8 % | 83.2 % |
| 175 Hz | 97.5 % | 95.6 % | 93.7 % | 92.4 % | 90.4 % | 84.1 % | 92.3 % |
| 200 Hz | 100.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % |

No retention threshold is proposed. The tradeoff is simply that an 80 Hz corner
keeps under half the low-damping function the shipping default provides, and it
buys a **worse** Problem A figure for it.

---

## Low-side curvature — and the measurement cannot resolve it

Mod Depth 0, Decay 2.5 s, Low Damp 100 %, High Damp 0 — the low-side mirror of
the registered High-Damp extreme. **These numbers create no quality gate.**

T20/T30 mismatch, per cent:

| low corner | 63 Hz | 80 Hz | 125 Hz | 180 Hz | 250 Hz | 355 Hz | 500 Hz | WORST | at |
|---|---|---|---|---|---|---|---|---|---|
| 80 Hz | 2.76 | 5.13 | 3.45 | 0.09 | 1.41 | 0.21 | 0.83 | 5.13 | 80 Hz |
| 100 Hz | 0.81 | 4.13 | 2.60 | 2.05 | 0.27 | 1.61 | 0.20 | 4.13 | 80 Hz |
| 125 Hz | 0.40 | 2.09 | 4.95 | 4.32 | 0.89 | 0.06 | 0.56 | 4.95 | 125 Hz |
| 150 Hz | 2.50 | 0.87 | 6.86 | 2.93 | 1.24 | 0.29 | 1.58 | 6.86 | 125 Hz |
| 175 Hz | 2.66 | 4.56 | 2.68 | 3.81 | 2.25 | 1.70 | 0.75 | 4.56 | 80 Hz |
| 200 Hz | 1.00 | 2.68 | 3.38 | 5.13 | 2.00 | 1.04 | 0.95 | 5.13 | 180 Hz |

Change against the 200 Hz shipping corner: 80 Hz +0.01, 100 Hz −0.99,
125 Hz −0.18, 150 Hz +1.73, 175 Hz −0.56, 225 Hz +0.29, 250 Hz −0.82,
300 Hz −1.70, 400 Hz +1.72 pp. No trend.

### The noise floor, measured rather than asserted

The analytic gradient is all but identical across candidates — slope 0.6398 to
0.6406 s/oct, in-band ratio 1.548 to 1.550, a 0.13 % spread — so the low
transition is **translating** in log frequency and not changing shape. If
in-band RT60 spread drives curvature, as the second-order shelf study
established, the curvature must be essentially the same for every candidate and
the scatter above is variance.

That is a claim about noise, so the noise was measured. Same tank, same
settings, realisations 0–15 against 16–31:

| band | first 16 | second 16 | difference |
|---|---|---|---|
| 63 Hz | 1.19 % | 0.86 % | 0.33 pp |
| 80 Hz | 2.88 % | 2.46 % | 0.41 pp |
| 125 Hz | 2.69 % | 3.00 % | 0.31 pp |
| 180 Hz | 4.03 % | 3.13 % | 0.90 pp |
| 250 Hz | 2.00 % | 1.46 % | 0.54 pp |
| 355 Hz | 0.51 % | 1.97 % | 1.46 pp |
| 500 Hz | 0.75 % | 1.08 % | 0.33 pp |

```
worst split-half difference        1.46 pp
worst between-candidate difference 1.73 pp
```

**The noise is the same size as the effect.** The between-candidate differences
carry no information. The honest statement is therefore: no candidate shows a
low-side curvature regression **above the noise floor of this measurement**,
and equally, differences below about 1.5 pp at these frequencies cannot be
resolved by a 32-realisation ensemble. Low-frequency bands hold few modes, and
that is the price.

---

## Gradient diagnostic

Analytic aggregate RT60(f), 30 Hz to 1 kHz, 2000 points. Slope in seconds per
octave because the analysis bands are constant-Q.

At Low Damp 20 %, the default operating point:

| low corner | worst slope | at | worst in-band ratio | at | max\|Hn\| |
|---|---|---|---|---|---|
| 80 Hz | 0.2117 s/oct | 90 Hz | 1.097 : 1 | 84 Hz | 0.999999998456 |
| 125 Hz | 0.2126 s/oct | 140 Hz | 1.097 : 1 | 131 Hz | 0.999999998192 |
| 200 Hz | 0.2138 s/oct | 224 Hz | 1.097 : 1 | 209 Hz | 0.999999998932 |

At Low Damp 100 %, the curvature diagnostic point:

| low corner | worst slope | at | worst in-band ratio | at | max\|Hn\| |
|---|---|---|---|---|---|
| 80 Hz | 0.6406 s/oct | 133 Hz | 1.550 : 1 | 93 Hz | 0.999999999 |
| 125 Hz | 0.6403 s/oct | 207 Hz | 1.549 : 1 | 146 Hz | 0.999999999 |
| 200 Hz | 0.6398 s/oct | 332 Hz | 1.548 : 1 | 233 Hz | 0.999999999 |

Two things to read off this. The in-band ratio is **flat** — 1.548 to 1.550 —
which is the prediction that the measured curvature cannot differ between
candidates, confirmed above by the split-half control. And at the default
20 % operating point the low side's in-band ratio is only **1.097 : 1**,
against the high side's 1.561 : 1 at the 10 kHz corner. The low transition is
far gentler than the high one, which is why there is no low-side Problem B to
find in the first place.

`max|Hn|` stays below 1 everywhere, so Option A holds — damping subtractive,
decay never extended above base Decay.

---

## Decision, against the four conditions

| condition | verdict |
|---|---|
| **A** 500 Hz – 2 kHz deviation reaches about 2 % | **FAILED by every candidate.** Best is 2.46 % at the shipping 200 Hz, which is no change at all. The minimum is interior, so the sweep has nothing better to offer. |
| **B** retains a meaningful low-frequency damping function | Met only by candidates near 200 Hz. An 80 Hz corner keeps 46.2 % of the shipping effect — and buys a worse Problem A figure for it, so the question is moot. |
| **C** no material new low-frequency curvature regression | **Not resolvable.** No candidate regresses above a 1.46 pp noise floor, and the analytic in-band ratio is flat across the sweep, so none is expected to. |
| **D** stability and Option A semantics unchanged | **MET** — `max|Hn|` below 1 at every candidate, topology and mapping untouched. |

Condition A fails for the whole sweep, so this study stops. Low Damp **amount**
is not touched: amount and frequency in one study would make the result
unattributable.

---

## Pareto frontier

| low corner | Problem A worst | low-damping retention (mean) | low-side curvature vs 200 Hz |
|---|---|---|---|
| 80 Hz | 3.31 % | 46.2 % | +0.01 pp (within noise) |
| 100 Hz | 3.15 % | 59.0 % | −0.99 pp (within noise) |
| 125 Hz | 2.97 % | 72.2 % | −0.18 pp (within noise) |
| 150 Hz | 2.79 % | 83.2 % | +1.73 pp (within noise) |
| 175 Hz | 2.62 % | 92.3 % | −0.56 pp (within noise) |
| **200 Hz** *(shipping)* | **2.46 %** | **100.0 %** | **baseline** |
| 225 Hz | 2.71 % | > 100 % | +0.29 pp (within noise) |
| 250 Hz | 3.18 % | > 100 % | −0.82 pp (within noise) |
| 300 Hz | 4.14 % | > 100 % | −1.70 pp (within noise) |
| 400 Hz | 5.98 % | > 100 % | +1.72 pp (within noise) |

There is no frontier to trade along. **200 Hz dominates on every axis
simultaneously** — best Problem A figure, full retention of the shipping
low-damping function, and no resolvable curvature difference. Every other
candidate is worse on Problem A *and* worse on retention, or worse on Problem A
while damping the bottom harder than anyone asked for.

That is an unusual and useful result: the shipping Low Damp Freq is not a
compromise that can be improved by moving it. It is already at the optimum of
its own axis.

---

## What is left

Problem A stands at **2.46 %** against a 2 % target with High Damp Freq at the
provisional 10 kHz and Low Damp Freq at the shipping 200 Hz. The remaining
0.46 pp cannot be removed by either corner frequency:

- the high corner's own sweep bottomed out at 2.31 % (10.5 kHz) and 2.46 %
  (10 kHz), limited by 500 Hz
- the low corner's sweep bottoms out at 2.46 % at 200 Hz, limited by 2 kHz
- the two are coupled through the Option A normalisation scalar, so the window
  cannot be optimised one corner at a time

The untested levers are therefore **Low Damp amount** — 20 % is what costs
500 Hz its 2.25 % — and the normalisation coupling itself. Both are decisions
to be taken deliberately rather than folded into a frequency study.

No production default change, no Source change, no mapping change, no gate
change. Low Damp amount untouched. Multi-knee topology not tested. Modulation ×
damping-corner regression untouched. No install, no FL Studio, Phase 7 not
complete.
