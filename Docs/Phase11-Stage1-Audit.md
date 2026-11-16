# PHASE 11 — Stage 1: stereo and spatial audit

Audit only. **No production DSP was changed and no Phase 10 constant was
touched.** Phase 10's two diagnostic gates are untouched. No Mode, Character,
Presets or GUI work was started.

**Headline: the architecture is strong and needs validation rather than
rebuilding — with one concrete defect that does need production work.** See
sections 14 and 19.

---

## 1. Exact stereo signal path, read from current Source

### Where the channels are independent

| stage | file | behaviour |
|---|---|---|
| input capture | `PluginProcessor.cpp` stage 1 | a mono host input is **copied** to both channels (`inR = inL`), so mono and dual-mono are identical from here on |
| Low Cut, High Cut | `CutFilter.h` | one filter pair per channel |
| Pre-Delay | `PreDelay.h` | two `DelayLine`s read at the **same tap index** — a pure common delay, structurally incapable of skewing the image |
| Early Reflections | `EarlyReflections.h` | two ring buffers, **different** tap times and signs per channel — the only deliberately asymmetric stage |
| Attack | `AttackInjection.h` | two rings, **identical** schedule — a scalar operator times the identity |
| input diffuser | `StereoDiffusionNetwork.h` | two fully independent chains with different delay sets; **no cross-channel path** |

### Where they interact

| stage | behaviour |
|---|---|
| **FDN injection** | `StereoFdn.h`: `injection[i] = injL[i]*l + injR[i]*r`. Both input channels drive all eight lines. **First cross-channel point.** |
| **FDN feedback** | `FdnCore`, an 8×8 orthogonal matrix, mixes every line into every other every sample. After a few loop traversals there is no per-channel state left. |
| **FDN readout** | `outL` and `outR` are each a different sign combination of all eight line states. |
| **Width** | `PluginProcessor.cpp` stage 3, mid/side: `mid = (L+R)/2`, `side = (L-R)/2 * w`. |
| Dry/Wet Mix | per channel — dry L with wet L. **Not** a cross-channel point. |

Vector geometry, verified rather than quoted: `injectionL · injectionR = 0`,
`readoutL · readoutR = 0`, and `injectionL · readoutL ≠ 0`. Both pairs are
orthogonal and the left readout is not aligned with the left injection, so
neither output is a privileged view of its own input.

## 2. Width implementation

Mid/side scaler on the **wet signal only**, after the Early/Late combine and
before Mix. Parameter range **0–200 %**.

| Width | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| 0 % | 1.0000 | 0.00 dB | −3.0 dB | **−3015 dB** | 0.00 dB |
| 25 % | 0.8806 | −0.16 dB | −3.3 dB | −15.2 dB | −0.27 dB |
| 50 % | 0.5947 | −0.26 dB | −4.0 dB | −9.9 dB | −0.98 dB |
| **100 %** | −0.0087 | −0.33 dB | −6.1 dB | −6.0 dB | −3.05 dB |
| 125 % | −0.2279 | −0.32 dB | −7.1 dB | −5.1 dB | −4.13 dB |
| 150 % | −0.3922 | −0.30 dB | −8.2 dB | −4.6 dB | −5.17 dB |
| 200 % | −0.6058 | −0.26 dB | −10.1 dB | −4.0 dB | −7.05 dB |

- **0 % produces true mono**, not a narrowed signal: side energy is −3015 dB,
  the arithmetic zero of the collapse, and the fold-down adds exactly 3.01 dB
  because the signal is already mono.
- **100 % is mathematically neutral** — reconstruction error measured at
  **1.4e-17**, which is float noise. It is an algebraic identity, not a
  calibration.
- **Above 100 % correlation goes negative**, reaching −0.61 at 200 %. The
  control doing what it says.
- **No centre movement and no gain change**: R−L rms stays inside 0.33 dB at
  every setting.
- Mono fold-down degrades monotonically with Width, as it must.

## 3. Mono input → stereo output

Centred mono sources, default settings, 100 % wet.

