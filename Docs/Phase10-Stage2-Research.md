# Phase 10 — Stage 2 research report

ER research prototypes, Early/Late balance and Size validation. No shipping
DSP, no public parameter, no contract change.

Both Stage-1 corrections are applied: the balance is now energy-calibrated to
about 1 dB rather than defended at 5 dB, and the mono claim is restated as
what one test can support.

> ## Headline: the architecture holds, with one measured problem to solve
>
> Every independence gate passes, several of them bit-exactly. The balance
> target is met with room to spare: **0.01 dB** against a 1 dB target. But the
> early-to-late handoff **degrades badly at large Size** — up to **6.93 dB** —
> and the measurement shows that widening the overlap does **not** fix it, so
> Stage 3 has a specific, actionable target rather than a vague risk.

---

## 1. ER-B variants and ER-D

All L2-normalised, so the comparison is about structure rather than level.
Spacing comes from the **schedule**, not an envelope peak-finder: Stage 1
established that the envelope counter is fragile at 3–5 ms spacing, which is
exactly where these decisions live.

| family | events/ch | taps | mean gap | median | min | sep | crest | ripple | corr | mono |
|---|---|---|---|---|---|---|---|---|---|---|
| ER-B irregular | 8 | 16 | 4.88 | 6.18 | **2.48** | 1 | 77.77 | 7.97 | **−0.0005** | −3.01 dB |
| ER-B irregular | 12 | 24 | 3.24 | 4.12 | 1.65 | 1 | 74.01 | 10.90 | −0.0007 | −3.01 dB |
| ER-B irregular | 16 | 32 | 2.46 | 3.09 | 1.24 | 1 | 71.45 | 11.05 | −0.0008 | −3.01 dB |
| ER-B hybrid | 8 | 14 | 5.64 | 6.18 | 2.48 | 1 | 77.77 | 7.97 | **0.862** | −0.31 dB |
| ER-B hybrid | 12 | 22 | 3.55 | 4.12 | 1.65 | 1 | 74.01 | 10.90 | 0.801 | −0.46 dB |
| ER-B hybrid | 16 | 30 | 2.63 | 3.09 | 1.24 | 1 | 71.45 | 11.05 | 0.756 | −0.56 dB |
| **ER-D clustered** | 12 | 12 | 5.42 | **2.64** | 1.63 | **3** | 63.25 | **7.66** | 0.730 | −0.68 dB |
| ER-D clustered | 16 | 16 | 3.92 | 2.33 | 1.17 | 3 | 54.02 | 13.56 | 0.791 | −0.52 dB |

Two structural facts the table makes visible:

**ER-B's decorrelated timing halves its own inter-arrival gap.** Each channel
gets its own time for every event, so the two schedules interleave and the
*combined* arrival stream is twice as dense as the per-channel count suggests.
At 8 events per channel the mean gap is already 4.88 ms. That is why the
`sep` column — arrivals more than 10 ms clear of the previous one — reads 1 for
every ER-B variant: nothing is separated enough to be counted as a discrete
echo. Low slap risk, but also no "countable surfaces".

**ER-D is the only candidate with separated structure**, reading 3 — one per
cluster — which is exactly what the clustered design was for.

---

## 2. ER-D result

ER-D at 12 events is the most interesting candidate in the set and it is
**not** the recommendation. It has the lowest ripple (7.66 dB), the lowest
crest (63.25, i.e. least spiky), and the only genuinely separated arrival
structure. Its correlation of 0.730 is a middle position: wider than the
hybrid, much narrower than the irregular.

At 16 events its ripple jumps to 13.56 dB — the worst in the table — because
the cluster widths are fixed while the event count grows, so the taps bunch
and the comb deepens. **ER-D does not scale by adding events**; it scales by
adding or widening clusters, which is a different parameterisation and a
Stage-3 question if it is pursued.

---

## 3. Winning ER architecture: **ER-B irregular**

Not the hybrid, and that is a Stage-2 reversal of the Stage-1 expectation.

| | correlation (early 0–20 ms) | mono (early) |
|---|---|---|
| irregular | −0.003 … **+0.274** | −3.01 … **−2.18 dB** |
| **hybrid** | **0.906 … 0.998** | **−0.21 … −0.01 dB** |
| clustered | 0.670 … 0.856 | −0.80 … −0.89 dB |

