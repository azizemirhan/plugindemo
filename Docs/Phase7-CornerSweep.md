# Phase 7 — Default High Damp Freq sweep (TEST-ONLY)

**Verdict: the corner alone does not reach the Problem A target, but it is the
best lever found so far and it improves Problem B rather than regressing it.**
Best candidate **10.5 kHz: 2.31 %** against a ~2 % target — missed by 0.31 pp.
Three of the four decision conditions hold; condition A is marginally missed,
and the remaining gap is a **low-end** limit, not a high-corner one.

Implementation: `Tests/DampingCornerSweep.cpp`. Run with
`AuroraVerbTests.exe CornerSweep`. No production default, Source, mapping or
gate was changed.

**The engine is the production one.** `FdnCore<8>` with `core.setDamping` and
`architecture::applyWithDepth` — the same calls the processor makes, with the
same mappings. There is no test-local reimplementation of the tank or the
damper in this file, so a number here is a number the shipping engine produces.
Cross-check: at the 6 kHz shipping corner this harness returns 1.26 / 2.52 /
4.13 / 5.82 % at 1/3/6/12 kHz, against the registered 1.27 / 2.55 / 4.22 /
6.00 % measured through a separate reimplementation. Two independent
implementations agree.

**Problem A is measured at the shipping Mod Depth (30 %, 0.5 Hz), not at zero.**
Problem A is a complaint about the product as it ships, so it is measured in
the state it ships in. Problem B stays at the registered static M0 point.

---

## PROBLEM A — the default broad mid

Decay 2.5 s, Low Damp 20 %, High Damp 50 %, Low Damp Freq 200 Hz, Mod Depth
30 %, Mod Rate 0.5 Hz. High Damp amount is not changed; only the corner moves.

RT60 in seconds, base Decay 2.50 s:

| corner | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 6 kHz | 8 kHz | 12 kHz | 16 kHz |
|---|---|---|---|---|---|---|---|---|
| 6.0 kHz | 2.4608 | 2.4594 | 2.3025 | 1.9723 | 1.7239 | 1.5668 | 1.3965 | 1.3260 |
| 7.0 kHz | 2.4562 | 2.4721 | 2.3552 | 2.0729 | 1.8389 | 1.6780 | 1.4947 | 1.4135 |
| 8.0 kHz | 2.4516 | 2.4795 | 2.3920 | 2.1517 | 1.9351 | 1.7784 | 1.5875 | 1.4988 |
| 9.0 kHz | 2.4475 | 2.4838 | 2.4186 | 2.2138 | 2.0167 | 1.8665 | 1.6744 | 1.5802 |
| 10.0 kHz | 2.4439 | 2.4861 | 2.4384 | 2.2646 | 2.0852 | 1.9432 | 1.7552 | 1.6575 |
| 12.0 kHz | 2.4377 | 2.4876 | 2.4645 | 2.3381 | 2.1927 | 2.0701 | 1.8963 | 1.7996 |

Deviation from base Decay, per cent:

| corner | 500 Hz | 1 kHz | 2 kHz | **worst 500 Hz – 2 kHz** |
|---|---|---|---|---|
| 6.0 kHz *(shipping)* | 1.57 | 1.63 | 7.90 | **7.90 %** |
| 7.0 kHz | 1.75 | 1.11 | 5.79 | **5.79 %** |
| 8.0 kHz | 1.93 | 0.82 | 4.32 | **4.32 %** |
| 9.0 kHz | 2.10 | 0.65 | 3.26 | **3.26 %** |
| 10.0 kHz | 2.25 | 0.56 | 2.46 | **2.46 %** |
| **10.5 kHz** | 2.31 | 0.53 | 2.14 | **2.31 %** ← best |
| 11.0 kHz | 2.38 | 0.51 | 1.87 | **2.38 %** |
| 12.0 kHz | 2.49 | 0.49 | 1.42 | **2.49 %** |

