# Phase 12 Stage 7b — Plate PV3: validation setup, final

Research only. `Source/` is unchanged. **Nothing here has been run.** This
fixes the procedure of `Phase12-Stage7-Reset.md` section 5 with the
sample-rate definition approved on 2026-11-18, before any measurement.

The candidate is unchanged: the 240-section reference chain as 16
sub-chains, two-wide, kept in registers (PV3). Plate is not redesigned
unless this validation disproves it.

## 1. Primary CPU closure — 48 kHz

**Machine.** Idle: no other applications, mains power, a fixed performance
plan, the process at high priority and pinned to one core. Ten minutes of
load before the first reading so the thermal state is settled.

**What is timed.** The wet path of Hall and of Plate PV3, as in Stage 6's
engineering table, single thread.

**Pairs.**

- One pair = one block of Hall and one block of Plate, back to back, each
  block at least 2 s of audio. The pair's figure is the ratio
  Plate time / Hall time.
- The order inside the pair alternates (H P, P H, H P, …) so that whichever
  runs second is not always the same one.
- Three warm-up pairs are run and discarded. Then **exactly 31 pairs**. The
  number is fixed in advance; a run is not extended or cut after looking at
  it.

**Statistic.** Sort the 31 ratios.

- Reported: the median (16th), the interval from the 10th to the 22nd, and
  the ratio of the two minima.
- The 10th-to-22nd interval is the exact distribution-free interval for a
  median of 31 values; its coverage is 97.1 %. The next narrower one
  (11th to 21st) covers 92.9 %, so this is the narrowest that reaches 95 %.
- **The 95 % upper bound is the 22nd smallest ratio.**

Section 5.1 of the Stage 7 plan said "95 % interval" without naming the
method. This is the method, fixed here before any data. It assumes nothing
about the shape of the timing distribution, which matters because timing
noise is one-sided and heavy-tailed.

**Conditions that gate.** The three operating points — medium (60, 2.5 s),
small / short (25, 0.8 s), large / long (85, 5.0 s) — with modulation at
its default, driven by noise.

**Runs.** Three complete runs of all three points; the third after a
restart of the machine.

**Pass.** The 22nd ratio is **below 1.75** at every point in every run:
nine figures, all under the line. (The Stage 7 plan wrote "≤ 1.75"; the
approved wording is "below", which is the stricter reading and is used.)

Preferred, reported, not required: median ≤ 1.5.

**Stress conditions, reported with the same 31 pairs, one run each:** Size
0 and Size 100; modulation off; input silence after an excitation (the
denormal case). These do not gate. Any one with an upper bound at or above
1.75 is reported as an open finding for decision, not passed over.

**Fallback form.** The scalar eight-way chain (PV1) is measured the same
way at the three points, one run. If only PV3 passes, the report says that
Plate depends on the two-wide register path being available.

## 2. Secondary rates — 44.1, 96 and 192 kHz

No full campaign at these rates unless one of the checks below fails.

### 2.1 What has to be said first: Plate is not yet defined off 48 kHz

Every Plate result so far is at 48 kHz. The chain is 240 first-order
allpass sections with coefficient −0.7. That fixes its dispersion in
*samples*. Run unchanged at another rate, the same chain gives:

| rate | group delay of the chain near DC |
|---|---|
| 44.1 kHz | 30.8 ms |
| 48 kHz | 28.3 ms (the Plate that was measured) |
| 96 kHz | 14.2 ms |
| 192 kHz | 7.1 ms |

