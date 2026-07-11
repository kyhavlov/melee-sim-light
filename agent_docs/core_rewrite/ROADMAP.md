# Core Rewrite Roadmap

This roadmap is intentionally owner-oriented. Internal scaffolding may be built in smaller steps,
but reviewable packets must replace behavior and delete displaced code.

## Phase 0: Reconnaissance

Status: complete.

- Recorded aggregate validation and architecture baselines.
- Audited MotionState/callback/script ownership.
- Audited map collision and stage representation.
- Audited fighter contact and grab/throw dependencies.
- Selected map collision as the proposed first target.

No runtime behavior changed in this phase.

## Phase 1: Map-collision vertical rewrite

Implementation is split into five behavior-bearing packets. Packet 1 is complete; the remaining
four are recorded in [callback-ledger.md](callback-ledger.md). A packet is a review boundary, not a
claim that the whole Phase 1 owner has been retired.

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

Do not start Phase 2 until every supported-domain Coll identity in the ledger is migrated or
explicitly proven out of RL scope, the legacy collision path is deleted, and Phase 1 is clean and
reviewable.

### Packet status

1. **Common grounded wrappers — complete.** `B108`, `B2DC`, `B4B0`, Run, Guard,
   GuardSetOff, and Ottotto identities now use the live table-selected source path for all six
   characters. See [packets/01-common-grounded.md](packets/01-common-grounded.md).
2. **Common air/landing — next.** Common airborne, AttackAir, EscapeAir, Landing, and complete
   ground-to-air/floor-loss handoff.
3. **Damage/passive.** Damage, DamageFly, DamageFall, passive/tech, and knockdown collision owners.
4. **Cliff/special/capture.** CliffCatch and simultaneous ledge ownership plus supported special and
   capture collision identities.
5. **Remainder/deletion.** Migrate or prove out of scope every remaining supported-domain identity,
   then delete legacy collision dispatch and dead state.

## Phase 2: Stateful fighter-contact engine

- Complete causal `MSLFTSC1` command execution and persistent HitCapsule/command state for every
  supported contact-producing motion, even when its Anim/IASA/Phys callbacks migrate in Phase 3.
- Refresh ordinary HitCapsule endpoints at priority 9 and model lazy hurt/shield/reflect geometry,
  or prove an eager cache phase-equivalent.
- Port bounded `lbColl` tests and exact victim-list semantics.
- Keep priority-12 catch selection and callbacks separate from damage aftermath.
- Port stable priority-13 traversal with per-current-fighter `dmg_log0[20]`/`dmg_log1[20]` scratch,
  then collapse results into persistent source-like pending fields.
- Port the priority-14 ProcessHit precedence ladder and stale/identity writeback.
- Route ordinary fighter, item, ordinary throw, scripted throw, thrown-body, and special damage
  through the corresponding source pending-field order. Temporary adapters must be deleted.
- Delete snapshot hitbox replay, semantic window helpers, pairwise clank approximation, geometry
  bridges, and distributed aftermath.
- Keep DamageFlyRoll RNG stream policy outside the deterministic contact cutover.

## Phase 3: Remaining MotionState execution families

With Coll and contact phases source-shaped, expand exact Anim/IASA/Phys dispatch through vertical
families:

1. common locomotion and defense;
2. grounded and aerial attacks;
3. damage/knockdown/passive lifecycle not already migrated;
4. character specials;
5. match-flow actions in the supported gameplay domain.

For each family, consume the causal script state already established for contact-producing motions,
extend the interpreter only for remaining non-contact events/actions, centralize all entry sites,
and delete old subsystem passes and fresh-entry bridges. Do not retain broad action-id switches as
a fallback for migrated callbacks.

## Phase 4: Capture, throw, items, and state reduction

- Rewrite remaining capture/throw attachment and release ownership on top of the contact engine.
- Converge item/article collision and ProcessHit producers on the same source traversal,
  pending-field, and aftermath order.
- Remove state lanes, seed derivations, binding fields, and tests that became dead during earlier
  cutovers.
- Reassess whether any retained semantic class or generated artifact still has a runtime consumer.

## Packet lock protocol

Every behavior packet follows the same sequence:

1. Write the source call graph and explicit in/out state before coding.
2. Identify existing code and state to delete on cutover.
3. Implement one complete owner slice with allocation-free fixed-capacity state.
4. Add synthetic positive/negative source-owner tests, including doubles where order matters.
5. Delete the displaced implementation before broad validation.
6. Run focused tests and `make fmt-check`.
7. Refresh and diff all required validation reports.
8. Run both performance gates when the hot path changed.
9. Update the candidate/decision docs and durable SPEC.
10. Stop at a clean review point before selecting the next slice.

## Definition of a successful rewrite

A rewrite is complete only when:

- the selected source owner is represented directly;
- all supported characters/stages using that owner share the same general implementation;
- replay initialization produces real hidden state rather than alternate runtime semantics;
- old owner code and compatibility APIs are deleted;
- validation has no hard red and shows broad owner-level improvement;
- random and replay workloads remain fast;
- no heap allocation occurs after initialization;
- no dataset, replay, record, or evaluator-state predicate exists in gameplay.

Large LOC deletion is expected, especially in collision and contact, but correctness and source
shape are the deciding evidence.
