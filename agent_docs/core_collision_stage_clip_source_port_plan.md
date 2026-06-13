# Core Collision Stage-Clip Source-Port Plan

## Purpose

Define the source-port scope needed to make catastrophic stage clips from core fighter mechanics structurally impossible for supported Fox/Falco legal-stage gameplay, except for explicitly deferred character-special cases. The implementation target is not a replay-specific patch. It is a source-backed collision ownership pass that closes the common fighter and `mpLib`/`mpColl` families used by grounded, airborne, aerial attack, air dodge, damage, knockback, wall, ceiling, static-stage, and moving-stage-platform gameplay on legal stages.

This document is an implementation contract for a future coding pass. It must be validated against decomp/source, extracted stage data, synthetic coverage, and aggregate validation, not against any single known row or trace.

The invariant this pass is trying to enforce is:

- If a source common collision callback would sweep a live `CollData` ECB/root interval into legal-stage floor/wall/ceiling geometry, MSL must publish the same contact, rejection, or transition through the same owner family.
- If source would not publish contact because provenance is stale, skipped, outside the segment, rejected by floor/joint policy, or owned by a different callback phase, MSL must not publish a compensating contact.
- The decision must come from live source state, extracted stage data, and ordered `mpLib`/`mpColl` queries, not from trace coordinates, stage names, or post-hoc clamps.

## Non-Goals

- Do not fit behavior to a known replay, dataset name, trace artifact, frame, or individual collision segment.
- Do not treat historical docs under `agent_docs/systems/` as authoritative. They may be used only as context to find code, tests, or questions that must be re-proven from source.
- Do not rewrite the whole simulator scheduler, combat model, or item system unless a touched common collision wrapper requires it.
- Do not add gameplay constants in `src/` without nearby source/decomp or extracted-data backing.
- Do not add runtime Python loops, dynamic maps, or allocation-prone helpers to gameplay or preprocessing hot paths.

## Scope

### Highest Priority: Static Legal-Stage Core Collision

The first implementation phases cover static stage collision for Final Destination, Battlefield, Fountain of Dreams static geometry, frozen Pokemon Stadium, Yoshi's Story static geometry, and Dream Land N64 static geometry. Supported fighters are Fox and Falco, with common mechanics favored over character-specific branches.

In-scope owner families:

- Common grounded movement: Wait, Walk, Dash, Run, Turn, Squat, Landing, grounded attack movement where it shares common grounded wrappers.
- Common airborne movement: Fall, FallAerial, FallSpecial, Jump, JumpAerial, ledge-release-to-air common wrappers where they feed the same airborne collision path.
- `AttackAir` and `EscapeAir`: common aerial attack and air dodge physics/collision ownership, including landing/floor contact handoff and wrapper selection.
- Damage, knockback, hitlag SDI, ASDI, hitstun, DamageFly/DamageFall, passive wall/ceiling/floor interactions, and common damage landing/bounce paths.
- Wall/ceiling ordered collision: shared left-wall, right-wall, ceiling, floor retry, squeeze/projection, and ordered substep behavior.
- `mpLib` query substrate: floor, ceiling, left wall, right wall, multi-check, remap, segment adjacency, endpoint, normal, flags, speed, and moving-surface query entry points used by common fighter collision.
- `CollData` lifetime/state: ECB source, previous/current ECB, environment flags, floor/wall/ceiling ownership, collision callbacks, skip state, and begin/end mutation timing.
- Floor skip and joint skip: source-backed skip clear/update, one-way platform/pass-through gates, joint-only/joint-skip filters, and action transitions that preserve or clear them.
- Legal static stage line metadata: extracted stage segment kind, flags, normals, adjacency, ledge/passability/material/platform class, and per-stage stable ordering required by `mpLib`.

### Lowest Priority Final Phase: Moving Stage Geometry

Moving platforms are in scope only after the static legal-stage core is source-covered and validated. This phase covers platform transforms and dynamic line updates for supported legal-stage gameplay, especially Randall and Fountain of Dreams platform motion.

The moving-geometry phase must cover:

- Dynamic joint transform application for collision geometry.
- `mpLib` speed query and surface-relative correction where common fighter wrappers consume moving-surface velocity.
- Platform visibility/activation, joint hide/unhide, bounding updates, and dynamic line position refresh.
- Randall-specific and Fountain-of-Dreams-platform behavior only through stage source/data owners, not ad hoc fighter branches.

## Out of Scope Until A Separate Pass

- Item-only collision and item-only `mpColl` wrappers, unless an item path shares a common fighter collision wrapper being ported.
- Camera boxes, blast zones, KO boundaries, and camera-driven correction.
- Unsupported characters and unsupported stages.
- Character-special bespoke callbacks that do not share common collision wrappers.
- Projectiles, article collision, and stage hazards except where their extracted stage geometry metadata is required by common fighter `mpLib` queries.

## Source Owners To Audit And Port

Use source files in `refs/melee/src/...` first. Use matching `refs/melee/build/GALE01/asm/...` only when the C decomp is incomplete, ambiguous, or missing a relevant inline.

### Common Fighter Scheduler And State Handoff

Audit/port:

- `refs/melee/src/melee/ft/fighter.c`
- `refs/melee/src/melee/ft/fighter.h`
- `refs/melee/src/melee/ft/ftcommon.c`
- `refs/melee/src/melee/ft/ftcommon.h`
- `refs/melee/src/melee/ft/ft_081B.c`
- `refs/melee/src/melee/ft/ft_0C88.c`
- `refs/melee/src/melee/ft/ft_0CDD.c`

Required function families:

- Fighter frame phases: `Fighter_procUpdate`, `Fighter_procMap`, `Fighter_8006A360`, `Fighter_8006A1BC`, `Fighter_8006C5F4`, `Fighter_8006CB94`.
- Motion transitions: `Fighter_ChangeMotionState`, `ftCommon_8007D5D4`, `ftCommon_8007D6A4`, `ftCommon_8007D7FC`, `ftCommon_8007D92C`.
- ECB and state handoff helpers around `ftCommon_UnlockECB`, air-to-ground and ground-to-air transitions, collision callback timing, and same-frame action entry semantics.

Acceptance for this owner: the simulator has one explicit source-backed place for callback phase ordering and CollData publication. Runtime bridges that compensate for missing callback ownership are either removed or documented as non-runtime forensic tooling.

### Grounded Movement Owners

Audit/port:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c`

Required function families:

- Ground friction/accel/clamp: `ftCommon_ApplyFrictionGround`, `ftCommon_ApplyGroundMovement`, `ftCommon_ApplyGroundMovementNoSlide`, `ftCommon_ClampGrVel`, and nearby `ftCommon_8007C*` helpers.
- Grounded collision wrappers used by the actions above, including floor retention, walk-off, edge behavior, one-way platform pass-through, and wall/ceiling post-floor retry.

Acceptance for this owner: common grounded actions use source-backed ground collision wrappers and do not branch on character id as a proxy for missing collision state.

### Airborne Movement, AttackAir, And EscapeAir

Audit/port:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c`

Required function families:

- Air friction/drift/fall: `ftCommon_ApplyFrictionAir`, `ftCommon_ClampAirDrift`, `ftCommon_Fall`, `ftCommon_FallBasic`, `ftCommon_FallFast`, `ftCommon_ClampFallSpeed`, `ftCommon_CheckFallFast`.
- Aerial collision wrappers for floor landing, wall/ceiling collision, ledge exclusion/inclusion, and special-fall handoff.
- `AttackAir` and `EscapeAir` landing handoff, ECB lock/unlock behavior, and collision callback selection.

Acceptance for this owner: aerial actions cannot bypass the ordered common collision pass by action-family shortcuts. Landing and wall/ceiling outcomes must be produced by the same `CollData`/`mpColl` owner family that source uses.

### Damage, Knockback, SDI, And Passive Collision Owners

