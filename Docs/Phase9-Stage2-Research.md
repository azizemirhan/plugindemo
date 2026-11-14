# Phase 9 — Stage 2 research report

Research prototypes and control-independence validation. No production
parameter, no shipping DSP path change, no listening validation.

> ## Final recommendation: REDUCE TO 1 CONTROL — Attack
>
> **Late Diffusion is rejected.** Not because it does nothing at Mod Depth 0,
> but because it works **backwards** there: turning it up from neutral makes
> the late field up to **16.13 more periodic — 5.3× the measurement floor** —
> and the single setting that gets below neutral does so by less than the
> measurement can resolve. The mechanism's benefit exists only when modulation
> is on, which fails gate 1 and gate 6 together.
>
> **Attack is retained**, with **AT-B, feedforward staggered injection**, as
> the winning mechanism. It is bit-exactly superposable, so every transient
> gets the same Attack; it moves buildup by 58 ms while leaving onset exactly
> where Pre-Delay left it; and it preserves wet energy to within 0.39 dB.

---

## 0. Baselines, re-measured rather than quoted

Stage 1's floors were re-derived in this file, because a floor is a property
of the measurement as actually performed and this file uses a different
render helper and tank lifetime.

| measure | this stage | Stage 1 |
|---|---|---|
| echo density (NED) | **0.0766** | 0.077 |
| envelope periodicity | **3.0284** | 3.03 |
| Attack onset | **1.0 ms** | new |
| Attack buildup | **0.0 ms** | new |

The Attack floors come from eight realisations at different moments in the
modulation cycle, **not** from sliding the analysis window: onset and buildup
are landmarks of a single event, so moving the window moves the answer with it
and would report a floor of zero.

Stage 1's two corrections are preserved: near-silent windows are refused
rather than printed, and periodicity is passed its window raw because
`maxEnvelopePeriodicity` detrends internally.

---

# Part A — Late Diffusion

## 1. Candidates tested

| | as briefed | what it became |
|---|---|---|
| **LD-A** | coefficient setter on the existing loop allpass bank | **fails gate 1 by construction, with no measurement needed.** `bypassed` is true whenever Mod Depth is 0, and `process()` returns before any coefficient can matter. Measuring it would only confirm the code |
| **LD-B** | decouple allpass diffusion from modulation | the *requirement*, met by LD-C's mechanism |
| **LD-C** | an independent static late-diffusion element | **the prototype actually built and measured** |

LD-A and LD-C collapse into one prototype in this architecture, and that is a
finding rather than a shortcut: `AllpassPhaseBank` already owns a **second,
allocated, prepared, unused section per line** — `maxSections` is 2 and
production ships `sectionsPerLine` 1. So an independent static element needs
no new memory and no new delay line. It needs the spare section driven by its
own engagement weight instead of by modulation depth.

**The prototype.** `AllpassPhaseBank::setStaticDiffusion(weight, coefficient)`,
research-only, plus per-section engagement in `process()`. Modulation owns
section 0, diffusion owns section 1, and neither depends on the other. The
semantics do not change with Mod Depth — section 1 is always what diffusion
controls — which is the awkward state-dependent alternative the brief said to
flag and this design avoids.

`getAddedLoopDelaySamples` was extended to include the static section's group
delay, and its early return on zero modulation weight removed: at Mod Depth 0
the modulation weight *is* zero while the static path may be engaged, and
returning early there would have hidden the added loop delay from the
decay-gain correction.

---

## 2. Legacy / neutral compatibility — the hard gate: **PASS**

At the research neutral (static weight 0), against a tank that never calls the
setter at all:

| Mod Depth | Size | Decay | High Damp | samples differing | worst diff |
|---|---|---|---|---|---|
| 0 % | 60 % | 2.5 s | 45 % | **0 of 48000** | 0.0 |
| 30 % | 60 % | 2.5 s | 45 % | **0 of 48000** | 0.0 |
| 100 % | 60 % | 2.5 s | 45 % | **0 of 48000** | 0.0 |
| 0 % | 20 % | 1.0 s | 0 % | **0 of 48000** | 0.0 |
| 30 % | 100 % | 6.0 s | 100 % | **0 of 48000** | 0.0 |
| 100 % | 20 % | 6.0 s | 45 % | **0 of 48000** | 0.0 |

**Bit-exact legacy path**, which was the strong preference. The static section
is skipped entirely at weight 0, so no smoother reaches the signal and the
arithmetic is the same arithmetic.

