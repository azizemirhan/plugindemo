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
