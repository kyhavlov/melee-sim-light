#include "knockdown.h"

#include <limits.h>
#include <math.h>

#include "action.h"
#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "msl_math.h"

enum { MSL_FTPART_HIPN = 4 };  // refs/melee/src/melee/ft/forward.h::Fighter_Part (FtPart_HipN)

static inline uint8_t is_down_bound(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_BOUND_U || a == (uint16_t)MSL_ACT_DOWN_BOUND_D) ? 1u : 0u;
}
static inline uint8_t is_down_wait(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_WAIT_U || a == (uint16_t)MSL_ACT_DOWN_WAIT_D) ? 1u : 0u;
}
static inline uint8_t is_down_stand(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_STAND_U || a == (uint16_t)MSL_ACT_DOWN_STAND_D) ? 1u : 0u;
}
static inline uint8_t is_down_attack(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_ATTACK_U || a == (uint16_t)MSL_ACT_DOWN_ATTACK_D) ? 1u : 0u;
}
static inline uint8_t is_down_roll(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_FOWARD_U || a == (uint16_t)MSL_ACT_DOWN_BACK_U ||
          a == (uint16_t)MSL_ACT_DOWN_FOWARD_D || a == (uint16_t)MSL_ACT_DOWN_BACK_D)
             ? 1u
             : 0u;
}
static inline uint8_t is_down_any(uint16_t a) {
  return (is_down_bound(a) || is_down_wait(a) || is_down_stand(a) || is_down_attack(a) ||
          is_down_roll(a))
             ? 1u
             : 0u;
}

static inline uint8_t is_passive(uint16_t a) { return (a == (uint16_t)MSL_ACT_PASSIVE) ? 1u : 0u; }
static inline uint8_t is_passive_stand(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_PASSIVE_STAND_F || a == (uint16_t)MSL_ACT_PASSIVE_STAND_B) ? 1u
                                                                                            : 0u;
}
static inline uint8_t is_knockdown_any(uint16_t a) {
  return (is_down_any(a) || is_passive(a) || is_passive_stand(a)) ? 1u : 0u;
}

static inline uint16_t down_wait_action_from_bound(uint16_t bound_act) {
  return (bound_act == (uint16_t)MSL_ACT_DOWN_BOUND_U) ? (uint16_t)MSL_ACT_DOWN_WAIT_U
                                                       : (uint16_t)MSL_ACT_DOWN_WAIT_D;
}

