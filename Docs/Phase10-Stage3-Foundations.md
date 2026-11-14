# Phase 10 Stage 3 — foundations

The first three items of the Stage-3 plan, in the order the Stage-2 report
specified, "because each step makes the next one safe":

1. **Fix the state-restore defect and build a real fixture test** — done
2. **Restructure the `versionHint` helper** — done
3. **Solve the large-Size handoff hole** — **partly solved, honestly bounded**

Items 4–7 of that plan — building the shipping ER stage, re-proving the
independence gates on it, adding the public `earlyLate` parameter, and
listening — are **not** started. The Stage-2 instruction not to add the public
parameter has not been lifted, and no new feature was begun.

---

## 1. The state-restore defect — fixed and tested

### The defect

`AudioProcessorValueTreeState::replaceState` assigns the tree and then
reconnects only the parameters that **have a child in it**:
`updateParameterConnectionsToChildTrees` walks the new tree's children and
calls `setNewState` once per child found. A parameter with no child is never
visited, so it keeps whatever value it currently holds.

| scenario | before the fix |
|---|---|
| Fresh instance + older session | parameter is at its default → correct by accident |
| **Live instance with a newer control moved, loading an older preset** | **parameter keeps the moved value → the preset does not reproduce the sound it was saved from** |

This matters from Phase 10 onwards specifically. Phases 8 and 9 added no
parameters — they consumed IDs that already existed, so every saved state
mentioned every parameter and nothing was ever missing. **Phase 10 is the
first phase to append one.**

### The fix

`AuroraVerbProcessor::resetParametersAbsentFrom (const juce::ValueTree&)`,
called from `setStateInformation` **after** `replaceState`, sets every
parameter the loaded tree does not mention back to its default.

Applied to **all** parameters rather than to a maintained list of recent
additions: a list would have to be extended with every future parameter, and
forgetting once would reintroduce this defect silently.

It sits inside the branch that only runs on a tree with the right tag, so
unparseable or empty input leaves the state alone. An empty tree mentions no
parameters, and treating that as "reset everything" would destroy a session on
a malformed load — a worse defect than the one being fixed.

### The test — a separate target, and why

`Tests/CMakeLists.txt` states the rule and enforces it by compilation:
nothing in `Source/DSP` may depend on JUCE or on the processor, and the DSP
suites deliberately do not link JUCE. That rule is worth keeping.

But a state-compatibility test is *about* the processor and about APVTS. The
Stage-2 report said "do not rely only on source-code reasoning", and reasoning
is exactly what missed this defect. So there is now a **second, small
executable**, `AuroraVerbStateTests`, which links JUCE and the plugin's shared
code and drives a real serialised fixture.

The fixture is **generated, not checked in as a binary**, and generated the
only way that proves anything: by asking the processor to serialise a state and
then removing one parameter's child, so the structure, property names and XML
wrapping are exactly what the plugin itself writes rather than a hand-made
approximation that might differ in a way that masks the defect.

### Results: 8 / 8

| check | result |
|---|---|
| No parameter carries version hint 0 | **PASS** (an AU-only debug assertion) |
| Every shipped parameter still at generation-1 hint | **PASS** — 19 of 19 |
| The fixture serialised | **PASS** — 18 children, `attack` omitted |
| A **fresh** instance leaves the omitted parameter at its default | **PASS** |
| A **live** instance also returns it to its default | **PASS** — this is the case that used to fail |
| Every parameter the state mentions is restored within its own step | **PASS** |
| Save → load → save → load is stable across every parameter | **PASS** |
| Unparseable or empty state leaves every parameter untouched | **PASS** |

The live-instance case is the whole point: with the control moved to 0.65 and
an older state loaded, it now reads its default.

> **One measurement correction.** The round-trip check first used a fixed
> normalised tolerance of 1e-5 and failed on `decay` and `lowCut` by 1.2e-5 and
> 1.7e-5. Those were not restore failures. Both are **skewed** parameters;
> APVTS stores the denormalised value as a float, and converting
> normalised → denormalised → float → normalised back through an inverse skew
> amplifies the float's relative error far more than a linear range does.
> 1.7e-5 normalised over `lowCut`'s 980 Hz range is **0.017 Hz**, and the
> parameter's own step is **0.1 Hz** — the discrepancy is smaller than the
> smallest value the control can represent. The comparison is now made in each
> parameter's own units against its own `interval`, which is a property of the
> parameter rather than a number chosen to make the test pass.

---

## 2. `versionHint` — restructured

### What it actually does