The wider focused regression confirms it at scale: **1377 / 1379**, the two
failures being the registered Phase-7 diagnostics at bit-identical values
(`0.06022872179`, `19.34162509`). No new regressions.

---

## 3. Late Diffusion × Mod Depth

Five research positions — minimum, low, middle, high, maximum — against three
Mod Depths.

| Mod Depth | LD | coeff | periodicity | NED late | kurt late | RT60 proxy |
|---|---|---|---|---|---|---|
| 0 % | min | 0.00 | 16.724 | 0.952 | 0.214 | 2.020 s |
| 0 % | low | 0.20 | **32.854** | 0.981 | −0.015 | 2.080 s |
| 0 % | mid | 0.35 | 29.449 | 0.996 | 0.002 | 2.060 s |
| 0 % | high | 0.50 | 23.249 | 0.994 | −0.004 | 2.080 s |
| 0 % | max | 0.60 | 14.996 | 1.035 | −0.035 | 2.100 s |
| 30 % | min | 0.00 | 23.699 | 0.932 | −0.007 | 2.060 s |
| 30 % | low | 0.20 | 20.172 | 0.965 | 0.053 | 2.100 s |
| 30 % | mid | 0.35 | 15.044 | 0.928 | 0.047 | 2.080 s |
| 30 % | high | 0.50 | 13.714 | 0.998 | −0.006 | 2.080 s |
| 30 % | max | 0.60 | 9.273 | 0.967 | −0.022 | 2.080 s |
| 100 % | min | 0.00 | 21.365 | 0.937 | 0.082 | 2.060 s |
| 100 % | low | 0.20 | 16.117 | 0.959 | 0.042 | 2.100 s |
| 100 % | mid | 0.35 | 15.195 | 0.985 | −0.008 | 2.060 s |
| 100 % | high | 0.50 | 12.939 | 0.967 | 0.039 | 2.080 s |
| 100 % | max | 0.60 | 8.368 | 0.939 | −0.007 | 2.080 s |

### Spans

| Mod Depth | periodicity span | ×floor | NED span | ×floor | RT60 drift |
|---|---|---|---|---|---|
| 0 % | 17.858 | 5.89× | 0.0832 | 1.08× | 3.81 % |
| 30 % | 14.426 | 4.76× | 0.0700 | 0.91× | 1.90 % |
| 100 % | 12.997 | 4.29× | 0.0481 | 0.63× | 1.90 % |

Read on spans alone this looks like a pass: a large periodicity range at every
Mod Depth, with density essentially inside its floor — exactly the signature
the brief describes as what would distinguish Late Diffusion from Density.

**The span is not the whole story, and this is where the candidate dies.**

---

## 4. The reversal — why Late Diffusion fails

At Mod Depth 30 % and 100 % the periodicity falls monotonically as the
coefficient rises: 23.70 → 9.27 and 21.37 → 8.37. Well behaved.

At Mod Depth 0 it does not. Swept finely:

| coefficient | periodicity | vs neutral | direction |
|---|---|---|---|
| off (neutral) | 16.724 | — | — |
| 0.10 | 32.528 | **+15.80** | — |
| 0.20 | 32.854 | **+16.13** | rising |
| 0.30 | 28.398 | +11.67 | falling |
| 0.40 | 27.064 | +10.34 | falling |
| 0.50 | 23.249 | +6.53 | falling |
| 0.60 | 14.996 | **−1.73** | falling |
| 0.70 | 17.324 | +0.60 | rising |
| 0.80 | 17.786 | +1.06 | rising |

**Two direction reversals.** The worst excursion above neutral is **+16.13 =
5.3× the 3.03 floor**. The only point below neutral is −1.73, which is **below
the floor** — not a resolvable improvement on leaving the control alone.

### Why, and it is not mysterious

The static section is a fixed 1–2 ms delay with coefficient *g*. At small *g*
an allpass is very nearly a **pure delay**, and a pure delay added to a
feedback loop is an extra *repeat*, not a disperser — so it **adds** periodic
structure. Only at large *g* does the section disperse more than it adds. At
Mod Depth 30 % and 100 % the modulation smears that extra repeat before it can
become audible periodicity, which is precisely why the reversal appears at
Mod Depth 0 and nowhere else.

That explanation accounts for both halves of the table, which is why it is
reported as the mechanism rather than as a guess.

---

## 5. Stereo, coloration and CPU — all acceptable

