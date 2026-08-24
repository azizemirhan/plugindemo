# Phase 6 FU-1 — Listening Record

> ## FU-1 IS CLOSED — COMPLETE
>
> **FINAL VERDICT: CHARACTER ACCEPTABLE.**
>
> An audible metallic / bell-like character exists in the AuroraVerb tank. The
> evidence does not show it to be an objectionable voicing defect in shipping
> use.
>
> Decided, and not to be reopened without new evidence:
>
> - the modal response will **not** be flattened;
> - **no** mode is a target — not #25 at ~7217 Hz, not any other;
> - **Mod Depth is unchanged**;
> - the Size-based localisation experiment will **not** be run;
> - **no production DSP change** is made under FU-1.
>
> The measurement stays. The modal map, the q-space Size tracker and the v4 mode
> table are kept as a **regression baseline** — the reference a future change is
> checked against, not a list of things to fix.
>
> **Re-check in the damping phase.** Damping acts directly on high-frequency
> tail decay, which is the region these rounds probed. The nine Round 2c files
> are the ready-made comparison set.
>
> ### The three records that must survive this closure
>
> **1. Shipping Mix 30%, Round 2c — the result the verdict rests on.**
>
> | source | metallicity | annoyance | overall |
> |---|---|---|---|
> | snare | 2 / 4 | 1 / 3 | pleasant |
> | vocal | 2 / 4 | 1 / 3 | pleasant |
> | piano | 1 / 4 | 0 / 3 | pleasant |
>
> **2. The test-retest, on byte-identical audio.** The same snare Mix 30% render
> was rated **4 / 2 / unpleasant** in Round 2 and **2 / 1 / pleasant** blind in
> Round 2c. The first, negative rating is therefore **not** to be used as strong
> voicing evidence. Working listener noise: up to ±2 metallicity, ±1 annoyance.
>
> **3. Source-material limitation.** The most metallic file in the entire Round
> 2c set was the **dry vocal at 4 / 2 / unpleasant** — a source with no reverb in
> it. All three sources are synthetic. **FU-1 was never validated against real
> recordings, and that limitation stands as recorded.** No new test is being
> started for it now.


A record of what happened in the FU-1 listening rounds - 1, 1b, 1c, the high-band
Round 2, its Round 2b stimulus control and the Round 2c decision gate: what was
asked, what came back, what survived decoding, and what did not.

It compiles results only. The closing recommendation in section 6f follows from
those results; no DSP change is proposed or implemented anywhere in this
document.

Companion documents:

- [`Phase6-FU1-Interpretation.md`](Phase6-FU1-Interpretation.md) — the
  measurement side, and how the listening candidates were chosen
- [`../Phase6ModalMap-v4/`](../Phase6ModalMap-v4/) — the measurement the
  candidates came from

Render sets, each with its manifest, blind key and content-free render check:

- [`../Phase6ListeningRound1/`](../Phase6ListeningRound1/) — Round 1
- [`../Phase6ListeningRound1b/`](../Phase6ListeningRound1b/) — Round 1b
- [`../Phase6ListeningRound1c/`](../Phase6ListeningRound1c/) — Round 1c
- [`../Phase6ListeningRound2/`](../Phase6ListeningRound2/) — Round 2, high band
- [`../Phase6ListeningRound2b/`](../Phase6ListeningRound2b/) — Round 2b, stimulus-alone control
- [`../Phase6ListeningRound2c/`](../Phase6ListeningRound2c/) — Round 2c, decision gate

Rounds 2, 2b and 2c also carry ANALYSIS-PLAN.txt, pre-registered at render time
and withheld from the listener along with the key.

---

## 1. Purpose

FU-1 measured the tank's modal structure and classified sixty modes by how they
behave under Size and under modulation. None of that is a statement about
sound. These rounds exist to find the perceptual counterpart of the modal /
bell-and-chime character the measurement describes — whether the structural
distinctions the classifier draws correspond to anything a listener hears.

Every round rendered through the shipping DSP at its defaults. No round changed
a parameter of the production signal path, and none applied normalisation,
loudness matching, limiting, EQ, notching or any other post-processing. The only
gain applied anywhere was the shipping preview trim, identical across every file
in a set.

---

## 2. Round 1 — class A against class B

**Design.** Three pairs, one per frequency region, each pairing a class B
(modulation-resistant) mode against a class A (modulation-moving) mode. Two
sources per candidate — a band-limited noise burst and the Phase 5F snare
stand-in — with independent letter permutations, so the same mode carried a
different letter in each source. Six blind decisions in total.

| pair | region | class B | class A |
|---|---|---|---|
| 1 | low-mid | #50 · 375.44 Hz | #4 · 646.34 Hz |
| 2 | mid | #58 · 1471.67 Hz | #2 · 1158.49 Hz |
| 3 | upper-mid | #54 · 4505.77 Hz | #56 · 4139.64 Hz |

**Result after decoding.**

| pair | source | winner | class |
|---|---|---|---|
| 1 | snare | #50 · 375 Hz | B |
| 1 | diagnostic | #50 · 375 Hz | B |
| 2 | snare | #58 · 1472 Hz | B |
| 2 | diagnostic | #2 · 1158 Hz | A |
| 3 | snare | #56 · 4140 Hz | A |
| 3 | diagnostic | #54 · 4506 Hz | B |

**B preferred 4, A preferred 2**, out of six decisions. The two sources agreed
on the same physical mode in one pair of three.

### What this result cannot be used for

No perceptual conclusion about class A versus class B can be drawn from it, for
four reasons, all recorded at the time:

- **Frequency-region confound.** The two candidates in a pair were different
  modes at different frequencies — 375 against 646 Hz is close to an octave.
  "Which is more beautiful" cannot be separated from a preference for a
  frequency region.
