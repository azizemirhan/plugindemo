# Phase 7 — the damping default listening gate

The automated regression is finished and it cannot answer this question. It
says the new default operating point is measurably healthier — the worst
500 Hz – 2 kHz deviation from the requested Decay falls from **7.90 % to
2.32 %**, every band's tail stays single-slope, and no real regression exists
anywhere in 2350 checks. It says nothing at all about whether the result is a
better product.

**Verdict: not yet taken.** This document is the procedure and the material.
The six answers and the verdict are to be written into it by the listener; I
have no ears and will not fill them in.

---

## The one thing to read before listening

The measured difference between these two settings is **a change to the tail**,
and at the shipping Mix of 30 % the tail is a minority of what is heard. That
is arithmetic, not a defect: a 1 dB difference between the two wet signals
arrives at the output as roughly 0.1 dB once it has been scaled by 0.3 and put
under a dry signal at 0.7.

Measured on the renders themselves, unsigned:

| source | at Mix 30 % | at 100 % wet |
|---|---|---|
| mixed | 0.09 dB tilt | **1.23 dB tilt**, 0.18 dB louder |
| snare | 0.02 dB tilt | **0.52 dB tilt**, 1.12 dB louder |
| hats | 0.00 dB tilt | **1.29 dB louder** |
| pad | 0.00 dB | 0.00 dB |
| piano | 0.00 dB | 0.01 dB |
| vocal | 0.02 dB tilt | 0.14 dB tilt |

**Nine of twelve pairs are near-identical**, taking 0.25 dB as the smallest
broadband level or tilt difference that survives an A/B on program material.

So the procedure is: **judge the new tail on the 100 % wet pairs, and use the
Mix 30 % pairs to confirm nothing in normal use got worse.** Expecting a large
difference at Mix 30 % and then hearing one is the failure mode this note
exists to prevent.

### Why three of six sources cannot show it

`pad` is a 220 Hz tone, `piano` a chord, `vocal` a formant "ah". None carries
meaningful energy above 6 kHz, and moving the high damping corner from 6 to
10 kHz can only act on energy above 6 kHz. Their null results are a real
finding — the change does not hurt that material — but they are not a sensitive
test and must not be read as one.

This is why `hats` was added. The established FU-2 material came out
near-identical on eight of ten pairs, and a gate that cannot reveal the change
it gates is not a gate. `hats` is a noise-based hi-hat stand-in with no
resonator in it, deliberately: question 3 asks whether the *tank* sounds
metallic, and the source must not answer that on its behalf. It is named as an
addition in the manifest and is not part of the established Phase 6 set.

---

## The two settings

Identical in everything but two values.

| | A | B |
|---|---|---|
| Low Damp | 20 % | 20 % |
| Low Damp Freq | 200 Hz | 200 Hz |
| **High Damp** | **50 %** | **45 %** |
| **High Damp Freq** | **6 kHz** | **10 kHz** |
| Mod Depth | 30 % | 30 % |

**A is the default the plugin shipped with before Phase 7. B is the default it
ships with now.**

Held fixed, and not to be moved during the comparison: Decay 2.5 s, Size 60 %,
Mix 30 %, Width 100 %, Mod Rate 0.5 Hz, early diffusion on, input and output
gain unity. Compensating for a damping difference by moving any of these turns
the comparison into a different question.

---

## Route 1 — the offline set (do this first)

Blind, level-matched, reproducible, and it needs no host.

```powershell
.\Scripts\export-damping-listening.ps1 -BuildDir C:\JuceBuilds\av-phase7
```

