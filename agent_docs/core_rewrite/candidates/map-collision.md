# Candidate: Source-Shaped Map Collision

Verdict: completed as Phase 1 packets 1–5.

Outcome: the predicted 12–18k runtime deletion landed at approximately 17,979 net `src/` lines.
Aggregate one-step and rollout improve by 15.3% and 19.7%, and both performance gates pass. The
design/inventory below is retained as the pre-implementation rationale; final coverage is in the
packet docs and residual validation ledger.

## Boundary

The owner is the complete map-collision chain, not an isolated floor or wall function:

```text
stage line data
  -> mpLib line queries and transforms
  -> persistent fighter CollData
  -> mpColl ordered resolution
  -> live installed Coll callback
  -> immediate landing/fall/wall/tech/ledge transition
```

Stage hazards and schedulers remain separate. Cliff option Anim/IASA behavior can follow, but
CliffCatch admission and entry ordering belong to this owner.

## Why this is first

It has the strongest combination of five properties:

1. **Complete core fighter-collision source.** `mplib.c`, `mpcoll.c`, `ft_081B.c`, and
   common/character Coll callbacks provide a bounded flow. Dynamic-stage publication fields and
   replay CollData initialization still require an explicit audit.
2. **Strong validation concentration signal.** Newer-character replays own almost all remaining
   `on_ground` and `ground_id` mismatches. This is not row-level causal attribution.
3. **Large deletion opportunity.** The current surface is roughly three times the relevant source
   surface.
4. **Performance leverage.** Prior local profiles identify stage collision as a major hot path. A
   source-shaped rewrite plausibly removes duplicate work, but performance improvement remains a
   hypothesis to verify on both benchmark gates.
5. **Architectural leverage.** It provides the first real use of exact MotionState callback dispatch
   and central collision-owned motion entry without requiring a scheduler-only migration.

## Current scale

- `src/mpcoll_*`: about 22.4k LOC.
- `src/stage_collision.c`: 3,917 LOC.
- `src/ledge.c`: 1,382 LOC.
- Additional root/provenance staging lives in `fighter_callbacks.c` and `state.h`.
- Total practical surface: about 28k LOC.

Relevant source is about 9-10k LOC, including behavior outside the simulator's domain:

- `refs/melee/src/melee/mp/mpcoll.c`: 4,547 LOC;
- required `mplib.c` queries/traversal: roughly 3-3.5k LOC;
- `refs/melee/src/melee/ft/ft_081B.c`: 1,382 LOC total;
- common cliff admission/action slices: roughly 600 LOC.

A plausible final implementation is 8-12k LOC, implying 12-18k net deletion. This is an estimate,
not a completion gate.

## Pre-rewrite evidence of split ownership (historical)

- Ground collision runs globally, followed by a separate wall/ceiling pass.
- Ledge mask publication and CliffCatch consumption run later as separate global passes.
- Landing, knockdown, locomotion, shine, Sheik, and other transitions are deferred to different
  post-collision modules.
- Floor sweep roots, wall roots, stage roots, callback-result packets, desired ECB packets,
  publication modes, provenance flags, and restore packets model pieces of one source `CollData`.
- Packet 5 deleted the former 41-name `MSL_MPCOLL_REJECT_*` diagnostic taxonomy; the remaining
  admission predicates still need to become direct CollData consequences
  under a shared CollData phase model.
- The collision surface contains more than 100 `suppress_*` predicates and hundreds of
  seed/reseed/history references.
- Gameplay contains explicit checks of replay rollout state, including
  `mpcoll_replay_rollout_advanced_past_reseed`. These are forbidden runtime bridges.
- The exact extracted Coll callback identity is never installed into a live runtime lane. Three
  manually generated semantic class words are instead converted into an approximate eight-bit
  source-phase mask.

## Source shape

Vanilla owns one persistent `CollData` with:

- `cur_pos`, `prev_pos`, and `last_pos`;
- current, previous, desired, and auxiliary ECBs;
- environment and previous-environment flags;
- independent floor, left-wall, right-wall, and ceiling surfaces;
- floor skip, joint filters, facing, and ledge data;
- contact normals, points, and line identity.

The Coll callback copies the fighter root into this object, loads the appropriate ECB, and invokes
one thin wrapper. Air resolution orders walls, ceiling, floor, squeeze, and ledge inside bounded
substeps. Ground resolution has its own ordered retry and floor-loss paths. The callback applies
its transition immediately.

## Target design

### 1. Source-oriented stage map

Load the static portion of `MSLSTG01` into one immutable line representation containing:

