#include "fighter_callbacks.h"

#include <errno.h>

#include "action.h"
#include "action_ids.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "combat.h"
#include "damage_source.h"
#include "hitboxes.h"
#include "hitlist.h"
#include "hurtboxes.h"
#include "blaster.h"
#include "grab_attachment.h"
#include "input.h"
#include "items.h"
#include "ledge.h"
#include "locomotion.h"
#include "match_flow.h"
#include "move_tables.h"
#include "motion_state_owners.h"
#include "knockdown.h"
#include "physics.h"
#include "shields.h"
#include "shine.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "mpcoll_env.h"
#include "timers.h"
#include "reflector_bubbles.h"
#include "state_flags.h"
#include "throw_flow.h"

// NOTE: `blaster_update_post_collision` is intentionally not part of the public blaster module API
// yet; keep the forward declaration local to preserve the current include surface.
extern void blaster_update_post_collision(MslBatch* batch);

static inline void clear_landing_transients(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Slippi post-frame `l_cancel` is a 1-frame status on LandingAir* entry. Clear it each frame
      // and let landing entry code set it when applicable.
      batch->state.l_cancel[idx] = 0;
      // Clear per-frame anim-timebase transients.
      batch->state.anim_defer_tick_once[idx] = 0;
    }
  }
}

static inline uint8_t step_keep_fighter_8006cda4_pre_gate_count(const MslBatch* batch, int bi,
                                                                int p, int num_players) {
  if (batch == NULL) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t count = batch->state.fighter_8006cda4_pre_gate_consume_count[idx];
  if (count == 0u || count > 4u) {
    return 0u;
  }

  const uint16_t action = batch->state.action_id[idx];
  if ((action == (uint16_t)MSL_ACT_ATTACK_AIR_N || action == (uint16_t)MSL_ACT_ATTACK_AIR_B) &&
      batch->state.on_ground[idx] == 0u && batch->state.hitlag[idx] == 0u &&
      batch->state.hitstun[idx] == 0u) {
    return 1u;
  }

  if (count <= 3u && action == (uint16_t)MSL_ACT_DAMAGE_FALL && batch->state.on_ground[idx] == 0u &&
      batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u) {
    const int attacker = msl_damage_source_local_slot_from_port0(batch, bi, num_players,
                                                                 batch->state.last_hit_by[idx]);
    if (attacker >= 0 && attacker != p) {
      // DamageFall IASA handoff owner:
      // - DamageFly_IASA can enter DamageFall, then DamageFall_IASA can immediately admit
      //   AttackAir through ftCo_AttackAir_CheckItemThrowInput.
      // - The hidden Fighter_8006CDA4 stream phase belongs to the same damage-entry source
      //   episode and must survive this one-frame IASA handoff until ftCo_8008DCE0 consumes it on
      //   the accepted hit. Marker 4 is not stream phase and is intentionally excluded.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
      return 1u;
    }
  }

  if (count <= 4u && action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
      batch->state.on_ground[idx] == 0u && batch->state.hitstun[idx] != 0u &&
      batch->state.instance_hit_by[idx] != 0u) {
    const int attacker = msl_damage_source_local_slot_from_port0(batch, bi, num_players,
                                                                 batch->state.last_hit_by[idx]);
    if (attacker >= 0 && attacker != p) {
      // DamageFlyTop hitlag-continuity owner:
      // - The explicit Fighter_8006CDA4 stream phase is replay-seeded for delayed
      //   ftCo_8008DCE0 DamageFlyRoll gates that can occur after a same-source active-hitlag
      //   interval.
      // - Hitlag is still part of the same common-damage source episode; dropping the lane while
      //   frozen makes rollouts seeded before the later hit lose both the replay frame-start RNG
      //   clock and the hidden pre-gate consume count before the source-owned gate runs.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_DamageFly_Anim,ftCo_DamageFly_Coll,ftCo_8008DCE0}
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
      return 1u;
    }
  }
  return 0u;
}

