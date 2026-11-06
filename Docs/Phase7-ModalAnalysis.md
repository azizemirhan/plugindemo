# Phase 7 — Modal pole/residue analysis (FROZEN)

**Status: COMPLETE and FROZEN.** The static modal formulation is VALID, the pole
inventory is CLOSED, and residue extraction is VALID. This tooling is not to be
redesigned further unless a future regression invalidates it.

Implementation: `Tests/DampingResidue.cpp`, with the validated fixed-Q ensemble
analyser in `Tests/DecayAnalyzer.h`. Run it alone with
`AuroraVerbTests.exe Residue`; the full suite is not required to reproduce any
number below. Result at freeze: **21 / 21 checks passed**.

The system under analysis throughout is static M0 — Mod Depth 0, which is
exactly LTI (shift-invariance error 0.0, homogeneity 8.5e-22, superposition
2.2e-16) — at Decay 2.5 s, Low Damp 0, High Damp 100 %, High Damp Freq 6 kHz.

---

## 1. Exact observable degree = 18657

The configured engine has state dimension **18665**: 18649 delay states plus 16
damping states (two one-pole shelves on each of 8 lines).

Eight of those are **unobservable**. Low Damp 0 makes `gainLow` exactly 1, so
the low-shelf factor is identically 1 and its state never reaches the output.
The transfer denominator therefore has degree

```
18665 - 8 = 18657
```

This is measured from the engine, not assumed: `PASS state dimension counted
from the configured engine (18665)`, and the derived denominator degree 18657
is printed alongside it.

The analysis works on the **scaled characteristic polynomial**

```
P(z) = det[ (z - a) z^{d_i} delta_ij  -  k_i (p_i z - q_i) A_ij ]
     = det(M) * (z - a)^8 * z^{sum d}
```

`P` has degree 18657 and **no poles**, so its winding number is a plain zero
count with no correction for pole order. `F = det(I - diag(L) A)` is
meromorphic (pole of order 18649 at z = 0, order 8 at z = a) and was the
instrument that failed at the real axis; see §4.

---

## 2. Final pole accounting

```
18657 = 2 * 9322   upper-half complex poles (with their conjugates)
      +        5   real poles near the unit circle
      +        8   real poles at low radius
```

| term | value |
|---|---|
| 2 x upper-half complex in band | 18644 |
| isolated but unrefined regions | 0 |
| regions that never separated | 0 |
| real roots near the unit circle | 5 |
| low-radius roots located | 8 (winding says 8) |
| **required total** | **18657** |
| **accounted for** | **18657** |
| **unexplained** | **0** |

Audit: **duplicate pairs 0**, closest pair separation 6.477e-6, mean frequency
spacing 2.5738 Hz, widest gap 7.9913 Hz at 15395 Hz (bound 10.2951 Hz). Every
root is strictly inside the unit circle; worst pole radius **0.9999424494**.

Global cross-check from circle windings: 8 zeros inside |z| = 0.9990, 18657
inside |z| = 0.99999999, so 18649 in the search annulus — which equals
2 x 9322 + 5, the complex pairs plus the five real near-unit roots.

---

## 3. Four clustered DC-side real poles

```
z = 0.999942448077   RT60 2.500480 s
z = 0.999942448842   RT60 2.500513 s
z = 0.999942449866   RT60 2.500558 s
z = 0.999942451318   RT60 2.500621 s
```

Four real DC modes, all RT60 ~ 2.5005 s, spanning **3.2e-9 in z**.

Certified by a box symmetric about the real axis whose frequency half-width is
set so that `r*dw` equals the radius window — a square contour, where the
winding number of `P` is a plain zero count. It returned **4 at four
independent scales** (span 1e-4, 2e-5, 5e-6, 2e-6 in radius; half-width 0.7639,
0.1528, 0.0382, 0.0153 Hz), and a sign-change scan of `P` on the axis returned
**4 at three resolutions** (200k, 800k, 3.2M samples over a 3e-6 window).

A box that is merely square but radius-deep counts the wrong thing: 1e-3 of
radius needs a 7.64 Hz half-width to be square, and at 2.57 Hz between modes
that sweeps in whole conjugate pairs. That box returns 6 at DC — four real
roots plus one pair — which is correct and is **not** a count of real roots.
The certifying box must bracket the cluster in radius.

---

## 4. Nyquist-side real pole

```
z = -0.999744702600   RT60 0.563630 s
```

One real root, Nyquist side, `|P/P'|` at the numerical floor.

**Why the DC cluster was invisible for most of this study.** Resolving four
roots spanning 3.2e-9 over [0.999, 1) needs a step near 1e-12, so about a
billion samples.

- The `F`-based scan steps 2.5e-8. It crosses all four sign changes inside one
  step, the sign returns to where it started, and it reports **none**. This is
  why only the Nyquist root was known for most of the phase.