- stable line/segment identity and source kind;
- raw endpoints and orientation;
- raw previous/next alternatives;
- line flags and joint ID;
- fighter-active/material/support policy;
- transform/path metadata.

Enabled/hidden state, transformed endpoints, and moving-surface velocity must remain per batch
lane. Stage/Ground updates publish that dynamic state before fighter map callbacks, following the
`Ground_801C2ED0`/`Ground_801C2FE0` ordering; there must be no globally mutable world-line view.

Port only the required `mpLib` operations: directional queries, projections, remaps, line flags,
normals, link traversal, ledge scans, floor skip/joint filtering, connectivity, and moving-surface
velocity.

This should replace four normalized graph families, duplicate raw endpoints, multiple intersection
implementations, stage-wide shape proxies, and seven 65,536-entry lookup maps per loaded stage
(five `int16_t` maps and two byte maps).

Before implementation, audit the artifact for dynamic joint flags, remap basis coordinates, line
enable/hide state, and Ground callback identity. Extend the full extraction contract if any are
missing.

### 2. Persistent CollData in SoA

Represent source fields directly in the batch. A lane-local stack view may gather the subset needed
for one fighter/callback and scatter it back, provided it remains allocation-free and does not copy
large unused state on the hot path.

Replay preprocessing may initialize an explicit CollData seed packet. The live runtime should not
know whether that packet came from a replay.

### 3. Direct mpColl translation

Closely port:

- ECB loading and normalization;
- `Fighter_procMap` ECB-lock decrement and `ftCommon_UnlockECB` before callback dispatch;
- interpolation and bounded substeps;
- directional collision primitives;
- ordered airborne and grounded resolution;
- squeeze, retry, floor skip, and moving-surface carry;
- wrapper flag variants;
- ledge query in its source substep;
- final CollData publication.

Source uses small fixed arrays for candidate walls; fixed-capacity lane-local scratch satisfies the
allocation rule.

### 4. Exact Coll callback dispatch

Motion entry translates the selected row's extracted Coll symbol into a stable live handler kind.
Dispatch that live lane, and model explicit source callback overrides as live-lane writes. Shared
callbacks should call shared ported helpers; character specials should select the same low-level
wrappers through their source handlers. Hardcoded raw numeric callback IDs are not acceptable.

Collision-owned action changes must call the central motion-state entry path with the procedural
flags from the source callback. They must not return a fact for a later global module to interpret.

## Callback ledger and cutovers

The final status is maintained in [../callback-ledger.md](../callback-ledger.md). All five packets
are complete.

The six supported artifacts contain 302 distinct non-null Coll callback identities. Every identity
is classified as migrated or intentionally outside the RL map-collision scope; no legacy dispatch
remains.

Reviewable packets should cut over exact shared callback families across all supported characters,
each deleting its old path:

1. source map/mpLib/CollData/mpColl plus common grounded callbacks;
2. common airborne, AttackAir/EscapeAir/Landing, and floor-loss callbacks;
3. Damage/DamageFly/DamageFall and common passive/tech callbacks;
4. CliffCatch admission, simultaneous-ledge ownership, and supported special/capture callbacks;
5. remaining supported-domain callback identities and final legacy-path deletion.

The first packet is therefore not bare substrate: common grounded callbacks use the new owner and
their old phase/reject/post-collision behavior is deleted. Internal development may use a debug-only
old/new comparator.

Every migrated transition destination also needs source-correct AObj/script initialization and
procedural `SkipHit`/`SkipAnim`/`UpdateCmd` effects. Phase 1 may reuse a shared causal script runtime,
but must not introduce a collision-specific partial motion-entry helper.

## Validation and performance expectations

The working hypothesis is a material reduction in `on_ground` and `ground_id` mismatches plus fewer
action/animation rollout breaks caused by collision transitions. The field concentration supports
that hypothesis but does not prove ownership without callback-level classification.

Performance may improve because the source flow eliminates global duplicate passes, giant stage
maps, repeated graph/provenance classification, and late repair logic. No meaningful regression is
the minimum gate; gains must be demonstrated in both random-input `bench-sim` and fixed
replay-derived timing before being claimed.

## Principal risks

- Slippi does not expose full `CollData`; a small history-derived initialization packet is likely
  required.
- Moving-platform remap/joint semantics must be proven from source data before deleting the old
  representation.
- The current split reaches locomotion, knockdown, ledge, capture, and special modules. Replacing
  geometry without immediate callback transitions would preserve the worst architectural fault.
- A large gather/scatter struct can erase performance gains.
- Fox/Falco raw-clean controls make stable ordering and floor identity non-negotiable.
- Doubles locks must cover simultaneous CliffCatch candidates, ledge occupancy, and stable fighter
  order.
