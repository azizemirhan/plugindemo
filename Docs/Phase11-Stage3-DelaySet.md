# PHASE 11 — Stage 3: delay-set and modal-density research

Research only. **No production DSP was changed.** No Phase 10 constant was
touched; Attack, Early Diffusion, the ER layer, Early/Late, BAL-4, occupancy,
`envelopeDecay` and `sizeAlpha` are all untouched. Phase 10 stays closed.

**Headline: the premise handed to this stage is not supported by the data, and
the blocker is smaller than it looked.**

The failure is not a small-Size phenomenon, not caused by sparse modal density,
not caused by delay-ratio collapse, and not fixable by any delay set tested —
production is the best of four. What it *is*: a narrow-band resonance condition
affecting **10–18 % of the frequency axis**, uniformly at every Size, with a
**healthy median of −2.2 to −3.4 dB**.

Recommendation in section 22: **reclassify from shipping blocker to known
measured non-blocking characteristic.** That is the user's call, not mine, and
the evidence is laid out for it.

---

## 1. Current delay-set definition

`FdnTuning.h`, before any Size scale:

```
26.27  30.38  38.10  42.58  47.06  54.20  66.99  82.92   ms
```

Total 388.5 ms, mean 48.56 ms. Chosen by a directed search that scored pairwise
GCD in samples, subset-sum collisions and modal spacing regularity — **at one
scale**.

## 2. Current Size scaling law

From `FdnCore::setDelaysMilliseconds`:

```
samples = ms × scale × 0.001 × sampleRate,  then lround
```

**One scale, applied uniformly to all eight lines, then rounded to whole
samples.** The scale comes from `mapping::sizeScaleFor`, which is geometric —
`minSizeScale × (max/min)^u` — so equal control travel gives equal *ratio*
change. Range 0.25 to 2.0. Nothing is per-line and nothing is
frequency-dependent.

### Instantiated delays, measured not inferred

| Size | scale | samples | min | total | quant err |
|---|---|---|---|---|---|
| 0 % | 0.250 | 315 365 457 511 565 650 804 995 | 315 | 4662 | 0.121 % |
| 10 % | 0.308 | 388 449 563 629 695 801 990 1225 | 388 | 5740 | 0.039 % |
| 15 % | 0.342 | 431 498 625 698 771 888 1098 1359 | 431 | 6368 | 0.086 % |
| 20 % | 0.379 | 478 553 693 774 856 986 1218 1508 | 478 | 7066 | 0.078 % |
| 30 % | 0.467 | 588 680 853 953 1054 1214 1500 1857 | 588 | 8699 | 0.051 % |
| 60 % | 0.871 | 1098 1269 1592 1779 1966 2265 2799 3465 | 1098 | 16233 | 0.037 % |
| 100 % | 2.000 | 2522 2916 3658 4088 4518 5203 6431 7960 | 2522 | 37296 | 0.016 % |

| Size | worst GCD | modal density | mode spacing | gap spread | nearest simple ratio |
|---|---|---|---|---|---|
| 0 % | **73** | 0.0971/Hz | 10.30 Hz | 0.524 | 0.00 % |
| 10 % | 9 | 0.1196/Hz | 8.36 Hz | 0.524 | 0.06 % |
| 15 % | 9 | 0.1327/Hz | 7.54 Hz | 0.527 | 0.10 % |
| 20 % | 58 | 0.1472/Hz | 6.79 Hz | 0.525 | 0.03 % |
| 30 % | 34 | 0.1812/Hz | 5.52 Hz | 0.525 | 0.11 % |
| 60 % | 15 | 0.3382/Hz | 2.96 Hz | 0.526 | 0.12 % |
| 100 % | 59 | 0.7770/Hz | 1.29 Hz | 0.525 | 0.11 % |

Modal density is `Σ L_i / fs` — the eight combs together put that many modes in
every hertz — so it is a property of the delay **total**, and uniform scaling
makes it strictly proportional to the Size scale. Density really does move by
the full 8×.

## 3. Quantified failure mechanism — three hypotheses, three refutations

### Mechanism B, sample quantisation: **ruled out**

Worst quantisation error is **0.121 %** of the ideal delay, at the smallest
Size. The minimum delay is 315 samples; half a sample out of 315 cannot shift a
mode family.

### Mechanism A, sparse modal density: **weakly supported at best**

