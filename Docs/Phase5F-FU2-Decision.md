# Phase 5F — FU-2 decision report: preview / wet gain staging

Measured by `Tests/WetGainStaging.*`, reported by the suites `FU-2 / …`.
**1974 / 1974 checks pass** (1816 before the sustained tone scan in the addendum was
added). Nothing was changed: `previewBaseTrim` is still 0.25, the Size compensation,
the mix law and the DSP are untouched, and a suite asserts that.

48 kHz. Decay 2.5 s, Size 60%, Width 100%, Mod Depth 30%, Mod Rate 0.5 Hz.
Four sources, each rendered through the wet path **once**, with every mix point taken
from that one render — so alignment and tank state are identical across mix points by
construction rather than by argument.

---

## The instrument had to be checked first

Two things were caught by checks that existed to catch them, and both would have
produced confident wrong numbers:

- **The compensation gain read 0.00 dB.** `getCompensationGain(0)` returns bank 0, but
  setting Size starts a morph and the morph swaps banks — so bank 0 held the gain for
  the size the tank opened at, and 1.0 is exactly the value that looks correct. Caught
  by requiring the accessor to agree with the ratio recovered from the two renders.
  The figure is now confirmed three independent ways: accessor, measured ratio, and
  `sizeComp::correctionDb`. All three read **-0.6380 dB**.
- **The candidate ceiling was initially derived from raw peak**, which is the wrong
  quantity: each case receives the base trim scaled by its own decay-aware factor, so
  the bound is on `rawTruePeak × decayFactor`. At 60 s decay those pick different
  binding cases.

---

## 1. Dry to raw tank, per source

**This is not one number, and it is not the -5.22 dB the old records suggested.**

| source | dry LUFS | raw tank LUFS | transfer |
|---|---|---|---|
| mixed | -13.72 | -18.94 | **-5.22** |
| pad | -13.26 | -19.52 | **-6.26** |
| snare | -22.44 | -23.91 | **-1.48** |
| piano | -20.78 | -24.64 | **-3.86** |

A spread of **4.78 dB** across four sources through an identical tank. The -5.22 dB on
record was the "mixed" source alone, and treating it as a constant tank loss would have
been wrong by up to 3.7 dB.

The reason is the one the plan predicted: the tank redistributes energy in time and in
frequency, and gated LUFS weights both. A snare loses least (-1.48 dB) because its dry
signal is sparse and peaky — gating discards much of the dry signal's silence while the
reverb fills it. A sustained pad loses most (-6.26 dB) because there is nothing for the
tail to fill in. **Any figure quoted for "the tank's loss" is a property of the source
as much as of the tank.**

## 2. Size compensation

**-0.6380 dB**, exactly, at the default Size. Signal-independent — confirmed by
measuring it on all four sources and requiring agreement to 1e-12.

## 3. Preview trim

**-12.5788 dB.** `previewBaseTrim` 0.25 is -12.0412 dB; the decay-aware factor at
Decay 2.5 s / Size 60% contributes the remaining -0.5376 dB.

The LUFS-measured step reads -12.58 dB for mixed, pad and snare but **-12.48 dB for
piano**. The trim is one scalar and cannot differ between sources — the 0.10 dB is the
BS.1770 relative gate moving as the whole signal drops past it. It is a real property
of gated loudness measurement, not a real property of the gain, and it is exactly the
kind of thing that would be misread as a fault if only the LUFS column were consulted.

## 4. Width at 100%

**0.00 dB.** In exact arithmetic the stage is an identity: `mid + side = (L+R)/2 +
(L-R)/2 = L`.

Measured rather than assumed, and the measurement disagrees with "bit-exact":
the largest `|afterWidth - afterTrim|` is **6.9e-18** (3.5e-18 for pad). The two halves
do not always reassociate exactly for a given pair of doubles. The loudness difference
is 7.1e-15 dB.

So: **unity to within one part in 10^17, and not bit-exact.** Below any conceivable
audibility, but the plan asked not to claim bit-exactness without testing, and the test
says it is not.

## 5. Final wet, dry to Mix 100%

| source | dry | Mix 100% | delta |
|---|---|---|---|
| mixed | -13.72 | -32.16 | **-18.44** |
| pad | -13.26 | -32.73 | **-19.47** |
| snare | -22.44 | -37.13 | **-14.69** |
| piano | -20.78 | -37.76 | **-16.97** |

Consistent in sign and order of magnitude, spread **4.78 dB** — the same spread as the
tank transfer, because everything downstream of the tank is source-independent.

### This contradicts the host measurement, and the offline figure is the reliable one