- **Loudness differences.** Nothing was levelled. In 5 of 6 decisions the
  preferred file was the *quieter* one, by 0.49 to 3.96 LU; by peak also 5 of 6.
  So a "louder wins" bias is ruled out, but a bias in the opposite direction —
  less reverb energy reading as cleaner — is not, and the data cannot separate
  it from mode character.
- **Preference and metallicity were not measured separately.** Only the
  preference question was answered. The question that the round existed to
  ask — in which is the bell / metallic character more obvious — was not
  answered for any pair. There is therefore no direct data on metallicity from
  Round 1 at all.
- **n = 6.** Four-to-two is inside chance for six binary decisions
  (P(≥4) = 0.34).

The set was built expecting class B to sound *more* metallic and therefore
worse. The preference went the other way. With the confounds above and n = 6,
that is neither support for the expected direction nor evidence against it.

### Cross-source perceptual consistency observation

One observation from Round 1 stands on its own and is recorded as such.

Mode **#50 at 375.44 Hz** won both of its comparisons — under the letter `E` in
the snare set and under the letter `B` in the diagnostic set, from different
permutations and different source material. Unprompted, the listener remarked
that the diagnostic winner felt tonally close to the snare winner. They were the
same physical mode.

This is recorded as a **cross-source perceptual consistency observation**: the
same modal neighbourhood was recognised twice, blind, across two different
excitations. It says the listening apparatus resolves a consistent identity.

