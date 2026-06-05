# Scheduler System Source Completion

Scope: frame and callback ordering for supported RL 1.0 gameplay. This covers the public
`step_input` frame spine, fighter GObj callback order, MotionState callback dispatch, transient
state lifetime, and scheduler-owned RNG clocks where phase order affects fighter gameplay.

Out of scope for this closure level: camera-only callbacks, full item lifecycle behavior that does
not affect fighter scheduling, cosmetic stage/effect callbacks, and detailed projectile/item
mechanics covered by `items_core.md` and `projectiles_reflect.md`.

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only. This
document has no BLOCKED rows for the stated scope.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| Public frame spine | CLOSED | `msl_batch_step_input` delegates to one ordered `fighter_callbacks_step_frame` spine, then advances rollout RNG clock ownership. |
| Fighter GObj callback order | CLOSED | Runtime phases mirror the supported gameplay subset of fighter procs: hitlag/timers, Anim, input/IASA, Phys, Coll, primitive refresh, item/combat, ProcessHit-style cleanup, post-frame transient cleanup. |
| MotionState callback dispatch | CLOSED | Callback identity and source phase classes are generated through `MSLMSO01`; runtime owners consume table-backed callback classes instead of local action lists where callback ownership is available. |
| Transient state lifetime | CLOSED | Frame-start, pre-input, pre-physics, collision, combat, ProcessHit, and post-frame cleanup lanes have explicit source ownership and tests. |
| RNG phase ownership | RETAINED SOURCE-POLICY | Frame RNG remains seed-owned for ordinary replay rows and sim-owned only when source-visible scheduler ownership exists; exact hidden stream-phase lanes remain explicit seed reconstruction. |
| Item/projectile scheduler subset | RETAINED SOURCE-POLICY | Fighter-visible item/projectile spawn, collision, and post-combat slots are scheduled here; detailed item/projectile state remains owned by the item/projectile systems. |