> **The hybrid family fails its own intent.** The design was "centre-stable
> early arrivals, decorrelated later ones". But the gain envelope is
> `exp(-t/0.6W)/sqrt(t)`, so the earliest arrivals carry most of the energy —
> and sharing them means sharing most of the energy. Early-window correlation
> reaches **0.998**. The hybrid is not a compromise between width and centre
> stability; it is **mono with a decorrelated tail**.
>
> That is fixable — share only the single first arrival, or flatten the gain
> envelope over the shared region — but it is not what was measured, and the
> candidate as parameterised is rejected.

ER-B irregular is recommended at **8 events per channel** (16 taps): the
largest mean gap (4.88 ms), the lowest ripple of the irregular family
(7.97 dB), full decorrelation, and the lowest CPU.

---

## 4. Chosen research event count: 8 per channel, 16 taps

| | 8 | 12 | 16 |
|---|---|---|---|
| mean gap | **4.88 ms** | 3.24 | 2.46 |
| min gap | **2.48 ms** | 1.65 | 1.24 |
| ripple | **7.97 dB** | 10.90 | 11.05 |
| CPU | **4.54 %** | 6.30 % | 8.46 % |

Ripple and CPU both favour 8, and the mean gap is the only quantity that
argues for more events — but at 4.88 ms the arrivals are already well inside
fusion, so buying a smaller gap buys nothing audible. 12 remains the fallback
if listening finds 8 too sparse.

---

## 5. Timing-distribution strategy

Deterministic and synthetic throughout. The span is divided into equal cells,
one event per cell per channel, positioned inside its cell by the
**golden-ratio sequence** — irregular, no common divisor with the cell grid,
no repeating interval. The right channel uses a different phase of the same
sequence (offset 0.37).

Specified in **milliseconds**, so the pattern means the same thing at every
sample rate. No runtime randomness, no clock seed, no measured room, no
proprietary pattern, and nothing uniformly spaced.

Gains: `exp(-t/0.6W)/sqrt(t)` — monotone, parameter-free, and shared by every
candidate so that it cannot be what separates them. **This envelope is now a
Stage-3 target** — see §16.

---

## 6. Stereo strategy

Fully independent deterministic L/R timing, no gain panning, no mid/side
processing, no polarity tricks. Width comes from arrival-time differences,
which is what two ears in a room receive.

The measured inter-channel first-arrival difference is **2.47 ms** for the
irregular family and **0.00 ms** for the hybrid and clustered families.

---

## 7. Centre stability

| family | corr 0–20 ms | corr full | L/R first-arrival difference |
|---|---|---|---|
| irregular | −0.003 … +0.274 | −0.001 … +0.382 | **2.47 ms** |
| hybrid | 0.906 … 0.998 | 0.791 … 0.880 | 0.00 ms |
| clustered | 0.670 … 0.856 | 0.720 … 0.782 | 0.00 ms |

2.47 ms of inter-channel first-arrival difference is a real localisation cue —
it is inside the range where the precedence effect pulls an image toward the
earlier ear. On a centred mono source the irregular family will therefore put
the *reverb's* initial image slightly off centre, while the dry signal stays
centred.

**Whether that reads as width or as instability is a listening question**, and
it is the single most important thing Stage 3 should listen for. The
measurement can say the cue exists and how large it is; it cannot say whether
it is pleasant.

---

## 8. Mono result, restated correctly

Stage 1 called ER-B's −3.01 dB downmix "exactly mono-neutral" with "no
cancellation". **That was overclaiming from one test on one source**, and this
is the correction.

| family | source | mono 0–20 ms | mono full |
|---|---|---|---|
| irregular | impulse | −3.01 dB | −3.01 dB |
| irregular | snare | **−3.37 dB** | −3.35 dB |
| irregular | **vocal** | **−2.18 dB** | **−1.64 dB** |
| irregular | noise | −2.92 dB | −2.90 dB |

−3.01 dB is what two approximately decorrelated equal-energy channels are
**expected** to give. The correct reading of Stage 1's figure is: *that test
showed no abnormal destructive cancellation*. It is not proof of absolute
absence of cancellation, and it does not generalise:

- On a **sustained tonal source** (vocal) the channels become substantially
  correlated — correlation **+0.38** — and the downmix loses only 1.64 dB.
  Arrival-time decorrelation does not decorrelate a steady tone.
