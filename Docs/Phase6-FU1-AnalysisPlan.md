# Phase 6 — Aurora Sound / Voicing & Character
## FU-1: modal / bell-like tonal character — analysis plan

> ## STATUS: FU-1 COMPLETE — CLOSED
>
> **Verdict: CHARACTER ACCEPTABLE.** The character is audible and real; it is
> not demonstrated to be an objectionable defect in shipping use. No production
> DSP change was made, the modal response is not flattened, no mode is targeted,
> Mod Depth is unchanged, and the Size-based localisation experiment will not be
> run.
>
> This plan's own condition was: *"No production DSP change until the item 7
> table exists and has been read."* The table exists — it is
> [`../Phase6ModalMap-v4/mode-table.csv`](../Phase6ModalMap-v4/mode-table.csv),
> read in [`Phase6-FU1-Interpretation.md`](Phase6-FU1-Interpretation.md) and
> taken to listening in [`Phase6-FU1-Listening.md`](Phase6-FU1-Listening.md).
> It was read, and the decision it produced is **no change**.
>
> ### What the seven measurements delivered
>
> | # | measurement | outcome |
> |---|---|---|
> | 1 | full-spectrum modal map | **done** — 60 detailed modes at the reference, 2814 peaks above the 10 dB floor |
> | 2 | Size sweep | **done, and redone** — the first tracker lost identity on this axis; replaced by the validated q-space tracker and regenerated as v4 |
> | 3 | Decay sweep | **done** — 0.5 to 20 s |
> | 4 | Mod Depth 0 vs default | **done** — full-list matching plus per-mode local response, measured 60/60 |
> | 5 | source comparison | **partial** — three full-band sources at Mix 100% and Mix 30% were rendered and *listened to* in Round 2c, but no spectral source-comparison table was produced, and the sustained tone aimed at a known mode was deliberately never used |
> | 6 | stereo correlation and modal movement | **partial** — per-band correlation and L/R energy are in the mode table for 60/60 modes; separate L and R modal maps were never produced |
> | 7 | fixed versus moving | **done** — the A/B/C/D table, regenerated from correct Size evidence |
>
> Two results the plan did not anticipate, both recorded rather than worked
> around:
>
> - **Per-mode modal decay is infeasible for 60 of 60 modes.** The tank's modal
>   density leaves no window that can both separate a neighbour and fit a decay.
>   The plan's central discriminator — "a mode that outlives the tail by a factor
>   of two is the defect" — could therefore never be evaluated. This is a
>   physical identifiability limit, not a gap left open.
> - **Class C is empty.** Nothing in the tank measures as size-insensitive. The
>   five modes that looked so under the first Size tracker were its artefacts.

Measurement only. No production DSP change, no new voicing algorithm, nothing in
`Source/` touched. FU-2 and everything Phase 5F settled stays as it is.

**The goal is not a flat reverb.** Aurora's target is lush, silky, dense,
blooming — and every one of those words describes a response that is *not*
flat. Modal density and modal colour are what produce them. So the question this
plan has to answer is not "where are the resonances" but **"which resonances are
the character, and which are the defect"** — and those cannot be separated by
looking at a spectrum alone. Anything that flattens the response blindly would
remove the product along with the problem.

---

## What is already established

From the FU-2 renders, relative to the 220 Hz fundamental, wet minus dry:

| boosted | dB | suppressed | dB |
|---|---|---|---|
| 660 Hz | +15.60 | 1100 Hz | -15.09 |
| 55 Hz | +14.59 | 2032 Hz | -12.90 |
| 254 Hz | +11.95 | | |
| 275 Hz | +11.68 | | |
| 3225 Hz | +9.68 | | |

Four bins gain 10-16 dB and two lose 13-15 dB. A flat tank would move them
together, so this is modal colouration and it is in the wet path.

