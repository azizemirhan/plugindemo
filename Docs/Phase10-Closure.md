# PHASE 10 — COMPLETE

Early Reflections and the Early/Late architecture. Accepted by measurement and
by human listening.

Phase 11 is **not** started and is not to begin without explicit
authorisation.

---

## 1. Shipping behaviour accepted by measurement and listening

### What was built

An explicit early-reflection layer running **in parallel** with the late
network, composed against it by one new public parameter.

```
input -> Low Cut -> High Cut -> Pre-Delay -+-> Attack -> tank -> trim -+-> balance -> Width -> Mix
                                           |                           |
                                           +-> early reflections ------+
```

Four properties follow from **where the split is**, not from discipline at the
call site, and each is enforced by a bit-level test:

| property | how it is guaranteed |
|---|---|
| Pre-Delay moves both branches equally | the split is downstream of it |
| Attack cannot reach the ER | the ER reads the signal before Attack |
| Early Diffusion cannot reach the ER | input diffusion is inside the tank |
| late damping stays a late property | the ER has no feedback to damp |

The decay-aware trim applies to the **tank branch only**: it compensates the
recurrence, and the early layer has no recurrence to compensate.

### Accepted constants

| | value |
|---|---|
| events per channel | 8 (16 taps) |
| placement | velvet noise, fixed seed, middle 40 % of each cell |
| `windowMs` | 80.0 ms at the default Size |
| `sizeAlpha` | 0.25 |
| `envelopeDecay` | 1.5 |
| `cellOccupancy` | 0.40 |
| gain law | `late = sqrt(1-t)`, `early = sqrt(t) * K` |
| calibration | BAL-4, 5 x 3 Decay x Size table, log-log bilinear |
| `earlyLate` | id `earlyLate`, "Early / Late", 0-100 %, default **0**, hint 2 |

Pattern fingerprint: **`5065939454054309816`**.

### What the measurements establish

| | |
|---|---|
| legacy neutrality | **12 / 12 byte-identical** to the accepted baseline |
| late-path independence | 7 controls byte-identical at Early/Late 100 % |
| Decay's only reach | scales the early layer by exactly the table ratio, zero per-sample drift over 11872 samples |
| Pre-Delay co-shift | early-branch residual **0.000000** at 20 and 80 ms |
| handoff valley | 2.50 dB at the default Size, 8.87 dB at Size 100 % |
| broadband traversal | **1.26 dB** worst; snare **1.28 dB** |
| parameter-space calibration error | **1.26 dB** worst |
| arrival sequence | 15 / 15 distinct gaps, **0 % repetition at every period** |
| sample rate | reflection times agree to **18.71 us** from 44.1 to 192 kHz |
| block size | bit-identical at 128 samples and above |
| stereo | correlation <= 0.011, downmix -3.05 to -3.06 dB |
| CPU | ER **0.041x** the tank active, **0.005x** bypassed |
| law and lookup | 3.33 ns and 76.18 ns **per block** |
| memory | **128 KiB** at 48 kHz; `prepareToPlay` the only allocation point |
| allocations | **zero** during combined Size and balance automation |
| automation | 0.707-1.005x the held reference; no clicks, no NaN or Inf |

### Human listening result

See section 4.

## 2. Known measured non-blocking characteristics

Each is measured, recorded, and **carried rather than hidden**. None is a
shipping blocker. The diagnostic gates that record them remain in the suite
and remain failing on purpose.

### 2.1 Narrow-band Early/Late anti-correlation — the significant one

**KNOWN MEASURED NON-BLOCKING CHARACTERISTIC.**

On sustained narrowband material the two branches acquire a definite phase
relationship at the source's own frequency, and sweeping their relative
weights sweeps through constructive or destructive interference. A pad at
Decay 0.3 s / Size 10 % measures:

| | |
|---|---|
| branch correlation | **-0.8566** |
| traversal 0 / 25 / 50 / 75 / 100 % | 0.0 / -6.7 / **-13.4** / -9.8 / -1.1 dB |

At the defaults the same pad measures **+0.6141** and a +3.8 dB bulge.