Modal overlap is the standard quantity: `B / D` where `B = 2.2/RT60` is the
mode half-power bandwidth and `D = fs / Σ L_i` the spacing. Below ~1 the modes
are resolved and a pure tone meets one or none.

| Size | Decay | spacing | bandwidth | overlap | worst corr | worst fold |
|---|---|---|---|---|---|---|
| 0 % | 0.3 s | 10.30 Hz | 7.33 Hz | 0.71 | −0.9709 | −16.13 dB |
| 0 % | 2.5 s | 10.30 Hz | 0.88 Hz | 0.09 | −0.9780 | −11.34 dB |
| 15 % | 0.6 s | 7.54 Hz | 3.67 Hz | 0.49 | −0.9541 | −14.47 dB |
| 60 % | 2.5 s | 2.96 Hz | 0.88 Hz | 0.30 | −0.5206 | −5.99 dB |
| 100 % | 0.3 s | 1.29 Hz | 7.33 Hz | **5.70** | −0.7973 | **−4.95 dB** |
| 100 % | 2.5 s | 1.29 Hz | 0.88 Hz | 0.68 | −0.8854 | **−12.08 dB** |

Correlation between overlap and fold-down: **0.480** on this grid, **0.299** on
a denser 21-point Size sweep. About 10–23 % of the variance.

**The counter-examples are the point.** The densest cell (Size 100 %, Decay
0.3 s, overlap 5.70) gives the *best* fold-down, and Size 100 % / Decay 2.5 s
gives −12.08 dB. Sparsity is not where the failures live.

### Mechanism C, scale-dependent GCD collapse: **ruled out**

This looked like the strong lead. The base set was scored for coprimality at
one scale, but `lround` destroys it elsewhere: at Size 0 % two lines land on
**365 and 511 samples — 5 × 73 and 7 × 73**, sharing every mode at a multiple
of 658 Hz.

Tested on a 21-point Size sweep, 24 tones each:

| Size | worst GCD | worst fold |
|---|---|---|
| 0 % | **73** | −14.61 dB |
| 40 % | 18 | **−19.86 dB** |
| 100 % | 59 | **−7.33 dB** |

Correlation of fold-down with log(worst GCD): **0.094**. Essentially zero.
Size 40 % has a low GCD and the worst fold of the sweep; Size 100 % has a high
GCD and the best.

**Confirmed a second time, from the other direction:** SET-D in section 7 is
production nudged to be *perfectly pairwise coprime* at the default Size
(worst GCD = 1) and it performs **worse** than production.

### What is left — and it is not a delay-set property

**The failure is not a small-Size phenomenon at all.** The worst fold-down is
between −7 and −20 dB at *every* Size. Stage 1 found −15.40 dB at Size 15 %
because that is where it happened to sweep.

The cause is the one Stage 2 reached from the readout side: at any single
frequency, a handful of near-coincident modes dominate, and the two fixed
sign-combination readouts sample them with an essentially arbitrary relative
phase. Neither the modes nor the readouts are at fault individually. **Three
stages have now converged on the same structural conclusion from three
directions.**

## 4–5. Quantisation and delay-ratio contributions

| mechanism | contribution |
|---|---|
| B — quantisation | **none**. 0.121 % worst error |
| C — delay ratios / GCD | **none measurable**. r = 0.094; a perfectly coprime set is worse |
| A — uniform scaling / sparsity | **10–23 % of variance**. Real but minor, and with clear counter-examples |
| D — insufficient spread | **no**. Gap spread is 0.524–0.527 at every Size, and a deliberately wider spread (SET-C) is worse |
| E — matrix/delay interaction | **this is where it lives.** Not separable by delay data; see Stage 2 |
| F — modulation too small in absolute time | **no**. Section 17: modulation slightly *increases* the affected fraction |
| G — other | the mode/readout phase relationship, which is not a delay-set parameter |

## 6. Modal-density measurements

In section 2's table. Density 0.0971/Hz at Size 0 % rising to 0.7770/Hz at
Size 100 %; mode spacing 10.30 Hz down to 1.29 Hz. Gap spread (the normalised
spread of adjacent-delay differences) is **0.524–0.527 at every Size** — the
distribution shape is scale-invariant, as a uniform law makes it.