The mix-law regression measured block RMS in FL Studio and found Mix 100% only 3.17 dB
below Mix 50%. Offline, at Mix 50% the mixed source reads -16.85 LUFS and at 100%
-32.16 — a drop of **15.31 LU**, nearly five times larger.

The host figure was flagged as unreliable when it was taken, for the reason that now
shows: the windows were not comparable. The offline construction removes that. It also
agrees far better with what was actually heard — "belirgin şekilde düşük" describes a
15 dB drop, not a 3 dB one.

## 6. Which case sets the hard ceiling

Pre-trim, per case. `effective` is `rawTruePeak × decayFactor`, which is what a base
trim is bounded by.

| case | kind | warm-up | window | raw peak | raw true peak | decay f | effective | 1/true peak |
|---|---|---|---|---|---|---|---|---|
| program material, default | musical | 0 s | 10 s | 0.2887 | 0.3167 | 0.9400 | 0.2977 | 3.157 |
| full-scale impulse | musical | 0 s | 20 s | 0.0364 | 0.0375 | 0.9400 | 0.0352 | 26.679 |
| full-scale impulse, 60 s decay | musical | 0 s | 20 s | 0.0413 | 0.0450 | 0.2014 | 0.0091 | 22.213 |
| sustained noise, default decay | stress | 5 s | 15 s | 2.6001 | 2.6265 | 0.9400 | 2.4689 | 0.381 |
| sustained noise, 20 s decay | stress | 5 s | 15 s | 7.4955 | 7.6290 | 0.3474 | 2.6501 | 0.131 |
| sustained noise, smallest Size | stress | 5 s | 15 s | 8.8579 | 8.8952 | 0.1870 | 1.6635 | 0.112 |
| **sustained noise, largest Size** | stress | 5 s | 15 s | 7.8341 | 8.0020 | 0.5222 | **4.1785** | 0.125 |
| full-scale DC | stress | 5 s | 15 s | 3.6163 | 3.6163 | 0.9400 | 3.3993 | 0.277 |

Noise uses a fixed seed, so it is the same signal every run. Sustained cases get 5 s of
warm-up which is discarded; impulses get none, because discarding the start would
discard the peak.

**Binding stress case: sustained full-scale noise at the largest Size, 20 s decay.**
Effective true peak 4.1785, so the stress ceiling is **0.2393**.

**Binding musical case: program material at the default.** Effective 0.2977, so the
musical ceiling is **3.3591**.

## 7. Permitted maximum base trim, at each margin

| margin | musical ceiling | vs current | stress ceiling | vs current |
|---|---|---|---|---|
| 0 dB | 3.3591 | +22.57 dB | 0.2393 | **-0.38 dB** |
| 1 dB | 2.9935 | +21.57 dB | 0.2132 | -1.38 dB |
| 3 dB | 2.3778 | +19.56 dB | 0.1694 | -3.38 dB |

## 8. What safety margin 0.25 actually has

| | worst post-trim peak | margin |
|---|---|---|
| musical cases | 0.0744 | **+22.57 dB** |
| stress cases | 1.0446 | **-0.38 dB** |

**The shipping trim already exceeds full scale on a stress case** — sustained full-scale
noise at the largest Size reaches 1.0227 post-trim, 0.20 dB over.

That is not a regression; it is what the value was chosen for. The existing
"Preview / gain safety" suite requires clip-freedom for the impulse cases and for
sustained noise at the *default* decay (which has 4.28 dB of headroom). It never
required it at every Size. So the note in `ParameterMapping.h` — "the worst sustained
full-scale case measured leaves this much headroom needed" — is slightly more confident
than the measurements support: 0.25 covers the worst case *at the default decay*, not
the worst case.

## 9. How conservative it is on normal music

**22.57 dB.**

Program material at the default peaks at 0.0744 after the trim. The trim could be raised
by more than twenty decibels before that material reached full scale.

## 10. Is a fixed trim enough?

**No — and the measurement says why, in one number.**

The musical ceiling is 3.3591 and the stress ceiling is 0.2393. They are **22.95 dB
apart**. No single value can be near both. Today's value sits at the stress end and
pays the full 22.57 dB on real material.

This is the same shape of problem the decay-aware factor was introduced to solve on a
different axis. There, the span between decay settings was about 18 dB and a fixed trim
could not cover it, so the trim was made a function of decay. Here the span is across
*input level and Size*, and the trim is not a function of either.

## 11. Is adaptive gain necessary?

**No, and it should not be added.**

Three reasons, in order of weight:

1. **The problem is static, not dynamic.** What was measured is a fixed staging offset:
   one scalar, 22.6 dB more conservative than real material needs. A signal-following
   gain would solve a problem that is not the one present.
2. **It would make every future measurement describe the follower.** The existing
   comment in `ParameterMapping.h` makes this point and it is correct: a trim that
   depends only on parameters is exactly reversible and cannot hide a peak. A limiter
   or auto-gain means that from then on, RT60, modal behaviour and every level figure
   are measurements of the control loop as much as of the reverb.
3. **It is not what the complaint asks for.** The user heard 100% wet as too quiet at a
   fixed setting. That is answered by choosing a better fixed number.

What is genuinely needed is a decision about **where on the 23 dB span to sit** — and
that is a judgement about which inputs the plugin owes protection to, not a DSP problem.

## 12. Recommended value

### Recommendation: `previewBaseTrim` = **1.0** (remove the fixed attenuation, keep the decay-aware factor)

Not a round number chosen for tidiness: 1.0 is the point at which the base trim stops
attenuating and the trim becomes purely the decay normaliser it was designed to be.
Everything the decay-aware factor does is preserved.

| | at 0.25 (now) | at 1.0 | change |
|---|---|---|---|
| musical worst peak | 0.0744 | 0.2977 | margin 22.57 → **10.52 dB** |
| stress worst peak | 1.0446 | 4.1785 | margin -0.38 → **-12.42 dB** |
| mixed, Mix 100% vs dry | -18.4 LU | **-6.4 LU** | |
| pad, Mix 100% vs dry | -19.5 LU | **-7.4 LU** | |
| snare, Mix 100% vs dry | -14.7 LU | **-2.6 LU** | |
| piano, Mix 100% vs dry | -17.0 LU | **-4.9 LU** | |

At 100% wet the reverb would land 2.6 to 7.4 LU below the dry signal, which is an
ordinary range for a reverb at full wet — energy spread over time is quieter than the
same energy concentrated, and that is the reverb working rather than a staging error.

### Headroom margin: 10.52 dB on musical material

### What happens at Mix 30% (the default)

**Almost nothing — 0.13 to 0.39 dB.**

Modelled by adding the dry and wet branches as incoherent power, using the measured dry
and 100%-wet loudness of each source and the Aurora coefficients at Mix 30%
(dry 0.891007, wet 0.381860). The model reproduces the *measured* Mix 30% loudness to
within 0.15 dB on all four sources, which is what makes it usable here:

| source | modelled 30% | measured 30% | error | at +4.19 dB | at +12.04 dB | at +19.56 dB |
|---|---|---|---|---|---|---|
| mixed | -14.71 | -14.83 | 0.12 | +0.02 | **+0.17** | +0.91 |
| pad | -14.25 | -14.10 | -0.15 | +0.01 | **+0.13** | +0.74 |
| snare | -23.42 | -23.41 | -0.01 | +0.04 | **+0.39** | +1.91 |
| piano | -21.77 | -21.76 | -0.01 | +0.03 | **+0.23** | +1.23 |

The reason the change is so small is the reason the complaint was about 100% and not
about 30%: at the default the wet path is 15 to 19 LU below the dry, so quadrupling its
amplitude still leaves it far below the dry branch it is added to. **Raising the trim by
12 dB moves the default preset by less than half a decibel.**

This is a strong argument for the change rather than against it. The setting the plugin
opens at barely moves, so nobody's existing balance is disturbed, while the region the
complaint came from is fixed. It also means the audible difference at 30% will be *more
reverb*, not *more level* — which is what should be listened for in the blind set.

The model is an approximation (LUFS is K-weighted and gated, and dry and wet are not
perfectly uncorrelated). It agrees with measurement to 0.15 dB here, but the 30% point
should still be rendered directly for each candidate rather than taken from this table.

### The risk, stated plainly

At 1.0, sustained full-scale noise at the largest Size reaches **4.18** — 12.4 dB above
full scale.

Two things about that. First, it is a number, not a defect: a VST3 float bus carries
values above 1.0 without clipping, and FL Studio's mixer is float throughout. Clipping
happens at a converter or at a stage that clamps, and the user's fader is between the
two. Second, it is a change from today, where the same case reaches 1.02. A user who
sends a sustained full-scale signal into a max-Size 20-second reverb at 100% wet will
see their meters go red, and if they do not lower the fader, their interface will clip.

### A gap in this evidence, stated before it is used

The "musical" set has **one** program signal, peaking at -4.50 dBFS. It does not include
a full-scale sustained tone at maximum Size and long decay, which is a far more
plausible real input than white noise and would sit somewhere between the two ceilings.