Read off the JUCE 8.0.15 tree this project fetches:

- `ParameterID(identifier, versionHint = 0)`, documented as "Influences
  parameter ordering in Audio Unit plugins".
- Consumed in exactly one functional place: the AU wrapper sorts the parameter
  list by identifier and then **stable-sorts by hint**, so lower hints come
  first (`juce_audio_plugin_client_AU_1.mm:2347`).
- Plus an AU-only debug assertion if any parameter has hint 0.
- **Not referenced anywhere in the VST3 wrapper**, and it plays no part in
  state save or load — APVTS keys state by the ID string.

### What was wrong

`ParameterIDs.h` carried one constant with the comment *"Bumped whenever
parameters are appended"*, and `ParameterLayout.cpp` had a single
`pid()` helper using it.

**That comment described the wrong mechanism and would have caused the problem
it was trying to avoid.** A shared constant gives an appended parameter the
*same* hint as the existing ones, which places it **among** them in the
identifier sort — exactly what the hint exists to prevent. And raising the
shared constant would change every existing parameter's hint at once, which
reorders the AU list for hosts that have cached it.

### The structure now

```cpp
inline constexpr int versionHintV1     = 1;   // the nineteen shipped. Never change.
inline constexpr int versionHintV2     = 2;   // PHASE 10 and later additions.
inline constexpr int versionHintLatest = versionHintV2;
inline constexpr int versionHint       = versionHintV1;   // deprecated spelling
```

with two helpers:

```cpp
juce::ParameterID pid (const char* name)          { return { name, versionHintV1 }; }
juce::ParameterID appendedPid (const char* name)  { return { name, versionHintLatest }; }
```

The rule, written where it will be read: **each generation of parameters gets
its own constant, earlier constants never change, and a new parameter uses the
newest one. Append constants; never edit one.**

`appendedPid` is unused today — Phase 10 has not added its parameter. It exists
so that when one is added, the correct call is the obvious one. The old
`versionHint` spelling is kept so that no call site changes meaning, marked
deprecated because it reads as "the current hint" and is not.

The suite checks both invariants: no hint is 0, and all 19 are still at
generation 1.

---

## 3. The large-Size handoff hole — partly solved

### First, a measurement correction that changes the Stage-2 numbers

Stage 2 measured the handoff notch **on the left channel alone**. That is
wrong for this question, and measuring it the same way here produced a **40 dB
"notch"** that was not a handoff at all.

With a decorrelated ER layer each channel carries only half the arrivals. At 8
events per channel over a 98 ms span the per-channel gap is about 12 ms — wider
than the 10 ms excitation burst — so there is real silence between blobs **in
each channel separately**. Those gaps are a stereo property; the two channels
fill each other's. The question "is there an energy hole between the ER layer
and the late field" is about the field a listener is in, which is both channels
at once.

The notch is now measured on `sqrt(l² + r²)`. Stage 2's grid, re-measured:

| alpha | Size | ER span | late 1st | overlap | notch b=.25 | notch b=.5 | notch b=.75 |
|---|---|---|---|---|---|---|---|
| 0.25 | 0 % | 58.6 ms | 8.6 ms | 50.0 ms | 1.20 dB | 0.26 dB | 0.39 dB |
| 0.25 | 20 % | 65.0 ms | 12.0 ms | 53.0 ms | 1.37 dB | 0.18 dB | 0.28 dB |
| 0.25 | 60 % | 80.0 ms | 24.8 ms | 55.2 ms | 2.86 dB | 0.29 dB | 0.22 dB |
| 0.25 | 80 % | 88.8 ms | 36.2 ms | 52.6 ms | 5.12 dB | 1.55 dB | 0.21 dB |
| 0.25 | **100 %** | 98.5 ms | 54.0 ms | 44.5 ms | **9.03 dB** | 4.92 dB | 0.63 dB |
| 0.50 | 100 % | 121.3 ms | 54.0 ms | 67.3 ms | 6.90 dB | 3.30 dB | 0.63 dB |

Two things the stereo measure reveals that the single-channel one obscured:

- **The hole only appears when the ER layer is quiet.** At balance 0.75 it is
  0.63 dB at *every* Size. At balance 0.25 it reaches 9.03 dB.
- Stage 2's 6.93 dB figure becomes **9.03 dB** at alpha 0.25 and 6.90 dB at
  alpha 0.50 — so the overlap argument still holds (more overlap, slightly
  smaller notch) but the magnitudes move.

### The envelope sweep