| source | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| impulse | −0.0062 | −0.38 dB | −6.0 dB | −6.0 dB | −3.04 dB |
| noise | −0.0087 | −0.33 dB | −6.1 dB | −6.0 dB | −3.05 dB |
| snare | −0.0067 | −0.38 dB | −6.0 dB | −6.0 dB | −3.04 dB |
| vocal | −0.0042 | −0.47 dB | −6.0 dB | −6.0 dB | −3.03 dB |
| piano | −0.0205 | −2.22 dB | −6.1 dB | −5.9 dB | −3.10 dB |
| **pad** | **−0.4562** | **−6.54 dB** | −7.9 dB | −4.7 dB | −4.89 dB |

Broadband and transient material is excellent: near-zero correlation, equal
mid and side, balance inside 0.5 dB, downmix at the decorrelated ideal.

**The pad is not.** A centred mono source producing 6.54 dB of channel
imbalance is a centre pull, and this is at Early/Late 0 — the ER layer is not
even running, so it is the late tank.

### The centre pull, chased to its mechanism

That one source cannot distinguish a systematic lean from frequency-dependent
scatter, so a 32-tone sweep was run (Mod Depth 0, late path alone, 80 Hz to
8 kHz log-spaced):

| | |
|---|---|
| mean imbalance | **−0.01 dB** |
| spread (1σ) | 6.37 dB |
| standard error | 1.13 dB |
| mean / standard error | **0.01** |
| worst single frequency | 14.51 dB |
| leaning left / right | **16 / 16** |

**It is scatter, not bias.** At any one frequency the two readouts weight
whichever lines resonate there differently, so the balance is arbitrary — but
there is no preferred side, and anything with bandwidth averages over it,
which is why the broadband rows sit inside 0.4 dB.

*A first pass with twelve tones gave a mean of +2.37 dB with an 8/4 split, which
looked like a mild rightward lean and was under 1.5σ. Declaring a bias on that
would have been the same small-sample error as choosing a constant from one
setting.*

## 4. Stereo input preservation

| input | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| left only | −0.0852 | **−1.06 dB** | −6.4 dB | −5.7 dB | −3.39 dB |
| right only | 0.0783 | **+0.42 dB** | −5.7 dB | −6.4 dB | −2.68 dB |
| correlated (centred) | −0.0087 | −0.33 dB | −6.1 dB | −6.0 dB | −3.05 dB |
| different L and R | −0.0063 | −0.46 dB | −6.0 dB | −6.0 dB | −3.04 dB |
| anti-correlated | −0.0045 | −0.37 dB | −6.0 dB | −6.0 dB | −3.03 dB |

**A source panned hard left produces a tail 0.74 dB louder on the left** (mean
of the two one-sided cases). That is not preservation of source-side
information — it is near-total collapse to the centre, and it is a concrete
finding.

It is the FDN doing what its topology says: both channels drive all eight
lines, the matrix mixes everything every sample, and the only thing left
distinguishing the outputs is the fixed readout geometry, which knows nothing
about where the source was.

The layer that *does* preserve side is the ER: it has no cross-channel path at
all. So the collapse is worst at Early/Late 0, which is the **default**.

## 5. L/R swap symmetry

| source | worst sample error | relative to peak |
|---|---|---|
| left only | 0.242664 | 133.6 % |
| different L and R | 0.372623 | 146.4 % |

**The engine is not L/R swap symmetric**, and that is structural: the
injection vectors are `(+,+,+,+,−,−,−,−)` and `(+,+,−,−,+,+,−,−)` — different
sign patterns, not mirror images — and the same is true of the readouts.
Swapping the inputs excites a different combination of lines and no output
swap undoes it.

**Whether it matters is a separate question and the answer is no.** Left-only
and right-only produce equal and opposite imbalance, and the 32-tone sweep
found a mean lean of −0.01 dB with a 16/16 split. The asymmetry is in *which*
lines each channel excites, not in *how much* either channel receives.

*The centred case was dropped from this test. For a centred input `swap(x)` is
`x`, so sample-level swap symmetry would require the output to have L = R — a
stereo reverb cannot satisfy that, and a test demanding it measures output side
energy rather than symmetry. The first version included it and read 134 %.*

