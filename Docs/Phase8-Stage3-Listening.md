# Phase 8 — Stage 3 listening set

Prepared, not evaluated. Phase 8 is **not** declared complete.

---

## 1. Validation artifact

```
C:\JuceBuilds\plugindemo\AuroraVerb_artefacts\Release\VST3\AuroraVerb.vst3
  \Contents\x86_64-win\AuroraVerb.vst3
```

| | |
|---|---|
| SHA256 | `BE4807868E2DC7A5FE67BA12C272D922B4D32B0CCFAA43572FD6E0109D73644C` |
| Size | 6 161 408 bytes |
| Built | 2026-11-14 12:21 |
| Profile | **validation** |
| `AV_DEV_DIAGNOSTICS` | ON |
| `AV_DEV_AUDIO_CAPTURE` | OFF |
| `AV_DEV_ALLPASS_CANDIDATE` | 1 — the shipped architecture |
| `AV_COPY_PLUGIN_AFTER_BUILD` | **OFF** |
| `AV_BUILD_TESTS` | ON |
| Generator / config | Visual Studio 17 2022, x64, Release |

**Commit correspondence.** Built from `63405bc`, with a clean working tree at
build time. `git diff --name-only bd04907 -- Source/ CMakeLists.txt` returns
**zero files**: Stage 3 touched only the test target, `Tests/CMakeLists.txt`
and a script. So this binary is the Stage 2.5 plugin, built in the validation
profile.

**Nothing was installed.** `%LOCALAPPDATA%\Programs\Common\VST3\AuroraVerb.vst3`
does not exist.

### One thing to know before loading it in a host

`C:\Program Files\Common Files\VST3\AuroraVerb.vst3` exists as a **leftover
junction from the Phase 7 FL workflow**, and it does **not** point at this
build:

```
target  C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
SHA256  4B25281AA97209E54073DB679AD6A564ED8A0CCAB6697E8D0D6803BEEE9DC7DE
dated   2026-11-13 15:42
```

So FL Studio would load that older binary, not the Stage 3 artifact. It was
left alone: repointing it needs elevation under Program Files, and Stage 3's
deliverable is the offline render set, which needs no host. Say the word and I
will repoint it to the path above, verifying the hash through the link as the
Phase 7 workflow does.

---

## 2. Listening render folder

```
c:\Users\Demirhan Özdemir\Desktop\iş\plugindemo\renders\phase8-stage3
```

39 files: 33 renders, 4 dry references, `MANIFEST.txt`,
`KEY-do-not-open-first.txt`. 48 kHz, 32-bit float stereo. Regenerate with
`.\Scripts\export-phase8-listening.ps1`.