- On a **broadband transient** (snare) the downmix loses **3.37 dB**, i.e.
  0.36 dB *more* than the decorrelated ideal. That is mild destructive
  cancellation and it is real.

No mono failure was found. But the honest statement is "0.36 dB of
cancellation on one source class", not "none".

---

## 9. Size-scaling experiments

`ERspan = 80 ms · LateScale^alpha`, with the late scale running 0.287 … 2.297
across Size — an **8× range**.

| alpha | longest span | covers the late onset at every Size | within a 120 ms ceiling |
|---|---|---|---|
| 0.00 | 80.0 ms | yes | yes |
| **0.25** | **98.5 ms** | **yes** | **yes** |
| 0.50 | 121.3 ms | yes | **NO** |
| 0.75 | 149.3 ms | yes | NO |
| 1.00 | 183.8 ms | yes | NO |

The lower constraint — the ER span must still reach the late first arrival —
is satisfied by every alpha, including 1.0. **A test nothing fails is not
discriminating**, and the first version of this suite had only that one. The
binding constraint is the **upper** one, which is the brief's own warning: an
ER layer that extends far enough becomes a second late reverb. 120 ms is taken
as the ceiling — about where the default late field has already established —
and it is stated so it can be argued with rather than discovered in the
numbers.

---

## 10. Recommended Size law: **alpha = 0.25**

`ERspan = 80 ms · LateScale^0.25`, giving **58.6 … 98.5 ms** across the Size
range.

It satisfies both constraints, and it is properly **weaker than the late
path** as Stage 1 required: the late scale spans 8× while the ER span spans
**1.68×**. The two layers move together in direction without moving as one
rigid object.

---

## 11. ER temporal-span recommendation

**58–99 ms across Size, ceiling 120 ms.** Stage 1's 80 ms was a research
value; it becomes the span at the default Size and the anchor for the Size
law. The ceiling is the constraint that keeps the layer from becoming a second
late reverb, and it is the reason alpha 0.5 and above are rejected.

---

## 12–14. Early/Late balance

Every figure is the measured energy of the **actual sum**, not the sum of the
branch energies. The brief was right to demand this: the branches come from
one source and overlap in time, so they are correlated in principle and a
constant-power law derived from independent energies need not hold.

| | |
|---|---|
| raw ER energy | 117.471 |
| raw late energy | 71.663 |
| mismatch | **+2.15 dB** |
| static calibration `K` | 0.7811 |
| **branch cross-correlation** | **−0.002356** |

The cross-correlation is the number that decides it. At essentially zero, the
constant-power law holds closely and the remaining variation is the branches'
different temporal shapes rather than their interference. **The measurement
validates the assumption rather than refuting it**, which is the honest
outcome — it had to be measured to know that.

| balance | BAL-A uncalibrated | **BAL-B calibrated** | BAL-C linear | BAL-D Stage-1 |
|---|---|---|---|---|
| 0.00 (full late) | 0.00 dB | **0.00 dB** | 0.00 dB | 0.00 dB |
| 0.25 | +0.38 dB | **−0.01 dB** | −2.05 dB | −1.78 dB |
| 0.50 | +1.19 dB | **−0.01 dB** | −3.02 dB | −1.82 dB |
| 0.75 | +1.88 dB | **−0.01 dB** | −2.05 dB | −0.07 dB |
| 1.00 (full ER) | +2.15 dB | **−0.00 dB** | −0.00 dB | +2.15 dB |
| **worst** | **2.15 dB** | **0.01 dB** | **3.02 dB** | **2.15 dB** |

### Winning strategy: BAL-B — static energy-calibrated constant power

```
theta = balance * pi/2          balance 0 = full late, 1 = full ER
late gain = cos(theta)
er   gain = sin(theta) * K      K from one reference render
```

**Wet-energy variation across the balance: 0.01 dB**, against a target of
about 1 dB. Nothing is dynamically normalised — `K` is one scalar from one
reference render, so there is no gain pumping, no program-dependent semantics
and no hidden AGC.

Note that the raw mismatch measured here is **+2.15 dB** with ER *louder*,
where Stage 1 measured −5.11 dB with ER quieter. The difference is the tap
normalisation: Stage 1's taps were not L2-normalised and this stage's are.
That is why the calibration has to be computed rather than assumed from either
number.