## 6. Early-reflection spatial metrics

| Early/Late | corr | R−L rms | mid | side | mono fold |
|---|---|---|---|---|---|
| 25 % | −0.0103 | −0.25 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| 50 % | −0.0108 | −0.18 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| 75 % | −0.0110 | −0.09 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| 100 % | −0.0103 | 0.00 dB | −6.1 dB | −6.0 dB | −3.06 dB |
| **ER branch isolated** | −0.0103 | **0.00 dB** | −6.1 dB | −6.0 dB | −3.06 dB |

Per-event L/R time offset: **mean 0.64 ms, worst 2.60 ms**. A mean near zero
means neither channel systematically leads, which is what stops the layer
reading as a pan: a consistent lead on one side is a position, a scatter of
leads and lags is a space.

Source placement through the ER layer: a one-sided source stays one-sided (no
cross-channel path), a centred source gets two decorrelated channels of equal
energy. **Width without an arbitrary pull is exactly that pair of properties**,
and the ER layer has both.

## 7. Late reverb spatial metrics

Early/Late 0, the legacy late path alone, centred broadband:

| setting | corr | R−L rms | mono fold |
|---|---|---|---|
| Size 20 % | 0.0077 | −0.23 dB | −2.97 dB |
| Size 60 % | −0.0087 | −0.33 dB | −3.05 dB |
| Size 100 % | −0.0127 | −0.22 dB | −3.07 dB |
| Mod Depth 0 % | −0.0014 | −0.59 dB | −3.02 dB |
| Mod Depth 30 % | −0.0087 | −0.33 dB | −3.05 dB |
| Mod Depth 100 % | −0.0096 | −0.47 dB | −3.05 dB |
| Decay 0.4 s | 0.0172 | −0.51 dB | −2.94 dB |
| Decay 2.5 s | −0.0087 | −0.33 dB | −3.05 dB |
| Decay 20 s | −0.0098 | −0.19 dB | −3.05 dB |

Worst imbalance **0.59 dB**, worst departure from the decorrelated downmix
**0.07 dB**. No periodic stereo movement and **no excessive widening at the
extremes** — correlation stays near zero rather than going negative.

## 8. Early + Late spatial interaction

Worst side-energy step between adjacent Early/Late positions: **small on
snare, vocal and piano; largest on the pad**, which is the narrowband source.
The centre step stays small throughout.

That is Phase 10's known characteristic showing as side energy rather than as
total level — the same mechanism, not a new one. Per the brief it is only a
Phase 11 blocker if the spatial analysis finds something new or audibly
reproducible, and the Phase 10 sanity listening already found no audible
collapse on the pad.

## 9–11. Size, Attack and Pre-Delay interaction

At Early/Late 50 %, centred broadband. Worst L/R imbalance across all three
sweeps: **under 1.5 dB**.

- **Pre-Delay** 0 / 20 / 80 / 250 ms: spatial neutrality is **structural** —
  `PreDelay.h` reads both lines at the same tap index, so there is no
  mechanism by which it could skew an image.
- **Attack** 0 / 50 / 100 %: cannot touch the ER at all (other branch), and
  its own schedule is channel-identical, so it is a scalar operator times the
  identity and cannot rotate the image either.
- **Size** 0 / 60 / 100 %: changes ER timing through the approved α = 0.25 law
  and produces no imbalance.

## 12. Modulation as a spatial control

Measured in 250 ms windows across the tail, so movement shows as variation
between windows rather than as one averaged number.

| setting | corr mean | corr swing | side swing | balance swing |
|---|---|---|---|---|
| Mod Depth 0 % | −0.0010 | 0.0365 | 0.33 dB | 0.76 dB |
| Mod Depth 30 % | −0.0094 | 0.0431 | 0.31 dB | 0.95 dB |
| Mod Depth 100 % | −0.0107 | 0.0588 | 0.42 dB | 1.07 dB |
| Mod Rate 0.05 Hz | −0.0083 | 0.0415 | 0.28 dB | 0.84 dB |
| Mod Rate 0.5 Hz | −0.0094 | 0.0431 | 0.31 dB | 0.95 dB |
| Mod Rate 10 Hz | −0.0103 | 0.0397 | 0.35 dB | 0.88 dB |