static inline uint16_t down_stand_action_from_wait(uint16_t wait_act) {
  return (wait_act == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? (uint16_t)MSL_ACT_DOWN_STAND_U
                                                     : (uint16_t)MSL_ACT_DOWN_STAND_D;
}

static inline uint16_t down_attack_action_from_wait(uint16_t wait_act) {
  return (wait_act == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? (uint16_t)MSL_ACT_DOWN_ATTACK_U
                                                     : (uint16_t)MSL_ACT_DOWN_ATTACK_D;
}

static inline uint32_t submotion_for_down_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DOWN_BOUND_U:
      return (uint32_t)MSL_SM_DOWN_BOUND_U;
    case (uint16_t)MSL_ACT_DOWN_WAIT_U:
      return (uint32_t)MSL_SM_DOWN_WAIT_U;
    case (uint16_t)MSL_ACT_DOWN_STAND_U:
      return (uint32_t)MSL_SM_DOWN_STAND_U;
    case (uint16_t)MSL_ACT_DOWN_ATTACK_U:
      return (uint32_t)MSL_SM_DOWN_ATTACK_U;
    case (uint16_t)MSL_ACT_DOWN_FOWARD_U:
      return (uint32_t)MSL_SM_DOWN_FOWARD_U;
    case (uint16_t)MSL_ACT_DOWN_BACK_U:
      return (uint32_t)MSL_SM_DOWN_BACK_U;
    case (uint16_t)MSL_ACT_DOWN_BOUND_D:
      return (uint32_t)MSL_SM_DOWN_BOUND_D;
    case (uint16_t)MSL_ACT_DOWN_WAIT_D:
      return (uint32_t)MSL_SM_DOWN_WAIT_D;
    case (uint16_t)MSL_ACT_DOWN_STAND_D:
      return (uint32_t)MSL_SM_DOWN_STAND_D;
    case (uint16_t)MSL_ACT_DOWN_ATTACK_D:
      return (uint32_t)MSL_SM_DOWN_ATTACK_D;
    case (uint16_t)MSL_ACT_DOWN_FOWARD_D:
      return (uint32_t)MSL_SM_DOWN_FOWARD_D;
    case (uint16_t)MSL_ACT_DOWN_BACK_D:
      return (uint32_t)MSL_SM_DOWN_BACK_D;
    case (uint16_t)MSL_ACT_PASSIVE:
      return (uint32_t)MSL_SM_PASSIVE;
    case (uint16_t)MSL_ACT_PASSIVE_STAND_F:
      return (uint32_t)MSL_SM_PASSIVE_STAND_F;
    case (uint16_t)MSL_ACT_PASSIVE_STAND_B:
      return (uint32_t)MSL_SM_PASSIVE_STAND_B;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint32_t submotion_for_damage_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DAMAGE_AIR_1:
      return (uint32_t)MSL_SM_DAMAGE_AIR_1;
    case (uint16_t)MSL_ACT_DAMAGE_AIR_2:
      return (uint32_t)MSL_SM_DAMAGE_AIR_2;
    case (uint16_t)MSL_ACT_DAMAGE_AIR_3:
      return (uint32_t)MSL_SM_DAMAGE_AIR_3;
    case (uint16_t)MSL_ACT_DAMAGE_FLY_HI:
      return (uint32_t)MSL_SM_DAMAGE_FLY_HI;
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
      return (uint32_t)MSL_SM_DAMAGE_FLY_N;
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW:
      return (uint32_t)MSL_SM_DAMAGE_FLY_LW;
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP:
      return (uint32_t)MSL_SM_DAMAGE_FLY_TOP;
    case (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL:
      return (uint32_t)MSL_SM_DAMAGE_FLY_ROLL;
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
      return (uint32_t)MSL_SM_DAMAGE_FALL;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint8_t anim_is_finished(uint8_t cid, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(cid, msid);
  if (!(end > 0.0f)) {
    return 0u;
  }
  // Decomp uses ftAnim_IsFramesRemaining; approximate deterministically with integer frame indices.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  const uint16_t cur = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(anim_frame_f32));
  const uint16_t end_i = msl_anim_frame_floor_u16(end);
  return cur >= end_i ? 1u : 0u;
}

static inline float apply_friction_ground(float gr_vel, float friction) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround
  float accel = friction;
  if (msl_absf(accel) > msl_absf(gr_vel)) {
    accel = -gr_vel;
  } else if (gr_vel > 0.0f) {
    accel = -accel;
  }
  return gr_vel + accel;
}

static inline void down_apply_phys_friction(MslBatch* batch, const MslCommonParams* c,
                                            const MslCharParams* ch, size_t idx) {
  // Decomp phys callback for DownBound/DownWait/DownStand/DownAttack: ft_80084F3C.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
  float friction = ch->gr_friction;
  if (msl_absf(batch->state.speed_ground_x_self[idx]) > ch->walk_max_vel) {
    friction *= c->high_speed_friction_mul;
  }
  batch->state.speed_ground_x_self[idx] =
      apply_friction_ground(batch->state.speed_ground_x_self[idx], friction);
}

static inline uint8_t down_roll_apply_phys_transn(MslBatch* batch, const MslCommonParams* c,
                                                  const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }

  // Decomp: downed roll Phys callback calls ft_80084FA8, which forwards to ft_80085030.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Phys
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80085030}
  //
  // ft_80085030 uses fp->x6A4_transNOffset.z * facing_dir as the target ground velocity when
  // fp->x594_b0 indicates TransN motion is active; otherwise it falls back to ground friction.
  //
  // Our ISO-derived SSANIM01 v3 artifacts store per-frame TransN translation as a tail (x,y,z);
  // approximate transNOffset.z as a finite difference between adjacent frames.
  const uint8_t cid = batch->state.char_id[idx];
  const uint32_t msid_u32 = batch->state.animation_index[idx];
  if (!(msid_u32 <= 0xFFFFu)) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)msid_u32;
  const uint16_t f_cur =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
  const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;

  float t_cur[3];
  float t_prev[3];
  if (anim_pose_get_transn(cid, msid, f_cur, t_cur) != 0 ||
      anim_pose_get_transn(cid, msid, f_prev, t_prev) != 0) {
    return 0u;
  }

  // Decomp uses transNOffset.z as the target ground velocity (after applying facing_dir).
  // For DownFoward/DownBack, TransN is active (ft_80085030's fp->x594_b0 branch).
  // Note: TransNPos is in fighter model space; apply per-character model scaling so the resulting
  // per-frame transNOffset matches engine/world units.
  // Source of truth for model scaling: ISO-extracted `data/characters/<char>.json` `model_scaling`.
  // Decomp: refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::model_scaling
  const float dz = (t_cur[2] - t_prev[2]) * ch->model_scaling;  // transNOffset.z finite difference
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  batch->state.speed_ground_x_self[idx] = dz * facing_dir;
  return 1u;
}