| LD | L/R corr | mono/stereo | peak | HF/LF tilt | spectral drift |
|---|---|---|---|---|---|
| min | −0.0265 | −3.13 dB | 0.0347 | 5.97 dB | — |
| mid | −0.0200 | −3.10 dB | 0.0348 | 6.09 dB | +0.12 dB |
| max | −0.0252 | −3.12 dB | 0.0326 | 5.95 dB | −0.03 dB |

No image collapse, no downmix hollowing, worst spectral drift **0.12 dB**.
Coloration was measured rather than argued from allpass flatness, because in a
*feedback* path the section is applied once per circulation and the per-line
gain is recomputed from the effective loop length, so magnitude-flatness of
one section does not settle the loop's behaviour.

| | ns/sample | vs neutral |
|---|---|---|
| LD neutral (Phase 8) | 235.767 | — |
| LD engaged, coeff 0.35 | 242.848 | **+3.00 %** |

Added delay memory **none** — section 1 is already allocated by Phase 5B.
Added state: one `LinearSmoother` per line (8 doubles) plus two scalars. Added
per-sample cost at neutral: one smoother advance and one compare, inside a
branch that already existed.

---

## 6. Late Diffusion verdict: **REJECT**

Against the brief's eight gates:

| | gate | result |
|---|---|---|
| 1 | works at Mod Depth 0 | **FAIL** — it works backwards: +16.13 (5.3× floor) *more* periodic than neutral across most of its travel, with the only sub-neutral point below the floor |
| 2 | works at Mod Depth 30 % | PASS — monotone, 23.70 → 9.27 |
| 3 | meaningful range under modulation | PASS — 4.76× and 4.29× the floor |
| 4 | neutral preserves Phase 8 | PASS — bit-exact, six configurations |
| 5 | changes texture beyond the floor | PASS |
| 6 | does not merely duplicate Mod Depth | **FAIL** — the benefit exists only when modulation is on, and modulation is what makes the mechanism's own added repeat inaudible |
| 7 | does not destabilise decay/damping | PASS — 3.81 % worst on the proxy; would need the Phase 7 certified analyser before any authorisation |
| 8 | CPU justified | PASS — +3.00 %, no new memory |

Two failures, and gate 1 is the one the brief called the key product
requirement. A control that reverses direction inside its own travel, in
exactly the configuration it was built to serve, is not shippable — and the
cause is structural, not a tuning problem: the mechanism's useful component
and its harmful component are the same section, separated only by how strongly
it is driven and by whether modulation is there to hide the harm.

> Per the brief: **"If these cannot all be satisfied cleanly: REJECT Late
> Diffusion. Phase 9 then becomes Attack only. That is an acceptable result."**

---

# Part B — Attack

## 7. Candidates tested

| | mechanism | result |
|---|---|---|
| **AT-A** | wet amplitude envelope | **rejected** — event-dependent, and partly a volume control |
| **AT-B** | feedforward staggered injection | **retained, the winner** |
| **AT-C** | dual / distributed late injection, normalised | **this is AT-B** — the prototype is an immediate tap carrying 25 % of the power plus a rising spread of 23 later taps, L2-normalised. AT-B and AT-C merged |
| **AT-D** | input-diffusion-based | **rejected** — it is Early Diffusion, see §10 |

**AT-B in detail.** A fixed FIR on the signal entering the tank: one immediate
tap plus a linearly rising spread of later taps over the Attack time, taps
L2-normalised so the established late energy stays put while *when* it arrives
moves. Linear, time invariant, outside the feedback recurrence, no onset
detector, no transport awareness.

---

## 8. Continuous-signal result — the mandatory test

Three impulses, 500 ms apart, one continuous render, no reset.

**The first version of this test did not work, and the way it failed matters.**
It measured onset and buildup per event in a window starting at each impulse,
and reported onset 0 ms for events 2 and 3 — because the window opens inside
the previous event's tail, which is already far above any onset threshold. A
landmark metric cannot tell a new event from the decay it arrives on top of,
so per-event landmarks in a continuous render are not a measurement.

**The second version was also wrong**, in a more interesting way. It tested
superposition — three impulses should give the sum of three shifted
single-impulse responses — at the shipping Mod Depth 30 %, and the *baseline*
failed at 9.95 %. A modulated tank is time-varying by design, so three
impulses meet it at three different modulation phases. The test was measuring
Phase 5's accepted character and blaming the Attack mechanism for it.

Run at Mod Depth 0, where Stage 1 verified the tank is time invariant to
1.3e-25:

