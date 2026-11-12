# Phase 7 — Option A overlap architecture (TEST-ONLY)

**Answer: NO. The multiplicative overlap is not the cause of the Problem A
floor, and the overlap family cannot move it — by a margin of five orders of
magnitude.** λ from 0 to 1 changes the worst 500 Hz – 2 kHz deviation by
**0.00 pp**. The study stops here and does not proceed to frequency-dependent
normalisation.

The useful finding is not the null result itself but **why** it is null, and
that it is null by construction rather than by accident.

Implementation: `Tests/DampingOverlap.cpp` with `Tests/DampingSweepHarness.h`.
Run with `AuroraVerbTests.exe Overlap`. 19 / 19 checks pass. No production
Source, default, mapping or Option A semantic was changed, and nothing here is
implemented in production.

---

## The derivation

With `u = z⁻¹`, `p = coeffLow`, `q = coeffHigh`, `A = cL(1−p)`, `B = cH·q`,
`cL = 1 − gainLow`, `cH = 1 − gainHigh`, production's two sections are

```
H_L = (1 − p u − A) / (1 − p u)
H_H = (1 − B − q·gainHigh·u) / (1 − q u)
```

and `H_λ = 1 + dL + dH + λ·dL·dH` over their common denominator is

```
D(u) = 1 − (p + q)u + p q u²

c0 = 1 − A − B + λ A B
c1 = −(p + q) + A q + B(1 + p) − λ A B
c2 = p q − B p                            ← no λ at all
```

Measured at the provisional best default, line 0:

| λ | c0 | c1 | c2 | d1 | d2 |
|---|---|---|---|---|---|
| 0.00 | 0.970681481 | −1.187380688 | 0.235127812 | −1.244250623 | 0.263111633 |
| 0.50 | 0.970689989 | −1.187389196 | 0.235127812 | −1.244250623 | 0.263111633 |
| 1.00 | 0.970698498 | −1.187397705 | 0.235127812 | −1.244250623 | 0.263111633 |

**The whole λ range moves c0 by 1.70e-5 on a coefficient of order 1.** That
number is `A·B`, and it is the entire answer to the study's question — see
below.

### Structural properties, all measured rather than trusted

| property | measured |
|---|---|
| λ=1 gives exactly the product's coefficients | **2.22e-16** — one machine epsilon |
| …and the same magnitude over 20001 points | 2.11e-13, against a derived floor of 1.18e-14 |
| Low Damp 0: coefficient spread across every λ | **exactly 0** |
| High Damp 0: coefficient spread across every λ | **exactly 0** |
| no new poles | poles are 0.974160 and 0.270091 — the production coefficients themselves, for every λ |
| every zero minimum-phase | worst \|z\| 0.974737 |
| DC gain independent of λ | 0.977074127 at every λ |

The two zero results are **structural, not numerical**: every λ term carries
the factor `A·B`, and `A = 0` when Low Damp is 0 while `B = 0` when High Damp
is 0. So the architecture can only change behaviour when both sections are
active, and Low-only / High-only behaviour is untouched by construction. That
is what made the family safe to test at all.

### Two tolerances of mine, and how they were set

The λ=1 check was first written against the **magnitude** at 1e-15 and it
failed at 2.11e-13. Loosening it would have been the wrong move; the right one
was to notice the algebraic claim is about **coefficients**, where there is no
division and no cancellation, and to test it there — it passes at one machine
epsilon. The magnitude comparison is a different quantity: two evaluation paths
for the same function, whose floor is set by cancellation in the denominator.
At DC that is `(1−p)(1−q) = 0.0189` formed from terms of order 1, a factor of
**53×**, so the floor is near `53 × 2.22e-16 = 1.18e-14` and two independent
paths cost a small multiple of it. The bound was set from that factor, not from
the observed number, and the factor is printed next to the result.

---

## A certified maximum for this numerator family

The old closed form is specific to a product of two first-order shelves and is
**not** valid here, so it was not reused. For this family, with `x = cos ω`,

```
|N(e^-jω)|² = (c0²+c1²+c2² − 2c0c2) + 2c1(c0+c2)·x + 4c0c2·x²
```

