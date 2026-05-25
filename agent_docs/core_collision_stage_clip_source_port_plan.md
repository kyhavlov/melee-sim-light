# Core Collision Stage-Clip Source-Port Plan

## Purpose

Define the source-port scope needed to make catastrophic stage clips from core fighter mechanics structurally impossible for supported Fox/Falco legal-stage gameplay, except for explicitly deferred character-special or moving-platform cases. The implementation target is not a replay-specific patch. It is a source-backed collision ownership pass that closes the common fighter and `mpLib`/`mpColl` families used by grounded, airborne, aerial attack, air dodge, damage, knockback, wall, and ceiling gameplay on legal stages.

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

Moving platforms are in scope only after the static legal-stage core is source-covered and validated. This phase covers platform transforms and dynamic line updates for supported legal-stage gameplay, especially Randall and Fountain of Dreams platform motion. It must not distract or block the first static-collision phases.

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

- Route common grounded movement through the source-backed ground collision wrappers.
- Route common airborne movement through the source-backed air collision wrappers.
- Prove walk-off, landing, ledge exclusion/inclusion where applicable, wall/ceiling contact, floor skip, and joint skip behavior with synthetic tests.
- Do not include moving platforms yet except preserving data structures that will support them later.
- Do not accept slice-only completion. Entry, sustained, transition, endpoint, platform, slope, ledge, and wall/ceiling variants must either route through the new owner or be explicitly deferred to a later named phase.

### Phase 4: AttackAir, EscapeAir, And Damage Families

- Port `AttackAir` and `EscapeAir` collision wrapper choice, landing handoff, ECB lock/unlock, and fall-special behavior.
- Port damage/hitlag exit/SDI/ASDI/knockback collision integration, including passive wall/ceiling/floor outcomes.
- Keep common wrappers authoritative; do not add downstream action exceptions for individual aerial or damage states.
- Do not mark complete until both positive and negative tests prove source-owned collision routing.
- Tests must include live-vs-seeded provenance boundaries, bottom-sweep versus root-projection boundaries, wrong floor/joint id negatives, expired provenance negatives, strict-span negatives, and no-crossing negatives.

### Phase 5: Static Legal-Stage Validation And Cleanup

- Run full validation and refresh required reports if core logic changed.
- Audit for leftover bridge/suppress/fallback paths and either remove or account for each.
- Audit for runtime allocations and nondeterministic iteration.
- Update `agent_docs/SPEC.md` only with stable, source-backed mechanics learned during implementation.
- Do not ship as complete if the pass only adds substrate without routing the in-scope owners through it.
- A validation-clean pass with hundreds of old suppressions still active is not complete. The cleanup must reduce or justify the existing compensating collision paths.

### Phase 6: Moving Platforms Last

- Only begin after static-stage source coverage, tests, validation, allocation checks, and cleanup are complete.
- Port dynamic joint transform, bounds, line position, speed, visibility/activation, and platform-specific stage owners for Randall and Fountain of Dreams platforms.
- Add focused moving-platform tests and rerun the full static suite to prove no static collision drift.
- Keep this phase independently revertible from static collision work.

## Dirty Tree Recommendation

For the future implementation pass, do not build directly on the current dirty tree without review. Separate changes by category:

- Potentially salvageable: generalized `CollData` substrate, `mpColl` wall/ceiling/floor helpers, static stage metadata extraction/loading, generated owner-table extensions, allocation-safe C helpers, and synthetic tests that assert source-level invariants rather than replay-row outcomes.
- Potentially salvageable with proof: `DATA_CONTRACT.md`/`SPEC.md` edits, validation tooling changes, and extraction artifacts if they are regenerated from source and pass fresh-extract smoke tests.
- Should be stashed aside or discarded before the source-port pass: refreshed validation reports not tied to accepted core logic, historical `agent_docs/systems/` planning files, replay-row lock tests that encode a specific observed case instead of an owner invariant, temporary bridges/suppressions/fallbacks, dataset/preprocess tweaks tied to trace selection, and local action-id lists that can be replaced by generated owner tables.
- Must not be carried forward unreviewed: runtime behavior that keys on dataset names, record identifiers, debug artifacts, specific observed cases, or character id as a collision-state proxy.

The safest path is to start the implementation from a clean branch, cherry-pick only source-proven substrate pieces after review, then re-run extraction, tests, validation, and allocation checks from that branch.