**It is architectural, not a defect of any chosen value.** Across 16 cells —
two narrowband sources, eight settings — the occupancy family's means are:

| | occ 0.20 | occ 0.40 | occ 0.70 |
|---|---|---|---|
| mean | 3.8 dB | 4.9 dB | 4.8 dB |
| worst | 8.4 dB | 13.4 dB | 11.3 dB |
| cells won | 5 | 5 | 6 |

The means sit within 1.1 dB against an effect reaching 13.4 dB and the
per-cell winner is a tie, so no occupancy escapes it and reopening that choice
would chase the effect around the family rather than fix it.

**No calibration or law can remove it.** K sets the endpoints and this lives in
the middle; the law's contribution is the cross term, which no exponent
removes. Closing it would require either decorrelated branches or
signal-driven gain — and the second is forbidden for a sound reason: a balance
that tracked the programme material would behave one way on a snare and
another on a pad.

**Human listening did not reproduce it.** The pad was the primary risk case
precisely because the diagnostic was strongest there, and no audible level
collapse, hollowing or reverb disappearance was heard.

**If later Mode or Character work makes this audibly reproducible, Phase 10 may
be revisited.** The measurements and the failing gates exist so that the next
person finds the evidence rather than rediscovering the effect.

### 2.2 Source-dependent calibration residual

The ideal K depends on the programme material by up to **4.29 dB** at a given
Decay and Size. A parameter-only law cannot track it, by definition. Reported
as the floor on what the calibration can achieve, not as an outstanding bug.

### 2.3 Block-size dependence below 128 samples

Relative differences of 0.007 (Early/Late 0) and 0.010 (Early/Late 50) at 32
samples. **The two columns move together, so it belongs to the tank's
control-rate updates, not to the ER layer.** Known engine characteristic that
predates Phase 10.

### 2.4 Composed Pre-Delay shift residual

Cannot be tightened to an exact identity while the tank is not exactly time
invariant at Mod Depth 0. The **early branch alone** shifts exactly
(0.000000); the composed figure is dominated by the tank.

### 2.5 Size compensation at Decay 0.1 s

Still misses its +/-0.5 dB target (worst 5.27 dB) because of the 12 dB safety
limit, exactly as Phase 6 recorded. It is the same limit that produces the
**inverted 0.1 s row** in the BAL-4 table — ideal K falling with Size where
every other row rises — and that corner is what rules out an analytic
calibration fit.

### 2.6 Phase 8 RT60 sensitivity harness

Remains known-bad and is still **not** used as an absolute gate.

### 2.7 The mono downmix figure

A value near -3.01 dB is *consistent with* two approximately decorrelated
channels of similar energy. It is **not** a proof that no cancellation occurs
anywhere in the spectrum, and Phase 10 has declined to make that claim since
Stage 1.

## 3. Historical research candidates that were rejected

Preserved in full. Nothing was rewritten to look cleaner.

### Architecture

| candidate | why rejected |
|---|---|
| no explicit ER at all | the tank produces **zero** countable arrivals in its first 80 ms at every Size and Early Diffusion setting, so there was nothing a diffusion control could substitute for |
| ER as another diffuser | a diffuser spreads until arrivals stop being countable; this layer exists to be countable |
| ER as the first 100 ms of the FDN relabelled | would contribute at Early/Late 0, which must be bit-exact |
| ER as a wet gain envelope or a second pre-delay | neither can produce discrete arrivals at irregular intervals |
| splitting **before** the pre-delay | would make Pre-Delay a hidden early/late timing control. An earlier source comment predicted this split and was wrong; the comment is left visible in `PluginProcessor.cpp` |

### ER timing and pattern

| candidate | why rejected |
|---|---|
| clustered and hybrid timing families | Stage 2 selected ER-B irregular decorrelated |
| **golden-ratio placement in equal cells** | `frac(phi*k)` advances by a constant mod 1, so one event per cell yields only **two distinct gaps**; the merged stereo sequence repeated exactly every seven arrivals. A comb with a rhythm in it |
| `sizeAlpha` 0.5 and above | 121.3 ms span at Size 100 %, past the point where the layer becomes a second late reverb |
| `cellOccupancy` 0.15 and 0.30 | not aperiodic — 15.4 % and 7.7 % gap repetition, 13/15 and 14/15 distinct |
| `cellOccupancy` 0.55 and 0.70 | handoff valley 11.05 and 16.47 dB at Size 100 % |
| `cellOccupancy` 0.20 | survived to a blind A/B against 0.40 and lost on third-octave ripple (5.18 vs 3.76 dB) after the listener reported no preference |
| `envelopeDecay` 0.6 | 11.19 dB valley against 1.5's 8.87 dB |

