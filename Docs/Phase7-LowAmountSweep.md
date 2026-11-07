# Phase 7 — Default Low Damp amount sweep (TEST-ONLY)

**Answer: NO. Reducing the Low Damp default does not remove the remaining
Problem A error — it makes it monotonically worse.** The shipping 20 % is a
genuine **interior** optimum, and the worst band changes identity exactly
there. No amount reaches the ~2 % target, so per the study rules this stops
here and the normalisation architecture is **not** modified.

Implementation: `Tests/DampingLowAmountSweep.cpp` with the shared
`Tests/DampingSweepHarness.h`. Run with `AuroraVerbTests.exe LowAmount`.
6 / 6 checks pass. No production default, Source, mapping or Option A semantic
was changed. High Damp Freq 10 kHz and Low Damp Freq 200 Hz are provisional
study candidates, not shipped values.

Baseline faithfulness: the 20 % row reproduces the registered reference to
**0.004 pp** (500 Hz 2.25 %, 1 kHz 0.56 %, 2 kHz 2.46 %).

---

## PROBLEM A — the sweep runs the wrong way, and the minimum is where we are

Decay 2.5 s, High Damp 50 %, High Damp Freq 10 kHz, Low Damp Freq 200 Hz,
Mod Depth 30 %, Mod Rate 0.5 Hz. Both corners held fixed; amount only.

Deviation from base Decay, per cent:

| Low Damp | 60 Hz | 160 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | **worst** | at |
|---|---|---|---|---|---|---|---|---|
| 0.0 % | 1.19 | 1.53 | 1.33 | 0.67 | 1.40 | 3.99 | **3.99 %** | 2 kHz |
| 5.0 % | 3.69 | 3.96 | 2.68 | 0.61 | 0.74 | 3.20 | **3.20 %** | 2 kHz |
| 10.0 % | 8.89 | 6.88 | 4.46 | 1.10 | 0.60 | 2.89 | **2.89 %** | 2 kHz |
| 12.5 % | 11.47 | 8.39 | 5.34 | 1.37 | 0.57 | 2.77 | **2.77 %** | 2 kHz |
| 15.0 % | 13.93 | 9.85 | 6.26 | 1.65 | 0.55 | 2.66 | **2.66 %** | 2 kHz |
| 17.5 % | 16.35 | 11.26 | 7.18 | 1.94 | 0.55 | 2.56 | **2.56 %** | 2 kHz |
| **20.0 %** *(shipping)* | 18.85 | 12.60 | 8.13 | 2.25 | 0.56 | 2.46 | **2.46 %** | 2 kHz ← best |
| 22.5 % | 21.45 | 13.90 | 9.04 | 2.56 | 0.57 | 2.38 | **2.56 %** | **500 Hz** |
| 25.0 % | 23.96 | 15.24 | 9.95 | 2.88 | 0.60 | 2.30 | **2.88 %** | 500 Hz |
| 30.0 % | 28.57 | 18.06 | 11.79 | 3.52 | 0.67 | 2.16 | **3.52 %** | 500 Hz |
| 40.0 % | 36.97 | 23.71 | 15.25 | 4.85 | 0.87 | 1.91 | **4.85 %** | 500 Hz |

Target 2.0 %, pre-registered and unchanged.

### The minimum is interior, and the worst band changes identity at it

The first pass ran 0 to 20 % and found the best figure at 20 % — the top of the
range, improving monotonically toward it. That is the **third** edge-pinned
optimum this line of study has produced, which makes it a signature of the
coupling rather than a coincidence, so the range was extended upward.

It turns around immediately, and the mechanism is visible in the "at" column:

```
<= 20.0 %   the worst band is 2 kHz
   20.0 %   2 kHz 2.46 % against 500 Hz 2.25 %   <- crossover
>= 22.5 %   the worst band is 500 Hz
```

The worst-of-window metric is minimised where the two ends are level, and that
is at the shipping value. 17.5 % and 22.5 % both give 2.56 %.

