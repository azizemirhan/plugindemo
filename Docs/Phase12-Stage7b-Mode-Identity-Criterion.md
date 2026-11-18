# Phase 12 Stage 7b — A Mode-identity criterion for future work, pre-registered

Research only. `Source/` is unchanged. **Nothing in this document has been
built, run or measured.** It is written and committed before any structure
is designed against it.

**Status: APPROVED on 2026-11-18 with one modification, and frozen.** The
modification replaces limit 4 of section 2; the original wording and the
reason are kept in section 10. The first version is commit `366d7e7`.

## 0. Scope

- This criterion (called **MI** below) applies to **future** candidates
  only. Chamber is the first Mode it is written for.
- **No earlier verdict changes.** The fourteen Chamber structures, the Room
  families R1–R6 / RS / RR, the Ambient repairs and every other recorded
  result stay exactly as recorded, under the gates they were measured
  against. None of them will be re-run under MI.
- **No existing threshold moves.** The frozen metric (thirteen families,
  worst-family verdict), REPRODUCED ≤ 1.5, DISTINCT ≥ 3.0, the seven-control
  search (frozen at `c18ecce`), the Stage 6 lines for Decay, Size, tone,
  ringing, mono and CPU are all used here with their existing values.
- **The Room pre-registration is not touched.** RN1–RN3 stay under the gates
  written in `Phase12-Stage7-Room-Preregistration.md`. Whether Room should
  later be judged under MI as well is a decision for review, not made here
  (section 8).
- What MI changes is **where** the existing thresholds must be met, and it
  adds requirements the old gates did not have.

## 1. Why the old rule is revised

The rule so far (G1 / G2): at three operating points, the reference Mode's
search, with all seven visible controls free, must fail to reproduce the
candidate — no run ≤ 1.5 and every median ≥ 3.0 — at **every** point.

Stage 7's study found that at short Decay a chamber is physically a room:
same mean free path, same reflection count. A rule that demands strong
separation at every point therefore rejects a correct Chamber for behaving
the way the real thing does. That is the error to remove.

The error not to introduce: a "Mode" that is another Mode with its visible
controls moved. Stages 3–6 recorded many of these, and each looked like a
new structure until Hall's search reproduced it.

So two different things have to be told apart:

- **A. Preset equivalence.** There is a setting of another Mode's visible
  controls that gives the same reverb, over the range the Mode is meant to
  be used in.
- **B. A different identity that converges somewhere.** The two are
  different algorithms with different behaviour over most of the control
  range, and become alike in a corner where the physics makes them alike.

The old rule cannot separate A from B because it looks at three points and
treats each as equally decisive. MI looks at the whole Size–Decay plane and
asks how much of it the overlap covers, and where.

## 2. Definitions

**The grid.** Nine operating points: Size 25 / 60 / 85 × Decay 0.8 / 2.5 /
5.0 s. The three legacy points (small / short, medium, large / long) are its
diagonal. All other controls at their defaults in the candidate. Centred
source, as before.

**Reference Modes.** Hall always. Then every other acoustic-space Mode that
exists as a candidate or better at the time (Room, Chamber). Plate and
Ambient are measured in both directions as in Stage 6 section 4, under the
old all-points rule; MI's periphery (below) does not apply to them.

**A search result at a point**, as now: five seeds; the point is
- DISTINCT if no run ≤ 1.5 and the median ≥ 3.0;
- REPRODUCED if any run ≤ 1.5;
- BETWEEN otherwise.

**Core and periphery.** Before a candidate is built, its pre-registration
names, per reference Mode, which grid points are *periphery* — points where
convergence with that reference is expected — and gives the physical reason.
Everything else is *core*. Limits on that declaration:

1. At most **three** of the nine points per reference.
2. They must be **one edge or corner** of the grid (for example the
   short-Decay row), not scattered points.