Target 2.0 %, pre-registered and unchanged.

### The limit is the low end, not the corner

The worst band **changes identity** between 10 and 12 kHz. As the corner rises,
2 kHz keeps recovering — 7.90 % down to 1.42 % — while 500 Hz drifts steadily
the other way, 1.57 % up to 2.49 %. The two cross at about 10.5 kHz, and from
there the worst figure is set by 500 Hz and gets worse, not better.

So the sweep has a genuine interior minimum at **10.5 kHz, 2.31 %**, and no
amount of further corner movement improves it. The remaining 0.31 pp is a
property of Low Damp 20 % at a 200 Hz corner, interacting with Option A's
composite-maximum normalisation — a different control, untested here.

That reframes the problem usefully: **the high corner can take Problem A from
7.90 % to 2.31 %, and the last 0.31 pp belongs to the low end.**

---

## HF damping retention — the tradeoff is explicit

A candidate that preserves 2 kHz only because the damping has stopped doing
anything is not a solution, so what remains up top is reported next to the
mid-band gain.

RT60 / base Decay, so 1.00 means no damping at all:

| corner | 6 kHz | 8 kHz | 12 kHz | 16 kHz | worst HF reduction |
|---|---|---|---|---|---|
| 6.0 kHz *(shipping)* | 0.690 | 0.627 | 0.559 | 0.530 | **47.0 %** |
| 8.0 kHz | 0.774 | 0.711 | 0.635 | 0.600 | 40.0 % |
| 10.0 kHz | 0.834 | 0.777 | 0.702 | 0.663 | 33.7 % |
| **10.5 kHz** | 0.846 | 0.791 | 0.717 | 0.678 | **32.2 %** |
| 11.0 kHz | 0.857 | 0.804 | 0.732 | 0.692 | 30.8 % |
| 12.0 kHz | 0.877 | 0.828 | 0.759 | 0.720 | 28.0 % |

At 10.5 kHz the damping still pulls 16 kHz from 2.50 s to 1.70 s — a 32.2 %
reduction. That is not a control that has stopped working, so condition B is
comfortably met. It is, though, about **two thirds** of the high-frequency
effect the 6 kHz default delivers (32.2 % against 47.0 %), and that is the
price. One control is doing both jobs, so the corner that gives the broad mid
back is the same corner that gives up high-frequency damping.

---

## PROBLEM B — the worst band searched, not assumed

Static extreme at each candidate corner: Mod Depth 0, Decay 2.5 s, Low Damp 0,
High Damp 100 %.

Band centres run 500 Hz to 16 kHz, 17 of them, third-octave spacing to 10 kHz
and **sixth-octave above** it, each band one octave wide as the gate specifies.

| corner | 1 kHz | 3 kHz | 6 kHz | 12 kHz | 16 kHz | **WORST** | at | interior? |
|---|---|---|---|---|---|---|---|---|
| 6.0 kHz *(shipping)* | 1.26 | 2.52 | 4.13 | 5.82 | 5.45 | **6.43** | 11313 Hz | interior |
| 7.0 kHz | 0.71 | 2.56 | 4.02 | 5.60 | 5.64 | **6.13** | 14254 Hz | interior |
| 8.0 kHz | 0.34 | 1.68 | 3.16 | 5.71 | 5.59 | **6.10** | 14254 Hz | interior |
| 9.0 kHz | 0.76 | 0.91 | 2.48 | 5.47 | 6.06 | **6.07** | 15999 Hz | **at the top** |
| 10.0 kHz | 0.45 | 0.06 | 2.16 | 4.29 | 5.28 | **5.51** | 14254 Hz | interior |
| 10.5 kHz | 0.12 | 0.18 | 2.33 | 4.15 | 5.51 | **5.51** | 15999 Hz | **at the top** |
| 11.0 kHz | 0.10 | 0.03 | 2.14 | 4.03 | 5.39 | **5.39** | 15999 Hz | **at the top** |
| 12.0 kHz | 0.34 | 0.87 | 2.22 | 3.63 | 4.15 | **4.15** | 15999 Hz | **at the top** |