It is **not** generalised to class B. It is one mode, and the other two B modes
in the round did not behave the same way (#58 won on snare and lost on
diagnostic; #54 lost on snare and won on diagnostic).

---

## 3. Round 1b — Mod Depth inside one mode

**Design.** Three pairs. Both files in a pair were the same mode, the same ERB
band and the same excitation sample; the only difference was Mod Depth, 0
against the shipping 30%. This removed the frequency-region confound of Round 1.
Variants were labelled X and Y, with X always presented first.

**Reported answers.** In all three pairs the listener chose **Y as more metallic
and X as more natural / more solid**. Difference magnitude was reported as
*slight*, *almost none* and *very little* respectively.

**Decoded assignment.**

| pair | mode | class | X | Y |
|---|---|---|---|---|
| 1 | #54 · 4505.77 Hz | B | Mod Depth 30% | Mod Depth 0 |
| 2 | #50 · 375.44 Hz | B | Mod Depth 0 | Mod Depth 30% |
| 3 | #4 · 646.34 Hz | A | Mod Depth 30% | Mod Depth 0 |

**Consistency.**

- **Presentation position: 3 of 3.** Every answer named the second file.
- **Treatment direction: 2 of 3.** Because Y was Mod Depth 0 in pairs 1 and 3
  but Mod Depth 30% in pair 2. Two of three is exactly chance (P(≥2 of 3) = 0.50).

**Level check.** All gaps were ≤ 0.60 LU. The file chosen as more metallic was
the louder one in 2 of 3 by integrated loudness and 1 of 3 by peak. Neither
metric reproduced the answer pattern. The only variable that tracked every
answer was position.

### Verdict: INCONCLUSIVE

The letters were randomised against the treatment, so a real treatment effect
should have produced consistency in treatment space. It produced consistency in
letter space instead.

**Methodological lesson recorded from this round:** forced choice combined with
a fixed X/Y presentation order can produce a position bias that is
indistinguishable from a treatment effect, and it does so most readily when the
real difference is near threshold. A same-letter run of three is itself only 25%
likely by chance, so the round did not prove a bias either — it proved the
design could not tell the two apart.

---

## 4. Round 1c — counterbalanced repeat

**Design.** The same question, built so that following the modulation and
following the position predict *different* answers on the same data.

- the same three modes: #54 (B), #50 (B), #4 (A)
- each mode heard twice, on two different deterministic excitation seeds
- the two repeats of a mode carried the treatment on opposite sides of the pair
- six independent letter pairs; the X/Y pattern was not reused
- the two repeats of a mode were never adjacent in the listening order
- **"cannot tell" was offered as a valid answer** and stated to be analysed
  rather than discarded
- one primary question only — which is more metallic. "Which is more natural"
  and "which do you prefer" were deliberately not asked, because in Round 1b
  those answers had moved in lockstep with the metallicity answer and carried no
  independent information
- the analysis plan was written into the manifest before the files were
  rendered

**Reported answers.**

| trial | primary answer |
|---|---|
| 1 | cannot tell |
| 2 | `S` slightly more metallic; difference *almost none* |
| 3 | cannot tell |
| 4 | cannot tell |
| 5 | cannot tell |
| 6 | cannot tell |

**Decoded assignment.**

| trial | mode | class | repeat | first file | second file |
|---|---|---|---|---|---|
| 1 | #50 · 375.44 Hz | B | 1 | `P` = Mod 30% | `Q` = Mod 0 |
| 2 | #4 · 646.34 Hz | A | 2 | `R` = Mod 0 | `S` = Mod 30% |
| 3 | #54 · 4505.77 Hz | B | 1 | `T` = Mod 0 | `U` = Mod 30% |
| 4 | #50 · 375.44 Hz | B | 2 | `V` = Mod 0 | `W` = Mod 30% |
| 5 | #4 · 646.34 Hz | A | 1 | `J` = Mod 30% | `K` = Mod 0 |
| 6 | #54 · 4505.77 Hz | B | 2 | `M` = Mod 30% | `N` = Mod 0 |

Counterbalance verified in the render check: each mode appeared once with Mod
Depth 30% first and once with Mod Depth 0 first; three of six pairs presented
the modulated file first.

**Applying the pre-registered rule.**

| mode | class | repeat 1 | repeat 2 | verdict |
|---|---|---|---|---|
| #50 | B | trial 1: cannot tell | trial 4: cannot tell | **unresolved** |
| #4 | A | trial 5: cannot tell | trial 2: Mod 30% | **unresolved** |
| #54 | B | trial 3: cannot tell | trial 6: cannot tell | **unresolved** |

- **Unresolved at trial level: 5 of 6 = 83.3%**
- **Unresolved at mode level: 3 of 3 = 100%**
- **Treatment-direction consistency: not computable.** No mode had two decided
  repeats; the denominator is zero.
- **Presentation-position consistency: not computable**, for the same reason.
  The round's discriminating comparison never ran.

**The single weak decision.** Trial 2's `S` was **Mod Depth 30%** — the
modulated file was called slightly more metallic, which is the opposite of the
direction the set was built expecting. It was also the *quieter* file of the
pair (`R` was +0.17 LU and +1.18 dB peak), so that one decision is not explained
by loudness.

**Across Round 1b and Round 1c together**, four metallicity decisions were made
in total. All four named the **second** presented file (12.5% by chance for
either position). In treatment terms the same four split **2 Mod 0 / 2 Mod
30%** — exactly even.

### Verdict: UNRESOLVED

The correct statement of the result:

> Under this test condition, the difference between Mod Depth 0 and Mod Depth
> 30% is at or below the listener's discrimination threshold.

**What must not be written from this result:** that modulation is inaudible, or
that modulation does not work. The test deliberately constructed the narrowest
possible view of the signal —

- a single ERB band,
- a single transient,
- 100% wet,
- a single modal neighbourhood.

Full-band programme material at the shipping Mix, where hundreds of modes sum,
is a different question, and this round did not ask it.

---

## 5. Side observations on "solidity" — kept apart from the primary endpoint

The listener volunteered a "more solid / more full-bodied" remark in three of
the six Round 1c trials. It was not an asked question, and it is recorded here
separately so it cannot be mistaken for the endpoint.

| trial | file called more solid | Mod Depth | position | louder by LUFS? |
|---|---|---|---|---|
| 2 | `R` | Mod 0 | first | yes, +0.17 LU |
| 3 | `T` | Mod 0 | first | yes, +0.38 LU |
| 6 | `N` | Mod 0 | second | yes, +0.23 LU |

All three landed on Mod Depth 0, and the positions were mixed (two first, one
second), so position does not explain them. That looked like the one
treatment-consistent pattern in the round.

It does not survive the level check. All three also landed on the louder file by
integrated loudness. Mod Depth 0 happened to be the louder file in exactly
trials 2, 3 and 6 — and those are exactly the three trials where the remark was
volunteered; no remark came in trials 1, 4 and 5, where Mod Depth 30% was
louder.

**"Solid = Mod Depth 0" and "solid = louder" are perfectly confounded in this
data and cannot be separated.** Together with the facts that the dimension was
not asked, that the remarks appeared in only half the trials (a selection
effect), and that n = 3, this is a **hypothesis for a future round, not a
finding**.

For completeness: modulation did not shift level systematically. Mod Depth 0 was
louder in three of six trials and quieter in three — no direction.

Round 1b's equivalent remarks carry no treatment information at all: all three
named the first file, so they are fully position-confounded.

---

## 6. Measurement to perception — what did not bridge

Recorded without generalisation.

Mode **#4 (class A, 646.34 Hz)** carries the largest measured modulation effect
of the three modes tested:

| measurement (v4) | #4 | #50 | #54 |
|---|---|---|---|
| spectral spread ratio | **×3.662** | ×1.700 | ×0.968 |
| concentration ratio | **0.535** | 0.994 | 0.967 |

Under modulation #4's neighbourhood spreads nearly four-fold and loses almost
half its energy concentration — by a wide margin the strongest redistribution in
the listening set.

In Round 1c, #4's two repeats returned **cannot tell** (trial 5) and **almost
none** (trial 2). In Round 1b the same mode's difference was reported as *very
little*.

The same reversal appeared in Round 1b across all three modes: the mode with the
smallest measured effect (#54, spread ×0.97) was reported as the most audible
difference (*slight*), and the mode with the largest (#4) as *very little*.

**In this narrow listening condition, the magnitude of the measured
local-response redistribution did not reliably predict the magnitude of
perceived metallicity change.** This is recorded for these three modes under
these conditions. It is not extended to other modes, other Mix settings, other
material, or the tank as a whole.

---

## 6b. Round 2 — the high band, rated absolutely

**Design.** Nothing comparative. Eight trials, each a single file rated on its
own; four modes rendered twice on different excitation seeds, repeats never
closer than three positions apart. Three separate endpoints — metallicity 0–4,
annoyance 0–3, tail pleasantness — recorded independently, and preference not
asked at all. Mod Depth fixed at the shipping 30% throughout; it was not under
test. Excitation raised ×8 before the tank (linear path, same factor for every
file), giving a trial loudness spread of 1.65 LU and peaks between −11.41 and
−9.69 dBFS. The analysis plan was written to a separate file at render time and
withheld from the listener along with the key, so the thresholds could not
invite rating towards them.

**Decode.**

| trial | file | mode | frequency | class | repeat |
|---|---|---|---|---|---|
| 1 | `high-01-G` | #3 | 10655.00 Hz | B | 1 |
| 2 | `high-02-Z` | #10 | 9146.01 Hz | B | 1 |
| 3 | `high-03-L` | #1 | 11561.40 Hz | D | 1 |
| 4 | `high-04-H` | #25 | 7217.22 Hz | A | 1 |
| 5 | `high-05-O` | #10 | 9146.01 Hz | B | 2 |
| 6 | `high-06-T` | #3 | 10655.00 Hz | B | 2 |
| 7 | `high-07-C` | #25 | 7217.22 Hz | A | 2 |
| 8 | `high-08-N` | #1 | 11561.40 Hz | D | 2 |

**Ratings, by mode.** (Trial 6's earlier provisional record of 4/3/neutral was
superseded by the listener; 4/1/pleasant is the value used.)

| mode | class | repeat 1 M/A/tail | repeat 2 M/A/tail | Δ metallicity | mean M | mean A | repeat-consistent | high-priority candidate |
|---|---|---|---|---|---|---|---|---|
| #3 · 10655 Hz | B | 4 / 2 / neutral | 4 / 1 / pleasant | 0 | 4.0 | 1.5 | **yes** | **yes** |
| #10 · 9146 Hz | B | 3 / 2 / pleasant | 4 / 2 / pleasant | 1 | 3.5 | 2.0 | **yes** | **yes** |
| #1 · 11561 Hz | D | 3 / 3 / neutral | 4 / 2 / neutral | 1 | 3.5 | 2.5 | **yes** | **yes** |
| #25 · 7217 Hz | A | 4 / 3 / unpleasant | 4 / 3 / unpleasant | 0 | 4.0 | 3.0 | **yes** | **yes** |

**All four modes repeat-consistent, 4 of 4** — the first round in this series to
produce stable within-listener judgements. All four clear the pre-registered
high-priority threshold. No mode qualified for "audible character, not
objectionable": the lowest annoyance recorded was 1.

Whole-tail context, unfiltered snare through the same tank, one rating each:

| file | metallicity | annoyance | tail |
|---|---|---|---|
| `context-wet100.wav` | 3 | 1 | neutral |
| `context-mix30.wav` | 4 | 2 | unpleasant |

**#1 at 11561 Hz is class D — structurally unresolved on the Size axis.** It is
recorded here as a *high-prominence, structurally unresolved high-band
perceptual candidate* only. Nothing about modulation resistance or movement is
claimed for it.

### Verdict: YES — with the limits below  *(SUPERSEDED — see section 6d)*

There is now sufficient evidence that a perceptually real high-band metallic /
bell-like character exists in the tank and is at least partly objectionable.
What carries the verdict:

- 4 of 4 modes repeat-consistent, across different excitations;
- metallicity 3.5–4.0 of 4 for every mode tested;
- annoyance ≥ 1 everywhere, 3.0 at #25 with both tails rated unpleasant;
- the **unfiltered, full-band context render** — no band-limiting anywhere —
  also rated 3 and 4 for metallicity, so the percept is not an artefact of the
  targeted excitation;
- trial loudness spread of 1.65 LU, which effectively rules out a level
  confound between trials.

What the verdict does **not** cover:

- **Which** modes. Metallicity saturated at 3.5–4.0 across all four, a range of
  0.5 on a five-point scale, so the round cannot discriminate between them or
  localise the character to any mode or sub-band.
- One listener, two ratings per mode.
- No stimulus-alone anchor was rendered, so the contribution of the band-limited
  burst's own pitched quality to the targeted trials was never quantified. The
  context result is what stands in for that control, and it is a substitute
  rather than the control itself.
- No low or mid-band comparison was rated on this scale, so "high-band" is where
  the character was measured, not where it was shown to be specific to.

### Round 2 against Rounds 1b and 1c

These do not conflict, and the distinction matters:

- 1b and 1c asked a **differential** question — can Mod Depth 0 be told from
  30%? Answer: at or below threshold.
- Round 2 asked an **absolute** question with Mod Depth fixed at 30% — is
  metallic character present at all? Answer: strongly, and repeatably.

A large absolute character and an inaudible 30% modulation change are perfectly
compatible: the modulation redistributes the neighbourhood by the amounts the
measurement records, and that amount does not move the percept. Round 2's 4-of-4
repeat consistency, against 1b and 1c's inability to produce stable answers,
also says the absolute-rating task is the tractable one.

### Measurement against perception, again

| mode | class | prominence | Δprom | spread | concentration | mean M | mean A |
|---|---|---|---|---|---|---|---|
| #25 | A | 29.76 dB | −11.42 | ×1.692 | 0.428 | 4.0 | **3.0** |
| #10 | B | 30.49 dB | +0.79 | ×1.272 | 1.042 | 3.5 | 2.0 |
| #3 | B | 32.19 dB | −6.33 | ×1.883 | 0.735 | 4.0 | **1.5** |
| #1 | D | 33.39 dB | −14.47 | ×3.422 | 0.687 | 3.5 | 2.5 |

Metallicity is at ceiling and cannot order anything. Annoyance does vary, and it
shows **no monotone relation** to prominence, to prominence change, or to spread
ratio. Against prominence it is close to inverted: the least prominent mode
(#25, 29.76 dB) was the most objectionable, the most prominent (#1) second.

The mode rated most objectionable, in both repeats, with both tails unpleasant,
is the **class A modulation-moving control** — not a class B mode. Recorded as
observed. With four modes and one listener no correlation is claimed in any
direction.

### On the two context files

They are a whole-tail control and are **not** mode-specific evidence; one mode
cannot be isolated inside a broadband source without adding a filter this set
does not add.

The question they do answer: **the high-band metallic character does not
disappear in whole-tail production context.** Both context files rated 3–4 for
metallicity, the same range as the targeted trials, from an unfiltered source.

`context-mix30` scored worse than `context-wet100` on all three endpoints (4/2/
unpleasant against 3/1/neutral). This is **not** attributed to the reverb. The
two files were a single unblinded, unrandomised, unrepeated pair, and at least
four explanations are open, none tested:

- dry/wet interaction — with the dry snare present the tail is heard as added
  colour against a reference rather than as the whole signal;
- level — `context-mix30` peaks 7.85 dB higher and is 1.17 LU louder;
- perceptual contrast and masking release from the dry transient;
- order — `wet100` was heard first, and this listener chose the second file in
  4 of 4 decisions across Rounds 1b and 1c.

## 6d. Round 2b — the stimulus alone

**Design.** The control Round 2 was missing. The same eight excitations — same
seeds, same centre frequencies, same one-ERB bands, same 8× pre-filter gain,
same 7.1 s duration — with **no tank**. New presentation order, new labels, four
modes twice each, repeats never closer than three apart. One common 0.5× output
gain on all eight files and on none individually.

Verified rather than asserted: 8 of 8 (seed, centre frequency) pairs matched a
Round 2 pair, and 0 of 8 files carried any non-zero sample after the excitation
window — a reverb would have left one.

**Level.** Controls −22.67 to −21.24 LUFS against Round 2's −23.12 to −21.47, so
loudness is effectively matched (~0.3 LU). Peaks are **~5 dB higher** on the
controls (−7.65 to −4.43 against −11.41 to −9.69), which is inherent: a 25 ms
burst has a far higher crest factor than a reverb tail. Recorded as a confound
that works **against** the tank — a higher-crest file plausibly rates as harsher,
so the near-zero wet-minus-dry annoyance deltas below may understate the tank's
contribution.

**Decode and ratings.**

| file | mode | frequency | class | seed | Round 2 counterpart | M | A | sound |
|---|---|---|---|---|---|---|---|---|
| control-01-R | #25 | 7217.22 Hz | A | 20260904 | `high-04-H` (r1) | 3 | 2 | neutral |
| control-02-M | #1 | 11561.40 Hz | D | 20260903 | `high-03-L` (r1) | 4 | 3 | unpleasant |
| control-03-V | #10 | 9146.01 Hz | B | 20260902 | `high-02-Z` (r1) | 2 | 1 | pleasant |
| control-04-K | #3 | 10655.00 Hz | B | 20260901 | `high-01-G` (r1) | 2 | 2 | neutral |
| control-05-S | #1 | 11561.40 Hz | D | 20260908 | `high-08-N` (r2) | 4 | 3 | neutral |
| control-06-Y | #25 | 7217.22 Hz | A | 20260907 | `high-07-C` (r2) | 2 | 3 | unpleasant |
| control-07-F | #3 | 10655.00 Hz | B | 20260906 | `high-06-T` (r2) | 4 | 1 | neutral |
| control-08-P | #10 | 9146.01 Hz | B | 20260905 | `high-05-O` (r2) | 3 | 2 | neutral |

**Dry against wet, per mode.** Descriptive only — two ratings per cell, one
listener. No statistical test was run and no correlation is claimed.

| mode | class | dry M (repeats) | wet M | Δ M | dry A | wet A | Δ A | pre-registered band |
|---|---|---|---|---|---|---|---|---|
| #25 · 7217 Hz | A | **2.5** (3, 2) | 4.0 | +1.5 | 2.5 | 3.0 | +0.5 | **B**, at its upper edge |
| #10 · 9146 Hz | B | **2.5** (2, 3) | 3.5 | +1.0 | 1.5 | 2.0 | +0.5 | **B**, at its upper edge |
| #3 · 10655 Hz | B | **3.0** (2, 4) | 4.0 | +1.0 | 1.5 | 1.5 | 0.0 | **C**, but see below |
| #1 · 11561 Hz | D | **4.0** (4, 4) | 3.5 | **−0.5** | 3.0 | 2.5 | −0.5 | **C** |

**No mode reached band A.** The lowest dry metallicity mean in the set is 2.5;
the pre-registered "strong evidence the tank added it" band required 0–1 and is
empty.

Three details that matter:

- **#1 at 11561 Hz rates 4 and 4 dry** — the stimulus alone is at ceiling, and
  its wet mean (3.5) is *lower* than its dry mean. For this mode the targeted
  trial measured the excitation, not the tank.
- **#3's dry repeats disagree by 2** (ratings 2 and 4). Its band C assignment
  rests on an unstable mean and should be treated as unresolved rather than as a
  clean band C.
- **#25 — the mode Round 2 flagged as most objectionable** (annoyance 3.0, both
  tails unpleasant) — carries dry annoyance 2.5 with one dry file rated
  unpleasant. Most of its annoyance is present with no reverb at all.

The metallicity deltas are +1.5, +1.0, +1.0 and −0.5 on a five-point scale with
two ratings per cell. The annoyance deltas are +0.5, +0.5, 0.0 and −0.5, and the
peak confound pushes in the direction that shrinks them.

### Does Round 2 localise the problem to a mode or region? **NO**

- No mode cleared band A; dry metallicity spans 2.5–4.0 against wet 3.5–4.0, so
  the two distributions largely overlap.
- Round 2's wet metallicity was already at ceiling across all four modes (range
  0.5), which by itself gave no ranking.
- One mode rates *higher* dry than wet.
- The mode Round 2 singled out on annoyance is largely as annoying without the
  tank.

The targeted, band-limited method cannot point at a high-band mode or sub-band.
A one-ERB band is roughly a ninth of its centre frequency, and a burst through
it is audibly pitched before the reverb is reached.

### What this does not touch

The whole-tail evidence is unaffected, and the distinction has to be kept sharp:

- **Targeted mode localisation** — confounded, and now withdrawn.
- **Whole-tail metallic character** — untouched by this control.

`context-wet100.wav` is the load-bearing file. It is an **unfiltered, full-band
snare through the tank at Mix 100%** — no band limiting anywhere, and at 100%
wet it contains no dry signal at all, so everything audible in it is tank
output. It rated **metallicity 3, annoyance 1, neutral**.

`context-mix30.wav` (4 / 2 / unpleasant) contains the dry snare as well, and
carries the level, order and blinding confounds already recorded in section 6b.
It is weaker evidence, not stronger.

One gap remains open on this side too: **the dry broadband source was never
rated on its own.** `context-wet100` needs no such anchor because it is 100% wet,
but `context-mix30` does, and does not have one.

### Verdict, revised: PARTIAL  *(SUPERSEDED — see section 6e)*

Round 2's verdict of YES is **downgraded to PARTIAL — whole-tail metallic
character is real, targeted localisation is confounded.**

What survives:

- `context-wet100` is 100% tank output from an unfiltered broadband source and
  was rated 3 of 4 for metallic character. The band-limiting confound cannot
  reach it.
- Wet metallicity exceeded dry for three of the four modes.

What does not:

- Any claim about *which* mode or sub-band. Withdrawn.
- The severity implied by Round 2's targeted ratings. The strongest
  unconfounded evidence — `context-wet100` — pairs metallicity 3 with annoyance
  **1** and an overall rating of **neutral**. That is audible character, not a
  demonstrated defect.

The honest position: a high-band metallic character exists in the tank's output
and is audible on broadband material; it is at most mildly objectionable on the
one unconfounded measurement available; and nothing yet says where in the high
band it lives.

**Mod Depth is unchanged by this round.** Rounds 1b and 1c put the 0 to 30%
difference at or below threshold, Round 2b produces no new evidence about it,
and that result stands as it was.

**#25 at 7217 Hz is not a target.** It was the worst-rated mode in Round 2 and
that is precisely the reading Round 2b removes: its dry control already carries
annoyance 2.5 and an unpleasant rating. Nothing here supports pointing an
intervention at it, and none is proposed.

---

## 6e. Round 2c — the decision gate

**Design.** No band limiting anywhere. Three full-band reference sources — the
snare Rounds 1 and 2 used, the developer set's formant vocal, the Phase 5F piano
chord — each rendered three ways: DRY (no reverb), WET100 (pure tank output) and
MIX30 (the shipping Mix). Nine files, each rated on its own, randomised, with a
source's three conditions never closer than three positions apart.

The three sources were brought to a common loudness **at the input**, one gain
per source and the same across that source's three conditions — snare ×1.0000
(anchor), vocal ×3.6295, piano ×0.8173. Natively they sat ~13 dB apart, and the
first render spread 14.82 LU almost entirely source-to-source rather than
condition-to-condition. No rendered file was normalised and the production gain
law is untouched, so every dry→wet→mix relationship is exactly what the shipping
chain produces. Final spread 5.03 LU, no clipping.

**Decode and ratings.**

| file | source | condition | LUFS | peak | M | A | overall | source in front |
|---|---|---|---|---|---|---|---|---|
| case-01-E | snare | MIX30 | −23.92 | −6.27 | 2 | 1 | pleasant | partly |
| case-02-I | vocal | WET100 | −24.41 | −13.80 | 2 | 1 | pleasant | partly |
| case-03-J | piano | DRY | −22.54 | −9.35 | 1 | 0 | pleasant | yes |
| case-04-Q | snare | WET100 | −25.09 | −14.12 | 3 | 2 | pleasant | partly |
| case-05-U | vocal | MIX30 | −23.08 | −13.86 | 2 | 1 | pleasant | yes |
| case-06-W | piano | MIX30 | −23.36 | −10.30 | 1 | 0 | pleasant | yes |
| case-07-X | snare | DRY | −22.44 | −5.33 | 1 | 1 | pleasant | yes |
| case-08-Y | vocal | DRY | −22.54 | −15.97 | **4** | 2 | unpleasant | partly |
| case-09-Z | piano | WET100 | −27.47 | −14.51 | 2 | 0 | unpleasant | partly |

**By source.**

| source | DRY | WET100 | MIX30 | ΔM dry→wet | ΔM dry→mix |
|---|---|---|---|---|---|
| **snare** | 1 / 1 / pleasant / yes | 3 / 2 / pleasant / partly | 2 / 1 / pleasant / partly | **+2** | +1 |
| **vocal** | **4 / 2 / unpleasant** / partly | 2 / 1 / pleasant / partly | 2 / 1 / pleasant / yes | **−2** | **−2** |
| **piano** | 1 / 0 / pleasant / yes | 2 / 0 / unpleasant / partly | 1 / 0 / pleasant / yes | +1 | 0 |

*(metallicity / annoyance / overall / source-in-front)*

The most metallic file in the entire set is a **dry source with no reverb in it
at all** — the vocal, at 4 of 4, annoyance 2, overall unpleasant. It is an
additive formant synthesis of forty harmonics, and it reads as metallic on its
own. Adding the reverb *lowered* its rating by two points.

### The pre-registered gate, applied unchanged

**A) WET100 metallicity high, annoyance ≤ 1, overall mostly neutral/pleasant →
audible tank character, not demonstrated as an objectionable defect.**
WET100 came back 3/2/pleasant, 2/1/pleasant, 2/0/unpleasant. Metallicity is
moderate (max 3), annoyance ≤ 1 in two of three, overall pleasant in two of
three. **A is satisfied.**

**B) MIX30, in two or more sources, metallicity ≥ 3 AND (annoyance ≥ 2 OR
unpleasant) → shipping-context voicing issue strengthened.**
MIX30 came back 2/1/pleasant, 2/1/pleasant, 1/0/pleasant. **Zero sources reach
metallicity 3. Zero reach annoyance 2 or unpleasant. B is not met — not in one
source, let alone two.** In the shipping Mix the highest metallicity across
every source is 2 of 4, the highest annoyance is 1 of 3, and all three are
overall pleasant.