**No subjective claim is made.** Whether this is desirable motion or excessive
wandering is not a question these numbers answer, and calling the result
"lush" on the strength of a correlation swing would be the kind of unearned
claim Phases 9 and 10 kept out.

**What the numbers do say: the centre does not wander.** Balance swing stays
near 1 dB at every depth and rate, so whatever movement modulation adds is in
the decorrelation, not in the placement. An image that walked would show as a
balance swing.

## 13. Mono compatibility

| setting | source | corr | mono fold | vs −3.01 |
|---|---|---|---|---|
| default | noise | −0.0087 | −3.05 dB | −0.04 dB |
| default | pad | −0.4562 | −4.89 dB | −1.88 dB |
| small room | noise | 0.0157 | −2.94 dB | +0.07 dB |
| **small room** | **pad** | **−0.9705** | **−14.40 dB** | **−11.39 dB** |
| large hall | noise | −0.0204 | −3.10 dB | −0.09 dB |
| large hall | pad | 0.9198 | −0.25 dB | +2.76 dB |
| high modulation | noise | −0.0096 | −3.05 dB | −0.04 dB |
| high modulation | pad | −0.3166 | −4.15 dB | −1.14 dB |
| Width 200 % | noise | −0.6058 | −7.05 dB | −4.04 dB |
| Width 200 % | pad | −0.8662 | −9.70 dB | −6.69 dB |
| early dominant | pad | −0.5421 | −4.73 dB | −1.72 dB |
| late dominant | pad | −0.4562 | −4.89 dB | −1.88 dB |

**Broadband material is excellent at every setting** — inside 0.1 dB of the
decorrelated ideal except at Width 200 %, where the control is explicitly
widening.

**Narrowband material at small, short settings is not.** That row is the
defect.

## 14. Worst negative-correlation case — THE DEFECT

| | |
|---|---|
| **minimum observed correlation** | **−0.9848** |
| settings | Size 15 %, Decay 0.3 s |
| source | sustained 220 Hz pad, centred mono |
| mono fold-down | −8.30 dB at that point; **−15.40 dB** at Size 15 % / Decay 0.6 s |
| **transient or sustained** | **sustained — 12 of 12 windows below −0.5** |

Windows across the tail at Size 15 % / Decay 0.6 s:

| window | corr | mono fold |
|---|---|---|
| 0–250 ms | −0.9781 | −10.91 dB |
| 500–750 ms | −0.9678 | −13.95 dB |
| 1000–1250 ms | −0.9870 | −13.80 dB |
| 1500–1750 ms | −0.9921 | −14.46 dB |
| 2000–2250 ms | −0.9813 | −15.40 dB |
| 2500–2750 ms | −0.9545 | −14.92 dB |

**Mechanism, identified rather than clamped.** At Size 15 % the delay lines
are short and the modal density at 220 Hz is low, so only a few lines carry
the energy. The two readouts are different sign combinations of those same few
lines — `readoutLeft` is `(−,−,−,+,+,+,+,−)` and `readoutRight` is
`(+,−,−,+,−,−,+,+)` — and when the surviving energy sits on lines where the two
vectors disagree, the outputs come out close to anti-phase. Nothing is wrong
with the vectors; this is what sparse modal content through fixed sign
readouts does.

**Is this Phase 10's known characteristic?** No. Phase 10 recorded a level
traversal against Early/Late. This is at Early/Late **0** — the late tank
alone, no ER — and it is about **mono compatibility**, which Phase 10 did not
measure at these settings. It is a **new** finding.

**No corrective DSP was added**, per the brief. The brief also says not simply
to clamp correlation, and the mechanism above is why that would be the wrong
move: the number is a symptom of sparse modal content, not of a runaway.

## 15. Sample-rate consistency