## Source Phase Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Acceptance Locks |
|---|---|---|---|---|---|
| Fighter proc registration order: `Fighter_8006A1BC` prio 0, `Fighter_8006A360` prio 1, `Fighter_procUpdate` prio 4, map/collision/ProcessHit later | `fighter_callbacks_step_frame` ordered phase spine | Proc priority order from `refs/melee/src/melee/ft/fighter.c::Fighter_Create_Inline2` | Direct fixed C call sequence; structural test locks order | CLOSED | `tests/test_scheduler_source_completion.py::test_step_frame_phase_spine_matches_source_order` locks begin-frame, pre-input Anim, input, IASA, Phys, Coll, primitive refresh, item/combat, post-frame. |
| Hitlag/timer pre-Anim callback: `Fighter_8006A1BC` | `timers_update`, `timers_consume_post_hitlag_callbacks_pre_input`, hitlag-exit sweep-root cache | Hitlag timers, post-hitlag callback kind, Damage ASDI/root state | Runtime timer lanes plus explicit seed lanes for teacher-forced hidden callback state | CLOSED | Locks include `tests/test_damage_hitlag_exit_anim_rate_replay_real_locks.py`, `tests/test_damagefly_hitlag_exit_wall_reflect_replay_real_locks.py`, and `tests/test_guardsetoff_post_hitlag_owner_seed_replay_real_locks.py`. |
| Anim/timebase callback: `Fighter_8006A360`, `ftAnim_8006EBA4`, per-action `anim_cb` | `anim_timebase_update_pre_input`, `timers_update_post_anim`, `action_update_anim_callback_pre_input_fighter`, global Anim callback subsets | Animation frame, frame speed, callback identity, hitlag gate, prior input snapshot | `anim_timebase` lanes, generated callback metadata, table-backed script/motion-state helpers | CLOSED | Locks include `tests/test_debug_step_pre_combat_contract.py`, `tests/test_guard_reflect_timer.py`, `tests/test_shine_start_no_early_loop_regression.py`, and structural scheduler tests. |
| Input sample/counter ownership inside `Fighter_8006A360` / `Fighter_procUpdate` | `input_apply_pre_input_snapshot`, `input_apply`, UCF/counter lanes | Previous/current controller state, button edges, tilt timers, UCF state | `MslInput` lanes, processed input debug view, input timers in state | CLOSED | `tests/test_ucf_input.py`, `tests/test_opening_input_lock_landing_iasa_replay_real_locks.py`, and B-special/facing locks cover input ordering. |
| MotionState IASA callback order: `Fighter_procUpdate` after Anim and current input | `fighter_callbacks_iasa_phase`, `action_update`, owner-specific IASA helpers | `input_cb` identity, current input edges, state-entry side effects | Generated `MSLMSO01` callback/class data plus local source helpers for implemented callbacks | CLOSED | `tests/test_motion_state_owners_table.py`, `tests/test_landingair_wait_iasa_spotdodge_replay_real_locks.py`, `tests/test_guard_entry_transient_rollout_locks.py`, `tests/test_special_family_collision_handoffs_replay_real_locks.py`. |
| Physics callback order: `phys_cb` before velocity integration/collision | `fighter_callbacks_phys_phase`, `physics_integrate`, pre-physics item spawn | `phys_cb`, self/attack/ground velocities, root motion, hitlag gates | Generated MotionState classes, C physics state, source-specific transient lanes | CLOSED | Locks include `tests/test_grounded_self_vel_sync_regression.py`, `tests/test_platform_action_entry_callback_locks.py`, and collision source-completion tests. |
| Collision callback order: `coll_cb` / map proc after physics and before fighter combat | `fighter_callbacks_collision_phase`, `stage_collision_apply`, `ledge_try_catch_post_collision`, post-collision owners | `coll_cb`, CollData, stage state, ledge masks, post-collision owner state | Collision source packets documented in `collision.md` | CLOSED | Collision closure tests cover floor/wall/ceiling, ledge, platform, Randall/FoD, and post-collision action transitions. |
| Primitive refresh before fighter/item narrowphase: `lb_8000B1CC`, hitbox/hurtbox/shield descriptors | `fighter_callbacks_primitive_refresh_phase` | Post-Anim/Phys/Coll JObj pose, dynamic chains, hitbox/hurtbox callback state | Fixed-capacity hitbox/hurtbox/shield descriptor refresh | CLOSED | Pre-combat debug tests and body/contact geometry locks cover that primitive refresh occurs before combat but after collision. |
| Item/projectile collision slot before fighter-vs-fighter combat where fighter gameplay observes it | `fighter_callbacks_item_collision_and_combat_phase`, `items_update_collision_phase`, `throw_flow_update_post_items` | Item live slots, projectile hitboxes, throw release pulses, shield/reflect state | Fixed item pool and explicit throw/item transient lanes | RETAINED SOURCE-POLICY | Detailed behavior belongs to item/projectile docs. Scheduler slot is locked by `tests/test_throw_pulse_consumed_transient_clear_regression.py`, `tests/test_thrownlw_attached_laser_no_damage_entry_regression.py`, and laser shield/reflect locks. |
| Fighter combat and post-combat state mutation | `combat_resolve`, `items_update_post_combat`, `knockdown_update_post_combat` | Pair order, hitlists, stale/source identity, ProcessHit side effects | Combat subsystem state plus scheduler position in frame spine | CLOSED | Combat owner tests use `debug_step_input_pre_combat` to prove pre-combat phase state and normal `step_input` to prove post-combat effects. |
| ProcessHit-style prio 0xE effects | `combat_processhit_consume`, shield recharge, source-clear cleanup, post-combat consume | Shield recharge gate, post-hit damage pending state, hitlist/source-clear transients | Explicit process-hit/source-clear seed/runtime lanes | CLOSED | Locks include `tests/test_guard_entry_transient_rollout_locks.py`, `tests/test_source_port0_replay_real_locks.py`, `tests/test_combat_attack_id_snapshot_replay_real_locks.py`. |
| Match-flow scheduler subset: Entry/Death/Rebirth timers and public visibility | `match_flow_update_pre_anim`, `post_anim`, `post_input`, `post_physics` | Match-flow timers, stock/death phases, entry states, hitcamera/magnify gameplay lanes | `match_flow.c` phase functions and explicit seed lanes | RETAINED SOURCE-POLICY | Match-flow has its own system doc. Scheduler-owned call placement is closed; detailed camera/magnify/death behavior remains in `match_flow.md`. Locks include DeadUpStar/DeadUpFall tests. |
| Frame-end transient lifetime | `fighter_callbacks_post_frame_phase`, `clear_seed_owned_transients_post_frame` | End-of-frame public state, seed-only lanes, source-clear windows, frame-start snapshots | Centralized post-frame cleanup in `fighter_callbacks.c` | CLOSED | `tests/test_throw_pulse_consumed_transient_clear_regression.py`, `tests/test_guard_entry_transient_rollout_locks.py`, and scheduler doc tests lock cleanup policy. |
| Rollout RNG frame clock | Replay validation applies `frame_pre_random_seed` before each `step_input`; `msl_batch_commit_rollout_clock_rng` keeps source-site admission without synthetic advancement when replay-fed | HSD RNG seed, frame-owner policy, hidden stream-phase reconstruction | Replay playback RNG is authoritative for validation; normal free-running sim remains internally clocked; source-owned lanes still prove Shy Guy, DamageFlyRoll/Fighter_8006CDA4, and match-flow RNG sites | RETAINED SOURCE-POLICY | Replay playback feeds replay inputs plus replay frame-start RNG. Locks include DamageFlyRoll RNG phase tests, Yoshi Shy Guy RNG tests, and match-flow RNG tests. |
| Debug pre-combat scheduler cut | `msl_batch_debug_step_input_pre_combat` / `step_one_frame_pre_combat` | Same phases as normal frame through primitive/item refresh, excluding `combat_resolve` | Debug-only API using same phase spine with `run_combat=0` | RETAINED SOURCE-POLICY | Debug cut is tooling policy, not vanilla gameplay. `tests/test_debug_step_pre_combat_contract.py` locks timebase parity when no combat occurs. |

