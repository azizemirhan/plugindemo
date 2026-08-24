# Phase 5F — FU-2: preview / wet gain staging

Purpose: measure exactly where the level goes on the wet path, so the production
decision about wet output gain is made from numbers rather than from the fact that
100% wet sounds quiet.

**Nothing in this stage changes a gain.** `previewBaseTrim`, the Size compensation,
the mix law, the FDN, modulation, damping, diffusion and voicing are all untouched.
The output of FU-2 is a recommendation and a set of blind A/B files, and then a stop.

---

## FU2-A — fresh controlled session: STATUS

| Item | State |
|---|---|
| Regression session finalized | Yes — closed cleanly by FL Studio; `summary.txt`, `environment.txt`, `session.json` all written |
| Offline analysis run | Yes — `analysis.txt` / `analysis.json` written. Live verdict PASS WITH WARNINGS, offline PASS. The two warnings are the known deadline pattern and the capture limit |
| Archived | `AuroraVerbDiagnosticsArchive\Phase5F-MixLawRegression\` — 453 MB, both folders (the second is FL Studio's scan instance) |
| Diagnostic root | **Empty.** Archived outside the root, not into a subfolder, because `Select-DiagnosticSession.ps1` enumerates the root's directories and a subfolder would appear as a session candidate |
| Production build | Installed VST3 hash `05D43BC5...845B` — **verified match**, mtime 2026-08-09 18:53:30 |

Capture limit is 600 s and is set in code (`DiagnosticCollector::setCaptureLimitSeconds`),
not by an environment variable. The FU-2 host session is therefore designed to finish
inside about 300 s, half the limit.

---

## A correction to the plan as sketched

FU2-B asks for the same input file to be **sample-aligned across all four mix points**.
A live host session cannot deliver that, and it matters:

- Four mix points in one host pass are four different stretches of time, over different
  material, with the tank in a different state at each.
- That is exactly what made the block-RMS comparison in the mix-law regression
  unusable — it was flagged there as "the windows may not be comparable".

The offline harness *can* deliver it, exactly rather than approximately. `renderWetOnly`
renders the wet path **once**; `mixDryWet` then combines that one dry/wet pair at any
mix position. Every mix point is then the same samples multiplied by two different
scalars — sample-aligned by construction, with the tank in an identical state.

So the division of labour is:

- **Offline harness — the instrument.** All four sources, all four mix points, the full
  metric set, and the whole wet-path decomposition. This is where the numbers come from.
- **Host session — the cross-check.** Proves the shipping plugin in FL Studio agrees
  with the offline model at two or three points, within a stated tolerance. It confirms
  the instrument; it is not the instrument.

Everything the plan asks for still gets measured. The difference is which measurement
carries the decision.

---

## FU2-B — controlled signal set (offline)

Four sources, each already generated deterministically from source code in
`exportMixLawCandidates`: **pad** (sustained tone), **snare** (transients), **piano**
(chord), **mixed** (program material). They need lifting out of that function into a
shared generator so FU-2 and the existing candidate export use the same signals.

Four mix points per source: **0% (dry reference), 30% (default), 50%, 100% (wet only)**.

Per window, measured separately — never a session-cumulative figure:

| Metric | Note |
|---|---|
| Gated integrated LUFS | BS.1770-4; the meter validated against the published table |
| RMS | full-signal, both channels |
| Sample peak | per window |
| True peak | `truePeakOfSignal`, already present |
| Crest factor | peak / RMS |
| L and R RMS separately | catches any width-stage asymmetry |
| DC offset | per channel |
| Clipping / over-unity count | samples with abs > 1.0 |
| Input vs output delta | LUFS and peak, against the dry input |

## FU2-C — wet path decomposition (offline)

At the defaults: Decay 2.5 s, Size 60%, Width 100%, Mix 100%.

The chain, in the order the plugin applies it (`PluginProcessor.cpp`, sections 2 to 4):

```
tank output (Size compensation is inside MorphingStereoFdn::processSample)
  -> preview trim          previewWetTrim(rt60, sizeScale)
  -> Width                 mid/side
  -> Mix                   mixCoefficientsFor(m)
