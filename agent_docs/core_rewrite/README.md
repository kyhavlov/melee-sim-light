# Core Rewrite Program

Status: Phase 1 architectural cutover complete. Packets 1–5 use one source-shaped map-collision
runtime; semantic source completion continues across the connected gameplay owners.

Baseline revision: `a293980533bd` (`Improve and benchmark simulator performance`), 2026-07-11.

## Purpose

The number-one goal of this branch is to port all gameplay-relevant Melee code and data for the
supported characters, stages, and singles/doubles runtime into direct, source-shaped owners. This
program replaces piecemeal, replay-shaped implementations with close rewrites of the decomp. It is
not a cosmetic refactor or a validation burndown project. A successful rewrite must:

- represent the relevant source call graph, persistent state, callback order, and data directly;
- delete compensating branches, duplicated state, and semantic query APIs;
- preserve the allocation-free batched runtime and deterministic ordering;
- preserve or improve both random-input and replay-derived performance;
- leave one authoritative runtime path for each migrated owner;
- use validation as evidence of coverage and correctness, never as the definition of patch scope.

The motivating observation is that the simulator now contains about 140k lines of C under `src/`,
yet most aggregate replays still have low-double-digit or low-hundreds discrete mismatches. Several
core systems are larger than the corresponding source surfaces because the live source state and
callback order were reconstructed a case at a time.

## Phase 1 architectural result

Packets 2–5 run through the consolidated `mp_lib`/`mp_coll`/persistent-CollData kernel. The seven
displaced floor, ground, and wall/ceiling source/header files are deleted, and the extension build
contains only the new path. Relative to the Packet 1 baseline, runtime `src/` is approximately
17.1k net lines smaller after including the six new source/header files; the complete dirty tree is
approximately 26.2k net lines smaller after accounting for new untracked files.

Correctness improves in aggregate one-step and rollout and in the doubles/Falcon/Sheik suites:

- aggregate one-step: 9,918 -> 8,689 scored mismatches (-1,229, -12.4%);
- aggregate rollout: 1,667 -> 1,396 first mismatches (-271, -16.3%);
- doubles one-step: 8,332 -> 7,713 (-619);
- doubles rollout: 1,252 -> 1,193 (-59);
- Falcon one-step/rollout: 3,143 -> 2,537 and 596 -> 526;
- Sheik one-step/rollout: 3,069 -> 2,711 and 462 -> 441.

The final recorded ThinLTO run measures 424.9k mixed-random FPS with p99/average 1.30x. The fixed
five-replay sample measures 311.7k FPS with p99/average 4.17x. Random throughput clears its 400k
gate; replay throughput is 2.6% below the 320k gate and 3.8% below the preceding committed report.
Neither workload shows a pathological tail. See
[residuals/phase1-validation.md](residuals/phase1-validation.md) for intentionally deferred
replay-level regressions.

The completed first vertical cutover is the fighter map-collision architecture:

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

This completion label means the old coordinator is gone and one source-shaped runtime owns map
collision. It does not claim that all map-collision semantics or the MotionState/contact owners
feeding and consuming CollData are already source-complete.

The next program is the causal MotionState/contact execution chain: exact motion entry and
Anim/IASA/Phys/Coll ownership, script/HitCapsule state, contact traversal, and ProcessHit. These are
one connected source surface and should be split only at boundaries that can delete displaced code.

The decisions are recorded in
[0001-first-target-map-collision.md](decisions/0001-first-target-map-collision.md) and
[0002-source-completion-first.md](decisions/0002-source-completion-first.md).

## Governing invariants

1. **Complete the source surface.** Decomp call graphs, state, tables, and scheduler order define
   both scope and behavior. The branch goal is gameplay-relevant source completion for the
   supported domain, not incremental parity with the current replay set.
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
6. **Validation is evidence, not the queue.** Broad regressions or mismatches inside a claimed
   source-complete owner demand investigation. Other replay-level reds identify connected owners
   still to port; they do not become bespoke repair tasks. Never preserve old machinery or add a
   replay-shaped compensation to satisfy a metric.
7. **Deletion is an outcome, not a quota.** LOC estimates rank opportunities. Source-complete code
   should not be distorted to hit a numeric deletion target.
8. **Packets track source coverage, not work turns.** Packets may remain useful ledgers and
   internal milestones, but they do not require separate review stops or a temporarily green
   aggregate suite while a shared owner is only partially migrated. Expand across files and systems
   whenever the source invariant requires it.

## Document map

- [baseline/validation.md](baseline/validation.md): aggregate correctness baseline and how to read it.
- [baseline/architecture.md](baseline/architecture.md): code/data inventory and recurring structural faults.
- [candidates/motion-state-kernel.md](candidates/motion-state-kernel.md): the required execution kernel.
- [candidates/map-collision.md](candidates/map-collision.md): recommended first vertical rewrite.
- [candidates/contact-engine.md](candidates/contact-engine.md): recommended second vertical rewrite.
- [decisions/0002-source-completion-first.md](decisions/0002-source-completion-first.md): branch-wide source-completion policy and terminology.
- [ROADMAP.md](ROADMAP.md): implementation order, cutover protocol, and completion gates.
- [callback-ledger.md](callback-ledger.md): the five map-collision packets and current cutover state.
- [packets/01-common-grounded.md](packets/01-common-grounded.md): Packet 1 implementation and gates.
- [packets/02-common-air-landing.md](packets/02-common-air-landing.md): common-air callback cutover.
- [packets/03-damage-down-passive.md](packets/03-damage-down-passive.md): damage/passive cutover.
- [packets/04-cliff-special-capture.md](packets/04-cliff-special-capture.md): exact cliff/special/capture recipe cutover.
- [packets/05-identity-ledger-and-deletion.md](packets/05-identity-ledger-and-deletion.md): complete identity classification and deletion ledger.
- [residuals/phase1-validation.md](residuals/phase1-validation.md): validation evidence and owner hypotheses; not a repair queue.

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
