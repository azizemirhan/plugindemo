# Phase 12 Stage 6 — checkpoint (work in progress)

**Stage 6 is NOT finished.** This file records where it stopped on
2026-11-18, about 00:45, when the machine had to be shut down. It is a
working note, not the Stage 6 report; it is to be deleted when the report is
written.

Nothing here changes a threshold, the pre-registration or the plan.
`Source/` is unchanged.

## What is committed

| | |
|---|---|
| Stage 5 closed | `77f32fe`, hash record `f1d54fb` |
| Stage 6 pre-registration | `61679d5` |
| addendum 1 (Room: two-rate Size laws, more block state, tone in the vector rule) | `e1df45d` |
| addendum 2 (Room: RS8, composite both ways; Ambient: low-frequency hypothesis, AT5–AT7) | `0f0f594` |
| addendum 3 (Plate: chain kept in registers; multirate fitted for fewest sections) | `a47a762` |
| this checkpoint: all Stage 6 code and the raw run logs | the commit that adds this file |

The code builds and the fast checks pass (12.5.0 and 12.6.0, 10 / 10) as of
the last build.

## Raw logs kept (`Docs/Phase12-Stage6-runs/`)

| file | what it is | complete? |
|---|---|---|
| `01-room-first-run.txt` | Part R as first pre-registered (RS1–3, RR1–6, RM1–3, composite R5) | yes |
| `02-room-addendum1-and-ambient-first-run.txt` | Part R with addendum 1 (RS5–7, RR7–8, RM4, composite R6); Part A first run (D1–D6, AT2, AT3) | yes |
| `03-plate-first-run-and-chamber.txt` | Part P first run (PV1, PV2, PM, PT); Part C complete (control, K1–K3) | yes |
| `04-plate-addendum3-INTERRUPTED.txt` | Part P with addendum 3 | **no — killed** |

## Completed

- **12.6.1** the search copy equals the frozen search, knob for knob.
- **Room, first and second runs.** Vectors solved (RM4: mono tone −4.2 dB,
  balance −0.2 dB, octave 0.52 dB, G1 lowest 3.71). Size: RS7 passes the
  Size criterion; composite R6 = R4c + RS7 + RM4 is REJECTED by Hall's search
  at large / long (median 1.90). Ringing: RR7 (23.1 dB) and RR8 (17.4 dB) are
  materially reduced and both REJECTED at G1 (1.41, 1.06); nothing that keeps
  the identity reduces the ringing.
- **Ambient, first run.** Hypothesis (loss accumulation) wrong: damping off
  makes it 2.04 dB, not less. The deviation is the 125 Hz band (−1.8 dB).
  AT3 (25 % direct) fixes tone (0.18 / 0.73 dB) and is REJECTED at small /
  short (1.98). AT2 is 1.51 dB over the grid.
- **Plate, first run.** PV1 (8 sub-chains, scalar): chain 3.83 × faster,
  output identical to the reference 7 samples later; REFERENCE-EQUIVALENT
  (G2 3.43 / 3.03 / 3.11, lowest run 2.72); 1.77–1.87 × Hall, so G3 FAIL on
  the 1.75 limit. All six PT candidates REJECTED at G1. PM as first designed
  was a bad design (see addendum 3).
- **Chamber, complete.** Control holds (open tank built as Hall is Hall,
  −310 dB). K1a, K1b, K3a REJECTED at G1; K2a, K2b, K3b REJECTED at G2.
  **No further Chamber candidates are to be produced in this Stage**
  (instruction of 2026-11-18): the fourteen negative results across Stages 3,
  5 and 6 are taken as sufficient.

## Interrupted

**Part P with addendum 3** (`AV_S6_PART=PE`), killed during section 12.6.9.
What it had printed before being killed, and which therefore stands only as
a partial reading until the part is run to the end:

- PV3 (two-wide, values in registers), 8 sub-chains: **6.76 ×** the reference
  chain (prediction ≥ 6: met); output identical to the reference.
- PM1 refitted: 242 sections at rate / 4 plus 230 at full rate, 290
  section-equivalents — still more than the reference's 240.

Not reached in that run: PM2's figures, the delay / click table, all of
12.6.10 (candidates against the reference and Hall, G2, G3 and the wet-path
cost of PV3), and Part E.

## Not started

- **Part R with addendum 2**: RS8; RR1 and RR6 through G1 for the record;
  composites R5b and R6 judged by 2.4.
- **Part A with addendum 2**: D7; AT5a / b / c, AT6, AT7.
- **Part X**: Room against Ambient, both directions.
- **Part M**: the cross-mode matrix (Hall, Room, Ambient, Plate; no Chamber).
- **Part E**: cost, memory, configuration time, switching.
- The single full validation run, the fast regression, the state suite, the
  37-item report, and the Stage 6 commit.

## How to resume

1. Build (nothing else running):
   `cmake --build C:/JuceBuilds/plugindemo --target AuroraVerbTests --config Release`
2. Fast checks, about a minute:
   `C:/JuceBuilds/plugindemo/tests/AuroraVerbTests.exe runPhase12Stage5FastTests runPhase12Stage6FastTests`
   — expect 10 / 10.
3. **First measurement: the interrupted one.**
   `AV_S6_PART=P` then `runPhase12Stage6Tests` (about 20 minutes).
4. Then `AV_S6_PART=RAX` (Room and Ambient with addendum 2, then Room
   against Ambient; about 1.5 hours). Part C is **not** re-run: its result is
   in log 03.
5. Then `AV_S6_PART=ME` (the matrix and the engineering tables). When R, A
   and P are not in the same process, M and E use the *standing* definitions
   in `standingRoomSpec()`, `standingAmbientSpec()` and
   `standingPlateStructure()`. **Those still return R4c, A1b and the plain
   240-section reference.** Before step 5 they must be set to what steps 3
   and 4 chose — or steps 3–5 run as one process, `AV_S6_PART=RAXPME`, which
   needs no standing definitions and is the simpler route (about 3–4 hours).
6. Report, fast regression (`--fast`), state suite, `git diff --stat -- Source`
   (must be empty), commit, record the hash, delete this file.

Environment switch: `AV_S6_PART`, any of `R A X P C M E`. The status section
12.6.15 prints only when all seven are run together; because C is not to be
re-run, the report will take Chamber's status from log 03 and say so.
