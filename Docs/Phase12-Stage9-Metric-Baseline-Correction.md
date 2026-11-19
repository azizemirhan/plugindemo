# Phase 12 — Metric baseline correction (2026-11-19)

Research only. `Source/` is unchanged. **No threshold is altered. No
candidate is reinterpreted in its favour.** This corrects an error of mine
in how results were read, and reports every affected figure before and
after.

## 1. The error

The frozen metric divides each family's distance by that family's
**same-family baseline** (the "floor": the largest distance the family
shows between Hall and another realisation of Hall), or by 1 if that is
larger. The baseline is measured by one section of the Stage 3 suite, or
by `ensureFloor()` when a later stage is run on its own. The heavy Stage
4, 5 and 6 suites call it.

**The diagnostic commands I wrote from Stage 7b on did not.** Run by name,
in a process where nothing had measured the baseline, they read an empty
one: every family divided by 1. Their scores were the raw, unscaled
distances. Nothing failed and nothing warned.

## 2. The baseline

| family | baseline | divisor |
|---|---|---|
| buildup | 1.25 | **1.25** |
| energy profile | 1.83 | **1.83** |
| echo density | 1.25 | **1.25** |
| stereo | 0.41 | 1 |
| decay | 0.13 | 1 |
| recurrence | 0.72 | 1 |
| late texture | 0.75 | 1 |
| loop repetition | 0.78 | 1 |
| modal structure | 0.89 | 1 |
| decay shape | 0.18 | 1 |
| band arrival | 0.73 | 1 |
| lateral spread | 0.53 | 1 |
| partial placement | 0.23 | 1 |

**Three families were overstated — buildup by 25 %, energy profile by
83 %, echo density by 25 %. The other ten were read correctly.** A
corrected figure can only be equal to or lower than the one reported. For
a search, the objective changes too, so a search was re-run, not divided.

## 3. What was and was not affected

**Affected** — every frozen-metric score produced by a named diagnostic
command: Stage 7b's Plate secondary-rate checks S0 and S3; Stage 8's DA0,
DA8, DA9, DA11, the family columns of the scan, AS1's two preservation
families, the nine-point searches, the Hall + Pre-Delay verifications; and
the same-knob table of the Stage 7 listening package's key, which was
written by a separate process that did not measure the baseline either.

**Not affected:** t10, t50, the shape number, onset, centroid, early
fraction; octave deviations, their means and standard deviations;
third-octave roughness; source-dependent level; the level law; RT60; Size;
stereo and mono figures; CPU; memory; the stage-by-stage root-cause
analysis; the wide-perturbation result; the rank correlations of the scan
(a uniform scaling of one column does not change ranks: they were
re-computed and are identical). **And every gate result recorded in
Stages 3 – 6, which measured the baseline.**

All eight affected measurements were re-run with the baseline in place.
Logs: `Docs/Phase12-Stage9-runs/02` … `09`.

## 4. Every affected result, before and after

Classes use the frozen thresholds: REPRODUCED ≤ 1.5; DISTINCT ≥ 3.0 with
no run ≤ 1.5; otherwise BETWEEN / WEAK.

### 4.1 A1b against Hall

| result | reported (unscaled) | corrected | class before → after |
|---|---|---|---|
| **default point, Hall + Pre-Delay, full support, local search** | 1.72 | **1.28** | WEAK → **REPRODUCED** |
| the same, target advanced (the earlier method) | 1.86 | 1.67 | WEAK → WEAK |
| one-time verification, Pre-Delay over five values — medium | 1.93 (run 1.88) | 1.85 (run 1.81) | WEAK → WEAK |
| — short (convergence region) | 1.46 (run 1.38) | 1.35 (run 1.34) | mimic → mimic |
| — long | 3.65 (run 3.47) | 3.27 (run 3.21) | not a mimic → not a mimic |
| DA11c, start aligned once — medium / short / long | 5.06 / 2.09 / 3.61 | 4.90 / 1.93 / 3.28 | distinct / weak / distinct → the same |
| DA11b, dust zeroed, absolute time | 5.81 / 3.86 / 11.89 | 5.32 / 3.98 / 8.75 | distinct → distinct |
| DA9, target with a −120 dB floor | 20.69 / 4.13 / 22.28 | 15.15 / 3.95 / 17.20 | "survives" → "survives" (not relied on, as before) |

**Nine-point search, onset-aligned, frozen A1b:**

| Size, Decay | region | reported | corrected | class before → after |
|---|---|---|---|---|
| 25, 0.8 | convergence | 1.77 | 1.69 | between → between |
| 60, 0.8 | convergence | 4.76 | 3.75 | distinct → distinct |
| 85, 0.8 | convergence | 3.07 | 2.42 | distinct → **between** |
| 25, 2.5 | core | 2.43 | 2.36 (run 1.93) | between → between |
| 60, 2.5 (default) | core | 5.13 | 5.03 | distinct → distinct |
| 85, 2.5 | core | 2.71 | 2.53 | between → between |
| 25, 5.0 | core | 3.82 | 3.32 (run 2.93) | distinct → distinct |
| 60, 5.0 | core | 4.14 | 3.57 | distinct → distinct |
| 85, 5.0 | core | 3.75 | 3.25 | distinct → distinct |
| | | 6 of 9 distinct | **5 of 9 distinct** | NOT DISTINCT → NOT DISTINCT |

