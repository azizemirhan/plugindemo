# Reverb benchmark and feature parity

**Created 2026-08-24, before Phase 7 (damping). Living document.**

## What this is

The Valhalla products are used here as a **yardstick**, in three separate
senses: feature coverage, quality, and workflow. They are a good yardstick
because they are mature, widely trusted, and cover the algorithmic-reverb space
broadly enough that a gap against them is usually a real gap rather than a
matter of taste.

**This is not a cloning target.** Aurora's algorithm, GUI, parameter naming, mode
names and character stay original. Nothing in this document authorises copying a
competitor's algorithm, topology, preset values, visual design or nomenclature.
What is borrowed is the *checklist*: what a professional algorithmic reverb is
expected to do, and how well.

Three layers, kept deliberately separate because they fail independently:

- **A. Core feature parity** — does the control exist and work?
- **B. Quality parity** — is what it does good enough to ship?
- **C. Aurora differentiation** — what makes it worth choosing, above parity.

A product can pass A and fail B completely. Aurora currently does something more
unusual: it has an unusually strong B for its stage, and a weak A.

---

## The single most important fact in this document

**Aurora has 19 host-visible parameters. Seven of them reach the audio path.**

Live: **Decay, Size, Early Diffusion, Mod Rate, Mod Depth, Width, Mix.**

Inert — present in the parameter tree, visible in the host, automatable,
saved in the session, smoothed on the audio thread, and connected to nothing:
**Pre-Delay, Late Diffusion, Attack, Density, Low Damp, High Damp, Low Damp
Freq, High Damp Freq, Low Cut, High Cut, Mode, Color.**

Pre-Delay deserves separate mention: it is read in `getTailLengthSeconds()`, so
the plugin *reports* a tail length that accounts for a pre-delay it does not
apply.

This is not a missing feature. It is a **correctness gap on a shipping
artefact** — a user can automate a control that does nothing, and nothing tells
them. It is the largest single item facing v1.0, and it is bigger than damping
alone.

---

# A. Core feature parity

Status vocabulary: **COMPLETE** — works in the audio path and has been verified.
**PARTIAL** — works, but not to shipping scope. **NOT STARTED** — no code.
**PLANNED** — no code, but a phase owns it.

*Inert* in the notes column means: the parameter exists and is automatable, but
does not affect audio.

| Feature | Aurora status | Phase | Blocker | v1.0 | Notes |
|---|---|---|---|---|---|
| Mix | **COMPLETE** | 5F | — | REQUIRED | `wet = m^0.799604`, `dry = cos(m·π/2)`. Host-verified. |
| Decay | **COMPLETE** | 5C | — | REQUIRED | RT60-calibrated, loop-length corrected. Host: 0.5 s / 5 s clearly distinct. |
| Size | **COMPLETE** | 5C | — | REQUIRED | Dual-bank morphing, click-free, compensated. Host: audible but subtle. |
| Pre-delay | **NOT STARTED** | 7 | **yes** | REQUIRED | Parameter exists, inert; already counted in reported tail length. |
| Width / stereo | **COMPLETE** | 5C | — | REQUIRED | Wet-only side scaling. Host: 0 %↔100 % clearly different. |
| Mod Rate | **COMPLETE** | 5C | — | REQUIRED | Drives both tank modulation engines. |
| Mod Depth | **COMPLETE** | 5C | — | REQUIRED | Works, but **perceptually weak** — see B and C. |
| Early reflections | **NOT STARTED** | unnumbered backlog | **yes** | REQUIRED | No parameter, no code. Lost its old number 6. |
| Early / late balance | **NOT STARTED** | with early reflections | **yes** | REQUIRED | No parameter. Meaningless until ER exists. |
| Early Diffusion | **COMPLETE** | 3 | — | REQUIRED | Drives tank input diffusion. Off-default harness parity item is backlog, not a defect. |
| Late Diffusion | **NOT STARTED** | 7 or later | **yes** | REQUIRED | Parameter exists, inert. `DiffusionNetwork` exists and is unwired to it. |
| Density | **NOT STARTED** | with algorithm families | no | REQUIRED | Parameter exists, inert. Meaning is algorithm-dependent by design. |
| Attack / Bloom | **NOT STARTED** | with Nonlinear family | no | OPTIONAL | Parameter exists, inert. Primary control only in the gated/reverse family. |
| Low damping | **NOT STARTED** | **7** | **yes** | REQUIRED | Parameter exists, inert. Default 20 %. |
| High damping | **NOT STARTED** | **7** | **yes** | REQUIRED | Parameter exists, inert. Default 50 %. |
| Damping crossover controls | **NOT STARTED** | **7** | **yes** | REQUIRED | `lowDampFreq` 200 Hz, `highDampFreq` 6 kHz. Both inert. |
| Frequency-dependent decay | **NOT STARTED** | **7** | **yes** | REQUIRED | The substance behind the four controls above. Seam reserved in `FdnCore`. |
| Low cut | **NOT STARTED** | 7 | **yes** | REQUIRED | Parameter exists, inert. Tone shaping, distinct from damping. |
| High cut | **NOT STARTED** | 7 | **yes** | REQUIRED | Parameter exists, inert. |
| Multiple algorithms / modes | **NOT STARTED** | 8–15 | **yes** | REQUIRED | 13 mode names in the enum; one engine behind all of them. Mode is inert. |
| Hall | **PARTIAL** | 8 | **yes** | REQUIRED | The current 8×8 FDN *is* a hall-class tank; it is not selectable or voiced as one. |
| Room | **NOT STARTED** | 9 | no | REQUIRED | |
| Plate | **NOT STARTED** | 10 | no | REQUIRED | |
| Chamber | **NOT STARTED** | 11 | no | OPTIONAL | Closest neighbour to Room/Hall; first candidate to cut if v1.0 needs scoping. |
| Ambience | **NOT STARTED** | 13 | no | REQUIRED | Short/ambient is a distinct use case, not a short hall. |
| Nonlinear / gated family | **NOT STARTED** | 14 | no | OPTIONAL | Distinctive but narrow. Needs Attack to mean something. |
| Color / era / character | **NOT STARTED** | 16 | no | REQUIRED | 3 names (Classic / Retro / Modern), inert. See C — 3–5 families is the target. |
| Presets | **NOT STARTED** | 18 | **yes** | REQUIRED | State save/load works; there is no factory bank. A reverb with no presets is not shippable. |
| Parameter automation | **COMPLETE** | 1 / 5C | — | REQUIRED | APVTS, stable IDs, three smoothing laws, click-free. **Caveat: automating an inert parameter is silently inert.** |
| Resizable production GUI | **NOT STARTED** | 19 | **yes** | REQUIRED | Current editor is generic development controls. Concept work is safe now — see E. |

