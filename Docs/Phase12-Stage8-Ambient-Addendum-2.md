# Phase 12 Stage 8 — Ambient: addendum 2 (DA7 – DA10 read; DA11 registered)

Addendum to `Phase12-Stage8-Ambient-Repair-Preregistration.md`. Written
after DA7 – DA10 were run (log
`Phase12-Stage8-runs/02-ambient-diagnosis-floor.txt`) and **before** DA11.
No candidate has been built. No recorded verdict is changed here.

## 1. DA7 – DA10, as measured

**DA7 — the bins are in dust. Confirmed.**

| | first non-zero sample | envelope passes −200, −120 and −60 dB | loudest at | fluctuation 50 – 200 ms | 200 – 500 ms |
|---|---|---|---|---|---|
| Hall, medium | 22.9 ms | 20 ms (all three) | 50 ms | 0.48 dB | 0.33 dB |
| A1b, medium | 37.1 ms | 55 ms (all three) | 215 ms | **77.20 dB** | 0.31 dB |
| A1b, medium, flat filter | 0.0 ms | 55 ms | 215 ms | **48.38 dB** | 0.31 dB |
| Hall, short | 11.0 ms | 10 ms | 30 ms | 0.33 | 0.42 |
| A1b, short | 17.9 ms | 25 ms | 100 ms | 0.31 | 0.35 |
| Hall, long | 38.5 ms | 35 ms | 75 ms | 0.71 | 0.46 |
| A1b, long | 62.3 ms | 100 ms | 385 ms | **148.40 dB** | 0.35 |

- A1b's response arrives abruptly: one 5 ms bin takes it from under
  −200 dB to above −60 dB. Before that bin there are non-zero samples more
  than 200 dB down (from 37 ms at medium, 62 ms at long). The family skips
  bins that are exactly zero and counts these.
- A "fluctuation" of 77 or 148 dB is the step from that dust to the signal.
  It is what the late-texture family has been reporting for A1b at medium
  and long.
- **In the window where the response actually exists, 200 – 500 ms, A1b's
  texture is Hall's:** 0.31 / 0.35 / 0.35 dB against 0.33 / 0.42 / 0.46.
- At short the response has arrived before the window opens, and the family
  reads 0.31 against Hall's 0.33: no difference.

**So A1b has no late-texture difference from Hall that this family can
show.** What the family has been reading is that A1b *starts later* —
35 ms later at medium, 15 ms at short, 65 ms at long — through an
accident of how it treats near-silent bins.

**DA8 — with the inaudible masked.** By the registered rule the unfloored
late-texture distance is *predominantly below −120 dB*: 155 → 37 at medium,
298 → 68 at long. But the control shows the floor is not a clean
instrument either: **Hall against Hall-plus-floor reads 3.08 at medium and
7.43 at long**, because Hall too has bins before its arrival, and the floor
turns their exact zeros into something the family counts. A floor moves
the artefact; it does not remove it. DA9 inherits that defect.

What DA8 does show cleanly is everything else. At the same knobs, with or
without a floor, A1b differs from Hall on:

| | buildup | energy profile | echo density | decay | modal structure | stereo |
|---|---|---|---|---|---|---|
| medium | 14.2 | 14.6 | 6.3 – 7.2 | 5.3 | 6.8 | 1.8 (9.5 unfloored) |
| short | 10.3 | 9.1 | 4.0 | 3.8 | 3.1 | 5.3 |
| long | 17.6 | 15.6 | 2.9 – 5.9 | 5.6 | 6.2 | 4.2 |

These do not move with the floor. They are real differences at the same
knobs. Whether Hall can remove them with its controls is the gate's
question, not this table's.

**DA9 — Hall's search against the floored target:** 20.69 / 4.13 / 22.28,
lowest run 4.06, "SURVIVES". I do not put weight on the medium and long
figures: the target still carries the floor-to-signal step, and the search
ran Hall to Size 100 with Decay 0.12 s chasing it. The small / short figure
(4.13, stopping on energy profile, against 4.00 unfloored) is unaffected by
any of this and is the one solid number: **at small / short A1b's
resistance to Hall does not depend on the artefact.**