static inline void clear_seed_owned_transients_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int platform_id = 0; platform_id < 2; platform_id++) {
      const size_t pidx = (size_t)bi * 2u + (size_t)platform_id;
      // FoD platform-height source bits are seed/current-frame provenance for sparse replay
      // reconstruction, not live scheduler state. Consume them during the reseeded frame; later
      // frames require live velocity/scheduler/contact evidence or generated initial-height data
      // again.
      // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
      // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
      if (batch->state.stage_fod_platform_deferred_velocity_valid[pidx]) {
        // Same-step FoD platform contacts expose the current collision height for this frame before
        // the next grIzumi velocity is source-visible. Promote that deferred velocity only after
        // the reseeded frame so the landing frame does not pre-advance the platform.
        batch->state.stage_fod_platform_velocity[pidx] =
            batch->state.stage_fod_platform_deferred_velocity[pidx];
        batch->state.stage_fod_platform_velocity_valid[pidx] = 1u;
        batch->state.stage_fod_platform_deferred_velocity[pidx] = 0.0f;
        batch->state.stage_fod_platform_deferred_velocity_valid[pidx] = 0u;
      }
      batch->state.stage_fod_platform_height_source[pidx] = 0u;
    }
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // `seed_t.throw_pulse_consumed` is a one-step seed bridge for throw_flags_b0 pulse ownership:
      // consume within the current simulated frame, then clear so it cannot stale-carry into later
      // rollout frames.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
      batch->state.throw_pulse_consumed[idx] = 0u;
      batch->state.throw_pulse_crossed_prev_frame[idx] =
          batch->state.throw_pulse_crossed_curr_frame[idx];
      batch->state.throw_command_pending_pulse_frame[idx] = 0u;
      batch->state.throw_command_pending_seed_valid[idx] = 0u;
      const uint16_t action_id = batch->state.action_id[idx];
      if (action_id != (uint16_t)MSL_ACT_THROW_B && action_id != (uint16_t)MSL_ACT_THROW_HI &&
          action_id != (uint16_t)MSL_ACT_THROW_LW) {
        batch->state.throw_command_deferred_pulse_frame[idx] = 0u;
      }
      batch->state.throw_pulse_crossed_curr_frame[idx] = 0u;
      // DamageFly wall-ASDI provenance is allowed to arm on the SpecialAirHi wall-contact frame
      // before combat starts hitlag, but it must become live only if the frame actually enters or
      // continues hitlag. Non-hitlag wall contacts are stale CollData for this owner.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
      if (batch->state.damage_hitlag_wall_asdi_latch[idx] != 0u &&
          batch->state.hitlag_pre_timer[idx] == 0u && batch->state.hitlag[idx] == 0u) {
        batch->state.damage_hitlag_wall_asdi_latch[idx] = 0u;
      }
      // `seed_t.source_clear_processhit_damage_pending_phase` is a one-step bridge for hidden
      // ProcessHit-owned source clear. Consume within this frame only.
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
      batch->state.source_clear_processhit_damage_pending_phase[idx] = 0u;
      // `seed_t.fighter_8006cda4_pre_gate_consume_count` is usually a one-step pre-gate owner.
      // Keep rollout continuity only across the source-owned airborne AttackAir*/DamageFall/
      // DamageFlyTop episode that can still reach ftCo_8008DCE0. Marker 4 is still not stream
      // phase, but for DamageFlyTop same-source AttackAirB segments it carries gate-admission
      // provenance until the delayed hit consumes the zero-pre-gate DamageFlyRoll decision.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      if (!step_keep_fighter_8006cda4_pre_gate_count(batch, bi, p, num_players)) {
        batch->state.fighter_8006cda4_pre_gate_consume_count[idx] = 0u;
      }
      // `seed_t.source_clear_grounded_damage_clear_phase` is a one-step bridge for grounded
      // source-owner clear ownership (`ftCommon_800804FC` path). Consume within this frame only.
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
      batch->state.source_clear_grounded_damage_clear_phase[idx] = 0u;
      // `seed_t.source_clear_terminal_phase` is also one-step bridge ownership. Consume in
      // timers_update_post_anim(), then clear to prevent sticky carry in rollout frames.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      batch->state.source_clear_terminal_phase[idx] = 0u;
      // One-step replay-facing locomotion / instance-order lanes. Runtime producers update the
      // causal state directly; seeded overrides are consumed within the current step only.
      batch->state.turn_kneebend_facing_override[idx] = 0u;
      batch->state.motion_entry_instance_id_override[idx] = 0u;
      batch->state.combat_shield_hit_int_damage[idx] = 0u;
      batch->state.combat_shield_damage_taken[idx] = 0u;
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBOUND_STOP &&
          batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBOUND) {
        batch->state.rebound_ground_accel_2[idx] = 0.0f;
        batch->state.rebound_anim_rate_fp_q16_16[idx] = 0;
      }
      // `seed_t.walljump_*` can bridge a hidden CollData WallHug phase for one-step replay rows.
      // Runtime after that step must require live mpColl WallHug bits again.
      // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
      batch->state.walljump_seed_phase_valid[idx] = 0u;
      if (!msl_specialhi_rotate_model_action(batch->state.action_id[idx])) {
        batch->state.specialhi_rotate_model_valid[idx] = 0u;
        batch->state.specialhi_rotate_model[idx] = 0.0f;
      }
    }
    // Teacher-forced shield-contact lanes are one-step reseed surfaces. Normal rollouts must
    // return to live shield geometry / collision ordering after the seeded frame.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    const size_t shield_base =
        ((size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS);
    for (int attacker = 0; attacker < num_players; attacker++) {
      const size_t a_idx = msl_idx_player(bi, attacker);
      // Authoritative per-HitCapsule seed-valid lanes are teacher-forced visibility for the
      // reseeded frame. Runtime rollout should then use live `fighter_hitlist` state, except when
      // the attacker remains hitlag-frozen and Fighter_8006A360 skips the script/update path that
      // would otherwise materialize or clear the live capsule. Leaving old per-HitCapsule seed
      // lanes valid after the source episode can make ftColl_80078C70 reject a later unrelated
      // hitbox-vs-hitbox clank candidate.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      const uint8_t preserve_hitlag_frozen_hitbox_seed =
          (batch->state.hitlag_pre_timer[a_idx] != 0u || batch->state.hitlag[a_idx] != 0u) ? 1u
                                                                                           : 0u;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t hb_valid_i =
            ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
            (size_t)hb;
        if (!preserve_hitlag_frozen_hitbox_seed) {
          batch->state.combat_hitlist_hb_valid[hb_valid_i] = 0u;
        }
        for (int victim = 0; victim < num_players; victim++) {
          const size_t si =
              shield_base + (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb) *
                                 (size_t)MSL_MAX_PLAYERS +
                             (size_t)victim);
          batch->state.combat_shield_contact_hb_kind[si] = 0u;
          if (!preserve_hitlag_frozen_hitbox_seed) {
            batch->state.combat_hitlist_hb_cd[si] = 0u;
            batch->state.combat_hitlist_hb_victim_iid[si] = 0u;
          }
        }
      }
    }
  }
}

