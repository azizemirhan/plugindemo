# Phase 12 Stage 9 — Ambient AS2 (sixteen lines, 4-4-4-4): result

Research only. `Source/` is unchanged. AS2 was built and measured once, as
pre-registered in `Phase12-Stage9-Ambient-AS2-Preregistration.md` and its
Addendum 1. No threshold was changed. No further Ambient topology has been
created.

**AS2 fails. It is closed.** Hall with its visible controls and Pre-Delay
reproduces it at the default point (lowest run 0.97) and at four other
points of the nine; no point is DISTINCT. It also fails the tone gate and,
at the long point, the source-level gate.

Logs, in `Docs/Phase12-Stage9-runs/`:

- `10-as2-design-check.txt` — algebra only, before any measurement.
- `11-as2-measurements-first-run-variance-not-measured.txt` — first run;
  the variance rows are invalid (section 9).
- `12-as2-measurements.txt` — the cheap measurements, valid.
- `13-as2-final-search.txt` — the final search and the onset-aligned
  diagnostic.

## 1. Verdict by gate

| gate | limit | AS2 | result |
|---|---|---|---|
| nine-point identity, final search with Pre-Delay (4.2, 4.3) | six core points DISTINCT, the default among them; at most three not DISTINCT | 0 of 9 DISTINCT; 5 REPRODUCED, the default among them | **fails — PRESET-EQUIVALENT** |
| tone (4.4) | ≤ 1.0 dB at the defaults and over 27 settings | 1.70 at the defaults; 4.60 worst of 27 | **fails** |
| source-dependent level (4.5), engineering criterion | excess ≤ 0.5 dB; ≤ half of A1b's where that exceeds 1.0 | 0.00 / 0.00 / 0.93 dB (medium / short / long) | **fails at long** |
| Decay (4.6) | RT60 within 5 % of Hall's | 0.999 – 1.000 | passes |
| Size (4.6) | t10 monotone | 110.7 … 153.0 ms | passes |
| stereo and mono (4.6) | Stage 3's limits | correlation 0.08, fold −2.4 dB, L/R 0.1 dB | passes |

The temporal-continuity diagnostics (section 3) are not a gate, by the
decision of 2026-11-19.

## 2. Instrument checks and calibration

- A1b rebuilt in the open tank reads as A1b in the eight-line core: metric
  0.41 / 0.30 / 0.00, t10 and t50 identical. AS2's figures can be compared
  with A1b's.
- The final search at Pre-Delay 0 returns the frozen search's result
  exactly (two seeds: 3.325113 and 2.493633, both ways).
- k = 0.2537, inside its range 0.10 – 1.50 (t50 142.6 ms at 0.10, 285.2 ms
  at 1.50, target 202.7 ms). A1b's own k is 0.40.

## 3. Temporal-continuity diagnostics against A1b (not a gate)

Onset-normalised, same knobs. Approved bands: t10 and t50 ± 10 %, shape
± 0.03, both families ≤ 1.5.

| point | t10, A1b / AS2 | t50, A1b / AS2 | shape, A1b / AS2 | buildup | profile | |
|---|---|---|---|---|---|---|
| medium | 77.9 / 73.5 (−6 %) | 202.7 / 202.3 (0 %) | 0.384 / 0.363 (−0.021) | 0.19 | 0.06 | within |
| short | 30.8 / 25.9 (**−16 %**) | 79.7 / 68.6 (**−14 %**) | 0.387 / 0.378 (−0.009) | 0.76 | 0.63 | outside |
| long | 155.8 / 147.0 (−6 %) | 404.1 / 401.3 (−1 %) | 0.386 / 0.366 (−0.019) | 0.12 | 0.02 | within |

At the other six grid settings (not gating even as registered): three
within, three outside — Size 60 / Decay 0.8 (t10 and t50 −11 %), Size 85 /
Decay 0.8 (t10 +32 %, t50 +12 %, shape +0.038), Size 85 / Decay 2.5 (t10
−12 %).

