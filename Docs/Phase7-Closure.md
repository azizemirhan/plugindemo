# Phase 7 — closure

**PHASE 7 COMPLETE**, with four documented accepted limitations. These are
accepted limitations, not hidden failures; each one is stated here with the
evidence that led to accepting it.

Phase 7 added frequency-dependent decay to the tank, then spent most of its
length answering a question the feature raised rather than building more of
it: *how close can the shipping default get to the Decay the user asked for?*
The answer is 2.3 %, against 7.90 % before and an approximately 2 % research
target that was not strictly reached.

---

## Final production defaults

| parameter | value |
|---|---|
| Low Damp | 20 % |
| Low Damp Freq | 200 Hz |
| **High Damp** | **45 %** |
| **High Damp Freq** | **10 kHz** |
| Mod Depth | 30 % |

Production commit **`44ae4f6`** — *Phase 7: adopt the measured default damping
operating point*. A defaults-only change: two values. Parameter ids, ranges,
skew centres, mappings, automation semantics, the damping topology, Option A
and the filter equations are untouched.

---

## Problem A — the default broad mid

**Materially improved. The approximately 2 % research target was not strictly
reached, and this is not recorded as a PASS.**

| | worst 500 Hz – 2 kHz deviation from the requested Decay |
|---|---|
| old default — High Damp 50 %, 6 kHz | **~7.90 %** |
| accepted default — High Damp 45 %, 10 kHz | **~2.3 %** |
| pre-registered research target | ~2 % |

### The accepted engineering decision

**The remaining ~0.3 pp does not justify additional damping DSP architecture
complexity on the completed evidence.**

Every default axis was swept to an *interior* optimum, separately and then
jointly. Low Damp Freq and Low Damp amount both landed on what was already
shipping. The mechanism behind the floor was located rather than guessed:
Option A normalises each line by a **single scalar**, so the two shelves are
coupled and the 500 Hz and 2 kHz ends of the window cross over near 2.3 %. The
overlap study established that the scalar is set by the maximum of
`1 + dL + dH` — the **sum**, not the product term — which is why reducing shelf
overlap cannot reach it.

---

## Problem B — tail curvature

### At the shipping default: healthy

| | |
|---|---|
| worst measured tail mismatch, Mod Depth 30 % | **~1.20 %** |
| `R²` | comfortably above the registered gate |
| gate | `\|T20−T30\| ≤ 5 %`, `R² ≥ 0.995` |

Measured across the seven analysis bands at the new default, every
`|T20−T30|` ≤ 3.11 % and every `R²` ≥ 0.9979. **The shipping default is not
affected by Problem B.**

### At High Damp 100 %: a known extreme diagnostic

Approximately **5–6 %** tail curvature depending on frequency and corner
configuration — above the 5 % investigation gate.

**KNOWN EXTREME DIAGNOSTIC. Not a shipping-default blocker.** The gate is
unchanged and remains not passed at that operating point.

---

## Modulation

- Modulation does **not** materially change the spectral RT60 law.
- It **redistributes** modal weighting and decorrelation.
- Default `Δ_mod` ≈ **−0.07 pp** — the default tail is healthy.
- Periodicity improves ≈ **34 %**; stereo decorrelation improves.
- The historical fixed-6 kHz interaction reproduced: **≈ 4.13 % → 6.68 %**.

That last figure is a **LOCAL damping-corner modulation interaction**. It is
*not* a global worst-case regression and *not* a shipping-default blocker.

---

## Modal analysis — frozen certified result

| | |
|---|---|
| observable transfer degree | **18657** |
| pole accounting | **18657 / 18657** |
| duplicates | **0** |
| unresolved | **0** |
| full reconstruction | **PASS** |
| MODEL R | reproduces T0 |
| MODEL E | **UNRESOLVED** under the pre-registered thresholds |

MODEL E's formal residue classification remains unresolved — the pre-registered
NOT MATERIAL (≤ 0.25 pp) and MATERIAL (≥ 0.75 pp) thresholds do not separate
it. The product-level conclusion stands regardless: **residue weighting is not
sufficient to solve Problem B.**

**Do not reopen this work.** `Docs/Phase7-ModalAnalysis.md` is the frozen
tooling record.

---

## Rejected and closed architectures

| architecture | status |
|---|---|
| first-order T1/T2 cascade | closed |
| second-order monotonic shelf (S2) | closed |
| λ overlap architecture | closed |
| additional default-frequency tuning | closed |
| Low Damp amount tuning | closed |
| frequency-dependent normalisation for the remaining ~0.3 pp | closed |
| multi-knee redesign | closed for Phase 7 |

**These are not universally impossible architectures.** They are **not
justified** for the remaining Phase 7 product problem, on the measured
tradeoffs. Each has its own document with the measurements that closed it;
none of those documents has been rewritten to hide a failed experiment.

---

## Automated regression

```
2344 / 2350 checks passed
6 failures
REAL REGRESSIONS: 0
```

All six were independently demonstrated to be unchanged by the
production-default diff: the change was stashed, the suite rebuilt and re-run,
and every failing figure came back **bit-identical**.