| rate | ER L/R offset, mean | worst |
|---|---|---|
| 44.1 kHz | 0.643 ms | 2.630 ms |
| 48.0 kHz | 0.641 ms | 2.604 ms |
| 96.0 kHz | 0.639 ms | 2.604 ms |
| 192.0 kHz | 0.643 ms | 2.615 ms |

The ER layer's spatial geometry is specified in milliseconds, so it is the
same field at every rate — offsets agree to a few microseconds. Phase 10
measured absolute times at 18.71 µs; this is the same property seen as a
stereo one.

## 16. Block-size consistency

From the state suite, relative to each configuration's own peak: **identical
at 128 samples and above**; at 32 and 64 samples both the Early/Late 0 and
Early/Late 50 columns move together (0.0071 vs 0.0105, and 0.0011 vs 0.0014).
The dependence belongs to the tank's control-rate updates and **no new spatial
defect is introduced**, which is the condition the brief sets.

## 17. CPU and memory

300 blocks of 512 samples, best of 5. Measurement only; nothing optimised.

| stage | time | % of real time |
|---|---|---|
| late stereo engine (tank) | 41.95 ms | 1.311 % |
| the same tank, Mod Depth 0 | 22.46 ms | 0.702 % |
| ER stereo layer | 1.69 ms | 0.053 % |
| Width mid/side | 0.07 ms | **0.002 %** |

**Modulation is 46.5 % of the tank's cost** — the largest single
stereo-related item, and worth knowing before Phase 12 adds Modes that may
want to vary it.

Memory: the stereo-specific storage is the ER layer's two ring buffers
(128 KiB at 48 kHz), the pre-delay's two lines, and the two independent
diffuser chains. **The tank's eight lines are shared by both channels by
construction** — there is no per-channel tank — so stereo costs nothing there.

## 18. Mode-readiness audit

### Already data — a Mode could vary these today

| attribute | reachable through |
|---|---|
| ER pattern | `er::Pattern`; `EarlyReflections::prepare` already takes one. Phase 10's engine/data split holds. |
| injection vectors | `StereoFdn::setStereoVectors`, a public runtime call — asserted by test |
| readout vectors | the same call |
| diffuser patterns | `StereoDiffusionNetwork::setDelayPatterns`, which replaces both chains and refuses as a whole |
| spatial scale | Pattern data for the ER, a mapping for the tank |

### Not data — would need code

| attribute | why |
|---|---|
| cross-channel coupling | there is no coupling parameter. The coupling *is* the 8×8 feedback matrix plus the injection/readout geometry. A Mode can change the stereo **view** via the vectors; the mixing itself is not parameterised. |
| per-channel tank | there is one shared eight-line tank. Genuinely separate left and right tanks would be new DSP. |
| ER cross-feed | the ER layer has no cross-channel path at all. |

**Verdict: Mode-ready for everything a Mode plausibly wants.** The one thing
that is not data is the degree of cross-channel mixing — and that is also
where this audit found its defect. **Those two facts are related and should be
weighed together.**

## 19. Concrete defects

### Defect 1 — severe sustained anti-correlation on narrowband material at small, short settings

**NEW, not Phase 10's characteristic.** Minimum correlation −0.9848, mono
fold-down to −15.40 dB, sustained for the whole note. At Size 15 % / Decay
0.3–0.6 s a sustained tone's reverb **almost completely disappears in a mono
downmix**.

Severity: a listener working in mono, or on a club system, loses the reverb
entirely on sustained material in a small-room setting. Broadband material is
unaffected.

### Defect 2 — the late field discards source-side information

A hard-panned source produces a tail **0.74 dB** off centre. Not a
catastrophe, and arguably correct for a diffuse late field, but it means that
at the **default** Early/Late 0 the reverb places everything centrally
regardless of where the dry source sits. Premium stereo reverbs generally
retain more.

Mitigated already by raising Early/Late: the ER layer has no cross-channel
path and preserves side fully.

### Not defects, confirmed