static inline float stick_angle_y_over_abs_x(float stick_x, float stick_y) {
  // Decomp:
  // - refs/melee/src/melee/ft/ftcommon.c::ftCo_GetLStickAngle
  // - refs/melee/src/melee/ft/ftcommon.c::ftCo_GetCStickAngle
  return atan2f(stick_y, msl_absf(stick_x));
}

static inline uint8_t cstick_up_edge(const MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float prev_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_c_y[idx]), c->lstick_deadzone_y);
  const float cur_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  // Decomp: refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF644 (cstick1.y < x7F4 && cstick.y >= x7F4).
  return (prev_y < c->down_attack_cstick_up_threshold &&
          cur_y >= c->down_attack_cstick_up_threshold)
             ? 1u
             : 0u;
}

static inline uint8_t cstick_roll_x_edge(const MslBatch* batch, const MslCommonParams* c,
                                         size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float prev_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_c_x[idx]), c->lstick_deadzone_x);
  const float cur_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  const float cur_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);

  // Decomp: refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF678
  // - ABS(cstick1.x) < x248 && ABS(cstick.x) >= x248 && ftCo_GetCStickAngle(fp) < x20_radians.
  if (!(msl_absf(prev_x) < c->down_stick_x_threshold)) {
    return 0;
  }
  if (!(msl_absf(cur_x) >= c->down_stick_x_threshold)) {
    return 0;
  }
  const float ang = stick_angle_y_over_abs_x(cur_x, cur_y);
  return (ang < c->attack_angle_threshold_radians) ? 1u : 0u;
}

static inline uint8_t lstick_roll_hold(const MslBatch* batch, const MslCommonParams* c,
                                       size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::inlineA0 (inside ftCo_Down_CheckInput)
  // - ABS(lstick.x) >= x248 && ftCo_GetLStickAngle(fp) < x20_radians.
  if (!(msl_absf(stick_x) >= c->down_stick_x_threshold)) {
    return 0;
  }
  const float ang = stick_angle_y_over_abs_x(stick_x, stick_y);
  return (ang < c->attack_angle_threshold_radians) ? 1u : 0u;
}