Three limits on that, unchanged and still binding: seventeen hand-picked
frequencies rather than a spectrum; the boosts are lower bounds because
modulation smears energy out of a 0.33 Hz bin; and the tone scan's worst points
(254 Hz, 3225 Hz) were measured at Size 100% while those renders were Size 60%,
so the apparent correspondence is a lead, not a confirmation.

Closing all three is the first job below.

## What the delay set predicts, before anything is measured

`fdn::delaysMs` = 26.27, 30.38, 38.10, 42.58, 47.06, 54.20, 66.99, 82.92 ms.
Total 388.5 ms unscaled.

| Size control | scale | total delay | mean mode spacing | minimum FFT window to resolve |
|---|---|---|---|---|
| 0.0 (min) | 0.2500 | 97.1 ms | **10.30 Hz** | 0.10 s |
| **0.60 (default)** | **0.8706** | **338.2 ms** | **2.96 Hz** | **0.34 s** |
| 1.0 (max) | 2.0000 | 777.0 ms | **1.29 Hz** | 0.78 s |

Two consequences the plan is built around.

**1. The existing persistence metric cannot do this job.**
`modalPeakPersistence` counts a peak as surviving if the next frame has one
within ±4 Hz. At the default Size the mean spacing is 2.96 Hz, so the tolerance
is **1.35× the spacing** — it cannot tell a mode from its neighbour, and it
cannot tell a mode that moved from a different mode that was always there. This
is the same tolerance that made the Mod Depth A/B report *higher* persistence
with modulation on. The metric is not wrong; it answers a coarser question than
FU-1 asks. **A frequency-tracking measurement has to be built, not borrowed.**

**2. Resolution and modulation pull in opposite directions.**
Resolving modes at max Size needs a window of order 1 s. The modulation moves
modes during that window, which smears them. Every measurement below therefore
has to be run at Mod Depth 0 as well as at the default, and the pair read
together — that is what item 4 exists for, and it is not optional.

---

## The seven measurements

Every one runs on the **current production build**, unmodified: `previewBaseTrim`
1.0, Aurora mix law, the shipping FDN, modulation, damping and diffusion.

### 1. Full-spectrum modal map

Replaces the seventeen hand-picked bins with an actual spectrum.

- Source: impulse at Mix 100%, so the tail is isolated from programme material.
  This is what `AnalyseAuroraVerbCapture(..., impulsive = 1)` exists for.
- Long window, and stated: at least 4× the minimum in the table above, so modes
  are separated rather than merged. Report the window and the resulting bin
  width alongside every figure.
- Per detected peak: centre frequency, height above the local median (reuse
  `peakAboveMedianDb`), Q or -3 dB width, and decay time of that peak alone.
- Reuse `spectralFlatness` over bands as the summary "how ringing is this"
  number, and `thirdOctaveEnergy` for the broad tilt.

**Expected output.** A peak list, and a judgement about how far it departs from
the design. The delay set was chosen by a directed search for a mean spacing of
about 2.6 Hz, so a healthy map is dense and even. What would explain the
bell-like character is a handful of peaks standing well above their neighbours —
and the useful number is not their height in absolute terms but **height above
the local median and how long each one rings relative to the broadband RT60.**
A mode that is 10 dB up but decays with everything else is colour; a mode that
outlives the tail by a factor of two is the defect.

### 2. Size sweep

Size scales every delay, so every modal frequency scales with it. This is the
measurement that settles whether a given resonance belongs to the tank's
structure or to something fixed.

- Size control 0.0, 0.25, 0.50, 0.60 (default), 0.75, 1.0.
- Same impulse, same Mix 100%, same analysis as item 1.
- Track each strong peak across the sweep.

**Expected output, and the discriminator.** A structural mode moves as `1/scale`
— halve the delays and its frequency doubles. Anything that **stays put** as
Size changes is not an FDN loop mode: it is a filter, a fixed coefficient, or
an artefact, and it is a different fault with a different fix. Report the two
groups separately. This also closes the third FU-2 limitation: it tells us
whether 254 Hz and 3225 Hz at Size 100% and the boosts at Size 60% are the same
modes seen at two scales, which was explicitly left unproven.