The envelope is `exp(-t/(W·decay))/sqrt(t)`, so `decay` scales the
exponential's time constant **against the window** — which is why a longer
window stretches the same shape and cannot help. Sweeping it where the hole is
worst:

| Size | decay | ER tail | notch b=.25 | notch b=.5 | notch b=.75 |
|---|---|---|---|---|---|
| 80 % | **0.60** | −24.3 dB | 4.30 dB | 1.02 dB | 0.19 dB |
| 80 % | 1.00 | −19.0 dB | 3.54 dB | 0.84 dB | 0.19 dB |
| 80 % | 1.50 | −16.4 dB | 3.14 dB | 0.86 dB | 0.19 dB |
| 80 % | 2.50 | −14.3 dB | 2.91 dB | 0.94 dB | 0.19 dB |
| 80 % | 4.00 | −13.1 dB | 2.78 dB | 1.01 dB | 0.19 dB |
| 100 % | **0.60** | −24.3 dB | **8.09 dB** | 2.83 dB | 0.63 dB |
| 100 % | 1.00 | −19.0 dB | 6.74 dB | 1.92 dB | 0.63 dB |
| 100 % | **1.50** | −16.4 dB | **6.04 dB** | 1.62 dB | 0.63 dB |
| 100 % | 2.50 | −14.3 dB | 5.47 dB | 1.48 dB | 0.63 dB |
| 100 % | 4.00 | −13.1 dB | **5.15 dB** | 1.44 dB | 0.63 dB |

`ER tail` is the last arrival's gain relative to the first — the shape number
that `decay` changes.

### The verdict: confirmed, and partial

> **Stage 2's elimination argument is confirmed.** A flatter envelope closes
> part of the hole, monotonically, at every Size and balance where there was a
> hole: 8.09 → 5.15 dB at Size 100 %, balance 0.25. Nothing else changed.
>
> **But it does not close the hole.** 5.15 dB remains at the worst corner, and
> the sweep is well into diminishing returns by decay 2.5.

The residual is **structural, not envelope-shaped**. At Size 100 % the late
field's first arrival is 54 ms. Even a perfectly flat ER layer spanning 98 ms
has to cover 0–54 ms alone, and at balance 0.25 the ER layer is well down
relative to the late plateau that follows — so the sum rises from a quiet early
region to a loud late one with a dip in between. That dip belongs to **the
tank's delay set at large Size**, not to the ER design.

### Recommended production value: `decay = 1.5`

It takes 70 % of the available improvement (2.05 dB of 2.94) and keeps the ER
tail at −16.4 dB. Going further buys little and pushes against the constraint
the span ceiling exists to protect: an ER layer still substantially present at
98 ms is edging toward being a second late reverb.

### What remains for Stage 3's later items

Three options, none of them "add a handoff processor":

1. **Accept it with a Size-dependent budget.** The hole is confined to Size
   ≥ 80 % at ER-light balances, and it is at most 0.63 dB wherever the ER layer
   is prominent. Gate it as such and let listening decide.
2. **Revisit the balance law's low-ER end.** The hole exists because the ER
   layer is quiet there; a law that keeps it slightly more present would
   reduce it — but that changes what the control means, so it is a product
   decision rather than a fix.
3. **Make the ER span's Size law depend on the late onset** rather than on the
   Size scalar, so the layer always reaches past wherever the late field
   actually starts. More complex, and it couples the two layers more tightly
   than Stage 1 wanted.

My recommendation is **(1)**, with the residual measured and stated, because it
is the only one that does not trade a known small artifact for an unknown
semantic change. It needs listening before it is settled.

---

## Regression

| | |
|---|---|
| `AuroraVerbStateTests` | **8 / 8** |
| Phase 10 Stage 2 suite | **10 / 10** |
| Focused DSP regression | the two registered Phase-7 diagnostics only, values unchanged |
| Plugin build | clean |

No Phase 7, 8 or 9 behaviour was touched. The only `Source/` changes are the
state-restore fix, the `versionHint` restructure, and the declaration they
need.

---

## What was not done

- The shipping ER DSP stage. Not started.
- The public `earlyLate` parameter. **Not added** — the Stage-2 instruction
  stands and was not lifted.
- Re-proving the independence gates on a production split. There is no
  production split yet.
- Listening validation.
- Mode, Character, Presets, GUI. Not started, per the V1 scope note.

---

## STOP

Stage 3 foundations end here. The next step is items 4–6 of the Stage-2 plan —
building the ER stage with an explicit bypass branch, re-proving the gates on
it, and only then adding the parameter — which needs authorisation because it
adds production DSP and a public parameter.