**`AttackInjection` still uses the rejected golden-ratio construction and was
deliberately left alone**: it was selected on mean arrival gap and third-octave
ripple, it passed its own listening validation, and its taps are ~8 ms apart
where this layer's are 3-12 ms. Changing it would alter the shipped Attack
sound for a property nobody reported hearing. The measurement is recorded in
`EarlyReflectionTuning.h`.

### Early/Late balance

| candidate | why rejected |
|---|---|
| Stage 1's level-changing policy | Early/Late is a composition control; Mix already owns dry against wet |
| uncalibrated equal-power | 2.15 dB traversal |
| linear amplitude (LAW-B) | 3.04 dB mid-travel sag on broadband material |
| **fixed K** (BAL-B) | wrong by up to **26.26 dB** across the Decay x Size plane |
| K from Decay only (BAL-2) | 6.72 dB worst fit error |
| K from Decay + Size, log-linear (BAL-3) | 5.77 dB |
| K log-quadratic (BAL-3b) | 3.13 dB over the musical range but **8.71 dB at Decay 0.1 s**, where the table leaves 1.26 dB |
| High Damp as a third calibration axis | 1.95 dB of sensitivity against Decay and Size's 10 dB-plus; triples the table for the smallest effect |
| exponent family p = 0.6 to 0.9 | sag grows monotonically with the exponent on **every** source; the branches never reach the correlation that would make amplitude addition the better model |
| **trigonometric constant power** | also exactly constant power, and bows up to 0.189 dB away from its chord under calibration error where the linear-in-power form stays flat; 1.28 dB against 0.08 dB of sag on a pad |
| any signal-driven normalisation | forbidden, and rightly: it would make the control behave differently on a snare and on a pad |

### Scope reductions from earlier phases, carried forward

Density, Bloom and Late Diffusion were all rejected as controls in Phase 9 on
measurement. Their parameter IDs remain in the serialised state contract
because removing them would change what existing sessions restore.

## 4. Human listening result

Final sanity listening, four renders on the complete production candidate,
each sweeping Early/Late from 0 to 100 % across its length.

| source | result |
|---|---|
| **pad** | no audible level collapse, no obvious hollowing, no reverb disappearance |
| **vocal** | no audible collapse or disconnected reverb behaviour |
| **piano** | no audible collapse; level and character stable through the sweep |
| **snare** | listener uncertain; **no specific repeatable defect identified** |

The pad was the primary risk case because the technical diagnostic produced the
strongest narrow-band anti-correlation there. **The predicted severe mid-sweep
collapse was not perceived in the musical render.**

Earlier listening in this phase, also preserved:

| set | question | result |
|---|---|---|
| `renders/phase10-stage3-ab/` | occupancy 0.70 vs 0.20, 8 files | superseded before judgement — the research overtook its premise |
| `renders/phase10-occupancy-ab/` | occupancy 0.20 vs 0.40, 4 files blind | **no meaningful preference on either case** |
| `renders/phase10-final-sanity/` | the accepted candidate, 4 files | the table above |

**Verdict: Phase 10 accepted perceptually.**

## 5. Regression result

**2520 / 2530.** Ten failures, every one accounted for, **no new regression and
no gate weakened**.

### Six pre-existing Phase 7 diagnostics

Verified by rebuilding `a1328f0` in a detached worktree and running it on the
same machine — identical values, identical order:

```
0.06022872179   T20/T30 agreement
19.34162509     stimulus-to-stimulus repeatability
0.05998214192   ensemble T20/T30 agreement
0.2154187833    the analytic decay law vs the tank
                candidate fit at 6000 Hz
                candidate fit at 12000 Hz
```