### 3. Decay sweep

- Decay 0.5, 1.0, 2.5 (default), 5.0, 10.0, 20.0 s, at the default Size.
- Per decay: the modal map, plus each strong peak's own decay time against the
  broadband RT60.

**Expected output.** Modal frequencies must **not** move with Decay — decay sets
feedback gain, not delay length. If any peak shifts, that is a finding in
itself. What should change is Q: longer decay means higher feedback, narrower
and taller peaks, and the ringing becoming more audible. The number to produce
is **at what Decay the worst peak's own decay time separates from the broadband
figure**, because that is where a listener stops hearing a tail and starts
hearing a note.

### 4. Mod Depth 0 versus default

The control for everything else, and the item that makes items 1-3 readable.

- Identical captures at Mod Depth 0 and at the default 30%.
- Compare peak heights, widths, and per-peak decay times.

**Expected output.** At Depth 0 the modes are stationary and the map is at its
sharpest — this is the tank's true modal structure with nothing hiding it. At
default depth the same modes should be lower and wider, because the energy is
spread over the excursion. **The difference is exactly how much the modulation
is already doing**, which is the first thing to know before proposing that it
do more. If a peak is equally tall at both depths, modulation is not reaching
it, and that is a specific, actionable finding.

### 5. Source comparison: snare / impulse / pad / tonal

- Snare and pad and piano from the existing FU-2 generator, plus a bare impulse,
  plus a sustained tone at a frequency the map says is a strong mode.
- At Mix 100% and at Mix 30%.

**Expected output.** Which sources excite the resonance and which do not, and at
what Mix it becomes audible. The FU-2 measurement already showed that the
`mixed` source's own sustained 220/330 Hz fifth accounts for much of what was
heard as a hum — **so this item exists partly to keep source content and tank
content apart**, which is the mistake most easily made here. The tonal source
aimed at a known mode is the direct test: if driving a mode deliberately
produces the bell the operator described, the mode is identified.

### 6. Stereo correlation and modal movement

- Per-band correlation between channels across the tail, not one broadband
  figure.
- Modal maps of L and R separately: are the strong peaks at the same
  frequencies in both?

**Expected output.** A mode present at the same frequency in both channels with
high correlation is heard as a centred tone — the worst case for a bell. The
same energy spread across decorrelated modes is heard as width. This is why
correlation belongs in a modal investigation at all, and the existing broadband
correlation figure (0.98 in one session, 0.677 in another) is far too coarse to
answer it.

### 7. Fixed versus moving: separating the two populations

The item the other six feed.

- From the Mod Depth pair and the Size sweep, classify every strong peak:
  moves with Size and moves with modulation (a normal loop mode); moves with
  Size but not with modulation (a mode the modulator is not reaching); moves
  with neither (not a loop mode at all).
- Track frequency over time within one capture, at a resolution finer than the
  ±4 Hz the current tool uses, and report the excursion per peak in Hz.

**Expected output.** Three named lists rather than one number, and for each
peak: frequency, height above local median, its own decay time, its excursion
under modulation, and its behaviour under Size. **That table is the deliverable.**
The decision about what, if anything, to change in Phase 6 should be made from
it and not before it.

---

## What has to be built

Reused as-is: `magnitudeSpectrum`, `spectralFlatness`, `peakAboveMedianDb`,
`bandDeviation`, `thirdOctaveEnergy`, `spectrogramOf`, `spectralFlux`,
`measureDecay`, `energyDecayCurve`, `truePeakOfSignal`, the FU-2 source
generators, and `AnalyseAuroraVerbCapture` for the impulsive path.

New, in the test harness only:

- A peak detector that returns a list with centre, height above local median,
  width, and per-peak decay time — the current tools give summary statistics,
  not a list.
- A frequency tracker with a tolerance set from the measured modal spacing
  rather than a fixed ±4 Hz, so a mode can be followed without being confused
  with its neighbour.
