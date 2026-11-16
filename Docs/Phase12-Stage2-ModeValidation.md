# PHASE 12 — Mode System · Stage 2: Mode family validation

**Research only. No production DSP was modified.** `Source/` is byte-identical
to the Phase 11 and Phase 10 closures.

## The result, first

> **None of the five Stage 1 candidates is proven to be a family.**
>
> Under the test fixed before any candidate was scored, **every one of them
> can be reproduced by a different Mode using only the ordinary user
> controls.** Room is Hall at a smaller Size. Chamber and Plate are each
> other. Ambient is the nearest to surviving and still falls short.

Two qualifications belong beside that sentence, not under it.

**The test has a blind spot, and I found it only because the result was
uniformly negative.** A positive control I added afterwards — the tank with
its mixing matrix removed — also scored as "reproduced", and an independent
measurement confirmed it is genuinely different. So the yardstick cannot see
at least one real structural property. I then checked whether the candidates
were hiding in that blind spot. On the evidence they are not, but that check
is weaker than the main test and is reported as such (section 15).

**"Not proven" is not "proven to be presets".** The honest reading is: in
every dimension this stage could measure, the candidates are reachable from
one another; in one dimension it could not measure well, they differ by a
small amount of unknown audibility. A listening pack that tests exactly that
is prepared (section 29).

The brief allowed this conclusion and asked that it not be softened. It is not.

---

## 1. Research configuration access added

**Nothing in `Source/` changed.** Every candidate is instantiated from public
calls that already exist, composed one level below the processor's own tank.

| needed | how it was reached |
|---|---|
| diffuser configuration | an external `StereoDiffusionNetwork` — the production class — in place of the one `StereoFdn` keeps private. `setStageCount` and `setDelayScale` are public on it |
| FDN delay configuration | `FdnCore<8>` directly — the production class — prepared with each Mode's own capacity. `StereoFdn` fixes capacity from the shipping set |
| ER pattern, ER timing | `er::Pattern`, already data |
| **ER channel geometry** | **the one re-implementation.** The production schedule builder has no seam for it, so the rig restates that function with one added parameter |
| spatial scale | the pattern's own milliseconds |
| injection / readout | the arithmetic of `StereoFdn::processSample`, restated; shipping vectors in every Mode |
| allpass sections per line | `FdnCore::setAllpassSectionsPerLine`, public |
| wet trim | `mapping::tankPowerGain`'s formula, given the delay set as an argument |

**No processing stage was added.** Stage 1's claim — no new DSP mechanism is
required — **holds for instantiating the candidates.** It does not hold for
making them families, which is the finding of this stage.

Rig: [Tests/Phase12ModeRig.h](../Tests/Phase12ModeRig.h). Harness:
[Tests/Phase12ModeFamilies.cpp](../Tests/Phase12ModeFamilies.cpp).

### The candidates as built

Each non-Hall delay set is the output of the same directed search that chose
the shipping one, aimed at the middle of its Stage 1 range. Nothing was tuned
against a result.

| mode | delay set, ms | sum | max/min |
|---|---|---|---|
| Room | 13.61 15.13 18.89 22.27 24.43 30.91 34.65 42.37 | 202.3 | 3.11 |
| **Hall** | **26.27 30.38 38.10 42.58 47.06 54.20 66.99 82.92** | **388.5** | **3.16** |
| Chamber | 27.04 29.64 32.58 33.85 38.10 41.82 47.13 52.40 | 302.6 | 1.94 |
| Plate | 20.64 22.97 25.35 26.27 28.54 29.55 32.49 36.43 | 222.2 | 1.77 |
| Ambient | 28.25 35.47 44.47 55.06 67.74 76.49 97.42 116.47 | 521.4 | 4.12 |

| mode | diffuser | sections | mod depth / rate | ER events | window | envelope | alpha | L/R spread |
|---|---|---|---|---|---|---|---|---|
| Room | 4 × 0.60 | 1 | 0.70 / 1.00 | 10 | 40 ms | 0.8 | 0.35 | 1.00 |
| **Hall** | **4 × 1.00** | **1** | **1.00 / 1.00** | **8** | **80 ms** | **1.5** | **0.25** | **1.00** |
| Chamber | 6 × 1.00 | 1 | 0.90 / 1.00 | 14 | 55 ms | 2.5 | 0.25 | 1.00 |
| Plate | 8 × 0.50 | 2 | 0.90 / 1.00 | 28 | 25 ms | 8.0 | 0.10 | 0.25 |
| Ambient | 8 × 1.50 | 1 | 1.30 / 0.70 | 14 | 135 ms | 8.0 | 0.25 | 1.00 |

One deviation from Stage 1. It said Ambient needed a new balanced long
diffuser set searched. It does not: the continuation stages of the shipping
input set already total 71.80 ms left against 71.44 ms right, so all eight
stages of the existing set are balanced as they stand.

## 2. Hall baseline identity

**Byte-identical.** Given the shipping data, the rig is the shipping engine.

| reference | cases | result |
|---|---|---|
| one production bank (`StereoFdn`) with the production `EarlyReflections`, `EarlyLateBalance`, `AttackInjection`, Size compensation and wet trim | defaults · Early/Late 50 · Early/Late 100 · small/short · large/long with Early/Late 30 · Attack 40 with Early Diffusion 20 · Mod Depth 0 · Mod Depth 100 at Rate 1.5 | **8 of 8 byte-identical**, worst difference 0 |
| the full dual-bank production tank (`MorphingStereoFdn`), after its opening Size transition settled on silence | defaults · small/short | **byte-identical** |

Also verified: the Hall candidate is the shipping data field for field; two
fresh rigs produce the same bytes; the delay-set-aware wet trim returns the
shipping trim exactly for the shipping set.

RT60, the ER layer, the late response, stereo behaviour and the spectrum are
functions of the samples, and the samples are the same samples. Width acts
after this point and no Mode touches it.

**One link is not closed here and is stated rather than implied.** This test
target does not link JUCE, so the rig is compared against the production
*classes*, not the *processor*. "The rig equals the plugin" rests on this
section plus the state suite's 12-of-12 legacy byte comparison of the
processor, which is unchanged because `Source/` is unchanged.

## 3. Room results

At the same knobs, Room is clearly different from Hall:

| defaults, Early/Late 0 | t10 | t50 | C50 | NED 0.9 at | NED at 10 ms |
|---|---|---|---|---|---|
| Room | 34.3 ms | 113.1 ms | −5.9 dB | 16 ms | 0.80 |
| Hall | 48.3 ms | 131.1 ms | −9.0 dB | 40 ms | 0.18 |

Faster buildup, more early energy, far denser onset — everything Stage 1
asked of it. Same-knob distance from Hall: **1.6 at its weakest operating
point, 3.1 median.**

**And Hall reproduces it with one hand.** Hall at Size 22 %, Early Diffusion
83 %, Mod Depth 15 % lands **0.32** from Room at the defaults.

