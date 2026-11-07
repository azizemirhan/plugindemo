# Phase 7 — Default High Damp amount sweep and joint Freq × Amount (TEST-ONLY)

**Answer: NO default pair reaches the ~2 % Problem A target.** The best is
**2.31 %** and the default space has a flat floor at 2.31–2.41 % along a ridge
in the (High Damp amount × High Damp Freq) plane. This is the first axis where
a figure **better than shipping** exists — 45 % gives 2.32 % against the 50 %
baseline's 2.46 % while retaining 90.6 % of the high-frequency damping — but
0.31 pp remains.

**This completes the default space.** All four axes and the local interaction
have now been tested, which is the condition the architecture decision gate was
waiting on.

Implementation: `Tests/DampingHighAmountSweep.cpp` with
`Tests/DampingSweepHarness.h`. Run with `AuroraVerbTests.exe HighAmount`.
9 / 9 checks pass. No production default, Source, mapping or Option A semantic
was changed.

Baseline faithfulness: the 50 % row reproduces the registered reference to
within 0.10 pp.

---

## PROBLEM A — the first interior minimum that beats shipping

Decay 2.5 s, Low Damp 20 %, Low Damp Freq 200 Hz, High Damp Freq 10 kHz,
Mod Depth 30 %, Mod Rate 0.5 Hz.

Deviation from base Decay, per cent:

| High Damp | 500 Hz | 1 kHz | 2 kHz | **worst** | at |
|---|---|---|---|---|---|
| 20.0 % | 2.81 | 0.54 | **0.50** | 2.81 % | 500 Hz |
| 25.0 % | 2.69 | 0.51 | 0.77 | 2.69 % | 500 Hz |
| 30.0 % | 2.59 | 0.49 | 1.06 | 2.59 % | 500 Hz |
| 35.0 % | 2.50 | 0.49 | 1.38 | 2.50 % | 500 Hz |
| 40.0 % | 2.41 | 0.50 | 1.72 | 2.41 % | 500 Hz |
| **45.0 %** | 2.32 | 0.53 | 2.08 | **2.32 %** | 500 Hz ← best |
| **50.0 %** *(shipping)* | 2.25 | 0.56 | 2.46 | 2.46 % | 2 kHz |
| 55.0 % | 2.17 | 0.60 | 2.86 | 2.86 % | 2 kHz |
| 60.0 % | 2.10 | 0.65 | 3.26 | 3.26 % | 2 kHz |
| 70.0 % | **1.98** | 0.76 | 4.08 | 4.08 % | 2 kHz |

Target 2.0 %, pre-registered and unchanged. The minimum is **interior** — a
`check` in the suite asserts it, after three previous sweeps each needed a
second run to escape an edge-pinned result. This sweep deliberately started
wider than the requested 25–60 % for that reason.

The same crossover as every other axis: at or below 45 % the worst band is
500 Hz, at or above 50 % it is 2 kHz, and the worst-of-window metric is
minimised near the crossing. Note 70 % brings 500 Hz to 1.98 % — under target
on its own — while 2 kHz goes to 4.08 %. Neither end can be fixed without
spending the other.

---

## High-frequency damping retention

Fraction of the shipping 50 % HF damping effect retained:

| High Damp | 6 kHz | 8 kHz | 10 kHz | 12 kHz | 16 kHz | mean | worst HF reduction |
|---|---|---|---|---|---|---|---|
| 20.0 % | 37.2 | 39.1 | 40.2 | 40.7 | 41.1 | **39.7 %** | 13.9 % |
| 30.0 % | 58.2 | 60.1 | 61.0 | 61.4 | 61.8 | **60.5 %** | 20.9 % |
| 40.0 % | 79.4 | 80.5 | 81.1 | 81.4 | 81.7 | **80.8 %** | 27.5 % |
| **45.0 %** | 89.7 | 90.4 | 90.7 | 90.9 | 91.1 | **90.6 %** | 30.7 % |
| 50.0 % *(shipping)* | 100.0 | 100.0 | 100.0 | 100.0 | 100.0 | **100.0 %** | 33.7 % |
| 70.0 % | 138.3 | 134.7 | 132.9 | 131.8 | 130.8 | **133.7 %** | 44.0 % |

**45 % buys a 0.14 pp Problem A improvement for 9.4 % of the high-frequency
damping function.** That is a cheap trade and it is the first one any sweep has
offered.

The trap the instruction warned about is visible: 20 % brings 2 kHz to 0.50 %,
the best mid-band figure anywhere in this study — and it does so by giving away
60 % of the HF damping, while pushing 500 Hz to 2.81 % so the window figure
gets *worse* anyway. Weakening the control into irrelevance does not even pass.

---

## Default curve smoothness