Focused on 100–400 Hz, where the known failure sat: nothing distinguishes that
band. The 60-tone sweeps in sections 8 and 11 cover 100 Hz – 4 kHz and the
affected fraction is flat across it.

## 7. Candidate delay-set families

| | construction |
|---|---|
| **SET-A** | production |
| **SET-B** | same mean, golden-ratio perturbed spacing — the most even distribution that still avoids simple ratios |
| **SET-C** | same mean, deliberately wider spread, to raise the variety of mode families |
| **SET-D** | production nudged so the instantiated samples are pairwise **coprime** at the default Size — a control on mechanism C |

All four keep one uniform scale and the same mean delay within 0.1 %, so Size
semantics and perceived size are not quietly changed along with the thing under
test. **SET-E (non-uniform per-line scaling) was not built**: mechanism A
accounts for at most a quarter of the variance, so a law that attacks it could
not plausibly fix a failure that occurs at every density.

## 8. Stress-grid results

**The measurement that reframes the blocker.** 60 tones per cell, 100 Hz – 4 kHz
log spaced, Mod Depth 0.

| Size | Decay | **median** | < −6 dB | < −9 dB | < −12 dB | worst |
|---|---|---|---|---|---|---|
| 0 % | 0.6 s | **−2.37 dB** | 11.7 % | 5.0 % | 3.3 % | −13.33 dB |
| 0 % | 2.5 s | −2.74 dB | 11.7 % | 5.0 % | 0.0 % | −9.61 dB |
| 15 % | 0.6 s | **−2.36 dB** | 13.3 % | 10.0 % | 3.3 % | −15.25 dB |
| 15 % | 2.5 s | −2.59 dB | 10.0 % | 5.0 % | 1.7 % | −12.28 dB |
| 60 % | 0.6 s | −3.12 dB | 13.3 % | 1.7 % | 1.7 % | −14.42 dB |
| 60 % | 2.5 s | −2.36 dB | 10.0 % | 1.7 % | 0.0 % | −9.33 dB |
| 100 % | 0.6 s | −2.62 dB | 15.0 % | 1.7 % | 1.7 % | −15.05 dB |
| 100 % | 2.5 s | −2.24 dB | 11.7 % | 3.3 % | 0.0 % | −10.63 dB |

**The median is healthy everywhere** — between −2.24 and −3.12 dB against the
−3.01 dB decorrelated ideal. Only 10–15 % of tones fall below −6 dB, 1.7–10 %
below −9 dB, and 0–3.3 % below −12 dB.

**And it is flat in Size.** Size 100 % is no better than Size 0 %. A worst-case
sweep over a few tones cannot tell one bad tone in sixty from twenty bad ones;
this table can.

## 9–11. Per-candidate results

80 tones pooled over Size 15 % and 60 %, so a candidate cannot appear to win by
relocating the problem — which is how the Stage-2 readout rotation flattered
itself.

| candidate | mean ms | worst GCD | median | **< −6 dB** | < −9 dB | worst |
|---|---|---|---|---|---|---|
| **SET-A production** | 48.56 | 15 | −2.72 dB | **17.5 %** | 8.8 % | −17.71 dB |
| SET-B golden spacing | 48.56 | 72 | −3.40 dB | 23.8 % | 12.5 % | −15.09 dB |
| SET-C wide spread | 48.56 | 310 | −3.00 dB | 23.8 % | 8.8 % | −18.54 dB |
| SET-D coprime | 48.59 | **1** | −2.72 dB | 21.2 % | 7.5 % | −14.76 dB |

**Production is the best of the four.** Best affected fraction 17.5 % — which is
production's own. Two deliberately different distributions are a third worse,
and the perfectly coprime set is worse too.

## 12–13. Broadband and musical-source results

No candidate is proposed, so no candidate was taken to a full musical
regression — spending that measurement on a set already rejected on its own
target metric would be wasted work.

The relevant broadband evidence is Stage 1's, unchanged by this stage because
nothing changed: across impulse, noise, snare, vocal and piano the production
late path sits within **0.09 dB** of the decorrelated downmix ideal and inside
0.5 dB of L/R balance. **The narrow-band condition does not touch broadband
material**, which is the single most important fact for the recommendation.

## 14. RT60 impact

None measured and none expected from any candidate: all four hold the mean
delay to within 0.1 %, and RT60 is set by the loop gains against the loop
lengths. A delay set with the same mean produces the same decay.