54 WAVs plus two text files land in `renders\damping-ab\`. The folder is
gitignored; the set is reproducible from any checkout and the repository
carries no audio.

- `MANIFEST.txt` — read this first. It carries the fixed settings, the
  difference table above, the six questions as a form, and the decision rule.
- `KEY-do-not-open-first.txt` — the letter mapping and every per-file
  measurement. **Do not open it until the six answers are written down.**

The two sides of each pair are written as `1` and `2`, the mapping is permuted,
and the permutation differs between sources so an order learned on one does not
carry to the next. Per-file loudness lives in the key and not the manifest,
because a level column is the mapping written in another alphabet.

Each pair exists twice. `*-matched.wav` has the louder side brought **down** to
the quieter side's loudness — gain only, never upward, so no file can be pushed
over full scale, and the amount is recorded in the key. Judge level on the
unmatched pair and tone on the matched one.

---

## Route 2 — FL Studio, with the plugin

### Loading it without installing it

The validation artifact is at

```
C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
SHA256  2C52D78F701A3024167A5651A6A90D5F5ADB971B147B30871631365715BA6151
```

Add **that folder** as an extra plugin search path in FL Studio
(*Options → Manage plugins → Plugin search paths*) and rescan. Do not install
globally: the production target `C:\Program Files\Common Files\VST3` must not
be written to, and a search path reaches the artifact without touching it.

A sweep of this machine confirms there is no installed `AuroraVerb.vst3`
anywhere — not at the production target, not at the dev-preview location
`%LOCALAPPDATA%\Programs\Common\VST3` that
`Docs/FlStudioTestChecklist.md` names, nowhere outside the two build trees. So
the installed-artifact hash invariant `2B45…0BD8` is preserved by there being
nothing to overwrite, not by a check that passed.

### Getting side A

**B is now the default**, so the plugin opens at B. For A, set High Damp to
50 % and High Damp Freq to 6 kHz by hand and change nothing else.

Use one plugin instance and switch those two controls, rather than two
instances or two artifacts. Two instances differ in modulation phase; two
artifacts differ in build configuration. One instance switching two values
differs in exactly the thing under test.

For the record, the older `Release` artifact in the same build tree
(`75834d2e…`, 5 Nov) is DSP-identical to the new one apart from these two
defaults — `git log` shows commit `44ae4f6` as the only change under `Source/`
since it was built. It is still the wrong way to run this A/B, for the reason
above.

### Material

The dry sources are rendered by the same tool:

```powershell
.\Scripts\prepare-fl-test.ps1
```

`Sources\` then holds the deterministic set — `snare`, `clap`, `piano-chord`,
`vocal`, `synth-pad`, `pluck`, `noise-burst`, `stereo`, `impulse`. Level-match
A against B before judging, and do not tune anything during the comparison.

---

## The six questions

Answer all six for every source, on the wet pairs first. **Write the answers
down before opening the key.**

1. Does B sound noticeably brighter than A?
2. Does B preserve low-mid / mid body better?
3. Does the tail remain silky, lush and continuous?
4. Does B still provide perceptually useful HF damping, or does it become too
   open / exposed?
5. Are transients and source separation at least as clean as A?
6. Does modulation remain organic and non-chorusy?

### Answers

| source | 1 brighter | 2 body | 3 tail | 4 HF damping | 5 transients | 6 modulation |
|---|---|---|---|---|---|---|
| mixed | | | | | | |
| snare | | | | | | |
| hats | | | | | | |
| pad | | | | | | |
| piano | | | | | | |
| vocal | | | | | | |

---

## The decision

| | |
|---|---|
| **PASS** | B is equal or preferable overall, with no material new tonal or modulation defect |
| **CONDITIONAL PASS** | B is clearly technically preferable and only a very small voicing difference exists, not enough to justify another DSP iteration |
| **FAIL** | an obvious undesirable product-level character change: harsh or brittle HF tail, noticeably weaker damping function, loss of lushness, metallic decay, transient smearing, or an obvious modulation / chorus character |

**Verdict:** _not yet taken_

On FAIL: do not tune anything. Record which source and which perceptual
dimension failed, and stop there.

---

## What this gate does not cover

It is a voicing comparison of two damping defaults and nothing else. Host
behaviour — block sizes, sample rates, automation, scanning, the transport —
is `Docs/FlStudioTestChecklist.md`, which is a separate pass and is not
superseded by this one.

Phase 7 is not complete until this verdict is written, the final documentation
is updated with the accepted limitations, and the remaining conditions of the
Phase 7 gate are met.