static inline void promote_seed_prev_action_snapshot_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.seed_prev_action_id[idx] = batch->state.prev_action_id[idx];
      batch->state.seed_prev_action_frame[idx] = batch->state.prev_action_frame[idx];
    }
  }
}

static inline void sync_runbrake_cmd0_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_RUN_BRAKE) {
        batch->state.runbrake_cmd0[idx] = 0u;
        continue;
      }
      // Decomp: RunBrake cmd_vars[0] is owned by the common RunBrake action script and consumed by
      // RunBrake_IASA before the TurnRun branch.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
      //   ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
      // Source of truth:
      // - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0).
      batch->state.runbrake_cmd0[idx] = move_tables_runbrake_cmd0_active(
          batch->state.char_id[idx], batch->state.anim_frame_f32[idx]);
    }
  }
}

static inline void cache_prev_action_state(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.prev_action_id[idx] = batch->state.action_id[idx];
      batch->state.prev_action_frame[idx] = batch->state.action_frame[idx];
      batch->state.frame_start_attack_id[idx] = batch->state.attack_id[idx];
      batch->state.frame_start_attack_instance[idx] = batch->state.attack_instance[idx];
      batch->state.frame_start_instance_id[idx] = batch->state.instance_id[idx];
      batch->state.frame_start_on_ground[idx] = batch->state.on_ground[idx] ? 1u : 0u;
    }
  }
}