Reading: AS2 keeps A1b's buildup at medium and long Decay and starts
early at short Decay. Under the thresholds as registered this is a
failure at the short point (t10 and t50).

## 4. Nine-point identity: the final Hall search with Pre-Delay

Hall delayed by zeros; the target whole, never moved or cut. Pre-Delay 0 –
150 ms in steps of 15, then 5 ms and 1 ms around the best; five seeds and
the frozen seven-control search at each. Figures are worst-family scores:
lowest median over Pre-Delay, and the lowest single run anywhere.

### AS2

| Size, Decay | kind | lowest median | lowest run | at Pre-Delay | class | stops on |
|---|---|---|---|---|---|---|
| 25, 0.8 | convergence | 1.94 | 1.90 | 8 ms | BETWEEN | energy profile |
| 25, 2.5 | core | 2.20 | 2.15 | 20 ms | BETWEEN | stereo |
| 25, 5.0 | core | 2.74 | 2.57 | 30 ms | BETWEEN | stereo |
| 60, 0.8 | convergence | 0.96 | 0.87 | 21 ms | REPRODUCED | loop repetition |
| **60, 2.5 (default)** | core | **1.12** | **0.97** | 28 ms | **REPRODUCED** | loop repetition |
| 60, 5.0 | core | 1.81 | 1.60 | 35 ms | BETWEEN | decay |
| 85, 0.8 | convergence | 1.56 | 1.42 | 52 ms | REPRODUCED | stereo |
| 85, 2.5 | core | 1.34 | 1.20 | 35 ms | REPRODUCED | loop repetition |
| 85, 5.0 | core | 1.19 | 1.12 | 58 ms | REPRODUCED | loop repetition |

**0 of 9 DISTINCT, 5 REPRODUCED. All six core points fail; three of them,
the default included, are REPRODUCED.** The Hall that reproduces the
default point: Size 59, Decay 2.42 s, Early/Late 0, Early Diffusion 76,
Attack 89, modulation 85, Width 75, Pre-Delay 28 ms. It is not a delayed
Hall alone: at every point the solution uses Attack and Early/Late with
Pre-Delay.

### Frozen A1b, same search (its status is not changed by this)

| Size, Decay | kind | lowest median | lowest run | at Pre-Delay | class |
|---|---|---|---|---|---|
| 25, 0.8 | convergence | 2.35 | 2.29 | 37 ms | BETWEEN |
| 25, 2.5 | core | 2.72 | 2.30 | 40 ms | BETWEEN |
| 25, 5.0 | core | 3.42 | 2.80 | 41 ms | DISTINCT |
| 60, 0.8 | convergence | 1.20 | 1.14 | 39 ms | REPRODUCED |
| 60, 2.5 (default) | core | 1.18 | 1.16 | 46 ms | REPRODUCED |
| 60, 5.0 | core | 2.47 | 2.27 | 50 ms | BETWEEN |
| 85, 0.8 | convergence | 1.51 | 1.51 | 65 ms | BETWEEN |
| 85, 2.5 | core | 1.56 | 1.47 | 108 ms | REPRODUCED |
| 85, 5.0 | core | 1.45 | 1.45 | 85 ms | REPRODUCED |

1 of 9 DISTINCT, 4 REPRODUCED. At the default point the full search finds
1.16, a little under the 1.28 the earlier local verification found. This
is the first time A1b has been put through the complete final search: with
Pre-Delay in the search it fails more widely than the onset-aligned search
showed (which read 5 of 9 DISTINCT).

### AS2 beside A1b

AS2's lowest median is below A1b's at eight of the nine points (the
exception: Size 85 / Decay 0.8, 1.56 against 1.51). Denser stages made the
response more reachable from Hall, not less.

### The onset-aligned diagnostic for AS2 — and a discrepancy

Not the gate. Target zeroed before its onset and moved to Hall's onset;
frozen search, five seeds.

