# Phase 9 — Stage 3 production candidate report

Attack, from research concept to shipping DSP. No other Phase-9 control was
touched; `lateDiffusion` and `density` remain reserved and unconsumed.

> ## Verdict: ATTACK PRODUCTION CANDIDATE ACCEPTED
>
> With one caveat recorded in §21 that the reviewer should see before listening
> validation: at **Mod Depth 0 and high Attack**, late-field envelope
> periodicity roughly doubles (16.7 → 37.5, where the measurement floor is
> 3.03). At the shipping Mod Depth 30 % the effect is much smaller and
> non-monotone. Nothing else in the gate list failed.

---

## 1. The existing Attack parameter contract — unchanged

Read off `ParameterLayout.cpp` and `ParameterLayout.h`, not remembered:

| | |
|---|---|
| Parameter ID | `"attack"` |
| Display name | `"Attack"` |
| Range | linear **0.0 – 100.0**, step 0.1 |
| Default | **0.0** |
| Skew | none — `lin (0.0f, max, 0.1f)` |
| Formatter | `percentToText`, label `"%"` |
| Serialized type | `juce::AudioParameterFloat`, normalised in APVTS |
| RawHandle | `std::atomic<float>* attack` |
| Smoother, before | `Level attack` — a `LinearSmoother`, 20 ms ramp, in the `advanceUnconsumed` skip list, **consumed by nothing** |
| Smoother, after | **removed** |

**Nothing public changed.** No ID, no name, no range, no default, no skew, no
formatter, no serialised type. The default was already 0 %, so Attack 0 is the
legacy neutral and existing sessions restore it unchanged.

The *smoother* was removed, exactly as `preDelayMs` was in Phase 8 and for the
same reason: a smoothed Attack read per sample means continuously moving tap
positions, and the derivative of a delay time is a pitch shift applied to
whatever is passing through it. `AttackInjection` takes the raw target and
owns its own transition.

### Reserved parameters

`lateDiffusion` (default 70 %) and `density` (default 70 %) are **not deleted,
not renamed, not repurposed.** Phase 9 rejected both as controls, but their
IDs are part of the serialised state contract. They are documented as dormant
in `PluginProcessor.h` where their smoothers still live.

---

## 2. Insertion position — before input diffusion

```
input -> Low Cut -> High Cut -> Pre-Delay -> ATTACK -> input diffusion -> tank
```

Measured both candidates across Early Diffusion 0 / 70 / 100 % and Attack
25 / 60 / 100 %, at Mod Depth 0 so the comparison is exact:

| Early Diff | Attack | worst relative difference |
|---|---|---|
| 0 / 70 / 100 % | 25 / 60 / 100 % | **0.0000000000 %** in all nine |

Worst across the grid: **4.31e-16**.

---

## 3. Why they commute — and a Stage-2 correction

**Stage 2's report said the two placements "are not the same sound". That was
wrong, and this is the correction.**

Attack is one scalar FIR applied **identically to both channels**, so as an
operator on the stereo pair it is `h(z)·I` — a scalar times the identity. That
commutes with *any* 2×2 linear operator, including the stereo diffuser,
whatever its delay patterns and whatever its cross-coupling. The two orders
therefore produce the same samples to rounding, which is what the measurement
shows.

The case that would **not** commute is a per-channel schedule, `diag(h_L, h_R)`,
which commutes with the diffuser only while the diffuser is itself diagonal.
That is a second reason the shipped schedule is channel-identical.

Placement A is chosen on practical grounds: the diffuser stays immediately
before the injection vectors where Phase 4 searched it, and Attack joins the
other input-conditioning stages in the processor rather than being threaded
into `StereoFdn`.

---

## 4–7. Final topology, taps, timing, weights

```
y[n] = sum_k  w_k * x[n - d_k],    d_0 = 0,    sum_k w_k^2 = 1
```

| | |
|---|---|
| Tap count | **32** (1 immediate + 31 spread) |
| Spacing | the spread is divided into 31 equal cells, one tap per cell, positioned inside its cell by the **golden-ratio sequence** — irregular, no common divisor with the cell grid |
| Signs | deterministic ±1 from a fixed-seed xorshift; the immediate tap is always positive |
| Weights | immediate tap carries **25 %** of the power; the rest rise linearly in power; **L2-normalised**, squares sum to 1.0000000000 |
| Mean gap at full spread | **8.1 ms** |