### Why reducing the amount hurts 2 kHz

This was predicted before the run and is worth stating because it is not
obvious. With Low Damp 0 the low shelf is an identity, so `max|H_pair|` is
`max|H_high|`, which peaks at exactly 1 at DC, so the Option A normalisation
is exactly 1. With Low Damp 20 % the composite maximum sits slightly below 1,
so the normalisation is slightly **above** 1 and quietly lengthens the whole
mid and high region. Taking the amount away removes that lengthening, and
2 kHz gets shorter — deviation 2.46 % to 3.99 %.

The analytic `max|Hn|` confirms the mechanism is present and bounded: exactly
1.000000000000 at Low Damp 0, and 0.999999997 to 0.999999999 once any amount
is applied.

---

## Low-frequency function retention

Fraction of the shipping 20 % low-damping effect retained:

| Low Damp | 60 Hz | 80 Hz | 100 Hz | 125 Hz | 160 Hz | 250 Hz | mean |
|---|---|---|---|---|---|---|---|
| 0.0 % | 0.0 % | 0.0 % | 0.0 % | 0.0 % | 0.0 % | 0.0 % | **0.0 %** |
| 5.0 % | 24.4 % | 23.5 % | 22.0 % | 22.3 % | 21.9 % | 19.8 % | **22.3 %** |
| 10.0 % | 50.3 % | 49.4 % | 48.5 % | 49.4 % | 48.3 % | 46.0 % | **48.7 %** |
| 12.5 % | 63.1 % | 62.7 % | 62.4 % | 63.4 % | 62.0 % | 59.0 % | **62.1 %** |
| 15.0 % | 75.4 % | 75.0 % | 75.0 % | 76.3 % | 75.2 % | 72.5 % | **74.9 %** |
| 17.5 % | 87.5 % | 87.6 % | 87.5 % | 88.1 % | 87.9 % | 86.1 % | **87.5 %** |
| 20.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % | 100.0 % | **100.0 %** |
| 22.5 % | 113.0 % | 113.0 % | 113.1 % | 113.1 % | 111.8 % | 113.5 % | 112.9 % |
| 25.0 % | 125.5 % | 126.3 % | 126.2 % | 126.3 % | 123.9 % | 126.8 % | 125.8 % |
| 40.0 % | 190.4 % | 199.9 % | 205.0 % | 206.1 % | 200.5 % | 204.8 % | 201.1 % |

The control is close to linear in its effect — 10 % gives about half the
damping of 20 %, 15 % about three quarters — which is what the mapping
`ratio(p) = ratioAtFull^p` should produce and is a useful sanity result in its
own right.

RT60 in the bass, so the cost is readable in seconds:

| Low Damp | 60 Hz | 160 Hz | 250 Hz |
|---|---|---|---|
| 0.0 % | 2.5297 s | 2.4616 s | 2.4667 s |
| 5.0 % | 2.4077 s | 2.4010 s | 2.4330 s |
| 10.0 % | 2.2777 s | 2.3279 s | 2.3886 s |
| 12.5 % | 2.2133 s | 2.2902 s | 2.3665 s |
| 15.0 % | 2.1519 s | 2.2537 s | 2.3435 s |

A result worth recording on its own: **the shipping Low Damp 20 % costs 60 Hz
18.85 % of the requested decay** — the tail there runs about 2.03 s against a
2.5 s setting. That is a large, deliberate voicing decision that no previous
document stated in those terms.

---

## The shape of the default curve

