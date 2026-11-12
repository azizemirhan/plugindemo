# Phase 7 — Modulation × damping corner (TEST-ONLY)

**Verdict: the provisional default is healthy and passes the investigation gate
with a wide margin. Only the extreme High Damp 100 % setting exceeds it, and
modulation is not what puts it there — over the searched range modulation makes
the worst case slightly better, not worse.**

Recorded per the decision rule as an **extreme-character diagnostic and
automation concern, not a shipping blocker**. Mod Rate is not reopened.

Implementation: `Tests/DampingModulationCorner.cpp` with
`Tests/DampingSweepHarness.h`. Run with `AuroraVerbTests.exe ModulationCorner`.
5 / 5 checks pass. No Source, default, mapping, Option A or gate change.

---

## The deferred finding, reproduced — and resolved

The interaction deferred earlier was: at the 6 kHz corner with High Damp 100 %,
M0 mismatch ~4.22 % and M30 ~6.85 %. Dismissing that without reproducing it
would have been unearned, so it was measured at the exact probe frequencies the
old note used:

| band | M0 | M30 | Δ |
|---|---|---|---|
| **6000 Hz** | **4.13 %** | **6.68 %** | **+2.54 pp** |
| 12000 Hz | 5.82 % | 4.98 % | **−0.84 pp** |

| | historical | here | drift |
|---|---|---|---|
| M0 at 6 kHz | 4.22 % | **4.13 %** | 0.09 pp |
| M30 at 6 kHz | 6.85 % | **6.68 %** | 0.17 pp |
| gap | 2.63 pp | **2.54 pp** | 0.09 pp |

**REPRODUCED.** Recorded as a **local damping-corner modulation interaction at
a fixed analysis frequency** — explicitly *not* a global worst-case regression.

The full configuration, so the claim is checkable rather than asserted:

```
operating point
  Decay              2.500 s
  Low Damp           0.0 %
  High Damp          100.0 %
  Low Damp Freq      200.0 Hz   (inert at Low Damp 0: gainLow is exactly 1)
  High Damp Freq     6000.0 Hz
  Mod Rate           0.500 Hz   (shipping default, not swept)
  Size               1.000      (delaysMs at scale 1.0, unchanged anywhere here)
  engine             production FdnCore<8>, core.setDamping +
                     architecture::applyWithDepth

analyser
  ensemble           32 realisations, 1024-sample Hann-windowed burst,
                     RMS equalised
  render length      6.250 s
  band width         1.0 octave, two constant-peak-gain sections applied
                     forward then backward
  analysis centre    6000.0000 Hz
  solved Q           0.547125
  -3 dB edges        4186.63 Hz to 8373.26 Hz
  bandwidth          4186.63 Hz, band ring 0.0760 ms
  EDC                normalised per realisation, then averaged in the linear
                     energy domain
```

And the same condition with the **worst band searched** rather than probed:

| | M0 | M30 | Δ |
|---|---|---|---|
| worst over 500 Hz – 16 kHz | 6.43 % at 11313 Hz | 6.30 % at 8000 Hz | **−0.12 pp** |

**Both measurements are correct and they measure different things.** Modulation
raises the mismatch in the band sitting **at** the damping corner by +2.54 pp,
lowers it above the corner by −0.84 pp, and does not raise the worst case over
the range at all.

So modulation **redistributes** tail curvature across frequency rather than
adding it. The deferred finding was a real band-specific effect read at a fixed
probe, not a worst-case regression — which is exactly the distinction between
a peak that moved and a peak that grew.

---

## Set A — extreme High Damp 100 %

Decay 2.5 s, Low Damp 0, High Damp 100 %, Mod Rate at the shipping 0.50 Hz,
unchanged. Worst band searched over 17 one-octave bands from 500 Hz to 16 kHz
(third-octave to 10 kHz, sixth-octave above), because the corner sweep already
showed the worst band moves away from nominal frequencies.