**C) MIX30 rates badly while the same source's DRY rates similarly → source
contribution confounded.** No MIX30 file rated badly, so C does not fire as
written. The related fact is stronger and points the same way: where the set did
rate badly it was the **dry vocal**, and its reverberated versions were two
points better.

**D) WET100 reaches annoyance ≥ 2 or unpleasant across different sources →
tank-level evidence strong.** Two WET100 files trip the literal wording: the
snare on annoyance 2, the piano on overall unpleasant. But the piano's annoyance
is **0** — its "unpleasant" is explicitly not metallic discomfort, which is why
the two endpoints are recorded separately. On the metallic-specific endpoint
only the snare qualifies, one source of three, and it was still rated overall
pleasant. **D is not met.**

### Level check

Decoded, the conditions sit at DRY −22.51 LUFS (loudest), MIX30 −23.45, WET100
−25.66 (quietest) — a 3.15 LU spread between condition means.

| condition | mean LUFS | mean metallicity | mean annoyance |
|---|---|---|---|
| DRY | −22.51 (loudest) | 2.00 | 1.00 |
| MIX30 | −23.45 | 1.67 | 0.67 |
| WET100 | −25.66 (quietest) | 2.33 | 1.00 |

**Louder does not track more metallic or more annoying.** The quietest condition
scored the highest mean metallicity; the loudest scored the middle. DRY and
WET100 tie on annoyance despite being 3.15 LU apart. Descriptive, three sources
per condition, no test run — but the level difference does not explain the
result, and it works against the direction that would have made the tank look
worse.