3. The medium point (60, 2.5 s) — the plugin's default — is never periphery.
4. *(As amended, section 10.)* A point may be declared periphery for
   **more than one** reference only if those references are themselves
   demonstrated to be mutually close or reproducible **at that same point**
   under the frozen metric. Otherwise overlapping declarations are not
   permitted.

   How "demonstrated" is read here: before the candidate's declaration is
   written, each of the two references' free seven-control searches is run
   against the other as target at that point, five seeds, and the point is
   REPRODUCED in **both** directions. The figures are recorded with the
   declaration. A BETWEEN result does not qualify. (The approved wording is
   "mutually close/reproducible"; taking it as REPRODUCED both ways is the
   stricter reading and is mine. If BETWEEN should also qualify, that needs
   a ruling before the first declaration that relies on it.)
5. The declaration is made once, before the build, and is not revised after
   a measurement. A candidate that converges somewhere it did not declare
   fails; it does not get its periphery redrawn.

## 3. The criterion

A candidate holds a Mode identity under MI only if **all** of MI-1 to MI-5
hold. Each is necessary; none is sufficient.

### MI-1 — Quality preconditions (unchanged values, all nine points)

Before any distinctness result counts:

- modal prominence clean: ≤ Hall + 3 dB (16.8 dB);
- octave bands within 1 dB of Hall;
- RT60 within ± 10 % of the Decay control;
- mono fold-down within 1.5 dB of Hall's, worst mono tone no more than 3 dB
  deeper;
- CPU ≤ 1.5 × Hall.

A candidate that is distinct only where it rings or is coloured is recorded
as *distinct by ringing* or *distinct by tone* and rejected, as the Room
pre-registration already does.

### MI-2 — Not a preset of another Mode (core points)

For each reference Mode, at **every core point**: DISTINCT under that
reference's free seven-control search, in the direction
reference-reproduces-candidate.

Also run and reported in the other direction (candidate's search against the
reference as target). A reference that the candidate reproduces across the
core is reported as a finding about the *reference's* redundancy; it does
not by itself fail the candidate.

Why this excludes presets: the frozen search frees all seven controls
**independently at every point**. A preset is one fixed relation between the
two Modes' controls. The search is therefore strictly more generous to the
reference than any preset could be. If the search cannot reproduce the
candidate at a point, no preset can.

### MI-3 — Bounded convergence (periphery points)

At a declared periphery point, against the declared reference only, the
result may be DISTINCT, BETWEEN or REPRODUCED. It is recorded as measured.

Limits:

- against every **other** reference that point is core and MI-2 applies;
- the count is fixed by section 2: at most three points per reference, so
  the candidate is DISTINCT from each reference at **six of nine points or
  more**, always including the default;
- where a periphery point is REPRODUCED, the reproducing control settings
  are recorded. If the same settings of the non-Size, non-Decay controls
  also reproduce the candidate at any core point, that is MI-2 failing and
  is reported in those words.

### MI-4 — A positive identity that is stable (all nine points)

MI-2 and MI-3 say what the candidate is *not*. A Mode also has to *be*
something, and be it everywhere — including the points where it is allowed
to resemble a neighbour.

Each Mode's pre-registration lists its signature as measurable properties
with thresholds, fixed before the build. The signature must hold at **all
nine points**. A candidate that is distinct in the core but loses its own
signature at the periphery has not converged with a neighbour; it has
collapsed, and fails.

For Chamber, from the Stage 7b decision, the signature is:

| property | measured with | form of the threshold |
|---|---|---|
| rapid echo-density buildup | the metric's echo-density family: time to Gaussian density | no later than a stated fraction of Hall's time at the same knobs, at every point |
| dense, smooth reflective field | late texture (short-time fluctuation, 50–200 and 200–500 ms) | at or below Hall's at the same knobs |
| weak obvious wall-slap | the first 50 ms: no single arrival standing above the running level by more than a stated margin | a stated crest limit |
| lower objectionable resonance than Room | modal prominence | ≤ Hall + 3 dB (MI-1), and below the Room reference's at every point |
| useful medium-scale character | not measurable; MI-5 | — |
| predictable Decay and Size | RT60 against Decay; t10 against Size | ± 10 %; Size monotone and "meaningful" as Stage 6 defined it |

