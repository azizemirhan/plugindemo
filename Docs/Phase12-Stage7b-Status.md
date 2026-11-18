# Phase 12 Stage 7b — status

Documents only. No code, no build, no measurement, no render. `Source/` is
unchanged. No Mode selector. Phase 13 not started.

## Decisions received (2026-11-18)

1. V1 remains five Modes: Room, Hall, Chamber, Plate, Ambient.
2. Hall accepted and frozen.
3. Plate validation plan approved. Primary CPU closure at 48 kHz, idle
   machine, at least 31 interleaved pairs, 95 % upper bound below 1.75 ×
   Hall. Mandatory secondary validation at 44.1, 96 and 192 kHz. No Plate
   redesign unless validation disproves the candidate.
4. Ambient undecided. A1b still fails the 1 dB tonal gate at about 1.82 dB.
   The short-Decay level issue (1.4–4.2 dB louder than Hall before
   matching) is separate and is hidden by the blind package on purpose; a
   perceptual pass would not solve it. `KEY.txt` stays closed until
   listening is complete.
5. RN1 / RN2 / RN3 are not to be coded. They stay frozen as registered.
6. Chamber stays in the target. Its acceptance philosophy is revised for
   future work by a new pre-registered Mode-identity criterion. No earlier
   candidate is reclassified; no old threshold moves.
7. No Chamber coding yet.
8. No Phase 13, no production Mode selector.

## Stop conditions

| | state | where |
|---|---|---|
| Plate validation setup finalized | done — procedure fixed; harness not written; nothing run | `Phase12-Stage7b-Plate-Validation-Setup.md` |
| Revised Mode-identity criterion documented | done — pre-registered, nothing run against it | `Phase12-Stage7b-Mode-Identity-Criterion.md` |
| RN1 / RN2 / RN3 definitions restated for review | done — the registration itself is unmodified | `Phase12-Stage7b-Room-Definitions-For-Review.md` |
| Ambient package untouched | untouched: not re-rendered, not edited, `KEY.txt` not opened in this stage | `renders/phase12-stage7-ambient/` |

## Three things review should see first

- **Plate is not defined off 48 kHz.** The chain's dispersion is fixed in
  samples; unchanged at 96 kHz it is half as long in time. The setup
  therefore registers a rule for what Plate is at another rate (same 240
  sections, coefficient re-fitted) and the checks that decide whether that
  rule gives the same Plate. This is a definition, not a redesign, but it
  is new and needs your eye.
- **The new criterion does not make Chamber easier against Hall.** It
  removes the requirement to be distinct from a *related* Mode at one
  declared edge of the Size–Decay plane. Against Hall it still demands
  distinctness at six of nine points including the default, and Stage 7's
  finding — a chamber's late field is a point in Hall's Size–Decay plane —
  is exactly what that tests.
- **The Room registration has an inconsistency of mine in it.** The rule
  for visibility controls is worded three different ways. It is listed, not
  fixed, because the document is frozen; it needs a ruling before any
  control is run.

## Regression

Not run. This stage changed no code and no build input; the last figures
are Stage 7's (fast 2583 / 2601, state 26 / 26, `Source` diff empty).

## Waiting for

1. Listening results on the Ambient package.
2. Review of RN1–RN3, with rulings on the listed points.
3. Review of the Mode-identity criterion, in particular its section 8.
4. A go-ahead to write the Plate harness, and an idle machine for the
   48 kHz campaign.

---

# Second round — decisions of 2026-11-18 on Stage 7b, and what was done

## Decisions received

- **Plate.** The sample-rate rule is approved: 240 sections fixed, the
  coefficient derived per rate by one deterministic law, the same at every
  rate, no manual tuning, the 15-sample pipeline delay explicit in the
  group-delay analysis, no redesign. The measurement tool is authorized.
  Primary closure unchanged: idle machine, 48 kHz, 31 interleaved pairs,
  1.75 × Hall. The statistic must be worded precisely and must not be read
  as a bound on the 95th percentile. Secondary validation mandatory at
  44.1 / 96 / 192 kHz.