### Why 32, and why not on ripple alone

The first version of this suite chose the tap count on third-octave ripple.
**That is the wrong measure**, and the table shows why:

| taps | mean gap | env crest | ripple | regular-spacing ripple |
|---|---|---|---|---|
| 6 | 50.0 ms | 6.43 | **0.330 dB** | 2.176 dB |
| 8 | 35.7 ms | 5.57 | 0.535 dB | 2.098 dB |
| 12 | 22.7 ms | 4.55 | 3.301 dB | 3.873 dB |
| 16 | 16.7 ms | 3.94 | 3.491 dB | 8.199 dB |
| 24 | 10.9 ms | 3.21 | 1.651 dB | 10.849 dB |
| **32** | **8.1 ms** | **2.78** | **0.746 dB** | 11.247 dB |
| 48 | 5.3 ms | 2.27 | 1.903 dB | 11.097 dB |

Six taps over 250 ms measure **better on ripple than any dense
configuration** while being five audible echoes 50 ms apart. The measure is
blind to them because a 50 ms spacing puts the comb's teeth 20 Hz apart, far
finer than a third-octave band, so the smoothing averages the comb away.
Ripple is kept as a **coloration check**, not a selection criterion.

What selects the count is the **mean gap** — arrivals separate perceptually
somewhere above roughly 10 ms — and the **envelope crest factor**, which falls
monotonically with tap count and is a direct measure of whether individual
taps stand out. 32 is the smallest count whose gap stays under 10 ms at the
full spread and whose ripple is under 1 dB.

The ripple column is not monotone in tap count because each count draws a
different sign and position pattern from the same deterministic sequence, so
its ripple is partly the luck of that draw. The gap and the crest are the
orderings to trust.

The **regular** column is Stage 2's prototype spacing with all-positive taps:
10 dB worse at the dense counts. That comparison is what third-octave ripple
*is* good for, because both sides have the same tap gap — and it is the
evidence for the irregular placement and the sign pattern.

---

## 8. Internal time mapping

`spread = 250 ms · u²`, with the public 0–100 % range untouched.

| Attack | linear would give | shipped gives | measured buildup |
|---|---|---|---|
| 0 % | 0.0 ms | 0.0 ms | 108 ms |
| 25 % | 62.5 ms | 15.6 ms | 115 ms |
| 40 % | 100.0 ms | 40.0 ms | 126 ms |
| 63 % | 157.5 ms | 99.2 ms | 159 ms |
| 80 % | 200.0 ms | 160.0 ms | 195 ms |
| 100 % | 250.0 ms | 250.0 ms | **252 ms** |

0–100 ms occupies the first **63 %** of the shipped mapping's travel against
40 % of a linear one.

### Why the maximum is 250 ms

| max spread | buildup | peak at | ripple | mean tap gap |
|---|---|---|---|---|
| 100 ms | 160 ms | 180 ms | 3.250 | 3.2 ms |
| 150 ms | 189 ms | 170 ms | 2.654 | 4.8 ms |
| **250 ms** | **252 ms** | 279 ms | **0.746** | **8.1 ms** |
| 400 ms | 331 ms | 426 ms | 0.629 | **12.9 ms** |

400 ms still lengthens the buildup, but its 12.9 ms gap is past where
arrivals separate. 250 ms is the longest maximum that keeps the taps fused.

---

## 9. Automation strategy

Two fixed tap schedules read from one delay line, crossfaded over **30 ms**.
A fade always runs to completion; a request arriving mid-fade overwrites a
single pending slot and is applied the moment the fade ends — coalesced, not
queued. Tap positions are integer samples and never move while being read.

The 30 ms was measured rather than inherited from Phase 8's 20 ms pre-delay
crossfade:

| fade | worst step in the transition window | settled step | ratio |
|---|---|---|---|
| 2 ms | 0.656 | 1.495 | 0.44 |
| 10 ms | 0.887 | 1.495 | 0.59 |
| 20 ms | 0.872 | 1.495 | 0.58 |
| **30 ms** | 0.998 | 1.495 | **0.67** |
| 50 ms | 0.961 | 1.495 | 0.64 |

No fade length produces a step the settled output does not already have — a
crossfade between two FIRs is a blend of two decorrelated versions of the same
signal, so it cannot step further than the signal does. The length is
therefore chosen for how long the blend's comb is audible, not for click
safety.

### Automation results

