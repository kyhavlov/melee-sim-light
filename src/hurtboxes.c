#include "hurtboxes.h"

#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "common_params.h"
#include "hurtcaps_tables.h"
#include "input_axis.h"
#include "items.h"
#include "motion_state_owners.h"
#include "msl_math.h"
#include "mtx34.h"
#include "move_tables.h"
#include "shield_tilt_table.h"
#include "specialhi_pose.h"
#include "stage_collision.h"

enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t hurtboxes_player_has_contact_geometry_demand(const MslBatch* batch, int bi,
                                                                   int p, int num_players) {
  if (batch == NULL) {
    return 0u;
  }
  if (items_row_has_fighter_collision_demand(batch, bi) != 0u) {
    return 1u;
  }
  for (int attacker = 0; attacker < num_players; attacker++) {
    if (attacker == p) {
      continue;
    }
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.hitbox_count[a_idx] != 0u) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t hurtboxes_guard_fallback_submotion(uint16_t action_id, uint16_t* out_msid) {
  if (out_msid == NULL) {
    return 0u;
  }
  // Guard-family motion-state -> submotion mapping from ftmotionstates table.
  // refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardOff,
  //                                            ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
  // Note: GuardReflect uses ftCo_SM_GuardOn in GALE01.
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
      *out_msid = (uint16_t)MSL_SM_GUARD_ON;
      return 1u;
    case MSL_ACT_GUARD:
      *out_msid = (uint16_t)MSL_SM_GUARD;
      return 1u;
    case MSL_ACT_GUARD_OFF:
      *out_msid = (uint16_t)MSL_SM_GUARD_OFF;
      return 1u;
    case MSL_ACT_GUARD_SET_OFF:
      *out_msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
      return 1u;
    case MSL_ACT_GUARD_REFLECT:
      // Decomp motion-state mapping: GuardReflect uses ftCo_SM_GuardOn as its submotion table.
      // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
      *out_msid = (uint16_t)MSL_SM_GUARD_ON;
      return 1u;
    default:
      return 0u;
  }
}

static inline uint16_t hurtboxes_timer_remaining_from_action_frame(uint16_t init_frames,
                                                                   int16_t action_frame) {
  if (init_frames == 0u) {
    return 0u;
  }
  if (action_frame <= 0) {
    return init_frames;
  }
  int rem = (int)init_frames + 1 - (int)action_frame;
  if (rem < 0) {
    rem = 0;
  }
  if (rem > 0xFFFF) {
    rem = 0xFFFF;
  }
  return (uint16_t)rem;
}

static inline uint8_t hurtboxes_runtime_specialhi_pose_owner(uint8_t char_id, uint16_t action_id) {
  if (char_id != (uint8_t)MSL_CHAR_FOX && char_id != (uint8_t)MSL_CHAR_FALCO) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hurtboxes_float_aobj_pose_owner(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_LANDING_AIR);
}

static inline float hurtboxes_pose_sample_frame(const MslBatch* batch, size_t idx, uint8_t char_id,
                                                uint16_t msid, uint16_t action_id,
                                                float anim_frame_f32, uint16_t pose_frame) {
  (void)batch;
  (void)idx;
  (void)char_id;
  (void)msid;
  return hurtboxes_float_aobj_pose_owner(action_id) ? anim_frame_f32 : (float)pose_frame;
}

static inline uint8_t hurtboxes_guard_tilt_live_body_pose_owner(const MslBatch* batch, size_t idx,
                                                                uint16_t pose_msid) {
  if (batch == NULL || pose_msid != (uint16_t)MSL_SM_GUARD) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.animation_index[idx] != UINT32_MAX || batch->state.action_frame[idx] >= 0) {
    return 0u;
  }
  if (batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  if (batch->state.pos_z[idx] <= 1.0e-6f && batch->state.pos_z[idx] >= -1.0e-6f) {
    return 0u;
  }
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[idx], &tv) != 0 || tv.xyz == NULL ||
      tv.frame_count == 0u) {
    return 0u;
  }
  return (batch->state.guard_tilt_x4[idx] > 0.0f) ? 1u : 0u;
}

static inline uint8_t hurtboxes_side_special_end_uses_pre_anim_collision_pose(uint8_t char_id,
                                                                              uint16_t action_id) {
  if (char_id != (uint8_t)MSL_CHAR_FOX && char_id != (uint8_t)MSL_CHAR_FALCO) {
    return 0u;
  }
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S_END ||
                   action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END);
}

static inline uint8_t hurtboxes_side_special_start_passivewalljump_entry_pose_owner(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t action_id) {
  if (batch == NULL || (char_id != (uint8_t)MSL_CHAR_FOX && char_id != (uint8_t)MSL_CHAR_FALCO)) {
    return 0u;
  }
  if (action_id != (uint16_t)MSL_ACT_FX_SPECIAL_S_START &&
      action_id != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
    return 0u;
  }
  if (batch->state.action_frame[idx] > 2) {
    return 0u;
  }
  return (uint8_t)(batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP ||
                   batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP);
}

static inline uint8_t hurtboxes_attackdash_post_hitbox_collision_pose_owner(const MslBatch* batch,
                                                                            int bi, size_t idx,
                                                                            uint16_t action_id,
                                                                            uint8_t char_id,
                                                                            float anim_frame_f32) {
  (void)bi;
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_ATTACK_DASH) {
    return 0u;
  }
  // Replay post-frame rows do not expose the live JObj/AObj phase for AttackDash's immediate
  // post-hitbox-clear collision frame. Use the same source predicate in one-step and rollout:
  // sampled frame 34 after the script hitbox clear and before the AttackDash IASA gate. Applying
  // the offset later over-admits frame-38 AttackDash BODY rows, so the boundary remains data-backed
  // by MSLFTSC1's AttackDash hitbox-clear timing and move-table allow_interrupt timing.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
  //   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
  if (batch->state.hitbox_count[idx] != 0u) {
    return 0u;
  }
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  if (frame != 34u) {
    return 0u;
  }
  if (move_tables_grounded_attack_allow_interrupt(char_id, action_id, anim_frame_f32) != 0u) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t hurtboxes_damageflyroll_live_xrotn_pose_owner(uint16_t action_id) {
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL);
}