| Low Damp | worst window | at | steepest slope (30 Hz – 16 kHz) | at | widest in-band | at | max\|Hn\| |
|---|---|---|---|---|---|---|---|
| 0.0 % | 3.99 % | 2 kHz | 0.4192 s/oct | 7461 Hz | 1.242 : 1 | 8620 Hz | 1.000000000000 |
| 10.0 % | 2.89 % | 2 kHz | 0.4277 s/oct | 7461 Hz | 1.245 : 1 | 8593 Hz | 0.999999997661 |
| 20.0 % | 2.46 % | 2 kHz | 0.4315 s/oct | 7437 Hz | 1.246 : 1 | 8593 Hz | 0.999999998932 |
| 25.0 % | 2.88 % | 500 Hz | 0.4332 s/oct | 7437 Hz | 1.246 : 1 | 8593 Hz | 0.999999998674 |
| 40.0 % | 4.85 % | 500 Hz | 0.4374 s/oct | 7437 Hz | 1.248 : 1 | 8593 Hz | 0.999999998143 |

The steepest slope moves by 4 % across the entire sweep (0.4192 to
0.4374 s/oct) and the widest in-band ratio by 0.5 % (1.242 to 1.248 : 1), and
both live at **7437 Hz and 8593 Hz** — on the high transition, not the low one.
A number that barely changes across a sweep is evidence that the swept control
is not where the curve's roughness comes from. **Low Damp amount is not a
smoothness lever**, and no amount makes the default curve smoother; the one
thing it does change is which end of the measurement window carries the error.

`max|Hn|` stays at or below 1 throughout, so Option A holds — damping
subtractive, decay never extended above base Decay.

---

## Low-side curvature — analytic first, ensemble only where it can tell

Following the previous sweep's finding that low-side differences sit at the
estimator noise floor (1.46 pp split-half against a 1.73 pp between-candidate
spread), the cheap analytic diagnostic ran for every amount and the
32-realisation ensemble for three points only.

Low Damp 100 %, High Damp 0 — the low section on its own. **These numbers
create no quality gate.**

| which | amount | 63 Hz | 80 Hz | 125 Hz | 180 Hz | 250 Hz | 355 Hz | 500 Hz | worst |
|---|---|---|---|---|---|---|---|---|---|
| 20 % baseline | 20.0 % | 4.61 | 1.60 | 2.81 | 0.39 | 1.24 | 1.20 | 0.73 | 4.61 % |
| 0 % control | 0.0 % | 2.61 | 1.79 | 0.33 | 0.78 | 0.19 | 1.08 | 1.67 | 2.61 % |

Reducing the amount does **not** move curvature into the bass; if anything the
low side gets slightly flatter. That is a clean negative result — the low
section is not where tail curvature comes from at any amount — and it does not
help, because the amounts that flatten it are the amounts that worsen
Problem A.

Note: in the first run the "best Problem A" row of this table printed the same
numbers as the baseline, because the best amount **is** the shipping default.
That is information, not a fault: there was no third point to measure.

---

## A fault I introduced, and the numbers it spoiled

When the sweep was extended upward, `kNumAmounts - 1` stopped being the
shipping index and a global substitution replaced it with a `shipping`
variable. The retention suite already had a **local `double` named
`shipping`**, and the substitution turned its declaration into a
self-initialisation — the variable used as its own array index. The compiler
accepted it. A second instance subtracted the integer index from an RT60
(`none - shipping`, with `shipping` being 6).

The result was a meaningless retention table, which printed 10 % as the 100 %
reference instead of 20 %, and a meaningless product-cost percentage. Both are
fixed, the local is renamed so the index cannot be shadowed again, and the code
records why. **The Problem A table was not affected** — the measurement grid
was filled correctly and the fault was confined to ratios computed from it —
but every number in this document comes from the re-run after the fix.

---

## Decision, against the four conditions

| condition | verdict |
|---|---|
| **A** 500 Hz – 2 kHz deviation reaches about 2 % | **FAILED by every amount.** Best is 2.46 % at the shipping 20 %, which is no change at all. The minimum is interior, so the sweep has nothing better to offer. |
| **B** low-frequency damping remains meaningfully useful | Only at or near 20 %. Reducing to 10 % keeps 48.7 % of the effect — and worsens Problem A to 2.89 %, so the question is moot. |
| **C** the full curve becomes smoother rather than moving the error | **FAILED.** The steepest slope and widest in-band ratio barely move (4 % and 0.5 % across the whole sweep) and both sit on the high transition. The only thing amount changes is which end of the window carries the error. |
| **D** Option A / stability unchanged | **MET** — `max|Hn|` at or below 1 at every amount, Option A and the mapping untouched. |