static inline void cache_floor_sweep_prev_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp: ft_80081DD4-style map callbacks preserve the previous CollData position as the start
  // of the collision sweep, then write the fighter's current position before calling mpColl. The
  // replay seed surface provides that previous sweep endpoint explicitly.
  //
  // In rollout, do not overwrite the current sweep endpoint with the current root at frame start:
  // DamageFly floor-contact rows can need the prior frame's sweep root to see the floor crossing.
  // The next endpoint is promoted after physics records prev_pos_* for this frame.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpCheckFloor}
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.floor_sweep_seed_prev_valid[idx]) {
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.floor_sweep_seed_prev_pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.floor_sweep_seed_prev_pos_y[idx];
        batch->state.floor_sweep_seed_prev_valid[idx] = 0u;
      } else if (!isfinite(batch->state.floor_sweep_prev_pos_x[idx]) ||
                 !isfinite(batch->state.floor_sweep_prev_pos_y[idx])) {
        batch->state.floor_sweep_prev_pos_x[idx] = batch->state.pos_x[idx];
        batch->state.floor_sweep_prev_pos_y[idx] = batch->state.pos_y[idx];
      }
    }
  }
}

static inline void promote_floor_sweep_prev_pos_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.floor_sweep_prev_pos_x[idx] = batch->state.prev_pos_x[idx];
      batch->state.floor_sweep_prev_pos_y[idx] = batch->state.prev_pos_y[idx];
    }
  }
}

static inline void cache_guard_reflect_timer_seed_snapshots(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.guard_reflect_timer_x14_seed[idx] = batch->state.guard_reflect_timer_x14[idx];
      batch->state.guard_reflect_timer_x18_seed[idx] = batch->state.guard_reflect_timer_x18[idx];
      const size_t flags_221b_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      batch->state.guard_seed_shield_desc_active[idx] =
          ((batch->state.state_flags[flags_221b_i] &
            (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u)
              ? 1u
              : 0u;
    }
  }
}

static inline void cache_collision_stage_prev_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp: the ledge-grab AABB builders (mpColl_80044164 / mpColl_800443C4) consume CollData.prev_pos
  // and CollData.cur_pos as managed inside the collision substep loop (mpColl_80043754).
  //
  // This sim does not yet implement mpColl substeps. Approximate the collision-stage prev/cur pair:
  // - coll_stage_prev_pos: position at start of collision stage this frame
  // - coll_stage_cur_pos: position immediately after stage_collision_apply() this frame
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_prev_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static inline void cache_collision_stage_cur_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_cur_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_cur_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static inline void cache_frame_start_state_flags_2218(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.state_flags_2218_frame_start[idx] =
          batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                   (size_t)MSL_STATE_FLAGS_2218_INDEX];
    }
  }
}