| # | failure | classification |
|---|---|---|
| 1 | High Damp 100 % / 12 kHz, `\|T20−T30\|` ~6.02 % | KNOWN EXTREME DIAGNOSTIC |
| 2 | Low Damp 100 % / 100 Hz repeatability ~19.34 % | ANALYSIS / LOW-MODAL-DENSITY LIMITATION |
| 3 | Ensemble High Damp 100 % mismatch ~5.998 % | KNOWN EXTREME DIAGNOSTIC |
| 4 | Synthetic analytic-law deviation ~21.54 % | HISTORICAL / RESEARCH-ONLY MODEL CHECK |
| 5 | S2 `R²` @ 6 kHz ~0.99329 | REJECTED-TOPOLOGY RESEARCH CHECK |
| 6 | S2 `R²` @ 12 kHz ~0.95514 | REJECTED-TOPOLOGY RESEARCH CHECK |

**No gate was altered to make the suite green**, and none should be. Failures
5 and 6 are the second-order study's own rejection evidence and reproduce the
figures recorded in `Docs/Phase7-SecondOrderShelfStudy.md` exactly.

---

## Listening

### LISTENING VERDICT: CONDITIONAL PASS / ACCEPTABLE

The new damping defaults create an **audible voicing difference**, especially
in fully-wet HF-sensitive material. Blind listening did **not** reveal a
consistent metallic tail, harsh or brittle HF character, loss of lushness,
transient smearing, obvious chorus character, modulation defect, or generally
degraded reverb quality.

**Preferences varied by source:**

| source | preferred | note |
|---|---|---|
| `mixed-wet` | **old** default | decay felt more natural / pleasant |
| `snare-wet` | **new** default | felt more natural |
| `hats-wet` | **old** default | the new default sounded *different* — **not** metallic, harsh or broken |

An initial "metallic" impression was clarified by the listener as **not**
actually metallic, harsh or defective.

Preferences were therefore **source-dependent**, and **no consistent
perceptual defect was identified**. The voicing difference is acceptable and
does not justify another Phase 7 DSP iteration.

### FL Studio A/B: WAIVED

**WAIVED after acceptable blind offline listening validation. Host listening
was not performed and is not claimed as performed.**

The grounds:

- The blind offline set used the **exact validation build**.
- The comparison was **level-matched** — the louder side brought down to the
  quieter, gain only, never upward, with the amount recorded in the key.
- Which setting was A and which was B was **hidden during evaluation**: the
  sides were written as `1` and `2`, the mapping permuted and permuted
  differently per source, with the mapping and every per-file measurement in a
  key file kept unread until the answers were written down.
- The **100 % wet** files exposed the damping-tail difference **more strongly
  than normal Mix = 30 % operation**, so the comparison was run at higher
  sensitivity than real use.
- The **Mix = 30 % difference was already measured to be small**: nine of
  twelve rendered pairs were near-identical at 0.25 dB, and every Mix 30 %
  pair was among them. A 1 dB difference in the tail arrives as roughly 0.1 dB
  once scaled by 0.3 under a dry signal at 0.7.
- The purpose of host listening was **perceptual validation, not another DSP
  measurement**, and the perceptual question was answered.
- Blind evaluation found **no consistent product defect**.

**FL Studio A/B is therefore not required as an additional blocking gate for
Phase 7.**

The validation artefact *was* made discoverable for FL Studio — see
`Docs/FlStudioValidationScanWorkflow.md` — so the host route exists and can be
used whenever it is wanted. It simply is not a Phase 7 gate.

---

## Validation artefact

```
C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
  Contents\x86_64-win\AuroraVerb.vst3        15 324 672 bytes

SHA-256  2C52D78F701A3024167A5651A6A90D5F5ADB971B147B30871631365715BA6151
```

Built from the `validation` profile — `AV_DEV_DIAGNOSTICS=ON`,
`AV_DEV_AUDIO_CAPTURE=OFF`, `AV_DEV_ALLPASS_CANDIDATE=1`,
`AV_COPY_PLUGIN_AFTER_BUILD=OFF`.

Verified: hash, 15 324 672 bytes, PE32+ / x86-64, `LoadLibrary` succeeds,
`GetPluginFactory` / `InitDll` / `ExitDll` present, no MSVC runtime imports
(static CRT), identifies as AuroraVerb with category `Fx|Reverb`.

**Nothing was installed.** `AV_COPY_PLUGIN_AFTER_BUILD` remained OFF
throughout. There is no installed `AuroraVerb.vst3` anywhere on this machine —
the production target, the per-user folder and the rollback archive named in
`Docs/ShippingBuildHardening.md` are all absent here, so the
do-not-overwrite invariant holds because there is nothing to overwrite, not
because a check passed.

---

## Phase 7 exit criteria, against `Docs/Phase7-DampingPlan.md` §11

