# Mismatch Roadmap

Baseline snapshot:
- Date: 2026-04-01
- Commit: `ffa903a`
- One-step discrete mismatches: `1145 / 4462843`
- One-step float norm MAE p95: `0.00328046`
- Rollout: `best_len.max=207`, `streak_len.p95=113`, `first_mismatch_total=863`, `first_mismatch_seeded_total=246`
- Guardrail preflight: `PASS`
- Taxonomy artifact: `reports/triage/20260401T235448Z_mismatch_taxonomy/summary.json`
- Audit artifacts:
  - `reports/triage/20260401T235448Z_mismatch_taxonomy/audit_summary.json`
  - `reports/triage/20260401T235448Z_mismatch_taxonomy/audit_samples.tsv`
  - `reports/triage/20260401T235448Z_mismatch_taxonomy/families/*.tsv`

Scope note:
- This document is analysis-only.
- No gameplay/runtime changes landed in `src/*.c` for this cycle.
- Counts below come from `uv run python -m tools.eval.mismatch_taxonomy --suite replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets`.

Top-family coverage:
- Top 12 families below cover `1055 / 1145` mismatches (`92.1%`).
- Remaining tail: `90` mismatches across `F15_guard_item_ownership`, `F11_locomotion_action_frame`, `F13_specialhi_landing`, `F14_throw_item_bookkeeping`, and `F16_item_identity_residual`.

Audit headline:
- `1.00` precision on `F08`, `F12`, `F05`, `F01`, `F04`, `F02`, `F03`, `F07`, `F11`, `F06`, `F15`, `F14`, `F16`
- `0.938` on `F13_specialhi_landing`
- `0.920` on `F09_aerial_combat_resolution`
- `0.800` on `F10_grounded_transition_resolution`
- `0.760` on `F99_misc_other`
- Interpretation: `F10` is usable but still overlaps a small grounding/hurtbox tail; `F99` remains explicitly non-execution-grade and should be split again before runtime work.

## Family Table