## 4. Room at Early/Late = 0

**REJECTED, on the brief's own criterion.**

> If Room becomes essentially Hall when ER is removed/reduced, reject the
> candidate.

The target had no early layer at all. Hall was allowed every control it has,
including its own early layer, and did not need it:

| target | closest Hall | residual |
|---|---|---|
| defaults | Size 22, Decay 2.45 s, E/L 0, Early Diff 83, Mod 15 | **0.32** |
| small / short | — | **0.84** |
| large / long | — | **0.40** |

REPRODUCED is anything at or below 1.50. For scale: 0.32 is closer than Hall
sits to *its own sibling delay set* at large/long (1.02).

**Why, and it is not subtle.** Room's delay set is Hall's at 0.52× with nearly
the same spread — 3.11 against 3.16. **A delay set that is a scaled copy of
another is exactly what the Size control already does.** Stage 1 required
Room's "Late geometry/buildup" to carry the identity, and the candidate's late
geometry is Hall's geometry, smaller. I designed it that way and did not see
it.

## 5. Hall results

Hall is the shipping engine and was not altered or exaggerated. Its quality is
the reference and is unchanged by construction.

As a *family* it shares Room's verdict, because the relation is symmetric:
**Room reproduces Hall at 0.22** (Room at Size 92 %, Attack 24 %). Hall and
this Room candidate are one family at two Sizes.

## 6. Chamber results

| defaults, Early/Late 0 | t10 | t50 | C50 | NED 0.9 at | corr 0–50 ms |
|---|---|---|---|---|---|
| Hall | 48.3 | 131.1 | −9.0 | 40 ms | 0.22 |
| **Chamber** | 63.9 | 145.7 | −15.4 | 38 ms | 0.09 |
| Plate | 61.9 | 144.5 | −14.4 | 36 ms | 0.06 |

Chamber and Plate are nearly the same response before any knob is touched:
same-knob distance **0.3 at the weakest point, 1.0 median** — *below the
same-family floor*.

## 7. Chamber mimic-search result

**CHAMBER FAMILY NOT PROVEN.**

| impostor | closest residual | at target | impostor settings |
|---|---|---|---|
| **Plate** | **0.23** | small / short | Size 14, Decay 0.60 s, Early Diff 68, Mod 32 |
| **Hall** | **0.36** | defaults | Size 68, Early Diff 62, Attack 32, Mod 100 |
| Room | 0.44 | defaults, E/L 50 | Size 92, E/L 50, Early Diff 34, Attack 36 |
| Ambient | 2.96 | defaults | — |

The one large number in Chamber's row — Room at large/long, **5.44** — is real
and does not save it: a Mode is a preset of another if *any* Mode reaches it,
and three do.

Stage 1 flagged Chamber as the candidate most at risk. That was right, but for
the wrong reason. It is not a point between Room and Hall. It is the same
point as Plate.

## 8. Plate results

Same-knob, Plate sits 1.6–2.0 from Hall and 0.3–1.0 from Chamber. Its onset is
denser than Hall's at the defaults (echo density 0.55 in the window from 10 ms
against 0.18) and that is the whole of its distinction.

## 9. Plate density result

**Plate is not the densest Mode. Room is.**

Echo density 0.9 reached at:

| setting | Room | Hall | Chamber | **Plate** | Ambient |
|---|---|---|---|---|---|
| small / short | **10 ms** | 16 | 30 | 22 | 50 |
| medium / medium | **16 ms** | 40 | 38 | 36 | 46 |
| large / long | **40 ms** | 96 | 64 | 48 | 86 |

Plate's target was density "from the first instant". At the defaults its echo
density in the window from 10 ms is **0.55**, against a target I set at 0.7
and Room's 0.80.

**And the plate-specific early pattern does the opposite of its purpose.**
Stage 1 proposed 28 taps in 25 ms on the argument that so many events so close
together are "a burst of density" rather than reflections:

| Plate | echo density from 10 ms |
|---|---|
| Early/Late 0 | 0.55 |
| Early/Late 50 | **0.06** |

28 taps in 25 ms is about one per millisecond, against 48 samples per
millisecond of a diffuse field. It is still a sparse tap list, merely a busier
one, and because it is loud relative to the tank's onset, turning it up makes
the onset **less** dense. Early/Late keeps its meaning in Plate. The pattern
is wrong.

## 10. Plate mimic-search result

**PLATE FAMILY NOT PROVEN.**

| impostor | closest residual | at target | impostor settings |
|---|---|---|---|
| **Chamber** | **0.15** | small / short | Size 20, Decay 0.60 s, Early Diff 70, Attack 8, Mod 46 |
| **Hall** | **0.45** | small / short | Size 36, Early Diff 68, Attack 32, Mod 30 |
| Room | 0.67 | small / short | Size 68, Early Diff 54, Attack 40, Mod 100 |
| Ambient | 2.78 | defaults | — |

No damping was used to establish Plate, as the brief required. Nothing else
established it either.

## 11. Ambient results

Ambient is the one candidate that is far from the others at the same knobs:

| defaults, Early/Late 0 | t10 | t50 | tPeak | C50 | C80 |
|---|---|---|---|---|---|
| Hall | 48.3 | 131.1 | 51.4 | −9.0 | −4.1 |
| **Ambient** | **129.5** | **232.7** | **171.9** | **−32.8** | **−20.3** |

Same-knob distance from every other Mode: **3.2 to 7.6 at the weakest point.**
It is what Stage 1 described — the only Mode whose onset is itself smeared.

## 12. Ambient mimic-search result

**AMBIENT FAMILY NOT PROVEN** — the closest call of the five.

| impostor | closest residual | at target | impostor settings |
|---|---|---|---|
| **Plate** | **1.08** | defaults | Size 92, Early Diff 78, **Attack 72**, Mod 22 |
| **Chamber** | **1.24** | defaults | Size 78, Early Diff 60, **Attack 78**, Mod 20 |
| Room | 1.36 | defaults, E/L 50 | Size 69, E/L 0, Early Diff 8 |
| Hall | 1.60 | defaults, E/L 50 | Size 31, E/L 0, Early Diff 0 |

**Against Hall at extreme controls** — the brief's specific test — Ambient
holds at Early/Late 0: Hall's best is **2.34** at the defaults, **2.15** small
and short, **2.32** large and long. All WEAK, none REPRODUCED. But with
Ambient's own early layer engaged, Hall reaches **1.60**, and Plate and
Chamber reach it outright.

**The mechanism is the Attack control.** Every successful impostor uses Attack
at 72–78 %. Phase 9 retained Attack precisely because it owns the onset of the
late field as an independent dimension, and Ambient's defining property is a
soft onset. **Ambient's identity is a dimension the product already sells as a
knob.**