### Test-retest: the same audio, rated twice

Two of the nine files are byte-identical to Round 2's context renders, forced by
the specification — same snare reference, unchanged production settings,
deterministic renderer. They had already been rated in Round 2.

| file | Round 2 rating | Round 2c rating | change |
|---|---|---|---|
| snare WET100 (`case-04-Q` = `context-wet100`) | 3 / 1 / neutral | 3 / 2 / pleasant | M **0**, A +1, overall +1 step |
| snare MIX30 (`case-01-E` = `context-mix30`) | 4 / 2 / unpleasant | **2 / 1 / pleasant** | M **−2**, A −1, overall +2 steps |

This has to be stated plainly because it is the most consequential single
finding of the round.

**The strongest piece of "objectionable" evidence in the whole programme does
not replicate.** `context-mix30` — rated 4 / 2 / unpleasant in Round 2, and the
main reason Round 2's verdict read as severe — came back **2 / 1 / pleasant** on
identical bytes.

The primary endpoint held on the other file (metallicity 3 → 3), so the scale is
not noise throughout. But on this one it moved two points, and the plausible
reason is context: in Round 2 that file was heard last, unblinded, after eight
band-limited targeted trials, second in an unrandomised pair, by a listener with
a documented second-file tendency. Here it was heard first, blind, among
full-band material.