| transition | worst step | settled step | ratio | finite |
|---|---|---|---|---|
| 0 → low | 1.316 | 1.316 | 1.000 | yes |
| low → high | 1.324 | 1.324 | 1.000 | yes |
| high → 0 | 1.323 | 1.324 | 0.999 | yes |
| 0 → max | 1.365 | 1.365 | 1.000 | yes |
| max → 0 | 1.158 | 1.365 | 0.848 | yes |
| random × 450 | 1.369 | 1.513 | 0.905 | yes |

No click, no pop, no spike, no NaN, no gain burst. The delay line is written
**unconditionally including while bypassed**, so a move off 0 reads real
recent signal rather than a buffer frozen from whenever the control was last
touched. The stale-history probe therefore measures a non-zero difference
between two histories **by design** — that is two genuinely different pieces
of audio, not a stale burst, and the automation table above is what rules out
a click.

---

## 10. Attack 0 bit-identity — hard gate: **PASS**

| signal | samples | differing | worst diff |
|---|---|---|---|
| impulse | 48000 | **0** | 0.0 |
| white noise | 48000 | **0** | 0.0 |
| sine 440 | 48000 | **0** | 0.0 |
| transient train | 48000 | **0** | 0.0 |
| DC 0.25 | 48000 | **0** | 0.0 |
| silence | 48000 | **0** | 0.0 |

The bypass is an explicit branch, and the gate's warning about collapsing taps
is justified by arithmetic: the 32 weights sum to **−0.732320**, not 1.0,
while their squares sum to 1.0000000000. Summing them at offset 0 would be
both a gain change and a different computation.

---

## 11. Attack vs Pre-Delay — re-certified

Onset is pinned to the Attack-0 reference level, so it is comparable across
settings by construction.

| Pre-Delay | Attack | onset | buildup |
|---|---|---|---|
| 0 ms | 0 % | 21 ms | 108 ms |
| 0 ms | 63 % | 21 ms | 159 ms |
| 0 ms | 100 % | 21 ms | 252 ms |
| 50 ms | 0 % | 71 ms | 108 ms |
| 50 ms | 63 % | 71 ms | 159 ms |
| 50 ms | 100 % | 71 ms | 252 ms |

**Pre-Delay moves onset by exactly 50 ms at every Attack setting and leaves
buildup untouched. Attack moves buildup by up to 144 ms at every Pre-Delay
setting and leaves onset untouched.** The two are orthogonal on the metric
pair.

---

## 12. Attack vs Early Diffusion

| Early Diff | Attack | buildup | NED 20 ms | late NED |
|---|---|---|---|---|
| 0 % | 0 → 100 % | 92 → 235 ms | 0.055 → 0.061 | 0.95 – 0.98 |
| 70 % | 0 → 100 % | 108 → 252 ms | 0.709 → 0.703 | 0.93 – 0.96 |
| 100 % | 0 → 100 % | 106 → 250 ms | 0.499 → 0.514 | 0.96 – 1.01 |

| | buildup span |
|---|---|
| Early Diffusion 0 % | 143 ms |
| Early Diffusion 70 % | 143 ms |
| Early Diffusion 100 % | 144 ms |

**Attack's range does not collapse** — it is within 1 ms at every Early
Diffusion setting. Early Diffusion owns early density (0.055 → 0.709, 8.5× the
0.077 floor); Attack leaves it inside the floor. The two controls move
different metrics.

---

## 13. Repeated transients — hard gate: **PASS**

Superposition at Mod Depth 0, where Stage 1 verified the tank is time
invariant to 1.3e-25:

| Attack | residual | relative to peak |
|---|---|---|
| 0 % | 0.0 | **0.00000000 %** |
| 30 % | 0.0 | **0.00000000 %** |
| 63 % | 0.0 | **0.00000000 %** |
| 100 % | 0.0 | **0.00000000 %** |

Worst 5.74e-16. **Every transient receives the same Attack**, with no onset
detector, no global envelope and no transport awareness. This is a property of
the architecture — a fixed FIR — rather than a result that could have come out
otherwise.

---

## 14. Mod Depth interaction