Where it still differs after the best impostor: the energy profile family,
2.48 units, with every other family under 0.62. That residue is the eight-stage
long diffuser, which section 28 shows is the only hidden dimension with any
leverage at all.

Ambient uses no shimmer, freeze, granular processing, pitch processing or
feedback-delay effect. It is ordinary configuration of existing stages.

## 13. Complete pairwise Mode distance matrix

### Same knobs — every control held still

Minimum / median over the nine Size × Decay points. Rows and columns are the
two Modes compared.

**Early/Late 0**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | 1.6 / 3.1 | 2.9 / 4.4 | 3.1 / 4.4 | 7.6 / 10.3 |
| Hall | 1.6 / 2.9 | — | 1.5 / 2.0 | 1.6 / 2.0 | 5.6 / 7.0 |
| Chamber | 2.8 / 3.7 | 1.5 / 1.9 | — | **0.3 / 1.0** | 4.0 / 4.9 |
| Plate | 2.9 / 3.7 | 1.6 / 1.8 | **0.4 / 1.0** | — | 4.4 / 5.9 |
| Ambient | 5.7 / 7.4 | 4.7 / 5.3 | 3.2 / 4.0 | 3.8 / 4.8 | — |

**Early/Late 50**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | 1.6 / 2.0 | 1.6 / 2.4 | **0.7 / 1.7** | 7.5 / 8.5 |
| Hall | 1.3 / 1.7 | — | **0.7 / 1.2** | 1.2 / 1.5 | 5.2 / 6.6 |
| Chamber | 1.4 / 2.0 | **0.8 / 1.1** | — | **0.6 / 1.2** | 5.8 / 6.4 |
| Plate | **0.7 / 1.5** | 1.4 / 1.7 | **0.6 / 1.4** | — | 7.1 / 7.5 |
| Ambient | 5.3 / 5.9 | 4.5 / 5.4 | 4.8 / 5.4 | 5.2 / 5.7 | — |

**Early/Late 90**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | 2.1 / 2.6 | 1.3 / 2.0 | **0.6 / 1.1** | 7.7 / 8.2 |
| Hall | 1.6 / 1.9 | — | **0.9 / 1.2** | 2.2 / 2.4 | 5.3 / 6.7 |
| Chamber | 1.3 / 1.9 | **1.0 / 1.3** | — | **0.9 / 1.7** | 6.0 / 7.0 |
| Plate | **0.6 / 1.1** | 2.4 / 3.2 | 1.1 / 2.0 | — | 7.8 / 8.7 |
| Ambient | 6.1 / 6.8 | 5.0 / 6.1 | 5.4 / 6.2 | 5.9 / 6.6 | — |

The matrix is asymmetric because each feature difference is scaled by the
*target's* value.

### After the impostor search

Rows: the Mode being imitated. Columns: the impostor. Each cell: residual
after the search, with the same-knob distance before it in brackets.

**Target: defaults, Early/Late 0**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | **0.32** (3.1) | 1.39 (4.1) | 1.48 (3.8) | 3.97 (9.6) |
| Hall | **0.22** (2.9) | — | 0.92 (1.6) | 1.33 (1.7) | 3.20 (7.0) |
| Chamber | 0.72 (3.7) | **0.36** (1.6) | — | **0.35** (1.0) | 2.96 (5.0) |
| Plate | 0.92 (3.4) | 0.72 (1.6) | **0.31** (1.0) | — | 2.78 (5.2) |
| Ambient | 3.88 (6.8) | 2.34 (5.5) | **1.24** (4.2) | **1.08** (4.5) | — |

**Target: defaults, Early/Late 50**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | 0.70 (1.6) | 0.70 (1.8) | 0.82 (1.6) | 6.00 (8.5) |
| Hall | 0.45 (1.4) | — | 0.55 (0.8) | 1.10 (1.3) | 4.90 (7.1) |
| Chamber | 0.44 (1.6) | 0.48 (0.9) | — | 0.90 (1.0) | 4.30 (6.7) |
| Plate | 0.71 (1.6) | 1.09 (1.7) | 0.63 (1.1) | — | 5.07 (8.0) |
| Ambient | **1.36** (5.9) | 1.60 (5.5) | 3.15 (5.5) | 4.26 (5.8) | — |

**Target: small / short, Early/Late 0**

| | Room | Hall | Chamber | Plate | Ambient |
|---|---|---|---|---|---|
| Room | — | 0.84 (2.3) | 1.86 (4.4) | 2.13 (4.4) | 5.62 (11.8) |
| Hall | 0.35 (2.2) | — | 1.38 (2.4) | 1.42 (2.4) | 4.62 (9.5) |
| Chamber | 0.49 (3.7) | 0.41 (2.0) | — | **0.23** (0.4) | 2.97 (6.1) |
| Plate | 0.67 (3.7) | 0.45 (2.1) | **0.15** (0.4) | — | 3.03 (6.1) |
| Ambient | 2.69 (7.4) | 2.15 (6.3) | **1.24** (4.8) | **1.13** (4.8) | — |

**Target: large / long, Early/Late 0** — adversarial pairs only

| imitated ← impostor | residual |
|---|---|
| Room ← Hall | 0.40 (4.9) |
| Chamber ← Room | 5.44 (6.6) |
| Chamber ← Hall | 0.88 (2.8) |
| Plate ← Hall | 1.08 (2.3) |
| Plate ← Chamber | 0.26 (3.5) |
| Ambient ← Hall | 2.32 (4.7) |

Two patterns are worth reading out of these.

**Ambient cannot imitate anything.** Its column is 2.8–6.0 everywhere. A long
diffuser cannot be un-smeared by a knob. The relation is one-way: others can
reach Ambient by adding Attack; Ambient cannot reach them.

**The other four form one cluster.** Every residual among Room, Hall, Chamber
and Plate at the defaults is 1.48 or below.

## 14. Best mimic settings for every Mode pair

At the target where the impostor came closest.