static inline void cache_damagefly_hitlag_exit_sweep_root(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] == 0u ||
          !msl_motion_state_common_class_has(batch->state.action_id[idx],
                                             MSL_MS_CLASS_DAMAGE_FLY)) {
        continue;
      }
      // Hitlag-exit ownership:
      // - Fighter_8006A1BC ends hitlag and ftCo_Damage_OnExitHitlag applies ASDI before Phys/Coll.
      // - ft_80081DD4 then copies the fighter's post-callback cur_pos into CollData.cur_pos while
      //   retaining CollData.prev_pos as the pre-callback sweep root for mpColl.
      // Cache that root before timer callbacks mutate position; physics_integrate preserves it on
      // the exit frame so DamageFlyRoll_Coll can see wall Hug from the ASDI displacement.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static void fighter_callbacks_begin_frame_phase(MslBatch* batch) {
  clear_landing_transients(batch);
  cache_prev_action_state(batch);
  cache_floor_sweep_prev_pos(batch);
  cache_guard_reflect_timer_seed_snapshots(batch);
  cache_frame_start_state_flags_2218(batch);
  cache_damagefly_hitlag_exit_sweep_root(batch);
}

static int fighter_callbacks_pre_input_anim_phase(MslBatch* batch, const uint8_t* prev_input_bytes,
                                                  size_t prev_input_stride_bytes) {
  // Decomp callback ordering: pre-input anim callbacks (prio1) run before input_cb (prio3), so
  // snapshot prior-frame inputs for pre-input gameplay ownership.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  int err = input_apply_pre_input_snapshot(batch, prev_input_bytes, prev_input_stride_bytes);
  if (err != 0) {
    return err;
  }

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  //
  // Decomp ordering note:
  // - refs/melee/src/melee/ft/fighter.c registers `Fighter_8006A1BC` (timer decrement) at proc prio 0,
  //   before input processing, animation advancement, and physics (`Fighter_procUpdate`).
  // We follow that by updating timers before applying inputs/advancing state.
  timers_update(batch);

  // Damage post-hitlag callback consume (subset) at proc prio 0 before current-frame input.
  // Decomp:
  // - Fighter_8006A1BC decrements hitlag and invokes Fighter_8006D10C when hitlag ends,
  // - damage path uses ftCo_Damage_OnExitHitlag.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
  timers_consume_post_hitlag_callbacks_pre_input(batch);

  // Match flow (Entry/Death/Rebirth) updates its own timers and can change action state before
  // animation timebase advancement.
  match_flow_update_pre_anim(batch);

  // Decomp: animation/script timebase advances at proc prio 1 (Fighter_8006A360) before input
  // (prio 3). Advance our deterministic cur_anim_frame accumulator here, after timers.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (ftAnim_8006EBA4 under !hitlag)
  anim_timebase_update_pre_input(batch);

  // Post anim-timebase match flow clamps (e.g. EntryStart animation end-frame).
  match_flow_update_post_anim(batch);

  // Post-anim timer updates (decomp prio 1 under Fighter_8006A360's non-hitlag gate):
  // - combo timer tick + victim clear (ftColl_800764DC)
  // - hitstun decrement + end effects (ftCo_8008F744 family)
  //
  // NOTE(anim_timebase_mapping):
  // `anim_timebase_update_pre_input()` in this sim only advances the deterministic `cur_anim_frame`
  // timebase (Slippi `state_age`) and applies AObj loop wrap. It does *not* run per-action Anim
  // callbacks. Those state-specific updates are modeled later in `action_update()`.
  //
  // This placement means timers_update_post_anim() observes the post-advance `action_frame`, while
  // still running before action_update(), matching the decomp ordering where ftColl_800764DC runs
  // before the per-action anim_cb within Fighter_8006A360.
  timers_update_post_anim(batch);

  // Per-action Anim-callback phase (subset) before input processing.
  // Decomp: Fighter_8006A360 (prio 1) runs anim callbacks before Fighter_procUpdate input_cb (prio 3).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  //
  // Ordering rationale vs ProcessHit consume:
  // - This phase currently owns GuardReflect x14/x18 timer tick/expire (ftCo_GuardReflect_Anim path),
  //   which is a prio-1 anim callback update.
  // - combat_processhit_consume() models the post-collision ProcessHit-style cleanup (decomp prio 14),
  //   so it should remain after prio-1 callback effects.
  items_update_pre_fighter_anim_phase(batch);
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const MslFighterCallbackContext ctx = msl_fighter_callback_context_make(
          batch, bi, p, num_players, MSL_FIGHTER_CALLBACK_PHASE_PRE_INPUT_ANIM);
      action_update_anim_callback_pre_input_fighter(&ctx);
    }
  }
  action_update_anim_callbacks_pre_input_global(batch);

  // Decomp-shaped "ProcessHit" consume / cleanup (see combat_processhit_consume for references).
  combat_processhit_consume(batch);
  return 0;
}