Rendered through the **production** `StereoCutFilters` and `PreDelay` classes
in the processor's own order — `input → Low Cut → High Cut → Pre-Delay → tank
→ trim → width → mix` — not a reimplementation. The dry path never passes
through either stage, which is the structural property Stage 2 was built
around.

---

## 3. Held fixed in every render

Decay 2.5 s · Size 60 % · Early Diffusion 70 % · Low Damp 20 % @ 200 Hz ·
High Damp 45 % @ 10 kHz · Width 100 % · Mod Depth 30 % · Mod Rate 0.5 Hz ·
unity in/out gain. These are the product's own defaults. Case 4 varies High
Damp, and nothing else anywhere varies.

---

## 4. The four cases

### Case 1 — Pre-Delay · 0 / 20 / 80 ms

`snare` at Mix 30 % and wet, `vocal` at Mix 30 %. Low Cut and High Cut OFF.

Measured wet onset, relative to the 0 ms render, as the time the wet signal
first reaches 2 % of its peak:

| setting | onset shift |
|---|---|
| 0 ms | 0.00 ms |
| 20 ms | **20.00 ms** |
| 80 ms | **80.00 ms** |

Wet-only peak is identical at all three (0.1789) — pre-delay changes timing
and nothing else.

### Case 2 — Low Cut · 20 / 200 / 400 Hz

`bass` at Mix 30 % and wet, `vocal` wet. Pre-Delay 0, High Cut OFF.

| render | loudness vs OFF | energy < 200 Hz vs OFF |
|---|---|---|
| bass, Mix 30 %, 200 Hz | −0.31 dB | −0.30 dB |
| bass, Mix 30 %, 400 Hz | −0.44 dB | −0.44 dB |
| bass, wet, 200 Hz | **−7.84 dB** | **−9.69 dB** |
| bass, wet, 400 Hz | **−11.96 dB** | **−14.96 dB** |
| vocal, wet, 200 Hz | −0.53 dB | −2.39 dB |
| vocal, wet, 400 Hz | −1.29 dB | −4.48 dB |

**0 of 6 comparisons near-identical.**

### Case 3 — High Cut · 20k / 10k / 6k Hz

`hats` at Mix 30 % and wet, `snare` wet. Pre-Delay 0, Low Cut OFF.

| render | loudness vs OFF | energy > 3 kHz vs OFF |
|---|---|---|
| hats, Mix 30 %, 10 kHz | −0.20 dB | −0.20 dB — **null trial** |
| hats, Mix 30 %, 6 kHz | −0.27 dB | −0.27 dB |
| hats, wet, 10 kHz | **−3.65 dB** | **−3.73 dB** |
| hats, wet, 6 kHz | **−6.71 dB** | **−6.86 dB** |
| snare, wet, 10 kHz | −2.27 dB | −2.65 dB |
| snare, wet, 6 kHz | −4.10 dB | −4.87 dB |

**1 of 6 near-identical**, and it is a finding rather than a flaw: at the
shipping Mix on an already-bright source, a 10 kHz High Cut is marginal. The
arithmetic is the same one Phase 7 recorded — at 30 % wet a change confined to
the reverb arrives scaled by 0.3 under 70 % of something unchanged.

### Case 4 — High Cut against High Damp · **blind**

`hats`, wet only, three versions, written both as rendered and
loudness-matched. The one place in the set where matching is used, because
this case is about the controls' **role** and a level difference is the
fastest way to mistake "quieter" for "different in kind".

| | setting |
|---|---|
| A | High Cut OFF, High Damp 45 % — the default |
| B | High Cut **8 kHz**, High Damp 45 % |
| C | High Cut OFF, High Damp **55 %** |

C's High Damp is chosen by measurement: the value whose high-band level late
in the free tail lands closest to B's (0.59 dB).

High-band level (> 6 kHz) in the free tail, after the source stops:

| | early tail | late tail | fall |
|---|---|---|---|
| A | −36.92 dB | −60.36 dB | −23.44 dB |
| B | **−41.85 dB** | −64.46 dB | −22.61 dB |
| C | −38.57 dB | −63.88 dB | **−25.31 dB** |

B and C are matched on the late column by construction, so the distinction
lives in the other two, and it appears exactly as the architecture predicts:
**B starts 4.9 dB darker and falls like A. C starts near A and falls 1.9 dB
further.** Uniformly darker against brighter-then-darkening.

---

## 5. What the listener should compare

The manifest carries the full instructions and the answer sheet. In short:

1. **Case 1** — does 20 ms separate the transient usefully, is 80 ms still
   clean rather than a slap, and does the **dry** hit arrive at the same
   instant in all three? Pre-Delay is wet-only; a dry shift is a defect.
2. **Case 2** — does 200 Hz read as low-end cleanup of the *tail* while the
   source's own low end is untouched, and is 400 Hz still usable? Any hollow
   resonance or ringing is a finding — a first-order pole has no resonance.
3. **Case 3** — is 10 kHz a gentle darkening and 6 kHz a usable darker, with
   no brittle edge and no HF collapse?
4. **Case 4** — on the `-matched` files: which of 1, 2, 3 is untreated; of the
   other two, which is uniformly dark and which starts brighter and darkens;
   and do they feel like two controls or two copies of one? **Write the answers
   down before opening the key.**

Acceptance is in the manifest and it does not require any setting to be
preferable. An extreme setting sounding extreme is not a defect. On FAIL,
record the case, the setting and the dimension, and stop — Phase 7 damping is
closed and is not to be retuned on a Phase 8 listening result.

---

## 6. Sources, and two additions

| | class | provenance |
|---|---|---|
| `snare` | percussive | established FU-2 material |
| `vocal` | vocal-like | established, the export set's reference |
| `hats` | bright percussion | added in Phase 7; noise-based, no resonator |
| `bass` | bass-heavy | **new for this set** |

`bass` exists for the reason Phase 7 added `hats`: a gate that cannot reveal
the change it gates is not a gate. Low Cut at 200 and 400 Hz can only act
below those frequencies, and the lowest content anywhere in the established
material is a 220 Hz fundamental. It is a 55 Hz plucked bass figure with a
kick-like thump and quiet mid content, deterministic, with no resonator and no
filter in it.

`makeBrightPercussion` was made visible rather than copied, so the bright
source cannot drift into two versions. Its arithmetic is unchanged, so every
earlier render made from it stays reproducible.

---

## 7. Measurement errors made and corrected while building this set

Four, all caught by looking at the numbers before handing the set over.

1. **Case 4 matched on an unreachable quantity.** Matching High Damp to the
   8 kHz High Cut on whole-render HF/LF balance ran to the top of the
   parameter range still 0.64 dB short. That is not a grid too narrow — it is
   the case's own finding, that High Damp cannot reproduce a High Cut however
   hard it is driven, because a cut keeps the high band out of the tank while
   damping only shortens how long it survives once in.
2. **Case 4's discriminator was diluted.** A high-band centroid over the whole
   render separated the two controls by 46 ms out of 3 s — 1.5 %, not a
   discriminator — because a 6 s source and a 3.5 s tail put the centroid
   mostly inside the source. Replaced with early- and late-tail band levels
   measured in the free tail.
3. **Then the late window was inaudible.** At 1.5–2.5 s after the source the
   high band sits near −95 dB. Matching there is matching on residue, and it
   chose a High Damp barely above the default. Moved to 0.6–1.2 s.
4. **Two tilt measures saturated.** A band *share* of the total cannot move on
   a source that is almost entirely one side of the split, which is why
   `hats` at Mix 30 % read −0.00 dB while its loudness moved. Replaced with
   absolute band levels. And the Case 3 split sat at 6 kHz, exactly on the
   6 kHz High Cut it was trying to see; moved to 3 kHz.

One source choice was overturned by measurement: Case 3 was to use `vocal`,
which turned out to hold 12 dB less energy above 3 kHz than below and moved by
0.05 dB at a 10 kHz cut — a null trial. Replaced with `snare`, and the vocal's
inability recorded rather than worked around.

---

## 8. Not done, deliberately

- The listening evaluation itself. The user performs it and reports.
- Phase 8 closure. Not declared.
- Phase 9. Not started.
- The Program Files junction. Left pointing where it was.