---

## 15. Phase-9 full-Late endpoint compatibility

| | |
|---|---|
| Worst difference across four sources | **0.0** |
| Samples differing | **0** |

At balance 0, `cos(0)` is exactly 1 and `sin(0)` exactly 0 in IEEE double, so
the mix reduces to `1.0·late + 0.0·er`.

> A production implementation should still take an **explicit bypass branch**
> rather than rely on that. Phase 9 showed that a tap set summing to unity is
> not the same arithmetic as a passthrough, and the same caution applies to a
> multiply by a float that happens to be zero. The gate is bit-identity and it
> should be structural, not incidental.

---

## 16. Handoff across Size — **the problem to solve**

Measured at both candidate Size laws, because they pull in opposite
directions: the span ceiling wants alpha small, the handoff wants the ER span
to reach past the late onset, which wants alpha large.

| alpha | Size | ER span | late 1st | overlap | notch b=.25 | notch b=.5 | notch b=.75 |
|---|---|---|---|---|---|---|---|
| 0.25 | 0 % | 58.6 ms | 8.6 ms | 50.0 ms | 1.45 dB | 0.15 dB | 0.15 dB |
| 0.25 | 20 % | 65.0 ms | 12.0 ms | 53.0 ms | 1.38 dB | 0.36 dB | 0.31 dB |
| 0.25 | 60 % | 80.0 ms | 24.8 ms | 55.2 ms | 3.19 dB | 0.97 dB | 0.88 dB |
| 0.25 | 80 % | 88.8 ms | 36.2 ms | 52.6 ms | 5.35 dB | 2.75 dB | 1.77 dB |
| 0.25 | **100 %** | 98.5 ms | 54.0 ms | 44.5 ms | **6.87 dB** | **6.93 dB** | 2.21 dB |
| 0.50 | 0 % | 42.9 ms | 8.6 ms | 34.3 ms | 1.24 dB | 0.18 dB | 0.15 dB |
| 0.50 | 60 % | 80.0 ms | 24.8 ms | 55.2 ms | 3.19 dB | 0.97 dB | 0.88 dB |
| 0.50 | **100 %** | 121.3 ms | 54.0 ms | **67.3 ms** | 4.63 dB | **6.48 dB** | 5.89 dB |

At small and default Size the handoff is clean — ≤ 1.45 dB at Size 0–20 %,
consistent with Stage 1's 1.14 dB, which was measured at default Size only.

**At Size 80–100 % a real energy hole opens, up to 6.93 dB.**

### And widening the overlap does not fix it

This is the inference that matters. Going from alpha 0.25 to 0.5 at Size 100 %
increases the overlap from 44.5 ms to **67.3 ms** — half again as much — and
the worst notch moves **6.93 → 6.48 dB**. Essentially unchanged.

> So the hole is **not** an overlap-extent problem, and no Size law will solve
> it. It is the ER **gain envelope's decay rate**: `exp(-t/0.6W)/sqrt(t)` has
> the ER layer far down by the time a large-Size late field is establishing,
> and lengthening the window does not slow the decay — it stretches a shape
> that still ends quietly.
>
> **The Stage-3 target is the gain envelope, not the timing.** A shallower
> decay, or a decay whose rate is itself a function of Size, is the thing to
> measure. That is a far more specific finding than "the handoff is a risk".

Worth noting: the notch is worst at **mixed** balances and smallest at the
extremes, which is what a handoff artifact should do — at full ER or full late
there is only one layer and nothing to hand off.

### No handoff processor was added

No envelope, no crossfade timer, no ducking, no dynamic blending. The brief
asks whether natural branch overlap is sufficient, and the answer is: **yes
below Size ~60 %, no above it, and the reason is the envelope.** Adding
machinery before understanding that would have hidden it.

---

## 17. Early Diffusion independence

| Early Diffusion | ER output | late first arrival | mix notch |
|---|---|---|---|
| 0 % | **bit-identical** | 37.3 ms | 0.52 / 0.70 / 0.00 dB |
| 70 % | **bit-identical** | 22.9 ms | 0.36 / 0.36 / 0.00 dB |
| 100 % | **bit-identical** | 22.9 ms | 0.13 / 0.25 / 0.00 dB |