### A — where the count lands

| | |
|---|---|
| COMPLETE | 8 of 29 |
| PARTIAL | 1 |
| NOT STARTED | 20 |
| REQUIRED for v1.0 and not complete | 19 |
| Blockers | 15 |

---

# B. Quality parity

Deliberately separate from A. **A feature that exists badly is worse than one
that does not exist**, and Aurora's quality work is far ahead of its feature
work. Nothing verified in Phase 5 or Phase 6 is reset to "not started" here.

| Quality target | Status | Evidence |
|---|---|---|
| Smooth decay | **COMPLETE** | RT60 calibration and Size compensation, Phase 5C; host-confirmed at 0.5 s and 5 s. |
| Low modal ringing | **COMPLETE for the current engine** | Phase 6 FU-1: 60-mode map, 2814 peaks above a 10 dB prominence floor, q-space Size tracker, A/B/C/D classification. Verdict CHARACTER ACCEPTABLE. |
| Controlled metallicity | **COMPLETE — conditionally** | Six blind rounds (1, 1b, 1c, 2, 2b, 2c), pre-registered analysis, withheld keys. Not an objectionable voicing defect in shipping use. **Re-check under Phase 7**, which acts on the same region; Round 2c's nine renders are the ready-made comparison. |
| Transient preservation | **PARTIAL** | Host pass: dry snare transient falls away naturally leaving tank character, no smear reported. Never measured; never tested on real recordings. |
| Stable RT60 | **COMPLETE** | Loop-length-aware decay gain; the ~1.7 % overshoot was found and closed in Phase 5C. **Must be re-measured under damping.** |
| Frequency-dependent decay correctness | **NOT STARTED** | Nothing to be correct about yet. Phase 7's core deliverable. |
| Stereo decorrelation | **COMPLETE** | `StereoDiffusionNetwork` — structural decorrelation, not M/S widening. Searched injection/readout vectors. |
| Mono compatibility | **PARTIAL** | Structural decorrelation rather than phase tricks means it should behave, and width acts on the wet side signal only. **Never explicitly measured.** Cheap to close; should be closed before v1.0. |
| Modulation without unintended pitch wobble | **COMPLETE** | Phase 5B/5C measured detune in cents across candidates; the shipped energy-preserving allpass was selected on that evidence. |
| Gain staging | **COMPLETE — with a standing condition** | Phase 5F FU-2. `previewBaseTrim` 1.0. Host reports Mix 100 % as slightly quiet but usable; raising the trim still depends on widening the evidence beyond a single program signal. |
| Automation safety | **COMPLETE** | Click-free Size morphing, allpass engagement crossfade, three smoothing laws chosen per parameter kind. |
| Parameter smoothing | **COMPLETE** | Explicit per-kind laws; smoothing runs even for inert parameters. |
| No clicks / crackles | **COMPLETE** | Host regression: bypass, re-enable, and every parameter sweep clean. |
| Denormal safety | **COMPLETE** | `ScopedFtz`, additive guard, `flushDenormal`. |
| No NaN / Inf | **COMPLETE** | Test suite plus pluginval; no host anomaly across Phase 5F and 6 sessions. |
| No audio-thread allocation | **COMPLETE** | Proven by a heap-allocation detector, not by inspection. Single allocation point in `prepareToPlay`. |
| CPU target | **COMPLETE for the current engine** | ~1.05 % measured for the shipped configuration. **Will rise in Phase 7** — 8 damping filters inside the feedback loop. Re-measure, do not assume. |
| pluginval strictness | **COMPLETE** | Passes at strictness 10. |
| Deterministic regression | **COMPLETE** | 2121 / 2121 checks at the shipping source state. |
| Host compatibility | **PARTIAL** | FL Studio verified twice, in depth. No second host has been tried. Should be widened before v1.0. |
| Preset / state compatibility | **PARTIAL** | APVTS XML round-trips; parameter IDs are stable and documented as part of saved state. Untested across a version change, and there is no factory bank to be compatible with yet. |

