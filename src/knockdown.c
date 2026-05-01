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
#include "coll_env_flags.h"
#include "common_params.h"
#include "input_axis.h"
#include "jump_input.h"
#include "locomotion.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_ground.h"
#include "msl_math.h"
#include "stage_collision.h"
#include "state_flags.h"
#include "turn.h"

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
static inline uint8_t is_down_damage(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_DOWN_DAMAGE_U || a == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) ? 1u : 0u;
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
static inline uint8_t is_passivewall_action(uint16_t a);
static inline void passivewall_launch_from_timer_expiry(MslBatch* batch, const MslCharParams* ch,
                                                        size_t idx);
static inline uint8_t passivewall_iasa_try_air_options(MslBatch* batch, const MslCommonParams* c,
                                                       const MslCharParams* ch, size_t idx);

static inline uint16_t down_wait_action_from_bound(uint16_t bound_act) {
  return (bound_act == (uint16_t)MSL_ACT_DOWN_BOUND_U) ? (uint16_t)MSL_ACT_DOWN_WAIT_U
                                                       : (uint16_t)MSL_ACT_DOWN_WAIT_D;
}

static inline uint16_t down_wait_action_from_damage(uint16_t damage_act) {
  return (damage_act == (uint16_t)MSL_ACT_DOWN_DAMAGE_U) ? (uint16_t)MSL_ACT_DOWN_WAIT_U
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
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_U:
      return (uint32_t)MSL_SM_DOWN_DAMAGE_U;
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
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_D:
      return (uint32_t)MSL_SM_DOWN_DAMAGE_D;
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
    case (uint16_t)MSL_ACT_FLY_REFLECT_WALL:
      return (uint32_t)MSL_SM_WALL_DAMAGE;
    case (uint16_t)MSL_ACT_FLY_REFLECT_CEIL:
      return (uint32_t)MSL_SM_STOP_CEIL;
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
      return (uint32_t)MSL_SM_DAMAGE_FALL;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint8_t damage_landing_action_owns_root_floor_snap(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DAMAGE_HI_1:
    case (uint16_t)MSL_ACT_DAMAGE_HI_2:
    case (uint16_t)MSL_ACT_DAMAGE_HI_3:
    case (uint16_t)MSL_ACT_DAMAGE_N_1:
    case (uint16_t)MSL_ACT_DAMAGE_N_2:
    case (uint16_t)MSL_ACT_DAMAGE_N_3:
    case (uint16_t)MSL_ACT_DAMAGE_LW_1:
    case (uint16_t)MSL_ACT_DAMAGE_LW_2:
    case (uint16_t)MSL_ACT_DAMAGE_LW_3:
    case (uint16_t)MSL_ACT_DAMAGE_AIR_1:
    case (uint16_t)MSL_ACT_DAMAGE_AIR_2:
    case (uint16_t)MSL_ACT_DAMAGE_AIR_3:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_HI:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL:
    case (uint16_t)MSL_ACT_FLY_REFLECT_WALL:
    case (uint16_t)MSL_ACT_FLY_REFLECT_CEIL:
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
      return 1u;
    default:
      return 0u;
  }
}

static inline void snap_root_y_to_ground_line_on_damage_land(MslBatch* batch, size_t bi,
                                                             size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return;
  }

  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  const float x = batch->state.pos_x[idx];
  float y = line->y0;
  // Decomp/data ownership for grounded root Y on landing:
  // - Damage/DamageFly collision callbacks resolve floor contact before ftCo_80090184 /
  //   ftCo_Landing_Enter_Basic choose the grounded destination state for the same frame.
  // - mpLib_8004DD90_Floor projects onto the owning floor line and applies a +0.0001 bias.
  // - The floor line itself comes from the ISO-derived stage collision graph loaded into
  //   stage_collision.{c,h}.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll,ftCo_80090184}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/stages/final_destination.json: collision.segments
  if (fabsf(line->x1 - line->x0) > 0.0001f) {
    y = ((line->y1 - line->y0) * (x - line->x0) / (line->x1 - line->x0)) + line->y0;
  }
  batch->state.pos_y[idx] = y + 0.0001f;
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
  // Our ISO-derived SSANIM01 v4 artifacts store per-frame TransN translation as a tail (x,y,z);
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

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1u : 0u;
}

static inline uint16_t jump_aerial_action_from_stick(const MslCommonParams* c, float stick_x,
                                                     float facing_dir) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? (uint16_t)MSL_ACT_JUMP_AERIAL_F
                                                            : (uint16_t)MSL_ACT_JUMP_AERIAL_B;
}

static inline uint8_t damage_jump_input_from_edges(const MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_XY) != 0) {
    return 1u;
  }

  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  return did_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx]);
}

static inline void enter_squat(MslBatch* batch, size_t idx);

static inline uint8_t damage_ground_try_enter_kneebend_from_wait_iasa(MslBatch* batch,
                                                                      const MslCommonParams* c,
                                                                      size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Decomp call chain:
  // - ftCo_Damage_IASA: when !x221C_b6 and grounded, delegates to ftCo_Wait_IASA.
  // - Before that delegate, it ORs XY when (mv.co.damage.x14 && x14 <= p_ftCommonData->x1D0).
  // - ftCo_Wait_IASA then reaches ftCo_Jump_CheckInput -> ftCo_Jump_GetInput ->
  //   ftCo_KneeBend_Enter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{ftCo_Jump_CheckInput,ftCo_Jump_GetInput}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Enter
  uint8_t jump_input = (uint8_t)MSL_JUMP_INPUT_NONE;
  const uint16_t x14 = batch->state.damage_jump_buffer_x14[idx];
  if (x14 != 0u && (float)x14 <= c->damage_jump_buffer_window_frames) {
    const float stick_y =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
    const uint8_t tap_jump = did_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx]);
    // Damage_IASA injects the buffered x14 path by ORing HSD_PAD_XY into input.x668, then
    // delegates to Wait_IASA. The downstream ftCo_Jump_GetInput still prioritizes a live
    // L-stick tap-jump over that injected XY bit; preserving that priority keeps held-up
    // damage-buffered jumps from being misclassified as released button short hops.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
    jump_input = tap_jump ? (uint8_t)MSL_JUMP_INPUT_LSTICK : (uint8_t)MSL_JUMP_INPUT_XY;
  } else if (damage_jump_input_from_edges(batch, c, idx)) {
    const float stick_y =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
    const uint8_t tap_jump = did_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx]);
    jump_input = tap_jump ? (uint8_t)MSL_JUMP_INPUT_LSTICK : (uint8_t)MSL_JUMP_INPUT_XY;
  }
  if (jump_input == (uint8_t)MSL_JUMP_INPUT_NONE) {
    return 0u;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.kneebend_jump_input[idx] = jump_input;
  batch->state.kneebend_is_short_hop[idx] = 0u;
  return 1u;
}