| imitated ← impostor | residual | target | impostor settings |
|---|---|---|---|
| Room ← Hall | **0.32** | defaults, E/L 0 | Size 22 · Decay 2.45 · E/L 0 · EDiff 83 · Atk 0 · Mod 15 |
| Room ← Chamber | 0.70 | defaults, E/L 50 | Size 0 · Decay 2.50 · E/L 54 · EDiff 100 · Atk 0 · Mod 90 |
| Room ← Plate | 0.82 | defaults, E/L 50 | Size 28 · Decay 2.50 · E/L 50 · EDiff 100 · Atk 0 · Mod 16 |
| Room ← Ambient | 3.97 | defaults, E/L 0 | Size 0 · Decay 1.57 · E/L 11 · EDiff 100 · Atk 16 · Mod 0 |
| Hall ← Room | **0.22** | defaults, E/L 0 | Size 92 · Decay 2.50 · E/L 0 · EDiff 42 · Atk 24 · Mod 48 |
| Hall ← Chamber | 0.55 | defaults, E/L 50 | Size 28 · Decay 2.50 · E/L 50 · EDiff 46 · Atk 0 · Mod 60 |
| Hall ← Plate | 1.10 | defaults, E/L 50 | Size 44 · Decay 2.50 · E/L 50 · EDiff 70 · Atk 0 · Mod 6 |
| Hall ← Ambient | 3.20 | defaults, E/L 0 | Size 38 · Decay 1.94 · E/L 13 · EDiff 96 · Atk 0 · Mod 70 |
| Chamber ← Room | 0.44 | defaults, E/L 50 | Size 92 · Decay 2.50 · E/L 50 · EDiff 34 · Atk 36 · Mod 62 |
| Chamber ← Hall | **0.36** | defaults, E/L 0 | Size 68 · Decay 2.50 · E/L 0 · EDiff 62 · Atk 32 · Mod 100 |
| Chamber ← Plate | **0.23** | small / short | Size 14 · Decay 0.60 · E/L 0 · EDiff 68 · Atk 0 · Mod 32 |
| Chamber ← Ambient | 2.96 | defaults, E/L 0 | Size 60 · Decay 1.64 · E/L 0 · EDiff 100 · Atk 0 · Mod 78 |
| Plate ← Room | 0.67 | small / short | Size 68 · Decay 0.60 · E/L 0 · EDiff 54 · Atk 40 · Mod 100 |
| Plate ← Hall | 0.45 | small / short | Size 36 · Decay 0.60 · E/L 0 · EDiff 68 · Atk 32 · Mod 30 |
| Plate ← Chamber | **0.15** | small / short | Size 20 · Decay 0.60 · E/L 0 · EDiff 70 · Atk 8 · Mod 46 |
| Plate ← Ambient | 2.78 | defaults, E/L 0 | Size 48 · Decay 1.58 · E/L 0 · EDiff 100 · Atk 20 · Mod 79 |
| Ambient ← Room | 1.36 | defaults, E/L 50 | Size 69 · Decay 2.46 · E/L 0 · EDiff 8 · Atk 0 · Mod 8 |
| Ambient ← Hall | 1.60 | defaults, E/L 50 | Size 31 · Decay 2.32 · E/L 0 · EDiff 0 · Atk 0 · Mod 66 |
| Ambient ← Chamber | 1.24 | defaults, E/L 0 | Size 78 · Decay 2.50 · E/L 0 · EDiff 60 · Atk 78 · Mod 20 |
| Ambient ← Plate | **1.08** | defaults, E/L 0 | Size 92 · Decay 2.50 · E/L 0 · EDiff 78 · Atk 72 · Mod 22 |

Mode-owned hidden configuration was never changed during a search.

## 15. Distinctness metrics — and how far they can be trusted

### What was fixed in advance

Committed as `5073e1e`, **before any candidate was scored**:

| family | features | scale |
|---|---|---|
| buildup | t10, t50, time of peak energy, early centroid, early spread | proxies: 10–15 % with an absolute floor |
| energy profile | six windows 0–320 ms, C50, C80 | clarity at its 1 dB JND; windows at 1.5 dB |
| echo density | time to 0.5 and 0.9, density at 10 ms and 40 ms | proxies |
| stereo | L/R correlation and side/mid ratio in three windows | near the 0.075 IACC JND, widened |
| decay | RT60, centre time | their JNDs: 5 %, 10 ms |
| recurrence | autocorrelation of the detrended energy envelope | proxy |

Each family is the RMS of its features in those units; the total is the RMS
over the six families. Every feature is a ratio, a time or a correlation, so
**level cannot enter**: the same response 11.4 dB louder scores 0.000000.

Thresholds are anchored to a measured floor, not to the number 1:

| | |
|---|---|
| floor — Hall against its own sibling delay set and against itself with Size nudged | **1.02** |
| REPRODUCED at or below `max (1.5, 1.25 × floor)` | **1.50** |
| DISTINCT at or above `max (3.0, 2.0 × floor)` | **3.00** |

**The optimiser was tested on a target it can reach.** Asked to recover Hall
at deliberately moved controls starting from the defaults, it went from 8.55
to **0.55** in 242 evaluations. So a large residual is not a failed search.

### Three weaknesses of the yardstick, all found during this stage

**1. It is weakly sensitive to Size in places.** In that recovery test the
target was Size 33 % and the search stopped at Size 58 % with a residual of
0.55 — the yardstick called two different Sizes nearly the same when Attack
and the early layer were engaged. Distinct knob settings can sit in the same
low-distance basin.

**2. The positive control failed.** After the results came back uniformly
negative I added a control the pre-registered design lacked: the tank with its
mixing matrix removed, which I asserted was "a different family by any
definition". **It scored 0.25 — REPRODUCED.** A blind instrument makes
everything look reproducible, so this had to be resolved before any verdict
could stand.

An independent measurement, outside the six families — the tallest
autocorrelation of the late waveform at loop-scale lags — settles which
reading is right:

| | loop-lag texture |
|---|---|
| Hall | 0.324 |
| Hall, sibling delay set | 0.315 |
| Householder matrix | 0.191 |
| **no mixing matrix** | **0.171** |

Hall to its own sibling: 0.009. Hall to the control: **0.153**, seventeen
times larger. **The control is genuinely different and the six families
cannot see it.** The yardstick is blind to mixing-matrix structure.

I also predicted the wrong direction. I argued unmixed loops would repeat
themselves *more*; they measure *less*. The difference is real, which is all
the control needs, but my mechanism for it is not the mechanism. It is left in
the source as written, with the measurement beside it.

**3. So were the candidates hiding in the blind spot?** Each Mode against the
closest impostor the search found — which was never shown texture:

| imitated ← impostor | texture | gap | sibling gap | positive-control gap |
|---|---|---|---|---|
| Room ← Hall | 0.282 vs 0.302 | 0.020 | 0.009 | 0.153 |
| Hall ← Room | 0.324 vs 0.292 | 0.032 | 0.009 | 0.153 |
| Chamber ← Plate | 0.294 vs 0.286 | 0.008 | 0.007 | 0.153 |
| Plate ← Chamber | 0.393 vs 0.318 | **0.075** | 0.007 | 0.153 |
| Ambient ← Plate | 0.288 vs 0.261 | 0.028 | 0.009 | 0.153 |

Three of five exceed three sibling-gaps. **All five are well under the
positive control**; four are about a fifth of it or less. The impostors, which were
not trying to match texture, mostly match it anyway.

**What this does and does not establish.** It does not rescue any candidate.
It also does not close the question: a "sibling gap" measured from one pair is
an optimistic yardstick, the residual gaps are of unknown audibility, and no
search was re-run with texture in the objective. I chose not to bolt a seventh
family on and re-score — changing a pre-registered metric after seeing the
results, until the candidates pass, is exactly what pre-registration is for
preventing.

### Bias, in both directions

- A search that stops short **flatters** the Modes: residuals are upper
  bounds.
