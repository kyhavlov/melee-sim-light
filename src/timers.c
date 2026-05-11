#include "timers.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "common_params.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "stage_collision.h"

enum { TIMERS_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };

static inline uint8_t timers_match_flow_dead_action_defers_source_clear(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
    case MSL_ACT_DEAD_UP_STAR:
    case MSL_ACT_DEAD_UP_FALL:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT:
    case MSL_ACT_DEAD_UP_FALL_ICE:
    case MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_ICE:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t timers_source_clear_downed_recovery_terminal_parks_owner(
    const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[idx];
  switch (action) {
    case MSL_ACT_DOWN_BOUND_U:
    case MSL_ACT_DOWN_WAIT_U:
    case MSL_ACT_DOWN_STAND_U:
    case MSL_ACT_DOWN_ATTACK_U:
    case MSL_ACT_DOWN_FOWARD_U:
    case MSL_ACT_DOWN_BACK_U:
    case MSL_ACT_DOWN_BOUND_D:
    case MSL_ACT_DOWN_WAIT_D:
    case MSL_ACT_DOWN_STAND_D:
    case MSL_ACT_DOWN_ATTACK_D:
    case MSL_ACT_DOWN_FOWARD_D:
    case MSL_ACT_DOWN_BACK_D:
    case MSL_ACT_PASSIVE:
    case MSL_ACT_PASSIVE_STAND_F:
    case MSL_ACT_PASSIVE_STAND_B:
      break;
    default:
      return 0u;
  }

  if (batch->state.source_clear_owner_set_phase[idx] == 0u || batch->state.combo_count[idx] == 0u ||
      batch->state.last_attack_landed[idx] == 0u || batch->state.hitlag[idx] != 0u ||
      batch->state.hitstun[idx] != 0u) {
    return 0u;
  }

  const uint16_t prev_action = batch->state.prev_action_id[idx];
  const int16_t prev_frame = batch->state.prev_action_frame[idx];
  const int16_t action_frame = batch->state.action_frame[idx];
  if (prev_action != action || prev_frame < 0 || action_frame != (int16_t)(prev_frame + 1)) {
    return 0u;
  }

  // Decomp owner:
  // - Fighter_ChangeMotionState starts x18C8 on grounded x9_b1 motions.
  // - Fighter_8006A360 decrements that live timer in the main fighter proc.
  // - The downed/passive recovery family (`ftCo_Down*` / `ftCo_Passive*`) can inherit an already
  //   live x18C8 run from an earlier grounded motion; when the replay-visible terminal row is
  //   reached, source ownership parks with the timer inactive rather than clearing during rollout.
  // This mirrors the one-step terminal phase producer for the same decomp callback family without
  // keying on a dataset, replay, or row id.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Passive.c,ftCo_PassiveStand.c}
  // refs/melee/src/melee/ft/ftmotionstates.c (Down*/Passive* callback rows)
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  return 1u;
}

void timers_update(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // Dataset packing/layout reference:
  // tools/slippi/make_dataset_from_slp.py (stack order 0..4 into `state_flags[..., 5]`)
  // Slippi records fp+0x221A directly. The replay-visible 0x20 bit is the engine's hitlag-active
  // lane (`x221A_b2` in the decomp comments / Slippi docs), not `allow_sdi`. Runtime SDI callbacks
  // consume the explicit internal `damage_allow_sdi` lane instead.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };
  // fp+0x221A bit 0x10 corresponds to x221A_b3 in decomp (see refs/melee/src/melee/ft/types.h).
  // Fighter_ProcessHit can set it alongside hitlag start, and Fighter_8006A1BC clears it on hitlag end.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };

  // Decomp-first references (GALE01):
  //
  // Hitlag:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //   decrements `fp->dmg.x195c_hitlag_frames` by 1.0f each frame and clamps at 0.0f.
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //   clears the per-fighter hitlag flag `fp->x221A_b2 = 0` when hitlag reaches 0.
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //   sets `fp->x221A_b2 = 1` when `fp->dmg.x195c_hitlag_frames > 0.0f`.
  // - refs/melee/src/melee/ft/types.h
  //   declares `x221A_b2` as a 1-bit field at fp+0x221A (the byte Slippi records into `state_flags[...,1]`).
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //   records hitlag frames left from offset 0x195c (`lwz r3,0x195c(REG_PlayerData)`).
  //
  // Hitstun frames left (as exposed by Slippi) live in the action-state motion var:
  // - refs/melee/src/melee/ft/chara/ftCommon/types.h::union ftCommon_MotionVars::damage.x0
  //   is at offset 0x2340.
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //   records "misc AS variable" from offset 0x2340 (`lwz r3,0x2340(REG_PlayerData)`),
  //   interpreted as hitstun frames left when the hitstun flag is set.
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  //   decrements `fp->mv.co.damage.x0` by 1 each call; it is invoked from
  //   `ftCo_Damage_Anim`, which is called via `anim_cb` only when not in hitlag
  //   (refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 has an `if (!fp->x2219_b5)` gate).
  //
  // Practical sim split:
  // - Hitlag decrements at proc prio 0 via Fighter_8006A1BC.
  // - Combo timer tick/clear and hitstun decrement happen later (prio 1, Fighter_8006A360) under
  //   the non-hitlag gate; see timers_update_post_anim().

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Pass 1: decrement hitlag + keep the Slippi "isHitlag" bit consistent with hitlag frames left.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      uint16_t hl = batch->state.hitlag[idx];
      batch->state.hitlag_pre_timer[idx] = (hl > 0u) ? 1u : 0u;
      // Decomp: hitlag frames are decremented at proc prio 0 before the main per-fighter update
      // block (Anim/Phys/Coll) runs.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
      if (hl > 0) {
        hl--;
        batch->state.hitlag[idx] = hl;
      }

      // Per-frame hitlag gate (see src/state.h for rationale).
      // Decomp update gate: refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (`if (!fp->x2219_b5)`).
      batch->state.hitlag_started_frame[idx] = (hl > 0) ? 1u : 0u;

      // Keep the Slippi `state_flags` "isHitlag" bit consistent with `hitlag` frames left.
      //
      // Decomp: `fp->x221A_b2` is toggled by the engine with hitlag start/end:
      // - set to 1 when hitlag is active (Fighter_ProcessHit_8006D1EC),
      // - cleared to 0 when hitlag reaches 0 (Fighter_8006A1BC).
      // refs/melee/src/melee/ft/fighter.c
      //
      // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      const size_t flags_221a_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
      uint8_t flags_221a = batch->state.state_flags[flags_221a_i];
      if (hl > 0) {
        flags_221a |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
      } else {
        flags_221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
        batch->state.damage_allow_sdi[idx] = 0u;
        // Decomp: when hitlag ends, Fighter_8006A1BC clears x221A_b2 (isHitlag) and, if set,
        // clears x221A_b3 after calling ftCo_80090718(fp).
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
        flags_221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B3;
      }
      batch->state.state_flags[flags_221a_i] = flags_221a;
    }
  }
}