| mechanism | residual | relative to peak | verdict |
|---|---|---|---|
| baseline, Mod Depth 0 | **0.0** | 0.0000 % | identical on every event |
| baseline, Mod Depth 30 % | 0.00349 | 9.95 % | the tank's own modulation |
| AT-A: wet envelope 150 ms | 0.03464 | **138.29 %** | **event-dependent** |
| **AT-B: staggered 150 ms** | **0.0** | **0.0000 %** | **identical on every event** |

AT-B is **bit-exactly superposable**. Every transient receives the same
Attack, by construction rather than by luck, with no event detection.

AT-A's residual is larger than the signal itself. The reason, as a number
rather than an argument — its envelope is one global ramp started once:

| event | time | envelope gain |
|---|---|---|
| 1 | 0 ms | 0.0000 |
| 2 | 500 ms | **0.9643** |
| 3 | 1000 ms | **0.9987** |

By the second impulse the ramp is 96 % open; there is no attack left to shape.
Making it work per event requires a retrigger, which requires an onset
detector, which the brief asks us not to introduce.

---

## 9. Attack vs Pre-Delay — the distinction holds

| configuration | onset | buildup | NED 20 ms | energy | peak |
|---|---|---|---|---|---|
| baseline | 21 ms | 14 ms | 0.7091 | 0.6071 | 0.0347 |
| **Pre-Delay 50 ms** | **71 ms** | **14 ms** | — | 0.6072 | 0.0347 |
| AT-B staggered 50 ms | 21 ms | 32 ms | 0.7375 | 0.5932 | 0.0297 |
| **AT-B staggered 150 ms** | **21 ms** | **72 ms** | 0.7025 | 0.5549 | 0.0285 |
| AT-A envelope 150 ms | 21 ms | 62 ms | 0.7222 | **0.2398** | 0.0148 |

| comparison | onset change | buildup change |
|---|---|---|
| Pre-Delay 50 ms | **+50 ms** | **0 ms** |
| AT-B staggered 150 ms | **0 ms** | **+58 ms** |

Pre-Delay moves onset by exactly its own value and leaves buildup untouched.
AT-B moves buildup by 58 ms and leaves onset untouched. Against floors of
1.0 ms (onset) and 0.0 ms (buildup), both are decisive. **A 50 ms Attack is
not +50 ms of Pre-Delay, and the two metrics separate cleanly.**

---

## 10. Attack vs Early Diffusion — distinct, and AT-D rejected

| configuration | onset | buildup | NED 20 ms |
|---|---|---|---|
| Early Diffusion 0 % | 36 ms | 0 ms | **0.0547** |
| Early Diffusion 70 % (shipping) | 21 ms | 14 ms | 0.7091 |
| Early Diffusion 100 % | 21 ms | 5 ms | **0.4990** |
| AT-B staggered 150 ms | 21 ms | **72 ms** | 0.7025 |

Early Diffusion owns early density: its NED-at-20 ms span is **0.444, which is
5.8× the floor**, and its effect on buildup is small and non-monotone (0 → 14
→ 5 ms). AT-B is the mirror image: buildup **+58 ms** while NED at 20 ms stays
**inside the floor** (0.709 → 0.703 / 0.738).

> Early Diffusion scatters; Attack delays establishment. Measured on the two
> metrics, they are near-orthogonal — which also disposes of **AT-D**: adapting
> the input diffuser to shape buildup would be another scaling of Early
> Diffusion, and the brief says to reject that.

---

## 11. Buildup metrics

| metric | definition | floor |
|---|---|---|
| **onset** | first 2 ms window reaching 40 dB below the eventual peak | 1.0 ms |
| **buildup** | time from onset to the first window within 3 dB of the established level | 0.0 ms |

Two scalars, deliberately, because neither is sufficient alone: onset alone
cannot tell Attack from nothing, and buildup alone cannot tell Attack from
Pre-Delay. Stage 1 additionally established that the landmark pair cannot
distinguish a formation control from a fader — which is what §12 is for.

---

## 12. Energy normalization findings

| configuration | buildup | energy | relative | peak | RT60 proxy |
|---|---|---|---|---|---|
| AT-B off | 14 ms | 0.60708 | 0.00 dB | 0.0347 | 2.060 s |
| AT-B 50 ms | 32 ms | 0.59316 | **−0.10 dB** | 0.0297 | 2.020 s |
| AT-B 150 ms | 72 ms | 0.55491 | **−0.39 dB** | 0.0285 | 2.080 s |
| AT-B 300 ms | 148 ms | 0.57845 | **−0.21 dB** | 0.0223 | 2.080 s |
| AT-A 150 ms | 62 ms | 0.23982 | **−4.03 dB** | 0.0148 | — |