| Size, Decay | median | lowest | class |
|---|---|---|---|
| 25, 0.8 | 1.32 | 1.20 | REPRODUCED |
| 25, 2.5 | 1.28 | 1.28 | REPRODUCED |
| 25, 5.0 | 2.58 | 2.36 | BETWEEN |
| 60, 0.8 | 5.57 | 5.38 | DISTINCT |
| 60, 2.5 | 3.26 | 2.96 | DISTINCT |
| 60, 5.0 | 2.03 | 1.74 | BETWEEN |
| 85, 0.8 | 6.11 | 5.72 | DISTINCT |
| 85, 2.5 | 6.05 | 5.70 | DISTINCT |
| 85, 5.0 | 5.76 | 5.49 | DISTINCT |

**The two searches disagree, and the disagreement is not explained.** At
Size 60 and 85 the diagnostic reads AS2 as far from Hall where the final
search reproduces it; at Size 25 it is the other way round. At the default
point the diagnostic's 3.26 is even above the final search's own figure at
Pre-Delay 0 (2.37), so the onset alignment is placing AS2 worse than no
alignment does. At five of the Size 60 / 85 points the diagnostic's
searches end at nearly the same Hall (Size 53 – 55, Early Diffusion 0,
Attack 3 – 5), which looks like a search that did not move far. I have not
investigated this. The final search is the gate by decision and is the
adversarial one (it can only find more, never less, than a fixed
alignment); the diagnostic is not relied on for anything here. It does
mean the onset-aligned figures reported for earlier candidates should not
be read as upper bounds on Hall's reach.

## 5. Tone and its variance

Octave deviation from Hall, dB.

| | defaults | worst of 27 |
|---|---|---|
| A1b | 1.82 | 3.02 |
| AS2 | 1.70 | **4.60** |

The log does not say at which of the 27 settings the 4.60 occurs.

Standard deviation at 125 Hz under delay perturbation:

| | medium | short | long |
|---|---|---|---|
| Hall, ± 4 % (floor) | 0.36 | 0.26 | 0.14 |
| A1b, ± 4 % | 1.14 | 0.57 | 1.10 |
| AS2, ± 4 % | 0.69 | 0.83 | 0.69 |
| A1b, ± 30 % | 1.30 | 0.88 | 1.22 |
| AS2, ± 30 % | 0.82 | 0.85 | 0.65 |

Twice the lines per stage cut the low-frequency scatter by about 40 % at
medium and long and by nothing at short. It is still two to five times
Hall's. The means are A1b's: mid bands +0.3 to +0.5, 8 kHz −0.4 to −0.5
(± 30 %), as expected from loss met while migrating, which AS2 does not
address. The full per-octave tables are in log 12.

## 6. Source-dependent level (engineering criterion, not a validated perceptual threshold)

| point | baseline | A1b spread / excess | AS2 spread / excess | roughness, A1b / AS2 | |
|---|---|---|---|---|---|
| medium | 0.66 | 2.27 / 1.61 | 0.64 / 0.00 | 1.61 / 1.10 | within |
| short | 0.82 | 2.83 / 2.01 | 0.62 / 0.00 | 1.94 / 1.01 | within |
| long | 0.72 | 0.63 / 0.00 | 1.65 / **0.93** | 0.72 / 0.85 | outside |

At medium and short the spread fell below Hall's own baseline. At long it
rose from 0.63 to 1.65 dB, where A1b had none.

The level law each would need (dB, Size 25 / 60 / 85 by Decay 0.8 / 2.5 /
5.0): AS2 −3.4 … +5.5; A1b −5.8 … +5.7.

## 7. Size, Decay, stereo, mono

| | Hall | AS2 | A1b |
|---|---|---|---|
| t10 at Size 10 / 30 / 50 / 70 / 90, ms | 36.2 / 39.6 / 44.9 / 52.8 / 64.9 | 110.7 / 118.4 / 128.8 / 136.9 / 153.0 | 110.7 / 124.8 / 144.8 / 156.6 / 181.6 |
| RT60 / Hall's, nine settings | 1.000 | 0.999 – 1.000 | 1.000 – 1.001 |
| correlation | −0.03 | 0.08 | −0.12 |
| mono fold, dB | −3.0 | −2.4 | −3.9 |
| L/R balance, dB | 0.4 | 0.1 | 0.7 |

