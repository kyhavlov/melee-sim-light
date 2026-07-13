# Decision 0001: First Target Is the Fighter Map-Collision Owner

Status: accepted; Phase 1 architectural cutover complete.

Date: 2026-07-11

## Decision

Begin the ambitious rewrite program with the full fighter map-collision owner:

```text
MSLSTG01 -> mpLib -> persistent CollData -> mpColl -> live Coll callback -> motion entry
```

Introduce the minimum source-shaped MotionState kernel required for exact collision callback
dispatch and collision-owned transitions as part of this vertical slice. Do not ship a detached
scheduler conversion first.

## Context

Three architectural candidates were audited independently:

1. MotionState entry, callback order, and causal scripts.
2. Stage/mpColl/map-callback collision.
3. Fighter contact, hitlists, DmgLog, ProcessHit, grab, and throw.

All three audits found the same root pattern: exact source data exists, while the runtime infers
source ownership from action families, current/previous public state, and replay provenance.

## Why map collision wins the first slot

- It has a nearly complete bounded source implementation to port.
- Its current surface is about 28k LOC versus about 9-10k relevant source LOC.
- It contains explicit evaluator-state gameplay branches and 41 late reject bits that the source
  state machine should eliminate.
- Newer-character replays account for 96% of `on_ground` and about 98% of `ground_id` mismatches,
  a strong concentration signal even though the rows have not all been causally classified.
- Collision callbacks directly own many landing, floor-loss, wall, ceiling, passive, and ledge
  motion changes.
- Prior local profiles identify collision as a major hot path, so the rewrite has credible
  performance upside in addition to correctness and size; the gain remains to be measured.
- It is a meaningful vertical proving ground for exact callback dispatch and procedural motion-entry
  flags.

## Alternatives considered

### Standalone MotionState/scheduler kernel first

Rejected as a reviewable first packet. It is the right architecture, but converting dispatch while
retaining all global subsystem passes would add a second ownership layer and might produce little
immediate validation improvement. The kernel should arrive with a complete family that deletes old
behavior.

### Stateful fighter-contact engine first

Strong alternative and selected second. It has a larger contact-shaped mismatch surface and
substantial deletion opportunity. It also depends on causal script execution, transition semantics,
stable multi-player traversal, shield/body precedence, and item/throw integration. Doing it second
lets it reuse a proven lifecycle kernel; DamageFlyRoll RNG policy stays outside the deterministic
contact rewrite.

### Common locomotion/defense callbacks first

Reasonable low-risk vertical slice, but current Fox/Falco common actions are comparatively mature.
The expected validation delta is less clear, and the work would not attack the largest size/perf
outlier. Common collision callbacks naturally include a large part of the same locomotion/defense
surface with stronger direct evidence.

### Grab/throw first

Rejected. The 4.3k-LOC grab/throw surface is comparatively source-shaped. Rewriting it before
contact would preserve APIs and geometry owners that the contact engine should remove.

## Required scope of the first cutover

The first reviewable packet must include:

- source-oriented stage line and required mpLib operations;
- persistent CollData fields for migrated callbacks;
- ordered mpColl wrapper behavior;
- stable live Coll callback identities initialized from extracted symbols, including explicit
  source callback overrides;
- immediate source callback transitions through central motion-state entry;
- causal entry/script/HitCapsule effects needed by every migrated transition destination;
- an explicit ledger for all supported-domain Coll callback identities and atomic all-character
  cutovers for each selected shared callback family;
- deletion of displaced floor/wall/ceiling/ledge phase inference, reject packets, post-collision
  transitions, and replay gameplay bridges.

It may be developed through smaller internal steps, but a bare data/map or CollData substrate is not
a sufficient checkpoint.

## Original exit criteria and superseding policy

The original plan used zero hard validation regression as a strict exit criterion. During the full
cutover, that gate proved capable of preserving the very compensating machinery the rewrite was
meant to delete. [Decision 0002](0002-source-completion-first.md) supersedes that validation policy.

The retained exit criteria are:

- the declared map-collision architecture has one source-shaped runtime;
- all supported callback identities are migrated or explicitly outside the declared boundary;
- displaced collision coordinators and gameplay bridges are deleted;
- no runtime allocation or evaluator-state branch;
- no meaningful loss in random and replay performance gates;
- validation is regenerated and any result disproving the architectural claim is resolved.

Packets 1–5 meet this architectural boundary. Semantic source completion continues as the branch
ports the MotionState and contact owners that feed and consume collision state.

## Revisit conditions

Revisit the order only if the MSLSTG01 field audit finds a genuine source-data blocker or CollData
reseed initialization cannot be bounded without new replay instrumentation. Discovery of hard work
is not itself a reason to switch targets.
