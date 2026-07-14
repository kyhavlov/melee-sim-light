# Core Rewrite Roadmap

This roadmap is intentionally source-owner-oriented. The branch objective is gameplay-relevant
decomp completion for the supported domain. Internal scaffolding may be built in smaller steps, but
reviewable packets must port real behavior and delete displaced code. Validation rows never define
the work queue; see [Decision 0002](decisions/0002-source-completion-first.md).

## Phase 0: Reconnaissance

Status: complete.

- Recorded aggregate validation and architecture baselines.
- Audited MotionState/callback/script ownership.
- Audited map collision and stage representation.
- Audited fighter contact and grab/throw dependencies.
- Selected map collision as the proposed first target.

No runtime behavior changed in this phase.

## Phase 1: Map-collision vertical rewrite

Status: complete. All five behavior-bearing packets use the source-shaped
map/mpLib/CollData/mpColl runtime, and the old floor/ground/wall coordinator is deleted.

### 1A. Source and artifact contract

- Map every required `mpLib`, `mpColl`, `ft_081B`, and Coll callback function.
- Audit MSLSTG01 fields against source line/joint/remap inputs.
- Inventory all supported-domain Coll callback symbols and generate stable handler kinds; raw
  alphabetically assigned callback IDs must not become runtime constants.
- Define per-fighter live callback lanes, including explicit source override sites.
- Audit the MSLFTSC1 payloads required by migrated transition destinations, including currently
  omitted `remove_hitbox`/`set_hitbox_size` events.
- Extend extractor, stable schema, required-key loader, packaged data, and fresh-extract smoke path
  together if fields are missing.
- Define the persistent CollData SoA and replay initialization packet.

This is an internal milestone, not a standalone completion packet.

### 1B. Stage map and mpLib

- Implement one immutable source-oriented line table plus per-batch dynamic line state.
- Publish enabled/transformed/moving line state from stage updates before fighter map callbacks,
  preserving `Ground_801C2ED0`/`Ground_801C2FE0` order and avoiding global mutable line views.
- Port directional queries, projections, remap, links, filters, connectivity, ledges, and velocity.
- Add source-owner unit tests on all six stages, including transformed and moving surfaces.
- Use a debug-only comparator against known source probes where useful.

### 1C. Persistent CollData and mpColl

- Implement ECB load, substeps, ordered air/ground resolution, squeeze/retry, floor skip, and
  surface publication.
- Port `Fighter_procMap` ECB-lock decrement/`ftCommon_UnlockECB` before live Coll dispatch.
- Preserve independent left/right wall state and exact environment flags.
- Keep scratch fixed-capacity and lane-local.

### 1D. Callback and transition cutover

- Initialize stable live Coll handler kinds from MotionState rows and dispatch those live lanes,
  including dynamic source overrides.
- Keep exact geometry in the generated v28 wrapper-selector lane. Resolve procedural selectors
  from live callback state and keep `coll_source_plan` limited to post-mpColl continuations.
- Add central motion-state entry with explicit procedural flags for collision-owned transitions.
- Implement the causal AObj/script/HitCapsule entry semantics required by every migrated
  destination, including `SkipHit`, `SkipAnim`, and `UpdateCmd`; do not create a collision-only
  partial entry helper.
- Apply landing, floor loss, wall/ceiling, passive, and ledge changes immediately.
- Prevent migrated callbacks from entering old post-collision passes.

### 1E. Delete and lock

- Delete old phase masks, reject matrices, root packets, post-collision transitions, and evaluator
  bridges for migrated callbacks.
- Cut over exact shared callback families across all supported characters in independently locked
  packets. Maintain a ledger for unmigrated callback identities, and never run old and new behavior
  for the same live identity.
- Remove now-unused MSLMSO semantic classes and tests.
- Run focused tests, formatting, all four validation reports, and report diffs.
- Run `bench-sim` and the fixed replay timing gate.
- Update `agent_docs/SPEC.md` with the durable mechanics; keep investigation detail here.

Phase 2 may start after review. Every supported-domain Coll identity is migrated or explicitly
classified outside the RL map-collision architecture, and the legacy path is deleted. Phase 1 is
an architectural completion boundary, not a claim that all connected gameplay semantics are done.

### Continuous completion and validation cadence

- Build complete source-owner slices before treating aggregate validation as an approval gate.
- Use focused mechanics tests, source probes, and debug-only CollData comparison while an owner is
  incomplete. Temporary aggregate reds are diagnostic evidence, not an automatic reason to revert
  a source-backed partial cutover.
- Keep validation evidence keyed by first divergent field, callback owner, and phase. Use it to
  challenge source-completion claims and prioritize source inventories; do not turn replay names
  into a repair backlog.
- Run short representative replay samples at coherent cutovers. Refresh all four reports and run
  both performance gates for the final Phase 1 result.
- Stop early only for a genuine external blocker. A difficult intermediate validation state is a
  reason to continue or pivot the implementation, not to shrink the requested scope.

### Packet status

1. **Common grounded wrappers — complete.** `B108`, `B2DC`, `B4B0`, Run, Guard,
   GuardSetOff, and Ottotto identities now use the live table-selected source path for all six
   characters. See [packets/01-common-grounded.md](packets/01-common-grounded.md).