static inline uint8_t damage_post_hitlag_cb_owner_action(uint16_t a) {
  switch (a) {
    // Modeled ownership subset for this pass:
    // - Damage entry writes `post_hitlag_cb = ftCo_Damage_OnExitHitlag` for common Damage states.
    // - Include grounded DamageHi/N/Lw + DamageFly* + DamageFall ownership lanes; keep DamageAir*
    //   deferred until its full callback/collision handoff lane is modeled.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    //   ftCo_DamageFly_Coll,ftCo_Damage_Anim,ftCo_DamageFly_Anim
    // }
    // - DownDamage re-enters the common damage pipeline through ftCo_8009F184 ->
    //   ftCo_8008DCE0 and reuses ftCo_Damage_Phys.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_8009F184,ftCo_DownDamage_Phys}
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
    case MSL_ACT_DAMAGE_FALL:
    case MSL_ACT_DOWN_DAMAGE_D:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t damage_post_hitlag_cb_damagefly_action(uint16_t a) {
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t timers_first_active_sdi_allows_radius_crossing(const MslBatch* batch, int bi,
                                                                     size_t idx) {
  (void)bi;
  if (batch == NULL) {
    return 0u;
  }
  if (!damage_post_hitlag_cb_damagefly_action(batch->state.action_id[idx])) {
    return 1u;
  }

  // Fresh common-Damage hitlag can consume a callback-local stick-radius crossing even when
  // ftCo_8008DCE0 has reset replay-visible x670/x671 to 0xFE. DamageFlyRoll's active-hitlag entry
  // is a narrower DamageFly owner: it must use the seeded/live x670/x671 timer-window path, not the
  // generic first-active radius bridge. Otherwise replay rollouts can apply SDI one frame early and
  // move a DamageFlyRoll victim during the frozen hitlag entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag,ftCo_DamageFlyRoll_Coll}
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
    return 0u;
  }

  // Floor-owned horizontal DamageFlyN rows are excluded when the seed carries an explicit
  // CollData_X130_Locked / ECB-lock floor-contact owner (`ecb_lock_timer` plus a persisted floor
  // index). A root-vs-floor-height bias is not source provenance.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80046904}
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_N &&
      batch->state.ground_id[idx] != 0xFFFFu && batch->state.ecb_lock_timer[idx] != 0u) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t damage_every_hitlag_sdi_timer_window_action(uint16_t a) {
  switch (a) {
    // DownDamageD re-enters ftCo_8008DCE0 via ftCo_8009F184 and owns the same per-hitlag SDI
    // callback, but it is not part of the common Damage* / DamageFly* action-id block above.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
    //   ftCo_8009F184,ftCo_DownDamage_Phys}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    case MSL_ACT_DOWN_DAMAGE_D:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t damage_timer_down_damage_action(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_DAMAGE_U || a == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) ? 1u : 0u;
}

static inline void damage_hitlag_exit_wall_project_asdi(const MslBatch* batch, size_t idx,
                                                        float* dx, float* dy) {
  if (batch == NULL || dx == NULL || dy == NULL ||
      !damage_post_hitlag_cb_damagefly_action(batch->state.action_id[idx]) ||
      batch->state.damage_hitlag_wall_asdi_latch[idx] == 0u ||
      batch->state.wall_id[idx] == 0xFFFFu) {
    return;
  }

  // Decomp ownership:
  // - ftCo_Damage_OnExitHitlag applies ASDI displacement before ftCo_8008E5A4 DI.
  // - Fighter_procMap still runs the DamageFly collision callback during hitlag; mpcoll_wall_ceil.c
  //   latches that phase-local wall evidence because CollData.wall.index persists after detach and
  //   cannot be used as provenance alone. Project the immediate ASDI displacement onto that wall
  //   tangent instead of letting the normal component create a stale PassiveWall cascade.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}
  const int bi = (int)(idx / (size_t)batch->config.num_players);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const uint16_t wall_id = batch->state.wall_id[idx];
  if (stage_collision_left_wall_line_index(stage_id, wall_id) >= 0 && *dx > 0.0f) {
    *dx = 0.0f;
    return;
  }
  if (stage_collision_right_wall_line_index(stage_id, wall_id) >= 0 && *dx < 0.0f) {
    *dx = 0.0f;
  }
}

static inline uint8_t guard_setoff_post_hitlag_cb_owner_action(uint16_t a) {
  return a == (uint16_t)MSL_ACT_GUARD_SET_OFF ? 1u : 0u;
}

void timers_consume_post_hitlag_callbacks_pre_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const float sdi_radius_sq = c->sdi_radius * c->sdi_radius;
  const float asdi_step_mul = c->asdi_step_mul;
  const float di_max_radians = c->di_max_deg * 0.017453292519943295f;  // pi / 180

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];

      if (!damage_post_hitlag_cb_owner_action(a) && !guard_setoff_post_hitlag_cb_owner_action(a)) {
        continue;
      }
      if (!(batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u)) {
        continue;
      }

      if (guard_setoff_post_hitlag_cb_owner_action(a)) {
        // GuardSetOff post-hitlag callback:
        // - ftCo_80092F2C installs `post_hitlag_cb = ftCo_800932DC` on GuardSetOff entry.
        // - Fighter_8006A1BC calls Fighter_8006D10C when hitlag reaches zero, before
        //   Fighter_procUpdate refreshes current-frame input, so this consumes the prior input
        //   snapshot just like Damage_OnExitHitlag above.
        // - ftCo_800932DC only moves grounded fighters and displaces along the floor tangent by
        //   floor.normal * (lstick.x * x4BC * x4C0).
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C,Fighter_procUpdate}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_800932DC}
        // data/common/ft_common_data.json::{sdi_radius,asdi_step_mul,shield_sdi_mul}
        if (!batch->state.on_ground[idx]) {
          continue;
        }
        const float lstick_x = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
        if (fabsf(lstick_x) < c->sdi_radius) {
          continue;
        }
        const float nx = batch->state.ground_normal_x[idx];
        const float ny =
            (batch->state.ground_normal_y[idx] != 0.0f) ? batch->state.ground_normal_y[idx] : 1.0f;
        const float scl = c->shield_sdi_mul * (lstick_x * c->asdi_step_mul);
        batch->state.pos_x[idx] += ny * scl;
        batch->state.pos_y[idx] += -nx * scl;
        continue;
      }

      // `Fighter_8006D10C` is invoked from `Fighter_8006A1BC` before Fighter_procUpdate input_cb,
      // so Damage_OnExitHitlag must consume the prior-frame input snapshot.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C,Fighter_procUpdate}
      //
      // The engine's hitlag gate (`x2219_b5`) freezes Fighter_8006A360 without zeroing
      // `frame_speed_mul`; when hitlag reaches zero, the same prio-1 update can immediately tick
      // Damage/DamageFly animation. Replay seeds expose a zero visible rate while frozen, so
      // restore the source-owned hidden rate on the exit frame before anim_timebase_update_pre_input
      // consumes it.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_8006D10C}
      if (batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = (int32_t)MSL_Q16_16_ONE;
      }

      const float lstick_x = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
      const float lstick_y = stick_i8_to_unit(batch->state.prev_input_main_y[idx]);
      const float lstick_full_x = apply_deadzone(lstick_x, c->lstick_deadzone_x);
      const float lstick_full_y = apply_deadzone(lstick_y, c->lstick_deadzone_y);
      const float cstick_x = stick_i8_to_unit(batch->state.prev_input_c_x[idx]);
      const float cstick_y = stick_i8_to_unit(batch->state.prev_input_c_y[idx]);
      const float cstick_full_x = apply_deadzone(cstick_x, c->lstick_deadzone_x);
      const float cstick_full_y = apply_deadzone(cstick_y, c->lstick_deadzone_y);
      const float lstick_full_mag_sq =
          lstick_full_x * lstick_full_x + lstick_full_y * lstick_full_y;
      const float cstick_full_mag_sq =
          cstick_full_x * cstick_full_x + cstick_full_y * cstick_full_y;
      const uint8_t use_cstick = (cstick_full_mag_sq >= sdi_radius_sq) ? 1u : 0u;
      const uint8_t use_lstick = (lstick_full_mag_sq >= sdi_radius_sq) ? 1u : 0u;
      if (!use_cstick && !use_lstick) {
        // DI/LSI still apply on hitlag exit even when ASDI stick displacement does not.
      } else {
        const float dx = (use_cstick ? cstick_full_x : lstick_full_x) * asdi_step_mul;
        const float dy = (use_cstick ? cstick_full_y : lstick_full_y) * asdi_step_mul;
        float projected_dx = dx;
        float projected_dy = dy;
        damage_hitlag_exit_wall_project_asdi(batch, idx, &projected_dx, &projected_dy);
        batch->state.pos_x[idx] += projected_dx;
        batch->state.pos_y[idx] += projected_dy;
      }
      batch->state.damage_hitlag_wall_asdi_latch[idx] = 0u;

      // DI: rotate kb velocity by up to x1A8 degrees based on L-stick and current kb direction.
      //
      // Grounded Damage note:
      // - ftCo_Damage_OnExitHitlag mutates fp->x8c_kb_vel through ftCo_8008E5A4.
      // - Later in the same Fighter_procUpdate, grounded knockback decay consumes
      //   fp->xF0_ground_kb_vel and rebuilds fp->x8c_kb_vel from that scalar and the floor tangent.
      // - This simulator currently has only the replay/output knockback velocity lane, not a
      //   separate xF0 scalar. While grounded, preserve that lane as the xF0-owned value so
      //   hitlag-exit DI/LSI does not incorrectly change the grounded slide speed.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_8008E5A4}
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (ground branch xF0_ground_kb_vel)
      if (!batch->state.on_ground[idx]) {
        float kb_x = batch->state.speed_x_attack[idx];
        float kb_y = batch->state.speed_y_attack[idx];
        const float kb_mag_sq = kb_x * kb_x + kb_y * kb_y;
        if (kb_mag_sq >= 0.00001f) {
          const float kb_neg_x = -kb_x;
          const float dot = kb_y * lstick_full_x + kb_neg_x * lstick_full_y;
          float dir = (dot * dot) / kb_mag_sq;
          const float cross_z = kb_x * lstick_full_y - kb_y * lstick_full_x;
          if (cross_z < 0.0f) {
            dir = -dir;
          }
          const float kb_angle = atan2f(kb_y, kb_x) + di_max_radians * dir;
          const float kb_mag = sqrtf(kb_mag_sq);
          kb_x = kb_mag * cosf(kb_angle);
          kb_y = kb_mag * sinf(kb_angle);

          // LSI: apply x1AC multiplier when digital L/R is held at hitlag exit.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
          if ((batch->state.prev_input_buttons[idx] &
               ((uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R)) != 0u) {
            kb_x *= c->lsi_lr_held_mul;
            kb_y *= c->lsi_lr_held_mul;
          }
          batch->state.speed_x_attack[idx] = kb_x;
          batch->state.speed_y_attack[idx] = kb_y;
        }
      }
    }
  }
}