The two core points that failed still fail. One convergence-region point
drops from distinct to between, which the criterion permits.

**Same knobs, A1b against Hall (the worst family and the three scaled
ones):**

| | reported | corrected |
|---|---|---|
| medium, as it is | 154.99, late texture (the artefact) | unchanged: late texture is not a scaled family |
| medium: buildup / energy profile / echo density | 14.21 / 14.55 / 6.32 | 11.39 / 7.95 / 5.05 |
| short: worst | 10.34, buildup | **8.29**, buildup |
| short: energy profile / echo density | 9.11 / 4.04 | 4.98 / 3.23 |
| long: buildup / energy profile / echo density | 17.59 / 15.56 / 5.93 | 14.10 / 8.50 / 4.74 |
| arrival removed (D\*), worst — medium / short / long | 10.25 / 7.38 / 13.00 | 9.40 (stereo) / 5.91 / 10.42 |

**DA0** (A1b with Hall's tone, against A1b): 134.82 / 2.21 / 144.12 →
134.82 / **1.76** / 144.12. Its reading — not measurable this way, because
the flat-filter control itself reads 58 and 75 — is unchanged; that
control is late texture and is not a scaled family.

### 4.2 AS1

| result | reported | corrected | before → after |
|---|---|---|---|
| preservation, buildup family vs A1b — medium / short / long | 0.76 / 1.16 / 0.91 | 0.61 / 0.93 / 0.73 | within → within |
| preservation, **energy-profile family, short** | **1.52** | **0.83** | **outside → within** |
| preservation, energy profile — medium / long | 0.57 / 0.47 | 0.31 / 0.26 | within → within |
| preservation, t10, t50, shape | unaffected | unaffected | t10 and shape outside at all three; t50 outside at short |
| **preservation, overall** | fails | **fails** | on t10 and the shape number at all three points and t50 at short. The energy-profile failure at short was the error's; it is withdrawn |

Nine-point search, onset-aligned, AS1:

| Size, Decay | region | reported | corrected | class before → after |
|---|---|---|---|---|
| 25, 0.8 | convergence | 1.31 | 1.30 (run 1.19) | REPRODUCED → REPRODUCED |
| 60, 0.8 | convergence | 1.41 | 1.26 (run 1.17) | REPRODUCED → REPRODUCED |
| 85, 0.8 | convergence | 2.62 | 2.03 | between → between |
| 25, 2.5 | core | 1.59 (run 1.26) | 1.58 (run 1.51) | REPRODUCED → **between** |
| **60, 2.5 (default)** | core | 1.67 (run 1.54) | **1.55 (run 1.40)** | between → **REPRODUCED** |
| 85, 2.5 | core | 3.43 | 2.47 | distinct → **between** |
| 25, 5.0 | core | 2.14 | 1.94 | between → between |
| 60, 5.0 | core | 2.24 | 1.92 | between → between |
| 85, 5.0 | core | 3.09 | 2.24 | distinct → **between** |
| | | 2 of 9 distinct, 3 reproduced | **0 of 9 distinct, 3 reproduced, the default among them** | NOT DISTINCT → **PRESET-EQUIVALENT** |

One point moved the other way (Size 25, Decay 2.5: a run at 1.26 became
1.51). That is the search finding a different optimum under the corrected
objective, not a reading in AS1's favour: the verdict as a whole is
harsher.

Hall + Pre-Delay verification, AS1: medium 1.47 (run 1.34) → 1.50
(run 1.31), mimic → mimic; short 1.27 → 1.16, mimic → mimic; long 1.59 →
1.58, weak → weak. Absolute time: 4.19 / 2.05 / 9.19 → 4.17 / **1.21** /
5.20.

**AS1 remains failed.** No methodological issue appeared in the re-run.

### 4.3 The scan (family columns only; onset-aligned, against Hall)

| | buildup, reported → corrected | energy profile, reported → corrected |
|---|---|---|
| A1b, medium / short / long | 9.39 / 6.27 / 12.69 → 7.53 / 5.03 / 10.17 | 5.07 / 3.74 / 5.40 → 2.77 / 2.05 / 2.95 |
| Stage 3 Ambient B | 9.01 / 9.51 / 7.23 → 7.22 / 7.62 / 5.80 | 5.41 / 6.39 / 5.40 → 2.95 / 3.49 / 2.95 |
| A1a | 7.11 / 4.70 / 11.75 → 5.70 / 3.77 / 9.42 | 4.23 / 2.92 / 4.70 → 2.31 / 1.59 / 2.57 |
| A3 | 8.06 / 2.94 / 8.54 → 6.46 / 2.36 / 6.85 | 3.24 / 2.20 / 3.54 → 1.77 / 1.20 / 1.93 |
| AT5a (8 % direct) | 1.29 / 0.59 / 2.12 → 1.04 / 0.47 / 1.70 | 1.40 / 0.90 / 2.42 → 0.76 / 0.49 / 1.32 |
| control: Hall, Attack max | 14.40 / 24.44 / 10.16 → 11.54 / 19.59 / 8.14 | 3.37 / 5.46 / 3.19 → 1.84 / 2.98 / 1.74 |
| control: Hall, pure shift | 0.01 / 0.01 / 0.03 → 0.01 / 0.03 / 0.97 | — |

Every structure is scaled alike within a column, so the ordering, the rank
correlations and the conclusions of the scan are unchanged. The absolute
sizes of the buildup and energy-profile separations are smaller than
reported. (The pure-shift control's 0.97 at long is on the buildup family
and appears in both runs' logs under a different column cut; it is under
1.0 either way.)

### 4.4 Plate, secondary sample rates (limit ≤ 1.5; all were passes)

| | reported | corrected | |
|---|---|---|---|
| S0 control, Hall vs Hall at 48 kHz — 44.1 kHz | 0.69 / 0.79 / 1.19 | 0.55 / 0.63 / 0.95 | pass → pass |
| — 96 kHz | 0.53 / 0.70 / 0.65 | 0.53 / 0.69 / 0.65 | pass → pass |
| — 192 kHz | 0.80 / 0.74 / 1.14 | 0.80 / 0.74 / 0.91 | pass → pass |
| S3 Plate vs Plate at 48 kHz — all three rates | 0.48 – 0.75 | unchanged to two decimals | pass → pass |
| the band limit alone | 0.55 / 0.32 / 0.43 | 0.44 / 0.32 / 0.43 | |
| Plate vs Hall at 48 kHz, "for scale" | 4.11 / 3.77 / 4.13 | unchanged | |

No Plate conclusion changes. S1, S2, S4, S5, S6 do not use the metric.

### 4.5 The Stage 7 listening package's key

`KEY.txt` and section 2.3 of `Phase12-Stage7-Reset.md` give same-knob
scores written without the baseline. **The package is not re-rendered and
`KEY.txt` is not edited.** Corrected:

| | in the key | corrected |
|---|---|---|
| Hall vs A1b, medium / long | 155 / 298 (late texture) | unchanged in value — and the late-texture artefact in any case |
| Hall vs A1b, short | 10.3 (buildup) | **8.3** |
| Hall vs AT5a | 9.7 / 3.7 (echo density) / 13.8 (late texture) | 7.8 / 3.0 by division, not re-measured / 13.8 |

The audio, the level matching, the octave tables and the level-by-source
table in the key do not use the metric and stand.

## 5. Classifications after the correction

Decided on the interim figures and confirmed by the full re-run:

- **A1b** — a temporal / sonic research reference only. At the default
  point Hall with Pre-Delay and its visible controls reaches **1.28** with
  no target sample discarded: REPRODUCED under the frozen threshold. Five
  of nine points distinct; two core points between.
- **AS1** — failed. Does not preserve A1b (t10 and shape at all three
  points), and is PRESET-EQUIVALENT to Hall under the nine-point search
  (reproduced at the default).
- **Plate PV3** — unchanged.
- Stages 3 – 6 — unchanged.

## 6. So that it cannot recur

1. **The baseline can no longer be skipped.** `floorOf()` — the one
   function every score reads the baseline through — now measures it if it
   has not been measured. A caller cannot obtain an empty baseline. This is
   one change in `Tests/Phase12Structural.cpp`; the baseline's values are
   the ones `ensureFloor()` has always produced, and no number of the
   metric changes. It is the only edit to existing rig code in Stages 7 – 9
   and is made by instruction.
2. **A self-consistency test in the fast regression**
   (`runPhase12MetricBaselineTests`, four checks). It deliberately asks
   for a score *without* initialising the baseline, as the diagnostic
   commands did, and passes only if: the baseline is found measured; the
   diagnostic path and the gate path give identical normalised scores,
   family by family; every family is its distance over the larger of its
   baseline and 1; and at least one family is divided by more than 1, so
   an unscaled reading would be detected. Run on its own in a fresh
   process, it is the exact situation in which the error occurred.
3. Every Stage 8 and Stage 9 diagnostic suite, and the Plate rates suite,
   also calls `ensureFloor()` explicitly at its start.

## 7. Documents

The affected reports are left as written. Each now carries a dated note at
its head pointing here: `Phase12-Stage7-Reset.md`,
`Phase12-Stage7b-Plate-Validation-Addendum-1.md`,
`Phase12-Stage8-Ambient-Addendum-1.md`, `-Addendum-2.md`,
`Phase12-Stage8-Ambient-Diagnosis-Report.md`,
`Phase12-Stage8-Ambient-Scan-And-RootCause.md`,
`Phase12-Stage8-AS1-Report.md`, `Phase12-Stage8-PreDelay-Verification.md`.
Where those documents and this one differ on a frozen-metric score, this
one governs.

## 8. Regression

Not yet re-run in full. By decision the fast regression is run after the
AS2 measurement cycle, to cover the research changes together. What has
been run: the new self-consistency suite, 4 / 4, in a fresh process.