static inline uint8_t damage_ground_try_enter_guard_from_wait_iasa(MslBatch* batch,
                                                                   const MslCommonParams* c,
                                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard(batch, c, idx)) {
    return 1u;
  }
  const uint16_t buttons = batch->state.input_buttons[idx];
  if ((buttons & (uint16_t)MSL_BUTTON_Z) == 0u) {
    return 0u;
  }
  if (!(batch->state.shield_hp[idx] > 0.0f)) {
    return 0u;
  }
  // Grounded Damage_IASA parity subset:
  // - ftCo_Damage_IASA delegates to Wait_IASA on ground when !x221C_b6.
  // - This narrowed simulator subset does not model Wait_IASA's earlier attack/grab ownership
  //   lanes, but grounded damage still needs the Z-mapped shield-hold lane to flow into
  //   `ftCo_80091A4C` and enter GuardOn on the same frame when those earlier owners do not
  //   consume.
  // - Keep that bridge local to grounded Damage_IASA so general locomotion guard ownership stays
  //   on the existing LR/trigger path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800923B4}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  // GuardOn entry helper shape:
  // - ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0
  // - ftCo_800924C0 calls ftAnim_8006EBA4 immediately and leaves GuardOn on the no-submotion
  //   Slippi snapshot shape (animation_index=-1, action_frame/state_age=-1).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800923B4,ftCo_800924C0}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B3 = 0x10 };
  enum { MSL_STATE_FLAG_221C_B1 = 0x40 };
  enum { MSL_STATE_FLAG_221C_B2 = 0x20 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(
      uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  batch->state.guard_release_latched_xc[idx] = 0;
  {
    uint16_t t = (uint16_t)c->guard_x10_init_frames;
    if (t > 255u) {
      t = 255u;
    }
    batch->state.guard_x10[idx] = (uint8_t)t;
  }
  batch->state.lightshield_amount[idx] = 0.0f;
  return 1u;
}

static inline uint16_t damage_ground_wait_iasa_walk_action(const MslCommonParams* c,
                                                           const MslCharParams* ch, float gr_vel) {
  if (c == NULL || ch == NULL) {
    return (uint16_t)MSL_ACT_WALK_SLOW;
  }
  // Decomp: Wait_IASA walk entry uses ftWalkCommon_GetWalkType on grounded velocity.
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
  const float v = msl_absf(gr_vel);
  if (v >= (c->walk_fast_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_FAST;
  }
  if (v >= (c->walk_mid_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_MIDDLE;
  }
  return (uint16_t)MSL_ACT_WALK_SLOW;
}

static inline uint32_t damage_ground_wait_iasa_walk_submotion(uint16_t walk_action) {
  switch (walk_action) {
    case MSL_ACT_WALK_FAST:
      return (uint32_t)MSL_SM_WALK_FAST;
    case MSL_ACT_WALK_MIDDLE:
      return (uint32_t)MSL_SM_WALK_MIDDLE;
    case MSL_ACT_WALK_SLOW:
    default:
      return (uint32_t)MSL_SM_WALK_SLOW;
  }
}

static inline uint8_t damage_ground_wait_iasa_is_dash_flick(const MslCommonParams* c, float stick_x,
                                                            uint8_t tilt_timer_x) {
  if (c == NULL) {
    return 0u;
  }
  // Decomp: Wait_IASA delegates to ftCo_Dash_CheckInput before Squat/Turn/Walk.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  const float ax = msl_absf(stick_x);
  return (ax >= c->dash_flick_abs && tilt_timer_x < c->dash_flick_tilt_max_frames) ? 1u : 0u;
}

static inline uint8_t damage_ground_wait_iasa_specials_has_input(const MslCommonParams* c,
                                                                 uint16_t buttons, float stick_x) {
  if (c == NULL) {
    return 0u;
  }
  // Wait_IASA routes through ftCo_SpecialS_CheckInput before Squat. Side-B only consumes grounded
  // held-B rows once ABS(lstick.x) >= p_ftCommonData->x218.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
  //   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
  // data/common/ft_common_data.json: special_stick_x_threshold_side
  return ((buttons & (uint16_t)MSL_BUTTON_B) != 0u &&
          msl_absf(stick_x) >= c->special_stick_x_threshold_side)
             ? 1u
             : 0u;
}

static inline uint8_t damage_ground_try_wait_iasa_locomotion_subset(MslBatch* batch,
                                                                    const MslCommonParams* c,
                                                                    const MslCharParams* ch,
                                                                    size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint8_t specials_has_input =
      damage_ground_wait_iasa_specials_has_input(c, buttons, stick_x);

  if (damage_ground_wait_iasa_is_dash_flick(c, stick_x, batch->state.tilt_timer_x[idx])) {
    if ((stick_x * facing_dir) < 0.0f) {
      // Decomp: Wait_IASA -> Dash_CheckInput can enter smash-turn on opposite-facing dash flick.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
      batch->state.turn_has_turned[idx] = 0;
      batch->state.turn_frames_to_turn[idx] = 0;
      batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      msl_anim_timebase_tick_once(batch, idx);
    } else {
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
      batch->state.dash_x4[idx] = 1u;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      msl_anim_timebase_tick_once(batch, idx);
      batch->state.tilt_timer_x[idx] = 0xFEu;
    }
    return 1u;
  }

  if (!specials_has_input && stick_y < -c->crouch_stick_threshold) {
    // Decomp: Wait_IASA checks Squat input after Dash and before Turn/Walk.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
    enter_squat(batch, idx);
    return 1u;
  }

  if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
    // Decomp: Wait_IASA turn path enters the normal Turn state (not smash-turn).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
    batch->state.turn_has_turned[idx] = 0;
    batch->state.turn_frames_to_turn[idx] = msl_turn_basic_frames_to_turn_for_entry(ch);
    batch->state.turn_x8[idx] = 0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    return 1u;
  }

  if (msl_absf(stick_x) >= c->walk_stick_threshold) {
    // Decomp: Wait_IASA falls through to Walk_CheckInput, which picks walk type from current
    // grounded velocity and enters Walk with an immediate ftAnim tick.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
    // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
    const uint16_t walk_action =
        damage_ground_wait_iasa_walk_action(c, ch, batch->state.speed_ground_x_self[idx]);
    batch->state.action_id[idx] = walk_action;
    batch->state.animation_index[idx] = damage_ground_wait_iasa_walk_submotion(walk_action);
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    return 1u;
  }

  return 0u;
}

static inline uint8_t damage_air_try_jump_aerial(MslBatch* batch, const MslCommonParams* c,
                                                 const MslCharParams* ch, size_t idx,
                                                 uint8_t force_jump_input) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  if (batch->state.jumps_left[idx] == 0) {
    return 0u;
  }

  const uint8_t has_jump_input =
      force_jump_input ? 1u : damage_jump_input_from_edges(batch, c, idx);
  if (!has_jump_input) {
    return 0u;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
  const uint32_t msid = (act == (uint16_t)MSL_ACT_JUMP_AERIAL_F) ? (uint32_t)MSL_SM_JUMP_AERIAL_F
                                                                 : (uint32_t)MSL_SM_JUMP_AERIAL_B;

  // ftCo_Damage inlineC0 path: on buffered jump gate, call ftCo_800CB870 which enters JumpAerial.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{inlineC0,ftCo_Damage_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = msid;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
  batch->state.speed_y_self[idx] = ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.fall_fast[idx] = 0u;
  batch->state.jumps_left[idx]--;
  // Decomp: ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.ecb_lock_timer[idx] = 10u;
  return 1u;
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

static inline uint16_t down_roll_action_from_prev_input_for_bound(const MslBatch* batch,
                                                                  const MslCommonParams* c,
                                                                  size_t idx,
                                                                  uint16_t cur_down_act) {
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  if (!(msl_absf(stick_x) >= c->down_stick_x_threshold)) {
    return 0;
  }
  const float ang = stick_angle_y_over_abs_x(stick_x, stick_y);
  if (!(ang < c->attack_angle_threshold_radians)) {
    return 0;
  }

  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  const uint8_t forward = ((stick_x * facing_dir) >= 0.0f) ? 1u : 0u;
  const uint8_t use_u = (cur_down_act == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? 1u : 0u;
  if (use_u) {
    return forward ? (uint16_t)MSL_ACT_DOWN_FOWARD_U : (uint16_t)MSL_ACT_DOWN_BACK_U;
  }
  return forward ? (uint16_t)MSL_ACT_DOWN_FOWARD_D : (uint16_t)MSL_ACT_DOWN_BACK_D;
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC (common "enter Wait").
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A348
  //
  // Ordering note:
  // - This path calls Fighter_ChangeMotionState(ftCo_MS_Wait, ...) via ft_8008A348.
  // - Unlike explicit immediate-tick helpers (e.g. ftCo_80098324), this path does not call
  //   ftAnim_8006EBA4 directly after the state change.
  // Keep Wait entry at frame 0 here; the normal per-frame anim update runs next frame.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t should_enter_squat_from_wait(const MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx);

static inline void passive_stand_anim_end_try_enter_squat(MslBatch* batch, const MslCommonParams* c,
                                                          size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // PassiveStand end -> Wait happens during Anim via ft_8008A2BC. In the rerun11 window the
  // destination frame immediately admits Squat on held-down input, but letting the full Wait_IASA
  // chain run here over-consumes shield input before locomotion_update_pre() reaches the normal
  // GuardOn owner. Keep only the proven crouch admission bridge in this owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Anim
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_WAIT &&
      wait_iasa_try_enter_spotdodge_before_guard(batch, c, idx)) {
    return;
  }
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_WAIT &&
      should_enter_squat_from_wait(batch, c, idx)) {
    enter_squat(batch, idx);
  }
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
  if (batch->state.hitlag_started_frame[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void enter_down_stand(MslBatch* batch, size_t idx, uint16_t wait_act) {
  const uint16_t stand_act = down_stand_action_from_wait(wait_act);
  batch->state.action_id[idx] = stand_act;
  batch->state.animation_index[idx] = submotion_for_down_action(stand_act);
  // Decomp: DownStand entry uses ftCo_80098160, which only calls Fighter_ChangeMotionState.
  // Unlike ftCo_80097E8C (DownWait) and ftCo_80098324 (DownFoward/DownBack), it does not call
  // ftAnim_8006EBA4 on the entry frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_80098160
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
  //   ftCo_DownWait_Anim,ftCo_DownWait_IASA}
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
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
  if (batch->state.hitlag_started_frame[idx] == 0) {
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
  if (batch->state.hitlag_started_frame[idx] == 0) {
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
  if (batch->state.hitlag_started_frame[idx] == 0) {
    batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline uint8_t should_enter_down_attack_from_bound(const MslBatch* batch,
                                                          const MslCommonParams* c, size_t idx) {
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
  // - inlineB0: (x67C < x24C || x67D < x24C)
  // - ftCo_800DF644: cstick up edge at x7F4
  //
  // Reseed ownership bridge:
  // - DownBound entry resets x67C/x67D to 0xFF (ftCo_8009794C), then Fighter input timers run
  //   causally from that reset point.
  // - On teacher-forced reseed rows inside DownBound (action_frame > 0), stale pre-entry A/B timer
  //   values can over-trigger this lane if they were not materialized from the entry-reset history.
  // - Restrict the timer lane to post-entry presses by requiring timer < action_frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
  const uint8_t x67c_recent =
      ((float)batch->state.x67C[idx] < c->down_attack_button_window_frames) ? 1u : 0u;
  const uint8_t x67d_recent =
      ((float)batch->state.x67D[idx] < c->down_attack_button_window_frames) ? 1u : 0u;
  const uint8_t ab_pressed_now =
      ((batch->state.input_buttons_pressed[idx] &
        (uint16_t)((uint16_t)MSL_BUTTON_A | (uint16_t)MSL_BUTTON_B)) != 0u)
          ? 1u
          : 0u;
  if (x67c_recent || x67d_recent) {
    const int16_t af_i16 = batch->state.action_frame[idx];
    const uint16_t af = (af_i16 > 0) ? (uint16_t)af_i16 : 0u;
    // DownBound_Anim runs in Fighter_8006A360 before Fighter_procUpdate input processing.
    // This simulator executes the downed Anim owner after input, so a current-frame A/B edge can
    // transiently zero x67C/x67D too early. Exclude only that same-frame edge; prior post-entry
    // timer values remain valid DownBound attack inputs.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
    const uint8_t x67c_same_frame_edge = (ab_pressed_now && batch->state.x67C[idx] == 0u) ? 1u : 0u;
    const uint8_t x67d_same_frame_edge = (ab_pressed_now && batch->state.x67D[idx] == 0u) ? 1u : 0u;
    const uint8_t x67c_post_entry =
        (!x67c_same_frame_edge && (af == 0u || (uint16_t)batch->state.x67C[idx] < af)) ? 1u : 0u;
    const uint8_t x67d_post_entry =
        (!x67d_same_frame_edge && (af == 0u || (uint16_t)batch->state.x67D[idx] < af)) ? 1u : 0u;
    if ((x67c_recent && x67c_post_entry) || (x67d_recent && x67d_post_entry)) {
      return 1u;
    }
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

static inline uint8_t is_damage_fly_action(uint16_t a);
static inline uint8_t is_damage_air_action(uint16_t a);
static inline uint8_t is_damage_ground_action(uint16_t a);
static inline uint32_t submotion_for_common_damage_action(uint16_t a);
static inline uint32_t submotion_for_damage_ground_action(uint16_t a);
static inline uint8_t knockdown_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32);
static inline void enter_damage_fall_from_damage_anim(MslBatch* batch, const MslCharParams* ch,
                                                      size_t idx);
static inline void enter_fall_from_downdamage_anim(MslBatch* batch, size_t idx);
static inline void enter_fall_from_damagefall_iasa(MslBatch* batch, size_t idx);
static inline void enter_down_stand_from_downdamage_anim(MslBatch* batch, size_t idx,
                                                         uint16_t down_damage_act);
static inline void clear_downed_damage_state(MslBatch* batch, size_t idx);
static inline uint8_t damage_ground_floor_loss_should_missfoot(const MslBatch* batch, size_t idx,
                                                               uint32_t stage_id);
static inline void enter_missfoot_from_damage_floor_loss(MslBatch* batch, const MslCharParams* ch,
                                                         size_t idx);
static inline uint8_t damagefall_iasa_try_stick_fall(MslBatch* batch, const MslCommonParams* c,
                                                     size_t idx);
static inline uint8_t is_damage_air_submotion(uint32_t smid);
static inline uint8_t is_damage_fly_submotion(uint32_t smid);
static inline uint8_t damage_iasa_lockout_x221c_b6(const MslBatch* batch, size_t idx);

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
      const uint8_t passivewall = is_passivewall_action(a0);
      const uint8_t down_damage = is_down_damage(a0);
      const uint8_t damage_fly = is_damage_fly_action(a0);
      const uint8_t damage_air = is_damage_air_action(a0);
      const uint8_t damage_ground = is_damage_ground_action(a0);
      const uint8_t common_damage_airborne =
          (uint8_t)((damage_air != 0u || damage_ground != 0u) && batch->state.on_ground[idx] == 0u);
      const uint8_t common_damage_grounded =
          (uint8_t)((damage_air != 0u || damage_ground != 0u) && batch->state.on_ground[idx] != 0u);
      if (!is_knockdown_any(a0) && !down_damage && !passivewall && !damage_fly && !damage_air &&
          !damage_ground) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      // Keep animation_index consistent with GALE01 submotion ids.
      batch->state.animation_index[idx] = submotion_for_down_action(a0);

      const uint8_t cid = batch->state.char_id[idx];
      const float anim_frame = batch->state.anim_frame_f32[idx];

      if (down_damage) {
        const uint32_t smid = submotion_for_down_action(a0);
        batch->state.animation_index[idx] = smid;
        if (smid <= 0xFFFFu && knockdown_anim_finished(cid, (uint16_t)smid, anim_frame)) {
          // Decomp: DownDamage_Anim decrements mv.co.downdamage.x0 while x2224_b2 is clear. On
          // animation end, airborne rows enter DamageFall/Fall, while grounded rows with the hidden
          // x0 timer expired enter DownStandU/D through ftCo_80098160.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
          if (!batch->state.on_ground[idx] && batch->state.dmg_x2224_b2[idx]) {
            enter_damage_fall_from_damage_anim(batch, ch, idx);
          } else if (!batch->state.on_ground[idx]) {
            enter_fall_from_downdamage_anim(batch, idx);
          } else if (!batch->state.dmg_x2224_b2[idx]) {
            if (batch->state.hitstun[idx] == 0u) {
              enter_down_stand_from_downdamage_anim(batch, idx, a0);
            } else {
              // Hidden timer continuation:
              // ftCo_DownDamage_Anim decrements mv.co.downdamage.x0 and, when the animation ends
              // while that timer is still positive, enters DownWaitU/D through ftCo_80097F38.
              // ftCo_80097F38 changes motion without reinitializing the motion-var union, so the
              // remaining downdamage.x0 countdown becomes the new downwait.x0 instead of the full
              // p_ftCommonData->x424 knockdown timer.
              // Fighter proc ordering then runs the newly-entered DownWait IASA in the same frame,
              // allowing buffered down-roll/getup options without waiting another replay row.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
              //   ftCo_80097F38,ftCo_DownWait_IASA}
              uint16_t remaining = batch->state.hitstun[idx];
              if (remaining == 0u) {
                remaining = 1u;
              }
              const int16_t downwait_x0 =
                  (remaining > (uint16_t)INT16_MAX) ? INT16_MAX : (int16_t)remaining;
              clear_downed_damage_state(batch, idx);
              enter_down_wait(batch, idx, down_wait_action_from_damage(a0));
              batch->state.downwait_timer[idx] = downwait_x0;
              if (should_enter_down_attack_from_wait(batch, c, idx)) {
                enter_down_attack(batch, idx, batch->state.action_id[idx]);
              } else {
                const uint16_t roll_act =
                    down_roll_action_from_input(batch, c, idx, batch->state.action_id[idx]);
                if (roll_act != 0u) {
                  enter_down_roll(batch, idx, roll_act);
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

      if (passivewall) {
        batch->state.animation_index[idx] =
            (uint32_t)((a0 == (uint16_t)MSL_ACT_PASSIVE_WALL) ? MSL_SM_PASSIVE_WALL
                                                              : MSL_SM_PASSIVE_WALL_JUMP);
        uint8_t timer = batch->state.passivewall_timer[idx];
        if (timer != 0u) {
          timer = (uint8_t)(timer - 1u);
          batch->state.passivewall_timer[idx] = timer;
          if (timer == 0u) {
            passivewall_launch_from_timer_expiry(batch, ch, idx);
            if (passivewall_iasa_try_air_options(batch, c, ch, idx)) {
              continue;
            }
          }
        } else if (passivewall_iasa_try_air_options(batch, c, ch, idx)) {
          continue;
        }
        continue;
      }

      if (damage_fly) {
        const uint32_t damage_msid_u32 = submotion_for_damage_action(a0);
        if (damage_msid_u32 <= 0xFFFFu) {
          batch->state.animation_index[idx] = damage_msid_u32;
        }

        const uint8_t in_hitstun = (batch->state.hitstun[idx] > 0) ? 1u : 0u;
        const uint8_t iasa_locked = damage_iasa_lockout_x221c_b6(batch, idx);
        uint8_t anim_done = 0u;
        if (damage_msid_u32 <= 0xFFFFu) {
          anim_done = anim_is_finished(cid, (uint16_t)damage_msid_u32, anim_frame);
        }

        // Decomp transition gates:
        // - DamageFly_Anim: enter DamageFall only when anim has ended and hitstun has ended.
        // - DamageFlyRoll_Anim: enter DamageFall immediately when hitstun has ended (no anim-end gate).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_DamageFly_Anim,ftCo_DamageFlyRoll_Anim
        // }
        // Callback ordering parity:
        // - This block is already under the non-hitlag path via `hitlag_started_frame` gate above,
        //   matching fighter update ordering for anim callbacks.
        const uint8_t fly_roll = (a0 == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) ? 1u : 0u;
        const uint8_t should_enter_damage_fall =
            fly_roll ? (uint8_t)(!in_hitstun && !iasa_locked)
                     : (uint8_t)(!in_hitstun && !iasa_locked && anim_done);
        if (in_hitstun && iasa_locked && damage_jump_input_from_edges(batch, c, idx)) {
          // DamageFly/DamageFlyRoll IASA uses the same x221C_b6 `doIasa` path as common Damage:
          // a qualifying jump input snapshots mv.co.damage.x0 into x14 before the later
          // post-hitstun DamageFly_Anim / DamageFall_IASA jump gate consumes it.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   doIasa,ftCo_DamageFly_IASA,ftCo_DamageFlyRoll_IASA}
          batch->state.damage_jump_buffer_x14[idx] = batch->state.hitstun[idx];
        }
        if (should_enter_damage_fall) {
          if (!fly_roll) {
            // Decomp IASA ordering (not collision/physics):
            // - ftCo_DamageFly_Anim calls inlineC0 first when
            //   !ftAnim_IsFramesRemaining && !x221C_b6.
            // - inlineC0 checks mv.co.damage.x14 against p_ftCommonData->x1D0 and can consume
            //   the jump-buffer path before DamageFall enter.
            // - Only when that x14 gate fails does the state enter DamageFall.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineC0
            const uint16_t x14 = batch->state.damage_jump_buffer_x14[idx];
            const uint8_t gate_open =
                (x14 != 0u && (float)x14 <= c->damage_jump_buffer_window_frames) ? 1u : 0u;
            if (gate_open && damage_air_try_jump_aerial(batch, c, ch, idx, 1u)) {
              continue;
            }
          }
          enter_damage_fall_from_damage_anim(batch, ch, idx);
        } else if (!in_hitstun && !iasa_locked) {
          // DamageFly IASA parity: when hitstun has ended, DamageFly_IASA delegates to
          // DamageFall_IASA even before DamageFly_Anim enters DamageFall. In that airborne IASA
          // chain, the shared direct AttackAir owner runs before the later JumpAerial fallback.
          //
          // Reuse that same shared airborne AttackAir IASA owner on post-lockout DamageFly rows,
          // then fall through to the existing JumpAerial subset. This is general DamageFly
          // callback ordering, not a row-specific bridge.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckItemThrowInput
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
          if (locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
            continue;
          }
          if (damage_air_try_jump_aerial(batch, c, ch, idx, 0u)) {
            continue;
          }
          if (damagefall_iasa_try_stick_fall(batch, c, idx)) {
            continue;
          }
        }
        continue;
      }

      if (common_damage_airborne) {
        // Decomp: ftCo_Damage_Anim / ftCo_Damage_IASA branch on fp->ground_or_air, not on whether
        // the current motion id is DamageAir* versus DamageHi/N/Lw*. Once a common damage state is
        // airborne, it follows the same Air callback ladder until landing or anim-end handoff.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_Anim,ftCo_Damage_IASA,ftCo_Damage_Coll
        // }
        const uint32_t damage_msid_u32 = submotion_for_common_damage_action(a0);
        if (damage_msid_u32 <= 0xFFFFu) {
          batch->state.animation_index[idx] = damage_msid_u32;
        }

        const uint8_t in_hitstun = (batch->state.hitstun[idx] > 0) ? 1u : 0u;
        const uint8_t iasa_locked = damage_iasa_lockout_x221c_b6(batch, idx);
        if (in_hitstun && damage_jump_input_from_edges(batch, c, idx)) {
          // Decomp: doIasa snapshots x0 into mv.co.damage.x14 when ftCo_Jump_GetInput succeeds.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
          batch->state.damage_jump_buffer_x14[idx] = batch->state.hitstun[idx];
        }

        if (!in_hitstun && !iasa_locked) {
          // Decomp airborne Damage_IASA:
          // - when x221C_b6 has cleared, Damage_IASA forwards into ftCo_Fall_IASA_Inner,
          // - if mv.co.damage.x14 is active and within p_ftCommonData->x1D0, it first ORs XY into
          //   the input lane, and
          // - ftCo_Fall_IASA_Inner can immediately enter JumpAerial via ftCo_800CB870.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
          const uint16_t x14 = batch->state.damage_jump_buffer_x14[idx];
          const uint8_t gate_open =
              (x14 != 0u && (float)x14 <= c->damage_jump_buffer_window_frames) ? 1u : 0u;
          if (gate_open && damage_air_try_jump_aerial(batch, c, ch, idx, 1u)) {
            continue;
          }
          // Fall_IASA_Inner checks aerial attacks before its later JumpAerial fallback. Keep B-edge
          // rows available for the SpecialAir dispatcher modeled by the dedicated spacie passes.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckItemThrowInput
          if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u &&
              locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
            continue;
          }
          // The injected x14 path is only the buffered-input half of Damage_IASA. Once x221C_b6 is
          // clear, the same Fall_IASA_Inner delegate can also consume a current-frame jump input.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
          if (damage_air_try_jump_aerial(batch, c, ch, idx, 0u)) {
            continue;
          }
        }

        uint8_t anim_done = 0u;
        if (damage_msid_u32 <= 0xFFFFu) {
          anim_done = anim_is_finished(cid, (uint16_t)damage_msid_u32, anim_frame);
        }
        if (anim_done && !in_hitstun && !iasa_locked) {
          const uint16_t x14 = batch->state.damage_jump_buffer_x14[idx];
          const uint8_t gate_open =
              (x14 != 0u && (float)x14 <= c->damage_jump_buffer_window_frames) ? 1u : 0u;
          // Decomp: Damage_Anim checks the inlineC0 jump-buffer gate first.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
          if (!(gate_open && damage_air_try_jump_aerial(batch, c, ch, idx, 1u))) {
            // `common_damage_airborne` is only entered while on_ground == 0, so the grounded
            // Damage_Anim -> Wait branch is unreachable here by construction.
            // Decomp airborne branch:
            // - ftCo_Damage_Anim enters Fall via ftCo_Fall_Enter when anim/hitstun gates clear.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
            //
            // ftCo_Fall_Enter does not call ftAnim_8006EBA4, so keep the previous DamageAir pose
            // for this frame's collision/hurtbox updates and commit only Fall animation/timebase
            // later. Fighter_ChangeMotionState identity side effects still run now, before other
            // fighters' later input callbacks and before ProcessHit can overwrite the action.
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
            msl_motion_state_enter_side_effects(batch, idx);
          }
        }
        continue;
      }

      if (common_damage_grounded) {
        // Grounded Damage callback parity subset.
        //
        // Decomp:
        // - ftCo_Damage_Anim enters Wait on anim end when !x221C_b6.
        // - ftCo_Damage_IASA delegates to Wait_IASA on ground when !x221C_b6.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Anim,ftCo_Damage_IASA}
        const uint32_t damage_msid_u32 = submotion_for_common_damage_action(a0);
        if (damage_msid_u32 <= 0xFFFFu) {
          batch->state.animation_index[idx] = damage_msid_u32;
        }
        const uint8_t iasa_locked = damage_iasa_lockout_x221c_b6(batch, idx);
        uint8_t anim_done = 0u;
        if (damage_msid_u32 <= 0xFFFFu) {
          anim_done = anim_is_finished(cid, (uint16_t)damage_msid_u32, anim_frame);
        }

        if (damage_ground != 0u && iasa_locked && batch->state.hitstun[idx] > 0u &&
            damage_jump_input_from_edges(batch, c, idx)) {
          // Decomp: while x221C_b6 keeps grounded DamageHi/N/Lw in hitstun lockout,
          // ftCo_Damage_IASA still calls doIasa; a qualifying jump input snapshots
          // mv.co.damage.x0 into mv.co.damage.x14 for the later post-hitstun Wait_IASA
          // injected-XY path. Keep landed DamageAir excluded until its floor-contact handoff
          // predicate is modeled; broadening this producer there regresses the modelplay
          // DamageAir floor-contact control.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_IASA}
          batch->state.damage_jump_buffer_x14[idx] = batch->state.hitstun[idx];
        }

        if (anim_done && !iasa_locked) {
          // Decomp proc order:
          // - ftCo_Damage_Anim runs before ftCo_Damage_IASA.
          // - On grounded anim end, ftCo_Damage_Anim enters Wait through ft_8008A2BC.
          // - The destination Wait_IASA may then enter Squat/other Wait-owned actions in the same
          //   fighter proc. That intermediate Wait motion-state entry owns its own ft_800895E0
          //   instance-id bump; direct Damage -> Squat would keep visible action parity while
          //   missing the hidden identity write.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
          // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          enter_wait(batch, idx);
        }

        if (!iasa_locked) {
          // Damage_IASA grounded path delegates to Wait_IASA when x221C_b6 is clear.
          // Keep the grounded subset in decomp order: grounded A-attacks first, then guard
          // ownership, then the later Wait IASA locomotion chain (jump, dash, squat, turn, walk).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          const uint16_t wait_iasa_owner_action = batch->state.action_id[idx];
          const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
          const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                               c->lstick_deadzone_x);
          const float stick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]),
                                               c->lstick_deadzone_y);
          const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
          const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
          const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(batch, c, idx, buttons_pressed,
                                                                    stick_x, stick_y, tilt_timer_x,
                                                                    tilt_timer_y, facing_dir)) {
            continue;
          }
          if (damage_ground_try_enter_guard_from_wait_iasa(batch, c, idx)) {
            continue;
          }
          guard_update_grounded(batch, c, idx, 1);
          if (batch->state.action_id[idx] != wait_iasa_owner_action) {
            continue;
          }
          if (damage_ground_try_enter_kneebend_from_wait_iasa(batch, c, idx)) {
            continue;
          }
          if (damage_ground_try_wait_iasa_locomotion_subset(batch, c, ch, idx)) {
            continue;
          }
        }

        continue;
      }

      if (is_passive(a0)) {
        // Decomp: ftCo_Passive_Phys uses ft_80084F3C.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Phys
        // Motion-state table binds Passive to ftCo_SM_Passive submotion.
        // refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_Passive entry)
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_PASSIVE;
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
        // Motion-state table binds PassiveStandF/B to ftCo_SM_PassiveStandF/B submotions.
        // refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_PassiveStandF / ftCo_MS_PassiveStandB)
        batch->state.animation_index[idx] = (uint32_t)msid;
        if (anim_is_finished(cid, msid, anim_frame)) {
          // Decomp: ftCo_PassiveStand_Anim ends through ft_8008A2BC, so the destination Wait state
          // still owns same-frame Wait_IASA/Phys ordering. This is required for held-down input to
          // enter Squat immediately on the tech-stand end frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Anim
          // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          enter_wait(batch, idx);
          passive_stand_anim_end_try_enter_squat(batch, c, idx);
          down_apply_phys_friction(batch, c, ch, idx);
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
          // Wait IASA includes pre-guard held-shield spotdodge (ftCo_80099794), guard entry
          // (ftCo_80091A4C), and squat entry (ftCo_800D5FB0).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          const uint8_t wait_spotdodge = wait_iasa_try_enter_spotdodge_before_guard(batch, c, idx);
          if (!wait_spotdodge) {
            guard_update_grounded(batch, c, idx, 1);
          }
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
            // DownBound_Anim runs before Fighter_procUpdate refreshes current-frame inputs. The
            // direct ftCo_Down_CheckInput call therefore sees the pre-input stick lane; if that
            // lane does not roll, ftCo_80097E8C enters DownWait and the newly entered DownWait IASA
            // can consume the current-frame stick later in this same simulator step.
            // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_CheckInput
            const uint16_t roll_act = down_roll_action_from_prev_input_for_bound(batch, c, idx, a0);
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

void knockdown_update_post_combat(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a0 = batch->state.action_id[idx];
      if (a0 != (uint16_t)MSL_ACT_FALL) {
        continue;
      }
      if (!is_damage_air_submotion(batch->state.animation_index[idx]) &&
          !is_damage_fly_submotion(batch->state.animation_index[idx])) {
        continue;
      }

      // Decomp ordering:
      // - Damage_Anim / DamageFly_IASA run before Fighter_ProcessHit_8006D1EC.
      // - A same-frame hit can overwrite the pending DamageAir/DamageFly->Fall result.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      if (batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
        continue;
      }

      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
      msl_anim_timebase_enter_raw(batch, idx, 0.0f, 1.0f);
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
    case (uint16_t)MSL_ACT_FLY_REFLECT_WALL:
    case (uint16_t)MSL_ACT_FLY_REFLECT_CEIL:
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

static inline uint8_t is_damage_ground_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DAMAGE_HI_1:
    case (uint16_t)MSL_ACT_DAMAGE_HI_2:
    case (uint16_t)MSL_ACT_DAMAGE_HI_3:
    case (uint16_t)MSL_ACT_DAMAGE_N_1:
    case (uint16_t)MSL_ACT_DAMAGE_N_2:
    case (uint16_t)MSL_ACT_DAMAGE_N_3:
    case (uint16_t)MSL_ACT_DAMAGE_LW_1:
    case (uint16_t)MSL_ACT_DAMAGE_LW_2:
    case (uint16_t)MSL_ACT_DAMAGE_LW_3:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint32_t submotion_for_common_damage_action(uint16_t a) {
  if (is_damage_air_action(a)) {
    return submotion_for_damage_action(a);
  }
  return submotion_for_damage_ground_action(a);
}

static inline uint32_t submotion_for_damage_ground_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_DAMAGE_HI_1:
      return (uint32_t)MSL_SM_DAMAGE_HI_1;
    case (uint16_t)MSL_ACT_DAMAGE_HI_2:
      return (uint32_t)MSL_SM_DAMAGE_HI_2;
    case (uint16_t)MSL_ACT_DAMAGE_HI_3:
      return (uint32_t)MSL_SM_DAMAGE_HI_3;
    case (uint16_t)MSL_ACT_DAMAGE_N_1:
      return (uint32_t)MSL_SM_DAMAGE_N_1;
    case (uint16_t)MSL_ACT_DAMAGE_N_2:
      return (uint32_t)MSL_SM_DAMAGE_N_2;
    case (uint16_t)MSL_ACT_DAMAGE_N_3:
      return (uint32_t)MSL_SM_DAMAGE_N_3;
    case (uint16_t)MSL_ACT_DAMAGE_LW_1:
      return (uint32_t)MSL_SM_DAMAGE_LW_1;
    case (uint16_t)MSL_ACT_DAMAGE_LW_2:
      return (uint32_t)MSL_SM_DAMAGE_LW_2;
    case (uint16_t)MSL_ACT_DAMAGE_LW_3:
      return (uint32_t)MSL_SM_DAMAGE_LW_3;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint8_t down_bound_airborne_ledge_cross_to_fall(const MslBatch* batch, size_t idx,
                                                              uint32_t stage_id) {
  float vx = batch->state.speed_x_attack[idx];
  if (!(vx > 0.0f || vx < 0.0f)) {
    // Decomp shape: ft_80082708 -> mpColl_8004B108 evaluates the active floor-contact motion
    // segment. In grounded DownBound rows where attack KB has already been consumed into
    // self/ground velocity ownership, use grounded horizontal velocity for the edge-cross test.
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    vx = batch->state.speed_ground_x_self[idx];
  }
  if (!(vx > 0.0f || vx < 0.0f)) {
    return 0u;
  }
  // Decomp shape:
  // - ftCo_DownBound_Coll uses ft_80082708 (allow-ground-to-air path).
  // - ft_80082708 delegates to mpColl_8004B108, which resolves ground->air by evaluating floor edge
  //   ownership against the motion segment on the active floor line.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  const int side = (vx > 0.0f) ? 1 : 0;
  const MslStageFloorLine* ledge = stage_collision_get_ledge_floor_line(stage_id, side);
  if (ledge == NULL) {
    return 0u;
  }

  const float edge_x = (side == 1) ? ((ledge->x0 > ledge->x1) ? ledge->x0 : ledge->x1)
                                   : ((ledge->x0 < ledge->x1) ? ledge->x0 : ledge->x1);
  // Use the current-step motion segment start (`prev_pos_x`) with callback-owned post-physics
  // horizontal velocity to match the mpColl allow-ground-to-air floor-edge test shape.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  const float next_x = batch->state.prev_pos_x[idx] + vx;
  // mpLib floor projection keeps a small endpoint clamp before reporting off-floor. DownBound uses
  // the allow-ground-to-air mpColl path, so preserve DownBound while the motion endpoint is still
  // inside that clamp instead of immediately entering Fall at the mathematical edge.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  enum { MSL_DOWNBOUND_FLOOR_ENDPOINT_CLAMP_MILLI = 100 };
  const float endpoint_clamp = (float)MSL_DOWNBOUND_FLOOR_ENDPOINT_CLAMP_MILLI * 0.001f;
  return (side == 1) ? (next_x > edge_x + endpoint_clamp ? 1u : 0u)
                     : (next_x < edge_x - endpoint_clamp ? 1u : 0u);
}

static inline uint8_t damage_iasa_lockout_x221c_b6(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Decomp: grounded Damage callback gates (Damage_Anim and Damage_IASA) branch on fp->x221C_b6.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Anim,ftCo_Damage_IASA}
  //
  return msl_state_flags_221c_b6_at(batch->state.state_flags, idx);
}

static inline uint8_t knockdown_anim_finished(uint8_t char_id, uint16_t msid,
                                              float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0u;
  }
  // Decomp gates on ftAnim_IsFramesRemaining from Anim callbacks.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end ? 1u : 0u;
}

static inline void enter_fall_from_downdamage_anim(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: ftCo_DownDamage_Anim calls ftCo_Fall_Enter when airborne, animation-ended, and
  // x2224_b2 is clear. This leaves the damage motion-var lane, so visible hitstun clears.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.hitstun[idx] = 0u;
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
}

static inline void clear_downed_damage_state(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.hitstun[idx] = 0u;
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  enum { MSL_STATE_FLAG_221C_IN_DAMAGE = 0x01 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_IS_HITSTUN | MSL_STATE_FLAG_221C_IN_DAMAGE);
}

static inline void enter_fall_from_damagefall_iasa(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // DamageFall_IASA's terminal X-stick gate enters Fall through ftCo_Fall_Enter. This is reached
  // directly from DamageFall and indirectly from DamageFly_IASA when x221C_b6 is clear.
  //
  // The DamageFly caller runs before same-frame collision. Match the existing DamageAir_Anim
  // ordering model by publishing the Fall action for collision, but deferring only Fall
  // pose/timebase until knockdown_update_post_combat if ProcessHit did not overwrite the state.
  // Fighter_ChangeMotionState side effects still happen here in source order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  msl_motion_state_enter_side_effects(batch, idx);
  // Decomp: this DamageFall_IASA branch calls ftCo_Fall_Enter, whose
  // Fighter_ChangeMotionState flags include Ft_MF_KeepFastFall.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  batch->state.fall_fast[idx] = keep_fastfall;
}

static inline uint8_t damagefall_iasa_try_stick_fall(MslBatch* batch, const MslCommonParams* c,
                                                     size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  uint8_t x670_for_iasa = batch->state.tilt_timer_x[idx];
  // The x670 timer is updated in Fighter_Spaghetti_8006AD10 before `input_cb`, so
  // ftCo_DamageFall_IASA observes the incremented current-frame timer. Do not rewind held
  // same-direction stick from 1 back to 0; p_ftCommonData->x214 uses a strict less-than gate.
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA

  if (msl_absf(stick_x) >= c->damagefall_fall_stick_x_threshold &&
      x670_for_iasa < c->damagefall_fall_tilt_max_frames) {
    enter_fall_from_damagefall_iasa(batch, idx);
    return 1u;
  }
  return 0u;
}

static inline void enter_down_stand_from_downdamage_anim(MslBatch* batch, size_t idx,
                                                         uint16_t down_damage_act) {
  if (batch == NULL) {
    return;
  }
  const uint16_t stand_act = (down_damage_act == (uint16_t)MSL_ACT_DOWN_DAMAGE_U)
                                 ? (uint16_t)MSL_ACT_DOWN_STAND_U
                                 : (uint16_t)MSL_ACT_DOWN_STAND_D;
  // Grounded DownDamage_Anim exit:
  // - ftCo_DownDamage_Anim routes to ftCo_80098160(DownStandU/D) when the hidden
  //   mv.co.downdamage.x0 timer has expired.
  // - This runtime does not carry x0 separately yet; the replay-visible expired subset has no
  //   remaining hitstun after Fighter_8006A360/timer ownership, so keep the handoff scoped to
  //   hitstun==0 rather than guessing on active damage-timer rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_80098160
  batch->state.action_id[idx] = stand_act;
  batch->state.animation_index[idx] = submotion_for_down_action(stand_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  clear_downed_damage_state(batch, idx);
  // DownStand entry starts in the downed getup hit-status window; Slippi reports the merged
  // x1988/x198C value as hurtbox_state=2 on replay-real entry frames.
  // data/hurtbox_states/{fox,falco}.bin
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_80098160
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071A14
  batch->state.hurtbox_state[idx] = 2u;
}

static inline uint8_t damage_ground_floor_loss_should_missfoot(const MslBatch* batch, size_t idx,
                                                               uint32_t stage_id) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return 0u;
  }
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  const float left = (line->x0 < line->x1) ? line->x0 : line->x1;
  const float right = (line->x0 > line->x1) ? line->x0 : line->x1;
  const float x = batch->state.pos_x[idx];
  const uint8_t facing_right = batch->state.facing[idx] ? 1u : 0u;
  // Decomp: grounded Damage_Coll calls ft_800848DC. When mpColl_8004B108 reports floor loss past
  // an open endpoint, it sets Left/RightLedgeSlip; ft_800848DC enters MissFoot only for the
  // facing/side pair shown below, otherwise it calls the supplied air-transfer callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_800848DC
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  if (x < left && facing_right) {
    return 1u;
  }
  if (x > right && !facing_right) {
    return 1u;
  }
  return 0u;
}

static inline void enter_missfoot_from_damage_floor_loss(MslBatch* batch, const MslCharParams* ch,
                                                         size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // MissFoot entry from grounded Damage floor loss:
  // - ft_800848DC routes ledge-slip floor loss to ftCo_8009F39C.
  // - ftCo_8009F39C zeros vertical KB, changes to MissFoot, clamps air drift, and leaves the
  //   fighter airborne through ftCommon_8007D5D4 when needed.
  // refs/melee/src/melee/ft/ft_081B.c::ft_800848DC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_MISS_FOOT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_MISS_FOOT;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.on_ground[idx] = 0u;
  batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0u;
  batch->state.speed_y_attack[idx] = 0.0f;
  if (batch->state.speed_air_x_self[idx] > ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = ch->air_drift_max;
  } else if (batch->state.speed_air_x_self[idx] < -ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = -ch->air_drift_max;
  }
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.hitstun[idx] = 0u;
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  enum { MSL_STATE_FLAG_221C_IN_DAMAGE = 0x01 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_IS_HITSTUN | MSL_STATE_FLAG_221C_IN_DAMAGE);
}

static inline uint8_t is_damage_air_submotion(uint32_t smid) {
  return (smid == (uint32_t)MSL_SM_DAMAGE_AIR_1 || smid == (uint32_t)MSL_SM_DAMAGE_AIR_2 ||
          smid == (uint32_t)MSL_SM_DAMAGE_AIR_3)
             ? 1u
             : 0u;
}

static inline uint8_t is_damage_fly_submotion(uint32_t smid) {
  return (smid == (uint32_t)MSL_SM_DAMAGE_FLY_HI || smid == (uint32_t)MSL_SM_DAMAGE_FLY_N ||
          smid == (uint32_t)MSL_SM_DAMAGE_FLY_LW || smid == (uint32_t)MSL_SM_DAMAGE_FLY_TOP ||
          smid == (uint32_t)MSL_SM_DAMAGE_FLY_ROLL || smid == (uint32_t)MSL_SM_WALL_DAMAGE ||
          smid == (uint32_t)MSL_SM_STOP_CEIL)
             ? 1u
             : 0u;
}

static inline void enter_damage_fall_from_damage_anim(MslBatch* batch, const MslCharParams* ch,
                                                      size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }

  // Decomp: ftCo_80090780 handles both air/ground callers, forcing GA_Air first when needed.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_80090780
  if (batch->state.on_ground[idx]) {
    batch->state.on_ground[idx] = 0;
    batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;
  }

  // Decomp: ftCo_80090780 calls ftCommon_ClampAirDrift after entering DamageFall.
  // Bound source: character air max horizontal velocity from extracted attrs (`air_max_horizontal_velocity`).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_80090780
  if (batch->state.speed_air_x_self[idx] > ch->air_max_horizontal_velocity) {
    batch->state.speed_air_x_self[idx] = ch->air_max_horizontal_velocity;
  } else if (batch->state.speed_air_x_self[idx] < -ch->air_max_horizontal_velocity) {
    batch->state.speed_air_x_self[idx] = -ch->air_max_horizontal_velocity;
  }

  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DAMAGE_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DAMAGE_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftCo_80090780 enters DamageFall with flags 0x18001, including
  // Ft_MF_KeepFastFall, before the same frame can run DamageFall_IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_80090780
  // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside Fighter_ChangeMotionState)
  batch->state.fall_fast[idx] = keep_fastfall;
}

static inline void transfer_air_to_ground_on_land(MslBatch* batch, const MslCharParams* ch,
                                                  size_t bi, size_t idx, uint16_t prev_action_id) {
  // Shared with locomotion landing behavior: keep self_vel.x and gr_vel aligned on ground entry.
  // Decomp:
  // - ftCommon_8007D6A4 sets fp->gr_vel = fp->self_vel.x and does not zero self_vel.x.
  // - Fighter_procUpdate keeps fp->self_vel.x synchronized from fp->gr_vel while grounded.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4 (jumps refresh on grounding)
  // DamageFly collision callbacks run after ft_80081DD4 floor resolution, so entering
  // DownBound/Passive from ftCo_80090184 owns a floor-contact root position this frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  if (batch->state.on_ground[idx] && damage_landing_action_owns_root_floor_snap(prev_action_id)) {
    snap_root_y_to_ground_line_on_damage_land(batch, bi, idx);
  }
  batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
  batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
  batch->state.fall_fast[idx] = 0;
  batch->state.jumps_left[idx] = ch->max_jumps;
}

static inline void damage_land_project_kb_to_ground_tangent(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: DownBound/Passive floor-contact entries call ftCommon_8007CCE8 after switching to the
  // grounded motion state. That helper transfers x8c_kb_vel.x into the grounded KB scalar and rebuilds
  // x8c_kb_vel from the current floor tangent, which clears vertical KB on flat FD floors.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_8009794C,ftCo_80097AF4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Enter
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CCE8
  const float ground_kb = batch->state.speed_x_attack[idx];
  const float nx = batch->state.ground_normal_x[idx];
  const float ny =
      (batch->state.ground_normal_y[idx] != 0.0f) ? batch->state.ground_normal_y[idx] : 1.0f;
  batch->state.speed_x_attack[idx] = ny * ground_kb;
  batch->state.speed_y_attack[idx] = -nx * ground_kb;
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

static inline uint8_t passivewall_prefers_jump(const MslBatch* batch, const MslCommonParams* c,
                                               size_t idx) {
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  // Decomp: ftCo_800C1E0C upgrades PassiveWall entry to PassiveWallJump when either:
  // - fp->x67E < p_ftCommonData->x250, or
  // - fp->input.lstick.y >= p_ftCommonData->tap_jump_threshold.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E0C
  return ((float)batch->state.x67E[idx] < c->tech_window_frames || stick_y >= c->tap_jump_threshold)
             ? 1u
             : 0u;
}

static inline uint8_t is_passivewall_action(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_PASSIVE_WALL || a == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP) ? 1u
                                                                                           : 0u;
}

static inline uint8_t passivewall_entry_wall_contact_x(const MslBatch* batch, size_t idx,
                                                       float* out_x) {
  if (batch == NULL) {
    return 0u;
  }
  if (out_x == NULL) {
    return 0u;
  }
  const uint8_t cid = batch->state.char_id[idx];
  const uint32_t cur_anim = batch->state.animation_index[idx];
  if (cur_anim > 0xFFFFu) {
    return 0u;
  }

  const uint16_t ecb_frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  // ftCo_800C1E64 snapshots coll->ecb.left/right before Fighter_ChangeMotionState enters
  // PassiveWall{Jump}; the later ft_80081F2C call owns the target-state collision correction.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
  const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
  MslEcbWorldPoints ecb = {0};
  msl_ecb_world_points_sample(&ecb, cid, (uint16_t)cur_anim, ecb_frame, fd, batch->state.pos_x[idx],
                              batch->state.pos_y[idx], 0u);

  const uint32_t env = batch->state.coll_env_flags[idx];
  if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
    *out_x = ecb.left_x;
    return 1u;
  }
  if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) {
    *out_x = ecb.right_x;
    return 1u;
  }
  return 0u;
}

static inline uint8_t passivewall_entry_endpoint_in_vertical_span(float root_y, float top_rel_y,
                                                                  float endpoint_y) {
  const float y0 = root_y;
  const float y1 = root_y + top_rel_y;
  const float lo = (y0 < y1) ? y0 : y1;
  const float hi = (y0 < y1) ? y1 : y0;
  return (endpoint_y >= lo && endpoint_y <= hi) ? 1u : 0u;
}

static inline void passivewall_apply_entry_wall_collision_clamp(MslBatch* batch, size_t idx,
                                                                uint16_t target_msid) {
  if (batch == NULL) {
    return;
  }

  const uint32_t env = batch->state.coll_env_flags[idx];
  const uint16_t wall_id = batch->state.wall_id[idx];
  if (wall_id == 0xFFFFu) {
    return;
  }

  // ftCo_800C1E64 calls ft_80081F2C immediately after the PassiveWall motion change.
  // ft_80081F2C -> mpColl_80048464 -> mpColl_LoadECB_inline(..., 0xA), and bit 0b1000 in that
  // load path forces the active collision ECB's horizontal side points to +/-1 before the wall
  // projection chooses endpoint/vertex candidates.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081F2C
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048464,mpColl_LoadECB_JObj,mpColl_80046224_LeftWall}
  const MslEcbExtentsRel target_ext =
      msl_ecb_extents_rel(batch->state.char_id[idx], target_msid, 0);
  const float top_rel_y = target_ext.max_y;
  const float side_x_0xa = 1.0f;
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];

  if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) {
    const MslStageWallGraph* g = stage_collision_get_left_wall_graph(stage_id);
    const int li = stage_collision_left_wall_line_index(stage_id, wall_id);
    if (g == NULL || li < 0 || (size_t)li >= g->line_count) {
      return;
    }
    const MslStageWallLine* line = &g->lines[(size_t)li];
    float best_x = batch->state.pos_x[idx];
    if (passivewall_entry_endpoint_in_vertical_span(batch->state.pos_y[idx], top_rel_y, line->y0)) {
      const float candidate = line->x0 - side_x_0xa;
      if (candidate < best_x) {
        best_x = candidate;
      }
    }
    if (passivewall_entry_endpoint_in_vertical_span(batch->state.pos_y[idx], top_rel_y, line->y1)) {
      const float candidate = line->x1 - side_x_0xa;
      if (candidate < best_x) {
        best_x = candidate;
      }
    }
    batch->state.pos_x[idx] = best_x;
  } else if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
    const MslStageWallGraph* g = stage_collision_get_right_wall_graph(stage_id);
    const int li = stage_collision_right_wall_line_index(stage_id, wall_id);
    if (g == NULL || li < 0 || (size_t)li >= g->line_count) {
      return;
    }
    const MslStageWallLine* line = &g->lines[(size_t)li];
    float best_x = batch->state.pos_x[idx];
    if (passivewall_entry_endpoint_in_vertical_span(batch->state.pos_y[idx], top_rel_y, line->y0)) {
      const float candidate = line->x0 + side_x_0xa;
      if (candidate > best_x) {
        best_x = candidate;
      }
    }
    if (passivewall_entry_endpoint_in_vertical_span(batch->state.pos_y[idx], top_rel_y, line->y1)) {
      const float candidate = line->x1 + side_x_0xa;
      if (candidate > best_x) {
        best_x = candidate;
      }
    }
    batch->state.pos_x[idx] = best_x;
  }
}

static inline void passivewall_align_entry_x(MslBatch* batch, size_t idx, uint16_t target_msid,
                                             float wall_contact_x) {
  if (batch == NULL) {
    return;
  }
  const uint8_t cid = batch->state.char_id[idx];
  float transn[3] = {0.0f, 0.0f, 0.0f};
  if (anim_pose_get_transn(cid, target_msid, 0u, transn) != 0) {
    transn[2] = 0.0f;
  }

  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  batch->state.pos_x[idx] = wall_contact_x + transn[2] * facing_dir;
}

static inline void passivewall_launch_from_timer_expiry(MslBatch* batch, const MslCharParams* ch,
                                                        size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  const uint16_t a = batch->state.action_id[idx];
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  if (a == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP) {
    float vx = ch->wall_jump_horizontal_velocity;
    float vy = ch->wall_jump_vertical_velocity;
    // Decomp owner:
    // - ftCo_PassiveWall_Anim writes launch from
    //   fp->co_attrs.{wall_jump_horizontal_velocity,wall_jump_vertical_velocity}.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Anim
    // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x104/+0x108, fighter fp+0x214/+0x218)
    batch->state.speed_air_x_self[idx] = facing_dir * vx;
    batch->state.speed_y_self[idx] = vy;
  } else if (a == (uint16_t)MSL_ACT_PASSIVE_WALL) {
    batch->state.speed_air_x_self[idx] = facing_dir * ch->passivewall_vel_x;
  }
}

static inline uint8_t passivewall_iasa_try_air_options(MslBatch* batch, const MslCommonParams* c,
                                                       const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  if (batch->state.passivewall_timer[idx] != 0u) {
    return 0u;
  }
  // Decomp: after mv.co.passivewall.timer reaches zero, PassiveWall_IASA runs the common aerial
  // option ladder. Fox/Falco B-specials are owned by the dedicated Shine/Blaster passes, so keep
  // B-edge rows in PassiveWall here; the retained subset covers the later AttackAir and
  // JumpAerial entries in source order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckItemThrowInput
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u &&
      locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
    return 1u;
  }
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u &&
      damage_air_try_jump_aerial(batch, c, ch, idx, 0u)) {
    return 1u;
  }
  return 0u;
}