static inline uint8_t hurtboxes_escapef_root_facing_owner(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_F) {
    return 0u;
  }
  if (batch->state.action_frame[idx] < 20 || batch->state.facing_dir1[idx] == 0) {
    return 0u;
  }
  // EscapeF has a script-owned mid-roll root-facing boundary when its vulnerable collision phase
  // starts: frame 20 emits `set_hit_status(0)` and `set_throw_flags(hit_idx=0)`. The root JObj
  // collision matrix still follows the motion-entry facing (`facing_dir1`) while the replay-visible
  // scalar facing byte can already describe the gameplay-facing side of the roll. EscapeB/EscapeN
  // are intentionally excluded because their extracted scripts do not emit the EscapeF throw-flag
  // event at the vulnerable boundary.
  // data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"].events
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState copies facing_dir -> facing_dir1)
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  return 1u;
}

static inline uint8_t hurtboxes_damageflyroll_xrotn_angle_from_velocity(const MslBatch* batch,
                                                                        size_t idx,
                                                                        float* out_angle) {
  if (batch == NULL || out_angle == NULL) {
    return 0u;
  }
  const float vx = batch->state.speed_air_x_self[idx] + batch->state.speed_x_attack[idx];
  const float vy = batch->state.speed_y_self[idx] + batch->state.speed_y_attack[idx];
  if (!(isfinite(vx) && isfinite(vy)) || (vx == 0.0f && vy == 0.0f)) {
    return 0u;
  }
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  *out_angle = facing_dir * atan2f(vx, vy);
  return 1u;
}