| High Damp | worst window | at | steepest slope | at | widest in-band | at | max\|Hn\| |
|---|---|---|---|---|---|---|---|
| 20.0 % | 2.81 % | 500 Hz | 0.2115 s/oct | 224 Hz | 1.096 : 1 | 209 Hz | 0.999999998017 |
| 30.0 % | 2.59 % | 500 Hz | 0.2767 s/oct | 8301 Hz | 1.133 : 1 | 8923 Hz | 0.999999998824 |
| 40.0 % | 2.41 % | 500 Hz | 0.3580 s/oct | 7870 Hz | 1.187 : 1 | 8756 Hz | 0.999999997876 |
| 45.0 % | 2.32 % | 500 Hz | 0.3958 s/oct | 7651 Hz | 1.216 : 1 | 8674 Hz | 0.999999998966 |
| 50.0 % | 2.46 % | 2 kHz | 0.4315 s/oct | 7437 Hz | 1.246 : 1 | 8593 Hz | 0.999999998932 |
| 70.0 % | 4.08 % | 2 kHz | 0.5532 s/oct | 6684 Hz | 1.374 : 1 | 8354 Hz | 0.999999998997 |

Unlike Low Damp amount — which moved these numbers by 4 % and 0.5 % across its
whole sweep — **High Damp amount moves them a great deal**: the steepest slope
runs 0.2115 to 0.5532 s/oct and the widest in-band ratio 1.096 to 1.374 : 1.
This control sets how far the high plateau falls, which is what the gradient is
a gradient of, so that is expected and it confirms the diagnostic is measuring
what it claims to.

At 20 % the steepest slope relocates to **224 Hz** — the low transition — which
is the point at which the high section has become gentler than the low one.

`max|Hn|` stays below 1 everywhere, so Option A holds throughout.

---

## Joint High Damp Freq × amount

Because Option A couples the shelves through one normalisation scalar, the
10 kHz frequency optimum was found at 50 % amount and could not be assumed to
survive a change of amount. The gate for running this was first written as a
literal "best ≤ 2.2 %", the sweep returned 2.32 %, and it was skipped — the
wrong reading. The reason the joint sweep exists is that the amount optimum
might move, and it had just moved to 45 %. The gate now states that intent:
run when the figure reaches 2.2 %, **or** when the amount optimum has moved off
the shipping default while improving on it.

| amount | freq | worst 500–2k | at | 8 kHz | 12 kHz | 16 kHz | slope | widest |
|---|---|---|---|---|---|---|---|---|
| 40 % | 9.0 kHz | **2.33 %** | 2 kHz | 0.794 | 0.729 | 0.697 | 0.3737 | 1.199 : 1 |
| 40 % | 9.5 kHz | 2.34 % | 500 Hz | 0.808 | 0.744 | 0.711 | 0.3659 | 1.193 : 1 |
| 40 % | 10.0 kHz | 2.41 % | 500 Hz | 0.821 | 0.757 | 0.725 | 0.3580 | 1.187 : 1 |
| 40 % | 12.0 kHz | 2.64 % | 500 Hz | 0.863 | 0.806 | 0.774 | 0.3260 | 1.164 : 1 |
| 45 % | 9.5 kHz | 2.41 % | 2 kHz | 0.785 | 0.714 | 0.679 | 0.4039 | 1.223 : 1 |
| 45 % | 10.0 kHz | **2.32 %** | 500 Hz | 0.799 | 0.729 | 0.693 | 0.3958 | 1.216 : 1 |
| 45 % | 10.5 kHz | 2.39 % | 500 Hz | 0.812 | 0.743 | 0.707 | 0.3876 | 1.209 : 1 |
| 50 % | 10.0 kHz | 2.46 % | 2 kHz | 0.777 | 0.702 | 0.663 | 0.4315 | 1.246 : 1 |
| **50 %** | **10.5 kHz** | **2.31 %** | 500 Hz | 0.791 | 0.717 | 0.678 | 0.4232 | 1.238 : 1 |
| 50 % | 11.0 kHz | 2.38 % | 500 Hz | 0.804 | 0.732 | 0.692 | 0.4147 | 1.231 : 1 |

### The ridge is flat, and 2.31 / 2.32 / 2.33 are the same number

Three different pairs give 2.31 %, 2.32 % and 2.33 %. **A 0.02 pp difference is
far below what this harness resolves** — the split-half control in the
low-corner study measured its own noise at 0.3 to 1.5 pp. Naming a winner on
that digit would be deciding on an unmeasurable difference, so the suite keeps
two candidates: the best Problem A pair, and the one with the narrowest in-band
RT60 ratio among all pairs within 0.10 pp of it.

```
best Problem A                     High Damp 50 % at 10.5 kHz   2.31 %
smoothest within 0.10 pp of it     High Damp 40 % at 10.0 kHz   2.41 %   in-band 1.187 : 1
```