**DA10 — the modulation hypothesis is wrong.** The gap between 125 Hz and
1 – 4 kHz, damping off, is −1.18 / −1.15 / −1.05 dB at modulation depth
0 / default / double (medium) and −1.84 / −1.87 / −1.86 (short). Modulation
has nothing to do with it. The low-frequency deficit is systematic, is not
loss, is not residence time and is not modulation. **Its cause is still
not established.** I had predicted this one would hold.

## 2. What this changes, and what it does not

It does not change A1b's status. That is the decision of 2026-11-18.

It does change what three phrases in that decision can be measured as:

- **"the structural metric is strongly separated"** — at medium and long
  the size of the separation (155, 298) is an artefact. The separation that
  is real at the same knobs is 14 – 18 on buildup and energy profile.
- **"A1b-like temporal / late-texture identity"** — A1b has no late
  texture of its own in the family's sense. Its measurable temporal
  identity is: *it starts later than Hall, and its energy arrives later
  and more gradually* (buildup, energy profile; loudest at 215 ms where
  Hall's is at 50 ms).
- **"Hall's mimic search still fails"** — true as run. But the frozen
  search has seven controls and **Pre-Delay is not one of them**, and the
  product has a Pre-Delay control. If part of A1b's identity is a later
  start, a user holding Hall can supply a later start. The search was
  never allowed to.

The third point is the one that could matter most, and it is not yet
measured. DA11 measures it.

## 3. DA11, registered now

**The question.** With A1b's late start taken away — as Hall with a
pre-delay would take it away — does Hall's search still fail?

**Preparation of the target, and only of the target.**

- *Dust zeroed.* Every sample before the first 5 ms bin that is above
  −200 dB (against the response's loudest bin) is set to exactly zero.
  Nothing above −200 dB is touched. This makes A1b's pre-arrival bins what
  Hall's are — exact zeros, which the family skips — without a floor.
- *Advanced by D.* The response is moved earlier by D, discarding the
  zeros at its start. A target advanced by D against Hall is Hall with a
  pre-delay of D against the target.

**D.** 0, and D\* = A1b's arrival minus Hall's at the same knobs, from
DA7: **35 ms at medium, 15 ms at short, 65 ms at long.** Both within the
product's Pre-Delay range. D\* is fixed from DA7 and is not searched.

**Measurements.**

| | | |
|---|---|---|
| DA11a | A1b (dust zeroed; D = 0 and D\*) against Hall at the same knobs: every family | what remains at the same knobs once the late start is not counted |
| DA11b | Hall's free seven-control search, five seeds, three points, against A1b dust-zeroed at D = 0 | the gate's reading without the artefact |
| DA11c | the same against A1b dust-zeroed and advanced by D\* | **Hall with Pre-Delay against A1b** |
| control | Hall dust-zeroed against Hall: must read 0.00 | that zeroing is not itself seen |

**Reading, fixed now.**

- DA11c, at a point: a run ≤ 1.5 → *"Hall with a pre-delay of D\*
  reproduces A1b at this point under the frozen metric."* Every median
  ≥ 3.0 and no run ≤ 1.5 → *"A1b's resistance to Hall at this point does
  not rest on its arrival time."* Between → reported as weak, with the
  figures.
- It is a diagnostic reading under the label **"A1b, arrival removed"**.
  It is not G2 and it reclassifies nothing.
- Its limits: D\* is one value per point, so a better pre-delay for Hall
  may exist and DA11c is then *optimistic for A1b*; and the search still
  reads Hall as the frozen code does.

**Prediction.** Small / short stands (15 ms is little, and that point's
4.00 was never late texture). Medium and long fall well below 9.77 and
15.33; I do not know whether below 3.0.

## 4. Where the repair stands

Not started, deliberately. Three things have to be settled first and only
the third is mine:

1. what a repaired Ambient is required to preserve, now that "late
   texture" is not it (section 2);
2. whether Pre-Delay belongs among Hall's controls in the gate — a change
   to how the frozen search is applied, which I will not make on my own;
3. the cause of the low-frequency deficit, which four hypotheses have now
   failed to explain (loss, stage count, modes per stage, modulation).

The variance arithmetic of addendum 1 stands regardless: with one two-line
stage heard, the octave shares of *any* delay set scatter by about 1.1 dB
at 125 Hz, and the 27-point tone gate cannot be passed without bringing
that near Hall's 0.36.