| corner | depth | worst | at | RT60 | EDT | T20 | T30 | R² |
|---|---|---|---|---|---|---|---|---|
| 6 kHz | M0 | 6.43 % | 11313 Hz | 0.8437 | 0.9561 | 0.7933 | 0.8478 | 0.99726 |
| 6 kHz | M30 | 6.30 % | 8000 Hz | 1.0444 | 1.1154 | 0.9843 | 1.0505 | 0.99799 |
| 8 kHz | M0 | 6.10 % | 14254 Hz | 0.9652 | 1.0834 | 0.9103 | 0.9694 | 0.99742 |
| 8 kHz | M30 | 5.44 % | 11313 Hz | 1.0869 | 1.1479 | 1.0324 | 1.0918 | 0.99832 |
| 10 kHz | M0 | 5.51 % | 14254 Hz | 1.1572 | 1.2220 | 1.0979 | 1.1619 | 0.99807 |
| 10 kHz | M30 | 5.21 % | 15999 Hz | 1.1244 | 1.1955 | 1.0733 | 1.1322 | 0.99856 |
| 12 kHz | M0 | 4.15 % | 15999 Hz | 1.2835 | 1.3387 | 1.2264 | 1.2795 | 0.99863 |
| 12 kHz | M30 | 4.40 % | 15999 Hz | 1.2957 | 1.3559 | 1.2354 | 1.2923 | 0.99897 |

### The interaction metric, with the two effects kept apart

| corner | M0 worst | at | M30 worst | at | Δ_mod | M30 at M0's band | band moved |
|---|---|---|---|---|---|---|---|
| 6 kHz | 6.43 % | 11313 Hz | 6.30 % | 8000 Hz | **−0.12 pp** | **4.73 %** | yes |
| 8 kHz | 6.10 % | 14254 Hz | 5.44 % | 11313 Hz | **−0.66 pp** | **5.31 %** | yes |
| 10 kHz | 5.51 % | 14254 Hz | 5.21 % | 15999 Hz | **−0.30 pp** | **5.03 %** | yes |
| 12 kHz | 4.15 % | 15999 Hz | 4.40 % | 15999 Hz | +0.25 pp | 4.40 % | no |

Three of four corners have a **negative** Δ_mod, and at M0's own worst band M30
is lower every time — 6.43 → 4.73, 6.10 → 5.31, 5.51 → 5.03. The one positive
Δ_mod, +0.25 pp at 12 kHz, is where the peak did not move and sits at the top
of the measurable range, so it is a lower bound rather than a maximum.

Note also that M30's fits are consistently **better conditioned**: R² runs
0.99799–0.99897 against M0's 0.99726–0.99863.

---

## Does modulation change the spectral decay law? No.

This was the question to separate a change in the decay **law** from a change
in modal weighting and decorrelation. The in-band RT60 spread is the quantity
the second-order shelf study tied to tail curvature, so it is the one that
would have to move.

| corner | depth | worst slope | at | widest octave spread | at |
|---|---|---|---|---|---|
| 6 kHz | M0 | 0.5181 s/oct | 6350 Hz | 1.5233 : 1 | 6350 Hz |
| 6 kHz | M30 | 0.5367 s/oct | 6350 Hz | **1.4914 : 1** | 6350 Hz |
| 8 kHz | M0 | 0.5097 s/oct | 6350 Hz | 1.4429 : 1 | 6350 Hz |
| 8 kHz | M30 | 0.4968 s/oct | 6350 Hz | **1.4254 : 1** | 6350 Hz |
| 10 kHz | M0 | 0.4812 s/oct | 8000 Hz | 1.3732 : 1 | 6350 Hz |
| 10 kHz | M30 | 0.4689 s/oct | 6350 Hz | **1.3563 : 1** | 6350 Hz |
| 12 kHz | M0 | 0.4534 s/oct | 8000 Hz | 1.3177 : 1 | 6350 Hz |
| 12 kHz | M30 | 0.4431 s/oct | 8000 Hz | **1.3043 : 1** | 6350 Hz |

The spread moves by about 1–2 % and always **downward** with modulation; the
slope likewise barely moves. **Modulation does not alter the spectral RT60 law.**
It changes modal weighting and decorrelation while the law stays put — the
second of the two alternatives.

That is also internally consistent rather than two separate observations: if
in-band RT60 spread drives tail curvature, and modulation lowers the spread
slightly, then the worst-case mismatch should fall slightly — which is what the
Δ_mod column shows. The two measurements support each other.

---

## The provisional default — product critical, and healthy

Low Damp 20 % at 200 Hz, High Damp 45 % at 10 kHz, Decay 2.5 s.

