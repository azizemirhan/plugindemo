# Phase 6 / FU-1 — Interpretation

Source: [`Phase6ModalMap-v4/`](../Phase6ModalMap-v4/) · machine-readable companion:
[`Phase6ModalMap-v4/fu1-priority-modes.csv`](../Phase6ModalMap-v4/fu1-priority-modes.csv)
· derivation: [`Scripts/fu1-priority-modes.awk`](../Scripts/fu1-priority-modes.awk)

This document reads the v4 tables. It renders nothing, re-tracks nothing, changes
no threshold and proposes no DSP. Its only job is to say which modes are worth
putting in front of a listener next, and why — measurably.

## What this document is not allowed to say

Three limits are load-bearing, and every section below stays inside them.

**No mode here is a defect.** A reverb that blooms is not flat. A tall, narrow,
modulation-resistant peak is a *structural* finding: it says the tank holds a
resonance still while everything around it moves. Whether that reads as
character or as a ring is a question this measurement cannot answer, which is
the entire reason the next step is listening rather than fixing. Class B modes
are therefore named **high-priority modulation-resistant candidates** and never
"bad resonances".

**No decay claim, anywhere.** Per-mode modal decay is `infeasible` for **60 of
60** modes — the tank's modal density at the reference setting leaves no window
that can both separate a neighbour and fit a decay. That was accepted as a
physical identifiability limit, not a gap to be filled. So nothing below infers
ring time, Q-as-decay, or "how long it hangs on". The `q` column in the tables
is the *spectral* Q from the -3 dB width, not a decay figure.

**No stereo claim beyond what was measured.** The Size axis carries no L/R data
at all. The reference setting does: `band_correlation` and the L-R energy
difference are reported, and they are reported as numbers, not as a verdict
about width or placement.

## The mode set

The 60 modes are the strongest of the reference render — Size 0.60, Decay 2.5 s,
Mod Depth 0 — down to a prominence cut of **28.38 dB**. They are not every mode
in the tank; they are the loudest sixty at one setting. Their frequencies run
**375.4 Hz to 12000 Hz**.

Everything about Size comes from the validated q-space tracker. Everything about
modulation comes from the local-response measurement at Mod Depth 0.30 against
Mod Depth 0, which was never in question and was carried across unchanged.

---

## 1. A / B / C / D summary

| class | meaning | n | frequency range | median prominence | prominence min–max |
|---|---|---|---|---|---|
| **A** | size-tracking, modulation-moving | **21** | 474 – 11586 Hz | 29.84 dB | 28.39 – 33.09 |
| **B** | size-tracking, modulation-resistant | **25** | 375 – 12000 Hz | 29.14 dB | 28.38 – 32.19 |
| **C** | size-insensitive / fixed or mixed | **0** | — | — | — |
| **D** | ambiguous / unresolved | **14** | 5015 – 11924 Hz | 29.64 dB | 28.56 – 33.47 |

Local modulation response, mean per class (Mod Depth 0 → 0.30, measured for
60/60 modes):

| class | integrated energy Δ | max-prominence Δ | spectral spread ratio | concentration ratio | peak-count Δ |
|---|---|---|---|---|---|
| **A** | −6.14 dB | **−9.30 dB** | **×2.696** | **0.527** | **+0.52** |
| **B** | −4.68 dB | −3.20 dB | ×1.776 | 0.847 | −0.28 |
| **D** | −5.34 dB | −6.29 dB | ×2.795 | 0.741 | +0.21 |

A and B separate on exactly the axis they are defined by, and by a wide margin:
under the same modulation, class A neighbourhoods spread 2.7× and lose half
their energy concentration while gaining peaks; class B neighbourhoods spread
1.8×, keep 85% of their concentration, and on average *lose* a peak. The
prominence of the class B peaks falls by 3.2 dB against class A's 9.3 dB.

Stereo, at the reference setting:

| class | stereo measured | mean \|L−R\| | mean band correlation |
|---|---|---|---|
| A | 21/21 | 5.75 dB | −0.574 |
| B | 25/25 | 6.98 dB | −0.479 |
| D | 14/14 | 6.81 dB | −0.589 |

Every mode's narrow band is strongly decorrelated between channels — the mean
correlation is negative in all three classes. This is reported, not
interpreted: it does not separate the classes and it is not evidence for or
against anything below.