and the same form holds for `D`. So `|H|² = P(x)/Q(x)` with both quadratic on
`[−1,1]`, the `x³` terms of `P'Q − PQ'` cancel identically, and the stationary
points are the roots of an explicit **quadratic**:

```
(P₂Q₁ − P₁Q₂)x² + 2(P₂Q₀ − P₀Q₂)x + (P₁Q₀ − P₀Q₁) = 0
```

The maximum is therefore at `x = ±1` or at one of two closed-form points. No
grid, no search, no tolerance.

### A real defect this found in my own code

The first version solved that quadratic with the naive `(−b ± √disc)/2a`. It
cancels when `−b` and `√disc` are close and blows up when `a` is small — and
`a = P₂Q₁ − P₁Q₂` does go small here. Somewhere on the 3600-point safety grid
it **under-stated the maximum by 4.3e-9**, which left `max|Hn| = 1.000000004`
and broke Option A. Under-stating is the one direction that matters, so this
was fixed structurally rather than by loosening the bound:

1. the stable form — `t = −½(b + sign(b)√disc)`, roots `t/a` and `c/t`
2. a coarse grid bound as a **floor** under the closed form, `max(closed, grid)`

The second is not a second opinion about the mathematics; the closed form is
exact in exact arithmetic. It is insurance against the arithmetic, and it is
the same guard production's own Option A study used when it introduced a new
pole family. Over-stating costs a hair of level; under-stating breaks the
invariant. How often the grid wins is counted and reported rather than hidden.

After the fix, `max|Hn| ≤ 1` holds everywhere, including across a 3600-point
grid of λ × Decay × Low × High × both corners.

---

## The harness is anchored to production

A test-local tank is unavoidable here because the filter is not production's.
So at λ=1 — where the filter **is** production's composite — the same settings
through the production `FdnCore` must give the same decay:

| band | test-local | production | difference |
|---|---|---|---|
| 500 Hz | 2.4578 s | 2.4578 s | 0.000 pp |
| 1 kHz | 2.5184 s | 2.5184 s | 0.000 pp |
| 2 kHz | 2.4408 s | 2.4408 s | 0.000 pp |

Worst drift **7.8e-7 pp**. Modulation is 0 on both sides, so the absolute
figures here are a different operating point from the 2.32 % measured at the
shipping Mod Depth; **the λ=1 row, 2.37 %, is this study's reference.**

---

## PROBLEM A across λ — the null result

| λ | 500 Hz | 1 kHz | 2 kHz | worst | at | vs λ=1 |
|---|---|---|---|---|---|---|
| 0.00 | 1.68 | 0.74 | 2.37 | **2.37 %** | 2 kHz | **0.00 pp** |
| 0.25 | 1.69 | 0.74 | 2.37 | 2.37 % | 2 kHz | 0.00 pp |
| 0.50 | 1.69 | 0.74 | 2.37 | 2.37 % | 2 kHz | 0.00 pp |
| 0.75 | 1.69 | 0.74 | 2.37 | 2.37 % | 2 kHz | 0.00 pp |
| 1.00 | 1.69 | 0.74 | 2.37 | 2.37 % | 2 kHz | — |

2 kHz does not move at all. 500 Hz moves 0.01 pp. There is no interior region
to refine, so the second sweep the instruction allowed was not needed.

### Why it is null, and why that is the valuable part

`dL` and `dH` are each the deviation of one **per-pass** shelf from unity. In
an FDN the frequency-dependent decay is accumulated over many loop passes, so
each pass only has to contribute a small fraction of it, and both deviations
are small: at the test point `A = cL(1−p) ≈ 5e-4` and `B = cH·q ≈ 3e-2`.

Their product is therefore **second-order small**: `A·B = 1.70e-5` against
coefficients of order 1. The multiplicative overlap is five orders of magnitude
below anything that could move a 2.37 % metric.

**So this is not a near miss. The overlap is inherently negligible in a
per-pass FDN damper, and no reweighting of it can matter.** The direction is a
dead end by construction, which is worth knowing precisely because it was
cheap to rule out.

---

## The normalisation coupling survives — which locates the real cause