This is the column that separates the two mechanisms on substance rather than
on convenience.

**AT-B preserves wet energy to within 0.39 dB** across a 10× Attack range,
while buildup goes 14 → 148 ms. The peak falls, which is correct and expected:
the same energy spread over more time has a lower peak. So AT-B changes **when
energy arrives**.

**AT-A loses 4.03 dB — 60 % of the energy.** It is substantially a volume
control, and its buildup measurement is partly reading that. The brief warned
against confusing "quieter" with "slower Attack"; AT-A is mostly the former.

No energy-matched diagnostic was needed to reach that conclusion: the raw
numbers separate the mechanisms by an order of magnitude. Both raw figures are
reported above; nothing is normalised anywhere in this stage.

---

## 13. Attack CPU

AT-B is a 24-tap FIR on the wet signal before the tank, outside the
recurrence. The prototype is written as a scatter over a sparse drive and is
not a shippable implementation, so no ns/sample figure from it would mean
anything. What can be said architecturally:

- **Added delay memory:** one ring buffer of Attack-maximum length per channel.
  At a 500 ms maximum that is 24000 doubles per channel at 48 kHz — the same
  order as the Phase 8 pre-delay line, which already allocates 500 ms.
- **Added per-sample cost:** 24 multiply-accumulates per channel, i.e. ~48
  operations against a tank carrying eight delay lines, an 8×8 matrix and
  sixteen damping shelves. Expect **LOW to MEDIUM**, comparable to the
  4-stage input diffuser's measured ~52 ns/sample stereo.
- **Neutral cost:** zero. At Attack 0 the FIR is a single unit tap and the
  stage can early-out on the Phase 8 flag-bypass pattern.
- **Added state:** the tap weights and offsets, recomputed at control rate.

A real figure belongs to Stage 3, measured on a real implementation.

---

## 14. Attack verdict: **RETAIN — AT-B, feedforward staggered injection**

| | requirement | result |
|---|---|---|
| works in a continuously running reverb | **PASS** — bit-exactly superposable |
| no onset detector | **PASS** — fixed LTI FIR |
| no transport awareness | **PASS** |
| onset stays fixed | **PASS** — 21 ms at every Attack setting |
| buildup moves materially | **PASS** — 14 → 148 ms, floor 0.0 ms |
| not a volume control | **PASS** — energy within 0.39 dB |
| distinct from Pre-Delay | **PASS** — orthogonal on the metric pair |
| distinct from Early Diffusion | **PASS** — 5.8× floor apart on which metric each owns |
| outside the recurrence | **PASS** — decay proxy within 0.040 s (1.9 %) |

---

## 15. Decay / damping regression findings

Nothing required retuning, and nothing was retuned.

| mechanism | worst RT60 proxy change | note |
|---|---|---|
| Late Diffusion (rejected) | 3.81 % | in the recurrence; `loopDelayCorrection` absorbed it, and the static section's group delay was added to that correction |
| Attack AT-B | 0.040 s on 2.06 s = **1.9 %** | outside the recurrence; structurally cannot touch it, and the measurement agrees |

Both figures are from a 30 dB-fall proxy with 20 ms granularity, so ~1 % is
its own quantisation. **Neither is a Phase 7 gate**, and any production
integration must re-measure against the certified ensemble analyser before
authorisation. Phase 7 was not reopened.

---

## 16. Stereo and modulation findings

**Late Diffusion.** No stereo cost at all: correlation −0.0265 → −0.0252 →
−0.0200, mono/stereo −3.13 → −3.10 dB, spectral drift ≤0.12 dB. Its failure is
not a stereo failure.

**Modulation.** The decisive interaction, and it runs the other way from the
usual worry: Late Diffusion does not damage modulation, modulation *rescues*
Late Diffusion. The static section adds a short fixed repeat to each loop, and
only modulation smears that repeat before it becomes audible periodicity. A
control whose harm is cancelled by another control's being on is a control
whose range is not its own.

**Attack.** AT-B is applied before the injection vectors and is identical on
both channels, so it cannot alter the stereo geometry — the image comes from
the injection/readout vectors and the input diffuser's differing patterns, all
downstream of it. Worth confirming on a real implementation in Stage 3, since
a per-channel variant would be a deliberate choice rather than an accident.