| Mod Depth | Attack | buildup | late NED | periodicity |
|---|---|---|---|---|
| 0 % | 0 % | 107 ms | 0.952 | **16.72** |
| 0 % | 63 % | 157 ms | 0.989 | 29.38 |
| 0 % | 100 % | 249 ms | 1.000 | **37.52** |
| 30 % | 0 % | 108 ms | 0.932 | 23.70 |
| 30 % | 63 % | 159 ms | 0.952 | 30.44 |
| 30 % | 100 % | 252 ms | 0.937 | **21.67** |
| 100 % | 0 % | 108 ms | 0.937 | 21.36 |
| 100 % | 63 % | 159 ms | 0.950 | 27.01 |
| 100 % | 100 % | 252 ms | 0.976 | 19.67 |

Buildup span: **142 / 143 / 142 ms** at Mod Depth 0 / 30 / 100. Direction
consistent, range preserved, late density inside its floor everywhere.

**The periodicity caveat.** At Mod Depth 0, Attack 0 → 100 % raises envelope
periodicity from 16.72 to 37.52 — a rise of 20.8, **6.9× the 3.03 floor**.
That is a material increase in exactly the quantity Phase 9 Stage 1 used to
define metallic repetition. At the shipping Mod Depth 30 % the picture is
different and smaller: 23.70 → 30.44 → 21.67, non-monotone with a span of
about 9.

The mechanism is not mysterious: 32 irregular taps imprint their own
autocorrelation structure on the excitation, and at Mod Depth 0 nothing smears
it. It is reported rather than gated because the shipping configuration is
Mod Depth 30 %, where it is much weaker — but it is a listening question and
it is the one open risk in this candidate.

---

## 15. Energy variation

| Attack | energy | relative | peak |
|---|---|---|---|
| 0 % | 0.607084 | 0.00 dB | 0.03473 |
| 25 % | 0.620188 | +0.09 dB | 0.03569 |
| 50 % | 0.614505 | +0.05 dB | 0.02721 |
| 63 % | 0.604355 | −0.02 dB | 0.02918 |
| 80 % | 0.615799 | +0.06 dB | 0.02754 |
| 100 % | 0.593837 | −0.10 dB | 0.02774 |

**Worst integrated wet energy change: 0.096 dB**, against the brief's ≤ 0.5 dB
target. Nothing was normalised to achieve it — the L2-normalised tap weights
control the energy intrinsically. Mix and the wet trim were not touched.

The peak falls as Attack rises, which is correct rather than a defect: the
same energy spread over more time has a lower peak.

---

## 16. Coloration

Third-octave ripple of the production FIR at full spread: **0.746 dB**
peak-to-peak across 100 Hz – 16 kHz. Against the regular-spaced all-positive
alternative at the same tap count: 11.247 dB. The irregular placement and the
sign pattern buy 10.5 dB.

---

## 17. Stereo and mono

| Attack | L/R correlation | mono/stereo |
|---|---|---|
| 0 % | −0.0265 | −3.13 dB |
| 50 % | −0.0253 | −3.12 dB |
| 100 % | −0.0238 | −3.11 dB |

The baseline is −3.13 dB and −0.0265. Nothing moves beyond 0.1 dB. The
schedule is channel-identical, so it cannot alter the stereo geometry — the
image comes from the diffuser's differing patterns and the injection/readout
vectors, all downstream.

A per-channel variant was measured: mono/stereo −2.97 dB against −2.90 dB for
channel-identical, and correlation marginally closer to zero. **The expected
mono penalty did not appear**, so the mono argument for channel-identical is
withdrawn and the choice rests on the commutation property and on simplicity.

---

## 18. Decay and damping — structural

Attack is a feedforward FIR on the wet signal **before** the tank. It cannot
modify Decay, the feedback gains, the loop lengths, the damping coefficients,
Mod Depth, Mod Rate or Size, because none of those can see it.

The RT60 proxy moves 2.060 → 2.040 s across the whole Attack sweep — under
1 %, which is the proxy's own 20 ms quantisation. **This is not a Phase 7
gate.** The known-bad Phase-8 RT60 sensitivity harness was not used as one,
and any quantitative decay claim before listening validation should come from
the certified ensemble analyser.

---

## 19. CPU and memory

| | ns/sample | share of tank |
|---|---|---|
| Phase 7/8 tank | 241.319 | — |
| Attack, neutral (0 %) | **1.688** | **0.70 %** |
| Attack, active (100 %) | **35.462** | **14.70 %** |

- **Taps:** 32 per channel, 64 multiply-accumulates per sample when active.
- **Delay memory:** two buffers of 32768 doubles = **512 KiB**, sized from the
  400 ms allocation limit and rounded to a power of two for index masking.