The recommendation of 1.0 does not depend on closing that gap — it is bounded by the
stress side, not the musical side. **Any value above 1.0 would depend on it**, and
should not be chosen until that case is measured.

### Conservative alternative

If the stress behaviour is judged unacceptable: **0.4050** (+4.19 dB), the ceiling at
which sustained noise at the *default* decay reaches exactly full scale. It preserves
every guarantee the current suite actually asserts. It leaves Mix 100% between 10.5 and
15.3 LU below dry — an improvement, but not enough to answer the complaint.

## 13. Blind A/B candidates

Four measured anchors, no invented values:

| file | trim | vs now | what it is |
|---|---|---|---|
| A | 0.2500 | 0.00 dB | current — the fixed point |
| B | 0.4050 | +4.19 dB | sustained noise at default decay reaches full scale |
| C | 1.0000 | +12.04 dB | no fixed attenuation — **recommended** |
| D | 2.3778 | +19.56 dB | musical ceiling with 3 dB margin |

Rendered at **Mix 30%** and at **Mix 100%**, for all four sources: 30% is what the
plugin opens at and 100% is where the complaint was heard, and a candidate that fixes
one and spoils the other has to be visible. Same dry and wet render per source, order
shuffled per source, key in a separate file.

---

## Stop

No gain changed. No limiter, clipper, saturator or auto-gain added. No topology change.
No VST3 installed. Phase 6 not entered.

Awaiting a decision on the recommended value before the blind set is rendered.

---

# Addendum — sustained tone stress scan

Run after the decision report, to close the gap it named: the musical case set
had no full-scale sustained tone at maximum Size and long decay.

Full-scale sine, third-octave centres 40 Hz to 12.5 kHz (26 points), Size 100%,
three decays. Warm-up is five fill time constants of the corrected RT60 and is
discarded; two equal 10 s blocks are then measured and the peak taken over both.

Full suite: **1974 / 1974 checks pass**, including 158 in this scan.

## The convergence criterion took four attempts, and three of them were the criterion

Recorded because the failures were not measurement failures:

| attempt | criterion | why it failed |
|---|---|---|
| 1 | halves of one window agree, `\|settle\|` small | impossible — the modulation beats against the excitation, so the envelope never flattens |
| 2 | same, one-sided | still failed, and got **worse** when warm-up went 3 → 5 → 8 time constants; that is the signature of sampling a cycle, not watching a climb |
| 3 | two equal blocks, every point converged | too strict — it failed a whole row because a point 20 dB below the worst wobbled |
| 4 | the binding point converged, and no other point can overtake it | passes |

The fourth is narrower and is the one that matches what the figure is for: the
headroom number comes from one point, so that point must have settled and nothing
else may be able to pass it. Everything else is free to wobble, because nothing
reads it.

## Worst case per decay

| decay | worst frequency | raw peak | raw true peak | effective | settle |
|---|---|---|---|---|---|
| 2.5 s | 2031.9 Hz | 2.3683 | 2.3688 | 3.1669 | -0.07 dB |
| 20 s | 2031.9 Hz | 8.1591 | 8.1594 | 4.2607 | +0.08 dB |
| **60 s** | **254.0 Hz** | **20.9445** | **20.9503** | **6.3782** | **-0.05 dB** |

Every binding point settled to within 0.12 dB, and in each row no other frequency
could overtake it even given one more block of its own observed growth.

## Tone against noise

| | effective true peak | ceiling | vs current 0.25 |
|---|---|---|---|
| worst noise stress (largest Size, 20 s) | 4.1785 | 0.2393 | -0.38 dB |
| **worst sustained tone (254 Hz, 60 s, Size 100%)** | **6.3782** | **0.1568** | **-4.05 dB** |

**Sustained tone is 3.67 dB worse than noise.** Noise was not a bound on tone, and
the scan was worth running.

## What this does to the 1.0 recommendation

It does not overturn it, and it does not leave it where it was either.

| candidate | trim | vs now | tone peak at 60 s | margin |
|---|---|---|---|---|
| A | 0.2500 | +0.00 dB | 1.5945 | **-4.05 dB** |
| B | 0.4050 | +4.19 dB | 2.5832 | -8.24 dB |
| C | 0.6745 | +8.62 dB | 4.3021 | -12.67 dB |
| D | 1.0000 | +12.04 dB | 6.3782 | -16.09 dB |

The load-bearing fact is the first row. **At the shipping trim of 0.25, this case
already reaches 1.59 — 4.05 dB above full scale.** The current build does not
protect against it either.