### B — where the count lands

| | |
|---|---|
| COMPLETE | 15 of 21 |
| PARTIAL | 5 |
| NOT STARTED | 1 |

**This is the inverted picture, and it is the right way round.** Quality
infrastructure is expensive to retrofit and cheap to maintain; features are the
reverse. Aurora built the hard half first.

---

# C. Aurora differentiation

Above parity, not instead of it. **Product direction, not implementation
commitment.** Nothing here is approved, scheduled, or promised.

### The rule that governs this section

**Do not build 22 modes because a competitor has 22 modes.** Perceptual
distinctness beats count, and Aurora already has hard in-project evidence for
why:

> Mod Depth 0 % ↔ 30 % is a real, working, measurable DSP difference. Six blind
> listening rounds could not establish that listeners hear it, and the host pass
> independently described the difference as barely audible.

A control that exists and cannot be heard is not a feature. Applied to modes:
a mode earns its place by sounding *unmistakably* like itself in a blind
comparison, or it does not ship. The Phase 6 listening apparatus — blind keys,
pre-registered analysis, content-free render checks, counterbalanced ordering —
already exists and is the natural instrument for that test.

### Candidate behaviour families

Evaluated later, none committed. Names are working labels.

| Family | Idea |
|---|---|
| **Bloom** | Tail that swells rather than starting at full level. Needs Attack to be real. |
| **Motion** | Slow, musical movement in the tail that is not vibrato. |
| **Spectral Motion** | Movement across frequency rather than time — bands decaying at drifting rates. Naturally downstream of Phase 7. |
| **Dream** | Long, soft, heavily diffused, minimal transient definition. |
| **Neon** | Bright, present, deliberately coloured. |
| **Cloud** | Very high density, no discernible structure. |
| **Void** | Extremely long, extremely sparse. |
| **Glass** | Deliberately resonant. **Note the irony:** Phase 6 spent an entire follow-up establishing that Aurora's metallic character is acceptable rather than a defect. That character is a candidate *feature* under intentional control. |
| **Dust** | Granular, textured, degraded. |

### v1.0 product target

| | |
|---|---|
| Genuinely distinct reverb / creative modes | **8–12** |
| Color / character families | **3–5** |
| Aurora-exclusive creative functions | **a few** — quality over quantity |

"Genuinely distinct" is a testable claim, and the test is blind listening, not a
feature list.

---

# D. Phase 7 connection

**Phase 7 = Damping.** Confirmed as the next real DSP phase, and not an
arbitrary choice: the seam already exists in the code.

`FdnCore` reserves a per-line processing slot between the delay read and the
matrix — `processLineState`, documented as *"Seam for PHASE 7 per-line damping.
Compiles to nothing today."* It is a real hook, not a comment, and it costs
nothing at present. **It must not be modified ahead of Phase 7.**

### Parity items Phase 7 closes

