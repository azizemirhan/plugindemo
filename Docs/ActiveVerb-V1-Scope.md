# ACTIVE VERB V1 — authoritative product scope

Recorded as the authoritative V1 scope from this point forward. This is a
**product roadmap decision**, not a technical one.

> It does **not** invalidate the completed Phase 7–10 research, and no
> completed DSP was redesigned or reverted because of it.

---

## What V1 is

A focused premium general-purpose algorithmic reverb.

V1 does **not** attempt to combine every creative reverb category into one
plugin.

---

## Core reverb — in scope

| | status |
|---|---|
| Professional algorithmic reverb engine | **shipping** (Phases 2–7) |
| Decay | **shipping** |
| Size | **shipping** |
| Pre-Delay | **shipping** (Phase 8) |
| Attack | **shipping** (Phase 9) |
| **Early Reflections** | **Phase 10, in progress** |
| **Early / Late balance** | **Phase 10, in progress** |
| Early Diffusion | **shipping** |
| Frequency-dependent Damping | **shipping** (Phase 7) |
| Modulation | **shipping** (Phases 5A–5C) |
| Stereo Width | **shipping** |
| Low Cut | **shipping** (Phase 8) |
| High Cut | **shipping** (Phase 8) |
| Dry / Wet Mix | **shipping** |

## Algorithm / Space modes — in scope, not started

A focused set of **genuinely different** reverb algorithms and spatial
characters. Target families:

**Room · Hall · Chamber · Plate · Ambient**

Additional modes only on demonstrated, genuinely different and useful reverb
behaviour. **The mode count is not a target.** Each retained mode must differ
**structurally or perceptually**, not by preset values.

> The repository currently declares 13 `Mode` enumerators in
> `ParameterIDs.h`. That enum is part of the saved state and is append-only, so
> it is not edited; what changes is which members V1 actually implements. The
> `mode` parameter is serialised and consumed by no DSP today.

## Character / Color — in scope, not started

A small set of genuinely different character families. Current target concept:

**Modern · Classic · Vintage**

Names may be revisited during product and UI work.

Character must **not** be an EQ preset. It may influence appropriate
combinations of damping, modulation, diffusion, spectral behaviour, texture,
and saturation or nonlinearity where justified — while preserving stability
and predictable controls.

> The repository declares three `Color` members (`classic`, `retro`, `modern`),
> serialised and consumed by no DSP. The target names differ from the enum's;
> that is a naming decision for the product phase, and the enum is append-only.

## Presets — in scope, not started

A strong factory preset system, with categories representing practical use
cases across the supported Modes and Characters.

## GUI / UX — in scope, not started

A simple premium interface:

- as immediately understandable as a simple professional reverb
- modern, premium visual hierarchy
- no unnecessary visual complexity
- important controls get visual priority
- advanced controls may be grouped rather than every parameter being equally
  prominent

## Performance and quality — in scope

Good CPU efficiency · robust parameter automation · stable state
serialization · strong host compatibility · sample-rate and block-size safety
· **no audio-thread allocation or locking** · thorough regression and
listening validation.

Most of these already have standing gates and suites. State migration is the
one with a known open defect — see the Phase 10 Stage 2 report, §22.

---

## Explicitly OUT of scope for V1

- Shimmer / pitch-shifted reverb
- Supermassive-style extreme delay/reverb systems
- ÜberMod-style dedicated modulation effect architecture
- Large creative FX layers
- Experimental pitch networks
- Extreme nonlinear / feedback creative modes
- Any addition whose primary purpose is matching another company's catalogue

These may be considered for V1.x, V2 or separate ACTIVE DSP products.

> **Do not reserve current architecture complexity merely for these future
> possibilities.** Where existing code already carries such reservations —
> the `Mode` enum's `randomSpace`, `chorusSpace`, `nonlinear`, `chaotic` and
> `experimental` members, for instance — they stay because the enum is
> append-only serialised state, not because V1 owes them an implementation.

---

## The product principle

A feature enters ACTIVE VERB V1 only if it:

1. creates a genuinely useful audible dimension,
2. works predictably,
3. does not unnecessarily compromise the core reverb,
4. justifies its CPU, UX and testing complexity.

If a planned control fails those criteria, **reject it.**

### The Phase 9 decisions are the model

| | outcome | why |
|---|---|---|
| Density | **rejected** | late echo density was already saturated at 0.93–0.99; early density already owned by Early Diffusion, Size and Mod Depth |
| Bloom | **rejected** | no dimension independent of Attack, though Decay was distinguishable |
| Late Diffusion | **rejected** | the only working mechanism *raised* periodicity at Mod Depth 0, depending on modulation to blur its own recurrence structure |
| **Attack** | **retained** | established a real independent dimension: orthogonal to Pre-Delay and Early Diffusion on the metrics each owns |

Three of four candidate controls were rejected on measurement, before any
production code was written for them. That ratio is the discipline, not a
failure.

---

## Remaining V1 work, as product blocks

The internal phase numbering stays compatible with repository history. No
historical phase number is rewritten and no prior research is erased.

| block | covers | status |
|---|---|---|
| **1. Early Reflections / Early-Late** | Phase 10 Stages 3+ | **in progress** |
| **2. Final stereo / spatial validation** | centre stability, mono across sources, Width interaction | not started |
| **3. Algorithm / Mode system** | Room · Hall · Chamber · Plate · Ambient | not started |
| **4. Character / Color system** | Modern · Classic · Vintage | not started |
| **5. Factory preset system** | categories across Modes and Characters | not started |
| **6. Final GUI / UX** | the premium interface | not started |
| **7. CPU / memory optimization** | Attack ~14.7 %, ER ~5 %, tank baseline | not started |
| **8. Host compatibility / state migration / automation** | the Phase-10 state-restore defect, `versionHint` restructure, pluginval across configurations | **partly open** |
| **9. Final product regression, listening and release validation** | the standing suites plus product-level listening | ongoing |

### Known debt carried into these blocks

- **State migration defect**, Phase 10 Stage 2 §22: APVTS leaves a parameter
  absent from a loaded state at its *current* value, not its default, so a
  Phase-9 preset loaded into a live instance with the new control moved would
  not reproduce Phase 9. Must be fixed with a real serialised fixture test.
- **`versionHint` helper shape**: one global constant applied to all 19
  parameters, which is the wrong shape for appending one.
- **Large-Size ER handoff hole**, up to 6.93 dB — a Phase 10 Stage 3 problem.
- **Test-infrastructure debt** from `Docs/Phase7-TestInfrastructureDebt.md`.
- **The Phase-7 FL junction hazard** in `Docs/Phase8-Closure.md`.

---

## Phase 10 continues unchanged

Stage 2 commit `81716c8`. These Stage-2 conclusions remain current and were
**not** revisited because the roadmap became more focused:

- explicit ER retained
- ER-B, 8 events per channel, remains the winning candidate
- Size law `alpha = 0.25` remains the candidate
- BAL-B energy-calibrated constant-power balance remains the candidate
- the Phase-9 full-Late endpoint must remain **bit-exact**
- Attack must remain **structurally** independent of ER
- Pre-Delay Model 1 remains the semantics
- the large-Size handoff hole remains the open Stage-3 problem
- the Phase-9 state-loading defect must be fixed before shipping integration

---

## What was not started

Recording this scope started no feature work. Mode, Character, Presets and GUI
remain not started, and no architecture was changed. The only work continuing
is Phase 10 Stage 3 as already planned.