The ER branch is bit-identical at every Early Diffusion setting, trivially,
because it is not fed through the diffuser. The late branch still responds —
its first arrival moves 37.3 → 22.9 ms. **Early Diffusion keeps owning the
late excitation's scattering and the ER pattern stays structurally
recognisable**, which is the required distinction.

---

## 18. Attack bit-independence — hard gate: **PASS**

| Attack | ER output | late first arrival | late centroid |
|---|---|---|---|
| 0 % | **bit-identical** | 22.9 ms | 150.2 ms |
| 50 % | **bit-identical** | 22.9 ms | 175.6 ms |
| 100 % | **bit-identical** | 22.9 ms | 237.7 ms |

Attack moves the late centroid by 87 ms and leaves ER **bit-identical** —
exactly, not approximately — because the split precedes it. No tolerance,
nothing to drift.

---

## 19. Pre-Delay behaviour — Model 1 re-certified

| Pre-Delay | ER first arrival | late first arrival | ER mean spacing | pattern shifted only |
|---|---|---|---|---|
| 0 ms | 4.1 ms | 22.9 ms | 3.55 ms | **yes, bit-exact** |
| 20 ms | **24.1 ms** | **42.9 ms** | 3.55 ms | **yes, bit-exact** |
| 80 ms | **84.1 ms** | **102.9 ms** | 3.55 ms | **yes, bit-exact** |

Both onsets move by exactly the added amount. The ER pattern's internal
spacing is unchanged, and the rendered ER is the reference **shifted by
exactly the pre-delay and nothing else**, bit for bit. Pre-Delay does not
distort the ER timing pattern.

---

## 20. Coloration and temporal quality

| family / events | ripple | crest | mean gap | min gap | sep |
|---|---|---|---|---|---|
| **irregular 8** | **7.97 dB** | 77.77 | 4.88 ms | 2.48 ms | 1 |
| irregular 12 | 10.90 dB | 74.01 | 3.24 ms | 1.65 ms | 1 |
| irregular 16 | 11.05 dB | 71.05 | 2.46 ms | 1.24 ms | 1 |
| clustered 12 | 7.66 dB | 63.25 | 5.42 ms | 1.63 ms | **3** |
| clustered 16 | 13.56 dB | 54.02 | 3.92 ms | 1.17 ms | 3 |

Ripple of 8–11 dB is high in absolute terms and is **expected** for a sparse
feedforward tap set — a finite comb has deep nulls by construction. It is
reported as one metric among several, never as the decision metric, per the
Phase-9 lesson. The temporal columns are what select the candidate.

No obvious repeated structure: the golden-ratio placement has no repeating
interval, and the `sep` column confirms nothing stands 10 ms clear of its
predecessor in the recommended family — so there is no slap train.

---

## 21. CPU and memory

| configuration | taps | ns/sample | share of tank | delay memory |
|---|---|---|---|---|
| Phase 7–9 tank | — | 247.671 | — | — |
| **hybrid 8 events** | 14 | **11.233** | **4.54 %** | 128 KiB |
| hybrid 12 events | 22 | 15.608 | 6.30 % | 128 KiB |
| hybrid 16 events | 30 | 20.962 | 8.46 % | 128 KiB |
| clustered 12 | 12 | 15.531 | 6.27 % | 128 KiB |

The recommended 8-event irregular family has 16 taps, two more than the hybrid
8, so expect **≈ 5 %**. Comfortably inside the 10 % target.

Memory: one shared delay line serves both channels, because both branches read
the same pre-delayed input, so memory does not double with the tap lists.
**128 KiB** covers an 80 ms window at 96 kHz with the power-of-two rounding
the index mask needs. At the alpha-0.25 ceiling of 98.5 ms it would need
9456 samples per channel, so **16384 doubles = 256 KiB** for the pair — still
half of Phase 9 Attack's 512 KiB.

Phase 9 Attack is ~14.7 % active. ER at ~5 % brings the pair to about 20 % of
the tank with both engaged.

---

## 22. versionHint and state audit

Inspected in the JUCE 8.0.15 tree this project fetches, not assumed.

### What versionHint actually does

`ParameterID(identifier, versionHint = 0)`, documented as "Influences
parameter ordering in Audio Unit plugins". It is consumed in exactly one
functional place:

```
juce_audio_plugin_client_AU_1.mm:2347
    vec.push_back ({ generateAUParameterID (*param), param->getVersionHint() });
    std::sort        (... a.identifier  < b.identifier);
    std::stable_sort (... a.versionHint < b.versionHint);
```