Investigation gate 5.0 %, pre-registered and unchanged.

Full fits at each candidate's worst band:

| corner | band | RT60 | EDT | T20 | T30 | R^2 | mismatch |
|---|---|---|---|---|---|---|---|
| 6.0 kHz | 11313 Hz | 0.8437 | 0.9561 | 0.7933 | 0.8478 | 0.99726 | 6.43 % |
| 7.0 kHz | 14254 Hz | 0.8608 | 0.9776 | 0.8141 | 0.8673 | 0.99738 | 6.13 % |
| 8.0 kHz | 14254 Hz | 0.9652 | 1.0834 | 0.9103 | 0.9694 | 0.99742 | 6.10 % |
| 9.0 kHz | 15999 Hz | 1.0207 | 1.1250 | 0.9638 | 1.0260 | 0.99759 | 6.07 % |
| 10.0 kHz | 14254 Hz | 1.1572 | 1.2220 | 1.0979 | 1.1619 | 0.99807 | 5.51 % |
| 10.5 kHz | 15999 Hz | 1.1565 | 1.2236 | 1.0974 | 1.1614 | 0.99807 | 5.51 % |
| 11.0 kHz | 15999 Hz | 1.2001 | 1.2496 | 1.1353 | 1.2000 | 0.99824 | 5.39 % |
| 12.0 kHz | 15999 Hz | 1.2835 | 1.3387 | 1.2264 | 1.2795 | 0.99863 | 4.15 % |

### Correction to a registered figure

**The shipping 6 kHz default's worst band is not 12 kHz.** It is 11313 Hz, and
the mismatch there is **6.43 %**, not the registered 6.00 % at 12 kHz. The
registered number was taken at a band that is not the worst one, so it
understated the problem by 0.43 pp. This is a measurement correction, not a
gate change: the 5 % investigation gate is untouched.

Searching rather than assuming changed **every** candidate's number upward
relative to a coarse third-octave ladder (6 kHz went 6.10 → 6.43, 7 kHz 5.89 →
6.13, 10 kHz 5.28 → 5.51), which is the direct evidence that the search was
necessary.

### An honest limitation

For four candidates — 9, 10.5, 11 and 12 kHz — the worst measurable band sits
at **15999 Hz, the top of the range**. This is a physical limit, not a choice:
a one-octave band centred at f spans f/√2 to f·√2, so the highest centre whose
upper edge stays inside Nyquist is about 16.6 kHz. Above that there is no
one-octave band left to measure with.

For those four candidates the true worst mismatch **may lie above 16 kHz and is
not measurable by the gate's own method.** Their reported figures — including
the 12 kHz candidate's attractive 4.15 % — are therefore lower bounds on the
worst case, not the worst case. The 10 kHz candidate is the strongest in this
respect: its worst is **interior** at 14254 Hz, so 5.51 % is a real maximum.

---

## Gradient diagnostic

Analytic aggregate RT60(f), 2000 points from 100 Hz to 20 kHz. The slope is in
seconds per **octave** because the analysis bands are constant-Q, so a log axis
is the one that compares across bands. The in-band ratio is the spread of decay
rates a single one-octave band has to fit with one exponential — the quantity
the second-order shelf study tied to tail curvature.

