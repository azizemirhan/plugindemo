# Phase 12 Stage 8 — Ambient: addendum 4 (two follow-up measurements, registered)

Addendum to `Phase12-Stage8-Ambient-Repair-Preregistration.md`. Written
after the scan and step A were run (logs `04-ambient-scan.txt`,
`05-ambient-root-cause.txt`) and **before** the two measurements below.
No candidate is built.

## 1. Why two more

**The ± 4 % perturbation may not have randomised the lowest modes.** A
stage's loop is 15 – 90 ms, so its modes are 11 – 65 Hz apart and the
125 Hz octave holds modes number 2 to 12 or so. Moving a delay by 4 %
moves mode number 4 by 4 % of its frequency — a few hertz, far less than
the spacing. At 1 kHz the same 4 % moves a mode by more than the spacing.
So the "mean over perturbations" is a true systematic figure at mid and
high frequency, but at 125 Hz it may still carry this delay set's
particular low-order alignment.

Two things in the scan say so: Hall's own sibling delay set reads a
"mean" of −0.44 dB at 125 Hz at medium under the same procedure, and
A1b's 125 Hz mean changes sign between settings (−0.95, −1.50, +0.47).

If that is right, the "systematic low-frequency deficit" that five
hypotheses failed to explain is not systematic: it is variance that my
instrument could not resolve, and there is nothing for steps C or D to
compensate at 125 Hz.

**A tolerance for "preserved" has to come from A1b itself.** The decision
asks for thresholds derived from the frozen A1b and a Hall baseline. How
much A1b's own buildup landmarks move when only its delays are perturbed
is the natural unit.

## 2. The measurements

| | | reads |
|---|---|---|
| DA12 | octave deviation from Hall with each delay multiplied by a factor drawn log-uniformly from 1 / 1.3 to 1.3 (**± 30 %**), 24 seeds; A1b at the three points; Hall perturbed the same way against unperturbed Hall | the mean and standard deviation at 125 and 250 Hz once the low-order modes really move |
| DA13 | A1b's onset-normalised landmarks (t10, t50, centroid, early fraction, t10 / t50) over the twelve ± 4 % perturbations, at the three points: mean and standard deviation; Hall's at the same knobs | the spread that "A1b's buildup" has before anything is changed |

A ± 30 % change of the delays also changes the stage loop times the
coupling angles were computed from, by up to 30 %. DA12 is therefore a
measurement of tone statistics only; its responses are not A1b and are
not used for anything else.

**Reading rule for DA12, fixed now.** At a point, the low-frequency
deficit is *systematic* if under ± 30 % the mean at 125 Hz is at least
0.5 dB in magnitude, at least two standard errors, **and** at least twice
the magnitude of perturbed Hall's mean. Otherwise it is *not separable
from variance*.

**Prediction.** Not separable at medium and long. At short I do not know:
the deficit there is larger (−1.50) and is built up over the first two
joins (step A), which looks more like a mechanism than a draw.