static inline void enter_passive_wall_from_damage_air(MslBatch* batch, size_t idx,
                                                      uint16_t prev_action_id,
                                                      uint16_t wall_action_id) {
  const MslCommonParams* c = msl_common_params();
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint32_t env = batch->state.coll_env_flags[idx];
  // DamageFly wall-tech ownership:
  // - ftCo_DamageFly_Coll calls ftCo_800C1D38 before ceiling tech / floor tech ladders.
  // - ftCo_800C1D38 picks PassiveWallJump when ftCo_800C1E0C is true, otherwise PassiveWall, then
  //   ftCo_800C1E64:
  //   * flips facing based on wall side,
  //   * enters ftCo_MS_PassiveWall{Jump} at frame 0,
  //   * clears damage hitstun ownership,
  //   * calls ftColl_8007B760(..., p_ftCommonData->x764) so x198C-visible hurt status is 2.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
  //   ftCo_800C1D38,ftCo_800C1E0C,ftCo_800C1E64}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
  // data/common/ft_common_data.json: colanim_passivewall_x1990_frames
  (void)prev_action_id;
  float wall_contact_x = batch->state.pos_x[idx];
  const uint8_t have_wall_contact_x = passivewall_entry_wall_contact_x(batch, idx, &wall_contact_x);
  const uint16_t target_action = (wall_action_id == (uint16_t)MSL_ACT_PASSIVE_WALL)
                                     ? (uint16_t)MSL_ACT_PASSIVE_WALL
                                     : (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP;
  batch->state.action_id[idx] = target_action;
  batch->state.animation_index[idx] = (target_action == (uint16_t)MSL_ACT_PASSIVE_WALL)
                                          ? (uint32_t)MSL_SM_PASSIVE_WALL
                                          : (uint32_t)MSL_SM_PASSIVE_WALL_JUMP;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.hitstun[idx] = 0u;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.passivewall_timer[idx] = (uint8_t)c->passivewall_timer_frames;
  batch->state.tilt_timer_x[idx] = 0xFEu;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.colanim_timer_x1990[idx] = c->colanim_passivewall_x1990_frames;
  batch->state.colanim_hit_status_x198c[idx] = 2u;
  batch->state.hurtbox_state[idx] = 2u;
  if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
    batch->state.facing[idx] = 1u;
  } else if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) {
    batch->state.facing[idx] = 0u;
  }
  if (have_wall_contact_x) {
    passivewall_align_entry_x(batch, idx, (uint16_t)batch->state.animation_index[idx],
                              wall_contact_x);
    passivewall_apply_entry_wall_collision_clamp(batch, idx,
                                                 (uint16_t)batch->state.animation_index[idx]);
  }
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
}

