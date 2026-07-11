# Core Rewrite Program

Status: Packet 1 (common grounded map callbacks) complete and awaiting review; Packets 2-5 remain.

Baseline revision: `a293980533bd` (`Improve and benchmark simulator performance`), 2026-07-11.

## Purpose

This program replaces piecemeal, replay-shaped simulator implementations with small, persistent,
source-shaped owners. The goal is not a cosmetic refactor. A successful rewrite must do all of the
following:

- materially improve aggregate one-step and rollout validation without fitting individual replay
  rows;
- delete compensating branches, duplicated state, and semantic query APIs;
- preserve the allocation-free batched runtime and deterministic ordering;
- preserve or improve both random-input and replay-derived performance;
- leave one authoritative runtime path for each migrated owner.

The motivating observation is that the simulator now contains about 140k lines of C under `src/`,
yet most aggregate replays still have low-double-digit or low-hundreds discrete mismatches. Several
core systems are larger than the corresponding source surfaces because the live source state and
callback order were reconstructed a case at a time.

## Current recommendation

The first vertical rewrite should be the complete fighter map-collision owner:

```text
MSLSTG01 stage lines
  -> source-shaped mpLib queries
  -> persistent CollData
  -> mpColl wrapper
  -> live installed Coll callback
  -> immediate contact/action publication through central motion-state entry
```

This is deliberately broader than cleaning up a floor-collision file. It includes the minimum
MotionState execution kernel needed to dispatch the current collision callback and to perform
collision-owned action changes correctly, including causal AObj/script/HitCapsule entry semantics
for migrated destinations. It does not include a detached, behavior-neutral scheduler conversion.

The stateful fighter-contact engine is the recommended second rewrite. It has at least as much
eventual correctness leverage, but it depends on causal script execution and central motion-state
entry and has more simultaneous-player, shield, hitlist, item, and branch-order risk.

The decision and alternatives are recorded in
[decisions/0001-first-target-map-collision.md](decisions/0001-first-target-map-collision.md).

## Governing invariants

1. **Source owner, not report row.** Validation identifies leverage and regressions. Decomp and
   extracted data define behavior.
2. **Live state, not repeated reconstruction.** If Melee owns a persistent `CollData`,
   `HitCapsule`, command cursor, callback, or pending contact field, model that state directly.
   Per-invocation scratch such as DmgLog arrays should remain bounded scratch. Replay preprocessing
   may initialize live state once; gameplay must not branch on evaluator state.
3. **Exact phase ownership.** Motion entry initializes live callback identities from the selected
   MotionState row, and explicit source sites may overwrite them later. Snapshot exactly one live
   callback for each phase. A callback may change motion state, but the destination callback is not
   recursively executed in the same phase.
4. **Procedural flags stay procedural.** The flags passed to `Fighter_ChangeMotionState` are not the
   selected MotionState row's `x4_flags`. Call sites must pass the source-backed transition flags.
5. **Atomic owner cutover.** Temporary debug comparison is allowed. A reviewable packet must replace
   and delete a complete owner slice rather than retain two gameplay implementations.
6. **No hard validation regression.** Existing Fox/Falco controls are especially valuable locks;
   newer-character improvement does not excuse regressions there.
7. **Deletion is an outcome, not a quota.** LOC estimates rank opportunities. Source-complete code
   should not be distorted to hit a numeric deletion target.

## Document map

- [baseline/validation.md](baseline/validation.md): aggregate correctness baseline and how to read it.
- [baseline/architecture.md](baseline/architecture.md): code/data inventory and recurring structural faults.
- [candidates/motion-state-kernel.md](candidates/motion-state-kernel.md): the required execution kernel.
- [candidates/map-collision.md](candidates/map-collision.md): recommended first vertical rewrite.
- [candidates/contact-engine.md](candidates/contact-engine.md): recommended second vertical rewrite.
- [ROADMAP.md](ROADMAP.md): implementation order, cutover protocol, and completion gates.
- [callback-ledger.md](callback-ledger.md): the five map-collision packets and current cutover state.
- [packets/01-common-grounded.md](packets/01-common-grounded.md): Packet 1 implementation and gates.

The older `agent_docs/systems/` closure documents remain useful histories of the current model.
Their `CLOSED` labels are scoped validation claims, not evidence that the implementation is a
minimal or source-complete architecture. This program reopens those systems from a fresh source
inventory rather than treating the older labels as constraints.

## Non-goals

- Float bit-exactness when the difference cannot alter discrete gameplay.
- Camera-only behavior already excluded by the RL 1.0 validation profile.
- Broad RNG reconstruction. Bounded source-owned RNG sites remain separate policy work.
- Unsupported characters, stages, and item kinds unless a shared source owner naturally covers
  them at negligible cost.
- Keeping compatibility APIs whose only remaining consumer is the implementation being deleted.

## Updating these documents

The baseline documents should change only when the committed aggregate reports or architecture
materially change. Candidate documents are living source inventories. Significant implementation
decisions should be recorded under `decisions/` before their original context disappears.
