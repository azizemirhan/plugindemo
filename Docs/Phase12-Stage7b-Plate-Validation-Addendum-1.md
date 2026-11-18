# Phase 12 Stage 7b — Plate validation: addendum 1 (the fitting law, the statistic, the tool)

Decided 2026-11-18. Addendum to
`Docs/Phase12-Stage7b-Plate-Validation-Setup.md`. Where the two differ,
this page governs. Research only; `Source/` is unchanged.

## 1. The sample-rate rule, approved

The 240-section topology is fixed at every supported rate. The allpass
coefficient is derived per rate by **one deterministic law**, the same at
44.1, 48, 96 and 192 kHz. No coefficient is tuned by hand.

**The law.** A first-order allpass with coefficient a delays zero frequency
by D(a) = (1 − a) / (1 + a) samples. The coefficient at rate fs is the one
at which that delay, *in seconds*, equals the 48 kHz section's:

    R = D(−0.7) · fs / 48000
    a(fs) = −(R − 1) / (R + 1)

| rate | coefficient |
|---|---|
| 44.1 kHz | −0.677744 |
| 48 kHz | −0.700000 (the frozen reference, returned exactly) |
| 96 kHz | −0.837838 |
| 192 kHz | −0.915493 |

**Why a closed form and not a least-squares fit.** The instruction is to
match the 48 kHz behaviour "as closely as possible" by a deterministic law
without manual tuning. A fit needs a band, a weighting and a stopping
tolerance — three places where a number can be chosen to suit. The closed
form has none. To show what that choice costs, the tool also computes, as a
diagnostic only, the single coefficient that minimises the largest
group-delay error over 100 Hz – 10 kHz:

| rate | the law: worst error | best any single coefficient can do |
|---|---|---|
| 44.1 kHz | 0.070 ms | 0.065 ms |
| 96 kHz | 0.262 ms | 0.246 ms |
| 192 kHz | 0.322 ms | 0.303 ms |

The law is within 0.02 ms of the best possible. The remaining error is not
the law's: 240 first-order sections cannot reproduce the 48 kHz curve
exactly at another rate with any coefficient. The diagnostic coefficient is
never used to build anything.

**It is not a redesign.** Section count, structure, pipelining and the
48 kHz response are untouched. One number per rate changes, and it is
computed, not chosen.

## 2. The statistic, worded precisely

This replaces the phrase "95 % upper bound" in the Setup document,
section 1.

- **The parameter.** The *median* of the distribution of the per-pair ratio
  (Plate block time / Hall block time) on this machine, in this condition.
- **The bound.** With exactly 31 pairs, the 22nd smallest ratio is a
  distribution-free, one-sided **upper confidence bound on that median**.
  Its confidence is 98.5 %: the median exceeds the 22nd of 31 only if 22 or
  more pairs fall below it, probability 0.0147. (With the 10th smallest it
  forms a two-sided interval of 97.1 %.) It is therefore conservative
  against a nominal 95 %.
- **The assumption.** The 31 pairs are independent draws. Consecutive
  timing blocks are not perfectly independent (thermal drift, background
  activity), so the stated confidence is an upper figure. Interleaving, the
  alternating order inside pairs and three separate runs are the defences;
  they do not make the assumption true.
- **What it is not.** It is **not** a bound on the 95th percentile of the
  ratio. It does not show that 95 % of blocks cost under 1.75 × Hall, and
  it says nothing about the worst block. It bounds where the *typical*
  ratio lies.
- **The criterion.** The 22nd smallest of 31 ratios below 1.75, at each of
  the three operating points, in each of three runs.

## 3. The pipeline delay in the group-delay analysis

The 15-sample delay is included explicitly. The tool reports the chain's
group delay per rate, the pipeline delay per rate in milliseconds, and the
comparison against 48 kHz both without it ("chain") and with it ("total").

The pipeline delay is 0.340 / 0.312 / 0.156 / 0.078 ms at 44.1 / 48 / 96 /
192 kHz. It is the same at every frequency, so it moves the whole wet path
and changes no dispersion. The law is fitted on the chain alone and is not
asked to compensate for it: doing so would trade dispersion shape for a
constant offset.

Check S1 as registered is on the chain. The total is evaluated against the
same limit and reported as a finding where it is outside (section 5).

## 4. The tool

Test code only: `Tests/Phase12Stage7Plate.h`, `.cpp`,
`Tests/Phase12Stage7Platform.cpp`; three lines in `Tests/RunAll.cpp`,
`Tests/Suites.h`, `Tests/CMakeLists.txt`.

| command | what it does |
|---|---|
| `AuroraVerbTests.exe runPhase12Stage7PlateCpu` | **one run** of the primary closure: 48 kHz, three operating points, 3 warm-up pairs then exactly 31, the criterion of section 2. Run it three times, the third after a restart |
| `AuroraVerbTests.exe runPhase12Stage7PlateStress` | Size 0 and 100, modulation off, silence after 100 s, and the scalar form (PV1) at three points. Reported, not gating |
| `AuroraVerbTests.exe runPhase12Stage7PlateRates` | 44.1 / 96 / 192 kHz: S0 to S7 |

- These three run **only when named exactly**. They are not part of the
  regression, fast or full, and a substring filter does not start them.
- A fourth suite, `runPhase12Stage7PlateFastTests`, **is** part of the
  regression: ten checks on the tool's own arithmetic (the law, the ranks,
  PV3 against the plain chain at every rate's coefficient, losslessness,
  the resampler). Nothing about CPU.
- The tool raises its priority and pins itself to one core. **It cannot
  establish that the machine is idle.** It prints, per condition, the
  slowest Hall block over the fastest; near 1.00 means a quiet machine.