| # | criterion | status |
|---|---|---|
| 1 | four damping parameters connected to DSP | **met** — `damping - control mapping` 4/4 |
| 2 | automation works on all four | **met** — `automation continuity` 2/2 |
| 3 | frequency-dependent RT60 measured low/mid/high | **met** — `RT60 by band` 8/8 |
| 4 | mid-band Decay preserved within 2 % at every setting | **NOT STRICTLY MET — 2.3 %.** Accepted limitation 1 |
| 5 | no instability, exhaustive sweep | **met** — `stability grid` 3/3, `subtractive invariant` 3/3 |
| 6 | no clicks, crackles or zipper | **met** — `automation continuity`, `sample rates and edge cases` 22/22 |
| 7 | CPU measured, at rest and during a Size morph | **met** — measured and reported |
| 8 | pluginval strictness 10 PASS | **not re-run for this artefact.** pluginval is not installed on this machine. The change is defaults-only — no DSP, no parameter ids, no ranges — so it alters no surface pluginval exercises, but this is reasoning, not a fresh PASS, and is recorded as such |
| 9 | full test suite PASS | **2344 / 2350, 0 real regressions.** Six classified non-regressions. Accepted limitation 3 |
| 10 | FU-1 default character regression acceptable | **met** — blind listening, CONDITIONAL PASS / ACCEPTABLE |
| 11 | FL Studio host regression PASS | **A/B listening WAIVED** (above). The defaults-only change adds no host-facing surface; the host regression from the earlier phases stands and no new host run was performed |

Criteria 4, 8, 9 and 11 are recorded as they are rather than as passes.

---

## Accepted limitations

1. **The approximately 2 % Problem A research target was slightly missed** —
   ~2.3 % achieved, against ~7.90 % before. The remaining ~0.3 pp lives in
   Option A's single-scalar normalisation, and closing it is not justified by
   the measured tradeoffs.
2. **High Damp 100 % retains a known extreme tail-curvature diagnostic** above
   the 5 % investigation gate. The gate is unchanged and not passed there.
3. **Some historical and research checks intentionally remain red** — six of
   2350, all proven unchanged by the production diff. Making them green would
   mean altering gates or rewriting recorded research results.
4. **Blind listening showed source-dependent voicing preferences** but no
   consistent perceptual defect.

---

## Test infrastructure debt — post-Phase-7 follow-up

**Not fixed as part of Phase 7 closure, deliberately.**

Multiple tests still duplicate historical shipping-default values by hand
instead of consuming a single shipping-default contract. Thirteen sites were
found; eleven still carry the pre-Phase-7 values. None fails — which is the
problem, because each still *claims* to describe the product.

The follow-up task, specified in `Docs/Phase7-TestInfrastructureDebt.md`:

1. a central shipping-default test contract
2. a `ParameterLayout` drift assertion, so the contract cannot silently go stale
3. migrate only the tests that truly mean "the shipping default"
4. preserve historical and research checkpoints independently — the frozen
   constants in `DampingSecondOrder.cpp` and the `kAurora` table in
   `DampingSynthetic.cpp` are records, and records do not get updated

---

## Research trail

No historical document was rewritten to hide a failed experiment. Each study
keeps its own record, including the ones that produced nothing.

| document | what it settles |
|---|---|
| `Phase7-DampingPlan.md` | the original plan, topology choice and exit criteria |
| `Phase7-ModalAnalysis.md` | frozen certified modal tooling, 18657/18657 |
| `Phase7-SecondOrderShelfStudy.md` | S2 rejected — selectivity is a direct tradeoff within the tested single-corner monotonic shelf family |
| `Phase7-CornerSweep.md` | High Damp Freq → 10 kHz; corrected a registered Problem B figure |
| `Phase7-LowCornerSweep.md` | Low Damp Freq closed as a lever; bit-identical proof it cannot touch Problem B |
| `Phase7-LowAmountSweep.md` | 20 % is an interior optimum; the 60 Hz product cost stated plainly |
| `Phase7-HighAmountSweep.md` | 45 % interior optimum; the joint ridge |
| `Phase7-OverlapArchitecture.md` | λ overlap rejected — 0.00 pp, and **why** it is null |
| `Phase7-ModulationCorner.md` | modulation closed as a non-blocker |
| `Phase7-ListeningValidation.md` | the blind A/B procedure and its verdict |
| `Phase7-TestInfrastructureDebt.md` | the deferred cleanup |
| `FlStudioValidationScanWorkflow.md` | how to reach a validation build from a host |

### Commits

```
55238bc  frequency-dependent decay, and the analysis behind it
ff294f3  certify modal analysis and second-order damping study
44897ca  characterize damping corner default tradeoff
a4b1cc3  characterize low damping corner tradeoff
07a1c6d  characterize low damping amount default
45f6074  complete damping default-space characterization
0474c7c  reject damping overlap architecture
d4161e2  characterize modulation damping interaction
44ae4f6  adopt the measured default damping operating point   <- production
d1cb2a3  prepare the damping default listening gate
ed3e905  fix the FL Studio validation scan-path workflow
22cd910  use a system VST3 junction for the validation build
```

---

## PHASE 7 COMPLETE

Declared complete with the four accepted limitations above. Phase 8 has not
been started.