- A sweep driver that runs a parameter axis and reports how each tracked peak
  moved along it.

## What the plan will not do

- No production DSP change until the item 7 table exists and has been read.
- No blind flattening. If a change is proposed it will name which peaks it
  targets and predict what it costs the density and bloom, and that prediction
  will be measured and listened to before anything ships.
- No claim about audibility from a spectrum. Every judgement about whether a
  resonance is objectionable stays with the operator, as it has throughout.

---

# Methodological guardrails

Added before implementation. These constrain how every figure above may be
computed and reported.

## 1. Modal spacing and FFT resolution

`1 / total-delay` is an **average modal-density estimate**, not the spacing of
any particular pair of modes. Real spacing varies across the spectrum and the
figure must never be used as if a mode sits every 2.96 Hz.

**Resolving power is set by the analysis window duration, not by the FFT size.**
Zero-padding interpolates the display of a spectrum; it does not separate two
components that the window cannot separate. Every reported spectrum therefore
carries three numbers, kept distinct:

- `windowSeconds` — the untapered data length
- `resolutionHz` — what that window can actually resolve, about `2 / windowSeconds`
  for the Hann window in use
- `binWidthHz` — `sampleRate / fftSize` after zero-padding, for display only

A peak separation smaller than `resolutionHz` is not a measurement of two modes.

## 2. Size tracking

A peak that moves as roughly `1/scale` is **strong evidence** of an FDN loop mode.

A peak that stays put under Size is **not** thereby proven not to be one. Fixed
delays, filters, the allpass bank and interaction between modes can all produce
a size-insensitive feature. Such peaks are labelled
**`size-insensitive / investigate fixed or mixed structure`** and go into class C
for investigation, never into a "not a mode" bin.

## 3. Decay tracking

Decay changes feedback gain, not delay length, so mode centres are expected to
stay approximately fixed.

**Approximately.** A measured centre can shift a little as Q changes and as
neighbouring modes interfere, and requiring zero movement would manufacture
findings out of ordinary behaviour. The tolerance is derived per peak from the
measurement noise floor (guardrail 5), the bin width and the local modal
spacing — whichever dominates. **Only systematic movement above that tolerance
is reported as a finding**, and the tolerance used is reported alongside it.

## 4. Peak tracker

No fixed ±4 Hz nearest-neighbour matching.

Association uses local modal spacing, spectral prominence and trajectory
continuity together. Where two peaks approach or cross, greedy nearest-neighbour
matching swaps their identities, so assignment is global across the frame pair
rather than per peak, scored on all three terms.

There is an explicit **`ambiguous / unresolved`** outcome. When no assignment is
clearly better than its alternatives the peaks are left unmatched and reported
as such. Nothing is force-matched.

## 5. Repeatability

Modulation phase and every seed are locked deterministically. Every sweep point
starts from an identical initial state, the same source and the same render
length, so two runs of the same point differ only through the analysis itself.

At least one point is rendered twice and analysed twice, and the spread of the
tracker's own outputs across that repeat **defines the noise floor**. Every
movement threshold used anywhere in the analysis is then set meaningfully above
that floor, and both the floor and the threshold are reported.

## 6. Per-mode reporting

For each strong mode, where measurable:

centre frequency; prominence above local median; bandwidth and Q; modal decay
time; its ratio to the broadband RT60; L and R energy; band-limited L/R
correlation; frequency excursion from Mod Depth 0 to default; Size trajectory;
and a confidence or ambiguity flag.

Anything not measurable at a given setting is reported as not measurable rather
than estimated.

## 7. Classification is structural, not perceptual

The harness assigns one of four classes and nothing more:

- **A** — size-tracking, modulation-moving
- **B** — size-tracking, modulation-resistant
- **C** — size-insensitive / fixed-or-mixed candidate
- **D** — ambiguous / unresolved

**No "good character" or "bad resonance" label is produced by the measurement.**
That judgement is made later, from the measurements together with blind
listening, and it stays with the operator.