| corner | worst slope | at | worst in-band ratio | at | max\|Hn\| |
|---|---|---|---|---|---|
| 6.0 kHz | 0.6853 s/oct | 3354 Hz | 1.665 : 1 | 5111 Hz | 1.000000000000 |
| 7.0 kHz | 0.6771 s/oct | 3963 Hz | 1.640 : 1 | 5928 Hz | 1.000000000000 |
| 8.0 kHz | 0.6682 s/oct | 4573 Hz | 1.614 : 1 | 6715 Hz | 1.000000000000 |
| 9.0 kHz | 0.6587 s/oct | 5207 Hz | 1.588 : 1 | 7465 Hz | 1.000000000000 |
| 10.0 kHz | 0.6483 s/oct | 5850 Hz | 1.561 : 1 | 8190 Hz | 1.000000000000 |
| 10.5 kHz | 0.6427 s/oct | 6169 Hz | 1.548 : 1 | 8522 Hz | 1.000000000000 |
| 11.0 kHz | 0.6369 s/oct | 6487 Hz | 1.534 : 1 | 8844 Hz | 1.000000000000 |
| 12.0 kHz | 0.6244 s/oct | 7136 Hz | 1.505 : 1 | 9475 Hz | 1.000000000000 |

**This is why Problem B improves instead of regressing, and it is the opposite
of the S2 result.** Moving the corner up does not merely relocate the gradient
— it **gentles** it. Worst slope falls from 0.685 to 0.624 s/oct and the worst
in-band RT60 ratio from 1.665 to 1.505, because at a higher corner less of the
total 2.5 s → HF-plateau drop happens inside the audio band.

Set against the second-order shelf study, which held a **3.0 : 1** in-band
ratio at 6 kHz and produced a 29.34 % mismatch, the mechanism is consistent in
both directions: in-band RT60 spread tracks tail curvature. The corner sweep
lowers that spread and the mismatch follows down; S2 raised it and the mismatch
followed up.

`max|Hn|` is exactly 1 at every candidate, so Option A holds throughout —
damping subtractive, decay never extended above base Decay.

---

## Decision, against the four conditions

| condition | verdict at the best candidate (10.5 kHz) |
|---|---|
| **A** 500 Hz – 2 kHz deviation reaches about 2 % | **MISSED by 0.31 pp** — 2.31 % against 2.0 %. Down from 7.90 % at the shipping corner. The interior minimum of the sweep; further corner movement makes it worse. |
| **B** HF damping stays meaningfully stronger than none | **MET** — 32.2 % reduction at 16 kHz. About two thirds of what the 6 kHz default delivers. |
| **C** no analysis band develops a worse tail-curvature regression | **MET** — 5.51 % against the 6.43 % shipping baseline, an improvement of 0.92 pp. No candidate is worse than the baseline. Caveat: this candidate's worst band is at the top of the measurable range. |
| **D** stability and Option A semantics unchanged | **MET** — `max|Hn|` exactly 1 at every candidate, topology and mapping untouched. |

Three of four conditions hold. Condition A is missed, narrowly, and the sweep
has no better point to offer.

### If a single candidate had to be named

**10.0 kHz**, not 10.5 kHz, despite 10.5 showing the lower Problem A figure:

- Problem A 2.46 % against 2.31 % — 0.15 pp worse, both over target anyway
- Problem B 5.51 % **interior at 14254 Hz**, so it is a measured maximum rather
  than a lower bound pinned at the edge of the measurable range
- HF retention 33.7 % against 32.2 %, marginally stronger

The difference between them is within the noise of what this harness
distinguishes; the reason to prefer 10 kHz is that its Problem B number can be
trusted as a maximum and 10.5 kHz's cannot.

### What this does not settle

Neither candidate passes the 5 % investigation gate at High Damp 100 %: 10 kHz
sits at 5.51 %. Moving the corner improves Problem B by 0.92 pp but does not
close that gate either, and for the higher corners the figure may be worse than
measured.

The untested lever that condition A now points at is the **low end** — Low Damp
20 % at a 200 Hz corner costs 2.25 to 2.49 % at 500 Hz, and that is what sets
the floor once the high corner is in the right place. It is a defaults
question, the same class of cheap experiment this sweep was, and it has not
been run.

No production default change, no Source change, no mapping change, no gate
change. Multi-knee topology not tested. Modulation × damping-corner regression
untouched. No install, no FL Studio, Phase 7 not complete.