static inline uint8_t damagefly_reflect_lockout_active(const MslBatch* batch,
                                                       const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_FLY_REFLECT_WALL && a != (uint16_t)MSL_ACT_FLY_REFLECT_CEIL) {
    return 0u;
  }
  // Decomp: ftCo_800C18A8 seeds mv.co.damage.x18 from p_ftCommonData->x1C0, and
  // ftCo_FlyReflect_Anim decrements it once per frame before FlyReflect_Coll admits another
  // wall-reflect branch. This core does not persist x18 separately yet; the action-frame age is
  // the source-shaped local representation for the active FlyReflect lockout window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{
  //   ftCo_800C18A8,ftCo_FlyReflect_Anim,ftCo_FlyReflect_Coll}
  return (uint8_t)(batch->state.action_frame[idx] < (int16_t)c->damagefly_reflect_lockout_frames);
}

static inline uint8_t damagefly_try_enter_flyreflect(MslBatch* batch, const MslCommonParams* c,
                                                     size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (!is_damage_fly_action(a) || damagefly_reflect_lockout_active(batch, c, idx)) {
    return 0u;
  }

  const uint32_t env = batch->state.coll_env_flags[idx];
  const float threshold = c->damagefly_reflect_speed_threshold;
  uint16_t target_action = 0u;
  uint32_t target_submotion = 0u;

  if (batch->state.speed_x_attack[idx] < -threshold &&
      (env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
    target_action = (uint16_t)MSL_ACT_FLY_REFLECT_WALL;
    target_submotion = (uint32_t)MSL_SM_WALL_DAMAGE;
  } else if (batch->state.speed_x_attack[idx] > threshold &&
             (env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) {
    target_action = (uint16_t)MSL_ACT_FLY_REFLECT_WALL;
    target_submotion = (uint32_t)MSL_SM_WALL_DAMAGE;
  } else if (batch->state.speed_y_attack[idx] > threshold &&
             (env & (uint32_t)MSL_COLLIDE_CEILING_HUG) != 0u) {
    target_action = (uint16_t)MSL_ACT_FLY_REFLECT_CEIL;
    target_submotion = (uint32_t)MSL_SM_STOP_CEIL;
  } else {
    return 0u;
  }

  float nx = (target_action == (uint16_t)MSL_ACT_FLY_REFLECT_CEIL)
                 ? batch->state.ceiling_normal_x[idx]
                 : batch->state.wall_normal_x[idx];
  float ny = (target_action == (uint16_t)MSL_ACT_FLY_REFLECT_CEIL)
                 ? batch->state.ceiling_normal_y[idx]
                 : batch->state.wall_normal_y[idx];
  const float normal_mag = sqrtf(nx * nx + ny * ny);
  if (!(normal_mag > 0.0f)) {
    if (target_action == (uint16_t)MSL_ACT_FLY_REFLECT_CEIL) {
      nx = 0.0f;
      ny = -1.0f;
    } else if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
      nx = 1.0f;
      ny = 0.0f;
    } else {
      nx = -1.0f;
      ny = 0.0f;
    }
  } else {
    nx /= normal_mag;
    ny /= normal_mag;
  }

  // Decomp no-tech reflect owner:
  // - ftCo_DamageFly_Coll runs wall tech, ceiling tech, then ftCo_800C17CC.
  // - ftCo_800C17CC gates on KB velocity and the CollData Hug bit.
  // - ftCo_800C18A8 mirrors self_vel + kb_vel across the collision normal, scales by
  //   p_ftCommonData->x1BC, clears self velocity, enters FlyReflectWall/Ceil, and starts the
  //   x1990 colanim hit-status window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{
  //   ftCo_800C15F4,ftCo_800C1718,ftCo_800C17CC,ftCo_800C18A8}
  // data/common/ft_common_data.json::{
  //   damagefly_reflect_speed_threshold,damagefly_reflect_speed_mul,
  //   damagefly_reflect_lockout_frames,colanim_flyreflect_x1990_frames}
  float vx = batch->state.speed_air_x_self[idx] + batch->state.speed_x_attack[idx];
  float vy = batch->state.speed_y_self[idx] + batch->state.speed_y_attack[idx];
  const float dot = vx * nx + vy * ny;
  vx = (vx - (2.0f * dot * nx)) * c->damagefly_reflect_speed_mul;
  vy = (vy - (2.0f * dot * ny)) * c->damagefly_reflect_speed_mul;

  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = vx;
  batch->state.speed_y_attack[idx] = vy;
  batch->state.facing[idx] = (vx < 0.0f) ? 0u : 1u;
  batch->state.action_id[idx] = target_action;
  batch->state.animation_index[idx] = target_submotion;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.colanim_timer_x1990[idx] = c->colanim_flyreflect_x1990_frames;
  batch->state.colanim_hit_status_x198c[idx] = 2u;
  batch->state.hurtbox_state[idx] = 2u;
  return 1u;
}

static inline void enter_passive_from_damage_land(MslBatch* batch, const MslCharParams* ch,
                                                  size_t bi, size_t idx, uint16_t passive_act,
                                                  uint16_t prev_action_id) {
  transfer_air_to_ground_on_land(batch, ch, bi, idx, prev_action_id);
  damage_land_project_kb_to_ground_tangent(batch, idx);
  batch->state.action_id[idx] = passive_act;
  batch->state.animation_index[idx] = submotion_for_down_action(passive_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp ownership: hitstun lives in the Damage motion-var lane (x2340). Entering Passive from
  // ftCo_80090184 switches to a non-Damage motion state, so Slippi post hitstun becomes 0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009872C
  batch->state.hitstun[idx] = 0u;
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  // Passive/PassiveStand entry in ftCo_80090184 uses Fighter_ChangeMotionState via
  // ftCo_80098928 / ftCo_8009872C and does not do a local immediate ftAnim_8006EBA4 tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009872C
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

  // Decomp: ftCo_80097570 uses the live Hip joint matrix and returns true when a chosen element
  // is > 0. DamageFlyRoll is not a plain SSANIM pose: its Anim/Phys callbacks call doFlyRoll,
  // which writes a runtime XRotN rotation from the current self+KB velocity before collision
  // followup can enter DownBound. Apply the same parent X-axis rotation to the sampled Hip matrix
  // for this selector; otherwise static DamageFlyRoll matrices choose DownBoundU for rows where
  // the engine's live XRotN transform chooses DownBoundD.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
  //   ftCo_80097570,ftCo_8009794C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   doFlyRoll,ftCo_DamageFlyRoll_Anim,ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll}
  float f = m[5];  // m11 (row 1, col 1) when using the "x2226_b0 == 0" axis selection.
  if (cur_action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
    const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    const float vel_x = batch->state.speed_air_x_self[idx] + batch->state.speed_x_attack[idx];
    const float vel_y = batch->state.speed_y_self[idx] + batch->state.speed_y_attack[idx];
    const float trajectory = facing_dir * atan2f(vel_x, vel_y);
    const float c = cosf(trajectory);
    const float s = sinf(trajectory);
    f = c * m[5] - s * m[9];
  }
  return (f > 0.0f) ? (uint16_t)MSL_ACT_DOWN_BOUND_U : (uint16_t)MSL_ACT_DOWN_BOUND_D;
}

static inline void enter_down_bound_from_damage_land(MslBatch* batch, const MslCharParams* ch,
                                                     size_t bi, size_t idx,
                                                     uint16_t prev_action_id) {
  const uint16_t bound_act = pick_downbound_action_from_pose(batch, idx, prev_action_id);
  transfer_air_to_ground_on_land(batch, ch, bi, idx, prev_action_id);
  damage_land_project_kb_to_ground_tangent(batch, idx);
  batch->state.action_id[idx] = bound_act;
  batch->state.animation_index[idx] = submotion_for_down_action(bound_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp ownership: ftCo_80097D40 enters DownBound from ftCo_80090184 and writes to the
  // non-Damage motion-var lane; Damage hitstun (x2340) is no longer the active state var.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
  batch->state.hitstun[idx] = 0u;
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;

  // Decomp: DownBound entry clears A/B press timers so buffered presses from before landing don't
  // trigger the getup attack window.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C
  batch->state.x67C[idx] = 0xFFu;
  batch->state.x67D[idx] = 0xFFu;
}

static inline void enter_damagefly_ground_contact_followup(MslBatch* batch,
                                                           const MslCommonParams* c,
                                                           const MslCharParams* ch, size_t bi,
                                                           size_t idx, uint16_t prev_action_id) {
  // Shared DamageFly/DamageFall floor-contact owner:
  // - DamageFly_Coll calls ftCo_80090184 after ft_80081DD4 reports floor contact.
  // - DamageFall_Coll routes through ftCo_80090984, which uses the same callback ladder.
  // - The ladder order is PassiveStandF/B, then Passive, then DownBound.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{ftCo_DamageFall_Coll,ftCo_80090984}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009872C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
  if (tech_is_available(batch, c, idx)) {
    const float stick_x =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
    if (msl_absf(stick_x) >= c->tech_roll_stick_threshold) {
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      const uint16_t act = (stick_x * facing_dir) >= 0.0f ? (uint16_t)MSL_ACT_PASSIVE_STAND_F
                                                          : (uint16_t)MSL_ACT_PASSIVE_STAND_B;
      enter_passive_from_damage_land(batch, ch, bi, idx, act, prev_action_id);
      return;
    }
    enter_passive_from_damage_land(batch, ch, bi, idx, (uint16_t)MSL_ACT_PASSIVE, prev_action_id);
    return;
  }
  enter_down_bound_from_damage_land(batch, ch, bi, idx, prev_action_id);
}

void knockdown_try_throw_release_damage_floor_contact(MslBatch* batch, size_t bi, size_t idx,
                                                      uint16_t prev_action_id) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (c == NULL || ch == NULL) {
    return;
  }
  if (!is_damage_fly_action(batch->state.action_id[idx])) {
    return;
  }

  MslMpcollFloorMaskResult floor_result = {0};
  if (!mpcoll_800477e0_floor_mask_probe(batch, idx, &floor_result)) {
    return;
  }

  // Decomp release-frame owner:
  // - ftCo_800DDDE4 places the detached fighter, calls mpColl_800471F8, then DamageFly_Coll can
  //   enter ftCo_80090184 on the same fighter callback frame.
  // - This sim defers the throw hit until after the normal stage-collision pass, so run only the
  //   release-local floor probe and existing DamageFly floor-contact ladder here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
  batch->state.on_ground[idx] = 1u;
  batch->state.ground_id[idx] = floor_result.ground_id;
  batch->state.pos_y[idx] = floor_result.corrected_pos_y;
  batch->state.ground_normal_x[idx] = 0.0f;
  batch->state.ground_normal_y[idx] = 1.0f;
  enter_damagefly_ground_contact_followup(batch, c, ch, bi, idx, prev_action_id);
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
    const uint32_t stage_id = batch->state.stage_id[bi];
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a0 = batch->state.action_id[idx];
      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1u : 0u;

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      if (!was_ground && !now_ground && is_damage_fly_action(a0) &&
          tech_is_available(batch, c, idx) &&
          (batch->state.coll_env_flags[idx] &
           ((uint32_t)MSL_COLLIDE_LEFT_WALL_HUG | (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG)) != 0u) {
        const uint16_t wall_action = passivewall_prefers_jump(batch, c, idx)
                                         ? (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP
                                         : (uint16_t)MSL_ACT_PASSIVE_WALL;
        enter_passive_wall_from_damage_air(batch, idx, a0, wall_action);
        continue;
      }

      if (!was_ground && !now_ground && is_damage_fly_action(a0) &&
          damagefly_try_enter_flyreflect(batch, c, idx)) {
        continue;
      }

      if (!was_ground && now_ground) {
        // Landing transitions into DownBound from tumble-style damage states.
        //
        // Decomp entry points:
        // - Damage: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
        // - DamageFly: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
        if (is_damage_fly_action(a0)) {
          enter_damagefly_ground_contact_followup(batch, c, ch, (size_t)bi, idx, a0);
          continue;
        }

        if (a0 == (uint16_t)MSL_ACT_DAMAGE_FALL) {
          enter_damagefly_ground_contact_followup(batch, c, ch, (size_t)bi, idx, a0);
          continue;
        }

        if (is_damage_ground_action(a0)) {
          const float kbx = batch->state.speed_x_attack[idx];
          const float kby = batch->state.speed_y_attack[idx];
          const float mag = sqrtf(kbx * kbx + kby * kby);
          // Decomp: ftCo_Damage_Coll (non-fly damage lanes) uses ft_80081DD4 floor contact and then:
          // - if fp->x2224_b2 or |kb| >= x1E0: enter DownBound via ftCo_80097D40,
          // - else if |kb| >= x1E4: enter Landing via ftCo_Landing_Enter_Basic,
          // - else: keep Damage state and run ftCommon_8007D7FC transfer helper.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
          if (batch->state.dmg_x2224_b2[idx] || mag >= c->damagefly_downbound_kb_vel_threshold) {
            enter_down_bound_from_damage_land(batch, ch, (size_t)bi, idx, a0);
            continue;
          }
          if (mag >= c->damagefly_landing_kb_vel_threshold) {
            transfer_air_to_ground_on_land(batch, ch, (size_t)bi, idx, a0);
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.hitstun[idx] = 0u;
            continue;
          }
          if (batch->state.hitstun[idx] > 0u) {
            transfer_air_to_ground_on_land(batch, ch, (size_t)bi, idx, a0);
          }
          continue;
        }

        if (is_damage_air_action(a0)) {
          const float kbx = batch->state.speed_x_attack[idx];
          const float kby = batch->state.speed_y_attack[idx];
          const float mag = sqrtf(kbx * kbx + kby * kby);
          if (mag >= c->damagefly_downbound_kb_vel_threshold) {
            enter_down_bound_from_damage_land(batch, ch, (size_t)bi, idx, a0);
            continue;
          }
          if (mag >= c->damagefly_landing_kb_vel_threshold) {
            // Decomp: ftCo_Landing_Enter_Basic.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
            transfer_air_to_ground_on_land(batch, ch, (size_t)bi, idx, a0);
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            // DamageAir -> Landing handoff ownership:
            // - Damage_Coll enters ftCo_Landing_Enter_Basic on this low-KB aerial landing path.
            // - Landing_Enter uses Fighter_ChangeMotionState(..., Ft_MF_None), so the destination
            //   no longer owns the Damage hitstun lane.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_Anim,ftCo_Damage_IASA,ftCo_Damage_Coll,ftCo_Landing_Enter_Basic
            // }
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            // Replay lock: MotionlessAggressiveJay rec=385 keeps the DamageAir1 IASA lockout flag
            // live on seed but Landing_Enter_Basic has already cleared the replay-facing hitstun
            // lane on the destination row.
            batch->state.hitstun[idx] = 0u;
            continue;
          }
          // Decomp: Damage_Coll fallback while grounded keeps Damage motion-state and applies the
          // common air->ground transfer helper. Keep this restricted to hitstun-active continuation
          // rows; grounded DamageAir fastfall visibility is owned separately by the visible
          // state-flags / fastfall lane.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
          if (batch->state.hitstun[idx] > 0u) {
            transfer_air_to_ground_on_land(batch, ch, (size_t)bi, idx, a0);
          } else {
            // Low-KB DamageAir floor contact can keep the visible DamageAir motion while replay
            // already exposes the jump refresh and gr_vel <- self_vel.x handoff from the same
            // grounding helper. Preserve the visible fastfall/state-flag lane here; full
            // ftCommon_8007D7FC bookkeeping would clear fastfall on replay-real grounded DamageAir
            // rows where Slippi still reports it.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
            // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
            batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
            batch->state.jumps_left[idx] = ch->max_jumps;
          }
          continue;
        }

        if (a0 == (uint16_t)MSL_ACT_DOWN_DAMAGE_U || a0 == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) {
          // DownDamage collision can preserve the downed damage motion while applying the same
          // ground-transfer bookkeeping as other air->ground contact paths. Keep this scoped to rows
          // where mpColl has already reported ground; rows that miss `now_ground` remain floor
          // contact substrate, not a bookkeeping fix.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
          transfer_air_to_ground_on_land(batch, ch, (size_t)bi, idx, a0);
          continue;
        }
      } else if (!now_ground && is_down_bound(a0) &&
                 down_bound_airborne_ledge_cross_to_fall(batch, idx, stage_id)) {
        // Decomp: DownBound_Coll immediately enters Fall when the allow-ground-to-air helper reports
        // edge exit for this collision step.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
        batch->state.fall_fast[idx] = 0;
        batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;
        batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      } else if (was_ground && !now_ground) {
        if (is_damage_ground_action(a0) &&
            damage_ground_floor_loss_should_missfoot(batch, idx, stage_id)) {
          enter_missfoot_from_damage_floor_loss(batch, ch, idx);
          continue;
        }

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