void timers_consume_post_hitlag_callbacks_after_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Decomp callback ownership:
  // - Damage hitlag runs the generic `ftCo_Damage_OnEveryHitlag` callback while allow_sdi is
  //   active. That callback applies full 2D `cur_pos += lstick * sdi_pos_scale`; it does not
  //   branch by action family inside the callback.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // - `hitlag_cb` executes inside Fighter_procUpdate after current-frame input processing.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // Current sim contract:
  // - Runtime owns `allow_sdi` through `state.damage_allow_sdi`, set by ProcessHit hitlag start
  //   paths and cleared by Fighter_8006A1BC-shaped hitlag exit above.
  // - Teacher-forced reseed initializes that lane from replay-visible ProcessHit provenance, then
  //   free-running steps use the internal lane rather than the hitlag-active state byte.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+221A:2 allow_sdi, fp+221A:3 x221A_b3)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c:824 (allow_sdi can be set without
  // x221A_b3 on attached/secondary hitlag paths)
  const float sdi_radius_sq = c->sdi_radius * c->sdi_radius;
  const float sdi_step_mul = c->sdi_step_mul;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.damage_hitlag_downward_sdi_consumed[idx] = 0u;
      const uint16_t a = batch->state.action_id[idx];
      if (!damage_post_hitlag_cb_owner_action(a)) {
        continue;
      }

      const float lstick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
      const float lstick_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
      const float lstick_full_x = apply_deadzone(lstick_x, c->lstick_deadzone_x);
      const float lstick_full_y = apply_deadzone(lstick_y, c->lstick_deadzone_y);
      const float lstick_mag_sq = lstick_x * lstick_x + lstick_y * lstick_y;
      const float prev_lstick_x = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
      const float prev_lstick_y = stick_i8_to_unit(batch->state.prev_input_main_y[idx]);
      const float prev_lstick_mag_sq =
          prev_lstick_x * prev_lstick_x + prev_lstick_y * prev_lstick_y;
      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
      const uint8_t sdi_tilt_window = (batch->state.tilt_timer_x[idx] < c->sdi_tilt_max_frames ||
                                       batch->state.tilt_timer_y[idx] < c->sdi_tilt_max_frames)
                                          ? 1u
                                          : 0u;
      // Probed callback-local owner for fresh common-Damage hitlag:
      // - `ftCo_8008DCE0` resets x670/x671 to 0xFE on entry, but Dolphin probes show
      //   `ftCo_Damage_OnEveryHitlag` can still consume the callback-local radius crossing on the
      //   first active common-Damage hitlag row (PPA:2185).
      // - Active DamageFly rows with the same replay-visible shape are not equivalent: BHH:1600
      //   stays frozen with x670/x671 reset despite a radius crossing. Keep DamageFly on the source
      //   timer-window path until its separate callback/input snapshot owner is modeled.
      //
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag}
      const uint8_t use_first_active_radius_crossing =
          (timers_first_active_sdi_allows_radius_crossing(batch, bi, idx) &&
           batch->state.action_frame[idx] == 1 && batch->state.tilt_timer_x[idx] == 254u &&
           batch->state.tilt_timer_y[idx] == 254u && batch->state.seed_prev_action_id[idx] != a &&
           lstick_mag_sq >= sdi_radius_sq && prev_lstick_mag_sq < sdi_radius_sq)
              ? 1u
              : 0u;
      const uint8_t flags_221a = batch->state.state_flags[flags_i];
      // Source predicate:
      // - ftCo_Damage_OnEveryHitlag's timer-window path is generic and gated by hidden `allow_sdi`.
      // - Runtime uses `damage_allow_sdi`; action-family/phantom distinctions only participate in
      //   teacher-forced reseed initialization for that lane.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
      const uint8_t allow_sdi = batch->state.damage_allow_sdi[idx] ? 1u : 0u;
      const uint8_t use_timer_window = allow_sdi ? sdi_tilt_window : 0u;
      // This callback runs before the end-of-frame state-flag refresh that mirrors active hitlag
      // into Slippi's fp+0x221A 0x20 byte. Use the runtime hitlag timers as the current-frame
      // derived hitlag-active owner; keep the allow-SDI provenance above separate.
      //
      // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_8006A1BC}
      const uint8_t is_current_hitlag_active =
          ((flags_221a & (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG) != 0u ||
           batch->state.hitlag[idx] != 0u)
              ? 1u
              : 0u;
      if (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u && allow_sdi &&
          (use_timer_window || use_first_active_radius_crossing) && is_current_hitlag_active &&
          lstick_mag_sq >= sdi_radius_sq) {
        batch->state.pos_x[idx] += lstick_full_x * sdi_step_mul;
        batch->state.pos_y[idx] += lstick_full_y * sdi_step_mul;
        if (lstick_full_y < 0.0f) {
          batch->state.damage_hitlag_downward_sdi_consumed[idx] = 1u;
        }
        batch->state.tilt_timer_x[idx] = 254u;
        batch->state.tilt_timer_y[idx] = 254u;
      }
    }
  }
}