So the tone scan does not distinguish the candidates the way it might have. It does
not show that 1.0 crosses a line the current value stays behind; it shows the line
was already crossed, and that every candidate including the current one is on the
same side of it. What changes between A and D is how far over, not whether.

That weakens one argument and strengthens another:

- **Weakened:** "0.25 is the safe value" is not supportable. Against the worst
  sustained tone it is 4.05 dB over, and the 22.57 dB it costs on real material
  does not buy protection from this case.
- **Strengthened:** if no fixed trim in the plausible range prevents a sustained
  full-scale tone at maximum Size and 60 s decay from exceeding full scale, then
  choosing the trim by that case means paying a large, permanent cost for
  protection that is not achieved. The value that would achieve it is 0.1568 —
  **quieter than today**, 4 dB further into the complaint this stage exists to fix.

**The recommendation stands at 1.0**, with its justification restated: not "it is
safe against every input", which is false for 0.25 as well, but "the cases it does
not protect against are cases nothing in the plausible range protects against, and
it stops paying 22.6 dB on the cases that matter."

The residual risk is now quantified rather than estimated. At 1.0 the worst
sustained tone reaches 16.09 dBFS, against 4.05 dBFS today — a 12 dB increase in an
overshoot that already exists. This is a float bus, so the value is carried rather
than clipped, but a user driving a max-Size 60 s reverb at 100% wet with a
sustained full-scale tone will see it at their converter.

If that is judged unacceptable, the honest response is not a smaller trim — it is
a decision to bound the wet path with something that reacts to the signal, which
FU-2 recommends against for the reasons in section 11, or to accept that this input
is outside what the plugin protects.

---

# Blind listening result and final recommendation

Key opened only after the operator submitted all eight rankings.

## 1. The mapping

| group | A | B | C | D |
|---|---|---|---|---|
| mixed Mix 30% | 0.405 | 0.674499 | 0.25 | 1.0 |
| mixed Mix 100% | 1.0 | 0.25 | 0.674499 | 0.405 |
| pad Mix 30% | 0.25 | 1.0 | 0.405 | 0.674499 |
| pad Mix 100% | 0.674499 | 0.405 | 1.0 | 0.25 |
| snare Mix 30% | 0.674499 | 1.0 | 0.25 | 0.405 |
| snare Mix 100% | 0.405 | 0.25 | 1.0 | 0.674499 |
| piano Mix 30% | 1.0 | 0.405 | 0.674499 | 0.25 |
| piano Mix 100% | 0.25 | 0.674499 | 0.405 | 1.0 |

## 2. The rankings, decoded

| group | as heard | as trims |
|---|---|---|
| mixed 100% | A > C > D > B | **1.0 > 0.674 > 0.405 > 0.25** |
| pad 100% | C > A > D > B | **1.0 > 0.674 > 0.25 > 0.405** |
| snare 100% | D > C > A > B | **0.674 > 1.0 > 0.405 > 0.25** |
| piano 100% | D > B > C > A | **1.0 > 0.674 > 0.405 > 0.25** |
| mixed 30% | D > A > C > B | **1.0 > 0.405 > 0.25 > 0.674** |
| pad 30% | B > D > A > C | **1.0 > 0.674 > 0.25 > 0.405** |
| snare 30% | B > A > D > C | **1.0 > 0.674 > 0.405 > 0.25** |
| piano 30% | A > D > B > C | **1.0 > 0.25 > 0.405 > 0.674** |

## 3. Mix 100% tally

| trim | 1st | 2nd | 3rd | 4th | Borda |
|---|---|---|---|---|---|
| 0.25 | 0 | 0 | 1 | 3 | 5 |
| 0.405 | 0 | 0 | 3 | 1 | 7 |
| 0.674499 | 1 | 3 | 0 | 0 | 13 |
| **1.0** | **3** | **1** | 0 | 0 | **15** |

## 4. Mix 30% tally

| trim | 1st | 2nd | 3rd | 4th | Borda |
|---|---|---|---|---|---|
| 0.25 | 0 | 1 | 2 | 1 | 8 |
| 0.405 | 0 | 1 | 2 | 1 | 8 |
| 0.674499 | 0 | 2 | 0 | 2 | 8 |
| **1.0** | **4** | 0 | 0 | 0 | **16** |

**1.0 first in all four, and the other three tie exactly at 8.** Below 1.0 the ordering
carries no information: the listener could not separate 0.25, 0.405 and 0.674 at the
default mix, and ranked them differently every time.

## 5. Did any trim break the balance at Mix 30%?

**No — including the largest change.** 1.0 was first in all four groups and never
placed below first. Last place went to 0.674 twice, 0.405 once and 0.25 once, which is
the pattern of guessing rather than of rejection.