2. **Common air/landing — complete.** Exact handlers and recipes run on the shared source-shaped
   air kernel with immediate landing/floor-loss publication.
3. **Damage/passive — complete.** Damage, Down, and passive transition ladders consume persistent
   CollData and the shared map kernel.
4. **Cliff/special/capture — complete.** Cliff, supported special, capture, and throw-release map
   callbacks use exact live recipes and source-shaped geometry/ordering.
5. **Remainder/deletion — complete.** All identities are classified, the seven legacy coordinator
   files and reject taxonomy are deleted, and the extension build has one collision runtime.

## Phase 2: Causal fighter execution and contact engine

Status: restarting from committed Phase 1. The abandoned broad cutover is preserved only as a
machine-local reference; no implementation is assumed correct or transplanted wholesale.

MotionState execution and fighter contact are connected, but they must not become one unauditable
all-or-nothing diff again. Phase 2 advances through vertical cuts that each own a real source path:

1. common defense, ShieldDesc/ReflectDesc contact, and Guard aftermath;
2. ordinary attack HitCapsules, BODY contact, DmgLog selection, and damage ProcessHit;
3. Catch, capture, scripted throw, and thrown-body ownership;
4. supported item/article, reflect/absorb, and character-special contact producers;
5. remaining callback/action families and deletion of displaced snapshot/history infrastructure.

The order may expand when a source dependency is real. A vertical is not complete until its old
owner is deleted, but it is not discarded merely because aggregate validation worsens while a
known connected source owner remains unported. Record every behavior-bearing checkpoint and
canonical report delta in [phase2/README.md](phase2/README.md).

Shared substrate—motion entry, callback lanes, script cursors, persistent capsules, DmgLog scratch,
and pending ProcessHit fields—must be introduced by the first vertical that uses it and kept no
broader than that vertical initially. Later cuts generalize the proven owner. Do not return with a
scheduler-only, cache-only, or behavior-neutral checkpoint.

DamageFlyRoll RNG stream policy remains outside the deterministic contact rewrite unless a selected
vertical reaches its source-owned RNG boundary directly.

## Phase 3: Remaining MotionState execution families

With Coll and contact phases source-shaped, continue exact Anim/IASA/Phys/Coll dispatch through
source families. The order may change when source dependencies make a different vertical slice
cleaner; these are coverage groups, not fixed work turns:

1. common locomotion and defense;
2. grounded and aerial attacks;
3. damage/knockdown/passive lifecycle not already migrated;
4. character specials;
5. match-flow actions in the supported gameplay domain.

For each family, inventory all gameplay-relevant callbacks for all supported characters, consume
the causal script state already established, centralize entry sites, and delete old subsystem
passes and fresh-entry bridges. Do not stop when the currently observed replay rows turn green, and
do not retain broad action-id switches as fallback for migrated callbacks.

## Phase 4: Capture, throw, items/articles, and state reduction

- Rewrite remaining capture/throw attachment and release ownership on top of the contact engine.
- Converge item/article collision and ProcessHit producers on the same source traversal,
  pending-field, and aftermath order.
- Remove state lanes, seed derivations, binding fields, and tests that became dead during earlier
  cutovers.
- Reassess whether any retained semantic class or generated artifact still has a runtime consumer.
- Audit the supported-domain source inventory for gameplay owners not naturally covered by Phases
  1–3; close those owners before calling the program complete.

## Packet coverage protocol

The coverage ledger follows this sequence, without requiring a user-facing stop between entries:

1. Write the complete gameplay-relevant source call graph, data inputs, persistent state, and
   supported-domain exclusions before coding.
2. Identify existing code and state to delete on cutover.
3. Implement one complete owner slice with allocation-free fixed-capacity state.
4. Add synthetic positive/negative source-owner tests, including doubles where order matters.
5. Delete the displaced implementation before broad validation.
6. Run focused tests and `make fmt-check`; use representative validation diagnostically.
7. Refresh and diff all required validation reports at coherent cutovers. Investigate evidence
   that the claimed owner is wrong; assign other rows to their unported source owner without
   patching them locally.
8. Run both performance gates for the completed hot-path cutover.
9. Update the candidate/decision docs and durable SPEC.
10. Continue into the next connected owner while the source invariant is in flow; return for review
    at a coherent architectural cutover or a genuine blocker, not at an arbitrary packet number.

## Definition of a successful rewrite

A source-owner rewrite is complete only when:

- every gameplay-relevant path in the declared source boundary is represented directly and
  exclusions are explicit;
- all supported characters/stages using that owner share the same general implementation;
- replay initialization produces real hidden state rather than alternate runtime semantics;
- old owner code and compatibility APIs are deleted;
- validation reports are regenerated and no result disproves the claimed source coverage; metric
  improvement is supporting evidence, not the completion definition;
- random and replay workloads remain fast;
- no heap allocation occurs after initialization;
- no dataset, replay, record, or evaluator-state predicate exists in gameplay.

The branch program is complete only when this standard covers all gameplay-relevant owners in the
supported domain and the old semantic approximations are gone. Large LOC deletion is expected,
especially in collision and contact, but close decomp coverage and direct ownership are the
deciding evidence.