**Class C is empty, and that is a result.** Under the old Size tracker five
modes looked size-insensitive. Two of them (#12 at 769 Hz, #28 at 3211 Hz) now
track with 9 matched points and an exponent of −1.0001 and −1.0002 — they were
never fixed features, the tracker was locking onto stationary neighbours. The
other three fell to D for lack of matched points. Nothing in the tank is
currently measured as size-insensitive.

---

## 2. Class B — high-priority modulation-resistant candidates

Ordered by a **priority score**, defined here and used nowhere else:

```
score = (prominence_dB / 10) × concentration_default × (1 / spread_ratio)
```

It rises with the three properties asked for — how far the peak stands above its
neighbourhood, how concentrated that neighbourhood's energy remains under
modulation, and how little the modulation spreads it. It is an **ordering for a
listening session**. It is not a severity, not an audibility estimate, and not a
threshold; nothing is gated on it.

### 2a. Structure and Size evidence

| id | freq Hz | prom dB | bw Hz | Q | Size matched | q CV | R² | resid RMS Hz | fwd/back | anchor | identity | evidence |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 9146.0 | 30.49 | 0.916 | 9990 | 4 | 0.0042% | 1.000000 | 0.303 | 3/3 | 4/4 | inverse-scale-consistent | confirmed |
| 54 | 4505.8 | 28.47 | 0.916 | 4922 | 7 | 0.0066% | 1.000000 | 0.224 | 6/6 | 8/8 | inverse-scale-consistent | confirmed |
| 37 | 3426.0 | 29.13 | 0.916 | 3742 | 10 | 0.0059% | 1.000000 | 0.504 | 10/10 | 11/11 | inverse-scale-consistent | confirmed |
| 53 | 11999.8 | 28.56 | 0.916 | 13107 | 4 | 0.0061% | 1.000000 | 0.432 | 3/3 | 4/4 | inverse-scale-consistent | weak-evidence |
| 41 | 6750.1 | 28.91 | 0.916 | 7373 | 5 | 0.0043% | 1.000000 | 0.299 | 7/7 | 5/5 | inverse-scale-consistent | confirmed |
| 32 | 5287.9 | 29.32 | 0.732 | 7220 | 8 | 0.0050% | 1.000000 | 0.318 | 8/8 | 9/9 | inverse-scale-consistent | weak-evidence |
| 16 | 7876.6 | 30.33 | 0.916 | 8603 | 6 | 0.0027% | 1.000000 | 0.191 | 6/6 | 7/7 | inverse-scale-consistent | confirmed |
| 15 | 3697.6 | 30.39 | 0.732 | 5048 | 9 | 0.0157% | 1.000000 | 0.552 | 5/5 | 8/8 | inverse-scale-consistent | confirmed |
| 21 | 7678.0 | 30.04 | 0.732 | 10483 | 6 | 0.0028% | 1.000000 | 0.311 | 6/6 | 7/7 | inverse-scale-consistent | confirmed |
| 50 | 375.4 | 28.63 | 0.916 | 410 | 7 | 0.0091% | 1.000000 | 0.112 | 7/7 | 8/8 | inverse-scale-consistent | confirmed |
| 20 | 8632.7 | 30.17 | 0.916 | 9429 | 6 | 0.0036% | 1.000000 | 0.295 | 5/5 | 7/7 | inverse-scale-consistent | confirmed |
| 33 | 7535.0 | 29.28 | 0.916 | 8230 | 6 | 0.0023% | 1.000000 | 0.161 | 6/6 | 7/7 | inverse-scale-consistent | confirmed |
| 57 | 624.2 | 28.39 | 0.732 | 852 | 10 | 0.0196% | 1.000000 | 0.466 | 10/10 | 11/11 | inverse-scale-consistent | confirmed |
| 59 | 7974.4 | 28.38 | 0.916 | 8710 | 6 | 0.0162% | 1.000000 | 0.629 | 1/4 | 5/5 | identity-uncertain | weak-evidence |
| 3 | 10655.0 | 32.19 | 0.916 | 11638 | 5 | 0.0032% | 1.000000 | 0.368 | 5/5 | 6/6 | inverse-scale-consistent | confirmed |
| 42 | 10312.8 | 28.90 | 0.732 | 14080 | 4 | 0.0034% | 1.000000 | 0.342 | 5/5 | 5/5 | inverse-scale-consistent | confirmed |
| 58 | 1471.7 | 28.38 | 0.916 | 1607 | 9 | 0.0049% | 1.000000 | 0.195 | 9/9 | 10/10 | inverse-scale-consistent | confirmed |
| 12 | 769.4 | 30.45 | 0.732 | 1051 | 9 | 0.0112% | 1.000000 | 0.270 | 8/8 | 10/10 | inverse-scale-consistent | confirmed |
| 46 | 4795.4 | 28.79 | 0.916 | 5238 | 8 | 0.0073% | 1.000000 | 0.579 | 8/8 | 9/9 | inverse-scale-consistent | weak-evidence |
| 30 | 7656.4 | 29.35 | 0.732 | 10454 | 5 | 0.0023% | 1.000000 | 0.127 | 4/4 | 5/5 | inverse-scale-consistent | weak-evidence |
| 24 | 5896.6 | 29.78 | 0.732 | 8051 | 5 | 0.0030% | 1.000000 | 0.224 | 5/5 | 6/6 | inverse-scale-consistent | confirmed |
| 43 | 1110.1 | 28.86 | 0.916 | 1213 | 10 | 0.0131% | 1.000000 | 0.486 | 10/10 | 11/11 | inverse-scale-consistent | confirmed |
| 36 | 1644.7 | 29.14 | 0.732 | 2246 | 9 | 0.0113% | 1.000000 | 0.582 | 9/9 | 10/10 | inverse-scale-consistent | weak-evidence |
| 35 | 6285.3 | 29.18 | 0.732 | 8582 | 6 | 0.0026% | 1.000000 | 0.158 | 6/6 | 7/7 | inverse-scale-consistent | confirmed |
| 38 | 8823.8 | 29.05 | 0.916 | 9638 | 5 | 0.0025% | 1.000000 | 0.255 | 4/4 | 5/5 | inverse-scale-consistent | weak-evidence |

### 2b. Local modulation response

| id | freq Hz | energy Δ dB | max-prom Δ dB | spread ratio | concentration ratio | peaks 0 → 0.30 | peak Δ | band corr | \|L−R\| dB | score |
|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 9146.0 | -4.24 | 0.79 | 1.272 | 1.042 | 6 -> 4 | -2 | -0.237 | 10.38 | 0.567 |
| 54 | 4505.8 | -8.93 | -0.75 | 0.968 | 0.967 | 8 -> 6 | -2 | -0.476 | 6.48 | 0.492 |
| 37 | 3426.0 | -1.85 | 1.30 | 1.218 | 0.907 | 6 -> 5 | -1 | -0.484 | 8.54 | 0.440 |
| 53 | 11999.8 | -0.90 | -3.51 | 1.358 | 0.998 | 5 -> 4 | -1 | -0.622 | 1.95 | 0.385 |
| 41 | 6750.1 | -1.87 | -7.28 | 1.187 | 0.730 | 5 -> 6 | +1 | -0.949 | 9.77 | 0.354 |
| 32 | 5287.9 | -3.43 | -2.23 | 1.804 | 1.008 | 6 -> 6 | 0 | 0.064 | 1.70 | 0.352 |
| 16 | 7876.6 | -5.97 | -6.57 | 1.587 | 0.844 | 5 -> 6 | +1 | -0.538 | 6.25 | 0.352 |
| 15 | 3697.6 | -5.29 | 2.27 | 1.398 | 0.700 | 7 -> 5 | -2 | -0.827 | 8.96 | 0.342 |
| 21 | 7678.0 | -4.67 | -2.14 | 1.897 | 0.865 | 5 -> 6 | +1 | -0.800 | 5.95 | 0.326 |
| 50 | 375.4 | -7.85 | 4.94 | 1.700 | 0.994 | 5 -> 5 | 0 | -0.554 | 6.83 | 0.322 |
| 20 | 8632.7 | -5.75 | -0.47 | 2.691 | 1.484 | 6 -> 5 | -1 | -0.866 | 9.17 | 0.310 |
| 33 | 7535.0 | -5.00 | -8.73 | 1.168 | 0.635 | 5 -> 6 | +1 | -0.237 | 4.29 | 0.306 |
| 57 | 624.2 | -2.53 | 2.40 | 2.592 | 1.377 | 6 -> 5 | -1 | -0.524 | 10.55 | 0.299 |
| 59 | 7974.4 | -4.49 | -2.51 | 1.181 | 0.736 | 5 -> 5 | 0 | -0.787 | 8.89 | 0.297 |
| 3 | 10655.0 | -10.15 | -6.33 | 1.883 | 0.735 | 5 -> 4 | -1 | 0.172 | 2.11 | 0.268 |
| 42 | 10312.8 | -2.15 | -8.08 | 1.891 | 0.800 | 5 -> 6 | +1 | 0.079 | 14.10 | 0.262 |
| 58 | 1471.7 | -4.10 | -5.44 | 1.891 | 0.815 | 5 -> 5 | 0 | -0.164 | 1.24 | 0.260 |
| 12 | 769.4 | -6.16 | -4.47 | 2.039 | 0.791 | 5 -> 5 | 0 | -0.856 | 15.07 | 0.242 |
| 46 | 4795.4 | -7.46 | -9.74 | 1.238 | 0.494 | 5 -> 6 | +1 | -0.251 | 4.47 | 0.211 |
| 30 | 7656.4 | -4.45 | -2.58 | 2.208 | 0.742 | 5 -> 5 | 0 | 0.538 | 9.78 | 0.207 |
| 24 | 5896.6 | -7.68 | -6.16 | 1.464 | 0.588 | 6 -> 6 | 0 | -0.898 | 8.17 | 0.200 |
| 43 | 1110.1 | -2.09 | -5.31 | 1.310 | 0.479 | 5 -> 4 | -1 | -0.639 | 9.52 | 0.192 |
| 36 | 1644.7 | -0.27 | -1.64 | 1.980 | 0.755 | 7 -> 6 | -1 | -0.997 | 3.52 | 0.185 |
| 35 | 6285.3 | -6.59 | -0.67 | 3.503 | 0.890 | 6 -> 5 | -1 | -0.687 | 5.83 | 0.162 |
| 38 | 8823.8 | -3.26 | -7.00 | 2.978 | 0.792 | 5 -> 6 | +1 | -0.432 | 1.02 | 0.160 |

### What stands out in B

**#10 — 9146 Hz** is the clearest case in the set. Prominence 30.5 dB, and
modulation moves it almost not at all: spread ×1.27, concentration ratio 1.042
(the neighbourhood is *more* concentrated at Mod Depth 0.30 than at 0), peak
count falls 6 → 4, and the peak's own prominence goes *up* 0.79 dB. Size
identity is clean but thin — 4 matched points, the rest boundary exits above
12 kHz.

**#54 — 4506 Hz** is the only mode in the whole 60 whose neighbourhood spread
ratio is **below 1** (0.968): the modulation narrows it. 7 matched points,
concentration 0.967, prominence Δ −0.75 dB.

**#37 — 3426 Hz** has the strongest Size evidence of any B candidate: **10 of 10
matched points**, forward/backward 10/10, anchor 11/11, q CV 0.0059%. Under
modulation its prominence *rises* 1.30 dB and spread grows only ×1.22.

**#50 — 375 Hz** is the lowest mode in the detailed set. Concentration ratio
0.994 — essentially untouched — with prominence rising 4.94 dB under modulation,
though spread does grow ×1.70.

**#57 — 624 Hz** and **#20 — 8633 Hz** both show a concentration ratio *above*
1.3, meaning modulation concentrates the neighbourhood rather than dispersing
it. Both are reported; neither is in the listening set, because their spread
ratios (2.59 and 2.69) are among the highest in class B and the two signals
disagree.

Seven of the 25 carry `weak-evidence` (a matched peak within 3 dB of the 10 dB
detector floor, or a -3 dB width off the trajectory median by more than 2×).
None carry `suspect`. **#53 at 11999.8 Hz sits exactly on the detector's upper
band edge and is weak-evidence**; its score is high but the measurement is at
its limit, and it is excluded from the listening set for that reason.

---

## 3. Class A — positive controls for the modulation layer

These are the modes where the existing anti-modal modulation demonstrably does
what it is there to do. Ordered by the same score; read this table the opposite
way round — **low** score, **high** spread ratio and **low** concentration ratio
is the modulation working.

### 3a. Structure and Size evidence

| id | freq Hz | prom dB | bw Hz | Q | Size matched | q CV | R² | resid RMS Hz | fwd/back | anchor | identity | evidence |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 18 | 1429.0 | 30.20 | 0.732 | 1951 | 10 | 0.0170% | 1.000000 | 0.600 | 7/7 | 10/10 | inverse-scale-consistent | confirmed |
| 49 | 9803.3 | 28.72 | 0.732 | 13385 | 5 | 0.0027% | 1.000000 | 0.207 | 5/5 | 6/6 | inverse-scale-consistent | weak-evidence |
| 28 | 3211.5 | 29.45 | 0.732 | 4385 | 9 | 0.0140% | 1.000000 | 0.444 | 8/8 | 10/10 | inverse-scale-consistent | confirmed |
| 55 | 10926.4 | 28.40 | 0.916 | 11934 | 4 | 0.0015% | 1.000000 | 0.231 | 3/3 | 4/4 | inverse-scale-consistent | confirmed |
| 19 | 1670.1 | 30.19 | 0.732 | 2280 | 9 | 0.0077% | 1.000000 | 0.134 | 9/9 | 10/10 | inverse-scale-consistent | confirmed |
| 48 | 9607.5 | 28.75 | 0.732 | 13118 | 5 | 0.0043% | 1.000000 | 0.388 | 5/5 | 6/6 | inverse-scale-consistent | weak-evidence |
| 7 | 8701.2 | 30.90 | 0.732 | 11880 | 5 | 0.0051% | 1.000000 | 0.355 | 5/5 | 6/6 | inverse-scale-consistent | confirmed |
| 23 | 3968.5 | 29.84 | 0.916 | 4335 | 5 | 0.0031% | 1.000000 | 0.134 | 5/5 | 6/6 | inverse-scale-consistent | confirmed |
| 25 | 7217.2 | 29.76 | 0.916 | 7883 | 6 | 0.0042% | 1.000000 | 0.330 | 6/6 | 7/7 | inverse-scale-consistent | confirmed |
| 39 | 11586.4 | 29.01 | 0.732 | 15819 | 4 | 0.0202% | 1.000000 | 1.055 | 1/3 | 4/4 | identity-uncertain | weak-evidence |
| 11 | 7703.2 | 30.48 | 0.732 | 10518 | 5 | 0.0250% | 1.000000 | 0.831 | 1/4 | 4/6 | identity-uncertain | confirmed |
| 56 | 4139.6 | 28.39 | 0.732 | 5652 | 5 | 0.0034% | 1.000000 | 0.135 | 6/6 | 5/5 | inverse-scale-consistent | confirmed |
| 40 | 6137.8 | 28.94 | 0.916 | 6704 | 5 | 0.0132% | 1.000000 | 0.652 | 2/5 | 4/6 | identity-uncertain | weak-evidence |
| 17 | 672.4 | 30.23 | 0.916 | 734 | 10 | 0.0111% | 1.000000 | 0.224 | 10/10 | 11/11 | inverse-scale-consistent | confirmed |
| 22 | 474.4 | 29.86 | 0.732 | 648 | 10 | 0.0203% | 1.000000 | 0.347 | 10/10 | 11/11 | inverse-scale-consistent | confirmed |
| 29 | 1701.0 | 29.44 | 0.732 | 2322 | 8 | 0.0083% | 1.000000 | 0.270 | 8/8 | 8/8 | inverse-scale-consistent | confirmed |
| 4 | 646.3 | 31.22 | 0.732 | 882 | 9 | 0.0077% | 1.000000 | 0.116 | 9/9 | 10/10 | inverse-scale-consistent | confirmed |
| 44 | 795.2 | 28.82 | 0.732 | 1086 | 9 | 0.0167% | 1.000000 | 0.129 | 8/8 | 10/10 | inverse-scale-consistent | weak-evidence |
| 5 | 3383.1 | 31.11 | 0.732 | 4619 | 7 | 0.0127% | 1.000000 | 0.666 | 5/9 | 7/7 | identity-uncertain | confirmed |
| 2 | 1158.5 | 33.09 | 0.732 | 1582 | 7 | 0.0048% | 1.000000 | 0.069 | 7/7 | 8/8 | inverse-scale-consistent | confirmed |
| 13 | 9632.1 | 30.44 | 0.732 | 13151 | 5 | 0.0100% | 1.000000 | 0.717 | 3/5 | 5/6 | identity-uncertain | weak-evidence |

### 3b. Local modulation response

| id | freq Hz | energy Δ dB | max-prom Δ dB | spread ratio | concentration ratio | peaks 0 → 0.30 | peak Δ | band corr | \|L−R\| dB | score |
|---|---|---|---|---|---|---|---|---|---|---|
| 18 | 1429.0 | -3.56 | -2.05 | 2.347 | 0.909 | 5 -> 4 | -1 | -0.409 | 4.40 | 0.215 |
| 49 | 9803.3 | -2.15 | -14.25 | 2.123 | 0.672 | 4 -> 5 | +1 | -0.958 | 5.68 | 0.210 |
| 28 | 3211.5 | -9.56 | -1.05 | 1.547 | 0.579 | 6 -> 6 | 0 | -0.860 | 9.28 | 0.210 |
| 55 | 10926.4 | -4.36 | -6.80 | 1.678 | 0.602 | 6 -> 6 | 0 | -0.135 | 1.93 | 0.203 |
| 19 | 1670.1 | -3.72 | -12.73 | 1.555 | 0.491 | 4 -> 6 | +2 | -0.066 | 3.00 | 0.193 |
| 48 | 9607.5 | -1.96 | -0.04 | 2.163 | 0.641 | 6 -> 5 | -1 | -0.659 | 6.56 | 0.184 |
| 7 | 8701.2 | -9.36 | -9.65 | 2.054 | 0.513 | 5 -> 6 | +1 | -0.474 | 5.39 | 0.181 |
| 23 | 3968.5 | -6.01 | -9.50 | 2.100 | 0.522 | 4 -> 6 | +2 | -0.305 | 4.94 | 0.157 |
| 25 | 7217.2 | -7.07 | -11.42 | 1.692 | 0.428 | 5 -> 6 | +1 | -0.853 | 1.33 | 0.147 |
| 39 | 11586.4 | -2.50 | -11.62 | 2.455 | 0.474 | 5 -> 6 | +1 | -0.968 | 9.41 | 0.134 |
| 11 | 7703.2 | -6.09 | -10.78 | 3.175 | 0.599 | 5 -> 5 | 0 | -0.991 | 7.07 | 0.134 |
| 56 | 4139.6 | -2.06 | -12.86 | 2.237 | 0.448 | 5 -> 7 | +2 | -0.598 | 6.66 | 0.128 |
| 40 | 6137.8 | -9.51 | -9.37 | 3.381 | 0.602 | 6 -> 6 | 0 | -0.606 | 2.84 | 0.110 |
| 17 | 672.4 | -8.90 | -6.43 | 3.548 | 0.561 | 7 -> 6 | -1 | -0.481 | 5.48 | 0.107 |
| 22 | 474.4 | -7.98 | -6.01 | 3.610 | 0.576 | 5 -> 6 | +1 | -0.459 | 11.35 | 0.106 |
| 29 | 1701.0 | -7.15 | -7.73 | 1.865 | 0.294 | 6 -> 5 | -1 | -0.142 | 3.83 | 0.099 |
| 4 | 646.3 | -7.90 | -8.00 | 3.662 | 0.535 | 6 -> 6 | 0 | -0.347 | 7.95 | 0.099 |
| 44 | 795.2 | -8.05 | -13.67 | 3.598 | 0.518 | 3 -> 6 | +3 | -0.688 | 4.34 | 0.090 |
| 5 | 3383.1 | -4.73 | -10.65 | 4.436 | 0.489 | 5 -> 6 | +1 | -0.642 | 4.88 | 0.084 |
| 2 | 1158.5 | -9.51 | -18.04 | 3.791 | 0.327 | 5 -> 6 | +1 | -0.421 | 7.79 | 0.069 |
| 13 | 9632.1 | -6.86 | -12.73 | 3.601 | 0.293 | 7 -> 6 | -1 | -0.987 | 6.72 | 0.058 |

### The strongest positive controls

Four modes where every one of the four signatures fires together — prominence
down, spread up, concentration down, peaks added:

| id | freq Hz | prom Δ | spread | concentration | peaks | Size evidence |
|---|---|---|---|---|---|---|
| **#2** | 1158.5 | **−18.04 dB** | ×3.79 | **0.327** | 5 → 6 | 7 matched, fb 7/7, anchor 8/8, confirmed |
| **#56** | 4139.6 | −12.86 dB | ×2.24 | 0.448 | 5 → 7 | 5 matched, fb 6/6, anchor 5/5, confirmed |
| **#19** | 1670.1 | −12.73 dB | ×1.56 | 0.491 | 4 → 6 | 9 matched, fb 9/9, anchor 10/10, confirmed |
| **#23** | 3968.5 | −9.50 dB | ×2.10 | 0.522 | 4 → 6 | 5 matched, fb 5/5, anchor 6/6, confirmed |

**#2 at 1158 Hz is the single clearest example in the tank**: an 18 dB
prominence collapse, the neighbourhood spreading nearly four-fold, and
concentration dropping to under a third — from a mode whose Size identity is
confirmed across 7 points with a q CV of 0.0048%.

**#29 at 1701 Hz** has the lowest concentration ratio of any confirmed mode
(0.294) with a modest spread ratio of 1.87 — energy redistributed without much
widening. **#44 at 795 Hz** adds the most peaks (+3, 3 → 6) but carries
`weak-evidence`, so it is reported and not used as a control.

Two A modes are worth flagging for their *Size* evidence rather than their
modulation: **#11 (7703 Hz)** and **#40 (6138 Hz)** show forward/backward
disagreement (1/4 and 2/5) despite being classed A — their exponent is sound but
their identity is only partly path-independent. Neither is in the listening set.

---

## 4. Class D — unresolved, and why

D is not a finding about these modes. It is a statement that the Size axis did
not produce enough evidence to classify them, and the reason is uniform.

| id | freq Hz | prom dB | matched | ambiguous | boundary exit | fwd/back | anchor up | anchor down | evidence | reason |
|---|---|---|---|---|---|---|---|---|---|---|
| 51 | 5015.4 | 28.57 | 3 | 5 | 2 | 5/5 | 3/3 | 0/0 | confirmed | too few matched points |
| 45 | 7043.2 | 28.80 | 2 | 4 | 4 | 2/2 | 2/2 | 0/0 | confirmed | too few matched points |
| 47 | 8337.8 | 28.77 | 2 | 4 | 4 | 1/1 | 1/1 | 0/0 | confirmed | too few matched points |
| 27 | 9337.1 | 29.63 | 2 | 3 | 5 | 5/5 | 2/2 | 0/0 | confirmed | too few matched points |
| 26 | 9779.4 | 29.66 | 3 | 0 | 5 | 3/3 | 3/3 | 1/1 | confirmed | too few matched points |
| 9 | 10266.6 | 30.51 | 2 | 3 | 5 | 1/1 | 2/2 | 0/0 | confirmed | too few matched points |
| 34 | 10435.0 | 29.19 | 3 | 2 | 5 | 3/3 | 3/3 | 1/1 | confirmed | too few matched points |
| 8 | 10629.7 | 30.70 | 3 | 2 | 5 | **2/3** | 3/3 | 0/0 | **weak-evidence** | too few points; chain disagreement; weak peak |
| 6 | 10901.4 | 30.98 | 3 | 2 | 5 | 4/4 | **2/3** | 1/1 | confirmed | too few points; chain disagreement |
| 52 | 10949.7 | 28.56 | 2 | 2 | 6 | 3/3 | 2/2 | 0/0 | confirmed | too few matched points |
| 31 | 11264.7 | 29.33 | 3 | 0 | 6 | 3/3 | 3/3 | 0/0 | confirmed | too few matched points |
| 0 | 11535.3 | 33.47 | 2 | 2 | 6 | 2/2 | 2/2 | 0/0 | confirmed | too few matched points |
| 1 | 11561.4 | 33.39 | 3 | 1 | 6 | 3/3 | 3/3 | 0/0 | confirmed | too few matched points |
| 14 | 11924.3 | 30.39 | 3 | 1 | 6 | 3/3 | 3/3 | 0/0 | confirmed | too few matched points |

**All 14 fail the same test: fewer than four matched Size points**, which is the
sweep's long-standing rule for a usable fit and was not changed. Two also show a
single disagreeing frame (#8, #6) and one of those is weak evidence.

The cause is not tracking failure — it is the band edge. Every one of these
modes sits above 5 kHz, and their modal coordinate puts them outside the
detector's 40 Hz to 12 kHz window at the smaller Size settings: **2 to 6 of
their 10 points are boundary exits**, and the sum of boundary exits across all
60 modes (180) matches the geometric prediction from the band limits exactly.
Where they *were* tracked the evidence is good — twelve of the fourteen are
`confirmed`, and the ones with an R-squared are at 1.000000.

So: **D here means "not enough of the Size axis is visible", not "this mode
misbehaves"**. Several are among the loudest modes in the tank (#0 at 33.47 dB
and #1 at 33.39 dB are the two most prominent modes measured). They are not
interpreted as A or B, and they are not carried into the listening set.

---

## 5. Frequency-region summary

Boundaries are stated here and are arbitrary in the sense that any set would be
— they were chosen to split the 60 modes into readable groups, not from any
measurement:

- **low** — 40 to 300 Hz
- **low-mid** — 300 to 800 Hz
- **mid** — 800 to 2500 Hz
- **upper-mid** — 2500 to 6000 Hz
- **high** — 6000 to 12000 Hz

| region | modes | A | B | D | median prominence | strongest B | strongest A |
|---|---|---|---|---|---|---|---|
| low (40–300 Hz) | **0** | — | — | — | — | — | — |
| low-mid (300–800 Hz) | 7 | 4 | 3 | 0 | 29.86 dB | #50 · 375 Hz · 0.32 | #17 · 672 Hz · 0.11 |
| mid (800–2500 Hz) | 7 | 4 | 3 | 0 | 29.44 dB | #58 · 1472 Hz · 0.26 | #18 · 1429 Hz · 0.21 |
| upper-mid (2500–6000 Hz) | 11 | 4 | 6 | 1 | 29.32 dB | #54 · 4506 Hz · 0.49 | #28 · 3211 Hz · 0.21 |
| high (6000–12000 Hz) | **35** | 9 | 13 | 13 | 29.35 dB | #10 · 9146 Hz · 0.57 | #49 · 9803 Hz · 0.21 |

Median prominence is flat across every populated region — 29.3 to 29.9 dB. The
strong modes are not concentrated in one band; what changes with frequency is
how *many* there are (35 of 60 above 6 kHz) and how much of the Size axis stays
inside the analysis band (all 14 D modes are up there).

The **low region is empty**: no mode below 300 Hz reached the 28.38 dB cut for
the detailed set.

### The frequencies flagged in earlier FU-1 work

Matched only where a v4 mode genuinely lands within 2.5% of the target. No
forcing.

| earlier | v4 mode | class | prominence | Size matched | evidence | note |
|---|---|---|---|---|---|---|
| ~254 Hz | **no match** | — | — | — | — | nearest detailed mode is #50 at 375 Hz, 48% away |
| ~646 Hz | **#4 · 646.34 Hz** (0.1%) | **A** | 31.22 dB | 9 | confirmed | spread ×3.66, concentration 0.535 |
| ~660 Hz | **#17 · 672.44 Hz** (1.9%) | **A** | 30.23 dB | 10 | confirmed | spread ×3.55, concentration 0.561 |
| ~1158 Hz | **#2 · 1158.49 Hz** (0.0%) | **A** | 33.09 dB | 7 | confirmed | the strongest positive control in the set |
| ~2032 Hz | **no match** | — | — | — | — | nearest detailed mode is #29 at 1701 Hz, 16% away |
| ~3225 Hz | **#28 · 3211.49 Hz** (0.4%) | **A** | 29.45 dB | 9 | confirmed | was class C under the old tracker |
| ~3383 Hz | **#5 · 3383.06 Hz** (0.0%) | **A** | 31.11 dB | 7 | confirmed | spread ×4.44 — the largest in the set |

**Five of the seven land on class A.** Every earlier-flagged frequency that has a
counterpart in the detailed set is one the modulation layer is already moving —
often strongly. That is a substantive result and it points the listening session
away from these frequencies rather than towards them.

On the two that did not match: the detailed set is the strongest 60 at the
reference, cut at 28.38 dB, so absence here means "not among the loudest sixty",
not "nothing there". Checking the nearest sweep point (UI 0.5556) at the
inverse-scale-mapped positions, detected peaks do exist in both
neighbourhoods — around 278 Hz at 16.5 / 21.1 / 29.6 dB prominence, and around
2229 Hz at 10.8 / 14.1 / 23.4 / 28.3 dB. They are at or below the cut for the
detailed table, which is why they are not carried. No claim is made about them.

### The 10 to 11.6 kHz region

Twelve modes, and the most measurement-limited part of the map:

| id | freq Hz | class | prominence | matched | identity | evidence | score |
|---|---|---|---|---|---|---|---|
| 9 | 10266.6 | D | 30.51 | 2 | uncertain | confirmed | 0.120 |
| 42 | 10312.8 | **B** | 28.90 | 4 | inverse-scale-consistent | confirmed | 0.262 |
| 34 | 10435.0 | D | 29.19 | 3 | uncertain | confirmed | 0.227 |
| 8 | 10629.7 | D | 30.70 | 3 | uncertain | weak-evidence | 0.349 |
| 3 | 10655.0 | **B** | 32.19 | 5 | inverse-scale-consistent | confirmed | 0.268 |
| 6 | 10901.4 | D | 30.98 | 3 | uncertain | confirmed | 0.145 |
| 55 | 10926.4 | **A** | 28.40 | 4 | inverse-scale-consistent | confirmed | 0.203 |
| 52 | 10949.7 | D | 28.56 | 2 | uncertain | confirmed | 0.290 |
| 31 | 11264.7 | D | 29.33 | 3 | uncertain | confirmed | 0.098 |
| 0 | 11535.3 | D | **33.47** | 2 | uncertain | confirmed | 0.086 |
| 1 | 11561.4 | D | **33.39** | 3 | uncertain | confirmed | 0.154 |
| 39 | 11586.4 | A | 29.01 | 4 | uncertain | weak-evidence | 0.134 |

**Eight of the twelve are D**, all for want of Size points. The region contains
the two most prominent modes in the entire measurement (#0 and #1, both above
33 dB) and neither can be classified. Only **#3 (10655 Hz)** and **#42
(10312.8 Hz)** are usable class B here, with 5 and 4 matched points.

This region is where the FU-1 measurement is weakest, and the honest statement
is that it is under-measured rather than clean. Resolving it would need Size
coverage the current sweep does not have — a future measurement question, not
something to be settled by listening or by DSP.

---

## 6. Perceptual candidate set

Eight modes for the listening session. Selection rules, applied in order:

1. class A or B only — no D, and no `suspect` evidence anywhere;
2. `evidence = confirmed` — no `weak-evidence`, which excludes #53, #32, #59,
   #46, #30, #36, #38 from B and #13, #44, #39, #40, #48, #49 from A;
3. `identity-consistent` with no chain disagreement;
4. spread across frequency regions rather than the highest scores alone;
5. B candidates ordered by priority score, A candidates chosen for the
   strongest simultaneous four-signature response.

### Five class B — high-priority modulation-resistant candidates

| # | id | freq | region | why, measurably |
|---|---|---|---|---|
| 1 | **#10** | 9146 Hz | high | Highest score in the set (0.567). Spread ×1.27, concentration **1.042** (rises under modulation), prominence +0.79 dB, peaks 6 → 4. Size: 4 matched, q CV 0.0042%, fb 3/3, anchor 4/4, confirmed. |
| 2 | **#54** | 4506 Hz | upper-mid | Score 0.492. The only mode measured with a spread ratio **below 1** (0.968) — modulation narrows the neighbourhood. Concentration 0.967. Size: 7 matched, fb 6/6, anchor 8/8, confirmed. |
| 3 | **#37** | 3426 Hz | upper-mid | Score 0.440 with the **best Size evidence of any B candidate**: 10/10 matched, fb 10/10, anchor 11/11, q CV 0.0059%. Spread ×1.22, prominence +1.30 dB under modulation. |
| 4 | **#50** | 375 Hz | low-mid | Score 0.322, and the **lowest mode in the detailed set**. Concentration ratio 0.994 — the neighbourhood is essentially unchanged — with prominence rising 4.94 dB. Size: 7 matched, fb 7/7, anchor 8/8, confirmed. |
| 5 | **#58** | 1472 Hz | mid | Score 0.260, the strongest confirmed B in the mid region, which is otherwise thin. Spread ×1.89, concentration 0.815. Size: 9 matched, fb 9/9, anchor 10/10, confirmed. |

### Three class A — positive controls

| # | id | freq | region | why, measurably |
|---|---|---|---|---|
| 6 | **#2** | 1158 Hz | mid | The clearest example of the modulation layer working: prominence **−18.04 dB**, spread **×3.79**, concentration **0.327**, peaks 5 → 6. Size: 7 matched, fb 7/7, anchor 8/8, q CV 0.0048%. Also one of the frequencies flagged in earlier FU-1 work. |
| 7 | **#56** | 4140 Hz | upper-mid | Prominence −12.86 dB, spread ×2.24, concentration 0.448, peaks 5 → **7** (+2). Size: 5 matched, fb 6/6, anchor 5/5, confirmed. Direct A/B contrast with #54 and #37 in the same region. |
| 8 | **#4** | 646 Hz | low-mid | Prominence −8.00 dB, spread **×3.66**, concentration 0.535. Size: 9 matched, fb 9/9, anchor 10/10, q CV 0.0077%. Direct A/B contrast with #50 in the same region, and an earlier-flagged frequency. |

The set pairs a resistant and a moving mode in three of the four populated
regions — low-mid (#50 vs #4), mid (#58 vs #2), upper-mid (#54 and #37 vs #56) —
so the comparison is within-region rather than across the spectrum. The high
region contributes #10 alone; its natural A counterpart, #49 at 9803 Hz, is
`weak-evidence` and was excluded by rule 2. **#25 at 7217 Hz** (confirmed,
6 matched, prominence −11.42 dB, spread ×1.69, concentration 0.428) is the
alternate if a high-region A control is wanted, taking the set to nine.

No listening threshold, ranking or expected outcome is proposed. Which of these
is audible, and whether audible means wanted, is what the session is for.

---

## 7. Modal decay

**Infeasible for 60 of 60 modes.** Every mode's `decay_status` is `infeasible`
and `modal_decay_s`, `decay_ratio` are empty. No window can simultaneously
separate a neighbour at the tank's default modal density and hold enough frames
to fit a decay; the adversarial validation showed that forcing one there
produces confident errors of up to 46%.

Nothing in this document infers decay, ring time, or Q-as-persistence for any
mode. Where `q` appears it is the spectral Q from the -3 dB width at a single
setting. The broadband RT60 (2.505 s at the reference) is a whole-tank figure
and is not divided among modes.

---

## 8. Provenance

- Input: `Phase6ModalMap-v4/mode-table.csv` and `local-response.csv` —
  regenerated from the v3 raw renders with the validated q-space Size tracker;
  zero new audio renders; `Source/DSP` unchanged.
- Every number here is either copied from those two files or is arithmetic on
  them. The one derived quantity is the priority score, defined in section 2.
- Reproduce the companion CSV with:

```
awk -f Scripts/fu1-priority-modes.awk \
    Phase6ModalMap-v4/local-response.csv Phase6ModalMap-v4/mode-table.csv \
    > Phase6ModalMap-v4/fu1-priority-modes.csv
```