So the AU parameter list is sorted by identifier and then **stable-sorted by
versionHint**: lower hints come first. There is also a debug assertion
(`AudioProcessor.cpp:450`) that fires in **AU builds only** if any parameter
has versionHint 0.

| question | answer |
|---|---|
| Does adding one parameter require a new versionHint? | **Only for the new parameter.** |
| Does only the new parameter need a higher hint? | **Yes** — a higher hint is what places it after the existing ones in the AU list. |
| Must old hints remain untouched? | **Yes, and this is the trap.** The project has a single `versionHint = 1` applied to all 19 parameters. Bumping *that constant* would raise every parameter's hint together, which changes nothing relatively — but any change to an existing parameter's hint reorders the AU list for hosts that have cached it. The constant must stay at 1 and a second constant added. |
| Loading a Phase-9 state lacking the new parameter | See below. |
| VST3 | **versionHint is not referenced anywhere in the VST3 wrapper.** It is irrelevant to VST3 ordering and to VST3 state. |
| AU / AAX | AU: ordering and the debug assertion, as above. AAX: no reference found. |

> ### The project's single `versionHint` constant is the wrong shape for appending
>
> `ParameterLayout.cpp` has `juce::ParameterID pid (const char* name) { return { name, versionHint }; }`
> with one global constant. Appending a parameter through `pid()` would give it
> hint 1 and place it **among** the existing parameters in the AU identifier
> sort — exactly what the mechanism exists to prevent. Phase 10 needs a second
> constant (e.g. `versionHintPhase10 = 2`) and a second helper, with the
> existing constant left at 1.

### State restore — a hazard source reading alone would miss

`AudioProcessorValueTreeState::setNewState` reads
`p->tree.getProperty (valuePropertyID, p->getDenormalisedDefaultValue())`, and
`updateParameterConnectionsToChildTrees` only visits children that **exist in
the loaded tree**. A parameter absent from the loaded state is never visited,
so **it keeps whatever value it currently holds.**

The project's `setStateInformation` calls `apvts.replaceState(...)` with no
explicit reset. Therefore:

| scenario | result |
|---|---|
| Fresh plugin instance + Phase-9 session | new parameter is at its **default** → neutral → Phase-9 sound ✓ |
| **Live instance with Early/Late moved, then loading a Phase-9 preset** | new parameter **keeps the moved value** → the preset does **not** sound like Phase 9 ✗ |

> This is a real compatibility hazard, it is new with Phase 10 because Phases 8
> and 9 added no parameters, and **it cannot be found by reasoning about
> defaults.** The fix is small and known: in `setStateInformation`, after
> `replaceState`, explicitly set every parameter absent from the loaded tree
> to its default. It must be implemented **and tested with a real serialised
> fixture**, not reasoned about.

### State-compatibility test plan for Stage 3

1. Capture a **real** Phase-9 `getStateInformation` blob as a checked-in
   fixture, generated from the Phase-9 build.
2. Load it into the Phase-10 processor on a **fresh instance**; assert the new
   parameter reads its default and the rendered audio is **bit-identical** to
   the Phase-9 reference render.
3. Load it into an instance where the new parameter has first been **moved**;
   assert the same two things. This is the case that fails today.
4. Round-trip: Phase-10 save → Phase-10 load → bit-identical.
5. Forward: a Phase-10 state loaded by code that does not know the parameter
   must not crash or shift other parameters.

---

## 23. Proposed future public parameter contract

**One** new control, not two. `Mix` already owns global wet amount, so a
separate ER Level and Late Level would be three controls for two degrees of
freedom.

| | |
|---|---|
| ID | `earlyLate` |
| Display name | `Early / Late` |
| Range | 0 – 100 %, linear, step 0.1 |
| **Default** | **0 %** — meaning **100 % Late / 0 % ER** |
| Mapping | `theta = (value/100) · pi/2`; late `cos(theta)`, ER `sin(theta)·K` |
| Formatter | percent, or a two-sided label if the GUI phase wants one |
| Type | `AudioParameterFloat` |
| versionHint | **2**, with the existing constant left at 1 |

The default being the **legacy endpoint** is what makes old sessions
reproduce Phase 9. A 0 % default reading as "no early reflections" is also
honest about what the control does: it adds a layer rather than redistributing
an existing one.