- **Mode identity.** The nine-point criterion is approved with one
  modification: a point may be convergent with more than one reference only
  if those references are demonstrated mutually close / reproducible at
  that point under the frozen metric. No retroactive reclassification.
- **Room.** One visibility rule for all hypotheses: ≥ 3.0 VISIBLE;
  1.5 – 3.0 INCONCLUSIVE; < 1.5 NOT VISIBLE TO THE CURRENT METRIC; the
  number always reported; no special threshold for RN3; METRIC INCAPABLE /
  REQUIRES DEDICATED METRIC where that is the case. RN1–RN3 not to be coded.
- **Ambient.** No decision. Package untouched, `KEY.txt` closed, the
  1.82 dB tonal-gate failure unchanged.
- **Chamber.** No DSP. Signature thresholds unfilled until Hall's grid
  baseline exists.
- No Phase 13. No production Mode selector.

## Done

| | state | where |
|---|---|---|
| Approved definitions updated and frozen | Mode-identity criterion amended (limit 4), amendment recorded | `Phase12-Stage7b-Mode-Identity-Criterion.md`, sections 2 and 10 |
| | Room visibility rule, as an addendum; the pre-registration itself not edited | `Phase12-Stage7b-Room-Visibility-Rule.md` |
| | Plate: the law, the statistic's wording, the tool | `Phase12-Stage7b-Plate-Validation-Addendum-1.md` |
| RN1 / RN2 / RN3 definitions for review | restated in the reply to this round; unchanged on file | `Phase12-Stage7-Room-Preregistration.md` |
| Plate measurement tool | written, built, run once to verify it | `Tests/Phase12Stage7Plate.*`, `Tests/Phase12Stage7Platform.cpp` |
| No Room candidate run | none built, none run | — |
| Ambient package | untouched | — |

## What the tool's check run showed

Machine-independent, and so results: at 44.1, 96 and 192 kHz the rig is
valid (S0), the Plate is the 48 kHz Plate by the frozen metric (S3,
0.48 – 0.75), Decay and Size hold (S4, S5), and PV3 is the plain chain
exactly 15 samples later (S6).

Two findings for decision:

1. The chain's group delay passes S1 on the 0.5 ms absolute allowance only;
   at 10 kHz it is 12 – 14 % short at 96 and 192 kHz.
2. With the pipeline delay added, the total at 192 kHz is 0.556 ms from the
   48 kHz total at 10 kHz, outside the 0.5 ms allowance.

CPU figures from that run are **not results** (the machine was in use).
They are in the addendum only to show the tool runs, with two things to
watch for when the real runs are made: modulation off (ratio about 1.9,
because Hall gets cheaper) and the scalar form PV1 (about 1.85).

## Open, mine to flag

- The amended limit 4 reads "demonstrated" as REPRODUCED in both
  directions. If BETWEEN should qualify, that needs a ruling.
- The visibility rule does not say what follows an INCONCLUSIVE result. I
  treat it as a stop.
- For the visibility value I take the lowest of the three points against
  Hall's free search. That reading is mine.

## Regression

Run on the final build, with nothing else building or measuring.

| check | result |
|---|---|
| Fast regression (`AuroraVerbTests.exe --fast`) | 2593 / 2611 passed, 18 failures |
| against Stage 7 (2583 / 2601) | ten checks added — the tool's arithmetic suite — and all ten pass |
| the 18 failures | the same 18 as Stage 7's run, compared **line by line**: no difference |
| State suite | 26 / 26 passed |
| `git diff --stat 77f32fe -- Source`, `git status -- Source` | empty |

Not covered: the heavy suites were not re-run. The three on-request Plate
suites are not part of any regression by design.

## Waiting for

1. Listening results on the Ambient package.
2. Individual approval or rejection of RN1, RN2, RN3.
3. An idle machine for the three Plate closure runs.
4. A decision on the two Plate findings above.