**Working figure for listener noise on identical audio: up to ±2 on
metallicity and ±1 on annoyance.** Any difference smaller than that, anywhere in
these rounds, cannot be read as real.

### Verdict: CHARACTER ACCEPTABLE

> Round 2c does **not** show that AuroraVerb's metallic character is
> objectionable enough, in real use, to justify a voicing intervention.

- In the shipping Mix, across three different full-band sources, metallicity
  never exceeds 2 of 4, annoyance never exceeds 1 of 3, and every file is rated
  overall pleasant. Gate B — the shipping-context test — is not met by any
  source.
- Pure tank output shows a real but moderate character: metallicity 2–3, and
  overall pleasant on two of three sources.
- The most metallic and most unpleasant file in the set contains no reverb.
- The single earlier rating that suggested a shipping-context problem does not
  replicate on identical audio.
- The level spread does not explain any of it, and runs the other way.

The character is **audible and real**. It is not demonstrated to be a defect.

## 6f. What this means for FU-1, and what it does not

Read together with the earlier rounds:

| round | question | outcome |
|---|---|---|
| 2 | which high-band mode is metallic? | YES, then withdrawn |
| 2b | is the targeting method sound? | **no** — the stimulus is itself metallic |
| 2c | is the character objectionable in real use? | **no** — CHARACTER ACCEPTABLE |