Width (exact at 100 %, true mono at 0 %), mono→stereo on broadband and
transient material, the late path's decorrelation and stability, the ER
layer's temporal decorrelation without pull, Pre-Delay/Attack/Size spatial
neutrality, modulation not moving the centre, sample-rate consistency, and the
fact that the stereo field is architectural rather than a final widening
trick.

## 20. Recommended Phase-11 production changes

**Not implemented. For review.**

### Priority 1 — Defect 1

Three candidate directions, in increasing invasiveness:

1. **Readout vector selection.** The anti-phase arises when surviving energy
   sits on lines where the two readout sign vectors disagree. A vector pair
   chosen to keep the *worst-case* sub-vector correlation bounded, rather than
   only being orthogonal overall, may remove most of it. Cheapest, no new DSP,
   and the vectors are already runtime-settable.
2. **Size-dependent modal density.** The effect needs sparse modal content; it
   vanishes at larger Size. Whether the smallest Sizes should use a different
   delay set is a Phase 12 Mode question as much as a Phase 11 one.
3. **A decorrelating element between the readouts.** Most invasive, and would
   need its own neutrality gate against Phase 10.

**Recommendation: investigate (1) first**, because it is data rather than code
and because a measured search over sign-vector pairs is cheap.

### Priority 2 — Defect 2

A product decision before an engineering one: how much source position *should*
a late field retain? If more is wanted, injection-vector geometry is the lever
and it is already data.

### Explicitly not recommended

- Clamping or correcting correlation. The brief rules it out and the mechanism
  shows why it would be papering over sparse modal content.
- Any change to Phase 10's accepted constants. Nothing here is evidence
  against them; Defect 1 is at Early/Late 0, where the ER layer does not run.
- Widening at neutral Width. Section 10 shows the field is already
  architectural.

## 21. Tests added

`Tests/Phase11StereoAudit.cpp`, 16 sections, **31 checks**, registered in
`RunAll.cpp`. Four fail deliberately and record the findings above:

```
a one-sided source produces a one-sided tail - IT DOES NOT (Defect 2)
a centred mono source produces no material L/R energy imbalance (narrowband)
no setting produces severe downmix cancellation (Defect 1)
no setting produces near-perfect anti-correlation - IT DOES (Defect 1)
```

Shared metric block: correlation, L/R rms and peak difference, mid and side
energy, stereo balance, mono fold-down — with the fold-down compared against
**−3.01 dB**, the decorrelated expectation, rather than against zero.

### Five test errors of my own, found and corrected

Recorded because the project's convention is to keep the trail:

1. Mono fold-down compared against 0 dB instead of −3.01 dB, so the expected
   answer failed.
2. Swap symmetry tested on a centred source, which is degenerate — it measured
   output side energy and read 134 %.
3. The centre-pull conclusion drawn from twelve tones, where the mean was
   under 1.5σ. Widened to 32; the apparent +2.37 dB lean became −0.01 dB.
4. A sample-rate reference read before the loop had reached it, so the first
   comparison was against zero.
5. A table header printed above a table with different columns.

## 22. Regression result

See the stage log. No production code changed, so no audio changed and no
existing gate moved.

## 23. Commit

`<recorded on commit>`

---

## The product question

**Does ACTIVE Reverb already have a sufficiently strong stereo architecture for
V1?**

**Largely yes — and one thing needs fixing.**

The architecture is genuine rather than cosmetic: two independent diffuser
chains, two orthogonal injection directions, two orthogonal readouts, a
per-channel ER schedule, and a Width control that adjusts a field already
there rather than manufacturing one. Width is exact at neutral and honest at
its endpoints. Broadband and transient behaviour is excellent everywhere
measured. Nothing moves the centre that should not. The spatial attributes a
Phase 12 Mode would want to vary are already data.

**But Defect 1 is real, new, severe and narrow.** A sustained tone in a small
short room collapses almost completely in mono. That is not polish; it is a
concrete architectural weakness with an identified mechanism, and it warrants
Phase 11 production work.

**Defect 2 is a product decision, not a fault.**

No work is invented beyond that. Sections 2, 6, 7, 9–13 and 15–18 found
nothing requiring change, and they say so.