The ridge trades smoothness against mid-band figure in a readable way: lower
amount with a lower corner is gentler (1.164–1.199 : 1) at essentially the same
Problem A, which matters because in-band RT60 spread is what the second-order
shelf study tied to tail curvature.

---

## Problem B at the final frequency candidates

Problem B depends on High Damp **Freq** but not on the shipping default
**amount**, because the registered extreme sets High Damp 100 % itself. So only
the final two frequency candidates were re-searched, with the worst band
searched rather than assumed (centres 500 Hz to 16 kHz, third-octave to 10 kHz
and sixth-octave above, each band one octave wide).

| freq | worst | at | interior? | R² there |
|---|---|---|---|---|
| **10.5 kHz** | 5.51 % | 15999 Hz | **at the top, so a lower bound** | 0.99807 |
| **10.0 kHz** | 5.51 % | 14254 Hz | **interior, so a real maximum** | 0.99807 |
| 10.0 kHz *(certified earlier)* | 5.51 % | 14254 Hz | interior | — |

Both give 5.51 %, which reproduces the certified result exactly and is a useful
independent confirmation of it. The difference is evidential, not numerical:
**10.0 kHz's figure is a measured maximum; 10.5 kHz's is pinned at the top of
what a one-octave band can reach and is therefore only a lower bound.** A
one-octave band centred at f spans f/√2 to f·√2, so no centre above about
16.6 kHz fits inside Nyquist — that is a physical limit of the gate's own
method, not a choice.

The 5 % investigation gate is unchanged and still not passed by any candidate.

---

## Decision

**Primary question — does any default pair reach ~2 % Problem A without
materially sacrificing the HF damping function? No.**

The floor of the default space is 2.31–2.41 % along a flat ridge. The best
single recommendation on the evidence is:

```
High Damp Freq    10.0 kHz   (not 10.5: its Problem B is a trustworthy maximum)
High Damp amount  45 %       (2.32 %, retaining 90.6 % of the HF damping)
Low Damp Freq     200 Hz     (interior optimum, unchanged)
Low Damp amount   20 %       (interior optimum, unchanged)
```

against the shipping 6 kHz / 50 % / 200 Hz / 20 %, which gives 7.90 %. That is
**7.90 % → 2.32 %**, a 5.58 pp improvement for 9.4 % of the HF damping effect
and no DSP change at all. It still misses the 2 % target by 0.32 pp.

| condition | verdict |
|---|---|
| **A** reaches about 2 % | **MISSED by 0.32 pp.** 2.32 % at the recommended pair; 2.31 % anywhere in the default space. |
| **B** HF damping stays meaningfully useful | **MET** — 90.6 % retained at 45 %, 30.7 % RT60 reduction at 16 kHz. |
| **C** the curve is smoother, not just passing at 2 kHz | **PARTLY** — 45 % is gentler than 50 % (slope 0.3958 against 0.4315, in-band 1.216 against 1.246 : 1). Lower amounts are gentler still but worse on Problem A. |
| **D** Option A / stability unchanged | **MET** — `max|Hn|` below 1 at every pair. |

---

## The architecture decision gate is now reached

Every default axis has been tested:

| axis | result | status |
|---|---|---|
| High Damp Freq | 6 → 10 kHz, 7.90 % → 2.46 % | large improvement, closed |
| Low Damp Freq | 200 Hz is an interior optimum | closed, no change |
| Low Damp amount | 20 % is an interior optimum | closed, no change |
| High Damp amount | 45 % gives 2.32 %, beats 50 %'s 2.46 % | small improvement, closed |
| High Damp Freq × amount | flat ridge, floor 2.31 % | closed |

So the conclusion the gate was waiting for can now be stated, and it is stated
no more broadly than the evidence allows:

> **Under the current topology and Option A composite-maximum normalisation,
> the remaining Problem A error is not reasonably solvable by shipping-default
> selection.** The default space has a measured floor of 2.31 % against a 2 %
> target, reached along a ridge rather than at a point, and every axis has been
> swept to an interior optimum.

The mechanism is the same in all five sweeps and is now well characterised: each
line is normalised by a **single scalar**, `1 / max|H_pair|`, so the low and high
sections are coupled; the 500 Hz and 2 kHz ends of the measurement window always
cross over, and the worst-of-window metric cannot fall below its own crossover
value. Lowering it requires lowering **both** ends at once, which no
single-scalar arrangement does.

An Option A normalisation architecture study is therefore now justified — and
that is the only thing this document concludes about it. Nothing here says what
such a change should be, or that it would work.

No production default change, no Source change, no mapping change, no Option A
change, no gate change. Multi-knee topology not tested. Modulation × damping-
corner regression untouched. No install, no FL Studio, Phase 7 not complete.