```

Two of the five questions can be answered before any new measurement:

**Q5 — does Width at 100% change energy?** No, and not approximately: at `widthAmount = 1`
the stage is an exact identity.
`mid + side = (L+R)/2 + (L-R)/2 = L`, and `mid - side = R`. Zero dB, bit-exact.

**Q3 — what does the preview trim contribute?** `previewBaseTrim = 0.25` is -12.04 dB
exactly, multiplied by the decay-aware factor `sqrt(reference/current)`, which is 1.0 at
the reference (2.5 s, Size 1.0) and something else everywhere else.

Q1, Q2 and Q4 need measurement. Q1 in particular — the tank's own input-to-output
transfer — is the quantity nobody has measured yet, and the preliminary arithmetic
suggests it is the largest single unexplained term:

| term | dB | source |
|---|---|---|
| dry reference | -13.72 LUFS | existing GainStaging A/B |
| 100% wet | -32.16 LUFS | existing GainStaging A/B |
| **total gap** | **-18.44 LU** | |
| preview trim | -12.58 | previously measured, effective at defaults |
| Size compensation | -0.64 | previously measured, defaults |
| Width | 0.00 | identity, shown above |
| Mix law at 100% | 0.00 | wet coefficient is exactly 1 |
| **accounted for** | **-13.22** | |
| **unaccounted** | **-5.22** | the tank's own transfer, to be measured |

That table is arithmetic on figures already on record, not a new measurement. Its
purpose is to say what FU2-C has to resolve: if roughly 5 dB of the 18 dB is the tank
rather than the trim, then raising the trim alone cannot close the gap, and the
recommendation has to say so.

Method: instrument the offline chain only — tap the tank output before the trim, read
`MorphingStereoFdn::getCompensationGain(bank)` (already public), and measure at each
node. No production DSP is touched; the taps live in the test harness.

## FU2-D — safety and headroom

`Tests/PreviewIntegrationTests.cpp`, suite "Preview / gain safety", already measures the
post-trim peak for: full-scale impulse, impulse at 60 s decay, sustained noise at
default / 20 s / smallest Size / largest Size, and full-scale DC. It records that before
the trim was made decay-aware, sustained noise at 20 s reached 2.18 and full-scale DC
reached 8.01 — a span of about 18 dB.

What it does not report is the **pre-trim** peak per case, which is what a maximum
permissible gain has to be computed from. That is the extension needed.

The calculation, in this order and not the other way round:

1. Measure raw tank peak and true peak per case, pre-trim.
2. For each case, `maxGain = 1.0 / rawTruePeak` — the gain at which that case reaches
   exactly full scale.
3. The binding constraint is the **smallest** `maxGain` across the cases the plugin must
   survive. Which cases those are is a judgement to be stated explicitly, not assumed:
   a full-scale impulse is a real signal; sustained full-scale noise and DC are stress
   tests. The existing suite already draws that line, and the reasoning should be
   re-examined rather than inherited.
4. Only then generate candidates: the current trim, the computed maximum, and points
   between — chosen from the measurement, not picked for roundness.

This answers "if we raise the trim, which material clips first" directly, because the
answer is whichever case produced the smallest `maxGain`.

## FU2-E — decision report

The twelve questions, answered from the above, with a recommended value, its headroom
margin in dB, the change to the Mix 30% default, and the blind A/B file set to confirm
it by ear. Then stop for approval.

---

## Host cross-check session (FU2-B-host)

Short, bounded, single instance, 48 kHz / 512, the verified hash. Under 300 s.

Four dwells of about 40 s — Mix 0%, 30%, 50%, 100% — with the same loop playing
throughout, plus a settling gap between each. Purpose is agreement with the offline
model within tolerance, not measurement in its own right. The known confounds (tank
state, different material per window) are why it is a cross-check and why its tolerance
will be stated before the run rather than after.