static int fighter_callbacks_input_phase(MslBatch* batch, const uint8_t* prev_input_bytes,
                                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                                         size_t input_stride_bytes) {
  int err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                        input_stride_bytes);
  if (err != 0) {
    return err;
  }

  // Damage hitlag callback consume (subset) after input apply and before collision ownership.
  // Decomp:
  // - hitlag callbacks (`hitlag_cb`) execute during Fighter_procUpdate after current-frame input,
  // - damage path uses ftCo_Damage_OnEveryHitlag while hitlag is still active.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  timers_consume_post_hitlag_callbacks_after_input(batch);
  return 0;
}

static void fighter_callbacks_iasa_phase(MslBatch* batch) {
  // Match flow IASA: certain match-flow states can exit based on current-frame inputs.
  // Decomp: motion state IASA callbacks run after Anim and before Phys/Coll.
  // NOTE: match_flow_update_post_input currently includes simplified approximations (see match_flow.c).
  match_flow_update_post_input(batch);

  action_update(batch);
}

static void fighter_callbacks_phys_phase(MslBatch* batch) {
  // Fighter-driven item spawns (blaster guns + shots) are evaluated before physics integration so
  // they use the pre-physics fighter pose/position snapshot (decomp: prio1 Anim vs prio4 Update).
  items_spawn_fighter_anim_phase(batch);
  state_flags_refresh_camera_targets_pre_physics(batch);
  physics_integrate(batch);
}

static void fighter_callbacks_collision_phase(MslBatch* batch) {
  // NOTE(grabbed-victim-coll):
  // - CapturePulled*/CaptureDamage* uses a Phys position driver (fn_800DAD18) before Coll.
  // - In this simulator, the capture delta is applied in grab_attachment_update_pre_collision()
  //   (pre-collision). physics_integrate() skips self/KB integration for those victims to avoid
  //   double-moving them in a single frame.
  // - Thrown victims are still updated in a post-collision "accessory callback" style slot.
  grab_attachment_update_pre_collision(batch);
  cache_collision_stage_prev_pos(batch);
  stage_collision_apply(batch);
  cache_collision_stage_cur_pos(batch);
  // Collision environment flags (mpColl-shaped): owns Collide_LedgeGrabMask for scheduling ledge
  // catch after collision.
  mpcoll_env_update_ledge_grab(batch);
  grab_attachment_update_post_collision(batch);
  ledge_try_catch_post_collision(batch);
  knockdown_update_post_collision(batch);
  physics_apply_attackdash_downbound_overlap_nudge_post_collision(batch);
  match_flow_update_post_physics(batch);
  blaster_update_post_collision(batch);
  locomotion_update_post_collision(batch);
  shine_update_post_collision(batch);
}