static inline uint8_t timers_magnify_live_fighter_action(uint16_t action_id) {
  // `fp->x221F_b0` is also used by match-flow/dead-flow camera-subject owners. The magnifying
  // damage branch gates on ifMagnify's offscreen state, so keep the x221F approximation to live
  // fighter states and exclude actions where the bit is camera/dead-flow bookkeeping.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
  switch (action_id) {
    case (uint16_t)MSL_ACT_DEAD_DOWN:
    case (uint16_t)MSL_ACT_DEAD_LEFT:
    case (uint16_t)MSL_ACT_DEAD_RIGHT:
    case (uint16_t)MSL_ACT_DEAD_UP_STAR:
    case (uint16_t)MSL_ACT_REBIRTH:
    case (uint16_t)MSL_ACT_REBIRTH_WAIT:
    case (uint16_t)MSL_ACT_ENTRY:
    case (uint16_t)MSL_ACT_ENTRY_START:
    case (uint16_t)MSL_ACT_ENTRY_END:
      return 0u;
    default:
      return 1u;
  }
}

void timers_update_magnify_damage_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL || c->magnify_damage_interval_frames == 0u || c->magnify_damage_amount == 0u) {
    return;
  }

  enum { MSL_STATE_FLAGS_STRIDE_LOCAL = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221F_INDEX_LOCAL = 4 };
  enum { MSL_STATE_FLAG_221F_B0_LOCAL = 0x80 };
  enum { MSL_STATE_FLAG_221F_B4_LOCAL = 0x08 };

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t replay_rollout =
        (batch->replay_rollout_reseeded != NULL && batch->replay_rollout_reseeded[bi] != 0u) ? 1u
                                                                                             : 0u;
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (replay_rollout != 0u && batch->state.magnify_damage_counter_x1910[idx] == 0u) {
        continue;
      }
      const uint8_t flags_221f = batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE_LOCAL +
                                                          (size_t)MSL_STATE_FLAGS_221F_INDEX_LOCAL];
      const uint8_t visible = (flags_221f & (uint8_t)MSL_STATE_FLAG_221F_B0_LOCAL) != 0u;
      const uint8_t disabled = (flags_221f & (uint8_t)MSL_STATE_FLAG_221F_B4_LOCAL) != 0u;
      const uint8_t offscreen =
          batch->state.camera_target_point_inside_stage_cam_bounds_u8[idx] == 0u ? 1u : 0u;
      if (!visible || !offscreen || disabled ||
          timers_magnify_live_fighter_action(batch->state.action_id[idx]) == 0u ||
          !(batch->state.percent[idx] < (float)c->magnify_damage_percent_limit)) {
        batch->state.magnify_damage_counter_x1910[idx] = 0u;
        continue;
      }

      uint32_t counter = (uint32_t)batch->state.magnify_damage_counter_x1910[idx] + 1u;
      if (counter >= (uint32_t)c->magnify_damage_interval_frames) {
        batch->state.percent[idx] += (float)c->magnify_damage_amount;
        counter = 0u;
      }
      batch->state.magnify_damage_counter_x1910[idx] =
          (uint16_t)(counter > 0xFFFFu ? 0xFFFFu : counter);
    }
  }
}