| Family | Count | Primary field(s) | Representative rows | Decomp hypothesis | Conf. | Fix type | Risk |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| `F08_damage_resolution_combat` | 267 | `instance_id`, `action_frame`, `hitlag` | `AttachedGoodNaturedGuanaco.msl:429:p0`, `AttachedGoodNaturedGuanaco.msl:2052:p0`, `AttachedGoodNaturedGuanaco.msl:2259:p0` | ProcessHit / damage-entry ordering is still off in the combat followup path. Refs: `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724}` | med | runtime-only | med |
| `F01_guard_release_collision` | 146 | `instance_id`, `action_frame`, `action_id` | `AttachedGoodNaturedGuanaco.msl:124:p1`, `AttachedGoodNaturedGuanaco.msl:1368:p0`, `AttachedGoodNaturedGuanaco.msl:1498:p1` | Guard / GuardSetOff / GuardReflect callback ordering still admits or suppresses shield-hit followups a frame off. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450,ftCo_Guard_IASA}` | high | runtime-only | med |
| `F07_knockdown_grounding` | 118 | `jumps_left`, `instance_id`, `action_id` | `AttachedGoodNaturedGuanaco.msl:609:p1`, `AttachedGoodNaturedGuanaco.msl:782:p0`, `AttachedGoodNaturedGuanaco.msl:4565:p0` | DamageFly collision ownership still chooses Passive / PassiveStand / DownBound / landing a frame off on grounded-contact rows. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40` | high | runtime-only | med |
| `F10_grounded_transition_resolution` | 102 | `action_id`, `animation_index`, `last_hit_by` | `AttachedGoodNaturedGuanaco.msl:1577:p1`, `AttachedGoodNaturedGuanaco.msl:2351:p1`, `AttachedGoodNaturedGuanaco.msl:3420:p0` | Grounded Dash / Walk / Turn / KneeBend / attack transition ordering still bumps motion state or callback-owned followups at the wrong point. Refs: `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0`, `refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C` | med | runtime-only | med |
| `F12_instance_id_transition_only` | 82 | `instance_id` | `AttachedGoodNaturedGuanaco.msl:124:p0`, `AttachedGoodNaturedGuanaco.msl:1100:p1`, `AttachedGoodNaturedGuanaco.msl:1368:p1` | Pure `instance_id` residuals are still concentrated in motion-entry rows where the `ft_800895E0` bump gate or `x2073` compare-byte parity differs. Refs: `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0`, `refs/melee/src/melee/ft/ftmotionstates.c` | high | runtime-only | low |
| `F09_aerial_combat_resolution` | 70 | `state_flags[1]`, `hitlag`, `last_attack_landed` | `AttachedGoodNaturedGuanaco.msl:2052:p1`, `AttachedGoodNaturedGuanaco.msl:2631:p0`, `AttachedGoodNaturedGuanaco.msl:4239:p1` | Jump / AttackAir followup rows still miss the correct aerial damage-admission or continuation window. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_DamageFly_IASA}`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}` | med | runtime-only | med |
| `F99_misc_other` | 61 | `hurtbox_state`, `state_flags[4]`, `state_flags[1]` | `AttachedGoodNaturedGuanaco.msl:2069:p1`, `AttachedGoodNaturedGuanaco.msl:3145:p0`, `AttachedGoodNaturedGuanaco.msl:3157:p0` | Mixed residual bucket. Current heads are SpecialLw enter/loop, ThrownLw, and a small TurnRun slice; split further before touching runtime. Refs: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrownLw_Anim`, `refs/melee/src/melee/ft/ftmotionstates.c` | low | instrumentation first | high |
| `F05_damage_state_flags` | 60 | `state_flags[4]`, `state_flags[3]` | `AttachedGoodNaturedGuanaco.msl:999:p0`, `AttachedGoodNaturedGuanaco.msl:2469:p0`, `AttachedGoodNaturedGuanaco.msl:2943:p0` | DamageFly / DamageFall / Down* state-flags still tick or clear at the wrong point relative to damage callbacks, without a full action fork. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll` | high | runtime-only | low |
| `F06_damageflyroll_rng_gate` | 42 | `action_id`, `animation_index`, `state_flags[4]` | `AttachedGoodNaturedGuanaco.msl:1772:p1`, `AttachedGoodNaturedGuanaco.msl:6050:p0`, `GracefulAttachedTurtle.msl:3106:p0` | Remaining DamageFlyTop -> DamageFlyRoll blocker still depends on an unmodeled pre-gate `Fighter_8006CDA4` RNG consumer family. Refs: `refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`, `refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf` | high | instrumentation first | high |
| `F03_capturewait_bridge` | 38 | `action_frame`, `instance_id`, `jumps_left` | `AttachedGoodNaturedGuanaco.msl:955:p0`, `AttachedGoodNaturedGuanaco.msl:3198:p1`, `AttachedGoodNaturedGuanaco.msl:3204:p0` | Catch / CapturePulled / CaptureWait callback ordering still leaves a one-tick victim anim-rate and action-frame ownership gap on first steady CaptureWait frames. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18,fn_800DB6C8}` | high | seed/schema + runtime | med |
| `F02_guard_timer_flags` | 35 | `state_flags[3]`, `action_frame` | `AttachedGoodNaturedGuanaco.msl:552:p1`, `AttachedGoodNaturedGuanaco.msl:1212:p1`, `AttachedGoodNaturedGuanaco.msl:1477:p0` | GuardSetOff / GuardReflect timer and state-flag lanes are still ticking or clearing at the wrong point relative to the anim callback. Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_80093A50,ftCo_80093BC0}` | high | runtime-only | low |
| `F04_match_flow_rebirth` | 34 | `state_flags[4]` | `AttachedGoodNaturedGuanaco.msl:1890:p1`, `AttachedGoodNaturedGuanaco.msl:1891:p1`, `AttachedGoodNaturedGuanaco.msl:1920:p1` | Death -> Rebirth still has residual `state_flags[4]` / identity-reset parity issues on the first visible Rebirth frames. Refs: `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0`, `refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebirth.c::ftCo_800D4FF4` | high | runtime-only | low |

Tail families not in the 92.1% table:
- `F15_guard_item_ownership`: `27`
- `F11_locomotion_action_frame`: `21`
- `F13_specialhi_landing`: `16`
- `F14_throw_item_bookkeeping`: `14`
- `F16_item_identity_residual`: `12`

## Dependency DAG