**Two differences from Stage 6's timer, both stricter.** Stage 6 timed a
call that allocated its output inside the timed region, the same amount for
Hall and Plate, which pulls a ratio towards 1. The tool times the sample
loop with no allocation. And the two rigs live for the whole campaign, so
the figure is the steady state, not a cold start. Stage 6's figures and
this tool's are therefore not the same measurement.

**How the cross-rate comparison is made.** A response is rendered at its
rate, band-limited to 20 kHz by a linear-phase filter, resampled to 48 kHz
and read by the frozen metric. The 48 kHz reference goes through the same
filter. The control S0 (Hall at the rate against Hall at 48 kHz) decides
whether the comparison is valid at that rate at all.

## 5. The tool's check run

The tool was run once to verify that it works. The machine was in use.

**S0 – S6 do not depend on the machine's load** — they are arithmetic and
rendered responses — and their figures are results. The log is
`Docs/Phase12-Stage7b-runs/01-plate-secondary-rates-tool-check.txt`.

| check | 44.1 kHz | 96 kHz | 192 kHz | limit |
|---|---|---|---|---|
| S0 control: Hall against Hall at 48 kHz (med / small / large) | 0.69 / 0.79 / 1.19 | 0.53 / 0.70 / 0.65 | 0.80 / 0.74 / 1.14 | ≤ 1.5 — **valid at all three** |
| S1 chain group delay, worst error | 0.070 ms | 0.262 ms | 0.322 ms | 5 % or 0.5 ms — **pass** |
| S2 / S6 PV3 against the plain chain, 15 samples later | exact | exact | exact | 1e-9 — **pass** |
| S2 chain energy | 0.000000 dB | 0.000000 dB | 0.000000 dB | 0.01 dB — **pass** |
| S3 Plate against Plate at 48 kHz | 0.52 / 0.69 / 0.65 | 0.53 / 0.48 / 0.64 | 0.73 / 0.75 / 0.60 | ≤ 1.5 — **pass** |
| S4 RT60, Plate / Hall at the rate | 1.007 / 1.027 / 1.004 | 1.009 / 1.032 / 1.005 | 1.011 / 1.030 / 1.006 | ± 10 % — **pass** |
| S4 RT60, Plate at the rate / at 48 kHz | 1.006 / 1.010 / 1.009 | 0.978 / 0.990 / 0.979 | 0.975 / 0.986 / 0.973 | ± 5 % — **pass** |
| S5 Size: t10 ratio largest / smallest (48 kHz: 1.697) | 1.723 | 1.761 | 1.772 | monotone, within 10 % — **pass** |

For scale: Plate against Hall at 48 kHz, same knobs, through the same
filter, reads 4.11 / 3.77 / 4.13; the band limit alone moves Hall by
0.55 / 0.32 / 0.43.

**Three things in that table that are not simply "pass":**

1. **S1 passes on the absolute allowance, not the relative one.** At
   10 kHz the chain's delay is 2.26 ms at 48 kHz and 1.94 ms at 192 kHz:
   14 % short (12 % at 96 kHz). The 5 % line alone holds at 4 kHz (3.5 %
   short) and fails by 8 kHz (10 %). It passes because the shortfall is 0.32 ms, under the
   0.5 ms allowance — the number in the Setup document that I flagged as my
   own judgement. Below 2 kHz the match is within 1.3 %.
2. **With the pipeline delay added, 192 kHz is outside the same limit.**
   Total error 0.556 ms at 10 kHz against 0.5 ms: the chain is 0.32 ms
   short and the pipeline is another 0.23 ms shorter than at 48 kHz, in the
   same direction. 96 kHz is inside (0.418 ms). The registered S1 is on the
   chain and passes; this is reported as a **finding for decision**, not
   settled either way here.
3. **The metric does not see either of these.** S3 reads 0.48–0.75 at
   every rate: by the frozen metric the Plate at 192 kHz is the 48 kHz
   Plate. Whether a 0.3–0.6 ms shortening of the highest octave's arrival
   is audible is a listening question I have not answered.

**S7 and the CPU suites were run on a machine in use and are NOT results.**
They show only that the tool runs:

| condition (48 kHz unless stated) | median ratio | note |
|---|---|---|
| three operating points | 1.55 – 1.57 | upper bounds 1.56 – 1.57 |
| 44.1 / 96 / 192 kHz, medium | 1.54 / 1.55 / 1.55 | no sign of a rate-specific regression |
| Size 0, Size 100, silence | 1.55 – 1.57 | |
| **modulation off** | **1.91** | Hall drops from about 510 to 290 ns per sample; the chain's cost does not change |
| **PV1, the scalar form** | **1.84 – 1.87** | |

Two of these would matter if an idle machine confirmed them:

- **Modulation off.** The chain adds a nearly constant 270–290 ns per
  sample in every condition. Against a Hall that has become cheaper, the
  same added cost is a larger ratio. This is a stress condition and does
  not gate; it does show that "× Hall" is not one number, and that the
  gate is measured where Hall is at its most expensive.
- **PV1.** If this holds, Plate inside 1.75 × depends on the two-wide
  register path (SSE2 / NEON) being present. There is no portable fallback
  inside the limit.

## 6. What is still owed for Plate

1. Three runs of `runPhase12Stage7PlateCpu` on an idle machine, the third
   after a restart. This is the closure and it has **not** been done.
2. `runPhase12Stage7PlateStress` and `runPhase12Stage7PlateRates` once
   each on an idle machine, for the CPU figures.
3. A decision on finding 2 of section 5 (the total at 192 kHz).
4. PV3's own ten-seed distribution; other processors; real-time safety on
   the chain — as listed in the Setup document, section 3. Not started.