static void fighter_callbacks_primitive_refresh_phase(MslBatch* batch, uint8_t run_combat) {
  // Motion-state entry owners that request an immediate ftAnim_8006EBA4-equivalent tick must do so
  // before primitive refresh. Otherwise hurtcaps/hitboxes are built from the entry-start pose while
  // the post-frame action_frame already reflects the advanced AObj timebase.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  anim_timebase_apply_deferred_tick_once_pre_collision(batch);
  // Fighter dynamic JObj chains update after animation/physics callbacks and before collision
  // primitive refresh, matching ftCo_8009DD94 feeding lb_8000B1CC consumers.
  // refs/melee/src/melee/ft/ftdynamics.c::ftCo_8009DD94
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  anim_pose_update_dynamic_state(batch);
  // Guard/Shield animation owner:
  // - ftCo_Guard_Anim calls ftCo_800925A4/ftCo_80091BC4 and ftCo_80091E78 before fighter
  //   collision. Keep the pre-combat refresh scoped to the angled no-submotion Guard BODY owner;
  //   the full ShieldDesc/state-flag refresh stays in its established post-hitlist slot below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_800925A4,ftCo_80091BC4,ftCo_80091E78}
  shields_refresh_guard_tilt_body_owner(batch);
  if (run_combat) {
    // Fighter hitbox refresh needs the current-frame merged hurtbox state for no-damage contact
    // carry, but BODY/catch/item collision only consumes endpoint geometry when an item or opposing
    // HitCapsule exists. Keep the decomp-shaped state/mode pass unconditional and defer the
    // lb_8000B1CC-style endpoint matrix sampling until the current frame has contact demand.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B868,ftColl_80076ED8,ftColl_80078A2C}
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    hurtboxes_refresh_metadata(batch);
    hitboxes_refresh(batch);
    hurtboxes_refresh_contact_geometry(batch);
  } else {
    // Debug pre-combat must expose the same primitive refresh state that full combat consumes.
    // Keep only combat_resolve()/post-combat mutations skipped.
    hurtboxes_refresh_metadata(batch);
    hitboxes_refresh(batch);
    hurtboxes_refresh_contact_geometry(batch);
  }
}

static void fighter_callbacks_item_collision_and_combat_phase(MslBatch* batch, uint8_t run_combat) {
  hitlist_tick(batch);
  shields_refresh(batch);
  reflector_bubbles_refresh(batch);
  items_update_collision_phase(batch);
  throw_flow_update_post_items(batch);
  if (run_combat) {
    combat_resolve(batch);
    items_update_post_combat(batch);
  }
  knockdown_update_post_combat(batch);
}

static void fighter_callbacks_post_frame_phase(MslBatch* batch) {
  anim_timebase_apply_deferred_tick_once_post_combat(batch);
  sync_runbrake_cmd0_post_frame(batch);
  state_flags_refresh_post_frame(batch);
  timers_update_magnify_damage_post_frame(batch);
  promote_floor_sweep_prev_pos_post_frame(batch);
  promote_seed_prev_action_snapshot_post_frame(batch);
  clear_seed_owned_transients_post_frame(batch);
}

int fighter_callbacks_step_frame(MslBatch* batch, const uint8_t* prev_input_bytes,
                                 size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                                 size_t input_stride_bytes, uint8_t run_combat) {
  if (batch == NULL) {
    return EINVAL;
  }

  combat_rng_trace_begin_frame(batch);
  fighter_callbacks_begin_frame_phase(batch);
  int err =
      fighter_callbacks_pre_input_anim_phase(batch, prev_input_bytes, prev_input_stride_bytes);
  if (err != 0) {
    combat_rng_trace_end_frame(batch);
    return err;
  }
  err = fighter_callbacks_input_phase(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                      input_stride_bytes);
  if (err != 0) {
    combat_rng_trace_end_frame(batch);
    return err;
  }
  fighter_callbacks_iasa_phase(batch);
  fighter_callbacks_phys_phase(batch);
  fighter_callbacks_collision_phase(batch);
  fighter_callbacks_primitive_refresh_phase(batch, run_combat);
  fighter_callbacks_item_collision_and_combat_phase(batch, run_combat);
  fighter_callbacks_post_frame_phase(batch);
  combat_rng_trace_end_frame(batch);
  return 0;
}