void timers_update_post_anim(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAGS_221F_INDEX = 4 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  enum { MSL_STATE_FLAG_221F_B3 = 0x10 };

  // Combo timer reset constant (GALE01 p_ftCommonData->x4CC).
  const MslCommonParams* c = msl_common_params();

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Decomp ordering inside Fighter_8006A360 (proc prio 1, under !fp->x221F_b3 gate):
    // - ftColl_800764DC (combo timer tick + victim clear) runs before per-action anim_cb.
    // - Damage anim_cb calls ftCo_8008F744 (hitstun decrement + end effects).
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744

    // Pass 0: Damage time-since-hit timer (fp->dmg.x18AC).
    //
    // Decomp:
    // - Fighter_8006A360 runs under `!fp->x2219_b5` (not in hitlag),
    // - if x18AC != -1, it increments before ftAnim_8006EBA4, ftColl_800764DC, and anim_cb,
    // - ftCo_8008DCE0 later resets x18AC to 0 on fresh Damage entry.
    //
    // Keep this before the other prio-1 timer/callback owners so current-frame ProcessHit sees the
    // same pre-collision timer value that ftCo_Damage_CalcVel uses in vanilla.
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] != 0u) {
        continue;
      }
      int16_t t = batch->state.damage_time_since_hit_x18ac[idx];
      if (t < 0) {
        continue;
      }
      if (t < INT16_MAX) {
        t++;
      }
      batch->state.damage_time_since_hit_x18ac[idx] = t;
    }

    // Pass 1: x198C timer ownership (x1990/x1994) before combo/hitstun passes.
    //
    // Decomp:
    // - Fighter_8006A360 decrements x1990/x1994 every frame and updates x198C on expiry.
    // - x1990 expiry: if (!x2221_b0) x198C = (x1994 != 0) ? 1 : 0
    // - x1994 expiry: x198C = (x2221_b0 || x1990 != 0) ? 2 : 0
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      uint16_t x1990 = batch->state.colanim_timer_x1990[idx];
      batch->state.colanim_terminal_x1990_item_body_guard[idx] = 0u;
      if (x1990 == 1u && batch->state.colanim_hit_status_x198c[idx] == 2u &&
          batch->state.colanim_lock_x2221_b0[idx] == 0u) {
        batch->state.colanim_terminal_x1990_item_body_guard[idx] =
            (batch->state.colanim_timer_x1994[idx] != 0u) ? 2u : 1u;
      }
      if (x1990 != 0u) {
        x1990--;
        batch->state.colanim_timer_x1990[idx] = x1990;
        if (x1990 == 0u && batch->state.colanim_lock_x2221_b0[idx] == 0u) {
          batch->state.colanim_hit_status_x198c[idx] =
              (batch->state.colanim_timer_x1994[idx] != 0u) ? 1u : 0u;
        }
      }

      uint16_t x1994 = batch->state.colanim_timer_x1994[idx];
      if (x1994 != 0u) {
        x1994--;
        batch->state.colanim_timer_x1994[idx] = x1994;
        if (x1994 == 0u) {
          batch->state.colanim_hit_status_x198c[idx] =
              (batch->state.colanim_lock_x2221_b0[idx] != 0u ||
               batch->state.colanim_timer_x1990[idx] != 0u)
                  ? 2u
                  : 0u;
        }
      }
    }

    // Pass 2: combo timer tick + combo-victim clear (ftColl_800764DC family).
    //
    // Also consume source-owner clear timer ownership (`fp->dmg.x18C8`) under the same
    // !fp->x221F_b3 gate:
    // - Fighter_8006A360 decrements x18C8 once per frame when active.
    // - On expiry (x18C8 reaches -1), clears source owner x18C4_source_ply to sentinel 6.
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/types.h (fp+0x221F bitfields; b3 gate)
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    //
    // Seed lane uses +1 bias:
    // - source_clear_timer_x18c8 == 0 -> inactive (decomp -1)
    // - source_clear_timer_x18c8 > 0  -> active countdown + 1
    enum { MSL_LAST_HIT_BY_SOURCE_NONE = 6 };
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t flags_221f =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221F_INDEX];
      if ((flags_221f & (uint8_t)MSL_STATE_FLAG_221F_B3) != 0u) {
        continue;
      }
      // Decomp reset owner path:
      // - ftCommon_800804FC clears x18c4_source_ply to 6 and sets x18C8 to -1 on grounded paths.
      // Keep the seeded countdown inactive when source owner is already cleared.
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
      if (batch->state.last_hit_by[idx] == (uint8_t)MSL_LAST_HIT_BY_SOURCE_NONE) {
        batch->state.source_clear_timer_x18c8[idx] = 0u;
        continue;
      }
      if (timers_match_flow_dead_action_defers_source_clear(batch->state.action_id[idx])) {
        // Dead-flow source attribution lifetime:
        // - Dead* entry stores death bookkeeping in ftCo_800D331C / ftCo_800D34E0 and later
        //   dead-flow callbacks still consume `fp->dmg.x18c4_source_ply` for KO/suicide and
        //   death-effect ownership.
        // - Do not let the ordinary Fighter_8006A360 x18C8 countdown clear the replay-visible
        //   source lane while the fighter is inside Dead* match-flow actions; Rebirth reset owns
        //   the eventual attribution clear through Fighter_UnkInitReset_80067C98.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D331C,ftCo_800D34E0}
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkInitReset_80067C98}
        continue;
      }
      // Grounded clear-path bridge:
      // - ftCommon_800804FC clears source-owner + disables x18C8 on grounded paths.
      // - Consume this one-step seed-owned phase before timer decrement so the replay-facing
      //   source-owner lane (`last_hit_by`) follows grounded ProcessHit ownership ordering.
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
      if (batch->state.source_clear_grounded_damage_clear_phase[idx] != 0u) {
        batch->state.last_hit_by[idx] = (uint8_t)MSL_LAST_HIT_BY_SOURCE_NONE;
        batch->state.source_clear_timer_x18c8[idx] = 0u;
        continue;
      }
      uint8_t t = batch->state.source_clear_timer_x18c8[idx];
      if (t == 0u) {
        continue;
      }
      // Damage callback ownership in active hitstun can preserve attacker identity lanes
      // through the current step snapshot; defer the terminal clear tick while hitstun is
      // still active to avoid premature source clear on Damage-family rows.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
      //
      // Some terminal x18C8 rows park source owner with the timer inactive due to callback-owned
      // ownership phase ordering inside Fighter_8006A360. The seed lane preserves one-step rows;
      // the downed/passive predicate reconstructs the same owner in free rollout.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      if (t == 1u && batch->state.hitstun[idx] != 0u) {
        continue;
      }
      if (t == 1u && (batch->state.source_clear_terminal_phase[idx] != 0u ||
                      timers_source_clear_downed_recovery_terminal_parks_owner(batch, idx))) {
        batch->state.source_clear_timer_x18c8[idx] = 0u;
        continue;
      }
      t--;
      batch->state.source_clear_timer_x18c8[idx] = t;
      if (t == 0u) {
        batch->state.last_hit_by[idx] = (uint8_t)MSL_LAST_HIT_BY_SOURCE_NONE;
      }
    }

    // Pass 3: combo timer tick + combo-victim clear (ftColl_800764DC family).
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Use post-decrement hitlag frames (not hitlag_started_frame) because the hitlag gate
      // `fp->x2219_b5` is cleared when hitlag reaches 0 in Fighter_8006A1BC.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      uint16_t t = batch->state.combo_timer_x2098[idx];
      if (t != 0) {
        t--;
        batch->state.combo_timer_x2098[idx] = t;
      }

      const uint8_t v_port = batch->state.combo_victim_port[idx];
      if (v_port == 0xFFu) {
        continue;
      }
      if (v_port >= (uint8_t)num_players) {
        batch->state.combo_victim_port[idx] = 0xFFu;
        batch->state.combo_victim_instance_id[idx] = 0;
        continue;
      }

      const size_t v_idx = msl_idx_player(bi, (int)v_port);
      const uint8_t v_flags_221c =
          batch->state.state_flags[v_idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t v_is_hitstun = (v_flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1u : 0u;
      if (!v_is_hitstun && batch->state.combo_timer_x2098[v_idx] == 0) {
        batch->state.combo_victim_port[idx] = 0xFFu;
        batch->state.combo_victim_instance_id[idx] = 0;
      }
    }

    // Pass 4: hitstun decrement + hitstun end effects (ftCo_8008F744 family).
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Use post-decrement hitlag frames (not hitlag_started_frame) because the hitlag gate
      // `fp->x2219_b5` is cleared when hitlag reaches 0 in Fighter_8006A1BC.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const uint8_t flags_221c =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t is_hitstun = (flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1u : 0u;
      if (!is_hitstun) {
        continue;
      }

      uint16_t hs = batch->state.hitstun[idx];
      if (hs > 0) {
        hs--;
        batch->state.hitstun[idx] = hs;
      }

      // Decomp: hitstun flag is cleared when the hitstun timer reaches 0.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
      if (hs == 0) {
        if (damage_timer_down_damage_action(batch->state.action_id[idx])) {
          // DownDamage owns a separate anim/timer lane:
          // - ftCo_DownDamage_Anim decrements mv.co.downdamage.x0,
          // - it does not call ftCo_8008F744 while the action remains DownDamageU/D, and
          // - x221C_b6 can remain visible after the replay-facing hitstun scalar reaches zero.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
          continue;
        }
        const size_t flags_221c_i =
            idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
        batch->state.state_flags[flags_221c_i] &=
            (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;

        // Decomp: when hitstun ends, set `fp->x2098 = p_ftCommonData->x4CC`.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
        if (c != NULL) {
          batch->state.combo_timer_x2098[idx] = c->combo_timer_post_hitstun_frames;
        } else {
          batch->state.combo_timer_x2098[idx] = 0;
        }
      }
    }
  }
}
