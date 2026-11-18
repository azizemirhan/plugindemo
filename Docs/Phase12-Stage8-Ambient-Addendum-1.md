# Phase 12 Stage 8 — Ambient: addendum 1 (the first diagnosis, and four more diagnostics)

Addendum to `Phase12-Stage8-Ambient-Repair-Preregistration.md`. Written
after DA0 – DA6 were run (log `Phase12-Stage8-runs/01-ambient-diagnosis.txt`)
and **before** DA7 – DA10 are run. No candidate has been built.

## 1. DA1 – DA6: what they show

Applied by the reading rule as registered.

**Both S and V are present.** Octave deviation from Hall over twelve
perturbations of the delays:

| | 125 Hz mean | 125 Hz SD | Hall's SD (floor) | bands with S | bands with V |
|---|---|---|---|---|---|
| medium | −0.95 | 1.14 | 0.36 | 125, 250, 8k | 125, 250 |
| short | −1.50 | 0.57 | 0.26 | 125 – 2k, 8k | 125, 250, 500 |
| long | +0.47 | 1.10 | 0.14 | none | 125, 250 |

- **The 1.82 dB at the defaults is about half systematic and half the
  draw.** The mean deficit at 125 Hz is 0.95 dB; the rest is where this
  particular delay set happens to fall in a distribution 1.14 dB wide.
  Stage 6's Size sweep, which changed sign four times, was that
  distribution.
- **S has two parts with different causes** (damping off, DA2):
  - the mid-band lift and the 8 kHz loss disappear with the damping off
    (medium: +0.5 → 0.0; 8 kHz −0.64 → −0.10). That is loss met while
    migrating.
  - the low-frequency deficit does **not** (125 Hz: −0.95 → −1.12 at
    medium, −1.50 → −1.66 at short). It is not a loss effect.
- **Residence time is the same in every band** (DA6: A1b / Hall 1.56 – 1.78
  everywhere). The low-frequency deficit is not energy that stayed longer.
  It is energy that was transferred less. Why is DA10's question.
- **V comes from the sparseness of the heard path** (DA4). Mean SD over the
  bands: A1b 0.50; last two stages heard 0.32; fed into the first two 0.33;
  every stage heard 0.09; 8 % direct 0.08; Hall 0.12. Only the two routes
  that are excluded bring it to Hall's floor.
- **V is also the source-dependent level** (DA3, DA5). Third-octave
  roughness: A1b 1.61 / 1.94 / 0.72 dB against 0.32 / 0.34 / 0.15 for
  Hall's sibling. Spread over the three broadband sources: A1b
  **2.27 / 2.83 / 0.63 dB**; sibling 0.08 / 0.28 / 0.55; AT5a
  0.54 / 0.32 / 0.48. These are requirement 4's baselines: a repair must
  reach **≤ 1.14 at medium and ≤ 1.42 at short**, and not exceed 0.93 at
  long.
- **The setting-dependent level is large and separate:** the level law A1b
  would need spans −5.8 to +5.7 dB over the nine grid settings. Stage 7's
  "1.4 – 4.2 dB louder at short" was mostly this (−2.43 dB at small / short)
  plus the source spread. A static law removes it; none exists yet.

**A consequence for the tone gate, stated before any repair is tried.**
The gate is 1.0 dB at the defaults *and* over 27 settings. With an SD of
1.1 dB at 125 Hz the worst of 27 draws is expected near 2.5 – 3 dB whatever
the mean is — A1b's recorded 3.02 dB. Passing the grid needs the SD at
125 Hz down to roughly 0.35 dB, which is Hall's own floor. A repair that
only corrects S cannot pass the grid. This is arithmetic on the measured
SD, not a measurement.

## 2. DA0: an unexpected result that has to be understood first

DA0 asked whether A1b with Hall's tone still reads as A1b. It reads
134.82 / 2.21 / 144.12. But the control beside it — the same filtering
process with a **flat** filter, which changes A1b's response by 2.5 × 10⁻¹⁴
of its peak — reads **58.21 / 0.11 / 75.42**, on *late texture*. Identical
input reads 0.00.

A change of 10⁻¹⁴ cannot be heard and should not be seen. The cause, from
the metric's code and not yet measured:

- The late-texture family is the spread, in dB, of 5 ms energy bins against
  their 45 ms neighbourhood, from 30 ms onward. It skips a bin only when
  its energy is *exactly* zero.