- **Added state:** two 32-entry tap vectors, a write index, a fade position,
  four scalars.
- **Crossfade:** 1440 samples at 48 kHz.
- **Neutral cost:** two buffer writes and one branch.

14.70 % active is the honest figure and it is the largest single cost Phase 9
adds. It buys a 144 ms buildup range with 0.096 dB of energy variation and
0.746 dB of coloration. Reducing to 24 taps would save about a quarter of it
at 1.651 dB ripple and a 10.9 ms gap; that trade is available if the reviewer
wants it.

---

## 20. Focused regression

**1500 / 1502.** The two failures are the registered Phase-7 diagnostics at
values **bit-identical** to the prior failures:

```
0.06022872179
19.34162509
```

**New regressions: 0.** Phase 9 Stage 3 suites: **20 / 20**. Phase 7 and
Phase 8 regression tests all preserved. The plugin builds clean in the
validation profile.

---

## 21. Failures and anomalies

**One open risk**, already stated in §14: periodicity rises materially at
Mod Depth 0 with high Attack (16.7 → 37.5, 6.9× floor). Smaller and
non-monotone at the shipping Mod Depth. This needs listening before Phase 9
closure, and it is the reason the verdict carries a caveat.

**Six measurement errors made and corrected in this stage**, all recorded in
the source where they happened:

1. **Tap count chosen on third-octave ripple.** Six taps over 250 ms score
   better than any dense configuration while being five audible echoes; the
   measure cannot see a comb finer than a third-octave band.
2. **The envelope crest factor was a constant.** Measured from sample 0 it
   returned 5.59 for every tap count, because the immediate tap always carries
   √0.25 and always dominates the peak while the squares always sum to 1. It
   had to start after the immediate tap to measure the later arrivals.
3. **Buildup landmark, attempt 1** — "within 3 dB of the render's own peak".
   At 400 ms the peak is later and higher, so the onset threshold rose with it
   and buildup read 24 ms, shorter than every faster setting.
4. **Attempt 2** — the same with the onset pinned to the Attack-0 reference.
   Still 24 ms, because the real cause was that a long spread lets early
   arrivals decay ~10 dB before the last land, shrinking the rise so a fixed
   −3 dB threshold is crossed by envelope fluctuation.
5. **Attempt 3** — "70 % of whatever the rise is". Collapsed the other way,
   reading ~11 ms everywhere, because the envelope has two regimes and 70 % of
   a 26 dB rise lands inside the fast initial jump that Attack does not
   change. **Resolved** by abandoning thresholds: buildup is now T50 minus
   onset, an integral with nothing to be fragile about, and it is monotone.
6. **Click references measured against the input.** A 32-tap FIR of noise is
   intrinsically steeper than the noise it was given, so an input-step
   reference reported every setting as a click — the same mistake Phase 8
   Stage 2.5 made. Both the automation and the crossfade sweeps now compare
   against the settled output at the destination setting, and the random
   sweep's reference is sampled at eleven points because it visits a
   continuum.

Also corrected: **Stage 2's claim that the two placements "are not the same
sound"**, which was wrong — they commute exactly, at 4.31e-16.

---

## 22. Production commit

See the commit that accompanies this document. Files changed:

```
new   Source/DSP/AttackInjection.h     the shipping stage
      Source/PluginProcessor.h         member; attack smoother removed
      Source/PluginProcessor.cpp       prepare, reset, stage 1c, tail length
new   Tests/Phase9Stage3.cpp           research and production gates
      Tests/{CMakeLists.txt,RunAll.cpp,Suites.h}
```

---

## 23. Verdict

> ### ATTACK PRODUCTION CANDIDATE ACCEPTED
>
> Every hard gate passes. Attack 0 is bit-exactly Phase 8 across six signal
> types. Every transient gets the same Attack, proved by exact superposition
> with no onset detector. Onset belongs to Pre-Delay and buildup to Attack,
> orthogonally. Attack's 143 ms range survives every Early Diffusion and every
> Mod Depth setting. Energy varies by 0.096 dB, coloration by 0.746 dB, stereo
> and mono by under 0.1 dB. No new regression.
>
> **Carried forward as the one open risk:** late-field periodicity rises 6.9×
> the measurement floor at Mod Depth 0 with high Attack. Listening validation
> should be pointed at that specifically.

---

## STOP

No listening validation, no Phase-9 closure, Phase 10 not started.