Measured, the same conclusion. Across all four sources at Mix 30% the total loudness
moves by at most **0.51 dB** between the extreme candidates, and **no file anywhere in
the set has a single over-unity sample**.

The one figure that looks wrong is real and worth keeping: on snare at Mix 30%, trim
1.0 measures **-23.92 LUFS against 0.25's -23.41** — the preferred candidate is the
*quietest* of the four by half a decibel. Adding diffuse tail to a sparse transient
source moves the BS.1770 relative gate. It also settles what the operator was judging
at that point: not level.

### Why 1.0 wins at 30% when it barely changes the level

| source | 0.25 | 0.405 | 0.674 | 1.0 |
|---|---|---|---|---|
| mixed | -25.8 | -21.6 | -17.2 | **-13.8** |
| pad | -26.8 | -22.6 | -18.2 | **-14.8** |
| snare | -22.0 | -17.9 | -13.4 | **-10.0** |
| piano | -24.3 | -20.1 | -15.7 | **-12.4** |

(how far the reverb sits below the dry signal, in dB, at Mix 30%)

**The tail is 12.0 dB more audible at 1.0 than at 0.25, while the total level moves by
at most 0.51 dB.** This is exactly the prediction made before the set was rendered —
that the audible difference at 30% would be more reverb, not more level — and the
operator's own note on pad confirms it independently.

## 6. Which trims were consistently too quiet at Mix 100%?

**0.25 and 0.405, unambiguously.**

0.25 placed last in three groups of four and third in the fourth — Borda 5 of a
possible 16. 0.405 placed third in three and last in one — Borda 7. Neither reached
second place in any group.

The shipping value was the worst-ranked candidate at full wet in every group but one,
and in that group it was still last.

## 7. Is there a consistent preference between 0.674499 and 1.0?

**Yes: 1.0, by seven groups to one.**