- A yardstick that cannot see a real difference **condemns** them.

The first is bounded by the recovery test. The second is bounded only
partially, by the table above. **Section 29's listening pack exists to bound
it properly.**

## 16. Size / Decay robustness

The distinction between Modes was measured at nine operating points — Size
20 / 60 / 90 % × Decay 0.6 / 2.5 / 6.0 s — and the tables in section 13 quote
the **weakest** of the nine, on the principle that a distinction which
disappears off its best setting is weak.

- **Ambient's same-knob distinction is robust**: 3.2 or more at every point
  against every Mode.
- **Room's is not**: 1.6 against Hall at its weakest point.
- **Chamber and Plate have none against each other** at any point.

Impostor residuals were measured at three targets for every pair and a fourth
for the adversarial pairs. No Mode is distinct at all of them.

## 17. Early/Late robustness

**Engaging the early layer makes the Modes more alike, not less.**

| weakest-point same-knob distance | E/L 0 | E/L 50 | E/L 90 |
|---|---|---|---|
| Room – Plate | 3.1 | **0.7** | **0.6** |
| Hall – Chamber | 1.5 | **0.7** | **0.9** |
| Room – Chamber | 2.9 | 1.6 | 1.3 |
| Chamber – Plate | 0.3 | 0.6 | 0.9 |
| Ambient – others | ≥ 3.2 | ≥ 4.5 | ≥ 5.0 |

Stage 1 expected the ER patterns to carry much of each Mode's identity. They
carry little: four different patterns, from 8 events over 80 ms to 28 over
25 ms, leave Room, Hall, Chamber and Plate within a floor-width of each other
once the layer dominates. Only Ambient's — spread over 135 ms with its first
event held off — is distinguishable, and it becomes *more* so with Early/Late.

- **Room explicitly fails at Early/Late 0** (section 4).
- **No Mode's identity depends excessively on ER.** The problem is the
  opposite: outside Ambient, the ER patterns do not confer one.
- **Plate's different ER strategy keeps the control semantically valid** and
  does the wrong thing with it (section 9).

## 18. Loudness-normalised results

Every distance in this report is level-invariant by construction, so no
comparison above is a loudness comparison.

The energy split at the defaults with Early/Late 50, each Mode on its own
analytic trim, relative to Hall:

| mode | total | 0–80 ms | 80 ms on | early share |
|---|---|---|---|---|
| Room | +0.20 dB | −1.56 | −4.58 | 66.7 % |
| Hall | 0.00 | −1.94 | −4.43 | 64.0 % |
| Chamber | +0.21 | −2.15 | −3.57 | 58.1 % |
| Plate | +0.27 | −2.10 | −3.48 | 57.9 % |
| Ambient | −0.25 | −4.31 | −2.42 | 39.3 % |

Totals agree within **0.27 dB**, so the Modes are not being told apart by
level. The early share is not matched and must not be — it is part of what a
Mode is. Note that it, too, puts four Modes within nine points of each other
and Ambient alone at 39 %.

## 19. Calibration requirements per Mode

Stage 1 assumed every Mode needs its own tables. Measured instead.

**A. One shared calibration** — Hall's wet trim and Hall's Size table applied
to every Mode. Late energy relative to Hall:

| | worst | range |
|---|---|---|
| Room | **+5.63 dB** | +2.6 … +5.6 |
| Plate | +3.98 dB | +1.6 … +4.0 |
| Chamber | +1.42 dB | +1.1 … +1.4 |
| Ambient | −1.88 dB | −1.2 … −1.9 |

**Not valid.** A shared calibration makes Room nearly 6 dB louder than Hall.

**B. The same trim *formula* given each Mode's own delay set**, with Hall's
Size table kept:

| | Decay 2.5 s and 10 s | Decay 0.5 s |
|---|---|---|
| Room | −0.29 … +0.95 dB | up to **+4.55 dB** |
| Plate | −0.39 … +1.01 dB | up to **+3.34 dB** |
| Chamber | +0.33 … +0.58 dB | up to +1.25 dB |
| Ambient | −0.28 … −0.79 dB | down to −1.71 dB |

| calibration | verdict |
|---|---|
| **wet trim** | **no table needed.** The existing analytic formula, evaluated on the Mode's delay set, does most of the work. It is a function of data the Mode already carries |
| **Size-level compensation** | **a per-Mode table is justified at short Decay only.** Within about 1 dB at 2.5 s and above; 3–4.5 dB out at 0.5 s for the short-delay Modes, at large Size |
| **BAL-4** | **follows the late energy exactly.** The early layer's energy is fixed by L2 normalisation — 1 per channel for every pattern — so K depends on the Mode only through the late energy above. Where late energy matches Hall's, Hall's K is valid; the K error is half the dB figure |

So the requirement is narrower than Stage 1 said: one analytic trim per Mode
for free, and a measured correction only in the short-Decay rows. No
production table was produced.

## 20. Room and the Phase 11 small-space risk

Phase 11's accepted characteristics are not reopened. The question was whether
Room makes them **materially worse** than Hall at the same settings, fixed in
advance as: more than 10 points more tones below −6 dB, or a worst tone more
than 3 dB deeper.

48 tones, 100 Hz – 4 kHz, late path only:

| setting | mode | median | < −6 dB | < −9 dB | worst |
|---|---|---|---|---|---|
| Size 15 %, Decay 0.3 s | Room | −2.39 | 20.8 % | 6.2 % | −14.12 |
| | Hall | −3.57 | 25.0 % | 8.3 % | −13.58 |
| Size 20 %, Decay 0.6 s | Room | −2.32 | 18.8 % | 8.3 % | **−19.31** |
| | Hall | −2.88 | 22.9 % | 10.4 % | −15.22 |
| Size 60 %, Decay 2.5 s | Room | −2.92 | 18.8 % | 4.2 % | −11.17 |
| | Hall | −2.70 | 16.7 % | 2.1 % | −9.97 |

**Mixed, and it trips the criterion on one number.** Room's *fraction* of
affected tones is lower than Hall's in the small corner, and its median is
healthier. But its single worst tone at Size 20 %, Decay 0.6 s is **4.09 dB
deeper** than Hall's, which is over the 3 dB line. On the rule as written,
that is materially worse. It rests on one tone of 48, and I would not reopen
Phase 11 on it — but the gate is recorded as failed rather than argued away.

Plate is the Mode that genuinely worsens the condition: **37.5 %** of tones
below −6 dB in both small corners, against Hall's 23–25 %.

Transient structure in the same corner:

| | NED 0.9 at | recurrence | modal prominence | shortest loop |
|---|---|---|---|---|
| Room, Size 15 % | 8 ms | 0.440 | 15.38 dB | **4.65 ms = 215 Hz** |
| Hall, Size 15 % | 16 ms | 0.450 | 11.29 dB | 8.97 ms = 111 Hz |
| Room, Size 20 % | 10 ms | 0.257 | 18.98 dB | **5.16 ms = 194 Hz** |
| Hall, Size 20 % | 16 ms | 0.326 | 14.15 dB | 9.95 ms = 100 Hz |