(Calculated from the section's formula, not measured.) At 96 kHz the
unchanged chain has half the dispersion in time, with its curve stretched
an octave upward. That is a different Plate. So the secondary validation
cannot be "run it at the other rates and look": it needs a rule that says
what Plate *is* at rate fs, and then checks that rule.

Three rules are possible. They are tried in this order, and the first that
passes all of 2.2 is the definition:

1. **Same 240 sections, coefficient re-fitted per rate** so that the
   chain's group delay near DC is the 48 kHz value in milliseconds. By
   calculation that is about −0.678 at 44.1 kHz, −0.838 at 96 kHz and
   −0.916 at 192 kHz. Cost per sample is unchanged, so the ratio to Hall
   should hold. By calculation the delay curve then matches the 48 kHz one
   closely at low and mid frequencies and falls short towards the top
   (about 3 % at 4 kHz and 12 % at 10 kHz, at 96 kHz). Whether that is
   inside the limit of 2.2 is what the check decides.
2. **Sections scaled with the rate** (480 at 96 kHz, 960 at 192 kHz) with a
   re-fitted coefficient. Closer in shape; the chain's cost per sample
   scales with the rate, and by estimate the ratio to Hall would be far
   over 1.75 at 192 kHz.
3. **The chain run at a fixed internal 48 kHz** with resampling around it.
   Exact by construction at 96 and 192 kHz; awkward at 44.1 kHz; adds a
   resampler's latency and cost.

Rule 1 is not a redesign: the structure, the section count and the 48 kHz
behaviour are untouched. If only rule 2 or 3 passes, that is reported as a
finding with its cost before anything is adopted.

**Readiness of the rig.** The research rig takes a sample rate, but it has
never been run at another one and some helpers use the 48 kHz constant
directly. Step zero of the secondary validation is a check that Hall itself
measures the same at each rate (the control in 2.2). Until that holds, no
Plate figure at that rate means anything.

### 2.2 The checks, with their limits fixed now

Each at 44.1, 96 and 192 kHz, at the three operating points unless stated.

| # | check | limit |
|---|---|---|
| S0 | **Control.** Hall at fs, resampled to 48 kHz, against Hall at 48 kHz, with the frozen metric at the same knobs | ≤ 1.5. If it is not, the rig is not valid at that rate and S1–S6 are not read |
| S1 | **Group delay of the chain** at fs against the 48 kHz chain, in milliseconds, 100 Hz to 10 kHz | within 5 % or 0.5 ms, whichever is larger, at every frequency |
| S2 | **Impulse response of the chain alone**: lossless, and the same after the pipelining as before it | energy within 0.01 dB; PV3 equal to the plain chain at that rate to 1e-9 after a shift of exactly 15 samples |
| S3 | **The same Plate.** Plate at fs, resampled to 48 kHz, against Plate at 48 kHz, frozen metric, same knobs | ≤ 1.5 (the PRESERVED line of Stage 6) |
| S4 | **Decay.** RT60 against the Decay control; and against the 48 kHz value | ± 10 % (Stage 6); within 5 % of the 48 kHz value |
| S5 | **Size.** t10 against Size, five Sizes | monotone; the ratio largest / smallest within 10 % of the 48 kHz ratio |
| S6 | **The pipeline delay.** The wet path's extra delay against Hall | exactly 15 samples at every rate: 0.34, 0.31, 0.16 and 0.08 ms. Reported in milliseconds per rate |
| S7 | **CPU.** Seven interleaved pairs, medium point, same procedure as section 1 | the median ratio no more than 0.15 above the 48 kHz median, and under 1.75 |

On S1: the limit was written after I had calculated a few points of rule 1
(the figures in 2.1). It was not tuned to them — at 10 kHz rule 1 is
predicted to miss the 5 % and to be saved or not by the 0.5 ms allowance,
which I chose as roughly the smallest timing shift in a dispersive onset
worth calling a change. That allowance is my judgement and is the number in
this table most open to challenge.

On S6: the delay is a fixed number of samples, so in milliseconds it
differs between rates by at most 0.26 ms. It is a pure delay of the whole
wet path, far below any pre-delay step. The check is that it is exactly
that and nothing else — S2 establishes it. Whether a sub-millisecond,
rate-dependent wet offset is acceptable is recorded for decision; I expect
it to be inaudible and have not listened.

### 2.3 What triggers a full campaign at a secondary rate

S7 failing either limit at a rate: the full 31-pair, three-point procedure
of section 1 is run at that rate, and its result is reported beside the
48 kHz one. The gate remains the 48 kHz figure unless you decide otherwise.

## 3. Also carried from the Stage 7 plan, unchanged

- PV3's own ten-seed distribution at the three points (Stage 7 section
  5.3). Rule unchanged: no median under the reference's ten-seed minimum.
- Other processors: PV3 is SSE2 on x64 and NEON on ARM; to be measured on
  each target. Not possible on this machine for ARM.
- Real-time safety on the chain: no allocation; no denormal stall.
- Switching cost and pre-warm length belong to the switching design.

## 4. What reopens Plate

Any of:

- a 22nd ratio at or above 1.75 at 48 kHz on an idle machine;
- no rule of 2.1 passing S1–S6 at a secondary rate within the CPU limit;
- a ten-seed median under 2.5, or any run ≤ 1.5;
- a listener who cannot tell it from Hall.

## 5. State

- The procedure is final.
- **The measurement harness is not written.** It is test code only
  (Stage 6's wet-path timer, extended to interleaved pairs and to a rate
  argument) and is the first step once this is given a go-ahead.
- The 48 kHz campaign needs the machine idle for its whole length. It
  cannot be run while the machine is in use, and I cannot establish
  idleness myself.