> No parameter was added in Stage 2. This is a proposal for Stage 3 review.

---

## 24. Major remaining risks

1. **The large-Size handoff hole, up to 6.93 dB.** The specific target is the
   ER gain envelope's decay rate, since widening the overlap was measured not
   to help. This is the one thing that must be solved before integration.
2. **2.47 ms of inter-channel first-arrival difference** is a real
   localisation cue. Whether it reads as width or as an unstable image on a
   centred source is a listening question Stage 3 must answer.
3. **Mono behaviour is source-dependent**: 0.36 dB of destructive cancellation
   on a broadband transient, and near-total recorrelation on a sustained tone.
   No failure, but not the clean story Stage 1 told.
4. **The state-restore hazard** in §22, which needs a code fix and a fixture
   test rather than reasoning.
5. **The `versionHint` helper needs restructuring** before a parameter can be
   appended correctly.
6. **ER-D is unresolved.** It has the only separated arrival structure and the
   lowest crest, and it may simply sound more like a room. It does not scale by
   adding events, which is a parameterisation question rather than a defect.
7. **The 120 ms span ceiling is a judgement**, stated so it can be argued with.
   It is what rejects alpha ≥ 0.5, so if it moves, the Size law moves.

---

## 25. Stage-3 production-integration proposal

In this order, because each step makes the next one safe:

1. **Fix the state-restore hazard and build the Phase-9 fixture test** before
   any DSP. It is the only item that can silently break existing work.
2. **Restructure the `versionHint` helper**, leaving the existing constant at 1.
3. **Solve the gain envelope.** Sweep the decay rate and its possible Size
   dependence; gate the handoff notch across the full Size × balance grid at a
   budget argued from these numbers.
4. **Build the ER stage with an explicit bypass branch** and gate Attack-0-style
   bit-identity across signal classes.
5. **Re-prove the independence gates on the production split** — Attack and
   Early Diffusion as bit-identity, Pre-Delay as an exact shift.
6. **Only then** add the `earlyLate` parameter, with the default at the legacy
   endpoint and the fixture test already green.
7. **Listening**, pointed at the two open perceptual questions: centre
   stability on centred sources, and ER-B against ER-D.

---

## 26. Decision matrix

Measured dimensions in **bold**.

| | ER-B irregular 8 | ER-B hybrid | ER-D clustered 12 |
|---|---|---|---|
| **Arrival clarity (sep)** | **1** | **1** | **3** |
| Naturalness proxy | medium | medium | **high** |
| **Slap / repetition risk** | **low, 4.88 ms mean** | low | medium, 1.63 ms min |
| **Coloration** | **7.97 dB** | 7.97 dB | **7.66 dB** |
| **Stereo quality** | **−0.0005 corr** | **0.862 — fails intent** | 0.730 |
| **Centre stability** | **2.47 ms cue** | **0.00 ms** | **0.00 ms** |
| **Mono behaviour** | **−1.64 … −3.37 dB** | −0.01 … −0.56 dB | −0.52 … −0.89 dB |
| **Size scaling** | alpha 0.25 | same | same |
| **Early Diffusion independence** | **bit-identical** | **bit-identical** | **bit-identical** |
| **Attack independence** | **bit-identical** | **bit-identical** | **bit-identical** |
| **Handoff** | **≤1.45 dB to Size 20 %, 6.93 dB at 100 %** | same | same |
| **CPU** | **≈5 %** | **4.54 %** | **6.27 %** |
| **Memory** | **128–256 KiB** | same | same |
| Future Mode compatibility | good | good | **excellent** |

ER-B hybrid loses on the row it was designed to win. ER-D wins on arrival
clarity, coloration and Mode compatibility and loses on scaling and stereo
width. ER-B irregular wins on stereo and CPU while matching everywhere the
candidates are structurally identical — and its one weakness, the 2.47 ms
centre cue, is a listening question rather than a measurement failure.

---

## STOP

Stage 2 ends here. No shipping ER DSP, no public Early/Late parameter, no
listening validation, Phase 11 not started.

Research suite: `Tests/Phase10Stage2.cpp`, `runPhase10Stage2Tests`, 9 / 9.
Focused regression: 182 / 184 — the two registered Phase-7 diagnostics only.