The three thresholds marked "stated" are **not fixed in this document.**
They are to be fixed in an addendum from measurements of Hall alone, on the
nine-point grid, before any Chamber structure exists. I have not picked
numbers for them here because I do not have Hall's values on the six new
grid points, and a number chosen without them would be a guess.

A warning that belongs here, not later: Stage 7's study found that this
signature — dense at once, smooth, no size being revealed — is what Hall
does at small Size. **MI-4 is therefore not what separates a Chamber from
Hall; MI-2 is.** MI-4 only guarantees that whatever passes MI-2 is
recognisably a chamber and stays one. The criterion does not make the
Hall-against-Chamber problem easier, and is not meant to.

### MI-5 — Listening

Unchanged in principle: a blind, level-matched package against each
reference, catch trials included, judged by a human. A candidate that
passes MI-1 to MI-4 has earned a listening test, nothing more. I do not
make the perceptual decision.

## 4. Verdicts

| verdict | condition |
|---|---|
| **PRESET-EQUIVALENT** | REPRODUCED by one reference at the default point, or at four or more of the nine points |
| **NOT DISTINCT** | MI-2 fails at any core point without reaching the line above |
| **COLLAPSES** | MI-2 and MI-3 hold, MI-4 fails at any point |
| **DISTINCT BY DEFECT** | distinct only where MI-1 fails |
| **RELATED FAMILY — eligible for listening** | MI-1 to MI-4 all hold, with at least one periphery point not DISTINCT |
| **SEPARATE FAMILY — eligible for listening** | MI-1 to MI-4 all hold and every point is DISTINCT against every reference |

Only the last two go forward. Every verdict is reported with the full
nine-point table per reference; the label never stands in for the table.

## 5. How it avoids both errors

**Accepting a fake preset.**

| a fake preset would be … | stopped by |
|---|---|
| another Mode with visible controls moved | MI-2: the free per-point search is more generous than any preset |
| another Mode with a hidden constant changed (a different delay set, a different diffuser length) | MI-2: this is the "Hall's sibling" case, which the frozen search already reproduces |
| distinct only by ringing or tone | MI-1 |
| distinct in a narrow band of settings chosen to suit it | section 2: the core is six points or more, includes the default, is declared before the build and cannot be redrawn |
| allowed to resemble Hall in one corner and Room in another until nothing is left | section 2, limits 1–3: three points per reference, one edge each, never the default |
| allowed to resemble two different Modes at one point, and so be nothing there | section 2, limit 4: only where those two Modes are measured to be the same thing at that point — in which case resembling both is resembling one |
| distinct from its neighbours but not actually a chamber | MI-4 |
| acceptable on paper and indistinguishable by ear | MI-5 |

**Rejecting a legitimate related family.**

| a real Chamber would have failed the old rule because … | under MI |
|---|---|
| at short Decay it is physically a room | the short-Decay row may be declared periphery against Room, with that reason; MI-3 |
| one of three points decided everything | nine points; a declared corner cannot veto |
| a BETWEEN score at an extreme was read as failure | permitted at declared periphery; still a failure in the core |
| at small / short Hall, Room and Chamber all converge, so no single neighbour could be named | limit 4 as amended: the point may be periphery for both, once the two references are shown to reproduce each other there |

**What MI deliberately does not relax.** The thresholds; the search; the
default point; the core; the quality lines; the requirement of a positive
identity. A candidate cannot use MI to turn a three-point failure into a
pass unless its failure was confined to one declared edge against one
declared neighbour.

## 6. Calibrating the instrument before using it

MI is a new instrument and has to be shown to read known cases correctly
before it is trusted on an unknown one. Three controls, run on the
nine-point grid before any Chamber candidate:

| control | known nature | MI must read |
|---|---|---|
| Hall with its sibling delay set | a preset in all but name | PRESET-EQUIVALENT against Hall |
| Plate PV3 | a different family (Stage 6, three points) | DISTINCT against Hall at the points where the old gate found it so; the six new points are reported, not predicted |
| Hall at a fixed different setting of Early/Late, diffusion and Width | a literal preset | PRESET-EQUIVALENT |