## Product cost in plain terms

**There is none, because there is no change to make.** The best amount is the
shipping default. Reducing Low Damp does not improve the Problem A figure, so
no retention is traded and no bass is given back.

---

## Pareto frontier

| Low Damp | Problem A worst | at | low-frequency retention | curve smoothness |
|---|---|---|---|---|
| 0.0 % | 3.99 % | 2 kHz | 0.0 % | 0.4192 s/oct |
| 5.0 % | 3.20 % | 2 kHz | 22.3 % | 0.4251 s/oct |
| 10.0 % | 2.89 % | 2 kHz | 48.7 % | 0.4277 s/oct |
| 12.5 % | 2.77 % | 2 kHz | 62.1 % | 0.4288 s/oct |
| 15.0 % | 2.66 % | 2 kHz | 74.9 % | 0.4298 s/oct |
| 17.5 % | 2.56 % | 2 kHz | 87.5 % | 0.4307 s/oct |
| **20.0 %** *(shipping)* | **2.46 %** | 2 kHz | **100.0 %** | 0.4315 s/oct |
| 22.5 % | 2.56 % | 500 Hz | 112.9 % | 0.4324 s/oct |
| 25.0 % | 2.88 % | 500 Hz | 125.8 % | 0.4332 s/oct |
| 30.0 % | 3.52 % | 500 Hz | 151.9 % | 0.4346 s/oct |
| 40.0 % | 4.85 % | 500 Hz | 201.1 % | 0.4374 s/oct |

As with Low Damp Freq, there is no frontier to trade along below 20 %: every
lower amount is worse on Problem A **and** worse on retention **and** no
smoother. Above 20 % retention rises but Problem A worsens, so 20 % is the
knee.

---

## What this closes, and what is still open

Three of the four default axes have now been swept:

| axis | shipping | optimum found | Problem A there | status |
|---|---|---|---|---|
| High Damp Freq | 6 kHz | **10 kHz** (a real improvement) | 7.90 % → 2.46 % | swept at 50 % amount |
| Low Damp Freq | 200 Hz | 200 Hz (already optimal) | 2.46 % | closed |
| Low Damp amount | 20 % | 20 % (already optimal) | 2.46 % | closed |
| **High Damp amount** | **50 %** | **not tested** | — | **OPEN** |

**No conclusion about the default space as a whole is drawn here, because one
axis is untested.** High Damp amount has not been swept, and it is the one
control that acts directly on the band where the error now sits.

There is a second reason not to close the question. Option A couples the
shelves through a single normalisation scalar, which is the mechanism behind
every result above — so the High Damp Freq optimum of 10 kHz was found **at
50 % amount** and is not guaranteed to stay optimal at a different amount. The
frequency and amount axes may have to be looked at together rather than one at
a time.

What is established about the low side is narrower and holds:

- the 500 Hz and 2 kHz ends of the measurement window cross over at about
  2.4 % under the three configurations swept so far, and the worst-of-window
  metric cannot fall below its own crossover value
- neither low-side control can lower both ends at once, so neither can move
  that crossover

Whether the **high-side** controls can is an open question, and until it is
answered a normalisation architecture change is not justified. This study does
not touch the architecture; folding such a change into a defaults sweep would
make neither attributable.

No production default change, no Source change, no mapping change, no Option A
change, no gate change. Multi-knee topology not tested. Modulation × damping-
corner regression untouched. Problem B carried forward unchanged: High Damp
Freq 10 kHz, worst measured mismatch 5.51 % at 14254 Hz, the 5 % investigation
gate still not passed. No install, no FL Studio, Phase 7 not complete.
