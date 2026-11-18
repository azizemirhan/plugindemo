# Phase 12 Stage 7b — Room: one visibility rule (addendum 1 to the Room pre-registration)

Decided 2026-11-18. This is an addendum to
`Docs/Phase12-Stage7-Room-Preregistration.md` (commit `9ad81db`). That
document is **not edited**; where its wording on visibility controls differs
from this page, this page governs. Everything else in it stands as written.

Nothing has been built or run. RN1, RN2 and RN3 are not to be coded until
each is individually approved.

## 1. The rule

One rule for every Room hypothesis:

| visibility | class |
|---|---|
| ≥ 3.0 | **VISIBLE** |
| 1.5 ≤ visibility < 3.0 | **INCONCLUSIVE** |
| < 1.5 | **NOT VISIBLE TO THE CURRENT METRIC** |

- The numeric value is always reported, at every point measured.
- No hypothesis has a lower threshold of its own. RN3 is not an exception.
- If RN3's inter-channel timing is genuinely outside what the frozen metric
  responds to, the result is classified **METRIC INCAPABLE / REQUIRES
  DEDICATED METRIC**. The threshold is not lowered and the metric is not
  extended inside this work.

The thresholds are the frozen metric's own (REPRODUCED ≤ 1.5,
DISTINCT ≥ 3.0); nothing new is introduced.

## 2. What it replaces

The pre-registration worded the consequence of a visibility control three
ways. All three are superseded:

| where | as registered | now |
|---|---|---|
| section 2 (general) | a control not scoring ≥ 3.0 makes the hypothesis *not testable* | the three classes of section 1; "not testable" is no longer a single outcome |
| RN1 | if Hall reproduces the control, "RN1 rests on the timed law only; this is reported either way" | classified by section 1 like the others |
| RN2 | *not testable* if the in-band version scores under 3.0 against Hall at the *same knobs* | classified by section 1, on the measurement of section 3 |
| RN3 | *not testable* if some Width brings the worst family under 1.5 | classified by section 1; below 1.5 reads METRIC INCAPABLE / REQUIRES DEDICATED METRIC |

## 3. What "visibility" is, operationally

The decision fixed the classes. To apply them, one number has to be defined
the same way for all three. This is how I read it, and it is the part of
this page that is mine:

- **The measurement.** The hypothesis's visibility control, as target,
  against Hall's free seven-control search — the frozen search, centred
  view, one search per point (the G1 form) — at the three operating points.
  This is the measurement section 2 of the pre-registration already named
  ("against Hall's search"). A same-knob score is reported beside it and
  does not classify.
- **The value that classifies** is the **lowest** of the three points. All
  three are reported. A property that the metric sees at medium and not at
  small / short is not visible where Room has to stand.
- **The controls** are the ones registered, unchanged:
  - RN1: Hall's lines × 8 with a fixed sparse tap set, no timed law;
  - RN2: the paraunitary matrix with its crossover in band. The two
    out-of-band versions (all specular, all diffuse) are run as checks on
    the control itself and reported; they are expected under 1.5;
  - RN3: the control is the candidate. Hall's free search includes Width,
    so this *is* the registered Width sweep, done by the search instead of
    by hand.

## 4. What follows from each class

| class | consequence |
|---|---|
| VISIBLE | the hypothesis may proceed to its build, if individually approved: modal prominence, then G1 at small / short, and so on, as registered |
| NOT VISIBLE TO THE CURRENT METRIC | not built further under this metric. For RN3: METRIC INCAPABLE / REQUIRES DEDICATED METRIC — the property is neither passed nor failed |
| INCONCLUSIVE | **not specified by the decision.** Until it is, I will treat it as a stop: report the number and wait, and not build |

## 5. Not changed

- The Room gates added in section 2 of the pre-registration: clean modal
  prominence (≤ Hall + 3 dB) before any distinctness result counts; the
  stopping family read; small / short first.
- G1, G2, G3 and their thresholds.
- The definitions of RN1, RN2 and RN3.
- The order: controls first; no candidate before its control has been read.