| depth | worst | at | RT60 | EDT | T20 | T30 | R² |
|---|---|---|---|---|---|---|---|
| M0 | 1.27 % | 15999 Hz | 1.7353 | 1.9157 | 1.7009 | 1.7228 | 0.99994 |
| **M30** | **1.20 %** | 630 Hz | 2.4690 | 2.6464 | 2.4963 | 2.4667 | 0.99971 |

```
Delta_mod                      -0.07 pp
M30 at M0's own worst band      0.57 %
worst band moved                yes
investigation gate   5.0 %      PASSED at the default
worst fit R^2                   0.99971   (soundness floor 0.995)
```

The default passes the gate by a factor of four, with the fit quality far above
the soundness floor, for both M0 and M30. Δ_mod is **negative**.

Decay stability, as the spread of per-realisation fits at 1 kHz rather than of
the ensemble average — the average is stable by construction, and what matters
is how far one excitation deviates from the law:

```
M0    0.273 %
M30   0.364 %
```

---

## The Phase 6 benefit is still there

Measured at the provisional default with the **repo's own definitions**, not
new ones: `maxEnvelopePeriodicity` over a `flattenDecay`-ed 0.5–4.5 s window
with 1–120 ms lags, `correlation(L, R)`, and
`monoDownmixDb = 20·log10(rms(mono)/rms(left))` — the same calls
`CandidateAnalysis.cpp` makes.

| metric | M0 | M30 | change | |
|---|---|---|---|---|
| late periodicity | 95.0371 | **63.0450** | **−31.99** | lower is better |
| stereo correlation | −0.0352 | **−0.0763** | −0.0410 | lower is more decorrelated |
| mono downmix (dB) | −3.6718 | −3.8328 | −0.1611 | nearer 0 is better |
| RT60 spread at 1 kHz | 0.273 % | 0.364 % | +0.091 | lower is more stable |

**The reason Mod Depth 30 % ships is intact**: late periodicity falls by 34 %,
and stereo decorrelation improves as well. The two small costs are 0.16 dB of
mono downmix and 0.09 pp of decay stability.

L and R come from a **single** render pass, because the tank is stateful and
rendering the channels separately would not give the same realisation.

---

## Classification, extreme and default kept apart

```
EXTREME, High Damp 100 %
  worst M30 mismatch anywhere   6.30 %
  largest Delta_mod             0.25 pp at 12 kHz
  gate 5.0 %                    NOT passed

DEFAULT, the provisional candidate
  worst M30 mismatch            1.20 %
  Delta_mod                    -0.07 pp
  gate 5.0 %                    passed
```

Applying the decision rule to the measurements: **the default is healthy and
only the extreme setting exceeds the gate, so this is recorded as an
extreme-character diagnostic and an automation concern — not automatically a
shipping blocker.**

And a sharper statement than the rule required: at High Damp 100 % the gate is
exceeded by the **static** damping geometry, not by modulation. M0 alone gives
6.43 % at the 6 kHz corner and 5.51 % at 10 kHz, both already over the 5 %
gate, and modulation then moves the worst case slightly **down**. Modulation is
not the cause of the extreme-setting failure.

**Mod Rate was not swept.** The rule was that it reopens only if Mod Depth 30
is a material driver *at the default*, and Δ_mod there is −0.07 pp — not a
driver, and marginally beneficial.

---

## What this leaves

| item | state |
|---|---|
| provisional default, M0 and M30 | healthy, gate passed with 4× margin |
| modulation at the default | harmless and marginally helpful |
| Phase 6 modulation benefit | intact, periodicity −34 % |
| extreme High Damp 100 % | over the 5 % gate, caused by the static geometry |
| modulation as the cause of that | **ruled out** |
| Mod Rate | not reopened, correctly |

The deferred modulation × damping interaction is **characterised and closed as
a non-blocker**. What remains over the gate at High Damp 100 % is the static
tail curvature already documented in the corner sweep, where the worst measured
mismatch at the 10 kHz corner is 5.51 % and the floor of the default space is
2.31 % against a 2 % Problem A target.

No production default change, no Source change, no mapping change, no Option A
change, no gate change, no modulation redesign. No install, no FL Studio,
Phase 7 not complete.