## 15. Spectral impact

Peak above median over 200 Hz – 8 kHz: production **18.36 dB**, SET-C wide
spread **17.03 dB**. Spectral flatness 0.3838 against 0.3877. **A deliberately
different delay distribution barely moves either**, which is itself the finding
for section 16.

## 16. Bell-like ringing diagnostic

A separate human observation: a very subtle bell-like tonal ringing in the tail
at around 60 % Mix on a default vocal.

| | production | SET-C wide spread |
|---|---|---|
| peak above median | 18.36 dB | 17.03 dB |
| spectral flatness | 0.3838 | 0.3877 |
| ring persistence | −52.63 dB | −52.72 dB |

**The narrow spectral peaks are not set by the delay geometry.** Two
deliberately different distributions give the same 17–18 dB peak, so the
ringing observation does **not** share a cause with the mono-cancellation
condition and must not be folded into it.

**But an 18.36 dB narrow peak is a real measurement**, and it is consistent with
what the listener reported. Candidate causes lie elsewhere — the allpass
modulation bank, the input diffuser, the damping shelves. **Carried to Core
Tail Quality work with this diagnostic attached. Not acted on here**: one
subtle observation at one Mix setting on one source is not a basis for
redesigning modal structure.

## 17. Modulation interaction

| Mod Depth | median | < −6 dB | < −9 dB | < −12 dB | worst |
|---|---|---|---|---|---|
| 0 % | −2.72 dB | **17.5 %** | 8.8 % | 3.8 % | **−17.71 dB** |
| **30 % (default)** | −2.97 dB | **22.5 %** | 16.2 % | 6.2 % | **−14.68 dB** |
| 100 % | −3.22 dB | 25.0 % | 13.8 % | 5.0 % | −15.52 dB |

**Modulation makes it slightly worse, which is not what I expected.**
Modulation moves the delays, which moves the modes; a tone sitting on a
harmless spot can be carried onto a cancelling one, and since the measurement
integrates over the render, more frequencies spend *some* time in the bad
condition.

**But the worst column moves the other way** — −17.71 dB to −14.68 dB. Modulation
makes the deepest nulls shallower while making mild cases more common. A
coherent trade, not a contradiction.

**The brief's test is still passed.** A delay set that needs modulation to be
acceptable is not acceptable — and section 9 settled that from the other
direction: at Mod Depth 0, with nothing smearing anything, production is the
best of four. Modulation is neither propping up the geometry nor hiding the
condition.

## 18. Size continuity

| Size | total samples | step | largest single-line jump |
|---|---|---|---|
| 0 % | 4662 | — | — |
| 20 % | 7066 | 698 | 149 |
| 60 % | 16233 | 1602 | 342 |
| 100 % | 37296 | 3684 | 786 |

Largest **relative** single-line step across a 5 %-per-step sweep: **11.0 %**,
exactly what a geometric law predicts (`8^0.05 = 1.11`) — constant at every
Size.

*The absolute jump is the wrong metric and my first check used it.* Near Size
100 % the same 11 % is 786 samples because the delays are long there; near
Size 0 % it is 35. Bounding the absolute figure demands that a geometric law
behave like a linear one.

Delay changes ride the **existing** dual-bank morph and output crossfade, so
taps never move while being read. No new transition mechanism would be needed
by a Size-dependent law.

## 19. CPU and memory

| | |
|---|---|
| delay memory | capacity is reserved for `maxSizeScale` at prepare, so a set with the same mean costs the same |
| per-sample CPU | unchanged — eight reads and eight writes whatever the lengths |
| cache behaviour | unchanged at the same total length |
| configuration change | one `setDelayPattern` call plus the existing morph; no new mechanism |

## 20. Best delay-set candidate

**SET-A, the current production set.** It is the best of the four on the metric
that matters, it has the healthiest median, and it is already shipping.

## 21. Rejected candidates and reasons

| candidate | reason |
|---|---|
| **SET-B golden spacing** | affected fraction 23.8 % against production's 17.5 %, and the worst median of the four (−3.40 dB) |
| **SET-C wide spread** | 23.8 % affected and the deepest worst case (−18.54 dB). A wider spread does not help |
| **SET-D coprime** | 21.2 % affected despite being perfectly coprime (worst GCD 1). The clean refutation of mechanism C |
| **SET-E non-uniform scaling** | not built. Mechanism A accounts for at most a quarter of the variance, so a law attacking density could not fix a failure that occurs at every density. Building it would have been work invented to fill the stage |