The single exception is **snare at Mix 100%**, and it has a mechanism rather than being
noise. Snare loses least of the four sources through the tank (-1.48 dB against the
pad's -6.26 dB), so at trim 1.0 its full-wet render lands closest to the dry signal of
any source — about 2.6 LU below. If there is a point where more wet gain stops helping,
the source with the least tank loss at the setting with no dry signal is where it should
appear first, and that is where it appeared.

1.0 was still second there, not rejected.

## 8. Pad at Mix 30% — reduced confidence, recorded as such

The operator reported that the overall level difference in this group was very small
and that the ranking was made mainly on the tail rather than on the whole sound.

That is consistent with the measurement: pad at Mix 30% spans **0.47 dB** from the
quietest candidate to the loudest, which is at or below the threshold at which a level
difference is reliable in a non-level-matched comparison.

This group should be read as a judgement about tail audibility only, and it carries
less weight than the others. **It changes nothing:** removing pad 30% entirely leaves
1.0 first in the three remaining Mix 30% groups and first overall.

## 9. Final recommendation

### `previewBaseTrim` = **1.0**

Two independent lines of evidence, and they agree.

**Listening.** 1.0 won 7 of 8 blind groups and all 4 at the default Mix 30%, with a
combined Borda of 31 against 21, 15 and 13. The shipping value came last.

**Measurement.** The trim is 22.57 dB more conservative than musical material needs.
At 1.0 the musical margin is still 10.52 dB, no candidate produced a single over-unity
sample on any of the four sources at either mix point, and the default preset moves by
less than half a decibel.

**Safety, stated without softening.** The sustained tone scan found that at the worst
case — 254 Hz, 60 s decay, Size 100%, full scale — the wet path reaches 6.3782 before
the trim. That case exceeds full scale at **every** candidate including the shipping
one: 4.05 dB over at 0.25, 16.09 dB over at 1.0. The value that would prevent it is
0.1568, which is quieter than today and further into the complaint this stage exists to
fix. So the choice is not between a safe value and an unsafe one; it is between paying
22.6 dB on real material for protection that is not achieved, and not paying it.

**Residual risk, quantified:** a sustained full-scale tone into a maximum-Size 60-second
reverb at Mix 100% will reach about 16 dBFS on the wet bus. This is a float bus, so the
value is carried rather than clipped, but it will show at the user's converter. It is a
12 dB increase in an overshoot that already exists.

### What is not recommended

- **No limiter, clipper, saturator or auto-gain.** The reasons in section 11 stand and
  the tone scan strengthens them: a signal-dependent bound would make every subsequent
  measurement of RT60, modal behaviour and level a measurement of the control loop.
- **No value above 1.0.** The musical ceiling of 3.36 rests on a single program signal,
  and that gap was named before the scan and is still open.
- **No change to the decay-aware factor, the Size compensation, Width or the mix law.**

### Before it ships

The existing "Preview / gain safety" suite asserts clip-freedom for cases that a change
to 1.0 will break. Those assertions encode the old decision and will have to be
restated as measurements with recorded values rather than deleted — otherwise the
change removes the evidence along with the constraint.

---

# Phase 5F — FINAL CLOSURE

## Status: **COMPLETE**

Two production changes, each measured, each chosen by blind listening, each
verified in FL Studio on the exact installed binary.

| | change | evidence |
|---|---|---|
| Mix law | linear -> Aurora Custom, `wet = m^0.799604`, `dry = cos(m·pi/2)` | blind: aurora 3 firsts, equal-power 1, linear 4 lasts; host regression FINAL PASS over 6379 s |
| Wet staging | `previewBaseTrim` 0.25 -> 1.0 | blind: 1.0 won 7 of 8 groups and all 4 at the default Mix 30%; host regression PASS |

Installed and verified: `A49D477DD353058287FA7FC75D4422E865497A91FC4051EB84D5E6A395622464`,
confirmed as the module FL Studio's process actually loaded.

## What the phase set out to fix, and whether it did

**The complaint.** At the default Mix 30% the level collapsed against the dry
signal, and at Mix 100% the reverb was much too quiet.

**Both resolved, and separately, because they had separate causes.**

- The 30% collapse was the linear crossfade: 3.21 LU, 97% of it from the mix
  law. The Aurora curve recovers 2.10 dB on the dry branch while preserving the
  wet/dry ratio exactly at the default. Confirmed by ear on the way up and on
  the way back down from full wet.
- The 100% shortfall was the wet path itself, not the crossfade — at Mix 100%
  the law contributes dry 0 / wet 1 exactly, so it could not have been the
  cause. It was `previewBaseTrim`, 12.04 dB of unconditional attenuation
  documented as safety it did not provide.

In the final host session, block RMS at Mix 100% now measures **2.69 dB above**
Mix 30%. Before the change the same measurement put full wet *below* the mix
positions around it.

## The safety position, recorded as a product decision

Correct staging for musical material is preferred. **No guarantee of wet output
<= 1.0 is offered** for extreme full-scale sustained input at maximum Size and
long Decay.

This is not a relaxation of a guarantee that previously held. A third-octave
sustained tone scan (26 frequencies, three decays, Size 100%) found the worst
case at 254 Hz / 60 s reaching an effective true peak of **6.3782** before any
trim. That case exceeded full scale at the old value too — by 4.05 dB at 0.25,
and by 16.09 dB at 1.0. The value that would have prevented it is 0.1568,
quieter than the value it replaced.

So the old 0.25 must not be described as safe, and the comment in
`ParameterMapping.h` that did so has been corrected.

No limiter, clipper, saturator, auto-gain or signal-dependent bound was added,
and none is recommended: a signal-dependent stage would make every subsequent
measurement of RT60, modal behaviour and level a measurement of the control loop.

## Verification summary

| check | result |
|---|---|
| Test suite | 2007 / 2007 |
| Audio-thread allocations | 0 |
| pluginval strictness 10, default | SUCCESS |
| pluginval strictness 10, preview | SUCCESS |
| pluginval strictness 10, diagnostic-preview | SUCCESS |
| pluginval strictness 10, exact installed binary | SUCCESS |
| Installed hash = build artifact | identical |
| Hash FL Studio actually loaded | identical |
| Duplicate plugin copies in any scan path | 0 |
| Production render vs the blind-chosen candidate | 32 / 32 files byte-identical |

Final host session `2026-08-15_12-03-49_edf25fa1`, 820 s, 48 kHz, prepare block
512, runtime blocks 1-231. At both Mix 30% and Mix 100%: NaN 0, Inf 0, subnormal
0, **clipped 0**, event drops 0, capture drops 0. Only Mix moved; every other
parameter held to zero off-nominal rows.

## Carried forward to Phase 6

**FU-1 — bell/chime-like modal character.** Not a defect and not a regression.
Kept as a tonal-character note for Phase 6 voicing work.

What is established: the mix stage is two scalar multiplies and cannot create
spectral content, so the resonance exists in the tank at every setting. Measured
in the FU-2 renders, relative to the 220 Hz fundamental, four bins gain 10-16 dB
against the dry source (55, 254, 275, 660, 3225 Hz) while two lose 13-15 dB
(1100, 2032 Hz). A flat tank would move them together.

Three limits on that measurement, unchanged: seventeen hand-picked frequencies
rather than a spectrum; the boosts are lower bounds because modulation smears
energy out of a 0.33 Hz bin; and the tone scan's worst points were measured at
Size 100% while those renders are Size 60%, so the apparent correspondence is a
lead and not a confirmation.

The operator's judgement at Mix 100% in the final session: the dry transient
disappears and the tail's tonal character dominates, and this reads as normal
full-wet behaviour rather than a fault. At Mix 30% it sits in the background.

Suggested method for Phase 6 is unchanged: a dedicated impulse capture at Mix
100%, analysed through `AnalyseAuroraVerbCapture` with `impulsive = 1`, so the
tail is isolated from source material.

## New observation — deadline pattern changed by one event

Every previous report in this phase stated that no deadline overrun occurred on
a block of 32 samples or more. **That is no longer true.** The final session
recorded one on a 36-sample block, at a ratio of 1.12.

Of 13 overruns in 820 s, 11 were on 1-2 sample blocks and the worst ratio (1.99)
was on a 1-sample block. The 36-sample event is a single occurrence, 12% over
budget. It cannot plausibly come from the trim, which is one multiplication, and
the likeliest explanation is host scheduling — but that was not measured, so the
cause is **unknown** rather than explained.

Low priority, recorded so the claim is not repeated unchecked.

## Also open, unchanged

- Diagnostic session spam: the diagnostic-preview configuration opens a session
  folder per plugin instantiation, so one pluginval run leaves dozens. Measured
  this cycle: 104 sessions, 548 MB. Archived, not fixed.
- The musical case set for headroom rests on a single program signal. Any future
  value above 1.0 would depend on closing that gap; 1.0 does not.

---

# Archive and backlog

## Archived sessions

| archive | contents |
|---|---|
| `AuroraVerbDiagnosticsArchive\Phase5F-FU2-FinalHostRegression\` | **the final host regression**, 447 MB, 2 folders |
| `AuroraVerbDiagnosticsArchive\Phase5F-MixLawRegression\` | the mix law regression, 454 MB, 2 folders |
| `AuroraVerbDiagnosticsArchive\pre-FU2-test-and-pluginval-sessions\` | 104 sessions, 548 MB, machine-generated |

**Final host regression session:**
`C:\Users\Demirhan Özdemir\AuroraVerbDiagnosticsArchive\Phase5F-FU2-FinalHostRegression\2026-08-15_12-03-49_edf25fa1`

(the companion folder `…_fdd5b922`, 20 KB, is FL Studio's closed scan instance)

Closed cleanly: `summary.txt`, `environment.txt` and `session.json` all written.
Its own verdict is **PASS WITH WARNINGS**, the two warnings being the deadline
pattern and the 600 s capture limit — both expected and both already accounted
for above. NaN/Inf 0, **clipped samples 0**.

The diagnostic root is empty.

## Correction to the deadline figures reported at closure

The closure report above described the deadline pattern from a read taken at
t = 819.8 s, while the plugin was still loaded. **The session ran on to
t = 3136.8 s**, and the finished totals are larger:

| | at t = 819.8 s (reported) | finished session |
|---|---|---|
| overruns | 13 | **54** of 945 744 callbacks |
| worst ratio | 1.99 on a 1-sample block | **3.87 on a 2-sample block** |
| blocks >= 32 samples | one, at 36 | **two, at 36 and 37** |

Block distribution over the whole session: 1 (x35), 2 (x11), 3, 4, 6, 24,
26 (x2), 36, 37.

The substance is unchanged — 46 of 54 are on 1-2 sample blocks, and the two
events at 32 samples or more are single occurrences — but the specific numbers
quoted at closure were a partial read, and the finished ones supersede them.

## Backlog — carried, not being worked

1. **Deadline overruns on blocks of 32+ samples.** Two events (36 and 37
   samples) in a 3137 s session. Cause unknown: it cannot come from the trim,
   which is one multiplication, and host scheduling is a guess rather than a
   measurement. Low priority. The claim "zero overruns above 32 samples", true
   in every earlier session of this phase, must not be repeated without
   rechecking.
2. **Diagnostic-preview session spam.** The diagnostic configuration opens a
   session folder per plugin instantiation, so one pluginval run leaves dozens.
   Measured this cycle: 104 sessions, 548 MB.
3. **FU-1: modal / bell-like tonal character.** Phase 6, voicing and character.
   Not a defect. Full measurement and its three stated limits are in
   Docs/Phase5FHostRegression.md.

Also standing: the musical case set behind the headroom figure rests on a single
program signal. Any future trim above 1.0 depends on closing that gap.