| Item | From | To |
|---|---|---|
| Low Damp | inert | live, default 20 % |
| High Damp | inert | live, default 50 % |
| Low Damp Freq | inert | live, default 200 Hz |
| High Damp Freq | inert | live, default 6 kHz |
| Frequency-dependent decay | NOT STARTED | the foundation every later algorithm family builds on |

Reasonably in scope alongside them, since they are tone shaping on the same
signal and would otherwise stay inert for no reason: **Low Cut, High Cut,
Pre-Delay.** Scope decision belongs to Phase 7 planning, not to this document.

### Phase 7 correctness gap — flagged

> **The four damping parameters are present in the shipping plugin, visible in
> the host, automatable and saved in session state, and are connected to no
> DSP.** A user can automate High Damp across a whole arrangement and hear
> nothing change, with no indication that this is expected.

This is recorded as a **correctness gap**, not a missing feature. The wider gap
is the same defect across twelve parameters — see the top of this document. It
is the same *class* of problem the shipping/build hardening work closed: the
artefact's behaviour not matching what it presents.

### Phase 7 obligations carried in from other phases

1. **Re-measure RT60.** Damping changes loop gain per band. Phase 5C's
   correction was measured, not assumed, and closed a ~1.7 % overshoot; it has
   to be re-established under damping. Decay must still mean what it says at mid
   frequencies.
2. **Re-check the FU-1 high-band character.** Phase 6 explicitly deferred this
   here. Round 2c's nine renders are the ready-made comparison set, and the v4
   mode table is the regression baseline.
3. **Re-measure CPU.** Eight filters inside a feedback loop is not free.
4. **Stability.** No band's loop gain may reach 1. Denormal behaviour must hold
   in the damped path.

---

# E. UI concept — safe to start, in parallel

**Recorded as safe.** Concept work on the interface may begin alongside Phase 7.

It is safe because the control surface is frozen: all 19 parameter IDs are
established and are part of saved state and host automation, and **Phase 7 adds
no new parameters** — all four damping controls already exist. Layout work done
now will not be invalidated by Phase 7.

### Safe now

- Overall layout and information hierarchy
- Knob hierarchy — which controls are primary, secondary, hidden
- Typography
- Aurora visual identity
- Control grouping
- Light / dark visual exploration
- Responsive / resizable layout concept

### Not yet — with the reason

| Do not | Why |
|---|---|
| Start `PluginEditor` implementation | That is Phase 19. Concept output stays as documents and mockups. |
| Show Mode or Color as working features | Both are inert. 13 mode names and 3 colour names exist in the enum; one engine sits behind all of them. A mockup implying otherwise will be believed. |
| Lock spectral / tail visualiser calibration | Phase 7 changes the tail spectrum directly. Design the component; do not calibrate it. |

### One design consequence worth carrying forward

Twelve of nineteen controls currently do nothing. Until they are wired, **any
mockup showing the full panel is showing a mostly non-functional plugin.** Two
options are worth thinking about at concept stage rather than discovering later:
design the interface around progressive disclosure, so incomplete areas are
absent rather than dead; or accept that the UI cannot ship before the parameters
are live, and let that set the ordering. This is a note for the concept work,
not a decision.

---

# v1.0 estimate

Judged against parity, not against a date.

| | |
|---|---|
| Quality foundation | **~85 %** — 15 of 21 complete, 1 not started |
| Feature surface | **~30 %** — 8 of 29 complete, 15 blockers |
| Overall feature parity for v1.0 | **~35 – 40 %** |

Weighted toward features on purpose: that is where the distance is.

### What the remaining distance actually consists of

1. **Wire the twelve inert parameters.** Phase 7 takes the damping four plus,
   probably, cuts and pre-delay. The rest follow their own phases.
2. **Early reflections and early/late balance.** The largest genuinely absent
   feature — no parameter, no code.
3. **Make Mode mean something.** Eight to twelve modes that pass a blind
   distinctness test. The single largest body of work in the project.
4. **Colour families.** Three to five.
5. **A factory preset bank.** A reverb without presets does not ship.
6. **A production GUI.** Concept may start now; implementation is Phase 19.

### Closing the quality PARTIALs is cheap — do it opportunistically

Mono compatibility (never measured), a second host, transient preservation, and
state compatibility across a version change. None is a phase; each is a
measurement. They should be picked up alongside whatever phase is running,
before they become v1.0 blockers.

---

## Maintenance

Update this document when a phase closes, not continuously. Every status change
must cite evidence — a measurement, a test result, or a host session. **A status
must never be downgraded to NOT STARTED for work that has evidence behind it**,
which is why Phase 5 and Phase 6 results are recorded here explicitly rather
than left to be re-derived.