- **Flutter / periodicity: not worse.** Room's onset recurrence is at or below
  Hall's.
- **Density: better.** Room reaches 0.9 in half the time.
- **Ringing: worse.** Modal prominence is 4–5 dB higher.
- **Repetition pitch: worse, as Stage 1 warned.** A loop that returns faster
  than about 20 ms is heard as a pitch. Hall is already inside that region at
  these Sizes; Room's shortest loop sits near 200 Hz.

## 21. Plate architecture limitation

**The eight-line late architecture is the limitation, and the measurement
shows where.**

Fast density needs short loops. Room has the shortest loops and is therefore
the densest Mode — which makes "fast dense onset" a property of *Size*, not of
Plate. Plate's narrow spread, eight diffuser stages and second allpass section
were stacked to compensate. Section 28's leverage probe measures what each is
worth against Hall's own knobs:

| hidden change to Hall | residual after Hall's best imitation |
|---|---|
| delay spread narrowed to 1.3 | 1.00 / 0.47 — **cancelled** |
| second allpass section per line | 0.33 / 0.19 — **cancelled**, and barely different to begin with |
| eight diffuser stages | 1.77 / 1.63 — partly |

**Two of Plate's three structural levers are worth nothing**, and the third is
shared with Ambient.

So, stated plainly as the brief asked: **the current architecture cannot
produce a sufficiently independent Plate family through configuration.** A
plate's defining properties are dispersion and immediate modal density, and
neither is reachable with eight lines and a sign-vector readout. No plate
dispersion DSP was added, and no lines were added.

## 22. Ambient memory cost

Per line the tank reserves the longest delay at the largest Size, rounded up
to a power of two. Eight lines per bank, two banks.

| rate | Hall today | Ambient alone | max over Modes | increase |
|---|---|---|---|---|
| 44.1 kHz | 1024 KiB | 2048 KiB | 2048 KiB | **+100 %** |
| 48.0 kHz | 1024 KiB | 2048 KiB | 2048 KiB | **+100 %** |
| 96.0 kHz | 2048 KiB | 4096 KiB | 4096 KiB | **+100 %** |
| 192.0 kHz | 4096 KiB | 8192 KiB | 8192 KiB | **+100 %** |

| | |
|---|---|
| Hall, longest line | 82.92 ms → 165.8 ms reserved |
| Ambient candidate, longest line | 116.47 ms → 232.9 ms reserved |
| **Mode-switch temporary memory** | **none.** The second bank already exists for every Size change |

**Stage 1's concern is confirmed: this candidate doubles tank memory for every
Mode**, because capacity is global and the rounding is to a power of two.

It is avoidable. At 48 kHz Hall's buffers already round up far enough to hold
a longest line of **85.3 ms**. An Ambient set that stayed under that would
cost nothing.

**Shared maximum versus reallocation.** Reallocating on a Mode change means
allocating on a parameter change, which the audio thread may not do and the
engine has never done. One allocation at prepare, sized for the largest Mode,
is the only option consistent with the existing real-time contract.

## 23. Mode switching architecture recommendation

**Generalise the Size transaction from one number to a configuration.** The
mechanism does not change.

A research experiment — two complete rigs standing in for the two banks, on
sustained noise, the hardest case for a level change to hide in:

| transition | method | worst dip | worst rise | largest step ÷ steady |
|---|---|---|---|---|
| Hall → Room | **primed output crossfade** | 0.33 dB | 0.36 dB | 1.05× |
| | crossfade, not primed | 2.93 dB | 0.36 dB | 1.05× |
| | naive in-place delay step | 0.00 dB | **2.97 dB** | **1.52×** |
| Room → Ambient | **primed output crossfade** | 0.30 dB | 0.47 dB | 0.95× |
| | crossfade, not primed | **11.51 dB** | 0.00 dB | 0.94× |
| | naive in-place delay step | *refused: exceeds capacity* | | |
| Plate → Hall | **primed output crossfade** | 0.42 dB | 0.28 dB | 0.90× |
| | crossfade, not primed | 3.53 dB | 0.00 dB | 0.84× |
| | naive in-place delay step | 2.30 dB | 0.00 dB | 0.83× |

| requirement | primed output crossfade |
|---|---|
| click-free | largest sample step **1.05×** steady state |
| no unstable feedback transition | the two loops never touch |
| no energy burst or drop | within **0.47 dB** of the band between the two steady levels |
| no moving-delay pitch sweep | delays never move while read; only outputs are faded |
| deterministic | no clock, no detector outside the existing priming trigger |
| automation-safe | coalesced, last value wins, bounded at about 1.62 s |

Priming is doing real work: fading into an empty tank dips up to 11.5 dB. And
the core's refusal of an oversized set, rather than a clamp, is what keeps an
invalid Mode request from becoming an invalid state.

**The smallest generalisation**, unchanged from Stage 1 and now measured:
`beginMorph` must apply a whole configuration to the incoming bank only; the
setters that write both banks need a per-bank form; the ER layer needs a
run-time pattern change through its existing schedule crossfade; and the
tank's 120 ms fade, the ER's 30 ms and K's 20 ms must move on one clock.

Not implemented.

## 24. New parameter specification

Designed, **not implemented**.

| | |
|---|---|
| parameter ID | **`spaceMode`** — proposed; any ID that is not `mode` |
| display name | **Mode** |
| type | `AudioParameterChoice`, exactly five entries |
| order | Room (0), Hall (1), Chamber (2), Plate (3), Ambient (4) |
| default | **Hall** (index 1) |
| versionHint | a new `versionHintV3`. V1 and V2 are never edited |
| smoothing | none. A change starts one coalesced configuration transaction |
| state | APVTS stores the denormalised value — the index |
| old-session fallback | the property is absent, so the default loads: **Hall**, bit-identical to the engine that saved the session |
| legacy `mode` | untouched: thirteen entries, `versionHintV1`, serialised, automatable, read by no DSP |

Two things follow from the locked decisions and need saying.

**The legacy parameter's display name is also "Mode".** A host's automation
list will show two parameters called Mode. Renaming the old one's *display*
name leaves its ID and version hint untouched, but it is a visible change to a
historical parameter and is your call.

**Pin `earlyLate` first.** `appendedPid` uses `versionHintLatest`. Moving that
constant to V3 would silently re-hint `earlyLate`. The state suite pins it at
V2 and would catch it; the right order is to pin it explicitly before adding
V3.

## 25. Sixth-Mode future compatibility

Verified in the JUCE source this project builds against, not assumed.

`AudioParameterChoice` builds its range as `0 … choices.size() − 1` and
normalises as `v / end`. APVTS stores the **denormalised** value in the state
tree.