static inline uint16_t down_roll_action_from_input(const MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx, uint16_t cur_down_act) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_CheckInput
  // Stick selection:
  // - if ftCo_800DF678(fp): use c-stick.x (edge)
  // - else if inlineA0(fp): use l-stick.x (hold)
  // else: no roll.
  float stick_x = 0.0f;
  if (cstick_roll_x_edge(batch, c, idx)) {
    stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  } else if (lstick_roll_hold(batch, c, idx)) {
    stick_x =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  } else {
    return 0;
  }

  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  const uint8_t forward = ((stick_x * facing_dir) >= 0.0f) ? 1u : 0u;
  // NOTE: Decomp picks the U roll only when the *current* motion is DownWaitU; otherwise it
  // selects the D roll variant (including from DownBoundU/D).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_CheckInput
  const uint8_t use_u = (cur_down_act == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? 1u : 0u;
  if (use_u) {
    return forward ? (uint16_t)MSL_ACT_DOWN_FOWARD_U : (uint16_t)MSL_ACT_DOWN_BACK_U;
  }
  return forward ? (uint16_t)MSL_ACT_DOWN_FOWARD_D : (uint16_t)MSL_ACT_DOWN_BACK_D;
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC (common "enter Wait").
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  // Decomp: almost all motion-state enters call ftAnim_8006EBA4 immediately after
  // Fighter_ChangeMotionState so that the first post-enter frame is `anim_start`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098324
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009856C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void enter_down_wait(MslBatch* batch, size_t idx, uint16_t wait_act) {
  batch->state.action_id[idx] = wait_act;
  batch->state.animation_index[idx] = submotion_for_down_action(wait_act);
  // Decomp: DownBound->DownWait initializes mv.co.downwait.x0 to p_ftCommonData->x424.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
  {
    const MslCommonParams* c = msl_common_params();
    const int32_t frames = (c != NULL) ? (int32_t)c->down_wait_frames : 0;
    batch->state.downwait_timer[idx] =
        (frames <= 0) ? 0 : (frames >= (int32_t)INT16_MAX ? INT16_MAX : (int16_t)frames);
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void enter_down_stand(MslBatch* batch, size_t idx, uint16_t wait_act) {
  const uint16_t stand_act = down_stand_action_from_wait(wait_act);
  batch->state.action_id[idx] = stand_act;
  batch->state.animation_index[idx] = submotion_for_down_action(stand_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void enter_down_attack(MslBatch* batch, size_t idx, uint16_t wait_or_bound_act) {
  uint16_t atk_act = 0;
  if (wait_or_bound_act == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
      wait_or_bound_act == (uint16_t)MSL_ACT_DOWN_WAIT_D) {
    atk_act = down_attack_action_from_wait(wait_or_bound_act);
  } else if (wait_or_bound_act == (uint16_t)MSL_ACT_DOWN_BOUND_U) {
    atk_act = (uint16_t)MSL_ACT_DOWN_ATTACK_U;
  } else {
    atk_act = (uint16_t)MSL_ACT_DOWN_ATTACK_D;
  }
  batch->state.action_id[idx] = atk_act;
  batch->state.animation_index[idx] = submotion_for_down_action(atk_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void enter_down_roll(MslBatch* batch, size_t idx, uint16_t roll_act) {
  // Decomp: ftCo_80098324 is used by ftCo_Down_CheckInput to enter DownFoward/DownBack and calls
  // Fighter_ChangeMotionState + ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098324
  batch->state.action_id[idx] = roll_act;
  batch->state.animation_index[idx] = submotion_for_down_action(roll_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline uint8_t should_enter_squat_from_wait(const MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  // Decomp: ftCo_Wait_IASA -> ftCo_800D5FB0 (Squat enter).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
  //
  // Gate: fp->input.lstick.y < -p_ftCommonData->x90.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_CheckInput
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  return (stick_y < -c->crouch_stick_threshold) ? 1u : 0u;
}

static inline void enter_squat(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_Squat_Enter calls Fighter_ChangeMotionState(ftCo_MS_Squat) + ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline uint8_t should_enter_down_attack_from_bound(const MslBatch* batch,
                                                          const MslCommonParams* c, size_t idx) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
  // - inlineB0: (x67C < x24C || x67D < x24C)
  // - ftCo_800DF644: cstick up edge at x7F4
  if ((float)batch->state.x67C[idx] < c->down_attack_button_window_frames ||
      (float)batch->state.x67D[idx] < c->down_attack_button_window_frames) {
    return 1u;
  }
  return cstick_up_edge(batch, c, idx);
}

static inline uint8_t should_enter_down_attack_from_wait(const MslBatch* batch,
                                                         const MslCommonParams* c, size_t idx) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800984D4
  // - pressed-edge A/B or cstick up edge.
  enum { AB = (uint16_t)MSL_BUTTON_A | (uint16_t)MSL_BUTTON_B };
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)AB) != 0) {
    return 1u;
  }
  return cstick_up_edge(batch, c, idx);
}

static inline uint8_t should_enter_down_stand_from_wait(const MslBatch* batch,
                                                        const MslCommonParams* c, size_t idx) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_800980BC
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0) {
    return 1u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(stick_y >= c->down_stand_stick_y_threshold)) {
    return 0u;
  }
  const float ang = stick_angle_y_over_abs_x(stick_x, stick_y);
  return (ang >= c->attack_angle_threshold_radians) ? 1u : 0u;
}

void knockdown_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a0 = batch->state.action_id[idx];
      if (!is_knockdown_any(a0)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      // Keep animation_index consistent with GALE01 submotion ids.
      batch->state.animation_index[idx] = submotion_for_down_action(a0);

      const uint8_t cid = batch->state.char_id[idx];
      const float anim_frame = batch->state.anim_frame_f32[idx];

      if (is_passive(a0)) {
        // Decomp: ftCo_Passive_Phys uses ft_80084F3C.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Phys
        down_apply_phys_friction(batch, c, ch, idx);
        if (anim_is_finished(cid, (uint16_t)MSL_SM_PASSIVE, anim_frame)) {
          enter_wait(batch, idx);
        }
        continue;
      }

      if (is_passive_stand(a0)) {
        // PassiveStand phys uses ft_80084FA8 (root-motion + friction via ft_80085030). We don't yet
        // model root motion here; leave self velocity teacher-forced and only handle anim-end exits.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
        const uint16_t msid = (a0 == (uint16_t)MSL_ACT_PASSIVE_STAND_F)
                                  ? (uint16_t)MSL_SM_PASSIVE_STAND_F
                                  : (uint16_t)MSL_SM_PASSIVE_STAND_B;
        if (anim_is_finished(cid, msid, anim_frame)) {
          enter_wait(batch, idx);
        }
        continue;
      }

      if (is_down_roll(a0)) {
        // DownFoward/DownBack share ftCo_Down_Anim/Phys/Coll (common downed roll actions).
        // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::{ftCo_Down_Anim,ftCo_Down_Phys,ftCo_Down_Coll}
        // Anim-end exits to Wait (ftCo_Down_Anim -> ft_8008A2BC).
        const uint32_t msid_u32 = submotion_for_down_action(a0);
        if ((msid_u32 <= 0xFFFFu) && anim_is_finished(cid, (uint16_t)msid_u32, anim_frame)) {
          // IMPORTANT ordering: Anim/IASA run before Phys in-engine. When the roll anim ends and
          // transitions to Wait, any immediate Wait IASA (shield/crouch) should happen before we
          // apply per-frame Phys. This prevents "one extra roll root-motion frame" when the player
          // buffers shield/crouch on the roll end frame.
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (Anim/IASA before Phys)
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Anim (end->ft_8008A2BC)
          enter_wait(batch, idx);

          // Same-frame Wait IASA subset after roll end (decomp-shaped ordering).
          // Wait IASA includes guard entry (ftCo_80091A4C) and squat entry (ftCo_800D5FB0).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          guard_update_grounded(batch, c, idx, 1);
          if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_WAIT &&
              should_enter_squat_from_wait(batch, c, idx)) {
            enter_squat(batch, idx);
          }

          // Apply the new state's Phys in the same frame (ft_80084F3C friction path).
          //
          // Decomp:
          // - Wait phys uses ft_80084F3C.
          // - Squat phys uses ft_80084F3C.
          // - Guard phys paths also include ft_80084F3C ground friction.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Phys
          down_apply_phys_friction(batch, c, ch, idx);
        } else {
          // Phys: ft_80084FA8 (TransN-driven ground velocity target; see down_roll_apply_phys_transn).
          down_roll_apply_phys_transn(batch, c, ch, idx);
        }
        continue;
      }

      // Ground phys for downed states (ft_80084F3C).
      down_apply_phys_friction(batch, c, ch, idx);

      if (is_down_bound(a0)) {
        const uint16_t msid = (a0 == (uint16_t)MSL_ACT_DOWN_BOUND_U)
                                  ? (uint16_t)MSL_SM_DOWN_BOUND_U
                                  : (uint16_t)MSL_SM_DOWN_BOUND_D;
        if (anim_is_finished(cid, msid, anim_frame)) {
          if (should_enter_down_attack_from_bound(batch, c, idx)) {
            enter_down_attack(batch, idx, a0);
          } else {
            const uint16_t roll_act = down_roll_action_from_input(batch, c, idx, a0);
            if (roll_act != 0) {
              enter_down_roll(batch, idx, roll_act);
              // Decomp ordering: when an Anim callback changes the motion state, the new state's
              // Phys runs later in the same frame (after Anim/IASA). Apply roll root motion now so
              // physics_integrate uses it this frame.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Phys
              down_roll_apply_phys_transn(batch, c, ch, idx);
            } else {
              enter_down_wait(batch, idx, down_wait_action_from_bound(a0));
            }
            // Decomp engine ordering: after an Anim callback changes the motion state, the new
            // state's IASA may run later in the same frame. This matters for DownBound->DownWait
            // where holding up/LR can immediately trigger DownStand.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_IASA
            if (is_down_wait(batch->state.action_id[idx])) {
              if (should_enter_down_attack_from_wait(batch, c, idx)) {
                enter_down_attack(batch, idx, batch->state.action_id[idx]);
              } else {
                const uint16_t roll_act2 =
                    down_roll_action_from_input(batch, c, idx, batch->state.action_id[idx]);
                if (roll_act2 != 0) {
                  enter_down_roll(batch, idx, roll_act2);
                  down_roll_apply_phys_transn(batch, c, ch, idx);
                } else if (should_enter_down_stand_from_wait(batch, c, idx)) {
                  enter_down_stand(batch, idx, batch->state.action_id[idx]);
                }
              }
            }
          }
        }
        continue;
      }

      if (is_down_wait(a0)) {
        // Auto getup when the DownWait timer expires (mv.co.downwait.x0).
        // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Anim
        //
        // Note: fp->x2224_b2 is not exposed by Slippi post-frame state_flags, but hitlag already
        // gates this whole callback, and the suite does not cover x2224_b2==1 cases yet.
        int16_t t = batch->state.downwait_timer[idx];
        if (t > 0) {
          t = (int16_t)(t - 1);
          batch->state.downwait_timer[idx] = t;
        }
        if (t <= 0) {
          enter_down_stand(batch, idx, a0);
          continue;
        }

        // IASA: Attack > Roll > Stand.
        // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_IASA
        if (should_enter_down_attack_from_wait(batch, c, idx)) {
          enter_down_attack(batch, idx, a0);
          continue;
        }
        {
          const uint16_t roll_act = down_roll_action_from_input(batch, c, idx, a0);
          if (roll_act != 0) {
            enter_down_roll(batch, idx, roll_act);
            // Decomp: ftCo_Down_CheckInput enters roll in IASA and the roll Phys runs later in the
            // same frame.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Phys
            down_roll_apply_phys_transn(batch, c, ch, idx);
            continue;
          }
        }
        if (should_enter_down_stand_from_wait(batch, c, idx)) {
          enter_down_stand(batch, idx, a0);
          continue;
        }

        continue;
      }

      if (is_down_stand(a0)) {
        const uint16_t msid = (a0 == (uint16_t)MSL_ACT_DOWN_STAND_U)
                                  ? (uint16_t)MSL_SM_DOWN_STAND_U
                                  : (uint16_t)MSL_SM_DOWN_STAND_D;
        if (anim_is_finished(cid, msid, anim_frame)) {
          enter_wait(batch, idx);
        }
        continue;
      }

      if (is_down_attack(a0)) {
        const uint16_t msid = (a0 == (uint16_t)MSL_ACT_DOWN_ATTACK_U)
                                  ? (uint16_t)MSL_SM_DOWN_ATTACK_U
                                  : (uint16_t)MSL_SM_DOWN_ATTACK_D;
        if (anim_is_finished(cid, msid, anim_frame)) {
          enter_wait(batch, idx);
        }
        continue;
      }
    }
  }
}