- A `P`-based scan at 1.2M samples steps 8.3e-10 and finds **two** of the four.
- Every sector moment integral was right all along. A root at `f = 0` lies
  inside no sector with `f1 > 0`, so `s0 = 0` was the true answer; the sector
  **winding count** was the corrupted one, reading a root sitting on its own
  boundary.

The fix in the frozen tooling is to separate the two jobs: the **count** comes
from a square bracketing box, the **positions** come from a coarse scan whose
every hit seeds a fine scan of its own neighbourhood, and the located total is
required to equal the certified count. Nothing is accepted on the locator alone.

---

## 5. Eight low-radius real poles

```
z = 0.144246789826   RT60 0.000074 s
z = 0.186547634459   RT60 0.000086 s
z = 0.227024750871   RT60 0.000097 s
z = 0.252188274578   RT60 0.000104 s
z = 0.268874470781   RT60 0.000110 s
z = 0.286242118144   RT60 0.000115 s
z = 0.317732831426   RT60 0.000126 s
z = 0.335184412234   RT60 0.000132 s
```

Eight fast real modes, 74–132 microseconds. The count is independently
certified: `P(z)` winding on |z| = 0.9990 gives 8.000, and the real-root scan
finds 8. The open-loop shelf pole sits at z = 0.4559; where the closed-loop
roots actually land is measured, not inferred from it.

---

## 6. Complete reconstruction results

Modal reconstruction against the direct structured transfer function
`T(z) = c^T (I - diag(L(z)) A)^{-1} diag(L(z)) b`, which itself matches the
rendered engine to 7.24e-7 dB in magnitude and 3.08e-9 rad in phase.

| band | \|H\| error | complex | impulse RMS | RT60 error | |
|---|---|---|---|---|---|
| 1 kHz | 0.1427 dB | 0.019 % | 0.041 % | 0.000 % | PASS |
| 3 kHz | 0.1296 dB | 0.008 % | 0.018 % | 0.000 % | PASS |
| 6 kHz | 0.0510 dB | 0.008 % | 0.013 % | 0.000 % | PASS |
| 12 kHz | 0.0103 dB | 0.006 % | 0.012 % | 0.000 % | PASS |

Limits fixed before the run and unchanged: `|H| <= 1.0 dB`,
`impulse <= 10 % RMS`, `RT60 <= 2 %`. **MODEL VALID.**

Residue extraction is validated two independent ways and they agree:

- Cauchy contour residue is radius-insensitive to **1.13e-12**
- Cauchy and null-vector/derivative residues agree to **1.32e-12**
- the partial-fraction convention is exact to 2.75e-15 in time and 2.76e-7 in
  frequency on a known system
- `T(z) -> 0` as `z -> inf` and is finite at `z = 0`, so there is no polynomial
  or direct term to account for

Residue magnitude distribution from the validated model:

| band | modes | p10 | median | p90 | max | p90/p10 |
|---|---|---|---|---|---|---|
| 1 kHz | 274 | 5.14e-6 | 2.939e-5 | 1.0581e-4 | 1.9794e-4 | 20.59x |
| 3 kHz | 824 | 5.31e-6 | 2.710e-5 | 1.0097e-4 | 2.2616e-4 | 19.02x |
| 6 kHz | 1648 | 6.50e-6 | 2.944e-5 | 9.986e-5 | 2.2905e-4 | 15.36x |
| 12 kHz | 3296 | 5.34e-6 | 2.930e-5 | 1.0020e-4 | 2.3380e-4 | 18.77x |

---

## 7. MODEL R / MODEL E results

MODEL R is the modal reconstruction with measured residues. MODEL E is the same
poles with residue magnitudes equalised.

| band | T0 | MODEL R | agreement | MODEL E | R - E |
|---|---|---|---|---|---|
| 1 kHz | 1.27 % | 1.27 % | 0.00 pp | 0.86 % | 0.41 pp |
| 3 kHz | 2.55 % | 2.55 % | 0.00 pp | 2.86 % | -0.32 pp |
| 6 kHz | 4.22 % | 4.22 % | 0.00 pp | 5.54 % | -1.32 pp |
| 12 kHz | 6.00 % | 6.00 % | 0.00 pp | 5.72 % | 0.28 pp |

Full fits:

| band | source | RT60 | EDT | T20 | T30 | R^2 | \|T20-T30\| |
|---|---|---|---|---|---|---|---|
| 1 kHz | T0 | 2.333 | 2.494 | 2.297 | 2.326 | 0.99975 | 1.27 % |
| 1 kHz | MODEL R | 2.333 | 2.494 | 2.297 | 2.326 | 0.99975 | 1.27 % |
| 1 kHz | MODEL E | 2.335 | 2.498 | 2.311 | 2.331 | 0.99970 | 0.86 % |
| 3 kHz | T0 | 1.750 | 1.842 | 1.687 | 1.730 | 0.99965 | 2.55 % |
| 3 kHz | MODEL R | 1.750 | 1.842 | 1.687 | 1.730 | 0.99965 | 2.55 % |
| 3 kHz | MODEL E | 1.751 | 1.732 | 1.678 | 1.727 | 0.99946 | 2.86 % |
| 6 kHz | T0 | 1.248 | 1.250 | 1.188 | 1.239 | 0.99815 | 4.22 % |
| 6 kHz | MODEL R | 1.248 | 1.250 | 1.188 | 1.239 | 0.99815 | 4.22 % |
| 6 kHz | MODEL E | 1.239 | 1.203 | 1.171 | 1.237 | 0.99797 | 5.54 % |
| 12 kHz | T0 | 0.816 | 0.939 | 0.770 | 0.817 | 0.99741 | 6.00 % |
| 12 kHz | MODEL R | 0.816 | 0.939 | 0.770 | 0.817 | 0.99741 | 6.00 % |
| 12 kHz | MODEL E | 0.809 | 0.833 | 0.760 | 0.804 | 0.99769 | 5.72 % |

MODEL R reproduces T0 **exactly** on the registered mismatch metric in every
band — 0.00 pp agreement, against a 0.50 pp limit.

---

## 8. Formal residue classification: UNRESOLVED

Pre-registered rule, **not to be changed**:

```
NOT MATERIAL  <= 0.25 pp
MATERIAL      >= 0.75 pp
```

Measured 12 kHz residue drop: **0.28 pp**. That lies between the two
thresholds, so the formal classification is **UNRESOLVED**. It is recorded as
measured; the thresholds were fixed before the measurement and stay fixed.

---

## 9. Product-level conclusion: residues cannot close the 5 % gate

At 12 kHz:

```
T0                  6.00 %
equalised residues  5.72 %
quality target      <= 5 %
```

Removing the entire measured residue contribution moves the mismatch by
0.28 pp, from 6.00 % to 5.72 %, and the tail-curvature gate is 5 %. **Even
fully equalising the residues does not close the gate.** Residue weighting is
therefore not a sufficient lever for Problem B, whatever the formal
classification says.

Recorded state:

| item | verdict |
|---|---|
| modal pole/residue formulation | VALID |
| pole inventory | CLOSED |
| residue extraction | VALID |
| residue contribution | SMALL / NOT SUFFICIENT TO SOLVE THE GATE |
| formal pre-registered classification | UNRESOLVED |

No further Phase 7 time is to be spent on pole/residue extraction. The
empirical spectral-RT60-gradient model explains most of Problem B; residues
explain only a small additional amount. The next lever under test is damping
**topology** — see the second-order monotonic shelf study.

---

## Rejected and superseded methods, kept so they are not retried

| method | why it failed |
|---|---|
| local-minimum detection of \|P\| on a grid | found 30 % of modes at 12 kHz; a shallow dip under a strong radial trend is not a strict local minimum. Its silence is never evidence. |
| thin-sliver winding probe | 0.02 Hz by 0.001 contour, aspect 1:1900. Its winding number is the small residue of two nearly cancelling radial phase walks, and it returned 0 for slivers with a root at the centre. Kept as a diagnostic; decides nothing. |
| Newton as a discovery method | each new tolerance discarded roots by a chosen number rather than by mathematics. Newton is refinement only, one step, and the moment root stands if the step leaves the contour. |
| absolute (origin-centred) moments for a close pair | the Newton-identity discriminant for two roots 7.7e-6 apart is 6e-11 while `s1^2` is 4, so it is destroyed by cancellation before quadrature error enters. Moments are taken about the region centroid, and the discriminant is formed as `2 s2 - s1^2` so the cancelling difference is taken once rather than twice. |
| frequency subdivision near DC | an annulus 1e-3 deep only stops being a needle at about 1e-4 rad, which is 0.76 Hz. Separating roots 0.004 Hz apart needs a finer cut by definition, so every such cut is degenerate. The radial direction is the only one with room. |
| `F`-based real-root scan inside the annulus | steps 2.5e-8 and crosses all four DC roots in one step, reporting none. The real axis must be scanned with `P`. |

## Invariants the frozen tooling enforces

- every cut is certified: `s0` on **both** children must reproduce that child's
  own winding count, at a resolution climbed until the integral lands on the
  integer or the cut is refused
- a shared boundary is moved for both siblings together, with the parent total
  required to survive displacement to either side; no overlap, no gap, and no
  tolerance on where a root is allowed to sit
- the strip count is read as `L(+) - L(-)` from two well-conditioned counts,
  never measured on a degenerate contour
- every locator passes through one duplicate gate
- the located count must equal the certified count before anything is accepted