Schema-first lanes:
- `S1_walk_timebase_seed_lane`
  - Files: `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
  - Blocks: `F11_locomotion_action_frame`
  - Soft dependency for: `F10_grounded_transition_resolution`
- `S2_capturewait_seed_bridge`
  - Files: `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`, `src/api.h`
  - Blocks: `F03_capturewait_bridge`
- `S3_damageflyroll_pregate_probe`
  - Files: `tools/eval/damageflyroll_rng_blocker_report.py`, `tools/slippi/make_dataset_from_slp.py`, optional `tools/dolphin/*`
  - Blocks: `F06_damageflyroll_rng_gate`
- `S4_item_owner_seed_cleanup`
  - Files: `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
  - Blocks: `F14_throw_item_bookkeeping`, `F15_guard_item_ownership`
  - Soft dependency for: `F08_damage_resolution_combat`

Runtime lanes blocked by schema:
- `F03` cannot be executed until `S2` makes the first-steady CaptureWait ownership lane explicit.
- `F06` should not take new `src/` changes until `S3` closes or isolates the `Fighter_8006CDA4` pre-gate consumer blocker.
- `F11` should wait for `S1` because the remaining rows are callback-rate shaped rather than pure runtime divergence.
- `F14` and `F15` should wait for `S4`; otherwise item rows will keep mixing seed and runtime ownership effects.

Safe parallelizable lanes:
- `F12_instance_id_transition_only` can run in parallel with any of `S1`, `S2`, `S3`, or `S4`.
- `F05_damage_state_flags` and `F04_match_flow_rebirth` can run together; both are timer/match-flow lanes and do not depend on schema-first work above.
- `F01_guard_release_collision` can run in parallel with `F07_knockdown_grounding` if `src/step.c` ownership is coordinated, because the primary write scopes are combat/items versus locomotion/mpcoll.
- `S2_capturewait_seed_bridge` and `S3_damageflyroll_pregate_probe` are safe to run in parallel; one is grab-flow seed work and the other is RNG instrumentation.

Explicit cycle ordering:
- `Cycle A`: `S1 + S2 + S3 + S4` in parallel, plus `F12`
  - Entry: baseline taxonomy regenerated; no open dataset schema mismatch
  - Exit: audit precision for `F03`, `F06`, `F11`, `F14`, and `F15` is either `>=0.95` or the blocker explanation is updated with a narrower residual family
- `Cycle B`: `F05 + F04 + F02 + F11`
  - Entry: `Cycle A` schema work landed or explicitly deferred with blocker notes
  - Exit: combined reduction of `>=90` and no new guardrail `state_flags` rows
- `Cycle C`: `F01 + F07 + F10`
  - Entry: `F12`, `F05`, and `F04` are already reduced; no new `instance_id` explosion
  - Exit: combined reduction of `>=180` and rollout first-mismatch totals do not regress
- `Cycle D`: `F08 + F09 + F13`
  - Entry: `F01` and `F07` no longer dominate adjacent rows
  - Exit: combined reduction of `>=160` and no new combat ownership guardrail rows
- `Cycle E`: `F03 + F06 + F14 + F15 + F16`, then split `F99`
  - Entry: schema blockers closed
  - Exit: remaining residuals are bounded by named families with audit precision `>=0.95`

## Family Execution Specs

| Family | Success target | Regression watch fields | Must-run tests |
| --- | --- | --- | --- |
| `F08_damage_resolution_combat` | Family count `<=167` and `instance_id` subcount `<=39`; `hitlag` subcount down by `>=10` | `action_id`, `hitlag`, `hitstun`, `instance_hit_by`, `instance_id`, `state_flags[1]` | `tests/test_combat_followup_locks_20260218.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`, `tests/test_seed_hitlist_ownership_carry_regression.py`, `tests/test_damagefall_attackair_replay_real_locks.py` |
| `F01_guard_release_collision` | Family count `<=66`; `action_id` and `instance_id` both down by `>=10` | `action_id`, `action_frame`, `animation_index`, `instance_id`, `item_exists`, `item_owner`, `state_flags[1]` | `tests/test_guardon_no_spurious_guardsetoff_regression.py`, `tests/test_guardreflect_laser_shieldhit_replay_real_locks.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`, `tests/test_todo_elimination_guard_reflect_item_seed_locks.py` |
| `F07_knockdown_grounding` | Family count `<=48`; `jumps_left` subcount `<=8` | `on_ground`, `ground_id`, `jumps_left`, `hurtbox_state`, `action_id`, `animation_index` | `tests/test_damagefly_downbound_state_selection_regression.py`, `tests/test_hurtbox_state_sim_owned.py`, `tests/test_damagefly_passivewalljump_replay_real_locks.py`, `tests/test_ledge_grab_treasuredbackkangaroo_regression.py` |
| `F10_grounded_transition_resolution` | Family count `<=62`; audit precision stays `>=0.80` until split | `action_id`, `animation_index`, `ground_id`, `hurtbox_state`, `last_hit_by`, `instance_id` | `tests/test_ground_attack_selector_regression.py`, `tests/test_walkslow_attacks3lw_replay_real_locks.py`, `tests/test_dash_anim_wait_turn_replay_real_locks.py`, `tests/test_dash_late_turn_replay_real_locks.py` |
| `F12_instance_id_transition_only` | Family count `<=22` | `instance_id`, `action_id`, `animation_index` | `tests/test_instance_id_transition_regression.py`, `tests/test_instance_id_x2073_bump_gate.py`, `tests/test_seedref_cluster_lock_regression_20260220.py` |
| `F09_aerial_combat_resolution` | Family count `<=35`; audit precision remains `>=0.90` | `hitlag`, `hitstun`, `last_attack_landed`, `last_hit_by`, `combo_count`, `state_flags[0]`, `state_flags[1]`, `state_flags[4]` | `tests/test_damagefall_attackair_replay_real_locks.py`, `tests/test_attackair_direction_and_jump_selection_regression.py`, `tests/test_combat_followup_locks_20260218.py`, `tests/test_item_hit_damage_facing_replay_real_locks.py` |
| `F99_misc_other` | Split into at least `3` named sub-families; bucket count `<=20` before runtime work | `hurtbox_state`, `state_flags[4]`, `state_flags[1]`, `ground_id`, `action_frame` | `tests/test_mismatch_taxonomy.py`, new triage-only tests for each split family, `tests/test_eval_triage_clis.py` |
| `F05_damage_state_flags` | Family count `<=20`; `state_flags[4]` subcount `<=12` | `state_flags[3]`, `state_flags[4]`, `hitlag`, `hitstun` | `tests/test_state_flags_parity_regression.py`, `tests/test_one_step_cluster_fixes_action_frame_state_flags_regression.py`, `tests/test_hitlag_hitstun_state_flags_timing_regression.py` |
| `F06_damageflyroll_rng_gate` | Blocker report no longer names the DamageFlyTop carry family as open; then family count `<=12` | `action_id`, `animation_index`, `state_flags[4]`, `instance_id` | `tests/test_damageflyroll_rng_blocker_report.py`, `tests/test_damageflyroll_fighter_8006cda4_phase_hint_replay_real_locks.py`, `tests/test_combat_ownership_seed_guardrail_locks.py` |
| `F03_capturewait_bridge` | Family count `<=10`; audit precision stays `>=0.95` after schema promotion | `action_frame`, `instance_id`, `jumps_left`, `last_hit_by`, `state_flags[1]`, `hurtbox_state` | `tests/test_capturewait_grab_mash_replay_real_locks.py`, `tests/test_grab_throw_final_sweep_regression.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`, `tests/test_capturewait_owner_tick_forensics.py` |
| `F02_guard_timer_flags` | Family count `<=10`; `state_flags[3]` subcount `<=5` | `state_flags[3]`, `state_flags[0]`, `action_frame`, `facing` | `tests/test_one_step_cluster_fixes_action_frame_state_flags_regression.py`, `tests/test_guard_action_frame_no_anim_idx_regression.py`, `tests/test_guard_reflect_timer.py` |
| `F04_match_flow_rebirth` | Family count `<=8`; `state_flags[4]` subcount `<=6` | `state_flags[4]`, `state_flags[3]`, `instance_id`, `facing` | `tests/test_rebirth_facing_respawn_side_replay_real_locks.py`, `tests/test_seed_match_flow_timer_prefix_invariance.py`, `tests/test_match_flow_damagefall_blastzone_guard.py` |

## Ranked Backlog

### Phase 1: Highest-Impact, Lowest-Risk

1. Instance-id motion-entry residuals (`F12_instance_id_transition_only`, plus the pure motion-entry subset of `F10_grounded_transition_resolution`)
   Touch candidates: `src/anim_timebase.c`, `src/api.c`, `src/match_flow.c`
   Tests to add/update: `tests/test_instance_id_transition_regression.py`, `tests/test_instance_id_x2073_bump_gate.py`, `tests/test_rebirth_facing_respawn_side_replay_real_locks.py`
   Acceptance checks: `F12` count drops by at least `60`; no new `instance_id` rows appear in `reports/triage/*_mismatch_taxonomy`; `make test` and guardrail preflight stay green
   Expected impact: `-60` to `-90`

2. Damage + rebirth state-flag parity (`F05_damage_state_flags` + `F04_match_flow_rebirth`)
   Touch candidates: `src/timers.c`, `src/match_flow.c`, `src/step.c`
   Tests to add/update: `tests/test_state_flags_parity_regression.py`, `tests/test_rebirth_facing_respawn_side_replay_real_locks.py`, `tests/test_one_step_cluster_fixes_action_frame_state_flags_regression.py`
   Acceptance checks: `F05 + F04` down by at least `60`; no regression in Rebirth / DamageFly state-flag rows; `make validate` / `make validate-rollout` unchanged or better
   Expected impact: `-55` to `-85`

3. Guard timer + walk action-frame cleanup (`F02_guard_timer_flags` + `F11_locomotion_action_frame`)
   Touch candidates: `src/timers.c`, `src/anim_timebase.c`, `src/step.c`
   Tests to add/update: `tests/test_one_step_cluster_fixes_action_frame_state_flags_regression.py`, `tests/test_walkslow_af2_callback_rate_seed_bridge_replay_real_locks.py`, `tests/test_guard_action_frame_no_anim_idx_regression.py`
   Acceptance checks: `F02 + F11` down by at least `35`; no new GuardOn / GuardSetOff / WalkSlow action-frame regressions
   Expected impact: `-30` to `-50`

### Phase 2: Medium-Risk Runtime Lanes

4. Guard release / reflect collision ordering (`F01_guard_release_collision`)
   Touch candidates: `src/combat.c`, `src/items.c`, `src/step.c`
   Tests to add/update: `tests/test_guardon_no_spurious_guardsetoff_regression.py`, `tests/test_guardreflect_laser_shieldhit_replay_real_locks.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`
   Acceptance checks: `F01` down by at least `80`; no new guard-family rows in rollout first-mismatch report
   Expected impact: `-80` to `-130`

5. Knockdown / passive grounding (`F07_knockdown_grounding`)
   Touch candidates: `src/mpcoll_ground.c`, `src/ledge.c`, `src/locomotion.c`
   Tests to add/update: `tests/test_damagefly_downbound_state_selection_regression.py`, `tests/test_hurtbox_state_sim_owned.py`, `tests/test_damagefly_passivewalljump_replay_real_locks.py`
   Acceptance checks: `F07` down by at least `70`; no ledge-grab or ECB regressions
   Expected impact: `-70` to `-110`

6. Core combat followup rows (`F08_damage_resolution_combat` + `F09_aerial_combat_resolution`)
   Touch candidates: `src/combat.c`, `src/timers.c`, `src/step.c`, `src/items.c`
   Tests to add/update: `tests/test_combat_followup_locks_20260218.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`, `tests/test_seed_hitlist_ownership_carry_regression.py`, `tests/test_damagefall_attackair_replay_real_locks.py`
   Acceptance checks: `F08 + F09` down by at least `140`; no new hitlag / instance ownership guardrail rows
   Expected impact: `-140` to `-220`

7. SpecialHi / FallSpecial landing continuations (`F13_specialhi_landing`)
   Touch candidates: `src/locomotion.c`, `src/ledge.c`, `src/char_params.h`
   Tests to add/update: `tests/test_specialhi_holdair_launch_velocity_regression.py`, `tests/test_spacie_bspecial_and_specialhi_regression.py`, `tests/test_landingfallspecial_illusion_source_replay_real_locks.py`
   Acceptance checks: `F13` materially shrinks without increasing `F07`
   Expected impact: `-10` to `-20`

### Phase 3: Blocked / Instrumentation-Dependent

8. CaptureWait ownership bridge (`F03_capturewait_bridge`)
   Touch candidates: `src/grab_flow.c`, `src/api.c`, `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
   Tests to add/update: `tests/test_capturewait_grab_mash_replay_real_locks.py`, `tests/test_grab_throw_final_sweep_regression.py`, `tests/test_combat_ownership_seed_guardrail_locks.py`
   Acceptance checks: taxonomy `blocker_rows` for `F03` disappear and no new CaptureWait seed-visible blockers are introduced
   Expected impact: `-20` to `-40`

9. DamageFlyRoll RNG gate (`F06_damageflyroll_rng_gate`)
   Touch candidates: `tools/eval/damageflyroll_rng_blocker_report.py`, `tools/slippi/make_dataset_from_slp.py`, `tools/dolphin/*` if a new probe is required
   Tests to add/update: `tests/test_damageflyroll_rng_blocker_report.py`, `tests/test_damageflyroll_fighter_8006cda4_phase_hint_replay_real_locks.py`
   Acceptance checks: blocker report no longer names the DamageFlyTop carry family as open; only then touch runtime
   Expected impact: `-20` to `-40`

10. Item bookkeeping tail (`F14_throw_item_bookkeeping` + `F15_guard_item_ownership` + `F16_item_identity_residual`)
   Touch candidates: `src/items.c`, `src/combat.c`, `tools/slippi/seed_history.py`
   Tests to add/update: `tests/test_throwhi_pulse_seed_bridge_replay_real_locks.py`, `tests/test_throwhi_item_damage_facing_replay_real_locks.py`, `tests/test_todo_elimination_guard_reflect_item_seed_locks.py`
   Acceptance checks: item-only families shrink without increasing `instance_id` or guard families
   Expected impact: `-20` to `-45`

11. Residual mixed bucket split (`F99_misc_other`)
   Touch candidates: `tools/eval/mismatch_taxonomy.py`, targeted triage CLIs under `tools/eval/`
   Tests to add/update: new classifier locks for the resulting sub-buckets before any runtime patch
   Acceptance checks: `F99` is replaced by named sub-families with explicit refs and owner modules
   Expected impact: planning-only until split is complete

## Avoid For Now

- Do not patch `F06_damageflyroll_rng_gate` in `src/` until the pre-gate consumer is made seed-visible or disproven with instrumentation. This is the highest-regression lane in the current suite.
- Do not broad-match `F99_misc_other` with ad hoc SpecialLw / ThrownLw rules. The bucket still mixes at least three unrelated behaviors and needs one more triage split first.
- Do not attack the item families (`F14` / `F15` / `F16`) before `F01_guard_release_collision` is materially reduced. Right now several item rows are downstream of unresolved guard ordering.
- Do not “fix” `F03_capturewait_bridge` with replay-shaped victim timer carry. The right lane is explicit seed/schema promotion plus callback-order runtime parity.

## Next 3 Cycles

### Cycle 1

Goal:
- Clear the low-risk identity/timer lanes first.

Entry criteria:
- Baseline still within `±10` mismatches of `1145`.
- `reports/triage/*_mismatch_taxonomy/summary.json` regenerated on the active branch.

Planned lanes:
- `F12_instance_id_transition_only`
- `F05_damage_state_flags`
- `F04_match_flow_rebirth`
- `F02_guard_timer_flags`
- `F11_locomotion_action_frame`

Exit criteria:
- Combined reduction of `>=120` mismatches.
- `make test`, `make validate OUT=reports/validation/one_step_suite_eval.txt`, and `make validate-rollout OUT=reports/validation/rollout_suite_eval.txt` all pass.

### Cycle 2

Goal:
- Remove the highest-leverage runtime transition bundles.

Entry criteria:
- Cycle 1 lands cleanly with no new guardrail rows.

Planned lanes:
- `F01_guard_release_collision`
- `F07_knockdown_grounding`
- `F08_damage_resolution_combat`
- `F09_aerial_combat_resolution`
- `F13_specialhi_landing`

Exit criteria:
- Combined reduction of `>=220` additional mismatches.
- Rollout first-mismatch totals do not regress.

### Cycle 3

Goal:
- Resolve the instrumentation-dependent blockers and the item tail.

Entry criteria:
- Phase 1 and 2 runtime lanes are mostly exhausted.
- `F99_misc_other` is either below `30` or has been split into named sub-families.

Planned lanes:
- `F03_capturewait_bridge`
- `F06_damageflyroll_rng_gate`
- `F14_throw_item_bookkeeping`
- `F15_guard_item_ownership`
- `F16_item_identity_residual`

Exit criteria:
- Blocker reports are replaced by seed-visible or runtime-complete explanations.
- Remaining one-step residuals are dominated by named, bounded lanes rather than mixed buckets.