If MI misreads the first or third, it is not fit for use and is reported so.
The Plate row is a measurement, and whatever it shows on the six new points
is information about Plate that does not exist yet; **it does not reopen
Plate**, whose gate is the Stage 6 one.

None of the fourteen rejected Chamber structures, and no rejected Room or
Ambient structure, is among the controls. That is deliberate: running them
would be a re-trial under a new rule.

## 7. Cost

The old G2 was three points × five seeds per reference. MI is nine points ×
five seeds per reference and direction: three times the search time per
reference, and Chamber has up to two references under MI (Hall, Room) plus
Plate and Ambient under the old rule. This is hours of search per candidate.
It argues for the order in the Room pre-registration — a cheap decisive
check first — and MI keeps that: MI-1 before anything; then the default
point alone; a REPRODUCED there ends the candidate.

## 8. Open points for review

These are choices I made in writing MI. Each could reasonably go the other
way, and none has been tested.

1. **Three of nine.** The periphery limit is a judgement, not a derivation.
   Two would be stricter; a whole row plus a corner (four) would be looser
   and would meet the PRESET-EQUIVALENT line.
2. **Periphery against Hall.** MI allows it, under the same limits. The
   decision text says a Chamber "must NOT be merely Hall or Room with
   visible user controls changed", which MI-2 enforces in the core. If Hall
   should get no periphery at all, that is one line to change and it makes
   MI stricter.
3. **No point periphery twice.** This is my addition. At small / short all
   three acoustic-space Modes tend to converge physically, so this rule may
   be the one that bites hardest. — **Decided 2026-11-18: replaced.** See
   limit 4 and section 10.
4. **A reference that does not exist.** Chamber against Room needs a Room.
   If no RN hypothesis survives, MI for Chamber runs against Hall, Plate
   and Ambient only, and the Room-against-Chamber question is deferred, not
   answered.
5. **Room under MI.** RN1–RN3 are registered under the old three-point
   rule with small / short run first. They stay there unless you decide
   otherwise. Applying MI to Room would let a Room converge with Hall at a
   declared edge — which is the opposite of what the Stage 6 decision asked
   of Room ("must survive even at short Decay").
6. **The signature thresholds** of MI-4 are owed in an addendum, from
   Hall-only measurements.

Decisions of 2026-11-18 on the other points: the three-of-nine limit, the
one-edge rule, the default point and the six-point minimum are kept as
written (points 1 and 2). The signature thresholds (point 6) stay unfilled
until Hall's baseline on the nine-point grid exists. Points 4 and 5 were
not ruled on and remain open.

## 9. What this does not do

It does not produce a Chamber. Stage 7's finding stands: as a late field a
chamber is a point in Hall's Size–Decay plane, and MI-2 will test exactly
that at six or more points. MI removes one wrong reason to reject a Chamber.
It does not supply a mechanism, and none is proposed here.

## 10. Amendment record

**2026-11-18 — limit 4 of section 2 replaced, by decision.**

As first written (commit `366d7e7`):

> 4. No point may be periphery for **two** references. Where a candidate is
>    allowed to resemble Room it must be distinct from Hall, and the reverse.

As decided:

> A grid point may be declared convergent with more than one reference Mode
> only if those reference Modes are themselves demonstrated to be mutually
> close/reproducible at that same operating point under the frozen metric.
> Otherwise overlapping convergence declarations are not permitted.

Reason given: the original rule would reject a candidate artificially at a
point where existing acoustic-space families naturally converge; the
replacement removes that without opening a loophole for fake Modes, because
the overlap has to be earned by a measurement on the references and not
asserted by the candidate.

What did not change: three points per reference at most; one predeclared
edge; never the default point; at least six points DISTINCT under the
existing gates; the candidate's own signature valid at all nine points; no
threshold; no earlier verdict.

One consequence worth stating: the amended limit needs a measurement that
does not exist yet — the references against each other on the grid. Until
it is run for a given point, that point can be periphery for one reference
only.