static inline uint8_t hurtboxes_common_action_to_msid(uint16_t action_id, uint16_t* out_msid) {
  if (out_msid == NULL) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_WAIT:
      *out_msid = (uint16_t)MSL_SM_WAIT1_0;
      return 1u;
    case MSL_ACT_WALK_SLOW:
      *out_msid = (uint16_t)MSL_SM_WALK_SLOW;
      return 1u;
    case MSL_ACT_TURN:
      *out_msid = (uint16_t)MSL_SM_TURN;
      return 1u;
    case MSL_ACT_DASH:
      *out_msid = (uint16_t)MSL_SM_DASH;
      return 1u;
    case MSL_ACT_RUN:
      *out_msid = (uint16_t)MSL_SM_RUN;
      return 1u;
    case MSL_ACT_SQUAT_RV:
      *out_msid = (uint16_t)MSL_SM_SQUAT_RV;
      return 1u;
    case MSL_ACT_ATTACK_DASH:
      *out_msid = (uint16_t)MSL_SM_ATTACK_DASH;
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hurtboxes_action_entry_carries_previous_jobj_pose(uint16_t prev_action,
                                                                        uint16_t cur_action) {
  switch (cur_action) {
    case MSL_ACT_WAIT:
      return (uint8_t)(prev_action == (uint16_t)MSL_ACT_WALK_SLOW ||
                       prev_action == (uint16_t)MSL_ACT_ATTACK_DASH);
    case MSL_ACT_RUN:
      return (uint8_t)(prev_action == (uint16_t)MSL_ACT_DASH);
    case MSL_ACT_WALK_SLOW:
      return (uint8_t)(prev_action == (uint16_t)MSL_ACT_SQUAT_RV ||
                       prev_action == (uint16_t)MSL_ACT_TURN);
    default:
      return 0u;
  }
}

static inline uint8_t hurtboxes_apply_specialhi_local_xrotn(const MslBatch* batch, size_t idx,
                                                            uint8_t char_id, uint16_t msid,
                                                            uint16_t pose_frame, uint16_t part_id,
                                                            float facing_dir, float model_scale,
                                                            float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL) {
    return 0u;
  }
  (void)facing_dir;
  const uint16_t action_id = batch->state.action_id[idx];
  if (!hurtboxes_runtime_specialhi_pose_owner(char_id, action_id)) {
    return 0u;
  }
  (void)part_id;

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, pose_frame, 2u, m) != 0) {  // FtPart_XRotN
    return 0u;
  }

  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  // Decomp: Firefox/Firebird launch writes `rotateModel = atan2f(self_vel.y, self_vel.x * facing_dir)`
  // and applies it with `ftPartSetRotX(..., 2*pi - rotateModel)` on FtPart_XRotN.
  // Victim hurtcaps bound under that subtree inherit the same runtime local rotation.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);

  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline uint8_t hurtboxes_apply_damageflyroll_local_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t pose_frame,
    uint16_t part_id, float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL) {
    return 0u;
  }
  if (!hurtboxes_damageflyroll_live_xrotn_pose_owner(batch->state.action_id[idx])) {
    return 0u;
  }
  if (!msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, pose_frame, 2u, m) != 0) {  // FtPart_XRotN
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  float angle = 0.0f;
  if (!hurtboxes_damageflyroll_xrotn_angle_from_velocity(batch, idx, &angle)) {
    return 0u;
  }

  // DamageFlyRoll live pose owner:
  // - ftCo_8008DCE0 enters DamageFlyRoll, immediately runs ftAnim_8006EBA4, then calls inlineA1
  //   to set FtPart_XRotN from current self+KB velocity.
  // - ftCo_DamageFlyRoll_Phys calls doFlyRoll before and after physics, rewriting that same XRotN
  //   pose while the ordinary AObj timeline continues to own the local JObj SRT.
  // - ftColl_80076ED8 consumes lb_8000B1CC world hurtcaps from the live JObjs.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008DCE0,inlineA1,doFlyRoll,ftCo_DamageFlyRoll_Phys}
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline void hurtboxes_apply_colanim_action_entry(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t action = batch->state.action_id[idx];
  const uint16_t prev_action = batch->state.prev_action_id[idx];
  if (action == prev_action) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int16_t action_frame = batch->state.action_frame[idx];

  // RebirthWait -> Fall colanim ownership (x1994/x198C):
  //
  // Decomp:
  // - RebirthWait_Anim and RebirthWait_IASA call ftColl_8007B7A4(gobj, p_ftCommonData->x5D8)
  //   immediately before Fall enter.
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::{ftCo_RebirthWait_Anim,ftCo_RebirthWait_IASA}
  // - RebirthWait_Coll helper fn_800D5A30 also calls ftColl_8007B7A4(..., x5D8) before ft_8008A2BC.
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::fn_800D5A30
  //
  // Seed-bridge note:
  // - Some teacher-forced seeds can observe a direct Rebirth -> Fall snapshot without the explicit
  //   intermediate RebirthWait row. Treat prev_action=Rebirth as equivalent for this entry hook.
  if (action == (uint16_t)MSL_ACT_FALL &&
      (prev_action == (uint16_t)MSL_ACT_REBIRTH_WAIT || prev_action == (uint16_t)MSL_ACT_REBIRTH)) {
    const uint16_t rem = hurtboxes_timer_remaining_from_action_frame(
        c->colanim_rebirth_fall_x1994_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1994[idx]) {
      batch->state.colanim_timer_x1994[idx] = rem;
    }
    if (rem != 0u) {
      batch->state.colanim_hit_status_x198c[idx] =
          (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
    }
  }

  // Throw entry ownership:
  // - ftCo_800DD398 enters Throw* and calls ftColl_8007B7A4(..., x348), which sets x1994 and x198C.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  if (msl_action_is_throw_owner(action)) {
    uint16_t rem =
        hurtboxes_timer_remaining_from_action_frame(c->colanim_throw_x1994_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1994[idx]) {
      batch->state.colanim_timer_x1994[idx] = rem;
    }
    batch->state.colanim_hit_status_x198c[idx] =
        (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
  }

  // Cliff catch/wait invulnerability timer ownership (x49C -> x1990):
  // decomp callsite anchor: CliffCatch/CliffWait entry paths set x1990 once via ftColl_8007B760.
  // Do not recreate the timer on steady CliffWait frames after Fighter_8006A360 expires it.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if ((action == (uint16_t)MSL_ACT_CLIFF_CATCH || action == (uint16_t)MSL_ACT_CLIFF_WAIT) &&
      action_frame <= 1) {
    uint16_t rem =
        hurtboxes_timer_remaining_from_action_frame(c->colanim_cliff_x1990_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1990[idx]) {
      batch->state.colanim_timer_x1990[idx] = rem;
    }
    if (rem != 0u) {
      batch->state.colanim_hit_status_x198c[idx] = 2u;
    }
  }
}

static void hurtboxes_refresh_impl(MslBatch* batch, uint8_t geometry_mode) {
  if (batch == NULL) {
    return;
  }

  // Decomp semantics:
  // - Hurt capsule init records are `ftHurtboxInit` (refs/melee/src/melee/ft/chara/ftCommon/types.h).
  // - ftColl_HurtboxInit assigns offsets/scale and binds `hurt->capsule.bone` to
  //   `fp->parts[hurt->capsule.bone_idx].joint` (refs/melee/src/melee/ft/ftcoll.c::ftColl_HurtboxInit).
  // - Hurt capsule endpoint world positions are computed from (bone joint matrix, offsets) via
  //   lb_8000B1CC (refs/melee/src/melee/lb/lbcollision.c::checkPos), written into HurtCapsule.a_pos/b_pos.
  //
  // We approximate that pipeline using our SSANIM01 pose sampler:
  // - anim_pose_get_matrix(char_id, msid, frame, part_id=Fighter_Part, out_3x4)
  // and then applying fighter translation (pos_x/pos_y/pos_z) in world space.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t build_geometry =
          (geometry_mode == 0u)
              ? 1u
              : ((geometry_mode == 1u)
                     ? 0u
                     : hurtboxes_player_has_contact_geometry_demand(batch, bi, p, num_players));
      batch->state.hurtcap_count[idx] = 0;
      batch->state.hurtcap_geometry_valid[idx] = 0u;
      // Clear fixed slots for stable debug readback (and to avoid stale values when pose lookups
      // or script masks disable/skip specific capsules).
      for (int ci = 0; ci < MSL_MAX_HURTCAPS; ci++) {
        const size_t hi = idx_hurtcap(bi, p, ci);
        batch->state.hurtcap_enabled[hi] = 0;
        batch->state.hurtcap_a_x[hi] = 0.0f;
        batch->state.hurtcap_a_y[hi] = 0.0f;
        batch->state.hurtcap_a_z[hi] = 0.0f;
        batch->state.hurtcap_b_x[hi] = 0.0f;
        batch->state.hurtcap_b_y[hi] = 0.0f;
        batch->state.hurtcap_b_z[hi] = 0.0f;
        batch->state.hurtcap_radius[hi] = 0.0f;
        batch->state.hurtcap_is_grabbable[hi] = 0;
        batch->state.hurtcap_height[hi] = 0;
      }

      const uint8_t char_id = batch->state.char_id[idx];
      const uint16_t action_id = batch->state.action_id[idx];
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      const uint16_t prev_action_id = batch->state.prev_action_id[idx];
      hurtboxes_apply_colanim_action_entry(batch, idx);
      uint8_t final_hurtbox_state = batch->state.colanim_hit_status_x198c[idx];
      const uint8_t preserve_visible_shieldbreak_hit_status =
          ((action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_FLY ||
            action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_FALL ||
            action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U ||
            action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D ||
            action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_U ||
            action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_D) &&
           batch->state.hurtbox_state[idx] == 2u)
              ? 1u
              : 0u;
      const uint8_t preserve_visible_downbound_colanim =
          (((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
             action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) ||
            ((prev_action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
              prev_action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
             (action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
              action_id == (uint16_t)MSL_ACT_DOWN_WAIT_D ||
              action_id == (uint16_t)MSL_ACT_FALL))) &&
           batch->state.colanim_hit_status_x198c[idx] == 1u &&
           batch->state.colanim_timer_x1994[idx] != 0u)
              ? 1u
              : 0u;
      if (preserve_visible_downbound_colanim) {
        // Narrow visible-state bridge for DownBound x198C=1 rows:
        // - Combat/collision still needs the explicit x198C lane for invincible-contact gating.
        // - Slippi visible hurtbox_state can remain 0 on these timer-owned rows even while the
        //   hidden x198C/x1994 internals are active.
        // Preserve the visible compare lane and let combat read x198C directly.
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        final_hurtbox_state = batch->state.hurtbox_state[idx];
      }
      if (preserve_visible_shieldbreak_hit_status) {
        // Shield-break hit-status owner:
        // - ftCo_80098B20 calls `ftColl_8007B62C(gobj, 2)` after entering ShieldBreakFly.
        // - ShieldBreakFall/Down/Stand transitions use motion flags that keep this hit status until
        //   Furafura entry clears it. Current runtime has no separate x1988 state lane, so preserve
        //   the replay-visible merged status for this source-owned family while still letting
        //   Furafura's explicit entry clear reset it.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_80098B20
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        final_hurtbox_state = batch->state.hurtbox_state[idx];
      }

      // Hurtbox-state composition:
      // - x1988 lane: movescript-derived hit status (opcode 26 / ftColl_8007B62C).
      // - x198C lane: timer/system-owned collision status (x1990/x1994 path).
      // Slippi post-frame reports x1988 when nonzero, else x198C.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
      uint8_t hit_status = 0;
      uint8_t have_hit_status_override = 0;
      if (batch->debug_hit_status_override != NULL) {
        const uint8_t ov = batch->debug_hit_status_override[idx];
        if (ov != 0xFFu) {
          hit_status = ov;
          have_hit_status_override = 1;
        }
      }

      uint16_t msid = 0u;
      if (anim_u32 > 0xFFFFu) {
        const uint8_t guard_reflect_from_guard_on =
            (uint8_t)(batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON ||
                      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON);
        if (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            batch->state.action_frame[idx] <= (int16_t)-2 && !guard_reflect_from_guard_on) {
          // GuardReflect no-submotion late-phase snapshots are ordering-sensitive with shield
          // descriptor ownership (x221B_b0 via ftColl_8007B1B8). Preserve raw snapshot geometry on
          // the active-x14 ReflectDesc lanes, but let GuardOn_IASA powershield entry
          // (ftCo_8009388C) fall through to the GuardReflect submotion fallback. Same-step GuardOn
          // entries are visible through live prev_action_id; already-seeded GuardReflect snapshots
          // use the frame-start seed provenance because live prev can advance before hurtcaps
          // refresh. Expired-x14 fighter-vs-fighter BODY has a local combat.c fallback so item
          // collision does not see broad synthetic GuardReflect hurtcaps.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
          // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
          //
          // Catch selection has its own local grabbable fallback in combat.c for ftColl_80078A2C.
          // Do not populate global BODY/debug hurtcap state for this no-submotion slice.
          if (hit_status != 0) {
            final_hurtbox_state = hit_status;
          }
          batch->state.hurtbox_state[idx] = final_hurtbox_state;
          continue;
        }
        // Seed-bridge fallback (guard-family only):
        // Slippi post-frames commonly encode guard-family snapshots with animation_index=-1 while
        // decomp collision still uses the active ftCo submotion timeline. Keep the raw compare
        // field unchanged, but derive hurtcaps from the decomp motion-state table mapping.
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // refs/melee/src/melee/ft/ftmotionstates.c (Guard* motion-state rows)
        //
        // Guard (hold) no-submotion snapshots with x221B_b0 can still resolve BODY contacts when
        // shield overlap fails ("shield poke"), so keep fallback hurtcaps available when we can
        // map the current guard-family motion-state to its submotion table.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076CBC,ftColl_80076ED8}
        if (!hurtboxes_guard_fallback_submotion(action_id, &msid)) {
          if (hit_status != 0) {
            final_hurtbox_state = hit_status;
          }
          batch->state.hurtbox_state[idx] = final_hurtbox_state;
          continue;
        }
      } else {
        msid = (uint16_t)anim_u32;
      }

      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
      uint16_t pose_msid = msid;
      uint16_t pose_frame = frame;
      if (batch->state.prev_action_id[idx] != action_id &&
          hurtboxes_action_entry_carries_previous_jobj_pose(batch->state.prev_action_id[idx],
                                                            action_id)) {
        uint16_t prev_msid = 0u;
        if (hurtboxes_common_action_to_msid(batch->state.prev_action_id[idx], &prev_msid)) {
          uint16_t prev_frame = 0u;
          if (batch->state.prev_action_frame[idx] >= 0) {
            prev_frame = (uint16_t)batch->state.prev_action_frame[idx];
            // Turn can enter Walk from ftCo_Turn_IASA after ftCo_Turn_Anim has already interpreted
            // the live JObj pose for this frame. Unlike the generic entry carry, the collision
            // pass still consumes that serialized Turn pose rather than a projected next Turn
            // frame. IAT:5092 is a replay-real negative: projecting +1 admits a false Shine BODY
            // hit against WalkSlow entry.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
            //   ftCo_Turn_Anim,ftCo_Turn_IASA}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
            // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
            if (prev_frame != 0xFFFFu &&
                !(batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_TURN &&
                  action_id == (uint16_t)MSL_ACT_WALK_SLOW)) {
              prev_frame = (uint16_t)(prev_frame + 1u);
            }
          }
          const float end_frame = msl_anim_end_frame(char_id, prev_msid);
          if (end_frame > 0.0f && (float)prev_frame > end_frame) {
            prev_frame = msl_anim_frame_floor_u16(end_frame);
          }
          // Same-frame locomotion/action entry pose order:
          // - Fighter_8006A360 interprets the current JObj AObj at proc prio 1.
          // - Input/IASA/action callbacks can then enter Wait/Run/Walk before collision refresh.
          // - Unless the entry path calls an immediate ftAnim_8006EBA4 tick, lb_8000B1CC still
          //   consumes the previous live JObj pose during this collision pass.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
          // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
          pose_msid = prev_msid;
          pose_frame = prev_frame;
        }
      }
      if ((action_id == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_F ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW) &&
          batch->state.anim_defer_tick_once[idx] != 0u && frame != 0xFFFFu) {
        // AttackAir entry hurtcaps need the post-ChangeMotionState immediate ftAnim tick.
        //
        // Decomp:
        // - ftCo_AttackAir_EnterFromMsid enters the motion state, then immediately calls
        //   ftAnim_8006EBA4 before the current frame's collision owner runs.
        // - The simulator defers that tick globally to preserve entry-pose hitbox timing, but the
        //   defender-side hurtcaps on the same frame must still sample the post-tick pose.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
        // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
        frame = (uint16_t)(frame + 1u);
      }
      if ((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
           action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
          (batch->state.prev_on_ground[idx] == 0u || batch->state.prev_action_frame[idx] > 0) &&
          batch->state.hurtbox_state[idx] == 0u &&
          ((batch->state.on_ground[idx] != 0u && batch->state.colanim_timer_x1994[idx] != 0u) ||
           (batch->state.on_ground[idx] == 0u && batch->state.ground_id[idx] != (uint16_t)0xFFFFu &&
            batch->state.speed_y_self[idx] == 0.0f &&
            stage_collision_floor_line_is_platform(batch->state.stage_id[bi],
                                                   batch->state.ground_id[idx]) != 0u))) {
        // Narrow DownBound post-Anim hurtcaps pose bridge:
        // - DownBound callback ordering is Anim then Coll on the same frame.
        // - Seed reseed preserves the hidden DownBound x1994 timer without globally raising
        //   x198C; when an airborne-at-snapshot DownBound row floors before collision, or a
        //   continuing DownBound row remains in that post-Anim collision-pose episode, pre-combat
        //   hurtcaps and hit-status masks need the post-Anim frame. Already-grounded frame-0
        //   DownBound rows keep their normal current pose, preserving real downed BODY contacts.
        // - Replay post-frame can show GA_Air with a retained platform `ground_id` and zero Y
        //   speed after DownBound_Coll's mpColl_8004B108 path. In that platform-supported
        //   floor-resting phase, the same current-frame Anim-before-Coll pose applies even though
        //   the visible ground byte has already flipped away from grounded.
        //   This fixes replay-false high-hurtcap AttackDash misses without suppressing real
        //   grounded DownDamage contacts or trusting arbitrary stale non-platform ground ids.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Anim,ftCo_DownBound_Coll
        // }
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
        // data/stages/bin/*.bin::MSLSTG01 segment.platform
        if (frame != 0xFFFFu) {
          frame = (uint16_t)(frame + 1u);
        }
        if (pose_frame != 0xFFFFu) {
          pose_frame = (uint16_t)(pose_frame + 1u);
        }
      }
      if (hurtboxes_side_special_end_uses_pre_anim_collision_pose(char_id, action_id) &&
          pose_frame > 0u && pose_frame != 0xFFFFu) {
        // Fox/Falco Side-B End collision-pose ownership:
        // - Replay-real ftColl probe evidence on SpecialAirSEnd shows ftColl_80076ED8 consuming
        //   the live hurt capsule pose from the same frame's pre-Anim JObj when Shine Start BODY
        //   contact is selected. Advancing to the post-Anim SSANIM frame moves the limb capsule
        //   out of the accepted lbColl_8000805C/80006E58 narrowphase boundary.
        // - Keep this to Side-B End, whose decomp callbacks do not call an immediate
        //   ftAnim_8006EBA4 on this steady-state row; generic actions continue to use the normal
        //   post-Anim collision pose.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
        //   ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim,ftFx_SpecialSEnd_Coll,
        //   ftFx_SpecialAirSEnd_Coll}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        pose_frame = (uint16_t)(pose_frame - 1u);
      }
      if (hurtboxes_side_special_start_passivewalljump_entry_pose_owner(batch, idx, char_id,
                                                                        action_id) &&
          pose_frame > 0u && pose_frame != 0xFFFFu) {
        // PassiveWallJump -> Fox/Falco Side-B Start collision-pose ownership:
        // - The first visible Side-B startup row after PassiveWallJump IASA still consumes the
        //   entry-source JObj pose for BODY collision. Generic Side-B startup rows keep the normal
        //   startup pose owner.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
        //   ftFx_SpecialSStart_Anim,ftFx_SpecialAirSStart_Anim,ftFx_SpecialSStart_Coll,
        //   ftFx_SpecialAirSStart_Coll}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        pose_frame = (uint16_t)(pose_frame - 1u);
      }
      if (hurtboxes_attackdash_post_hitbox_collision_pose_owner(batch, bi, idx, action_id, char_id,
                                                                anim_frame_f32) &&
          pose_frame != 0xFFFFu) {
        // AttackDash late teacher-forced collision-pose phase:
        // - ftCo_AttackDash_Anim owns the command-script hitbox clear before collision.
        // - While the action is still before its command-script allow_interrupt frame, BODY
        //   collision consumes the post-clear JObj pose that is one AObj step ahead of the
        //   replay-visible action-frame lane. Apply the same source predicate in one-step and
        //   rollout; do not apply this after allow_interrupt, where IASA can hand ownership to
        //   Wait/Walk on the same frame.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
        //   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
        // Source of timing windows: data/scripts/{fox,falco}.bin (MSLFTSC1).
        pose_frame = (uint16_t)(pose_frame + 1u);
      }
      if (!have_hit_status_override && !preserve_visible_downbound_colanim) {
        (void)move_tables_hit_status_at_frame(char_id, msid, frame, &hit_status);
      }
      if (hit_status != 0) {
        // Decomp timing: move-induced hit status (fp->x1988) is set by movescript opcode 26
        // (ftAction_80071A14 -> ftColl_8007B62C) while executing the fighter cmd script inside
        // ftAnim_8006EBA4 (ftAction_80073240), which runs at proc priority 1 before input/IASA.
        // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        //
        // Collision eligibility in decomp consults the aggregate hit status via ftColl_8007B868,
        // which combines x1988 (movescript), x198C (game-induced), and x221D_b6.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        //
        // If the sim enters a new motion state after the pre-input Anim tick (e.g. due to
        // input/IASA), the new state's movescript will not execute until the next frame's Anim
        // proc. Slippi's post-frame `hurtbox_state` prefers x1988 only when it has actually been
        // set nonzero for that frame (SendGamePostFrame.asm checks fp+0x1988 then fp+0x198C).
        //
        // Mirror that ordering by deferring the table-derived x1988 override on the entry frame
        // when we can observe that:
        // - the fighter changed action state this step (prev_action_id != action_id), and
        // - the new state's anim timebase is still at integer frame 0 (no Anim proc yet).
        //
        // NOTE(shine_entry): Some motion-state entry helpers call ftAnim_8006EBA4 immediately
        // after Fighter_ChangeMotionState, meaning the new state's cmd script (and opcode 26 hit
        // status) can run on the entry frame even though the transition happened post-Anim.
        // Shine Start (Fox/Falco SpecialLwStart / SpecialAirLwStart) is a decomp-anchored example.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
        // Entry happens after the prio 1 Anim proc and before later callback phases.
        const uint16_t cur_action = batch->state.action_id[idx];
        const uint8_t is_shine_start_entry =
            (cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
             cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START)
                ? 1u
                : 0u;
        const uint8_t is_passive_tech_entry = (cur_action == (uint16_t)MSL_ACT_PASSIVE ||
                                               cur_action == (uint16_t)MSL_ACT_PASSIVE_STAND_F ||
                                               cur_action == (uint16_t)MSL_ACT_PASSIVE_STAND_B)
                                                  ? 1u
                                                  : 0u;
        const uint8_t is_down_stand_entry = (cur_action == (uint16_t)MSL_ACT_DOWN_STAND_U ||
                                             cur_action == (uint16_t)MSL_ACT_DOWN_STAND_D)
                                                ? 1u
                                                : 0u;
        // Passive / PassiveStand entry ownership:
        // - ftCo_80090184 resolves grounded tech callbacks before the post-frame snapshot.
        // - Replay-visible entry frame 0 already carries the new motion state's script hit-status
        //   on these tech entries, so do not defer the frame-0 table override here.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_800987D0
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_800989D4
        // data/scripts/{fox,falco}.bin (MSLFTSC1 set_hit_status / hurt-state events)
        // DownStand entry from DownDamage/DownWait has the same entry-frame visibility property:
        // ftCo_80098160 enters the getup submotion before post-frame capture, and the extracted
        // script hit-status timeline owns the entry hurtbox_state.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_80098160
        // data/scripts/{fox,falco}.bin (MSLFTSC1 set_hit_status / hurt-state events)
        if (!(frame == 0u && batch->state.prev_action_id[idx] != cur_action &&
              !is_shine_start_entry && !is_passive_tech_entry && !is_down_stand_entry)) {
          final_hurtbox_state = hit_status;
        }
      }
      batch->state.hurtbox_state[idx] = final_hurtbox_state;

      if (geometry_mode == 1u) {
        continue;
      }

      if (msl_action_owns_x2219_collision_skip(action_id)) {
        // Dead*/Rebirth source states set fp->x2219_b1. Fighter_8006CB94 skips the common fighter
        // collision pass while that bit is live, and item-vs-fighter collision rejects x2219_b1
        // targets. Preserve the visible Slippi hurtbox_state above, but do not build BODY/catch
        // capsules that vanilla collision will not consume.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3680,ftCo_800D3950,ftCo_800D3BC8,ftCo_800D3E40,ftCo_800D4580,ftCo_800D481C}
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        continue;
      }

      const MslHurtCap* caps = NULL;
      uint16_t cap_count_u16 = 0;
      if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL || cap_count_u16 == 0) {
        continue;
      }
      uint16_t cap_count = cap_count_u16;
      if (cap_count > (uint16_t)MSL_MAX_HURTCAPS) {
        cap_count = (uint16_t)MSL_MAX_HURTCAPS;
      }

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];

      // NOTE (scaling): Vanilla applies a per-fighter model scale factor (fp->x34_scale.y) to
      // hurt capsule derived quantities.
      //
      // - Radius: ftCo_800A0DA4 uses `scale = hurt->capsule.scale * fp->x34_scale.y`.
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c::ftCo_800A0DA4
      //
      // - Endpoints: In-engine capsule endpoints are computed via lb_8000B1CC against the bone's
      //   joint matrix. Since fp->x34_scale is applied at the model level, this scaling is baked
      //   into the runtime joint matrices. Our SSANIM01 pose matrices are extracted without that
      //   runtime fighter-scale, so we apply the same scalar uniformly to the pose-space endpoints
      //   before adding world translation.
      //
      // Facing parity: In-engine joint matrices are fighter-facing dependent because the fighter's
      // root part is rotated about Y by +/-90° based on `fp->facing_dir`, and lb_8000B1CC consumes
      // that runtime joint matrix when producing world endpoints.
      // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
      // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
      //
      // Our SSANIM pose matrices are extracted in a single canonical orientation without that
      // runtime facing rotation, so apply the same decomp-shaped Y rotation here (mixing X/Z).
      //
      // We intentionally use only the y component (as decomp does for collision/bounds), treating
      // it as a uniform scalar for x/y/z here.
      const float scale_y = batch->state.fighter_scale_y[idx];
      // Decomp: runtime joint matrices include ftCommon_GetModelScale(fp) (co_attrs.model_scaling)
      // in addition to fp->x34_scale.y. Our SSANIM pose matrices are extracted without those runtime
      // scalars, so apply model_scaling here alongside fighter_scale_y.
      // refs/melee/src/melee/ft/ftparts.c::ftParts_80074B8C (uses ftCommon_GetModelScale)
      const MslCharParams* chp = msl_char_params(char_id);
      const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                      ? chp->model_scaling
                                      : 1.0f;
      // DamageFlyRoll hurtcap scale follows the common hurt-capsule source path
      // (`ftCo_800A0DA4`: capsule.scale * fp->x34_scale.y). Do not apply the generic
      // collision-skeleton model-scaling compensation on this live XRotN damage pose.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c::ftCo_800A0DA4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doFlyRoll
      const float model_scale = hurtboxes_damageflyroll_live_xrotn_pose_owner(action_id)
                                    ? scale_y
                                    : (scale_y * model_scaling);
      const uint8_t apply_specialhi_xrotn =
          hurtboxes_runtime_specialhi_pose_owner(char_id, action_id);
      const uint8_t apply_damageflyroll_xrotn =
          hurtboxes_damageflyroll_live_xrotn_pose_owner(action_id);
      float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      if (action_id == (uint16_t)MSL_ACT_TURN && batch->state.turn_has_turned[idx] != 0u) {
        // Standing Turn has an internal facing owner that can lead the replay-visible facing lane.
        // ftCo_Turn_Enter records `facing_after = -fp->facing_dir`; ftCo_Turn_Anim_Inner flips
        // `fp->facing_dir` and sets `has_turned` once `frames_to_turn` has elapsed. BODY
        // collision consumes the runtime joint matrices via lb_8000B1CC, so hurtcap world space
        // must follow that internal `has_turned` orientation, not the stale visible facing byte.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
        //   ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
        // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
        facing_dir = -facing_dir;
      }
      if (hurtboxes_escapef_root_facing_owner(batch, idx)) {
        facing_dir = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
      }
      // Fallback policy: missing pose data for a specific capsule only drops that capsule, keeping
      // the rest usable under partial animation coverage.
      //
      // Ordering policy:
      // - Preserve init-table capsule ordering/identity (slot i corresponds to `caps[i]`).
      // - Disabled/intangible capsules (movescript) and capsules with missing pose data are kept in
      //   their original slot but marked `hurtcap_enabled=0` and given radius=0.
      uint32_t can_hit_mask = 0xFFFFFFFFu;
      (void)move_tables_hurtbox_can_hit_mask_at_frame(char_id, msid, frame, cap_count,
                                                      &can_hit_mask);

      uint16_t cap_part_ids[MSL_MAX_HURTCAPS];
      float cap_mats[MSL_MAX_HURTCAPS * 12u];
      uint8_t cap_mat_ok[MSL_MAX_HURTCAPS];
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        cap_part_ids[ci] = caps[ci].bone_part_id;
      }
      if (build_geometry == 0u) {
        for (uint16_t ci = 0; ci < cap_count; ci++) {
          const size_t hi = idx_hurtcap(bi, p, (int)ci);
          const uint8_t can_body_hit = (uint8_t)((can_hit_mask >> ci) & 0x1u);
          batch->state.hurtcap_enabled[hi] = can_body_hit;
          batch->state.hurtcap_radius[hi] = caps[ci].scale * model_scale;
          batch->state.hurtcap_is_grabbable[hi] = caps[ci].is_grabbable ? 1 : 0;
          batch->state.hurtcap_height[hi] = caps[ci].height;
        }
        batch->state.hurtcap_count[idx] = (uint8_t)cap_count;
        continue;
      }
      // HSD_AObjInterpretAnim owns JObj local SRT at float `cur_anim_frame`; landing-aerial
      // states can run non-integer animation rates, and lb_8000B1CC consumes the live matrix for
      // hurtcap endpoints before BODY admission. Keep integer SSANIM01 for actions whose
      // float-frame collision-pose owner has not been proven against replay/probe data.
      // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
      // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
      // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
      const float pose_sample_frame = hurtboxes_pose_sample_frame(
          batch, idx, char_id, pose_msid, action_id, anim_frame_f32, pose_frame);
      (void)anim_pose_get_collision_matrices_f32(batch, idx, pose_msid, pose_sample_frame,
                                                 cap_part_ids, cap_count, cap_mats, cap_mat_ok);
      float guard_tilt_mats[MSL_MAX_HURTCAPS * 12u];
      uint8_t guard_tilt_mat_ok[MSL_MAX_HURTCAPS];
      uint8_t use_guard_tilt_body_pose = 0u;
      float guard_tilt_mag = 0.0f;
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        guard_tilt_mat_ok[ci] = 0u;
      }
      if (hurtboxes_guard_tilt_live_body_pose_owner(batch, idx, pose_msid)) {
        guard_tilt_mag = batch->state.guard_tilt_x4[idx];
        if (guard_tilt_mag > 1.0f) {
          guard_tilt_mag = 1.0f;
        }
        uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[idx];
        const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
        if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
          guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
        }
        if (anim_pose_get_collision_matrices_f32(batch, idx, (uint16_t)MSL_SM_GUARD,
                                                 (float)guard_tilt_frame, cap_part_ids, cap_count,
                                                 guard_tilt_mats, guard_tilt_mat_ok) == 0) {
          use_guard_tilt_body_pose = 1u;
        }
      }
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        const size_t hi = idx_hurtcap(bi, p, (int)ci);
        batch->state.hurtcap_is_grabbable[hi] = caps[ci].is_grabbable ? 1 : 0;
        batch->state.hurtcap_height[hi] = caps[ci].height;

        const uint8_t can_body_hit = (uint8_t)((can_hit_mask >> ci) & 0x1u);
        if (!can_body_hit && !caps[ci].is_grabbable) {
          continue;
        }
        if (cap_mat_ok[ci] == 0u) {
          continue;
        }
        const float* m = &cap_mats[(size_t)ci * 12u];
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        msl_mtx34_mul_point(m, caps[ci].a_offset, &ax, &ay, &az);
        msl_mtx34_mul_point(m, caps[ci].b_offset, &bx, &by, &bz);

        if (use_guard_tilt_body_pose != 0u && guard_tilt_mat_ok[ci] != 0u) {
          float tax = 0.0f, tay = 0.0f, taz = 0.0f;
          float tbx = 0.0f, tby = 0.0f, tbz = 0.0f;
          const float* tm = &guard_tilt_mats[(size_t)ci * 12u];
          msl_mtx34_mul_point(tm, caps[ci].a_offset, &tax, &tay, &taz);
          msl_mtx34_mul_point(tm, caps[ci].b_offset, &tbx, &tby, &tbz);

          // Source owner: steady Guard_Anim calls ftCo_80091E78(..., 1). With nonzero
          // mv.co.guard.x4, that path samples the Guard tilt AObj at mv.co.guard.x8 and blends it
          // into the live JObj chain before ftColl_80078C70/lbColl_8000805C sample BODY
          // hurtcaps through lb_8000B1CC. These matrices come from the same extracted SSANIMT1 /
          // SSANIM01 Guard timeline used by the ShieldDesc guard-bone table; BODY no longer uses
          // a ShieldDesc-miss depth fallback in combat selection.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_80091E78}
          // refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006F4C8,ftAnim_80070710,ftAnim_80070108,ftAnim_8006FF74}
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
          ax += guard_tilt_mag * (tax - ax);
          ay += guard_tilt_mag * (tay - ay);
          az += guard_tilt_mag * (taz - az);
          bx += guard_tilt_mag * (tbx - bx);
          by += guard_tilt_mag * (tby - by);
          bz += guard_tilt_mag * (tbz - bz);
        }

        ax *= model_scale;
        ay *= model_scale;
        az *= model_scale;
        bx *= model_scale;
        by *= model_scale;
        bz *= model_scale;
        if (apply_specialhi_xrotn) {
          (void)hurtboxes_apply_specialhi_local_xrotn(batch, idx, char_id, msid, frame,
                                                      caps[ci].bone_part_id, facing_dir,
                                                      model_scale, &ax, &ay, &az);
          (void)hurtboxes_apply_specialhi_local_xrotn(batch, idx, char_id, msid, frame,
                                                      caps[ci].bone_part_id, facing_dir,
                                                      model_scale, &bx, &by, &bz);
        }
        if (apply_damageflyroll_xrotn) {
          (void)hurtboxes_apply_damageflyroll_local_xrotn(batch, idx, char_id, pose_msid, 0u,
                                                          caps[ci].bone_part_id, model_scale, &ax,
                                                          &ay, &az);
          (void)hurtboxes_apply_damageflyroll_local_xrotn(batch, idx, char_id, pose_msid, 0u,
                                                          caps[ci].bone_part_id, model_scale, &bx,
                                                          &by, &bz);
        }

        // Decomp: apply root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z.
        // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
        const float ax_rot_x = facing_dir * az;
        const float ax_rot_z = -facing_dir * ax;
        const float bx_rot_x = facing_dir * bz;
        const float bx_rot_z = -facing_dir * bx;
        ax = ax_rot_x;
        az = ax_rot_z;
        bx = bx_rot_x;
        bz = bx_rot_z;

        ax += pos_x;
        ay += pos_y;
        az += pos_z;
        bx += pos_x;
        by += pos_y;
        bz += pos_z;

        // Keep BODY-hit enablement separate from catch/grab geometry:
        // - BODY selection consumes `hurtcap_enabled`, which mirrors movescript hurtbox mode.
        // - Catch selection in ftColl_80078A2C gates on `hurt_capsules[j].is_grabbable` after the
        //   fighter-wide x1988/x198C/victim-mask checks; it does not use the body-hit capsule mask.
        // Populate grabbable capsule world positions even when `can_hit_mask` disables BODY hits,
        // so grabs can still connect against shield/guard victims.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        batch->state.hurtcap_enabled[hi] = can_body_hit;
        batch->state.hurtcap_a_x[hi] = ax;
        batch->state.hurtcap_a_y[hi] = ay;
        batch->state.hurtcap_a_z[hi] = az;
        batch->state.hurtcap_b_x[hi] = bx;
        batch->state.hurtcap_b_y[hi] = by;
        batch->state.hurtcap_b_z[hi] = bz;
        batch->state.hurtcap_radius[hi] = caps[ci].scale * model_scale;
      }

      batch->state.hurtcap_count[idx] = (uint8_t)cap_count;
      batch->state.hurtcap_geometry_valid[idx] = 1u;
    }
  }
}

void hurtboxes_refresh(MslBatch* batch) { hurtboxes_refresh_impl(batch, 0u); }

void hurtboxes_refresh_metadata(MslBatch* batch) { hurtboxes_refresh_impl(batch, 1u); }

void hurtboxes_refresh_contact_geometry(MslBatch* batch) { hurtboxes_refresh_impl(batch, 2u); }