All pass. Stage 5's G3 table fails AS2 on tone only. Size moves AS2's t10
less than A1b's (a range of 42 ms against 71 ms).

## 8. Cost (reported, not gating)

- **CPU: 2.16 × Hall** (2.10 in the first run), A1b 0.96. Indicative: the
  machine was in use, and this is Stage 5's timer, **not** the
  interleaved-pair timer of the Plate tool that section 4.7 named.
- **Tank memory at the largest Size: 768 KiB** (Hall and A1b 498 KiB).
- **Switching and pre-warm: not measured.** Section 4.7 listed the cost of
  two networks during a crossfade and the pre-warm needed at long Decay.
  Neither was built. By sum, two networks would cost about 3.2 × Hall; the
  time to 90 % of the energy (507 / 177 / 977 ms at medium / short / long,
  A1b 504 / 192 / 983) indicates the scale of a pre-warm. Both are
  estimates, not measurements.

## 9. Deviations and limits

- **The perturbation tool did not perturb open-tank delays.** In the first
  run AS2's standard deviation read 0.00 in every band. `perturbed()` and
  `widePerturbed()` now perturb the open tank's delays too. AS2 itself was
  not touched; between the two runs only the variance rows and the CPU
  timing changed. The first log is kept.
- CPU timer and switching / pre-warm: as section 8.
- The final search ran from 2026-11-19 13:35 to 2026-11-20 about 14:55;
  the machine slept for about 14.5 hours of that. Sleep suspends the
  process and does not affect results.
- The onset-aligned discrepancy of section 4 is open.
- Same knobs against Hall (not a gate): worst family buildup, 10.78 / 5.87
  / 13.04; modal structure 1.51 at medium against A1b's 6.75.

## 10. The predictions, against the outcome

| | predicted | outcome | |
|---|---|---|---|
| P1 | preservation probably passes; if it fails, t10 early | within at medium and long; fails at short, t10 −16 % and t50 −14 % | wrong; the failure mode named is the one that occurred |
| P2 | 125 Hz SD near 0.8 at medium, not near Hall's | 0.69 (± 4 %), 0.82 (± 30 %) | held |
| P3 | tone fails; worst of 27 about 2 dB | fails; 4.60 | held, and worse than expected |
| P4 | source level improves, still outside at medium and short (1.0 – 1.4) | 0.00 at medium and short; 0.93 at long | wrong in both directions |
| P5 | no more distinct than A1b, possibly less | less distinct at 8 of 9 points; 0 of 9 DISTINCT | held |
| P6 | weak at the default with Pre-Delay | REPRODUCED at the default (0.97) | wrong, in the bad direction |
| P7 | stereo and mono pass | pass | held |
| P8 | Decay and Size pass | pass | held |
| P9 | CPU 1.6 – 2.1 ×; memory about 1 MiB | 2.16 × (indicative); 768 KiB | CPU just over; memory under |

## 11. What the experiment answered

- **Is the variance a density problem?** Partly. Doubling the lines per
  stage removed about 40 % of the low-frequency scatter at two of three
  points and fixed the source-level spread at two of three. It did not
  reach the tone gate, and the worst setting got worse.
- **Does density add a dimension Hall cannot reach?** No. AS2 is closer to
  Hall than A1b at eight of nine points.
- **The stop condition set on 2026-11-19 is met.** AS2 keeps A1b's
  temporal identity at medium and long Decay and fails the core points —
  all six. With Pre-Delay in the search A1b itself is reproduced at its
  default and at both large-Size core points. This is the evidence that
  condition described: the staged-migration buildup, as built here, is
  reachable from Hall with Attack, Early/Late and Pre-Delay.

## 12. Stop

AS2 is closed and failed. No AS3 and no other Ambient topology has been
created. What Ambient's identity should be is a decision, not a
measurement, and is not taken here.