static inline uint8_t is_damage_fly_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DAMAGE_FLY_HI:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_damage_air_action(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DAMAGE_AIR_1 || a == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
          a == (uint16_t)MSL_ACT_DAMAGE_AIR_3)
             ? 1u
             : 0u;
}

static inline void transfer_air_to_ground_on_land(MslBatch* batch, const MslCharParams* ch,
                                                  size_t idx) {
  // Shared with locomotion landing behavior: transfer air X to ground X and refresh jumps.
  // Decomp: refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4 (jumps refresh on grounding)
  batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.fall_fast[idx] = 0;
  batch->state.jumps_left[idx] = ch->max_jumps;
}

static inline uint8_t tech_is_available(const MslBatch* batch, const MslCommonParams* c,
                                        size_t idx) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
  // - x680 < x250 and x684 >= x1C.
  return ((float)batch->state.x680[idx] < c->tech_window_frames &&
          batch->state.x684[idx] >= c->tech_lr_debounce_frames)
             ? 1u
             : 0u;
}

static inline void enter_passive_from_damage_land(MslBatch* batch, const MslCharParams* ch,
                                                  size_t idx, uint16_t passive_act) {
  transfer_air_to_ground_on_land(batch, ch, idx);
  batch->state.action_id[idx] = passive_act;
  batch->state.animation_index[idx] = submotion_for_down_action(passive_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (batch->state.hitlag[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline uint16_t pick_downbound_action_from_pose(const MslBatch* batch, size_t idx,
                                                       uint16_t cur_action_id) {
  if (batch == NULL) {
    return (uint16_t)MSL_ACT_DOWN_BOUND_U;
  }

  const uint8_t cid = batch->state.char_id[idx];
  uint32_t msid_u32 = batch->state.animation_index[idx];
  if (!(msid_u32 <= 0xFFFFu)) {
    msid_u32 = submotion_for_damage_action(cur_action_id);
  }
  if (!(msid_u32 <= 0xFFFFu)) {
    return (uint16_t)MSL_ACT_DOWN_BOUND_U;
  }

  const uint16_t frame =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
  float m[12];
  if (anim_pose_get_matrix(cid, (uint16_t)msid_u32, frame, (uint16_t)MSL_FTPART_HIPN, m) != 0) {
    return (uint16_t)MSL_ACT_DOWN_BOUND_U;
  }

  // Decomp: ftCo_80097570 uses the Hip joint matrix and returns true when a chosen element is > 0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097570
  const float f = m[5];  // m11 (row 1, col 1) when using the "x2226_b0 == 0" axis selection.
  return (f > 0.0f) ? (uint16_t)MSL_ACT_DOWN_BOUND_U : (uint16_t)MSL_ACT_DOWN_BOUND_D;
}

static inline void enter_down_bound_from_damage_land(MslBatch* batch, const MslCharParams* ch,
                                                     size_t idx, uint16_t prev_action_id) {
  const uint16_t bound_act = pick_downbound_action_from_pose(batch, idx, prev_action_id);
  transfer_air_to_ground_on_land(batch, ch, idx);
  batch->state.action_id[idx] = bound_act;
  batch->state.animation_index[idx] = submotion_for_down_action(bound_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // Decomp: DownBound entry clears A/B press timers so buffered presses from before landing don't
  // trigger the getup attack window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C
  batch->state.x67C[idx] = 0xFFu;
  batch->state.x67D[idx] = 0xFFu;
}

void knockdown_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a0 = batch->state.action_id[idx];
      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1u : 0u;

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      if (!was_ground && now_ground) {
        // Landing transitions into DownBound from tumble-style damage states.
        //
        // Decomp entry points:
        // - Damage: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
        // - DamageFly: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
        if (is_damage_fly_action(a0)) {
          // Decomp: DamageFly landing calls ftCo_80090184:
          // - tech-roll (PassiveStandF/B): refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
          // - tech-in-place (Passive): refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009872C
          // - else: DownBound via ftCo_80097D40 (ftCo_DownBound.c)
          if (tech_is_available(batch, c, idx)) {
            const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                                 c->lstick_deadzone_x);
            if (msl_absf(stick_x) >= c->tech_roll_stick_threshold) {
              const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
              const uint16_t act = (stick_x * facing_dir) >= 0.0f
                                       ? (uint16_t)MSL_ACT_PASSIVE_STAND_F
                                       : (uint16_t)MSL_ACT_PASSIVE_STAND_B;
              enter_passive_from_damage_land(batch, ch, idx, act);
              continue;
            }
            enter_passive_from_damage_land(batch, ch, idx, (uint16_t)MSL_ACT_PASSIVE);
            continue;
          }
          enter_down_bound_from_damage_land(batch, ch, idx, a0);
          continue;
        }

        if (is_damage_air_action(a0)) {
          const float kbx = batch->state.speed_x_attack[idx];
          const float kby = batch->state.speed_y_attack[idx];
          const float mag = sqrtf(kbx * kbx + kby * kby);
          if (mag >= c->damagefly_downbound_kb_vel_threshold) {
            enter_down_bound_from_damage_land(batch, ch, idx, a0);
            continue;
          }
          if (mag >= c->damagefly_landing_kb_vel_threshold) {
            // Decomp: ftCo_Landing_Enter_Basic.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
            transfer_air_to_ground_on_land(batch, ch, idx);
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            continue;
          }
        }
      } else if (was_ground && !now_ground) {
        // Downed ground -> air fallback: enter Fall.
        // Decomp: DownBound_Coll/DownStand_Coll/DownWait_Coll/DownAttack_Coll select common ground
        // collision helpers which transition into Fall when no longer grounded.
        if (!is_knockdown_any(a0)) {
          continue;
        }

        batch->state.fall_fast[idx] = 0;
        batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;
        batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
    }
  }
}