### Four intentional Phase 10 diagnostic gates — PRESERVED, STILL FAILING

```
C: the shipping law contributes under 1 dB of sag
the traversal target is met on normal musical material
the traversal target is met on tonal material too - IT IS NOT, and no value of K can fix it
the source-dependent floor is small next to the error being fixed - IT IS NOT
```

**These are not weakened at closure and must not be.** They are the standing
record of section 2.1, and they are the thing that will fail loudly if a future
Mode or Character change makes the behaviour worse.

One gate was **replaced** during the phase — the original "a fixed K does not
generalise" gate — because the design it indicted no longer exists. Its
replacement applies a stricter criterion to the new design. That is the only
gate that changed, and it was tightened rather than relaxed.

### Suite totals

| suite | result |
|---|---|
| `AuroraVerbStateTests` | **26 / 26** |
| `runPhase10ProductionTests` | 18 / 20 sections, 2 intentional |
| `runPhase10Stage35Tests` | 15 / 17, 2 intentional |
| `runPhase10BalanceLawTests` | 13 / 13 |

CPU benchmarks pass on an idle machine. Two runs during this phase failed them
while a build ran in parallel; `MatrixModulationCpuBenchmark`'s own source
already records that threshold firing twice on machine state rather than on a
code change.

### Measurement errors found and corrected in this phase

Recorded because Stage 2's report asked for the research trail to be preserved,
and because a report that lists only its successes is not evidence.

1. A handoff figure of **6.02 dB** quoted after the placement law changed; the
   real value was 16.47 dB. The **pattern fingerprint** test now exists to make
   that class of mistake fail.
2. A handoff gate on "the default Size" against a loop that **never rendered
   the default Size**, so it could not fail.
3. A gap-equality criterion that integer sample offsets violate by coincidence;
   replaced by a periodicity criterion.
4. A Pre-Delay gate demanding an exact zero without a control; the late path
   alone gives a larger residual.
5. A version-hint check that counted parameters; replaced by one that names the
   nineteen, which is stricter.
6. An allocation gate that read its counter twice, so building its own
   "0 allocations" message tripped it.
7. `==` on a log/exp round trip, which is not bit-exact.
8. A fine-ripple metric as peak-to-trough, which reported the deepest single
   FFT bin and swung 20 dB for nothing audible.
9. An absolute block-size difference that flattered the bypassed path, because
   the ER's sparse FIR raises the peak it sits on.
10. An attempt to attribute the narrow-band effect from one source at one
    setting.

## 6. Final production constants

```
er::Pattern
    events            8          (16 taps)
    windowMs          80.0       ms at the default Size
    envelopeDecay     1.5
    sizeAlpha         0.25
    cellOccupancy     0.40
    earliestFraction  0.0

er::maxSpanMs         160.0
er::maxEvents         32
er::referenceSizeControl  0.60

gain law    late = sqrt(1 - t), early = sqrt(t) * K    endpoints explicit

BAL-4, indexed [decay][size]:
                 Size 0 %   Size 50 %  Size 100 %
    Decay  0.1 s  0.0963     0.0643     0.0350
    Decay  0.5 s  0.3049     0.4245     0.5210
    Decay  2.5 s  0.4156     0.6575     1.0324
    Decay 10.0 s  0.4426     0.7217     1.1937
    Decay 60.0 s  0.3750     0.6169     1.0375

parameter   earlyLate, "Early / Late", 0-100 %, default 0, versionHint 2
```

**Do not reopen these values without new evidence.**

## 7. Commit

`4c2ae71` — "Phase 10 closure".

Phase history: `7b0d2ab` Stage 1 · `81716c8` Stage 2 · `a1328f0` Stage 3
foundations · `aa8c27e` Stage 3.5 · `d7b5089` crossfade law · `37ae030` final
candidate · this commit.

---

# PHASE 10 — COMPLETE

Shipping behaviour accepted by measurement and by listening. Seven known
measured non-blocking characteristics carried with their evidence. Four
diagnostic gates left failing on purpose as the standing record of the one
that matters.

**Phase 11 is not started and is not to begin without explicit
authorisation.**