| λ | M, line 0 | M, line 7 | raw / normalised RT60, 500 Hz | 1 kHz | 2 kHz |
|---|---|---|---|---|---|
| 0.00 | 0.998278457 | 0.994899418 | 0.97790 | 0.97737 | 0.97764 |
| 0.50 | 0.998289925 | 0.994998707 | 0.97816 | 0.97763 | 0.97790 |
| 1.00 | 0.998301394 | 0.995097998 | 0.97841 | 0.97789 | 0.97816 |

`M` is the single scalar Option A divides by. Removing the overlap entirely
changes it by **2.3e-5**, and the broadband lift it applies — about 2.2 % of
RT60 — barely moves with λ at all.

That is the diagnostic this study was for, and it answers the question
cleanly: **`M` is set by the maximum of `1 + dL + dH`, the sum, not by the
product term.** The single-scalar coupling that produced the default floor is
therefore a property of the normalisation itself, not of the overlap, and
reducing overlap cannot reach it.

---

## Curve quality — the family is clean but inert

| λ | steepest slope | at | widest in-band | at | interior maxima | min\|Hn\| |
|---|---|---|---|---|---|---|
| 0.00 | 0.3957 s/oct | 7659 Hz | 1.2159 : 1 | 8683 Hz | **1** | 0.956111 |
| 0.50 | 0.3958 s/oct | 7659 Hz | 1.2159 : 1 | 8683 Hz | **1** | 0.956107 |
| 1.00 | 0.3958 s/oct | 7659 Hz | 1.2160 : 1 | 8683 Hz | **1** | 0.956103 |

Exactly one interior maximum of `|Hn|` at every λ — the point Option A
normalises to 1 — so **no new hump and no notch is created**; `min|Hn|` stays
at 0.9561. λ=0 is as clean a curve as λ=1. The family passes every safety and
quality condition and simply does nothing.

LF / HF retention, as RT60 relative to λ=1: every band between 60 Hz and 16 kHz
is within **1.0004** of its λ=1 value. Neither control is weakened — and
neither is helped.

---

## Cost and realisability

```
one overlap section      4.308 ns/sample     (production cascade 4.891)
whole tank              54.648 ns/sample
```

**It can be implemented as one second-order section with two states** — exactly
the state count the two first-order sections already use, because `H_λ` shares
their denominator. No duplicated filter state, no new poles, one biquad per
line. If it did anything, it would be free.

---

## Decision

| condition | verdict |
|---|---|
| **A** clearly improves Problem A beyond the 2.32 % floor | **NO — 0.00 pp.** |
| **B** retains useful Low and High damping | yes, within 1.0004 everywhere |
| **C** creates no new hump or notch | yes, one interior maximum at every λ |
| **D** preserves Option A safety | yes, after the quadratic-solve fix |
| **E** leaves Low-only / High-only unchanged | yes, exactly, by construction |
| **F** reasonable implementation complexity | yes, one biquad, same state count |

Five of six conditions are met and the one that matters is not. **The overlap
family produces no win, so this stops here.** Per the study rules it does not
proceed automatically to frequency-dependent normalisation.

### The standing recommendation

```
Low Damp Freq     200 Hz    (interior optimum)
Low Damp          20 %      (interior optimum)
High Damp Freq    10 kHz    (Problem B worst is interior and trustworthy there)
High Damp         45 %      (interior optimum, beats the shipping 50 %)
```

**This is the practical optimum under the simple architecture**: Problem A
2.32 % at the shipping Mod Depth against the current 7.90 %, a 5.58 pp
improvement for 9.4 % of the HF damping function and no DSP change at all.
Problem B stays at 5.51 % worst measured mismatch, with the 5 % investigation
gate unchanged and not passed.

What remains is a product decision rather than a measurement: whether the last
~0.3 pp of Problem A is worth any further DSP complexity. This study removes one
candidate from that conversation and narrows where the remaining 0.3 pp lives —
in the single-scalar normalisation, not in the shelf overlap.

No production default change, no Source change, no mapping change, no Option A
change, no gate change. No multi-knee topology, no frequency-dependent
normalisation, modulation × damping-corner regression untouched. No install, no
FL Studio, Phase 7 not complete.