---

## 17. Proposed Phase-9 production architecture

```
input
  -> Low Cut -> High Cut -> Pre-Delay                      (PHASE 8)
  -> [ATTACK: staggered injection FIR]                     <- PHASE 9, new
       immediate tap + rising spread over 0..N ms
       L2-normalised; Attack 0 = single unit tap = bypass
  -> input diffuser, 4 allpass stages                      <- Early Diffusion
  -> injection vectors -> FdnCore<8> -> readout vectors    (unchanged)
  -> Size compensation -> wet trim -> Width -> Mix
```

One new stage, one new control, on the wet path, outside the recurrence,
bypassable to a bit-exact Phase 8 at Attack 0.

**Where it sits relative to the input diffuser is a Stage-3 question.** Before
it, Attack spreads a clean transient and the diffuser scatters the spread.
After it, Attack spreads an already-scattered signal. Both are LTI and both
are defensible; they are not the same sound, and the choice should be measured
and listened to rather than assumed.

### Parameter-contract consequences

- `attack` already exists in `ParameterLayout.cpp`, 0–100 %, **default 0 %**,
  smoothed in the processor and consumed by no DSP. Its default is already the
  neutral this architecture needs, so legacy sessions restore a neutral
  Attack and sound unchanged. **No contract change required.**
- `lateDiffusion` exists at **default 70 %** and is now not recommended for a
  control. Its ID stays reserved and unconsumed, exactly as it is today.
- `density` exists at **default 70 %** and was rejected in Stage 1. Same.
- Had Late Diffusion survived, its existing 70 % default would have been a
  real problem: APVTS serialises every registered parameter, so sessions saved
  since the ID was added already carry 0.70, and a neutral-at-zero design
  would have had those sessions restore a non-neutral value. Worth recording,
  since it applies to any future attempt to consume those two IDs.

---

## 18. Technical risks carried into Stage 3

1. **The decay proxy is not a Phase 7 gate.** AT-B's 1.9 % must be confirmed
   against the certified ensemble analyser.
2. **Attack's position relative to the input diffuser is unresolved**, and the
   two orders are different products.
3. **The 500 ms Attack range is an assumption.** 300 ms already produces a
   148 ms buildup; whether anyone wants more is a listening question.
4. **AT-B's tap profile is a first guess** — 24 taps, 25 % immediate share,
   linear rise. Nothing has been swept. The buildup/energy behaviour is good
   enough to justify the mechanism, not to fix its tuning.
5. **No listening has been done.** Every Attack finding in this stage is a
   measurement.

---

## 19. Final recommendation

> ### REDUCE TO 1 CONTROL — Attack
>
> **Late Diffusion: REJECT.** It fails gate 1 and gate 6. At Mod Depth 0 —
> the case the mechanism exists to serve — turning it up from neutral makes
> the tail up to 16.13 more periodic, 5.3× the measurement floor, with two
> direction reversals across its travel and no resolvable improvement on
> neutral anywhere. The cause is structural: at the coefficients where the
> section disperses least it behaves as a pure delay and adds a repeat, and
> only modulation hides that. Its benefit is not separable from Mod Depth.
>
> Everything else about the candidate was fine — bit-exact neutral, no stereo
> cost, 0.12 dB of coloration, +3.00 % CPU, no new memory. It is rejected on
> behaviour, not on cost, which is the more final kind of rejection.
>
> **Attack: RETAIN, mechanism AT-B.** Bit-exactly superposable, so every
> transient gets the same Attack with no onset detector and no transport
> awareness. Onset fixed at 21 ms while buildup moves 14 → 148 ms. Wet energy
> preserved within 0.39 dB, against AT-A's 4.03 dB loss. Orthogonal to
> Pre-Delay and to Early Diffusion on the metrics each of them owns.
>
> Phase 9 ships one control. Four were in the roadmap, Stage 1 supported two,
> and measurement has taken it to one. That is the roadmap meeting evidence,
> and it is the acceptable result the brief named in advance.

---

## STOP

Stage 2 ends here. No shipping Phase-9 DSP, no production parameters, no
listening validation, Phase 10 not started. The research path added to
`AllpassPhaseBank` is inert at its default and proved so bit-exactly; it can
be removed or kept as a documented research facility, which is a Stage-3
decision.

Research suite: `Tests/Phase9Stage2.cpp`, `runPhase9Stage2Tests`, 10 / 10.
Focused regression: 1377 / 1379, the two registered Phase-7 failures only.
