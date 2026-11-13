# Phase 7 — test infrastructure debt

**The test suite's idea of "the shipping default" is duplicated by hand in
thirteen places, none of which reads `ParameterLayout`. Eleven of them still
carry the pre-Phase-7 values.** Nothing fails because of it. That is the
problem: eleven sites still *claim* to describe the product and no longer do.

Only one file in `Tests/` reads `ParameterLayout` at all — `GainStaging.h` —
and its `defaults` namespace covers Decay, Size, Mod Depth, Mod Rate, Width and
Mix, with **no damping parameter in it**. So nothing in the suite could have
noticed the change, and nothing did.

This is recorded, not fixed. Fixing it changes the measured output of suites
that currently pass, and it must not be mixed into the Phase 7 production
validation. The cleanup is a separate task, specified at the end.

---

## What happened

Phase 7 changed two production defaults:

```
High Damp        50 %   ->  45 %
High Damp Freq    6 kHz ->  10 kHz
```

`Tests/DampingGate.cpp` had a row labelled `B  shipping default` with
`{ 0.20, 0.50, 200.0, 6000.0 }` written into it as literals. The moment the
default moved, that row stopped testing the product and started testing a
historical operating point under a label that said otherwise. It was found
during the focused regression, updated in `44ae4f6`, and the commit records the
old value, the new value and the fact that **the thresholds were not touched** —
`|T20−T30| ≤ 5 %` and `R² ≥ 0.995` are still the pre-registered numbers.

Then the same search was run across the whole suite. `DampingGate` was not the
only site.

---

## The sites

| file | line | what it says | what it holds |
|---|---|---|---|
| `Tests/DampingTests.cpp` | 528 | `tank.damp (...)` in a default-damping check | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTests.cpp` | 601 | the same, in the default-settings suite | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTests.cpp` | 607 | `info ("default damping (20 % / 50 %, 200 Hz / 6 kHz)...")` | printed text |
| `Tests/DampingTests.cpp` | 749 | `tank.damp (...)` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTests.cpp` | 1119 | a settings row labelled `"default"` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTests.cpp` | 1225 | `tank.damp (...)` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTests.cpp` | 1227 | `info ("default: Low 20 %, High 50 %, 200 Hz / 6 kHz...")` | printed text |
| `Tests/DampingRootCause.cpp` | 887 | `reference.damp (...)` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingRootCause.cpp` | 971 | `tank.damp (...)` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingTopology.cpp` | 911 | a case labelled `"shipping default"` | `0.20, 0.50` |
| `Tests/DampingEnsemble.cpp` | 316 | a row labelled `B  shipping default` | `0.20, 0.50, 200.0, 6000.0` |
| `Tests/DampingGate.cpp` | 313 | a row labelled `B  shipping default` | `0.20, 0.45, 200.0, 10000.0` — **current** |
| `Tests/DampingListening.cpp` | 66 | `kSettingB`, the current default for the A/B set | `0.20, 0.45, 200.0, 10000.0` — **current** |

The last two rows are the sites that are correct today, and they are on this
list anyway: they are hand-copied production defaults like all the others, so
they will drift the next time a default moves. The list is a list of
duplication, not only of staleness.

### What the staleness costs, concretely

`Tests/DampingEnsemble.cpp` prints a block headed `B  shipping default` and
reports 1.392 s at 12 kHz. The product's actual default now decays 1.862 s
there. The number is right for the configuration the row contains and wrong for
the thing the row is named after — which is the worst kind of wrong, because it
reads as a product fact.

---

## What is NOT debt, and must not be "fixed"

Two groups of hard-coded damping values look identical to the ones above and
are deliberate. Changing them would retroactively alter a committed research
result.

**`Tests/DampingSecondOrder.cpp`** — `kDefaultHighDamp`, `kDefaultHighHz` and
the registered `kT0Mismatch*` constants are the baseline the second-order shelf
study was measured against. The study's verdict is a comparison *against the
then-shipping default*; re-pointing it at today's default would make the
recorded conclusion describe a comparison that was never run. Its two failing
checks reproduce `0.99329` and `0.95514`, bit-identical to
`Docs/Phase7-SecondOrderShelfStudy.md`.

**`Tests/DampingSynthetic.cpp`** — `kAurora` is a frozen literal table of
registered measurements. It is evidence, not configuration.

The distinction worth keeping: a site that means *"the configuration the
product opens with"* is debt. A site that means *"the operating point this
study was measured at"* is a record, and records do not get updated.

---

## The deferred cleanup task

To be done after Phase 7 closes, and not before the listening validation.

1. **One contract.** A single header — say `Tests/ShippingDefaults.h` —
   declaring every shipping default as a named constant, with nothing else in
   it.

2. **A drift assertion.** A suite check that compares that header against
   `ParameterLayout`'s declared defaults and fails if they disagree. This is
   the piece that makes the exercise worth doing: without it, the single
   contract is just a fourteenth copy. `Tests/GainStaging.h` already states the
   intent for the parameters it covers — "the test suite checks these against
   ParameterLayout so the duplication cannot drift" — so extending that
   namespace and its check to the damping parameters is the natural home.

3. **Migration, one site at a time, deciding what each one means.** A site that
   means "the shipping default" moves to the contract. A site that means "this
   historical operating point" keeps its literals and gets a comment saying so.
   Each migration changes measured output, so each needs its new numbers
   recorded and justified — not silently updated.

The reason for the ordering is simple. Right now the suite's damping figures at
the "default" are a known, documented lie with a known cause. After a careless
mass-update they would be an unknown quantity, and the listening validation
would be the first thing to run against them.