| | five entries | six entries |
|---|---|---|
| saved **state** | the index | the index — **survives** |
| saved **automation** | index ÷ 4 | index ÷ 5 — **does not survive** |

An existing automation lane replayed against a six-entry parameter:

| lane written for | stored value | reads back as |
|---|---|---|
| Room | 0.00 | index 0.00 → Room ✓ |
| Hall | 0.25 | index 1.25 → Hall ✓ |
| Chamber | 0.50 | index 2.50 → **rounding boundary** — Chamber or Plate |
| Plate | 0.75 | index 3.75 → **Ambient** ✗ |
| Ambient | 1.00 | index 5.00 → **the new Mode** ✗ |

**Appending a sixth entry is not automation-safe.** Sessions reload correctly
and automation does not, silently, for three of the five.

This is consistent with the locked decision and it defines the migration: a
sixth Mode is a **new parameter ID** with its own count, an explicit mapping
from the five-entry parameter, and the five-entry parameter left inert — the
same treatment `mode` is getting now.

## 26. Phase 13 compatibility

The boundary itself held, and that is the one thing in this stage that worked
as designed.

| check | result |
|---|---|
| band RT60 relative to 1 kHz, each Mode against Hall | worst departure **2.0 %** |
| damping used to establish any Mode | none |

**No candidate carries a tonal signature.** Mode did not consume Character's
design space, and Modern / Classic / Vintage could be applied over any of
these without redefining it.

But two overlaps appeared that are not about tone:

| Mode dimension | already owned by | measured |
|---|---|---|
| **modulation factor** | the user's Mod Depth | Hall cancels a 1.4× factor at residual **0.00** by moving Mod Depth from 30 to 46 |
| **delay-set scale** | the user's Size | residual 0.45 / 0.24 |

The modulation factor is not a Mode dimension at all: it is the Mod Depth knob
pre-turned. That matters for Phase 13, which Stage 1 said would own modulation
*style*. If Character later scales modulation depth too, three things will be
multiplying one number.

## 27. Surviving Mode candidates

**None passes the Stage 2 acceptance logic.**

| | 1 distinct | 2 not reproducible | 3 robust | 4 not loudness | 5 semantics | 6 no Character | 7 no Phase 11 regression |
|---|---|---|---|---|---|---|---|
| Room | same-knob only | **✗ Hall 0.32** | ✗ | ✓ | ✓ | ✓ | marginal ✗ |
| Hall | reference | **✗ Room 0.22** | ✗ | ✓ | ✓ | ✓ | ✓ |
| Chamber | ✗ vs Plate | **✗ Plate 0.23** | ✗ | ✓ | ✓ | ✓ | ✓ |
| Plate | ✗ vs Chamber | **✗ Chamber 0.15** | ✗ | ✓ | ✓ | ✓ | **✗** |
| Ambient | **✓** | **✗ Plate 1.08** | ✓ same-knob | ✓ | **✗** Decay | ✓ | ✓ |

Hall survives as a *product*: it is the shipping engine and the reference.
What it does not have is a second family beside it.

Control semantics, measured per Mode:

- **Size is monotone in every Mode.** Larger is later, never earlier.
- **Decay means decay in four of five.** Ambient at Decay 0.6 s measures
  0.59 s where the others measure 0.50 s — **17 % long**. Its 107 ms of
  diffuser smear is a material fraction of a 0.6 s decay.

## 28. Rejected and weak Mode candidates

| | verdict | why |
|---|---|---|
| **Room** | **REJECTED** | its delay set is Hall's, scaled. Size already does that. Fails the brief's explicit Early/Late 0 test |
| **Chamber** | **FAMILY NOT PROVEN** | indistinguishable from Plate at the same knobs; reproduced by Plate, Hall and Room |
| **Plate** | **FAMILY NOT PROVEN** | not the densest Mode; its early pattern lowers onset density; two of its three structural levers are cancelled by Hall's knobs; worsens narrow-band mono |
| **Ambient** | **WEAK — NOT PROVEN** | robustly different at the same knobs, but its identity is onset softness and the Attack control reproduces it; doubles tank memory; stretches short Decays |
| Hall | reference | unchanged |

### Why — the finding underneath the five verdicts

Each row below is Hall with **one** Mode-owned dimension pushed to an extreme,
and shipping Hall then searched to imitate it. *Exploratory: written after the
results, not pre-registered, and not used to rescue any candidate.*

| hidden change | residual, defaults | residual, small/short | what cancels it |
|---|---|---|---|
| delay set × 0.5 | 0.45 | 0.24 | **Size** |
| delay spread 1.3 | 1.00 | 0.47 | Size, Early Diffusion, Attack |
| delay spread 6.0 | 0.52 | 0.37 | Size, Early Diffusion |
| allpass sections 2 | 0.33 | 0.19 | almost nothing needed |
| modulation × 1.4 | **0.00** | **0.00** | **Mod Depth, exactly** |
| matrix Householder | 0.17 | 0.22 | *invisible to the yardstick* |
| diffuser 1 stage | 2.32 | 1.42 | Early Diffusion, partly |
| **diffuser 8 stages × 1.0** | **1.77** | **1.63** | Attack, partly |
| **diffuser 8 stages × 2.0** | **2.61** | **2.89** | Attack, partly |

> **The dimensions Stage 1 gave to Mode are, almost without exception,
> dimensions the user already owns.**

Delay scale is Size. The modulation factor is Mod Depth. Delay spread and the
second allpass section are cancelled by small moves of two or three knobs. The
ER pattern acts only where Early/Late is above zero, and does little there.
**Only diffuser stage count has leverage a knob cannot fully remove** — and
that one dimension is the whole of Ambient's partial success.

This is the same structural conclusion Phase 11 reached about stereo, arriving
from a different direction: a single eight-line FDN with a fixed topology has
few degrees of freedom that are both audible and not already exposed.

## 29. Human-render candidates

The brief asks for renders of candidates that **survive** objective rejection.
None does, so **no Mode identification pack was rendered.** Asking a listener
to tell five Modes apart, after measuring that four of them are one cluster,
would be asking a question whose answer is already known.

A different pack is prepared instead, and it tests the result rather than the
candidates.

**`renders/phase12-impostor-pairs/`** — 50 WAVs, `KEY.txt`, `LISTENING.md`.

Each pair is one source through two reverbs: a Mode candidate at the product
defaults, and a *different* Mode with its user controls moved to imitate it —
the same impostors section 14 tabulates. 100 % wet, level-matched by
integrated energy. Five pairs × five sources (vocal, snare, piano, pad,
percussion).