## Transient Lifetime Map

| Lifetime Boundary | Owner | Policy |
|---|---|---|
| Frame start | `fighter_callbacks_begin_frame_phase` | Clear landing transients, cache previous action/floor/state flags, and cache hitlag-exit sweep roots before timer/Anim mutation. |
| Pre-input Anim | `fighter_callbacks_pre_input_anim_phase` | Previous input snapshot, hitlag timer exit, match-flow pre-Anim, animation timebase, post-Anim timers, per-action Anim callbacks, ProcessHit consume. |
| Current input | `fighter_callbacks_input_phase` | Apply current inputs and then run hitlag every-frame callbacks that observe current inputs. |
| IASA | `fighter_callbacks_iasa_phase` | Match-flow IASA, then action/locomotion/special/grab/shield IASA owners. |
| Physics | `fighter_callbacks_phys_phase` | Fighter-driven item spawns, camera target state flags, and velocity/root integration. |
| Collision | `fighter_callbacks_collision_phase` | Attachment pre-collision, stage collision, ledge mask/env, post-collision action owners. |
| Primitive refresh | `fighter_callbacks_primitive_refresh_phase` | Deferred Anim ticks, dynamic pose, shield/body/hitbox/hurtbox primitive refresh. |
| Item/combat | `fighter_callbacks_item_collision_and_combat_phase` | Hitlist tick, shield/reflect descriptors, item collision, throw post-items, combat, post-combat item/knockdown effects. |
| Frame end | `fighter_callbacks_post_frame_phase` | Deferred post-combat tick, post-frame state flags, magnify damage, floor/action snapshots, seed-owned transient clear. |

## Retained Source Policies

| Policy | Status | Why It Remains |
|---|---|---|
| Replay playback frame RNG | RETAINED SOURCE-POLICY | Validation playback feeds each replay row's `frame_pre_random_seed` before stepping; ordinary RL/free-running stepping does not pull future replay RNG. |
| Explicit hidden RNG phase lanes | RETAINED SOURCE-POLICY | DamageFlyRoll/Fighter_8006CDA4 and Yoshi Shy Guy phases reconstruct hidden source stream state for exact reseed without changing free-running semantics. |
| Debug pre-combat cut | RETAINED SOURCE-POLICY | This is a triage API that intentionally stops before `combat_resolve`; it is not canonical gameplay output. |
| Item/projectile scheduler slot only | RETAINED SOURCE-POLICY | Scheduler owns item/projectile placement relative to fighters; detailed projectile/item mechanics remain in their system docs. |
| Match-flow phase placement only | RETAINED SOURCE-POLICY | Scheduler owns call order; detailed match-flow semantics remain in `match_flow.md`. |

## Validation And Performance Evidence

Evidence from this closure pass:

- `make build BUILD_FORCE=1`: passed.
- `make validate-all`: passed.
- `make test`: passed.
- `make fmt-check`: passed.
- `git diff --check && git diff --cached --check`: passed.
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`: no hard reds and no unclassified reds.
- No hot frame-step runtime code changed in this pass, so no benchmark was required.

## Historical References

- `refs/melee/src/melee/ft/fighter.c::Fighter_Create_Inline2`
- `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_procUpdate,Fighter_ProcessHit_8006D1EC}`
- `agent_docs/DATA_CONTRACT.md` sections for `MSLMSO01`, RNG stream phase lanes, and seed-owned transient lanes.