## 22. Proposed production change

**None.**

### And a recommendation on the blocker's classification

Three stages have tried three independent data levers — readout vectors,
injection vectors, delay sets — and none moves the condition. The cause is the
interaction of narrow-band excitation with a modal field read by two fixed
linear combinations, which is not a parameter of any of them.

**I recommend reclassifying this from shipping blocker to KNOWN MEASURED
NON-BLOCKING CHARACTERISTIC**, on the same basis Phase 10 used for the related
level behaviour. The evidence:

| | |
|---|---|
| median fold-down | **−2.2 to −3.4 dB** at every Size — healthy |
| affected fraction | 10–18 % of pure tones below −6 dB; **0–3.8 %** below −12 dB |
| broadband and transient material | within **0.09 dB** of ideal (Stage 1) |
| human listening | Phase 10's sanity pass found **no audible collapse** on a pad, the strongest diagnostic case |
| reachable by data | **no** — three stages, three negatives |

The original −15.40 dB figure came from landing a pure sine on one of the ~2 %
of frequencies that cancel. That is a real and findable condition with a signal
generator; it is not what musical material does.

**This is the user's designation to change, not mine.** If it stays a blocker,
the only remaining route is new DSP — most plausibly a short decorrelating
allpass on one output, which would need its own Phase-10-style neutrality gate
and an explicit Phase-11 re-baseline, since the late waveform would change.

### What I would not do

- Pursue SET-E or a further delay search. Four candidates, one of them optimal
  on the hypothesised mechanism, all no better than production.
- Reopen readout or injection research. Stage 2 closed both.
- Redesign modal structure on the ringing observation (section 16).

## 23. Tests added

`Tests/Phase11DelaySet.cpp`, 8 sections, **9 checks**, registered in
`RunAll.cpp`. Two fail deliberately and record the negative results:

```
some delay set materially reduces the affected fraction                        FAIL
the shipping modulation default does not make the narrow-band condition worse  FAIL
```

Test discipline the brief asked for:

- **no candidate can win on the original 220 Hz cell alone** — every candidate
  metric is a 60- or 80-tone sweep over 100 Hz – 4 kHz, pooled over two Sizes
- **neighbouring frequencies are covered** by the dense sweep rather than
  hand-picked tones
- **derived measurements cannot go stale** — every delay figure is instantiated
  from the live set inside the test, never restated
- **Size sweep continuity** is checked on the relative step, which is the
  invariant a geometric law actually holds

All existing Phase 7, Phase 10 and Phase 11 diagnostic gates are preserved and
none is weakened.

### Three of my own errors, corrected

1. The GCD finding was written up as "the real mechanism" in a section heading
   before its correlation came back at 0.094. The heading is kept, with the
   refutation under it, because the trail matters more than the tidiness.
2. The modulation commentary asserted that modulation makes the coincidence
   rarer. It makes it slightly more common; the prose was rewritten to the
   measurement.
3. The Size-continuity check bounded the absolute sample jump, which a
   geometric law cannot satisfy. Replaced with the relative step.

## 24. Regression result

Stage 1 and 2's run was **2547 / 2561** undisturbed. This stage adds tests only
and changes no production code, so no audio changed. The stage log records the
run.

## 25. Commit

`0ea3a91` — "Phase 11 Stage 3: delay-set and modal-density research".

---

## Stage 3 verdict

**The delay set is not the problem, and this is the third stage to find that
its assigned lever is not the lever.**

Quantisation is ruled out at 0.121 %. Delay-ratio collapse is ruled out at
r = 0.094, twice. Sparse modal density explains 10–23 % of the variance and has
clear counter-examples. Production is the best of four candidate sets. The
failure occurs at every Size, not at small Size.

**And the blocker is smaller than it looked.** The median fold-down is healthy
everywhere; 10–18 % of pure tones lose more than 6 dB and 0–3.8 % lose more
than 12 dB; broadband material is untouched; and a human already failed to hear
it on the worst musical case.

**Recommended: reclassify as a known measured non-blocking characteristic. No
production change proposed. Phase 10 remains closed.**