Audit/port:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveCeil.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopWall.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopCeil.c`
- `refs/ucf/src/sdi/sdi.cpp`
- `refs/ucf/src/shield_sdi/shield_sdi.cpp`
- `refs/ucf/src/tumble/tumble.cpp`

Required function families:

- Damage entry/exit, hitlag exit, hitstun movement, knockback decay, floorhug/floor sweep, bounce, passive wall/ceiling/floor, and tumble/meteor/passive gates.
- UCF SDI/ASDI behavior that changes post-hitlag displacement before collision resolution.

Acceptance for this owner: damage-family movement and SDI displacement feed the same ordered collision substrate as ordinary airborne movement. Any special collision suppression during hitlag or damage entry must be source-backed and localized to the damage owner.

### `mpColl`, Wall/Ceiling Ordering, And `CollData`

Audit/port:

- `refs/melee/src/melee/mp/mpcoll.c`
- `refs/melee/src/melee/mp/mpcoll.h`
- `refs/melee/src/melee/mp/types.h`
- `refs/melee/src/melee/lb/types.h` for `CollData` and `SurfaceData` layout.

Required function families:

- CollData lifecycle: `mpCollPrev`, `mpCollCheckBounding`, `mpColl_SetECBSource_JObj`, `mpColl_SetECBSource_Fixed`, `mpColl_LoadECB_JObj`, `mpColl_LoadECB_Fixed`, `mpColl_LoadECB`, `mpCollInterpolateECB`, `mpCollEnd`, `mpCopyCollData`.
- Floor wrappers: `mpColl_80044628_Floor`, `mpColl_80044838_Floor`, `mpColl_80044948_Floor`, `mpColl_800477E0`, `mpColl_8004A45C_Floor`, `mpColl_8004A678_Floor`, `mpColl_8004A908_Floor`.
- Airborne and grounded wrapper dispatch: `mpColl_80046904`, `mpColl_80046F78`, `mpColl_800471F8`, `mpColl_80047AC8`, `mpColl_80047BF4`, `mpColl_80047D20`, `mpColl_80047E14`, `mpColl_80047F40`, `mpColl_8004806C`, `mpColl_8004ACE4`, `mpColl_8004C534`.
- Ordered wall/ceiling helpers: `mpColl_80044AD8_Ceiling`, `mpColl_80044C74_Ceiling`, `mpColl_80044E10_RightWall`, `mpColl_800454A4_RightWall`, `mpColl_80045B74_LeftWall`, `mpColl_80046224_LeftWall`, `mpColl_80048AB0_RightWall`, `mpColl_800491C8_RightWall`, `mpColl_80049778_LeftWall`, `mpColl_80049EAC_LeftWall`, `mpColl_8004B894_RightWall`, `mpColl_8004BDD4_LeftWall`, `mpColl_8004C328_Ceiling`.
- Squeeze/speed/skip helpers: `mpCollSqueezeHorizontal`, `mpCollSqueezeVertical`, `mpCollGetSpeedCeiling`, `mpCollGetSpeedLeftWall`, `mpCollGetSpeedRightWall`, `mpCollGetSpeedFloor`, `mpColl_IsOnPlatform`, `mpUpdateFloorSkip`, `mpClearFloorSkip`.

Acceptance for this owner: there is a single allocation-free C substrate for floor, wall, ceiling, squeeze, speed, and skip state. Wrapper selection is table/data-backed where possible and not replicated as scattered action-id lists.

### `mpLib` Query Substrate And Stage Line Metadata

Audit/port:

- `refs/melee/src/melee/mp/mplib.c`
- `refs/melee/src/melee/mp/mplib.h`
- `refs/melee/src/melee/mp/mpisland.c`
- `refs/melee/src/melee/gr/ground.c`
- `refs/melee/src/melee/gr/grlib.c`
- `refs/melee/src/melee/gr/stage.c`
- Legal-stage sources: `grbattle.c`, `grizumi.c`, `grlast.c`, `grpstadium.c`, `grstory.c`, `groldpupupu.c`, `groldyoshi.c`.
- Extractors/loaders: `tools/extraction/extract_stage_collision.py`, `tools/extraction/extract_stage_metadata.py`, `src/stage_collision.c`, `src/stage_collision.h`, `src/mpcoll_env.c`, `src/mpcoll_env.h`, `data/stages/*.json`, `data/stages/bin/*`.

Required function families:

- Base map data load and stable ordering: `mpLibLoad`, `mpPruneEmptyLines`, `mpGetGroundCollVtx`, `mpGetGroundCollLine`, `mpGetGroundCollJoint`.
- Segment queries: `mpCheckFloor`, `mpCheckFloorRemap`, `mpCheckCeiling`, `mpCheckCeilingRemap`, `mpCheckLeftWall`, `mpCheckLeftWallRemap`, `mpCheckRightWall`, `mpCheckRightWallRemap`, `mpCheckMultiple`, `mpCheckAll`, `mpCheckAllRemap`.
- Projection/adjacency/endpoints: `mpLib_8004DD90_Floor`, `mpLib_8004E090_Ceiling`, `mpLib_8004E398_LeftWall`, `mpLib_8004E684_RightWall`, `mpLineNextNonFloor`, `mpLinePrevNonFloor`, `mpLinePrevNonCeiling`, `mpLineNextNonCeiling`, `mpLineNextNonLeftWall`, `mpLinePrevNonLeftWall`, `mpLinePrevNonRightWall`, `mpLineNextNonRightWall`, `mpFloorGetRight`, `mpFloorGetLeft`, `mpCeilingGetRight`, `mpCeilingGetLeft`, `mpLeftWallGetTop`, `mpLeftWallGetBottom`, `mpRightWallGetTop`, `mpRightWallGetBottom`, `mpLineGetKind`, `mpLineGetFlags`, `mpLineGetNormal`, `mpLinesConnected`, `mpJointFromLine`.
- Dynamic hooks needed later: `mpJointHide`, `mpJointUnhide`, `mpJointUpdateDynamics`, `mpJointUpdateBounding`, `mpLineSetPos`, `mpGetSpeed`, joint callback setters/getters.

Acceptance for this owner: extracted stage metadata is complete enough that runtime collision does not infer legal-stage behavior from local semantic lists. Any new runtime-required field updates extractor, stable JSON/binary layout, loader required keys, packaged fallback, and fresh-extract smoke coverage in the same change.

### Moving Platforms Final Phase

Audit/port only after all static acceptance gates pass:

- Stage-specific moving-geometry code in `refs/melee/src/melee/gr/grstory.c`, `refs/melee/src/melee/gr/grizumi.c`, and `refs/melee/src/melee/gr/groldpupupu.c`.
- Common ground/dynamic support in `refs/melee/src/melee/gr/ground.c`, `refs/melee/src/melee/gr/granime.c`, `refs/melee/src/melee/gr/grdynamicattr.c`, `refs/melee/src/melee/mp/mplib.c`, and `refs/melee/src/melee/mp/mpcoll.c`.

Acceptance for this owner:

- Static core acceptance remains green before and after enabling moving-geometry work.
- Dynamic platforms update collision segment positions, bounds, visibility/activation, normals, and speed in the same order as source.
- Common fighter wrappers consume moving-surface speed through `mpCollGetSpeed*`/`mpGetSpeed`-equivalent state, not through stage-specific fighter hacks.
- Randall and Fountain of Dreams platform tests cover platform present/absent, rising/falling, edge contact, airborne landing, wall/ceiling adjacency where applicable, and pass-through behavior.
- Moving-platform work is rejected if it introduces static-stage regressions, runtime allocations, or character-id proxy branches.

## Acceptance Criteria

Meeting validation alone is not enough. A pass is accepted only if source structure, tests, and cleanup all pass.

### Source Coverage

- Every runtime gameplay change in `src/` has a nearby source pointer to `refs/melee/src/...`, matching asm, UCF source, or extracted `data/...`.
- Each in-scope owner family has a short source map in comments or docs that names the decomp owner and the simulator owner.
- Historical docs and dirty local changes are not accepted as proof. They may only provide hypotheses to verify from source.
- Character-specific special callbacks are included only where they call common collision wrappers; otherwise they are explicitly deferred.
- For every removed or retained old collision suppress/reject path, the implementation must name the source owner that now subsumes it or the exact source policy that still requires it.

### Code Structure

- Common collision behavior is centralized in C under the existing `mpcoll_*`, `stage_collision`, `state`, and callback-owner substrates.
- Wrapper selection is driven by extracted/generated owner tables or source-backed helper predicates where feasible.
- `CollData` begin/end state, ECB source, skip state, floor/wall/ceiling ownership, and env flags are explicit simulator state, not reconstructed by replay bridges.
- No new local action-id lists are added if `MSLMSO01`, `MSLSTG01`, `MSLPART1`, `MSLITAR1`, or `MSLFTSC1` can express the owner.
- No old bridge, suppress, fallback, or compensating path remains unaccounted for. Each one must be removed, converted into explicit source state, or documented as non-runtime debug tooling.

### Tests

- Add focused synthetic tests for each owner family before relying on replay validation: grounded edge/walk-off, airborne floor/wall/ceiling, AttackAir landing, EscapeAir landing/fall-special, DamageFly floor/wall/ceiling, SDI displacement into collision, floor skip/pass-through, joint skip/filtering, and static legal-stage segment metadata.
- Add negative tests proving that excluded item-only and bespoke special-only paths are not accidentally routed through the new common wrappers.
- Add data contract tests for any new stage metadata fields, including fresh extraction and packaged fallback expectations.
- Add moving-platform tests only in the final phase and keep them separate from static collision tests.

### Validation

- Required commands for implementation passes: `make test`, `make fmt-check`, and `make validate-all`.
- If core sim logic changes, refresh committed validation reports listed in the repository instructions.
- Acceptance does not require every aggregate metric to improve immediately, but it does require no unexplained regressions and no replay-fitted exception paths.
- Validate both one-step and rollout behavior on supported legal stages, singles and viable doubles paths, with Fox/Falco coverage.

### Performance And Determinism

- No heap allocations after initialization on runtime gameplay paths, including `reseed_seed`, `step_input`, `write_compare`, and frame-step passes.
- Fixed-capacity buffers, deterministic iteration order, deterministic tie-breaking, and SoA/AoSoA-compatible state layout are required.
- New extraction/preprocess seed-lane derivation that affects hot paths must be native C or must include timing proof that Python cost is negligible.
- Debug-only allocation is allowed only in clearly non-runtime tools or forensic paths.

## Staged Execution Strategy

The coding agent should work in large source-owner phases, but each phase has a hard acceptance boundary. It must not report a phase complete because a subset of actions or replay rows works.

When this document is used as a `/goal` objective, the agent must treat the active phase section as the detailed prompt. Do not wait for additional instructions unless a required source file or generated data artifact is missing.

### Phase 0: Freeze The Contract

- Start from a clean branch or a deliberately reviewed dirty tree snapshot.
- Inventory current collision-related dirty changes, but do not trust them as source proof.
- Build a source map from decomp to current simulator owners for the static in-scope families.
- Do not mark this phase complete until every planned runtime behavior has a source owner or a documented deferral.
- Do not generate hundreds of `NEEDS_PORT` rows as a substitute for implementation. The map should identify owner families, source functions, MSL owners, and concrete acceptance tests.

### Phase 1: Stage Metadata And `mpLib` Substrate

Phase 1 is the first implementation phase. Its target is only static legal-stage metadata and reusable `mpLib` query substrate. It must leave fighter action routing, `CollData` wrapper replacement, and moving-platform runtime behavior for later phases.

Required source reads before coding:

- `refs/melee/src/melee/mp/mplib.c`
- `refs/melee/src/melee/mp/mplib.h`
- `refs/melee/src/melee/mp/mpcoll.c`, only for static query consumers and flags passed into `mpLib`
- `refs/melee/src/melee/mp/types.h`
- `refs/melee/src/melee/gr/ground.c`
- `refs/melee/src/melee/gr/grlib.c`
- `refs/melee/src/melee/gr/stage.c`
- Legal-stage sources listed in the `mpLib Query Substrate And Stage Line Metadata` section
- Current MSL files: `src/stage_collision.c`, `src/stage_collision.h`, `src/mpcoll_env.c`, `src/mpcoll_env.h`, `src/mpcoll_ground.c`, `tools/extraction/extract_stage_collision.py`, `tools/extraction/extract_stage_metadata.py`, `tools/extraction/known_data_artifacts.py`, and existing stage-data tests.

Do not spawn subagents for this work unless the user explicitly authorizes it in the current turn.
The implementation agent owns the full source read, current-code mapping, test design, and perf-risk
check locally. Use concise notes in the report instead of delegating these roles.

Hard non-scope for Phase 1:

- Do not route any fighter action, MotionState callback, or wrapper through new behavior.
- Do not replace `CollData` begin/end, ECB interpolation, `mpColl_80043754`, or action-level floor/wall/ceiling publication.
- Do not implement moving-platform runtime behavior, Randall runtime behavior, or Fountain-of-Dreams dynamic platform behavior.
- Do not add replay-row locks, trace-specific fixtures, stage-name gameplay branches, y clamps, tolerance broadening, or action-id exceptions.
- Do not use `agent_docs/systems/` as authority. It can only point to code that must be re-verified from source.

Implementation targets:

- Prove extracted static legal-stage metadata can represent all segment kind, flags, normals, endpoints, adjacency, ledge/pass-through, joint, and stable ordering needed by `mpLib`.
- Update extractor, binary/JSON layout, loader required keys, packaged data, and fresh-extract smoke tests together for any missing field.
- Port or replace local query approximations with source-backed `mpLib` helpers.
- Do not move to fighter wrappers while `mpLib` still needs local semantic lists for static legal-stage collision.
- This phase should produce reusable query functions and tests for floor/wall/ceiling intersection, endpoint/adjacency, strict segment bounds, remap/tie behavior, line flags, floor skip, and joint skip. It should not add action-specific collision behavior.
- Data-only or schema-only changes are not enough. Any new field must be consumed by a reusable static query helper now, or the implementation must name the exact next-phase function that will consume it and add a test proving the field is generated/loaded correctly.
- If the current runtime already has a source-equivalent static query helper, keep it and document the proof instead of rewriting it.

Required tests:

- Data contract tests for any new or corrected stage metadata fields.
- Synthetic query tests for floor, ceiling, left wall, and right wall intersection on static legal-stage lines.
- Endpoint/adjacency tests for connected and disconnected lines.
- Strict segment-bound tests: in-span hit, endpoint hit, and outside-span reject.
- Remap/tie tests where source behavior is clear from `mpLib`.
- Floor skip, joint skip, and joint-only filter tests at the query-helper level.
- Negative tests proving moving-platform/dynamic lines are not accidentally handled by static-only helpers.

Required validation:

- `make build_data`
- `make build BUILD_FORCE=1`
- focused new/changed tests
- `make validate-all`
- `make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- If any runtime hot path changes, run the usual light `rollout_compare` bench and report clean-vs-dirty.

Required report back:

- Exact source files/functions audited.
- Metadata fields added/corrected, with source backing.
- Reusable static query helpers added/replaced, and which old approximations or duplicate helpers they remove.
- Tests added and what source boundary each proves.
- Validation and perf results.
- Whether Phase 1 is complete by this document's criteria. If incomplete, name the remaining Phase 1 items and do not move to Phase 2.
- Leave changes uncommitted for review.


### Phase 2: `CollData` Lifetime And Ordered `mpColl`

Phase 2 is the ordered collision substrate phase. Its target is the common `CollData`/`mpColl`
machinery that later action owners call. It must not be a replay-row fix, an action-family routing
pass, or a substrate-only checkpoint with no replacement of old ordered collision paths.

Phase 2 acceptance is binary: the static legal-stage ordered substrate rows below must be complete,
and rows marked `OUT_OF_SCOPE` or `DEFERRED` are not Phase 2 acceptance criteria. Broad action-owner
routing, moving-platform/Ground-object callbacks, source `mpBoundingCheck` joint mutation, and
separate source left/right wall `SurfaceData` lifetime are later named phases, not Phase 2 debt.

Required source reads before coding:

- `refs/melee/src/melee/mp/mpcoll.c`
- `refs/melee/src/melee/mp/mpcoll.h`
- `refs/melee/src/melee/mp/mplib.c`, only for query calls consumed by `mpColl`
- `refs/melee/src/melee/mp/types.h`
- `refs/melee/src/melee/lb/types.h`, especially `CollData`, `SurfaceData`, ECB fields, env/contact
  flags, floor/wall/ceiling surface records, skip fields, and callback fields
- `refs/melee/src/melee/ft/ft_081B.c`, only for wrapper call sites and expected callback outcomes
- Current MSL files: `src/mpcoll_ground.c`, `src/mpcoll_wall_ceil.c`, `src/mpcoll_env.c`,
  `src/mpcoll_context.h`, `src/mpcoll_ecb_points.h`, `src/stage_collision.c`,
  `src/stage_collision.h`, `src/state.h`, `src/state_fields.inc`, `src/state.c`, `src/api.c`,
  `bindings/msl_binding.c`, `src/motion_state_owners.*`, and existing collision tests.

Do not spawn subagents for this work unless the user explicitly authorizes it in the current turn.
The implementation agent owns the full source read, current-code mapping, test design, and perf-risk
check locally:

- Source read: produce a concise source-order map for `mpCollPrev`, `mpColl_80043754`,
  `mpCollInterpolateECB`, `mpColl_80046904`, `mpColl_8004ACE4`, floor/wall/ceiling producer
  helpers, `mpCollEnd*`, skip helpers, and squeeze/speed helpers.
- MSL mapping: identify current split scans, bridge/suppress predicates, seed-only lanes, duplicate
  floor/wall/ceiling producers, and which call sites can be replaced by a common ordered substrate.
- Test design: add pure wrapper/substrate tests before replay validation. Tests must prove positive
  and negative source-state boundaries, not just one observed trace.
- Perf risk: check state layout, loop bounds, allocation-free behavior, deterministic ordering, and
  whether old duplicate scans are actually removed when new ordered paths are routed.

Hard non-scope for Phase 2:

- Do not route broad grounded/airborne action families yet. Phase 2 may add debug/test entry points
  or replace existing low-level wrapper helpers, but Phase 3 owns action-owner rollout routing.
- Do not implement moving-platform runtime behavior, Randall runtime behavior, or FoD dynamic
  platform behavior beyond preserving the Phase 1 static query contract.
- Do not implement character-special bespoke behavior unless it is already a direct call to a common
  `mpColl` wrapper required for the substrate.
- Do not add replay-row locks, trace-specific fixtures, stage-name gameplay branches, y clamps,
  tolerance broadening, or action-id exceptions.
- Do not claim a broad source row as complete when only a slice is wired. If a function has static,
  moving, platform, ledge, skip, remap, entry, sustained, or endpoint variants, either port all
  in-scope static variants or split the row explicitly and leave the rest open.

Implementation targets:

- Model `CollData` callback lifetime in SoA/runtime state:
  `cur_pos`, `prev_pos`, `last_pos`, current/previous/desired ECB, ECB source kind, env flags,
  previous env flags, contact flags, floor/wall/ceiling surface ids, floor skip, joint skip,
  joint-only, callback-local result scratch, and the source bits needed by interpolation/squeeze.
- Port the callback lifecycle:
  `mpCollPrev`, `mpCollCheckBounding` boundaries, `mpCollInterpolateECB`, `mpCollEnd`,
  `mpCollEnd_inline`, `mpCollEnd_inline2`, `mpCopyCollData`, and source clear/update timing.
  Phase 2 may keep `mpCollCheckBounding` as a static/debug AABB substrate until extracted joint
  bounds and source `TooFar`/dynamic joint state exist; do not wire it as a generic segment filter.
- Port ordered floor/wall/ceiling producers as reusable packet/result helpers:
  `mpColl_80044628_Floor`, `mpColl_80044838_Floor`, `mpColl_80044948_Floor`,
  `mpColl_80044AD8_Ceiling`, `mpColl_80044C74_Ceiling`,
  `mpColl_80044E10_RightWall`, `mpColl_800454A4_RightWall`,
  `mpColl_80045B74_LeftWall`, `mpColl_80046224_LeftWall`.
- Port ordered airborne and grounded wrapper cores enough that later action owners can call one
  common owner:
  `mpColl_80043754`, `mpColl_80046904`, `mpColl_8004ACE4`, `mpColl_8004A908_Floor`,
  and static legal-stage portions of `mpColl_800471F8`, `mpColl_800477E0`,
  `mpColl_80047E14`, and `mpColl_8004B108` where they share the same ordered substrate.
- Port skip/filter/squeeze/speed state needed by the ordered substrate:
  `mpUpdateFloorSkip`, `mpClearFloorSkip`, joint skip, joint only, `mpCollSqueezeHorizontal`,
  `mpCollSqueezeVertical`, static `mpCollGetSpeedFloor`, static `mpCollGetSpeedCeiling`, and
  `mpColl_IsOnPlatform`. `mpCollGetSpeedLeftWall` / `mpCollGetSpeedRightWall` require separate
  source `left_facing_wall` / `right_facing_wall` `SurfaceData` records and are deferred out of
  Phase 2 rather than approximated as source-complete through singleton wall state.
- Replace or collapse existing old-path scans/suppressions only when the new source state actually
  subsumes them. Every retained bridge/suppress/fallback must be named in the report with the exact
  source policy or the later phase that owns its removal.
- Keep all new runtime paths allocation-free and deterministic. Do not allocate, format, grow
  buffers, or use Python in gameplay/preprocess hot paths.

Required tests:

- Pure `CollData` lifecycle tests: callback begin/end root state, previous/current/last position,
  ECB load/source selection, current/previous/desired ECB promotion, interpolation ordering, stale
  seed state rejection, and callback-local scratch clear/update.
- Ordered collision tests independent of replay rows:
  floor-only, ceiling-only, left-wall-only, right-wall-only, floor+wall, floor+ceiling,
  simultaneous same-distance tie, strict no-crossing, endpoint tolerance, source-order tie, and
  remap/joint filter behavior using the Phase 1 static query substrate.
- Skip/filter tests: floor skip clear/update, one-way platform pass-through state, joint skip,
  joint only, wrong-joint negatives, expired skip negatives, and skip preservation/clear boundaries.
- Wrapper-core tests:
  airborne ordered core, grounded ordered core, floor retry, edge/squeeze behavior, env/contact
  flag lifetime, and `mpCollEnd*` publication/clear boundaries.
- Negative tests proving Phase 2 does not accidentally route broad action-owner behavior,
  item-only collision, moving-platform runtime behavior, or bespoke special-only callbacks.
- If debug/test-only API hooks are added, tests must prove they do not allocate after init and are
  not used by normal `step_input`, `reseed_seed`, or `write_compare` paths.

Required validation:

- `make build_data` if any data/schema/extraction path changes
- `make build BUILD_FORCE=1`
- focused new/changed collision tests
- `make validate-all`
- `make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- Run the usual light `rollout_compare` clean-vs-dirty bench, because Phase 2 changes runtime
  collision hot paths.

Required report back:

- Exact source files/functions audited.
- For each target source function family, whether it is `COMPLETE`, `DEFERRED`, or `OUT_OF_SCOPE`.
  Do not use `PARTIAL` in the Phase 2 acceptance table; split the row so the in-scope portion is
  either complete or the phase is incomplete.
- Concrete MSL symbols added/changed for `CollData` state, ordered producers, wrapper cores,
  skip/squeeze/speed state, and finalizers.
- Old scans/suppressions/fallbacks removed, collapsed, or retained with source-policy accounting.
- Tests added and what source boundary each proves.
- Validation and clean-vs-dirty perf results.
- Whether Phase 2 is complete by this document's criteria. If incomplete, name the remaining
  Phase 2 items and do not move to Phase 3.
- Leave changes uncommitted for review.

Current Phase 2 source-status report:

| Source family | Status | Evidence / remaining boundary |
| --- | --- | --- |
| `CollData` root lifetime (`cur_pos`, `prev_pos`, `last_pos`) | COMPLETE for modeled 2D collision roots | SoA lanes cover visible `cur_pos`, callback/substep previous/current roots, wall/ceiling callback roots, and `last_pos`. Tests cover reseed/debug initialization, copy, and callback-root publication. |
| `CollData` ECB lifetime and `mpCollInterpolateECB` | COMPLETE for current static substrate | Current/previous/desired ECB and source squeeze-restore (`x64_ecb`/b6) are modeled; tests cover load, promotion, multi-substep penultimate `prev_ecb`, stale seed rejection, and squeeze restore. |
| `mpCollPrev` / callback-local scratch clear-update | COMPLETE for split floor/wall/ceiling producers | Floor and wall/ceiling callback-entry roots are materialized before producer scans; tests cover scratch clear/update, no stale floor result, and `last_pos`. |
| `mpCollCheckBounding` / `mpBoundingCheck` | DEFERRED out of Phase 2 runtime | Static/debug AABB helper is source-shaped and tested only as a boundary probe. Runtime `TooFar` joint mutation, hidden/enabled joint flags, and dynamic joint counts require extracted joint-bounds and moving-platform/Ground-object state. It must not be wired as a generic segment filter in Phase 2. |
| Floor producers `mpColl_80044628_Floor`, `mpColl_80044838_Floor`, `mpColl_80044948_Floor` | COMPLETE for Phase 2 static legal-stage substrate | Static no-preference floor sweeps use `stage_collision_static_query`; persisted/preferred, floor skip, endpoint, 4A908 retry, remap, and joint filters have synthetic coverage. FoD/Randall/deferred transform behavior is explicitly out of Phase 2 and owned by the moving-platform phase. |
| Ceiling producers `mpColl_80044AD8_Ceiling`, `mpColl_80044C74_Ceiling` | COMPLETE for Phase 2 static legal-stage ordered substrate | Axis-aligned no-preference ceilings use source-shaped zero-delta rejection, endpoint tolerance, and deterministic ordering in the ordered graph scan; preferred/sloped graph scans remain source-policy fallbacks for current static legal stages. Tests cover ceiling-only, ECB top crossing, joint-only negative, speed, and env previous/current lifetime. |
| Right-wall producers `mpColl_80044E10_RightWall`, `mpColl_800454A4_RightWall` | COMPLETE for static ordered substrate | Tests cover right-wall-only persistence, ECB side crossing, bottom-only Push negative for walljump, joint skip negative, speed, and env previous/current lifetime. |
| Left-wall producers `mpColl_80045B74_LeftWall`, `mpColl_80046224_LeftWall` | COMPLETE for Phase 2 static ordered substrate | Tests cover left-wall-only persistence and previous/current env lifetime; shared candidate filtering uses the same joint/static substrate as right wall. Separate source `left_facing_wall` / `right_facing_wall` surface lifetime is not claimed here and is listed below as deferred wall-surface work. |
| Grounded ordered wrapper `mpColl_8004ACE4` | COMPLETE for Phase 2 static ordered core | Grounded wall/ceiling prepass ordering, floor retry, floor+wall, floor+ceiling squeeze, horizontal/vertical squeeze, and floor skip are covered. Broad grounded action-owner routing is Phase 3 and is not a Phase 2 acceptance criterion. |
| Airborne ordered wrappers `mpColl_80043754`, `mpColl_80046904`, static portions of `mpColl_800471F8`, `mpColl_800477E0`, `mpColl_80047E14`, `mpColl_8004B108` | COMPLETE for Phase 2 static ordered core | Shared floor/wall/ceiling producers and callback-local state are callable and tested through existing low-level/runtime paths. Broad airborne action-owner routing is Phase 3/4; moving platforms and Ground callbacks are deferred out of Phase 2. |
| `mpUpdateFloorSkip`, `mpClearFloorSkip`, joint skip, joint only | COMPLETE for static substrate | Runtime lanes and helpers replace direct writes in touched paths. Tests cover skip update/clear, expired skip, platform pass-through, wrong-joint negatives, joint skip, joint only, and copy/debug exposure. |
| `mpCollSqueezeHorizontal`, `mpCollSqueezeVertical` | COMPLETE for static squeeze state | Horizontal/vertical squeeze helpers save source restore ECB and update current/previous/desired state. `mpCollSqueezeVertical` does not write `floor_skip`; floor-skip writes remain tied only to source `mpUpdateFloorSkip` owners. Moving-platform squeeze variants remain deferred. |
| Static `mpCollGetSpeedFloor`, static `mpCollGetSpeedCeiling`, `mpColl_IsOnPlatform` | COMPLETE for Phase 2 static floor/ceiling/platform checks | Static floors/ceilings return valid zero speed and platform flags from generated line metadata. FoD/Randall dynamic endpoint speed is deferred. |
| `mpCollEnd`, `mpCollEnd_inline`, `mpCollEnd_inline2` | COMPLETE for Phase 2 static finalizer event helper | Static event selection covers forced/edge floor callbacks, ceiling push/hug callbacks, sentinels, and `dy = cur_pos.y - last_pos.y`. Dynamic `grDynamicAttr_801CA284` low-byte refresh and actual Ground callbacks are deferred with moving-platform/Ground-object behavior. |
| `mpCopyCollData` | COMPLETE for modeled MSL CollData lanes | Debug-only helper copies modeled CollData root/last/root-sweep, ECB, env/contact, floor/wall/ceiling ids, `floor_skip`, `joint_id_skip`, and scratch lanes without copying `joint_id_only` or Fighter-owned action/stock/animation state. |
| Non-scope routing guard | COMPLETE as source-token guard only | Source-token tests guard against accidentally referencing Phase 2 helpers from item-only or special-only source files. They are not proof of broad runtime non-routing; runtime non-scope is enforced by keeping Phase 2 entry points debug/test-only or low-level substrate calls and by leaving broad action-owner routing to Phase 3/4. |

Deferred / out-of-scope rows not counted against Phase 2 acceptance:

| Source family | Status | Owning later phase |
| --- | --- | --- |
| Source `left_facing_wall` / `right_facing_wall` `SurfaceData` and `mpCollGetSpeedLeftWall` / `mpCollGetSpeedRightWall` | DEFERRED | Separate wall-surface state and moving/static wall speed helpers are owned by the wall-surface/moving-platform follow-up. Phase 2 keeps singleton wall debug fields explicitly non-source-complete. |
| Moving platforms, Randall, FoD dynamic endpoint speed, `grDynamicAttr_801CA284`, and Ground callbacks | DEFERRED | Moving-platform/Ground-object phase. |
| Broad grounded/airborne action-owner routing through common wrappers | OUT_OF_SCOPE | Phase 3. |
| AttackAir, EscapeAir, Damage, item-only, and bespoke special-family routing | OUT_OF_SCOPE | Phase 4 or owner-specific later phases. |

Phase 2 status by this document: COMPLETE once the required validation and clean-vs-dirty
rollout_compare bench below pass without a material retained regression.

Retained fallback/source-policy accounting:

- Preferred/persisted line scans remain graph-based when source needs connected-line traversal,
  endpoint preference, wall hug/slop, or explicit skip semantics not expressible as a no-preference
  static query. Axis-aligned wall/ceiling graph scans carry the source zero-delta rejection and
  endpoint tolerance directly, avoiding a duplicate static-query prepass in the hot path.
- Sloped wall/ceiling paths remain graph-scanned under the current legal-stage ordered producers.
- FoD height platforms, Randall, dynamic `grDynamicAttr_801CA284`, source `mpBoundingCheck`
  `TooFar` joint mutation, and Ground callbacks are deferred to the moving-platform/Ground-object
  phase.
- Broad common grounded/airborne and AttackAir/EscapeAir/Damage action-owner routing remains Phase
  3/4 work; Phase 2 only makes the ordered substrate callable and replaces bounded low-level
  producer paths.

### Phase 3: Common Grounded And Airborne Owners

Phase 3 is the common action-owner routing phase. Its target is to route ordinary grounded and
ordinary airborne fighter collision callbacks through the Phase 2 ordered substrate without
duplicating floor/wall/ceiling scans in each action family.

Do not spawn subagents for this work unless the user explicitly authorizes it in the current turn.
The implementation agent owns the source read, current-code mapping, test design, and perf-risk
check locally.

Required source reads before coding:

- `refs/melee/src/melee/ft/ft_081B.c`, especially common grounded/airborne wrappers around
  `ft_80082708`, `ft_800827A0`, `ft_80082C74`, `ft_80082D40`, `ft_80083090`,
  `ft_800831CC`, `ft_800835B0`, `ft_80083F88`, and adjacent wrapper variants.
- `refs/melee/src/melee/mp/mpcoll.c`, especially `mpColl_80043754`, `mpColl_80046904`,
  `mpColl_8004ACE4`, `mpColl_8004A908_Floor`, `mpColl_8004B108`, `mpColl_8004B2DC`,
  `mpCollEnd*`, floor skip helpers, squeeze helpers, and static floor/wall/ceiling producers.
- Motion-state owner data in `data/motion_state/owners/*.bin::MSLMSO01` and current generated
  helpers in `src/motion_state_owners.*`.
- Current MSL call sites in `src/fighter_callbacks.c`, `src/locomotion.c`,
  `src/mpcoll_ground.c`, `src/mpcoll_wall_ceil.c`, `src/mpcoll_env.c`, `src/shine.c`, and
  collision tests.

In-scope owner families:

- Common grounded locomotion/idle families whose source collision callback uses the common grounded
  wrappers, including sustained and entry/transition variants for Wait, Walk, Dash/Run/Turn where
  they use common ground collision, Squat/SquatWait, KneeBend, Landing/LandingFallSpecial where
  they are not AttackAir/EscapeAir/Damage-owned, Guard/GuardOff/GuardSetOff common grounded
  movement where collision is not combat-owned, Ottotto/MissFoot/Pass as common grounded movement,
  and common grounded special wrappers that call the same common mpColl owner rather than bespoke
  special collision.
- Common airborne non-attack/non-damage/non-escape families whose source collision callback uses the
  common airborne wrappers, including Fall/FallSpecial, JumpAerial/CliffJump aerial continuation,
  common airborne special wrappers that call the same common mpColl owner, and wall/ceiling/landing
  outcomes from those owners.
- Static legal-stage variants of the above: hard floors, static soft platforms, static walls,
  static ceilings, slopes, endpoints, ledge exclusion/inclusion where owned by the common wrapper,
  floor skip, joint skip/only, squeeze, floor retry, and `mpCollEnd*` publication.

Hard non-scope for Phase 3:

- Do not implement moving-platform runtime behavior, Randall carry, FoD dynamic platform endpoint
  speed, dynamic Ground callbacks, or runtime `mpBoundingCheck` `TooFar` joint mutation. Preserve
  existing behavior and keep these deferred to Phase 6.
- Do not route `AttackAir`, `EscapeAir`, Damage/DamageFly/DamageFall/DamageAir, item-only
  collision, projectile collision, catch/throw/capture, or character-bespoke special collision
  families. These are Phase 4 or owner-specific later phases.
- Do not add replay-row fixes, trace-specific fixtures, dataset gates, stage-name gameplay gates,
  y clamps, broad tolerance hacks, or local action-id lists when generated MSLMSO01 owner data can
  express the routing.
- Do not mark a broad owner complete from one sustained/static slice. Entry, sustained, transition,
  endpoint, platform, slope, ledge, wall/ceiling, floor-loss, and skip variants must either route
  through the common owner or be explicitly deferred to a named later phase with source reasoning.

Implementation targets:

- Route in-scope common grounded callbacks through a single source-shaped grounded owner built on
  the Phase 2 ordered substrate (`mpColl_8004ACE4`, floor retry, endpoint/floor edge, squeeze,
  skip, wall/ceiling prepass, floor-loss/fall handoff, and `mpCollEnd*` publication).
- Route in-scope common airborne callbacks through a single source-shaped airborne owner built on
  the Phase 2 ordered substrate (`mpColl_80043754` / `mpColl_80046904` style ordering, floor
  publication/landing handoff, wall/ceiling contact, ledge exclusion/inclusion when common-owned,
  floor skip, joint filters, and `mpCollEnd*` publication).
- Replace or collapse old local scans/suppressions/fallbacks in in-scope action owners only when
  the common owner subsumes them. Every retained fallback must be named in the report with the exact
  source policy or later phase that owns removal.
- Use generated MSLMSO01 owner classes for action routing. If table data is missing, extend
  extraction/table helpers rather than adding a local action list, unless a small manual predicate is
  explicitly source-backed and documented.
- Keep runtime allocation-free and deterministic. No heap allocation, formatting, dynamic buffers,
  Python loops, nondeterministic iteration, or data reads on gameplay paths.

Required tests:

- Pure owner-routing tests proving generated MSLMSO01 classes select the common grounded/airborne
  owners and do not select AttackAir/EscapeAir/Damage/item/projectile/bespoke-special owners.
- Grounded owner tests for entry, sustained, transition, platform, hard floor, slope, endpoint,
  floor-loss, floor retry, squeeze, wall, ceiling, floor+wall, floor+ceiling, skip clear/update,
  joint skip/only, and `mpCollEnd*` publication/clear boundaries.
- Airborne owner tests for falling no-contact, hard-floor landing, platform admission/rejection,
  wall/ceiling contact, floor skip, joint filters, endpoint/ledge exclusion where common-owned,
  strict no-crossing negatives, and transition into grounded state.
- Negative tests proving excluded families are not routed through Phase 3: AttackAir, EscapeAir,
  Damage, item/projectile, and bespoke special collision.
- Regression tests should be owner-invariant synthetic locks first. Replay/trace locks may be kept
  only as secondary evidence and must not define the runtime branch.

Required validation:

- `make build_data` if generated owner data or extraction changes
- `make build BUILD_FORCE=1`
- focused changed collision/action-owner tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- Clean-vs-dirty light `rollout_compare` bench. A small retained slowdown may be accepted only if
  it is tied to source-correct routing and there is no obvious duplicate work left.

Required report back:

- Exact source functions and generated owner tables audited.
- In-scope grounded and airborne owner families routed, with concrete MSL symbols.
- Excluded/deferred families and why they are not Phase 3.
- Old scans/suppressions/fallbacks removed, collapsed, or retained with source-policy accounting.
- Tests added/updated and what source boundary each proves.
- Validation and clean-vs-dirty perf numbers.
- Whether Phase 3 is COMPLETE by this document's criteria. If incomplete, name exact remaining
  Phase 3 work and do not move to Phase 4.
- Leave changes uncommitted for review.

Phase 3 status by this document: COMPLETE for the static common grounded/airborne owner scope.

Routed common owner classes:

- `MSL_MS_CLASS2_COMMON_GROUNDED_COLL`: Wait/Walk/Dash/Run/Turn, Squat/SquatWait/SquatRv,
  Landing/LandingFallSpecial, Guard common movement, and Ottotto/OttottoWait callback owners.
- `MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL`: KneeBend, Turn/Dash/Run/RunDirect, Squat-family, and
  Guard common callbacks that route through `ft_80082708 -> mpColl_8004B108`.
- `MSL_MS_CLASS2_COMMON_GROUNDED_B2DC_COLL`: TurnRun/Ottotto/OttottoWait common endpoint callbacks
  that route through `ft_800827A0 -> mpColl_8004B2DC`.
- `MSL_MS_CLASS2_COMMON_GROUNDED_B4B0_COLL`: Wait/Walk/RunBrake and Landing/LandingFallSpecial
  callbacks that route through `ft_80084280 -> mpColl_8004B4B0`.
- `MSL_MS_CLASS2_COMMON_AIRBORNE_COLL`: Jump/JumpAerial, Fall/FallSpecial, CliffJump2
  continuation, Pass, and MissFoot callback owners.

Retained outside Phase 3:

- AttackAir, EscapeAir, Damage/DamageFly/DamageFall/DamageAir, item/projectile, catch/throw/capture,
  and Fox/Falco bespoke special callbacks stay on their older broad owner classes for Phase 4 or
  later owner-specific work. They may still consume the same low-level ordered substrate, but they
  do not enter the Phase 3 `class2_bits` common owner word.
- Moving-platform runtime behavior, Randall carry, FoD dynamic endpoint speed, dynamic Ground
  callbacks, and runtime `mpBoundingCheck` `TooFar` joint mutation remain Phase 6.

Validation for completion:

- `make build_data`
- `make build BUILD_FORCE=1`
- focused changed owner/collision tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- clean-vs-dirty `rollout_compare` bench retained +0.32% ns/env (`2520.229` clean,
  `2528.372` dirty), same checksum `13352543245587097619`.

### Phase 4: AttackAir, EscapeAir, And Damage Families

Phase 4 is the later-owner fighter collision phase for the high-risk action families deliberately
excluded from Phase 3. Its target is to route `AttackAir`, `EscapeAir`, and Damage-family collision
callbacks through source-shaped owner gates and the Phase 2 ordered substrate, without replay-row
bridges or broad action exceptions.

Do not spawn subagents for this work unless the user explicitly authorizes it in the current turn.
The implementation agent owns the source read, current-code mapping, test design, and perf-risk
check locally.

Required source reads before coding:

- `refs/melee/src/melee/ft/ft_081B.c`, especially wrappers around `ft_80083090`,
  `ft_800831CC`, `ft_800835B0`, `ft_80081DD4`, `ft_80081D0C`, `ft_80082B1C`,
  `ft_80082708`, `ft_800827A0`, and ledge/cliff handoff helpers used by these families.
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c`,
  `ftCo_EscapeAir.c`, `ftCo_Damage.c`, damage-fly/fall source files, and adjacent common
  transition code for landing, hitlag, SDI/ASDI, wall/ceiling/floor outcomes, and action changes.
- `refs/melee/src/melee/mp/mpcoll.c`, especially `mpColl_80043754`, `mpColl_80046904`,
  `mpColl_800471F8`, `mpColl_800473CC`, `mpColl_800477E0`, `mpColl_80047E14`,
  `mpColl_8004B108`, floor skip helpers, locked/desired ECB handling, floor producer/remap
  helpers, wall/ceiling producers, ledge/cliff admission, and `mpCollEnd*`.
- Current MSL owner tables and runtime call sites in `src/motion_state_owners.*`,
  `src/mpcoll_ground.c`, `src/mpcoll_wall_ceil.c`, `src/mpcoll_env.c`, `src/locomotion.c`,
  damage/attack/ledge tests, and any existing debug CollData/ECB APIs.

In-scope owner families:

- `AttackAir` collision callback routing for supported Fox/Falco aerial attacks, including landing
  handoff, platform pass-through, floor skip, endpoint/static-platform cases, wall/ceiling contact,
  hitlag/IASA boundaries where collision ownership changes, and strict negatives for non-crossing
  or wrong-floor candidates.
- `EscapeAir` collision callback routing, including ledge/cliff handoff, locked desired ECB,
  ledge-floor/root remap policy, platform/floor skip interactions, floor-loss/landing outcomes, and
  stale-seed versus live-runtime authority.
- Damage-family collision routing for `DamageAir`, `DamageFly`, `DamageFall`, hitlag continuation,
  SDI/ASDI, knockback movement, hitlag-exit landing, passive wall/ceiling/floor outcomes, and
  active-hitlag stay-airborne floor contact. Runtime-produced CollData/ECB provenance must be kept
  distinct from teacher-forced seed reconstruction.

Hard non-scope for Phase 4:

- Do not implement moving-platform runtime behavior, Randall carry, FoD dynamic endpoint speed,
  dynamic Ground callbacks, or runtime `mpBoundingCheck` `TooFar` joint mutation. Preserve existing
  behavior and keep these deferred to Phase 6.
- Do not route item/projectile, catch/throw/capture, generic common grounded/airborne owners already
  completed in Phase 3, or bespoke Fox/Falco special collision families except where they are
  directly required as negative boundaries for `AttackAir`, `EscapeAir`, or Damage.
- Do not add replay-row fixes, trace-specific fixtures as the primary proof, dataset gates,
  stage-name gameplay gates, y clamps, broad tolerance hacks, or local action-id lists when
  generated MSLMSO01 owner data can express the routing.
- Do not mark a broad family complete from one sustained/static slice. Entry, sustained,
  transition, platform, endpoint, slope, ledge, wall/ceiling, floor-loss, hitlag, locked-ECB,
  floor-skip, and hitlag-exit variants must either route through the owner or be explicitly
  deferred to a named later phase with source reasoning.

Implementation targets:

- Extend generated MSLMSO01 owner data only where the source callback identity can express a Phase 4
  owner boundary. Prefer generated table predicates over local action-id switches.
- Route in-scope `AttackAir` callbacks through source-shaped owner gates that select the Phase 2
  ordered substrate and preserve source platform-pass/floor-skip semantics.
- Route in-scope `EscapeAir` callbacks through source-shaped owner gates that select the Phase 2
  ordered substrate and preserve source ledge/cliff/locked-ECB semantics.
- Route in-scope Damage callbacks through source-shaped owner gates that select the Phase 2 ordered
  substrate and preserve source live CollData/ECB lifetime across hitlag, SDI/ASDI, and hitlag-exit
  boundaries.
- Replace or collapse old local suppress/reject/fallback paths only when the new owner state
  subsumes them. Every retained fallback must be named in the report with the exact source policy or
  later phase that owns removal.
- Keep runtime allocation-free and deterministic. No heap allocation, formatting, dynamic buffers,
  Python loops, nondeterministic iteration, or data reads on gameplay paths.

Required tests:

- Pure owner-routing tests proving generated MSLMSO01 Phase 4 classes select only the intended
  `AttackAir`, `EscapeAir`, and Damage-family callbacks and exclude Phase 3 common owners,
  item/projectile, catch/throw/capture, and bespoke special collision.
- `AttackAir` tests for hard-floor landing, platform admission/rejection, floor skip clear/update,
  endpoint/static-platform behavior, wall/ceiling contact, hitlag boundary behavior, no-crossing
  negatives, wrong floor/joint id negatives, and excluded-owner negatives.
- `EscapeAir` tests for ledge/cliff handoff, locked desired ECB, root-remap versus bottom-sweep
  boundaries, platform/floor skip interactions, stale-seed negatives, expired-provenance negatives,
  strict-span negatives, and non-ledge floors.
- Damage-family tests for active-hitlag stay-airborne contact, runtime-produced versus seed-produced
  provenance, SDI/ASDI floor/wall/ceiling outcomes, hitlag-exit landing, passive floor/wall/ceiling
  outcomes, bottom-sweep versus root-projection boundaries, wrong floor/joint id negatives, expired
  provenance negatives, and no-crossing negatives.
- Regression tests should be owner-invariant synthetic locks first. Replay/trace locks may be kept
  only as secondary evidence and must not define the runtime branch.

Required validation:

- `make build_data` if generated owner data or extraction changes
- `make build BUILD_FORCE=1`
- focused changed owner/collision tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- Clean-vs-dirty light `rollout_compare` bench. A small retained slowdown may be accepted only if
  it is tied to source-correct routing and there is no obvious duplicate work left.

Required report back:

- Exact source functions and generated owner tables audited.
- `AttackAir`, `EscapeAir`, and Damage owner families routed, with concrete MSL symbols and any
  explicitly deferred variants.
- Old suppressions/rejects/fallbacks removed, collapsed, or retained with source-policy accounting.
- Tests added/updated and what source boundary each proves.
- Validation and clean-vs-dirty perf numbers.
- Whether Phase 4 is COMPLETE by this document's criteria. If incomplete, name exact remaining
  Phase 4 work and do not move to Phase 5.
- Leave changes uncommitted for review.

Phase 4 implementation status after the class3 routing repair: COMPLETE, contingent on the
validation list above remaining clean. The phase routes the in-scope callback families through the
generated `MSLMSO01` v16 `class3_bits` owner word:

- `PHASE4_ATTACK_AIR_COLL` owns AttackAir `ft_80082C74 -> ft_80081D0C -> mpColl_800471F8`
  floor/wall/ceiling routing, transformed-platform floor-skip boundaries, and LandingAir handoff.
- `PHASE4_ESCAPE_AIR_COLL` owns EscapeAir `ft_80082C74 -> ft_80081D0C -> mpColl_800471F8`
  floor/wall/ceiling routing, ledge/cliff floor handoff, locked desired ECB lifetime, and
  LandingFallSpecial publication.
- `PHASE4_DAMAGE_COMMON_COLL`, `PHASE4_DAMAGE_FLY_COLL`, and `PHASE4_DAMAGE_FALL_COLL` own the
  Damage/DamageFly/DamageFall `ft_80081DD4` / `ft_8008370C` floor-routing families, active-hitlag
  stay-airborne floor contact, hitlag-exit landing/passive publication, and DamageFly wall/ceiling
  contact routing.

Phase 4 fallback/accounting after repair:

- Removed/collapsed broad owner gates:
  `is_attackair_action`, `action_consumes_cliff_ledge_floor_owner`,
  `attackair_platform_ecb_owner`, `damage_terminal_owner` collision helpers,
  `msl_escapeair_coll_episode_make`, the Damage active-hitlag floorhug gate, DamageFly wall-ASDI
  latch gate, and the `ft_80081D0C` wall/ceiling envelope now consume `class3_bits` for Phase 4
  families instead of broad `class_bits` or raw action ids.
- Retained non-Phase4 broad wrappers:
  `FT80081D0C_AIR_COLL`, `FT_CHECK_GROUND_LEDGE_AIR_COLL`, and bespoke Fox/Falco special collision
  peers remain as explicitly retained later-owner/non-scope wrappers. They are not evidence for
  Phase 4 completion and are covered by excluded-owner table/runtime negatives.
- Retained AttackAir publication guards:
  transformed-platform ECB-only, carried `floor_skip`, off-span hard-floor edge, transformed
  platform-from-below, and hard-slope root-without-bottom guards remain because they model source
  callback-local `CollData` floor/ECB and `mpCheckFloor` publication policy, not local replay row
  exceptions.
- Retained EscapeAir publication guards:
  same-platform/same-ledge locked owners, missing locked-bottom owner, desired-platform and
  desired-nonplatform without bottom sweep, transformed remap, strict-span, stale/expired
  provenance, KneeBend slope entry, JumpAerial high-lift/static-platform overstep, and
  cliff-horizontal ledge lock remain tied to source `CollData_X130_Locked`, ledge-floor ownership,
  and `EscapeAir_Coll` bottom-sweep boundaries.
- Retained Damage publication guards:
  active-hitlag root-below-bottom, downward-SDI stay-airborne floorhug, DamageAir -> AttackAir
  same-frame entry, DamageFlyRoll shallow hitlag, hitlag-exit bottom-vs-root boundaries, and
  transformed-platform ECB-only guards remain tied to `ftCo_Damage_OnEveryHitlag`,
  `ftCo_Damage_OnExitHitlag`, `ft_80081DD4`, and `mpColl_800477E0`/`mpColl_800473CC` source
  policy.
- Explicitly outside Phase 4 completion:
  full moving-platform runtime behavior, dynamic platform endpoint speed, dynamic Ground callbacks,
  runtime `mpBoundingCheck` `TooFar` joint mutation, separate source `left_facing_wall` /
  `right_facing_wall` `SurfaceData`, and the fully ungated DamageFly shell-wall candidate model.
  These are static/moving substrate follow-ups, not Phase 4 owner-table or callback-routing
  acceptance items.

Phase 4 runtime proof matrix:

- `tests/test_motion_state_owners_table.py` proves generated Phase 4 class3 bits match source
  collision callbacks and exclude common, item/projectile, catch/throw/capture, and bespoke-special
  owners.
- `tests/test_platform_collision_runtime.py` covers Phase 4 ordered-floor joint skip/only,
  no-crossing negatives, static wall/ceiling contact publication, transformed-platform admission,
  held-down platform non-rejection, DamageFall static platform admission, and excluded-owner
  platform-skip negatives.
- `tests/test_platform_action_entry_callback_locks.py` covers AttackAir floor-skip clear/update,
  FoD endpoint/static-platform boundaries, EscapeAir ledge/cliff handoff, locked desired ECB,
  root-remap versus bottom-sweep, platform/floor-skip interaction, strict-span/stale/expired
  provenance negatives, non-ledge-floor negatives, and AttackAir/EscapeAir replay-real entry
  boundaries.
- `tests/test_attackair_entry_mpcoll_replay_real_locks.py` and
  `tests/test_modelplay_20260507_platform_throw_air_locks.py` cover AttackAir wall contact and
  hitlag-start landing boundaries.
- `tests/test_escapeair_ledge_floor_landing_replay_real_locks.py` covers EscapeAir ledge/cliff
  handoff and stale/provenance negatives.
- `tests/test_damage_hitlag_floorhug_collision_regression.py`,
  `tests/test_damagefly_hitlag_exit_wall_reflect_replay_real_locks.py`, and
  `tests/test_damagefly_passivewalljump_replay_real_locks.py` cover Damage active-hitlag
  stay-airborne contact, runtime-vs-seed CollData/ECB provenance, SDI/ASDI floor/wall/ceiling
  outcomes, hitlag-exit landing, passive floor/wall/ceiling outcomes, bottom-vs-root boundaries,
  wrong floor/joint negatives, expired-provenance negatives, and no-crossing negatives.

### Phase 5: Static Legal-Stage Validation And Cleanup

Phase 5 is the static legal-stage closure pass. Its target is to prove that Phases 1-4 form one
coherent static collision system for supported fighter gameplay, remove obsolete bridge logic, and
leave every retained suppression/fallback with source-policy accounting. This phase should not add
new broad mechanics unless a cleanup exposes a small source-backed repair needed to make the static
system internally consistent.

Phase 5 is not complete if any static ledge/cliff floor publication path still depends on local
line-shape policy such as flat-only ledge admission, stage-specific ledge bands, or generated-slope
exceptions. For supported fighter core movement, a generated fighter-solid ledge floor is a normal
static floor candidate. If the source-shaped floor producer accepts it for the current callback,
publication must not be rejected solely because the line is sloped or generated.

Do not spawn subagents for this work unless the user explicitly authorizes it in the current turn.
The implementation agent owns the audit, cleanup, tests, validation, and perf check locally.

Required audit scope:

- Static legal-stage floor/wall/ceiling/ledge/platform collision for supported Fox/Falco fighter
  gameplay after Phases 1-4.
- Runtime collision files: `src/mpcoll_ground.c`, `src/mpcoll_wall_ceil.c`, `src/mpcoll_env.c`,
  `src/locomotion.c`, `src/fighter_callbacks.c`, `src/stage_collision.c`, CollData helpers, and
  any owner helper headers touched by Phases 1-4.
- Generated owner/data plumbing: `src/motion_state_owners.*`, MSLMSO01 extraction/readers/tests,
  MSLSTG01 static stage metadata/query paths, and debug APIs added for collision proof.
- Tests and docs that claim closure for static collision.

Required cleanup:

- Audit every `MSL_MPCOLL_REJECT_*`, `suppress_*`, `fallback`, `compat`, `bridge`, `proxy`,
  `approx`, `temporary`, and `TODO`-style collision path in the static collision files.
- For each audited path, do exactly one of:
  - delete it because Phase 1-4 source state now subsumes it;
  - convert it to the source-shaped owner/state that should have owned it;
  - retain it with nearby source-policy comments and a row in the Phase 5 accounting table.
- Delete dead helpers, dead debug APIs, stale comments, stale tests, and stale docs that describe
  old bridge behavior no longer true after Phases 1-4.
- Do not delete a source-policy guard just to reduce count. Retained guards are acceptable only when
  backed by decomp/data/source-owner reasoning and positive/negative tests.
- Do not route moving-platform runtime behavior here. Moving platforms remain Phase 6.
- Replace static ledge/cliff floor handoff rules that are expressed as local line-shape filters with
  source-produced floor-result ownership. EscapeAir/cliff-owned floor handoff must be driven by the
  same source-shaped `mpColl_80044628_Floor` / `mpColl_80044838_Floor` result used for ordinary floor
  publication, plus explicit source state such as live cliff floor id, ledge cooldown, locked ECB
  provenance, strict span, and bottom/root crossing. Do not retain flat-only ledge rules,
  stage-specific ledge bands, or generated-slope exceptions as static closure.

Phase 5 structured accounting:

Each retained floor-publication reject bit and suppression predicate below is recorded by source
owner family. A row's owner metadata applies to every exact guard name in that row. Guards are
retained only because the source callback still has that publication precondition after Phases 1-4;
none are retained as validation safety.

#### Phase 5 Owner Accounting: AttackAir

- Owner family: AttackAir.
- Source function(s): `ftCo_AttackAir_Coll`, `ft_80082C74`, `ft_80081D0C`,
  `mpColl_800471F8`, `mpColl_80044628_Floor`, `mpColl_80044838_Floor`,
  `mpUpdateFloorSkip`.
- Why retained after Phases 1-4: Phase 4 routes AttackAir through the ordered substrate, but
  LandingAir publication still depends on callback-local bottom sweep, `floor_skip`, edge-span,
  hard-slope, and transformed-platform source authority.
- Test/proof: `tests/test_platform_action_entry_callback_locks.py`,
  `tests/test_attackair_entry_mpcoll_replay_real_locks.py`,
  `tests/test_locomotion_attackair_landing_contact_y_regression.py`,
  `tests/test_platform_collision_runtime.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_ATTACKAIR_HARD_SLOPE_ROOT_WITHOUT_BOTTOM`, `MSL_MPCOLL_REJECT_ATTACKAIR_OFFSPAN_HARD_FLOOR_EDGE`, `MSL_MPCOLL_REJECT_ATTACKAIR_SINGLE_CREATE_NO_BOTTOM_OWNER`, `MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_BELOW`, `MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY`, `MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_FLOOR_SKIP` |
| Suppression | `suppress_attackair_hard_floor_root_projection_without_bottom_final_land`, `suppress_attackair_offspan_hard_floor_edge_final_land`, `suppress_attackair_offspan_hard_floor_edge_land`, `suppress_attackair_single_create_no_bottom_owner_final_land`, `suppress_attackair_transformed_platform_below_final_land`, `suppress_attackair_transformed_platform_ecb_only_final_land`, `suppress_attackair_transformed_platform_ecb_only_land`, `suppress_attackair_transformed_platform_floor_skip_final_land`, `suppress_attackair_transformed_platform_floor_skip_first_crossing_land`, `suppress_attackair_transformed_platform_root_below_land`, `suppress_downheld_transformed_platform_land`, `suppress_locomotion_attackair_entry_platform_land`, `suppress_projected_attackair_offspan_hard_floor_edge_land`, `suppress_projected_attackair_transformed_platform_below_land`, `suppress_projected_attackair_transformed_platform_ecb_only_land`, `suppress_projected_attackair_transformed_platform_floor_skip_land` |

#### Phase 5 Owner Accounting: EscapeAir / Ledge-Cliff Handoff

- Owner family: EscapeAir / ledge-cliff.
- Source function(s): `ftCo_EscapeAir_Coll`, `ft_80082C74`, `ft_80081D0C`,
  `mpColl_800471F8`, `mpColl_80044628_Floor`, `mpColl_80044838_Floor`,
  `mpColl_80046904`.
- Why retained after Phases 1-4: Phase 4 routes EscapeAir through the ordered substrate, but
  LandingFallSpecial publication still requires live locked desired ECB, ledge/platform handoff,
  bottom/root floor-producer acceptance, or allow-interrupt ownership. Static cliff-owned generated
  ledge floors are no longer filtered by flatness or generated-slope shape; the retained guards
  consume source state only: carried cliff floor id, live ledge cooldown, strict span, and the
  accepted source bottom/root floor producer. The remaining JumpAerial/EscapeAir high-lift ledge
  suppressor is accounted as a separate entry-lifetime false-publication guard; it is not the
  carried-cliff floor publication owner and is covered by adjacent replay-real negatives plus the
  generic generated-sloped carried-floor prefix positive.
- Test/proof: `tests/test_platform_action_entry_callback_locks.py`,
  `tests/test_escapeair_ledge_floor_landing_replay_real_locks.py`,
  `tests/test_platform_collision_runtime.py::test_cliff_owned_sloped_ledge_floor_prefix_lands_from_jumpaerial_escapeair`,
  and its wrong-owner/expired/off-span/no-crossing boundary companions.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_ESCAPEAIR_JUMPAERIAL_SOFT_OWNER_EARLY_DIRECT_LAND`, `MSL_MPCOLL_REJECT_ESCAPEAIR_TRANSFORMED_REMAP`, `MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_HIGH_LIFT_LEDGE`, `MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_STATIC_PLATFORM_OVERSTEP`, `MSL_MPCOLL_REJECT_KNEEBEND_ESCAPEAIR_SLOPE`, `MSL_MPCOLL_REJECT_LOCKED_DESIRED_NONPLATFORM_WITHOUT_BOTTOM_SWEEP`, `MSL_MPCOLL_REJECT_LOCKED_DESIRED_PLATFORM_WITHOUT_BOTTOM_SWEEP`, `MSL_MPCOLL_REJECT_LOCKED_ESCAPEAIR_MISSING_BOTTOM_OWNER`, `MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_LEDGE_LOCK`, `MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_PLATFORM_LOCK` |
| Suppression | `suppress_cliff_ledge_floor_shallow_projection`, `suppress_cliff_ledge_remap_without_live_owner`, `suppress_escapeair_entry_locked_platform_land`, `suppress_escapeair_jump_entry_platform_from_below`, `suppress_escapeair_jumpaerial_soft_owner_early_direct_land`, `suppress_escapeair_late_jump_entry_platform_lifetime`, `suppress_escapeair_locked_desired_bottom_above_floor_land`, `suppress_escapeair_no_lock_jumpaerial_entry_sloped_ledge_land`, `suppress_escapeair_no_lock_vertical_af3_land`, `suppress_escapeair_platform_root_snap_without_bottom_hit`, `suppress_escapeair_root_below_projection`, `suppress_escapeair_transformed_remap_land`, `suppress_fresh_jumpaerial_downheld_nonplatform_stale_land`, `suppress_jumpaerial_escapeair_entry_land`, `suppress_jumpaerial_escapeair_first_locked_static_platform_land`, `suppress_jumpaerial_escapeair_high_lift_ledge_final_land`, `suppress_jumpaerial_escapeair_static_platform_overstep_final_land`, `suppress_kneebend_escapeair_missing_ledge_owner_entry_land`, `suppress_kneebend_escapeair_missing_ledge_owner_final_land`, `suppress_ledge_endpoint_entry_projection`, `suppress_locked_desired_nonplatform_without_bottom_sweep`, `suppress_locked_desired_platform_without_bottom_sweep`, `suppress_locked_escapeair_missing_bottom_owner_land`, `suppress_locked_ledge_land`, `suppress_locked_off_end_platform_land`, `suppress_locked_seed6_platform_land`, `suppress_locked_vertical_af3_land`, `suppress_off_end_ledge_remap_projection`, `suppress_projected_cliff_floor_without_live_owner`, `suppress_projected_escapeair_ledge_without_allow_interrupt`, `suppress_projected_escapeair_missing_bottom_owner_land`, `suppress_projected_escapeair_off_end_ledge_land`, `suppress_projected_escapeair_off_end_platform_land`, `suppress_projected_escapeair_platform_root_snap_without_bottom_hit`, `suppress_projected_escapeair_transformed_platform_land`, `suppress_projected_jumpaerial_escapeair_shallow_ledge_land`, `suppress_same_platform_projection_from_below`, `suppress_seeded_escapeair_first_locked_land`, `suppress_stale_carried_cliff_ledge_land`, `suppress_sustained_escapeair_adjacent_ledge_without_allow_interrupt`, `suppress_sustained_escapeair_same_ledge_lock_land`, `suppress_sustained_escapeair_same_platform_lock_land`, `suppress_transformed_remap_projection` |

#### Phase 5 Owner Accounting: Damage

- Owner family: Damage.
- Source function(s): `ftCo_Damage_Coll`, `ftCo_Damage_OnEveryHitlag`, `ft_80081DD4`,
  `mpColl_800477E0`, `mpColl_80044628_Floor`, `mpColl_80044948_Floor`.
- Why retained after Phases 1-4: Phase 4 routes Damage/Fly/Fall through the ordered substrate, but
  active-hitlag and hitlag-exit Damage separate floor/contact publication from `GA_Ground` landing
  and must reject root-only, shallow, or stale transformed-platform candidates.
- Test/proof: `tests/test_damage_hitlag_floorhug_collision_regression.py`,
  `tests/test_damagefly_hitlag_exit_wall_reflect_replay_real_locks.py`,
  `tests/test_damagefly_passivewalljump_replay_real_locks.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_DOWNWARD_SDI_AIRBORNE`, `MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_ROOT_BELOW_BOTTOM_ABOVE_FLOOR` |
| Suppression | `suppress_active_damage_hitlag_bottom_above_floor_land`, `suppress_active_damage_hitlag_land`, `suppress_damage_transformed_platform_ecb_only_land`, `suppress_damageair_attackair_entry_land`, `suppress_damageflyroll_below_floor_active_hitlag_land`, `suppress_damageflyroll_hitlag_exit_floor_land`, `suppress_damageflyroll_shallow_land` |

#### Phase 5 Owner Accounting: Fall / FallSpecial

- Owner family: Fall / FallSpecial.
- Source function(s): `ftCo_Fall_Coll`, FallSpecial collision callback, `ft_800831CC`,
  `ft_80082B1C`, `mpColl_800471F8`, `mpColl_80044628_Floor`.
- Why retained after Phases 1-4: Phase 3 routes common airborne owners, but Fall/FallSpecial
  platform, same-floor, first-sustained, and destination-action publication still depend on the
  current callback's bottom sweep. The obsolete fresh hard-floor same-floor guard was deleted.
- Test/proof: `tests/test_platform_collision_runtime.py`,
  `tests/test_locomotion_attackair_landing_contact_y_regression.py`,
  `tests/test_nonfd_action_entry_platform_pass_locks.py`,
  `tests/test_shield_contact_seed_owner_replay_real_locks.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_FALLSPECIAL_FIRST_SUSTAINED`, `MSL_MPCOLL_REJECT_FALLSPECIAL_PLATFORM_NO_SOURCE_BOTTOM`, `MSL_MPCOLL_REJECT_FALL_SAME_FLOOR_EARLY`, `MSL_MPCOLL_REJECT_FALL_SHALLOW_TERMINAL_HARD_FLOOR`, `MSL_MPCOLL_REJECT_FALL_STALE_PLATFORM_FIRST_HARD_FLOOR`, `MSL_MPCOLL_REJECT_FALL_TRANSFORMED_PLATFORM_FASTFALL` |
| Suppression | `suppress_fall_ledge_floor_first_root_crossing`, `suppress_fall_same_floor_early_final_land`, `suppress_fall_shallow_terminal_hard_floor_land`, `suppress_fall_stale_platform_first_hard_floor_land`, `suppress_fall_transformed_platform_fastfall_land`, `suppress_fallspecial_b_transformed_platform_skip`, `suppress_fallspecial_entry_af3_land`, `suppress_fallspecial_first_sustained_current_ecb_land`, `suppress_fallspecial_platform_final_without_source_bottom`, `suppress_fallspecial_platform_first_root_crossing`, `suppress_fallspecial_same_floor_early_root_crossing`, `suppress_projected_fallspecial_first_sustained_land` |

#### Phase 5 Owner Accounting: JumpAerial / Common Air

- Owner family: JumpAerial / common air.
- Source function(s): `ftCo_JumpAerial_Coll`, `ftCo_Jump_Coll`, `ft_800835B0`,
  `ft_80082B1C`, `mpColl_80046904`, `mpColl_800471F8`.
- Why retained after Phases 1-4: Phase 3 routes common air through the ordered substrate, but
  soft-platform-from-below, transformed-platform fastfall, pre-handoff, and shallow ledge contacts
  still require current bottom/root owner and wall/cliff handoff state.
- Test/proof: `tests/test_platform_action_entry_callback_locks.py`,
  `tests/test_nonfd_action_entry_platform_pass_locks.py`,
  `tests/test_platform_collision_runtime.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_AIRBORNE_TRANSFORMED_PLATFORM_PRE_HANDOFF`, `MSL_MPCOLL_REJECT_JUMPAERIAL_STATIC_PLATFORM_FROM_BELOW`, `MSL_MPCOLL_REJECT_JUMPAERIAL_TRANSFORMED_PLATFORM_FASTFALL` |
| Suppression | `suppress_airborne_transformed_platform_pre_handoff_land`, `suppress_jumpaerial_entry_shallow_ledge_projection`, `suppress_jumpaerial_static_platform_from_below_final_land`, `suppress_jumpaerial_transformed_platform_fastfall_land` |

#### Phase 5 Owner Accounting: SpecialHi

- Owner family: SpecialHi.
- Source function(s): `ftFx_SpecialAirHi_Coll`, `ftFx_SpecialHiFall_Coll`,
  `ftFox_SpecialHi_IsBound`, `ftCo_8009A134`, `mpColl_800471F8`, `mpUpdateFloorSkip`.
- Why retained after Phases 1-4: SpecialHi floor publication remains character-callback owned:
  platform pass, floor-angle bound, JObj ECB, transformed-platform, and under-stage floor-side
  policy cannot be replaced by generic floor publication.
- Test/proof: `tests/test_specialhi_mpcoll_ecb_replay_real_locks.py`,
  `tests/test_special_cliffcatch_collision_window_regression.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_SPECIALAIRHI_FLOOR_ANGLE`, `MSL_MPCOLL_REJECT_SPECIALAIRHI_PLATFORM`, `MSL_MPCOLL_REJECT_SPECIALHI_FROM_BELOW_HARD_FLOOR`, `MSL_MPCOLL_REJECT_SPECIALHI_TRANSFORMED_PLATFORM`, `MSL_MPCOLL_REJECT_SPECIALHI_UNDERSTAGE_HARD_FLOOR` |
| Suppression | `suppress_projected_specialairhi_floor_angle_land`, `suppress_projected_specialhi_from_below_hard_floor_clip`, `suppress_projected_specialhi_transformed_platform_land`, `suppress_projected_specialhi_understage_hard_floor_clip`, `suppress_specialairhi_floor_angle_land`, `suppress_specialairhi_platform_land`, `suppress_specialhi_from_below_hard_floor_land`, `suppress_specialhi_transformed_platform_land`, `suppress_specialhi_understage_hard_floor_land` |

#### Phase 5 Owner Accounting: SpecialAirLw

- Owner family: SpecialAirLw.
- Source function(s): `ftFx_SpecialAirLwStart_Coll`, `mpColl_800471F8`,
  `mpColl_80044628_Floor`.
- Why retained after Phases 1-4: aerial Shine startup must not publish a stale previous platform
  before current callback ECB-bottom acceptance.
- Test/proof: `tests/test_platform_action_entry_callback_locks.py`,
  `tests/test_special_cliffcatch_collision_window_regression.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_SPECIALAIRLW_START_STALE_PLATFORM` |
| Suppression | `suppress_specialairlw_start_stale_platform_land` |

#### Phase 5 Owner Accounting: Ledge / Cliff

- Owner family: Ledge / cliff.
- Source function(s): `ftCliffCommon_*`, `ft_80082F28`, `mpColl_80046904`,
  `mpColl_80044628_Floor`.
- Why retained after Phases 1-4: ledge/cliff floor contact and locked cliff-floor publication are
  consumed by the cliff owner before normal floor publication. The legacy reject bit name remains
  internal, but the live suppressions are generic to carried static ledge floors and do not branch
  on stage id, ledge band, flatness, or generated-slope shape.
- Test/proof: `tests/test_escapeair_ledge_floor_landing_replay_real_locks.py`,
  `tests/test_special_cliffcatch_collision_window_regression.py`,
  `tests/test_platform_action_entry_callback_locks.py`.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_CLIFF_HORIZONTAL_LEDGE_LOCKED` |
| Suppression | `suppress_cliff_ledge_locked_final_land`, `suppress_cliff_ledge_locked_zero_bottom_hit` |

#### Phase 5 Owner Accounting: Moving-Platform Deferral

- Owner family: moving-platform deferral.
- Source function(s): `ftCo_Landing_Coll`, `ft_80084280`, `ft_800844EC`,
  `mpColl_8004B4B0`, `grIzumi_801CC358`, generated `stage_collision_floor_line_*` metadata.
- Why retained after Phases 1-4: transformed/moving platform current-source or scheduler authority
  is required before replacing carried floors. Phase 5 records the deferral and does not route
  moving-platform runtime behavior.
- Test/proof: `tests/test_platform_action_entry_callback_locks.py`,
  `tests/test_platform_collision_runtime.py`, Phase 6 deferral row.

| Kind | Exact guard name(s) |
| --- | --- |
| Reject bit | `MSL_MPCOLL_REJECT_FALL_LOOP_WRAP_STAGE_OBJECT_FLOOR_TO_HARD_FLOOR` |
| Suppression | `suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land`, `suppress_large_distinct_platform_root_projection` |

#### Phase 5 Fallback / Keyword Accounting

| Audited path | Phase 5 action | Source-policy owner / proof |
| --- | --- | --- |
| `msl_mpcoll_80044628_floor_wall_adjacent_fallback` and adjacent wall/ceiling fallback comments | RETAINED SOURCE-POLICY | `mpColl_80044628_Floor` and `mpColl_8004ACE4` retry through connected floor/wall graph state after ordered wall/ceiling side bits. The path is bounded to source side bits, endpoint tolerance, and static graph lines. |
| `msl_mpcoll_80044838_floor_edge_snap_from_bottom` hard-floor off-end rejection | RETAINED SOURCE-POLICY | This helper owns platform endpoint admission only. Hard-floor off-end rows stay on ordinary bottom-sweep/direct publication because their source edge snap needs current mpColl scratch floor ownership, not this platform endpoint helper. |
| `mpcoll_action_uses_retained_ft80081d0c_air_collision` | RETAINED LATER-OWNER FALLBACK | AttackAir and EscapeAir route through Phase 4 `class3_bits`; the retained broad `FT80081D0C_AIR_COLL` wrapper is only for later/non-Phase4 owners sharing `ft_80082C74 -> ft_80081D0C -> mpColl_800471F8`. It is not accepted as Phase 4 or Phase 5 static closure evidence. |
| Static ledge-grab ECB and prev/cur sampling in `src/mpcoll_env.c` | CONVERTED COMMENT / RETAINED SOURCE-STATE | Stale approximation wording was replaced with explicit MSL state lanes: ISO ECB-table samples and within-frame collision-stage prev/cur positions used by `mpColl_80044164`, `mpColl_800443C4`, and `mpColl_80046904`. |
| Landing-contact root-Y helper and generic floor-loss Fall branch in `src/locomotion.c` | CONVERTED COMMENT / RETAINED SOURCE-OWNER | Old bridge/unmodeled wording was replaced. Landing root-Y is bounded to callbacks that already published floor contact; explicit ledge-slip owners enter MissFoot before the generic `ftCo_Fall_Enter` branch. |
| Seed-only FoD platform height restore in `src/mpcoll_ground.c` | RETAINED SOURCE-AUTHORITY GUARD | Rows with only seed-provided FoD platform height and no live scheduler/contact source restore carried floor. Phase 6 keeps this as the source-shaped stale-sparse-height rejection boundary while live grIzumi scheduler/contact state enters through the moving-surface packet. |
| Remaining `fallback`, `bridge`, `compat`, and `temporary` wording in `src/locomotion.c`, `src/fighter_callbacks.c`, and `src/damage_terminal_owner.h` | RETAINED NON-STATIC OWNER ACCOUNTING | Remaining occurrences are random idle selection, throw/combat/IASA/match-flow/BODY, or one-step seed ownership comments outside static legal-stage collision publication. They are not accepted as static collision closure evidence and keep nearby decomp/source comments. |
| `stage_collision_stage_has_deferred_static_floor_transform` and `deferred_*` stage metadata helpers | RETAINED STATIC-QUERY CLASSIFIER | These classify floor transform metadata for static query exclusion and tests. After Phase 6, transformed lines are still deferred from `stage_collision_static_query`, but live FoD/Randall runtime behavior is routed through the moving-surface packet. |

Required verification:

- Focused owner-invariant tests for every cleanup that changes behavior.
- Ledge/cliff owner-invariant tests proving:
  - generated sloped ledge floors are accepted when the source bottom sweep accepts the carried
    cliff-owned floor;
  - a compact Fall -> JumpAerial -> EscapeAir prefix can carry the generated sloped ledge floor
    through live ledge cooldown and source floor-producer authority;
  - flat ledge floors are accepted through the same owner path;
  - wrong carried cliff floor id is rejected;
  - expired ledge/cliff cooldown is rejected;
  - off-span candidates are rejected;
  - no-crossing candidates are rejected;
  - non-ledge floors are rejected for the cliff-owned handoff path;
  - the implementation does not branch on stage id, trace name, dataset, or a one-off ledge band.
- Tests or static checks proving retained suppressions/fallbacks are accounted for in this plan doc
  or `agent_docs/SPEC.md`.
- Static checks or focused tests proving stale collision comments do not claim unsupported
  approximation/proxy status for completed static owners.
- Runtime allocation/nondeterminism audit for the collision paths touched by Phases 1-4. This can
  be source inspection plus targeted grep/static checks; do not add heavy tooling unless necessary.
- `make build_data` if generated data or extraction changed.
- `make build BUILD_FORCE=1`
- focused changed collision/owner tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- Clean-vs-dirty light `rollout_compare` bench.

Required report back:

- List deleted, converted, and retained collision suppressions/fallbacks.
- Explain every retained static collision guard with source-policy owner and test/proof.
- State whether any behavior changed during cleanup and why.
- State whether any dirty validation reports remain and why.
- State clean-vs-dirty perf delta and whether any retained cost is expected.
- Whether Phase 5 is COMPLETE by this document's criteria. If incomplete, name exact remaining
  Phase 5 work and do not move to Phase 6.
- Leave changes uncommitted for review.

### Phase 6: Moving Platforms / Ground-Object Collision

Phase 6 starts from the committed static-collision source-port state. Do not revisit static
EscapeAir/ledge-cliff ownership unless a moving-platform path directly shares that owner. Do not
spawn subagents unless the user explicitly authorizes it in the current turn.

Phase 6 target:

- Close common fighter collision ownership for legal-stage moving platform surfaces:
  - Fountain of Dreams `grIzumi` side platforms.
  - Yoshi's Story Randall.
  - Any generated legal-stage moving/platform support line that is already represented in
    `MSLSTG01` and consumed by common fighter `mpColl`/`mpLib` wrappers.
- Port dynamic line world-position, activation/visibility, speed, support, and final publication
  semantics so fighter collision consumes the same moving surface the source stage/Ground owner
  publishes for that frame.
- Remove or convert seed-only and compatibility fallbacks that were retained as Phase 6 deferrals
  during static phases.

Source owners to audit and cite near gameplay code:

- `refs/melee/src/melee/gr/grizumi.c` and related FoD Ground callbacks for platform height,
  target/hidden state, JObj transform, activation, and scheduler timing.
- `refs/melee/src/melee/gr/grstory.c`, `refs/melee/src/melee/gr/groldyoshi.c`, and related Yoshi
  Ground callbacks for Randall path clock, support state, visibility, and line publication.
- `refs/melee/src/melee/gr/grlib.c`, `refs/melee/src/melee/gr/stage.c`, and `Ground`/dynamic attr
  helpers such as `grDynamicAttr_801CA284` where they feed collision line transforms or speed.
- `refs/melee/src/melee/mp/mplib.c` dynamic/remap/speed helpers consumed by common fighter
  collision, especially `mpCollGetSpeedFloor`, `mpCollGetSpeedCeiling`,
  `mpCollGetSpeedLeftWall`, `mpCollGetSpeedRightWall`, `mpColl_IsOnPlatform`, remap helpers, and
  any dynamic-line query entry point used by the supported wrappers.
- `refs/melee/src/melee/mp/mpcoll.c` finalizer and support/carry paths that read moving floor,
  wall, or ceiling surfaces after a query result is accepted.

Required implementation shape:

- Add or complete one stage-collision runtime packet for moving surface state. It should expose
  world endpoints, normal, segment id, joint id, platform/support kind, current visibility/active
  state, source clock/state, and surface velocity. It must be allocation-free and stable across
  batch lanes.
- Static query helpers must continue to exclude deferred moving lines. Moving lines are admitted
  only through the moving-surface packet/source owner, not by broadening static geometry scans.
- Fighter `mpColl` code should consume moving surfaces through the same floor/wall/ceiling result
  packets used by static collision. Do not add row-local FoD/Randall branches in fighter code when
  a stage packet or generated `MSLSTG01` field can express the distinction.
- Stage-id use is allowed only to select generated stage metadata or stage-owned runtime state. It
  is not allowed as a gameplay shortcut such as "if FoD then accept this floor" or "if Yoshi then
  clamp here."
- Live runtime platform state must be separate from teacher-forced seed reconstruction. Seed lanes
  may initialize hidden source state, but they must not create live platform/contact authority in
  free-running gameplay without the source scheduler/contact owner.
- Surface speed/carry must be source-owned. Standing, landing, damage floorhug, aerial landing,
  platform pass-through, and current-floor continuation should consume the platform velocity only
  when source would read the moving surface as current support/contact.
- Runtime gameplay paths must stay heap-allocation-free.

Phase 6 is not complete if any of these remain true:

- `src/` has a moving-platform collision branch whose behavior is keyed on replay row, dataset,
  trace name, hardcoded record, or undocumented magic coordinate.
- FoD/Randall behavior is still routed through seed-only height/path restoration when live runtime
  stage state should own the surface.
- Static helpers admit moving/deferred lines as a shortcut.
- Dynamic platform speed/carry is missing or only exposed as debug/viewer state while fighter
  collision uses a different surface.
- A retained fallback is justified only as "validation safety" instead of a source owner, explicit
  Phase 6 out-of-scope boundary, or non-runtime forensic tool.

Required tests:

- Positive and negative synthetic owner tests for each moving platform family:
  - FoD platform visible/hidden or active/inactive state.
  - FoD rising/falling/current-target state and surface velocity.
  - Randall path positions, support active/inactive state, and raw/public ground-id mapping.
  - landing from air onto moving platform;
  - grounded carry while standing on moving platform;
  - platform pass-through / floor skip;
  - edge/endpoint contact;
  - wall/ceiling adjacency if the moving surface can produce those contacts;
  - wrong/stale seed state does not create live contact.
- Regression tests for existing static Phase 1-5 invariants that are most likely to be disturbed:
  static hard floor, static ledge/cliff EscapeAir, static platform pass-through, static wall/ceiling
  ordered collision, and Damage hitlag floor contact.
- At least one rollout/prefix test per moving-platform family that starts before the terminal
  contact/carry row and proves the live stage scheduler state is consumed.

Required validation:

- `make build_data`
- `make build BUILD_FORCE=1`
- focused moving-platform/static-regression tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- clean-vs-dirty `rollout_compare` benchmark with the same command used in recent phases

Acceptance:

- No hard or unclassified validation reds. Distribution-only movement may be accepted only if suite
  totals are neutral/improved and the report explicitly explains the retained movement.
- No meaningful performance regression without a source-correctness reason and a clear later
  optimization target.
- Phase 6 report names deleted, converted, retained, and deferred moving-platform fallbacks.
- Static Phase 1-5 tests and validation remain clean.
- Final status is left unstaged/uncommitted for review.

#### Phase 6 Completion Report

Status: COMPLETE by this document's criteria.

Converted:

- FoD and Randall generated floor transforms now route through
  `MslStageMovingSurfaceState` / `stage_collision_floor_line_moving_surface_state`, which exposes
  world endpoints, normal, segment id, joint id, support/transform kind, visibility/active state,
  source clock/state, and surface velocity.
- `stage_collision_floor_line_world`, `stage_collision_floor_line_motion_delta`, debug
  moving-surface inspection, runtime carry, and debug `mpCollGetSpeedFloor` state consume that same
  packet path rather than separate FoD/Randall endpoint logic.
- Previous Phase-6 comments for seed-only FoD platform height restore and
  `stage_collision_stage_has_deferred_static_floor_transform` were reclassified as source-authority
  guard/static-query classifier wording, not unfinished moving-platform runtime work.

Retained:

- `stage_collision_static_query` still excludes height-transformed FoD and Randall path lines.
  Moving floors are admitted only through the runtime moving-surface packet/source owner.
- Sparse seed-only FoD platform heights without live scheduler, same-step contact, or current
  source bits still restore carried floor in sustained Landing-style paths. This is retained as a
  stale-provenance rejection guard, not a validation-safety fallback.
- Wall/ceiling speed helpers remain static because supported legal-stage MSLSTG01 data does not
  generate moving wall or ceiling surfaces; moving floor speed is handled by the packet.

Deleted:

- No obsolete moving-platform branch was deleted in this closure pass; existing behavior was already
  mostly source-shaped and was consolidated behind the shared packet.

Deferred:

- Runtime `mpBoundingCheck` `TooFar` joint mutation remains outside this moving floor/platform
  closure. No FoD/Randall moving-floor publication or support/carry fallback remains deferred.

Validation:

- `make build_data`
- `make build BUILD_FORCE=1`
- focused FoD/Randall/moving-platform/static-regression tests
- `make validate-all`
- `MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
  reported no suite total changes, no replay regressions, no hard reds, no distribution-only reds,
  and no unclassified regressions.
- Clean-vs-dirty light `rollout_compare` bench, same command/env
  (`MSL_DATA_DIR=/mnt/nvme0/projects/melee-sim-light/data make bench-sim
  ARGS='--mode rollout_compare --batch 512 --frames 5000'`): clean HEAD `2714.505 ns/env_step`,
  dirty Phase 6 packet `2722.674 ns/env_step`, same checksum `920057968263732859`, retained
  +0.30% ns/env.

## Dirty Tree Recommendation

For the future implementation pass, do not build directly on the current dirty tree without review. Separate changes by category:

- Potentially salvageable: generalized `CollData` substrate, `mpColl` wall/ceiling/floor helpers, static stage metadata extraction/loading, generated owner-table extensions, allocation-safe C helpers, and synthetic tests that assert source-level invariants rather than replay-row outcomes.
- Potentially salvageable with proof: `DATA_CONTRACT.md`/`SPEC.md` edits, validation tooling changes, and extraction artifacts if they are regenerated from source and pass fresh-extract smoke tests.
- Should be stashed aside or discarded before the source-port pass: refreshed validation reports not tied to accepted core logic, historical `agent_docs/systems/` planning files, replay-row lock tests that encode a specific observed case instead of an owner invariant, temporary bridges/suppressions/fallbacks, dataset/preprocess tweaks tied to trace selection, and local action-id lists that can be replaced by generated owner tables.
- Must not be carried forward unreviewed: runtime behavior that keys on dataset names, record identifiers, debug artifacts, specific observed cases, or character id as a collision-state proxy.

The safest path is to start the implementation from a clean branch, cherry-pick only source-proven substrate pieces after review, then re-run extraction, tests, validation, and allocation checks from that branch.