- A1b's response has not arrived at 30 ms. Its first non-zero sample is at
  37 ms (medium), 18 ms (short), 62 ms (long), and what follows first is
  numerically tiny. The family is taking logarithms of numerical dust and
  of the climb out of it.
- Hall has signal within a few milliseconds, so its bins are ordinary.

If that is right, then part of A1b's distance from Hall on late texture —
155 and 298 at the same knobs; "two and three orders above the threshold"
against Room in Stage 6 — is **the metric reading the silence before A1b
starts**, at levels hundreds of dB down. The late start itself is real and
is a property of the structure. The size of the number is not.

This matters for three things and I am not deciding any of them:

1. **Requirement 2** ("≤ 1.5 against A1b") cannot be used as written: the
   metric does not read A1b as itself after a 10⁻¹⁴ change. DA0 as
   registered says "not reachable"; the honest reading is "not measurable
   this way".
2. **Requirement 1 and A1b's structural pass.** Hall's search failed against
   A1b at 9.77 / 4.00 / 15.33. If the residual at medium and long is this
   family reading pre-arrival dust, the margin there is not what it seems.
   Hall's search has no pre-delay among its seven controls.
3. **Earlier results that rest on the same family** for a structure with a
   late arrival.

**None of this reclassifies anything.** A1b's status is the decision of
2026-11-18 and stays so unless you change it. The frozen metric is not
edited. What follows are diagnostics that read the same responses with the
inaudible part masked, to find out how much of the separation survives.

## 3. Four more diagnostics, registered now

| | diagnostic | reads |
|---|---|---|
| DA7 | the 5 ms energy envelope of A1b and Hall, dB against each response's loudest bin, at the three points: when it first passes −200, −120 and −60 dB; and the two fluctuation figures for Hall, A1b and A1b through the flat filter | whether the family's bins are in dust |
| DA8 | A1b against Hall at the same knobs with **the same white noise added to both responses** at a level L below each response's loudest 5 ms bin: L = none, 200, 160, 120, 100 dB. Controls: Hall against Hall-plus-floor; A1b-plus-floor against A1b with a different noise seed | how much of the separation lies below each level, and that the floor itself is not seen |
| DA9 | Hall's free seven-control search, five seeds, three points — the frozen search — against A1b's features **read with the −120 dB floor** | requirement 1 with everything below −120 dB masked |
| DA10 | DA1's mean at 125 Hz against the mid bands, damping off, at modulation depth 0, default and twice default; medium and short | whether the low-frequency deficit is the absence, at low frequency, of the phase scrambling that modulation gives the high bands |

**Why −120 dB.** It is below the noise floor of 16-bit audio by 24 dB and
of any playback chain. A difference that exists only below it is not a
difference between two reverbs. −100 is included as a looser reading and
−160 / −200 to show where the number moves.

**Reading rules, fixed now.**

- DA8: if late texture at L = 120 is under half its unfloored value, the
  unfloored figure is reported as *predominantly below −120 dB*.
- DA9: reported as the gate would report it (SURVIVES / REJECTED, medians,
  lowest run, stopping family), under the label **"A1b, floor −120 dB"**.
  It is a diagnostic reading, not G2, and it changes no recorded verdict.
  Its limitation is stated with it: only the target is floored; the search
  reads Hall without a floor, as the frozen code does.
- DA10: the hypothesis holds if, at modulation depth 0, the difference
  between the 125 Hz mean and the mean of 1 – 4 kHz is under half of what
  it is at the default depth.

**Predictions, so they can be wrong.** DA7: the 50 – 200 ms window of A1b
at long starts below −200 dB. DA8: late texture falls by more than an
order of magnitude at L = 120 and something else becomes the worst family.
DA9: I do not know; small / short was already the weakest point (4.00) and
is the one least affected by this, so I expect it to stand and the other
two to fall sharply but not necessarily under 3.0. DA10: holds.

## 4. Candidates

Not yet. Section 5 of the pre-registration stands, but which families make
sense depends on DA8 – DA10:

- if the low-frequency deficit is a transfer effect (DA10), AR2 is the
  family for S and AR1 for the loss part;
- V needs AR3 or AR4, and section 1's arithmetic says how far they must go;
- what "A1b's identity" means for requirement 2 has to be re-stated on
  something the metric can actually read, and that is a decision for
  review, not something I will redefine alone.