| pair | target | impostor | residual |
|---|---|---|---|
| pair1 | Room | Hall — Size 22, Early Diff 83, Mod 15 | 0.32 |
| pair2 | Hall | Room — Size 92, Early Diff 42, Attack 24, Mod 48 | 0.22 |
| pair3 | Chamber | Plate — Size 76, Early Diff 74, Mod 0 | 0.35 |
| pair4 | Plate | Chamber — Size 44, Early Diff 58, Attack 12, Mod 100 | 0.31 |
| pair5 | Ambient | Plate — Size 92, Early Diff 78, Attack 72, Mod 22 | 1.08 |

One question per pair:

> **Are X and Y the same reverb?**

- **If you cannot tell them apart**, the measurement was right and the
  candidates are presets.
- **If you can**, the yardstick is missing something a person hears — and
  *what you describe hearing* is where a real Mode dimension would be found.

Either answer is worth more than a five-way identification of candidates that
have already failed. pair5 is the one I would expect to be audible.

Script: `scripts/export-phase12-impostor-pairs.ps1`. Not yet listened to;
nothing in this report depends on it.

## 30. Recommended Stage 3 action

**Do not proceed to production Mode implementation.** There is nothing yet to
implement that is more than a preset.

Three steps, in this order.

**1. Listen to the impostor pairs.** One sitting. It decides whether this
report's conclusion is trusted or whether the yardstick needs a seventh family
before anything else is concluded.

**2. Then choose between three directions, which are yours to choose.**

| | direction | what it means |
|---|---|---|
| **A** | **families from topology** | Accept that configuration data cannot make families in this engine, and authorise class-C research: a structurally different late path per family where it is needed. Plate and Room most clearly require it. This reverses Stage 1's "no new DSP" constraint, with a CPU and testing cost per Mode |
| **B** | **fewer, truer Modes** | Ship the Modes the architecture can honestly support. On this evidence that is Hall plus an Ambient redesigned around the diffuser — and Ambient only if it can be separated from Attack |
| **C** | **Modes as voicings** | Decide that a Mode is a coordinated starting point rather than a family: clearly different at the same knobs (they are — 3 to 10 units), reachable from one another by an expert. The brief calls that "probably a preset". It is also what many shipping reverbs mean by the word |

My recommendation is **A for Plate and Room, B's standard of evidence for the
rest** — but C is a legitimate product position and I would rather you chose
it knowingly than arrive at it by attrition.

**3. Whatever is chosen, two findings carry forward.**

- **Do not give Mode a dimension a knob already owns.** Delay scale and the
  modulation factor should leave the Mode record entirely.
- **The diffuser is the only configuration dimension with real leverage.**
  Any class-A/B Mode work should start there.

### What I got wrong in Stage 1

Recorded because the trail is the convention, and because Stage 3 should not
inherit these.

1. **Room's design.** I specified "a short delay set" as the carrier of Room's
   late identity without noticing that a scaled delay set *is* the Size
   control.
2. **The modulation factor.** I gave Mode a multiplier on a quantity the user
   multiplies directly.
3. **Plate's early pattern.** I reasoned that many close taps are density.
   They are a busier sparse pattern, and the measurement runs the wrong way.
4. **"Every Mode needs its own tables."** Assumed. The trim is analytic.
5. **Ambient needs a new diffuser set searched.** The existing eight stages
   are already balanced.
6. **The impostor test's missing positive control.** I designed a test that
   could report "reproduced" without first showing it could report anything
   else. It was caught only because the results were suspiciously uniform.
7. **The direction of the texture effect.** Predicted more repetition in
   unmixed loops; measured less.

## 31. Tests added

One suite, `runPhase12ModeFamilyTests`, twelve sections, **31 checks: 17
passing, 14 failing on purpose.**

| section | checks |
|---|---|
| 12.2.1 research access and candidate data | 2 / 2 |
| 12.2.2 the Hall control | 5 / 5 |
| 12.2.3 the yardstick: floor, invariance, optimiser | 2 / 2 |
| 12.2.4 same-knob distance | — |
| 12.2.5 the impostor search | 0 / 6 |
| 12.2.6 energy, loudness, calibration | 0 / 2 |
| 12.2.7 control semantics per Mode | 2 / 3 |
| 12.2.8 Room in the small-space corner | 1 / 2 |
| 12.2.9 Plate density | 0 / 3 |
| 12.2.10 Ambient memory | 1 / 1 |
| 12.2.11 Mode switching | 3 / 3 |
| 12.2.12 exploratory: positive control, leverage | 1 / 2 |

### The fourteen intentional failures

They are findings, kept as gates so that a redesign has to move them.

```
Room cannot be reproduced by another Mode's user controls - IT CAN, by Hall
Hall cannot be reproduced by another Mode's user controls - IT CAN, by Room
Chamber cannot be reproduced by another Mode's user controls - IT CAN, by Plate
Plate cannot be reproduced by another Mode's user controls - IT CAN, by Chamber
Ambient cannot be reproduced by another Mode's user controls - IT CAN, by Plate
Room at Early/Late 0 is not reachable from Hall - its late geometry carries the identity
one shared calibration keeps every Mode within 1 dB of Hall's late energy
the trim formula on each Mode's own delay set, with Hall's Size table, keeps every Mode within 1 dB
every Mode's RT60 is within 10 % of Hall's at the same Decay - Decay means the same thing in each
Room does not make the accepted PHASE 11 narrow-band mono characteristic materially worse than Hall at the same settings
Plate reaches echo density 0.9 no later than every other Mode at all three settings
Plate's echo density is already at 0.7 in the window starting 10 ms in, at the defaults
Plate's dense early pattern raises onset echo density - IT LOWERS IT
the yardstick is not blind: shown a genuinely different family, the search cannot make Hall reproduce it
```

The last is a failure of my instrument rather than of a candidate, and stays
for that reason.

The impostor search is deterministic: its tables were identical across
separate runs.

## 32. Production Source changes

**None.** `git diff --stat 20362dd HEAD -- Source/` is empty.

Changed outside `Source/`:

| | |
|---|---|
| `Tests/Phase12ModeRig.h` | new — the research rig, features, distance |
| `Tests/Phase12ModeFamilies.cpp` | new — the suite and the pack exporter |
| `Tests/Phase12Listening.h` | new |
| `Tests/Suites.h`, `RunAll.cpp`, `CMakeLists.txt`, `TestExports.cpp` | registration |
| `scripts/export-phase12-impostor-pairs.ps1` | new |

## 33. Regression status

| suite | result |
|---|---|
| **full DSP regression** | **2590 / 2622** — 32 failures: the 18 preserved from Phases 7, 10 and 11, unchanged, plus the 14 of section 31. **No other new failure** |
| `AuroraVerbStateTests` | **26 / 26**, legacy **12 / 12 byte-identical** |

The eighteen earlier diagnostics reproduced with the same values in the same
order. Run undisturbed, with no build in parallel.


## 34. Commit

Pre-registration of the yardstick, before any candidate was scored: `5073e1e`

Results: `<recorded on commit>`

---

**Stage 2 ends here.** No production Mode implementation was begun. Phase 13
is not started.
