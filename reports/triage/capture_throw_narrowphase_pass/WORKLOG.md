# Capture / Throw Narrowphase Pass Worklog

## Scope

Target owner family: grab/catch collision classification and capture-state transitions around `Catch`, `CatchPull`, `CapturePulled*`, and adjacent `Passive` rollout clusters.

This is a checkpoint, not full owner-family closure. The pass promoted source-backed catch wall obstruction and a capture-root floor owner, and it rejected several row-shaped bridges that did not survive suite validation.

## Source Map

- `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C`
  - Catch acquisition keeps one nearest victim by X distance.
  - It rejects teams / skip states / target masks / invulnerable victims before catch connect.
  - It selects grabbable hurt capsules, not shield rim or BODY-only capsules.
  - It calls `ft_80084CE4` for wall obstruction after capsule contact.
- `refs/melee/src/melee/ft/ft_081B.c::ft_80084CE4`
  - Builds attacker/victim ECB midpoints and calls `mpCheckRightWall` when attacker X is greater than victim X, else `mpCheckLeftWall`.
- `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC`
  - Confirms catch contact is HitCapsule vs grabbable hurt capsule using the collision skeleton path.
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,ftCo_CapturePulledHi_Coll,fn_800DAECC,fn_800DAEEC}`
  - Catch connect enters `CapturePulledHi/Lw` from callback-target ground state, then can immediately call the victim collision callback.
  - `CapturePulledHi_Coll -> ft_80083C00 -> mpColl_800477E0` can hand off to low capture when the floor mask owner succeeds.
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll`
  - DamageFly collision owns stale floor ids before capture entry; those ids must not force low capture.
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c` and `ftCo_Thrown.c`
  - Reviewed for attachment/release setup. Existing shared attachment substrate still covers the currently implemented throw entry/release owners.

## Implemented Owners

- Catch wall obstruction:
  - Added source-shaped `ft_80084CE4` gate in `src/combat.c`.
  - Uses extracted MSLSTG01 left/right wall graphs and fighter-solid line metadata.
  - Uses attacker/victim ECB midpoint Y from generated ECB extents plus live CollData bottom where valid.
  - No dataset, row, stage-line allowlist, character proxy, heap allocation, or replay-future lane.
- Capture-root immediate floor callback:
  - Broadened `CapturePulledHi` same-frame low handoff to consume a locked CollData floor id when `ground_id` is valid and `ecb_lock_timer` is live.
  - DamageFly-family pre/prev/seed-prev actions are explicitly excluded because the floor id belongs to damage collision ownership.
  - Retained the older `KneeBend -> AttackAirHi` missing-floor-index bridge as a documented source episode until CollData seed provenance is promoted.
- Capture entry selection:
  - Kept `fn_800DAADC` mapping through `on_ground` for source `xE0`, but excluded DamageFly-family stale grounded results so airborne DamageFly victims enter `CapturePulledHi`.
- CaptureWait jump latch:
  - `fn_800DC014` gates the XY jump latch on the input edge lane (`fp->input.x668`), so runtime now consumes `input_buttons_pressed` for that narrow IASA latch.
  - This is separate from the first-steady CaptureWait ownership bridge.
- Locks:
  - Added replay-real positive and negative locks in `tests/test_capture_lw_floor_projection_replay_real_locks.py`.
  - Positive: FD/cardinal `AttachedGoodNaturedGuanaco.msl` record 3204 enters `CapturePulledLw` from same-frame locked floor owner.
  - Negative: FD/cardinal `TreasuredBackKangaroo.msl` record 5861 remains `CapturePulledHi` from DamageFly ownership.
  - Existing catch/combat/throw focused locks cover catch skeleton scale, GuardReflect grabbability, dash-grab, attachment, and throw substrates.

## Rejected Bridge Attempts

- Replacing the broader CaptureWait first-steady prev-button carry with a pressed-edge-only rule:
  - Source-shaped locally but regressed primary F03 from 20 to 36.
  - Rejected because the current seed surface still needs the existing prev-button carry bridge. The retained edge-lane change is limited to `fn_800DC014`'s IASA jump latch.
- Broad stale-ground / unlocked-floor capture handoff:
  - Moved individual rows but reopened DamageFly-owned floor ids.
  - Rejected in favor of requiring live `ecb_lock_timer` with `ground_id`.
- Broad DamageFly floor handoff:
  - Regressed DamageFly-owned capture entry rows.
  - Rejected; DamageFly floor state remains a separate damage-collision owner.
- Removing the ShieldDesc-center catch bridge:
  - Reopened known missing-pose catch rows.
  - Retained as an explicit missing-pose bridge, not promoted as the source owner.

## Passive Cluster Audit

`tools.eval.next_desync_investigation` packet:

- `reports/triage/capture_throw_narrowphase_pass/baseline_next_desync/top_packet.md`
- Dataset: `datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl`
- Record: 5976 p0, rollout first mismatch offset 30, compare record 6005.
- Current row starts from `Passive` with `ftCo_Passive_*` callbacks and no one-step diff.

Classification: rollout-only `Passive` / locomotion adjacency, not a capture-row patch boundary. No capture-specific runtime branch was added for it.

## Metrics

Primary one-step taxonomy:

- Before: total mismatches 204, `F03_capturewait_bridge` 20.
- After: total mismatches 200, `F03_capturewait_bridge` 16.
- No new primary F03 rows; the removed rows are the `AttachedGoodNaturedGuanaco.msl` record 3204 field set (`action_id`, `animation_index`, `jumps_left`, `on_ground`).

Aggregate taxonomy after this pass:

- Total mismatches 7866.
- `F03_capturewait_bridge` 116.

`validation_report_diff --before HEAD --after reports/validation --top 180`:

- Primary one-step discrete: 118 -> 114.
- Primary strict discrete: 204 -> 200.
- Aggregate one-step discrete: 6548 -> 6524.
- Aggregate strict discrete: 7920 -> 7896.
- Aggregate rollout first-mismatch seeded: 848 -> 842.
- Aggregate rollout first-mismatch total: 1414 -> 1409.
- Aggregate rollout streak count: 1247 -> 1242.
- Distribution-only regression: primary rollout median streak 351 -> 346, concentrated in `AttachedGoodNaturedGuanaco.msl` median 341 -> 314 while first-mismatch seeded count improves from 2 -> 1.

AGG rollout median autopsy:

- Details: `reports/triage/capture_throw_narrowphase_pass/agg_rollout_median_regression.md`.
- Before windows: `[2694,3204)=510`, seeded break at `3204` (`action_id` p0 `366/223/226`), then `[3205,3759)=554`.
- After windows: `[2694,3468)=774`, break at `3468` (`hitlag` p0 `0/6/7`), then `[3468,3759)=291`.
- Current-code controls prove this is not a local capture regression:
  - Seeding at `3204` reaches `3468` exact and first breaks at `3759`.
  - Seeding at `3205` reaches `3468` exact and first breaks at `3759`.
  - Seeding at `3468` is exact.
- AGG one-step improves (`discrete 15 -> 11`, `strict 34 -> 30`) and AGG seeded rollout first-mismatch count improves (`2 -> 1`).
- Classification: harmless distribution shift. Removing the true `3204` capture action-id seeded break exposes a later hitlag/contact timing residual during a longer continuous rollout from `2694`; it does not introduce a new one-step or local catch/capture regression.

## Validation

- `make build`
- `uv run pytest -q tests/test_catch_collision_skeleton_scale_replay_real_locks.py tests/test_guardreflect_grabbable_catch_replay_real_locks.py tests/test_grab_dash_slice4_regression.py`
- `uv run pytest -q tests/test_capture_delta_applied_once.py tests/test_grab_capturepulled_entry_velocity_lock_regression.py tests/test_capture_lw_floor_projection_replay_real_locks.py tests/test_capturewait_grab_mash_replay_real_locks.py tests/test_grab_attachment_throw_entry_position_regression.py tests/test_grab_attachment_thrown_posy_regression.py tests/test_throw_common_substrate_acceptance_replay_real_locks.py tests/test_throw_ground_root_motion_replay_real_locks.py tests/test_grab_throw_final_sweep_regression.py`
- `uv run pytest -q tests/test_capture_lw_floor_projection_replay_real_locks.py`
- `uv run pytest -q tests/test_catch_collision_skeleton_scale_replay_real_locks.py tests/test_capture_lw_floor_projection_replay_real_locks.py`
- `make test` (`1673 passed, 1183 skipped`)
- `make validate-all`
- `make fmt`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 180`
- `uv run python -m tools.slippi.preprocess_suite --suite replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets --force`
- `uv run python -m tools.slippi.preprocess_suite --suite replays/suites/aggregate_recent.json --datasets-dir datasets --force`

`make build_data` was not run: no extraction or generated data contract changed.

## Residuals

- Remaining primary `F03_capturewait_bridge`: 16.
- Remaining aggregate `F03_capturewait_bridge`: 116.
- Capture/throw family is not closed.
- Source-named residual buckets:
  - `CollData/mpColl_800477E0` provenance: missing seeded CollData floor index for the older `KneeBend -> AttackAirHi` same-frame capture-root floor episode.
  - `ShieldDesc` / guard-family pose substrate: missing catch fallback pose data for no-submotion guard-family rows.
  - `ftCo_CaptureWaitHi/Lw` first-steady ownership: prev-button carry remains a bridge pending a better source seed surface for first-steady CaptureWait continuity.
  - `ftCo_Passive_*` / locomotion rollout ownership: PASSIVE-adjacent rollout cluster is not catch narrowphase debt.

## Current Status

Checkpoint. The source-backed wall gate is in place, and one material capture-state owner moved primary and aggregate reports without one-step regressions. The capture/throw family is not closed while the source-named residual bridges above remain.