Round 2b removed the *localisation*. Round 2c removes the *motivation*. The
PARTIAL verdict after 2b resolves to **CHARACTER ACCEPTABLE**: whole-tail
metallic character is real and audible, and there is no evidence it needs
fixing.

**Closing recommendation for FU-1 — documentation, not intervention.**

- **Do not flatten the modal response.** Nothing in three listening rounds
  supports it, and the measurement was never a complaint about the sound.
- **Do not target #25 at 7217 Hz, or any other mode.** Localisation was
  withdrawn in 2b and the motivation was withdrawn in 2c.
- **Do not change Mod Depth.** Rounds 1b and 1c put the 0 to 30% difference at
  or below threshold; nothing since has changed that.
- **Do not run the Size-based localisation experiment.** It was contingent on a
  VOICING ISSUE verdict, which did not arrive.
- **Keep the character and keep the measurement.** The FU-1 modal map, the
  q-space Size tracker and the v4 mode table stay valuable as a **regression
  baseline** — the reference against which a future change is checked, rather
  than a to-do list.
- **Re-observe in the Phase 7 damping work.** Damping acts directly on
  high-frequency tail decay, which is the region these rounds probed. Any change
  there should be re-checked against this baseline, and Round 2c's nine files
  are the ready-made comparison.

**Limits that come with this verdict, and they are not small:**

- **One listener throughout**, one rating per cell in Round 2c.
- **Synthetic sources.** All three are generated, not recorded. The dry vocal
  rated 4 of 4 for metallic character on its own, which says the material may
  not represent real programme material well. A repeat on recorded sources would
  be the single most valuable addition, and would be cheap.
- **One Decay, one Size, one Mod Rate.** Only the shipping defaults were heard.
- The verdict is "not demonstrated to be objectionable", which is not the same
  as "demonstrated to be fine".

## 6g. If the question is ever reopened

Scoping only. No DSP change is proposed, none is implemented, and nothing below
says what a fix would be. None of this is recommended now - the verdict is
CHARACTER ACCEPTABLE and no localisation is needed. It is recorded so that a
future round does not have to rediscover it.

The unsolved problem, if localisation is ever wanted again: **how do you localise
a whole-tail metallic character in frequency, when the only targeting tool tried
so far — a band-limited excitation — is itself metallic before the reverb
touches it?**

Three routes, in order of cost:

**1. Give the broadband side its own dry anchor.** `context-mix30` contains the
dry snare and has never been rated without the tank. One extra rating closes
that gap. `context-wet100` does not need it — at 100% wet it contains no dry
signal — which is exactly why it is the file the verdict now rests on.

**2. Localise with Size instead of with a filter.** This is the route that
avoids the confound entirely. FU-1 established that loop modes obey
f = q / sizeScale to about 0.01%. Render the *whole tail* from an unfiltered
broadband source at several Size settings — Size is a production control, so no
DSP changes and no filtering anywhere — and ask what happens to the pitch of the
ringing. If it moves inversely with Size, it is a loop mode and its modal
coordinate can be recovered from the perceived pitch; if it stays put, it is
not. The listener's pitch perception does the localising that the band-limited
burst could not.

A pitch-matching probe makes this quantitative: play the tail, let the listener
tune a sine to the ring, record the frequency. The sine is a measuring
instrument played separately, never mixed into the render.

**3. Only if a targeted stimulus is still wanted: find one that is not itself
metallic.** Run the dry-anchor ladder — 1, 2, 4, 8 ERB and an octave — and find
the narrowest bandwidth whose dry metallicity rates 0–1. If such a bandwidth
exists it becomes the targeting tool. If none does, then band-limited excitation
is simply not a usable localisation method for this question and should be
dropped rather than repeated.

Two corrections to carry into any of them:

- **Match peak as well as loudness**, or run both and report both. Round 2b's
  controls sat ~5 dB higher in peak than the wet trials, which biases annoyance
  in the direction that hides the tank's contribution.
- **A scale that does not saturate.** Wet metallicity ran 3.5–4.0 and dry
  2.5–4.0; both are near ceiling and neither can rank anything. Anchors or a
  wider range are needed before any ordering claim is possible.

---

## 7. Methodological guardrails

Carried forward as lessons from Rounds 1, 1b and 1c, and applied in Round 2 -
which was the first round to produce stable within-listener judgements. These
are constraints on how a round is built; they do not specify what it should
test.

- **No forced choice.** "Cannot tell" must be an offered, valid answer, and must
  be analysed rather than discarded. Round 1b withheld the option and got three
  answers; Round 1c offered it and got five abstentions from six trials.
- **Counterbalance the treatment order.** Every condition must appear on both
  sides of the pair across the round.
- **Do not reuse a fixed X/Y or first/second mapping.** Independent labels per
  trial, and a labelling scheme that has not been used in a previous round.
- **Include repeat trials** where the design allows, so within-listener
  consistency can be measured instead of assumed.
- **Keep preference and metallicity as separate endpoints.** In Round 1b they
  moved in lockstep and the round lost its endpoint.
- **State any frequency-region confound explicitly.** Where two candidates sit
  at different frequencies, the comparison cannot separate mode character from
  region preference, and the record must say so.
- **Measure and report loudness differences.** Do not level, do not loudness
  match, but always publish the per-pair difference and check every conclusion
  against it. Two apparent findings in these rounds dissolved under this check.
- **Do not push the listener to find a difference.** An honest "no difference"
  is a result.
- **Fix the analysis plan before decoding**, and write it into the manifest so
  it is timestamped ahead of the answers.
- **Report treatment-direction and presentation-position consistency
  separately.** Never report letter-space or position-space consistency as
  though it were a treatment result.

Also carried forward from the render side: keep the blind key and all per-file
measurements in a separate file, verify sets with content-free checks only, and
regenerate excitations with fresh seeds when a previous round's key is already
open — otherwise a published key lets a reused file be identified by checksum.

Round 2 applied all of these. One was added by it: keep the analysis plan out of
the listener-facing manifest. Pre-registration exists so the analyst cannot pick
the rule after seeing the answers - it does not require handing the listener the
thresholds their own ratings will be measured against.

---

## 8. Final status

| item | status |
|---|---|
| **FU-1** | **COMPLETE — CLOSED** |
| Round 1 | completed — exploratory, confounded |
| Round 1b | **INCONCLUSIVE** |
| Round 1c | **UNRESOLVED — 3 of 3 modes** |
| Round 2, high band | completed — 4 of 4 modes repeat-consistent |
| Round 2 verdict | superseded by Round 2b |
| Round 2b, stimulus-alone control | completed — no mode reached band A; lowest dry metallicity 2.5 of 4 |
| Round 2b verdict | PARTIAL, superseded by Round 2c |
| Round 2c, decision gate | completed — **gate B not met by any source** |
| **FINAL VERDICT** | **CHARACTER ACCEPTABLE** — the metallic character is audible and real, and is not demonstrated to be objectionable |
| Targeted mode localisation | **withdrawn** — the one-ERB excitation is itself metallic before the tank |
| Shipping-context evidence | metallicity ≤ 2 of 4, annoyance ≤ 1 of 3, overall pleasant on all three sources |
| Most metallic file in Round 2c | a **dry source with no reverb** (vocal, 4 of 4) |
| Test-retest on identical bytes | metallicity moved **2 points**; listener noise up to ±2 M / ±1 A |
| #25 · 7217 Hz as a target | **no** |
| Production DSP | unchanged |
| Voicing decision | **made — no intervention**; keep the character, keep the measurement as a regression baseline |
| Mod Depth | unchanged, and no round supports changing it |
| `Source/DSP` diff | **0** |

Round 2 found a strong, repeatable metallic character in targeted high-band
renders. Round 2b showed the targeting stimulus carries much of it on its own,
so the claim about which mode is responsible was withdrawn. Round 2c then asked
the only question that decides anything - is it objectionable in real use - and
the answer, across three full-band sources at the shipping Mix, is no.

The character is audible and real. It is not demonstrated to be a defect, the
strongest earlier evidence that it was does not replicate on identical bytes,
and no intervention is proposed. Section 6f is the closing recommendation;
section 6g records the unsolved localisation problem in case the question is
ever reopened.
