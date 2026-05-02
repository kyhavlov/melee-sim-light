#include "physics.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "stage_collision.h"
#include "specialhi_pose.h"
#include "state_flags.h"

static inline uint8_t physics_action_skip_common_air_helper_first_frame(uint16_t action_id,
                                                                        uint16_t prev_action_id,
                                                                        int16_t action_frame) {
  // Decomp:
  // - ftCo_Jump_Phys_Inner skips `ft_80084DB0` on the first frame after entering JumpF/B.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Phys_Inner
  // - ftCo_CliffJump2_Phys skips the common fall helper on the first frame.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Phys
  switch (action_id) {
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
      return (uint8_t)(action_frame <= 0);
    case MSL_ACT_CLIFF_JUMP_SLOW2:
      return (uint8_t)(prev_action_id == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1);
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return (uint8_t)(prev_action_id == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1);
    default:
      return 0;
  }
}

static inline float air_apply_friction_step(float vel, float friction) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionAir
  float accel = friction;
  if (msl_absf(accel) >= msl_absf(vel)) {
    accel = -vel;
  } else if (vel > 0.0f) {
    accel = -accel;
  }
  return vel + accel;
}

static inline float physics_q16_16_to_f32(int32_t x) { return (float)x * (1.0f / 65536.0f); }

static inline float air_apply_accel_step(float vel, float accel, float target_vel, float friction,
                                         float air_max_horizontal_velocity) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D174
  if (target_vel == 0.0f) {
    return air_apply_friction_step(vel, friction);
  }

  float a = accel;
  if (!(vel * a < 0.0f)) {
    if (a > 0.0f) {
      if (vel + a > target_vel) {
        a = -friction;
        if (vel + a < target_vel) {
          a = target_vel - vel;
        }
        if (vel + a > air_max_horizontal_velocity) {
          a = air_max_horizontal_velocity - vel;
        }
      }
    } else {
      if (vel + a < target_vel) {
        a = friction;
        if (vel + a > target_vel) {
          a = target_vel - vel;
        }
        if (vel + a < -air_max_horizontal_velocity) {
          a = -air_max_horizontal_velocity - vel;
        }
      }
    }
  }
  return vel + a;
}

static inline float ground_friction_step_delta(float gr_vel, float friction) {
  // Decomp: ftCommon_ApplyFrictionGround writes fp->xE4_ground_accel_1.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround
  float accel = friction;
  if (msl_absf(accel) > msl_absf(gr_vel)) {
    accel = -gr_vel;
  } else if (gr_vel > 0.0f) {
    accel = -accel;
  }
  return accel;
}

static inline float ground_accel_step_delta(float gr_vel, float accel, float target_vel,
                                            float friction, float ground_max_horizontal_velocity) {
  // Decomp: ftCommon_8007C98C writes fp->xE4_ground_accel_1 (ground accel/traction step).
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007C98C
  if (target_vel == 0.0f) {
    return ground_friction_step_delta(gr_vel, friction);
  }

  float a = accel;
  if (!(gr_vel * a < 0.0f)) {
    if (a > 0.0f) {
      if (gr_vel + a > target_vel) {
        a = -friction;
        if (gr_vel + a < target_vel) {
          a = target_vel - gr_vel;
        }
        if (gr_vel + a > ground_max_horizontal_velocity) {
          a = ground_max_horizontal_velocity - gr_vel;
        }
      }
    } else {
      if (gr_vel + a < target_vel) {
        a = friction;
        if (gr_vel + a > target_vel) {
          a = target_vel - gr_vel;
        }
        if (gr_vel + a < -ground_max_horizontal_velocity) {
          a = -ground_max_horizontal_velocity - gr_vel;
        }
      }
    }
  }
  return a;
}

static inline void physics_apply_specialhi_air_reverse_accel(const MslBatch* batch, size_t idx,
                                                             const MslCharParams* ch,
                                                             int16_t action_frame, float* io_vel_x,
                                                             float* io_vel_y) {
  if (ch == NULL || io_vel_x == NULL || io_vel_y == NULL) {
    return;
  }
  // Decomp: ftFx_SpecialAirHi_Phys increments `mv.fx.SpecialHi.unk`, then subtracts the x78
  // reverse-accel vector projected along `rotateModel` once `unk >= x70`.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Phys
  if ((int16_t)(action_frame + 1) < (int16_t)ch->firefox_launch_reverse_accel_start_frames) {
    return;
  }

  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
    return;
  }
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  *io_vel_x = -((facing_dir * (ch->firefox_launch_reverse_accel * cosf(rotate_model))) - *io_vel_x);
  *io_vel_y = -((ch->firefox_launch_reverse_accel * sinf(rotate_model)) - *io_vel_y);
}

static inline void physics_apply_knockback_decay(MslBatch* batch, size_t idx,
                                                 const MslCharParams* ch, const MslCommonParams* c,
                                                 uint8_t on_ground) {
  if (batch == NULL || c == NULL) {
    return;
  }
  float kb_x = batch->state.speed_x_attack[idx];
  float kb_y = batch->state.speed_y_attack[idx];
  if (kb_x == 0.0f && kb_y == 0.0f) {
    return;
  }

  // Decomp: Fighter_procUpdate applies knockback decay before integrating current-frame position.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  if (!on_ground) {
    // Decomp air branch: if |kb| < p_ftCommonData->x204 then zero; else subtract that amount along
    // the current knockback direction.
    // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    const float kb_mag = sqrtf(kb_x * kb_x + kb_y * kb_y);
    const float decay = c->knockback_frame_decay;
    if (kb_mag < decay) {
      kb_x = 0.0f;
      kb_y = 0.0f;
    } else {
      const float kb_angle = atan2f(kb_y, kb_x);
      kb_x -= decay * cosf(kb_angle);
      kb_y -= decay * sinf(kb_angle);
    }
  } else if (ch != NULL) {
    // Decomp ground branch:
    // - decay scalar ground KB (`xF0_ground_kb_vel`) via ftCommon_8007CCA0 with
    //   arg = ft_GetGroundFrictionMultiplier(fp) * co_attrs.gr_friction * p_ftCommonData->x200.
    // - rebuild kb_vel as ground_kb_vel * floor tangent.
    // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CCA0
    //
    // Ground friction multiplier lane ownership:
    // - `ground_friction_mul` is a seeded/runtime lane mirroring ft_GetGroundFrictionMultiplier(fp).
    // - For stale datasets/tests without this lane, reseed sanitizes non-positive values to 1.0f.
    // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
    const float nx = batch->state.ground_normal_x[idx];
    const float ny = batch->state.ground_normal_y[idx];
    const float tangent_x = ny;
    const float tangent_y = -nx;
    float ground_kb = kb_x * tangent_x + kb_y * tangent_y;
    const float friction =
        batch->state.ground_friction_mul[idx] * ch->gr_friction * c->ground_kb_friction_mul;

    if (ground_kb < 0.0f) {
      ground_kb += friction;
      if (ground_kb > 0.0f) {
        ground_kb = 0.0f;
      }
    } else {
      ground_kb -= friction;
      if (ground_kb < 0.0f) {
        ground_kb = 0.0f;
      }
    }

    kb_x = tangent_x * ground_kb;
    kb_y = tangent_y * ground_kb;
  }

  batch->state.speed_x_attack[idx] = kb_x;
  batch->state.speed_y_attack[idx] = kb_y;
}

static inline uint8_t physics_action_is_walk(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_WALK_SLOW || action_id == (uint16_t)MSL_ACT_WALK_MIDDLE ||
          action_id == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1
             : 0;
}

static inline uint8_t physics_action_uses_ft_80084FA8(uint16_t action_id) {
  // Decomp callback ownership:
  // - ftCo_Attack11/12/13 all use ftCo_Attack11_Phys.
  // - ftCo_Attack11_Phys calls ft_80084FA8.
  // - ftCo_Attack100Start/Loop/End_Phys call ft_80084FA8.
  // - ftCo_AttackS4_Phys also calls ft_80084FA8 for all AttackS4* variants.
  // - PassiveStandF/B Phys calls ft_80084FA8.
  // - CliffClimb/Attack/Escape quick grounded Phys paths share ftCo_CliffClimb_Phys, which calls
  //   ft_80084FA8 once the option has reached the stage.
  // - Common AppealS Phys calls ft_80084FA8.
  // refs/melee/src/melee/ft/ftmotionstates.c (Attack11/12/13 entries)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_Attack100Start_Phys,ftCo_Attack100Loop_Phys,ftCo_Attack100End_Phys}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_Phys
  switch (action_id) {
    case MSL_ACT_ATTACK_11:
    case MSL_ACT_ATTACK_12:
    case MSL_ACT_ATTACK_13:
    case MSL_ACT_ATTACK_100_START:
    case MSL_ACT_ATTACK_100_LOOP:
    case MSL_ACT_ATTACK_100_END:
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
    case MSL_ACT_PASSIVE_STAND_F:
    case MSL_ACT_PASSIVE_STAND_B:
    case MSL_ACT_CLIFF_CLIMB_SLOW:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_SLOW:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_APPEAL_SR:
    case MSL_ACT_APPEAL_SL:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t physics_action_is_common_ground_friction_only(uint16_t action_id) {
  // Decomp: these callbacks use `ft_80084F3C` (ground friction helper) in their Phys function.
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Phys
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //     ftFx_SpecialLwStart_Phys,ftFx_SpecialLwLoop_Phys,ftFx_SpecialLwHit_Phys,
  //     ftFx_SpecialLwTurn_Phys,ftFx_SpecialLwEnd_Phys}
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_TURN:
    case MSL_ACT_KNEE_BEND:
    case MSL_ACT_SQUAT:
    case MSL_ACT_SQUAT_WAIT:
    case MSL_ACT_SQUAT_RV:
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL:
    case MSL_ACT_LANDING_AIR_N:
    case MSL_ACT_LANDING_AIR_F:
    case MSL_ACT_LANDING_AIR_B:
    case MSL_ACT_LANDING_AIR_HI:
    case MSL_ACT_LANDING_AIR_LW:
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
    case MSL_ACT_REBOUND:
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
    case MSL_ACT_ATTACK_HI3:
    case MSL_ACT_ATTACK_LW3:
    case MSL_ACT_ATTACK_HI4:
    case MSL_ACT_ATTACK_LW4:
    case MSL_ACT_FX_SPECIAL_S_START:
    case MSL_ACT_FX_SPECIAL_LW_START:
    case MSL_ACT_FX_SPECIAL_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_LW_HIT:
    case MSL_ACT_FX_SPECIAL_LW_END:
    case MSL_ACT_FX_SPECIAL_LW_TURN:
    case MSL_ACT_DOWN_BOUND_U:
    case MSL_ACT_DOWN_BOUND_D:
      return 1;
    default:
      return 0;
  }
}

static inline float physics_cur_anim_frame_f32(const MslBatch* batch, size_t idx) {
  // Fighter root-motion consumers read the live cur_anim_frame timebase, not the seed snapshot.
  // - anim_timebase_update_pre_input() advances anim_frame_fp_q16_16 before Phys callbacks.
  // - anim_frame_f32 is a derived snapshot lane and can be stale mid-step.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  if (batch == NULL) {
    return 0.0f;
  }
  return msl_anim_frame_sanitize_f32(physics_q16_16_to_f32(batch->state.anim_frame_fp_q16_16[idx]));
}

static inline float physics_prev_anim_frame_f32(const MslBatch* batch, size_t idx) {
  // Use the actual previous cur_anim_frame from the deterministic Q16.16 timebase so root-motion
  // only advances when the animation crosses a new integer frame.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  if (batch == NULL) {
    return 0.0f;
  }
  return msl_anim_frame_sanitize_f32(physics_q16_16_to_f32(
      batch->state.anim_frame_fp_q16_16[idx] - batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline uint8_t physics_try_get_transn_delta_xyz(const MslCharParams* ch, uint8_t char_id,
                                                       uint32_t msid_u32, float prev_anim_frame_f32,
                                                       float anim_frame_f32,
                                                       float out_delta_xyz[3]) {
  if (ch == NULL || out_delta_xyz == NULL) {
    return 0;
  }
  if (!(msid_u32 <= 0xFFFFu)) {
    return 0;
  }
  const uint16_t msid = (uint16_t)msid_u32;

  // Our ISO-derived SSANIM01 v4 artifacts store per-frame TransN translation as a tail (x,y,z);
  // approximate the per-frame TransN offset as a finite difference between the previous and current
  // animation frames.
  // - tools/extraction/extract_fighter_anims.py (SSANIM01 v4 + per-frame TransN tail)
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80085030 (consumer of fp->x6A4_transNOffset.{y,z})
  const uint16_t f_cur = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(anim_frame_f32));
  const uint16_t f_prev =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(prev_anim_frame_f32));
  if (f_cur == f_prev) {
    out_delta_xyz[0] = 0.0f;
    out_delta_xyz[1] = 0.0f;
    out_delta_xyz[2] = 0.0f;
    return 1;
  }

  float t_cur[3];
  float t_prev[3];
  if (anim_pose_get_transn(char_id, msid, f_cur, t_cur) != 0 ||
      anim_pose_get_transn(char_id, msid, f_prev, t_prev) != 0) {
    return 0;
  }

  // TransNPos is in fighter model space; apply per-character model scaling so the resulting
  // per-frame transNOffset matches engine/world units.
  // Source of truth for model scaling: ISO-extracted `data/characters/<char>.json` `model_scaling`.
  // Decomp: refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::model_scaling
  out_delta_xyz[0] = (t_cur[0] - t_prev[0]) * ch->model_scaling;
  out_delta_xyz[1] = (t_cur[1] - t_prev[1]) * ch->model_scaling;
  out_delta_xyz[2] = (t_cur[2] - t_prev[2]) * ch->model_scaling;
  return 1;
}

static inline uint8_t physics_try_get_transn_delta_xyz_f32(const MslCharParams* ch, uint8_t char_id,
                                                           uint32_t msid_u32,
                                                           float prev_anim_frame_f32,
                                                           float anim_frame_f32,
                                                           float out_delta_xyz[3]) {
  if (ch == NULL || out_delta_xyz == NULL) {
    return 0;
  }
  if (!(msid_u32 <= 0xFFFFu)) {
    return 0;
  }
  const uint16_t msid = (uint16_t)msid_u32;

  // Weighted throws can advance the live AObj by fractional frames. ft_80085030 consumes the
  // current HSD_AObjInterpretAnim TransN offset, so sample the SSANIMT1 FObj stream instead of
  // flooring to the integer SSANIM01 tail.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
  // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
  float t_cur[3];
  float t_prev[3];
  if (anim_pose_get_transn_f32(char_id, msid, anim_frame_f32, t_cur) != 0 ||
      anim_pose_get_transn_f32(char_id, msid, prev_anim_frame_f32, t_prev) != 0) {
    return 0;
  }

  out_delta_xyz[0] = (t_cur[0] - t_prev[0]) * ch->model_scaling;
  out_delta_xyz[1] = (t_cur[1] - t_prev[1]) * ch->model_scaling;
  out_delta_xyz[2] = (t_cur[2] - t_prev[2]) * ch->model_scaling;
  return 1;
}

static inline uint8_t physics_action_anim_uses_root_motion(uint8_t char_id, uint32_t msid_u32) {
  if (msid_u32 > 0xFFFFu) {
    return 0u;
  }
  return msl_anim_uses_root_motion(char_id, (uint16_t)msid_u32);
}

uint8_t physics_apply_attackdash_entry_phys_now(MslBatch* batch, size_t idx, float facing_dir) {
  if (batch == NULL || idx >= (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ATTACK_DASH ||
      batch->state.on_ground[idx] == 0u) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  float dxyz[3];
  if (!physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                            batch->state.animation_index[idx]) ||
      !physics_try_get_transn_delta_xyz(
          ch, batch->state.char_id[idx], batch->state.animation_index[idx],
          physics_prev_anim_frame_f32(batch, idx), physics_cur_anim_frame_f32(batch, idx), dxyz)) {
    return 0u;
  }
  // Dash/Run/RunDirect IASA run before Phys in source, but this simulator's grounded locomotion
  // IASA pass is post-collision. For the narrow same-frame AttackDash entry path, restore the
  // missed `ftCo_AttackDash_Phys -> ft_80085030` root-motion velocity immediately after the
  // source-shaped entry tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Run.c,ftCo_RunDirect.c}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{doEnter,ftCo_AttackDash_Phys}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
  const float vx = dxyz[2] * facing_dir;
  batch->state.speed_ground_x_self[idx] = vx;
  batch->state.speed_air_x_self[idx] = vx;
  return 1u;
}

static inline uint8_t physics_action_is_fall_special_like(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_FALL_SPECIAL ||
          action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
          action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B)
             ? 1
             : 0;
}

static inline uint8_t physics_action_uses_common_air_drift(uint16_t action_id) {
  // Decomp: the common helper `ft_80084DB0` calls `ftCommon_8007D268` to compute x drift.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  //
  // EscapeAir is special-cased: when cmd_skip_decay is false, EscapeAir scales self_vel and does
  // not call `ft_80084DB0` in-engine.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return 0;
  }
  return msl_action_allows_fastfall(action_id);
}

static inline float physics_apply_common_air_drift(const MslCharParams* ch,
                                                   const MslCommonParams* c, uint16_t action_id,
                                                   uint8_t fallspecial_xc, float stick_x,
                                                   float vel_x) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D28C (via ftCommon_8007D268)
  float accel_scaling = stick_x * ch->air_drift_stick_mul;
  float accel_flat = stick_x > 0.0f ? +ch->aerial_drift_base : -ch->aerial_drift_base;
  float target = stick_x * ch->air_drift_max;

  // FallSpecial drift cap ("mobility").
  //
  // Decomp: ftCo_80096900 stores `mv.co.fallspecial.mobility = ca->air_drift_max * mobility_scalar`, and
  // ftCo_FallSpecial_Phys clamps |target_vel| to that mobility only on the `xC == 0` branch.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Phys
  if (physics_action_is_fall_special_like(action_id) && c != NULL && fallspecial_xc == 0) {
    const float mobility = ch->air_drift_max * c->fall_special_mobility_scalar;
    if (msl_absf(target) > mobility) {
      target = (target < 0.0f) ? -mobility : +mobility;
    }
  }

  return air_apply_accel_step(vel_x, accel_scaling + accel_flat, target, ch->aerial_friction,
                              ch->air_max_horizontal_velocity);
}

static inline void physics_apply_specialhi_hold_air(const MslCharParams* ch, int16_t action_frame,
                                                    float* io_vel_x, float* io_vel_y) {
  if (ch == NULL || io_vel_x == NULL || io_vel_y == NULL) {
    return;
  }
  // Decomp: SpecialHi HoldAir applies air friction using ftFox_DatAttrs.x5C each frame.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiHoldAir_Phys
  *io_vel_x = air_apply_friction_step(*io_vel_x, ch->firefox_hold_air_friction);

  // Decomp exact gate in ftFx_SpecialHiHoldAir_Phys:
  // - if (mv.fx.SpecialHi.gravityDelay != 0) { --gravityDelay; } else { apply gravity; }
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiHoldAir_Phys
  //
  // Mapping in this sim:
  // - `action_frame` is our post-advance integer frame index (derived from anim timebase), not the
  //   fighter's internal float countdown field.
  // - Therefore parity is `>` (not `>=`) against the extracted hold delay frame count.
  // - Gravity accel and terminal clamp are still sourced from decomp/data:
  //   ftFox_DatAttrs.x60 and co_attrs.terminal_vel.
  if (action_frame > (int16_t)ch->firefox_hold_gravity_delay_frames) {
    *io_vel_y -= ch->firefox_hold_air_fall_accel;
    if (*io_vel_y < -ch->terminal_vel) {
      *io_vel_y = -ch->terminal_vel;
    }
  }
}

static inline uint8_t physics_shine_air_applies_fall_this_frame(const MslCharParams* ch,
                                                                uint16_t action_id,
                                                                uint16_t prev_action_id,
                                                                int16_t action_frame) {
  if (ch == NULL || action_id < (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START ||
      action_id > (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN) {
    return 0u;
  }
  // Decomp: all aerial Reflector Phys callbacks decrement `mv.fx.SpecialLw.gravityDelay` until it
  // reaches 0, then call ftCommon_Fall with ftFox_DatAttrs.xAC and common terminal velocity.
  // Start is the only state that can still own the initial delay in supported replay seeds; Loop,
  // Hit, Turn, and End are entered after the start animation has consumed that countdown.
  //
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFox_SpecialLw_SetVars,ftFx_SpecialAirLwStart_Phys,
  //   ftFx_SpecialAirLwLoop_Phys,ftFx_SpecialAirLwHit_Phys,
  //   ftFx_SpecialAirLwTurn_Phys,ftFx_SpecialAirLwEnd_Phys}
  // data/characters/{fox,falco}.json::{reflector_gravity_delay_frames,reflector_fall_accel}
  if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
    return (uint8_t)(action_frame >= (int16_t)ch->reflector_gravity_delay_frames);
  }
  if (prev_action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
      action_id != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START && action_frame <= 0) {
    return 0u;
  }
  return 1u;
}

static inline void physics_apply_shine_air_fall(const MslCharParams* ch, float* io_vel_y) {
  if (ch == NULL || io_vel_y == NULL) {
    return;
  }
  *io_vel_y -= ch->reflector_fall_accel;
  if (*io_vel_y < -ch->terminal_vel) {
    *io_vel_y = -ch->terminal_vel;
  }
}

static inline uint8_t physics_action_is_shine_air(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t physics_action_is_damage_fly(uint16_t action_id) {
  // Generated from decomp MotionState callback symbols:
  // - ftCo_DamageFly_* for DamageFlyHi/N/Lw/Top/Roll
  // - ftCo_FlyReflect_* for wall/ceiling reflect follow-up states
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t physics_is_pending_throw_release_victim(const MslBatch* batch, int bi,
                                                              int victim_p) {
  if (batch == NULL) {
    return 0u;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players) {
    return 0u;
  }
  for (int owner_p = 0; owner_p < num_players; owner_p++) {
    if (owner_p == victim_p) {
      continue;
    }
    const size_t oidx = msl_idx_player(bi, owner_p);
    if (batch->state.throw_pending_victim_port[oidx] == (uint8_t)victim_p &&
        batch->state.throw_pending_hit_idx[oidx] != 0xFFu) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t physics_action_is_common_damage(uint16_t action_id) {
  // Generated from decomp MotionState callback symbols:
  // - ftCo_Damage_* for DamageHi/N/Lw/Air
  // - ftCo_DownDamage_* for DownDamageU/D, whose Phys delegates to common Damage Phys
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Phys
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_DAMAGE_COMMON);
}

static inline uint8_t physics_action_is_passivewall(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_PASSIVE_WALL ||
          action_id == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP)
             ? 1u
             : 0u;
}

static inline uint8_t physics_damage_iasa_lockout_x221c_b6(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Decomp: common Damage and DamageFly/DamageFlyRoll Phys callbacks branch on fp->x221C_b6:
  // - x221C_b6==0: ft_80084DB0 (fall helper + drift)
  // - x221C_b6==1: ft_80084EEC (fall + aerial friction)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_Phys,ftCo_DamageFly_Phys,ftCo_DamageFlyRoll_Phys
  // }
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084DB0,ft_80084EEC}
  //
  return msl_state_flags_221c_b6_at(batch->state.state_flags, idx);
}

static inline float physics_apply_ftcommon_8007cf58_x_clamp(const MslCharParams* ch,
                                                            const MslCommonParams* c, float vel_x) {
  // Decomp: aerial Reflector (SpecialAirLw*) uses `ftCommon_8007CF58`, which sets fp->x74_anim_vel.x to a
  // friction/clamp step using p_ftCommonData->x1FC when |vel| exceeds air_drift_max, else co_attrs.aerial_friction.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58
  float friction = ch->aerial_friction;
  if (c != NULL && msl_absf(vel_x) > ch->air_drift_max) {
    friction = c->air_drift_overmax_friction;
  }
  return air_apply_friction_step(vel_x, friction);
}

static inline uint8_t physics_action_use_pre_integration_common_air_gravity(uint16_t action_id) {
  // Decomp: many common airborne action states call `ft_80084DB0` from their phys callbacks, which
  // runs `ftCommon_CheckFallFast` + `ftCommon_Fall/FallFast` (mutating `self_vel.y`) before
  // `Fighter_procUpdate` integrates `cur_pos`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  //
  // However, in the v1 teacher-forced reseed loop we currently treat Damage*/hitstun-y states as
  // mostly seed-driven. Applying the common fall helper for DamageFall shifts `pos_y` by ~grav on
  // some frames and can cause match-flow blastzone false positives (stocks decremented) vs the
  // replay reference.
  //
  // Repro (suite): `AttachedGoodNaturedGuanaco.msl` record=2959 p=0 (DamageFall) crosses FD blast
  // bottom and triggers `MSL_ACT_DEAD_DOWN` when we apply common gravity pre-integration.
  //
  // Until DamageFall's full physics path is modeled (including its interaction with hitstun/KB),
  // keep the ordering fix for locomotion/attackair states but exclude DamageFall here.
  // Note:
  // - Common airborne Damage states (for example DamageAir2) route through ftCo_Damage_Phys,
  //   which calls ft_80084DB0 when x221C_b6 is clear.
  // - DamageFly is handled via its own x221C_b6-gated branch below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys
  if (!msl_action_allows_fastfall(action_id)) {
    return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_2);
  }
  return (uint8_t)(action_id != (uint16_t)MSL_ACT_DAMAGE_FALL);
}

static inline uint8_t physics_action_use_post_integration_common_air_gravity(uint16_t action_id) {
  // Temporary v1 compatibility: update `speed_y_self` for next frame without affecting current
  // frame displacement (see note above).
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL);
}

static inline uint8_t physics_damagefall_entry_uses_pre_integration_phys(uint16_t action_id,
                                                                         uint16_t prev_action_id,
                                                                         int16_t action_frame) {
  // DamageFly_Anim can enter DamageFall via ftCo_80090780 before the same Fighter_procUpdate
  // reaches Phys. The destination DamageFall_Phys calls ft_80084DB0, so gravity mutates
  // self_vel.y before Fighter_procUpdate integrates cur_pos on that entry frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{ftCo_80090780,ftCo_DamageFall_Phys}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL && action_frame <= 0 &&
                   physics_action_is_damage_fly(prev_action_id));
}

static inline uint8_t physics_action_is_guardsetoff_turnover_owner(uint16_t action_id,
                                                                   uint16_t prev_action_id) {
  // Narrow grounded-overlap subset for the replay-real GuardSetOff->Escape turnover lane:
  // - ftCommon_8007E0E4 is a common grounded overlap helper invoked after Anim callbacks and before
  //   Fighter_procUpdate physics integration.
  // - Keep this runtime slice to the GuardSetOff steady/turnover window where the missing
  //   `p_ftCommonData->x450` nudge is directly evidenced.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF ||
                   prev_action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF);
}

static inline uint8_t physics_action_suppresses_self_player_nudge_x221d_b5(
    uint16_t action_id, uint16_t prev_action_id) {
  // Decomp: Escape entry (`ftCo_80099314`) sets `fp->x221D_b5 = true`; common grounded
  // fighter-overlap nudge (`ftCommon_8007E0E4`) skips the self `ftCommon_8007DD7C` pass while that
  // bit is set. The peer can still nudge away because `ftCommon_8007DD7C` does not filter the
  // other fighter on x221D_b5.
  //
  // Source ordering boundary:
  // - Fighter_8006A360 runs Anim + ftCommon_8007E0E4 before the later IASA/input proc.
  // - Same-frame Guard_IASA -> Escape* entries set x221D_b5 after the current frame's common
  //   nudge pass, so only continuous Escape frames suppress the self pass here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099314
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
  const uint8_t current_escape =
      (action_id == (uint16_t)MSL_ACT_ESCAPE_N || action_id == (uint16_t)MSL_ACT_ESCAPE_F ||
       action_id == (uint16_t)MSL_ACT_ESCAPE_B)
          ? 1u
          : 0u;
  const uint8_t prev_escape =
      (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_N ||
       prev_action_id == (uint16_t)MSL_ACT_ESCAPE_F || prev_action_id == (uint16_t)MSL_ACT_ESCAPE_B)
          ? 1u
          : 0u;
  return (uint8_t)(current_escape && prev_escape);
}

static inline uint8_t physics_floor_lines_adjacent_or_equal(const MslStageFloorGraph* g, int a,
                                                            int b) {
  if (g == NULL || a < 0 || b < 0 || (size_t)a >= g->line_count || (size_t)b >= g->line_count) {
    return 0u;
  }
  if (a == b) {
    return 1u;
  }
  const MslStageFloorLine* la = &g->lines[(size_t)a];
  return (uint8_t)(la->prev == b || la->next == b);
}

static inline uint8_t physics_floor_line_contains_or_connects_to_nudged_x(
    const MslStageFloorGraph* g, int line_idx, float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  // Conservative safety gate for the reduced horizontal x450 nudge:
  // decomp computes xF8_playerNudgeVel before Fighter_procUpdate, but the full mpColl follow-up can
  // preserve floor ownership at connected endpoints. Admit only current/adjacent connected floor
  // segments here; the later motion-state collision callback owns the actual floor.index update.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004A45C_Floor}
  if (x >= line->x0 && x <= line->x1) {
    return 1u;
  }
  const int adj[2] = {line->prev, line->next};
  for (size_t i = 0; i < 2; i++) {
    const int adj_idx = adj[i];
    if (adj_idx < 0 || (size_t)adj_idx >= g->line_count) {
      continue;
    }
    const MslStageFloorLine* other = &g->lines[(size_t)adj_idx];
    if (x >= other->x0 && x <= other->x1) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t physics_action_is_attackdash_knockdown_overlap_owner(uint16_t action_id,
                                                                           uint16_t other_action) {
  const uint8_t other_is_knockdown = (other_action == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_BOUND_D ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_WAIT_D ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_STAND_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_STAND_D ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_ATTACK_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_ATTACK_D ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_FOWARD_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_FOWARD_D ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_BACK_U ||
                                      other_action == (uint16_t)MSL_ACT_DOWN_BACK_D)
                                         ? 1u
                                         : 0u;
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_ATTACK_DASH && other_is_knockdown);
}

static inline void physics_compute_grounded_player_nudge(MslBatch* batch, int bi,
                                                         float out_nudge_x[MSL_MAX_PLAYERS],
                                                         float out_nudge_z[MSL_MAX_PLAYERS]) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || out_nudge_x == NULL ||
      out_nudge_z == NULL) {
    return;
  }

  // Grounded fighter-overlap nudge constants (`p_ftCommonData->x450` / x454 / x458):
  // - ftCommon_8007DD7C accumulates +/-x450 on horizontal overlap.
  // - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate; Fighter_procUpdate then
  //   adds xF8_playerNudgeVel to position before self/KB velocity integration.
  // - The hidden depth lane uses x454 steps and x458 clamp on the normal non-x221F_b4 path, then
  //   decays toward z=0 when no overlap writes xF8_playerNudgeVel.y.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/types.h::ftCommonData (+0x450/+0x454/+0x458)
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const float player_nudge_x_step = c->player_nudge_x;

  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
  if (floor_graph == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    out_nudge_x[p] = 0.0f;
    out_nudge_z[p] = 0.0f;
  }

  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.stocks[idx] == 0u || batch->state.on_ground[idx] == 0u ||
        batch->state.hitlag_started_frame[idx] != 0u) {
      continue;
    }
    if (msl_action_is_grabbed_victim(batch->state.action_id[idx])) {
      continue;
    }
    if (physics_action_is_guardsetoff_turnover_owner(batch->state.action_id[idx],
                                                     batch->state.prev_action_id[idx]) ||
        physics_action_is_guardsetoff_turnover_owner(batch->state.action_id[idx],
                                                     batch->state.seed_prev_action_id[idx])) {
      continue;
    }

    const MslCharParams* self = msl_char_params(batch->state.char_id[idx]);
    if (self == NULL) {
      continue;
    }

    const int self_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    if (self_line < 0) {
      continue;
    }

    if (!physics_action_suppresses_self_player_nudge_x221d_b5(batch->state.action_id[idx],
                                                              batch->state.prev_action_id[idx])) {
      const float self_center_x =
          batch->state.pos_x[idx] + self->pushbox_x * (float)batch->state.facing_dir1[idx];
      for (int q = 0; q < num_players; q++) {
        if (q == p) {
          continue;
        }
        const size_t oidx = msl_idx_player(bi, q);
        // Fighter_8006A360 invokes ftCommon_8007E0E4 once per fighter after that fighter's Anim
        // callback, not after every fighter's callback has completed. For later GObj/player slots,
        // the current fighter's nudge still sees the peer's frame-start grounded state. This
        // matters for KneeBend -> JumpF/B rows where the peer becomes airborne later in the same
        // global pass.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
        const uint8_t other_on_ground_for_nudge =
            (q > p) ? batch->state.frame_start_on_ground[oidx] : batch->state.on_ground[oidx];
        const uint16_t other_action_for_nudge =
            (q > p) ? batch->state.prev_action_id[oidx] : batch->state.action_id[oidx];
        if (batch->state.stocks[oidx] == 0u || other_on_ground_for_nudge == 0u) {
          continue;
        }
        if (msl_action_is_grabbed_victim(other_action_for_nudge)) {
          continue;
        }
        if (physics_action_is_attackdash_knockdown_overlap_owner(batch->state.action_id[idx],
                                                                 other_action_for_nudge) ||
            physics_action_is_attackdash_knockdown_overlap_owner(other_action_for_nudge,
                                                                 batch->state.action_id[idx])) {
          // Existing replay-real seed bridge owns this family after collision/knockdown resolution.
          // Applying the common pre-physics approximation here double-counts the x450 lane for
          // those rows. Keep that narrower owner until the full ftCommon_8007E0E4 Z/ceiling branch
          // is modeled.
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C
          continue;
        }

        const MslCharParams* other = msl_char_params(batch->state.char_id[oidx]);
        if (other == NULL) {
          continue;
        }

        const int other_line =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[oidx]);
        if (!physics_floor_lines_adjacent_or_equal(floor_graph, self_line, other_line)) {
          continue;
        }

        const float other_center_x =
            batch->state.pos_x[oidx] + other->pushbox_x * (float)batch->state.facing_dir1[oidx];
        const float delta_x = self_center_x - other_center_x;
        if (msl_absf(delta_x) >= self->pushbox_y + other->pushbox_y) {
          continue;
        }
        float nudge_x = 0.0f;
        if (delta_x < 0.0f) {
          nudge_x = -player_nudge_x_step;
        } else if (delta_x > 0.0f) {
          nudge_x = player_nudge_x_step;
        } else if (q < p) {
          nudge_x = -player_nudge_x_step;
        } else {
          nudge_x = player_nudge_x_step;
        }
        if (!physics_floor_line_contains_or_connects_to_nudged_x(
                floor_graph, self_line, batch->state.pos_x[idx] + nudge_x)) {
          continue;
        }
        out_nudge_x[p] += nudge_x;

        // Grounded fighter-overlap depth nudge:
        // - ftCommon_8007DD7C writes xF8_playerNudgeVel.y from p_ftCommonData->x454.
        // - If fighters already differ in z, it pushes along that signed depth delta; otherwise it
        //   uses the same horizontal/tie-break sign as the x450 lane.
        // - Fighter_procUpdate applies the resulting Vec2 to cur_pos before collision primitives
        //   are refreshed, so BODY lbColl sees the separated depth lane even though Slippi commonly
        //   leaves replay seed pos_z at 0.
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        float nudge_z = 0.0f;
        const float delta_z = batch->state.pos_z[idx] - batch->state.pos_z[oidx];
        if (delta_z < 0.0f) {
          nudge_z = -c->player_nudge_z;
        } else if (delta_z > 0.0f) {
          nudge_z = c->player_nudge_z;
        } else if (delta_x < 0.0f) {
          nudge_z = -c->player_nudge_z;
        } else if (delta_x > 0.0f) {
          nudge_z = c->player_nudge_z;
        } else if (q < p) {
          nudge_z = -c->player_nudge_z;
        } else {
          nudge_z = c->player_nudge_z;
        }
        out_nudge_z[p] += nudge_z;
      }
    }

    // ftCommon_8007E0E4 post-processes the depth lane after ftCommon_8007DD7C:
    // - no overlap and z != 0: step toward z=0 by x454;
    // - do not cross through zero;
    // - clamp the resulting normal depth to +/-x458.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
    float z_step = out_nudge_z[p];
    const float z = batch->state.pos_z[idx];
    if (z_step == 0.0f && z != 0.0f) {
      z_step = (z < 0.0f) ? c->player_nudge_z : -c->player_nudge_z;
    }
    if ((z_step > 0.0f && z < 0.0f && z + z_step >= 0.0f) ||
        (z_step < 0.0f && z > 0.0f && z + z_step <= 0.0f)) {
      z_step = -z;
    }
    const float z_max = c->player_nudge_z_max;
    if (z + z_step > z_max) {
      z_step = z_max - z;
    } else if (z + z_step < -z_max) {
      z_step = -z_max - z;
    }
    out_nudge_z[p] = z_step;
  }
}

static inline void physics_compute_guardsetoff_turnover_player_nudge(
    MslBatch* batch, int bi, float out_nudge_x[MSL_MAX_PLAYERS]) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || out_nudge_x == NULL) {
    return;
  }

  // Existing replay-real GuardSetOff bridge. This intentionally keeps the old scoped owner
  // separate from the broader common-player-nudge approximation above because this family has
  // already been locked against current replay rows.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  // refs/melee/src/melee/ft/types.h::ftCommonData (+0x450/+0x454)
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const float player_nudge_x_step = c->player_nudge_x;

  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
  if (floor_graph == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    out_nudge_x[p] = 0.0f;
  }

  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.stocks[idx] == 0u || batch->state.on_ground[idx] == 0u ||
        batch->state.hitlag_started_frame[idx] != 0u) {
      continue;
    }
    const uint8_t guardsetoff_turnover_from_prev = physics_action_is_guardsetoff_turnover_owner(
        batch->state.action_id[idx], batch->state.prev_action_id[idx]);
    const uint8_t guardsetoff_turnover_from_promoted_seed_prev =
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD &&
         batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
            ? physics_action_is_guardsetoff_turnover_owner(batch->state.action_id[idx],
                                                           batch->state.seed_prev_action_id[idx])
            : 0u;
    if (!guardsetoff_turnover_from_prev && !guardsetoff_turnover_from_promoted_seed_prev) {
      continue;
    }

    const MslCharParams* self = msl_char_params(batch->state.char_id[idx]);
    if (self == NULL) {
      continue;
    }

    const int self_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    if (self_line < 0) {
      continue;
    }

    const float self_center_x =
        batch->state.pos_x[idx] + self->pushbox_x * (float)batch->state.facing_dir1[idx];
    for (int q = 0; q < num_players; q++) {
      if (q == p) {
        continue;
      }
      const size_t oidx = msl_idx_player(bi, q);
      if (batch->state.stocks[oidx] == 0u || batch->state.on_ground[oidx] == 0u) {
        continue;
      }

      const MslCharParams* other = msl_char_params(batch->state.char_id[oidx]);
      if (other == NULL) {
        continue;
      }

      const int other_line =
          stage_collision_floor_line_index(stage_id, batch->state.ground_id[oidx]);
      if (!physics_floor_lines_adjacent_or_equal(floor_graph, self_line, other_line)) {
        continue;
      }

      const float other_center_x =
          batch->state.pos_x[oidx] + other->pushbox_x * (float)batch->state.facing_dir1[oidx];
      const float delta_x = self_center_x - other_center_x;
      if (msl_absf(delta_x) >= self->pushbox_y + other->pushbox_y) {
        continue;
      }

      if (delta_x < 0.0f) {
        out_nudge_x[p] -= player_nudge_x_step;
      } else if (delta_x > 0.0f) {
        out_nudge_x[p] += player_nudge_x_step;
      } else if (q < p) {
        out_nudge_x[p] -= player_nudge_x_step;
      } else {
        out_nudge_x[p] += player_nudge_x_step;
      }
    }
  }
}

void physics_apply_attackdash_downbound_overlap_nudge_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Grounded fighter-overlap nudge constant (`p_ftCommonData->x450`).
  // Decomp:
  // - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate.
  // - ftCommon_8007DD7C accumulates +/-x450 on grounded fighter overlap.
  // - Fighter_procUpdate later applies xF8_playerNudgeVel to position.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  // refs/melee/src/melee/ft/types.h::ftCommonData (+0x450)
  //
  // Seed-bridge note:
  // - Teacher-forced knockdown rows can reseed with `on_ground=0` or pre-transition knockdown
  //   action ids even when current-frame floor collision and DownBound->Down* post-collision
  //   action resolution are visible in post-frame output.
  // - Apply this narrow AttackDash<->grounded-knockdown subset after stage collision and
  //   knockdown post-collision resolution so it can observe the current-frame grounded owner
  //   before pre-combat hurtbox/contact refresh.
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const float player_nudge_x_step = c->player_nudge_x;

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
    const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
    if (floor_graph == NULL) {
      continue;
    }

    float nudge_x[MSL_MAX_PLAYERS] = {0.0f};
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u || batch->state.on_ground[idx] == 0u ||
          batch->state.hitlag_started_frame[idx] != 0u) {
        continue;
      }

      const int self_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      if (self_line < 0) {
        continue;
      }

      const MslCharParams* self = msl_char_params(batch->state.char_id[idx]);
      if (self == NULL) {
        continue;
      }

      const float self_center_x =
          batch->state.prev_pos_x[idx] + self->pushbox_x * (float)batch->state.facing_dir1[idx];
      for (int q = 0; q < num_players; q++) {
        if (q == p) {
          continue;
        }

        const size_t oidx = msl_idx_player(bi, q);
        if (batch->state.stocks[oidx] == 0u || batch->state.on_ground[oidx] == 0u ||
            batch->state.hitlag_started_frame[oidx] != 0u) {
          continue;
        }
        if (!physics_action_is_attackdash_knockdown_overlap_owner(batch->state.action_id[idx],
                                                                  batch->state.action_id[oidx]) &&
            !physics_action_is_attackdash_knockdown_overlap_owner(batch->state.action_id[oidx],
                                                                  batch->state.action_id[idx])) {
          continue;
        }

        const int other_line =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[oidx]);
        if (!physics_floor_lines_adjacent_or_equal(floor_graph, self_line, other_line)) {
          continue;
        }

        const MslCharParams* other = msl_char_params(batch->state.char_id[oidx]);
        if (other == NULL) {
          continue;
        }

        const float other_center_x = batch->state.prev_pos_x[oidx] +
                                     other->pushbox_x * (float)batch->state.facing_dir1[oidx];
        const float delta_x = self_center_x - other_center_x;
        if (msl_absf(delta_x) >= self->pushbox_y + other->pushbox_y) {
          continue;
        }

        if (delta_x < 0.0f) {
          nudge_x[p] -= player_nudge_x_step;
        } else if (delta_x > 0.0f) {
          nudge_x[p] += player_nudge_x_step;
        } else if (q < p) {
          nudge_x[p] -= player_nudge_x_step;
        } else {
          nudge_x[p] += player_nudge_x_step;
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      if (nudge_x[p] == 0.0f) {
        continue;
      }
      const size_t idx = msl_idx_player(bi, p);
      batch->state.pos_x[idx] += nudge_x[p];
    }
  }
}

static inline uint8_t physics_is_match_flow_airborne(uint16_t action_id) {
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
    case MSL_ACT_REBIRTH:
    case MSL_ACT_REBIRTH_WAIT:
    case MSL_ACT_ENTRY:
    case MSL_ACT_ENTRY_START:
    case MSL_ACT_ENTRY_END:
    // Cliff / ledge actions are updated via dedicated callbacks in-engine and should not receive
    // generic gravity/fastfall updates in this simplified core.
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_SLOW:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_SLOW:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_SLOW1:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t ftCommon_CheckFallFast(const MslCommonParams* c, float stick_y, float vy,
                                             uint8_t* io_fall_fast,
                                             uint8_t* io_x671_timer_lstick_tilt_y) {
  // refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)
  if (c == NULL || io_fall_fast == NULL || io_x671_timer_lstick_tilt_y == NULL) {
    return 0;
  }
  if (*io_fall_fast) {
    return 0;
  }
  if (!(vy < 0.0f)) {
    return 0;
  }
  if (!(stick_y <= -c->fastfall_stick_threshold)) {
    return 0;
  }
  if (!(*io_x671_timer_lstick_tilt_y < c->fastfall_tilt_max_frames)) {
    return 0;
  }

  *io_fall_fast = 1;
  *io_x671_timer_lstick_tilt_y = 0xFEu;
  return 1;
}

static inline void physics_apply_combo_push_timer(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  if (batch == NULL || c == NULL || batch->state.combo_push_timer_x2092[idx] == 0u) {
    return;
  }

  uint16_t timer = batch->state.combo_push_timer_x2092[idx];
  if (batch->state.stocks[idx] != 0u && batch->state.on_ground[idx] != 0u &&
      batch->state.attached_victim_port[idx] == 0xFFu) {
    const float speed =
        (batch->state.combo_count[idx] < (uint8_t)c->combo_push_stronger_count_threshold)
            ? c->combo_push_low_speed
            : c->combo_push_high_speed;
    const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    const float nx = batch->state.ground_normal_x[idx];
    const float ny =
        (batch->state.ground_normal_y[idx] != 0.0f) ? batch->state.ground_normal_y[idx] : 1.0f;

    // Decomp: ftColl_80076528 decrements fp->x2092 and, while grounded with no victim_gobj,
    // applies comboCount_Push:
    //   cur_pos.x -= floor.normal.y * facing_dir * x4D0/x4D4
    //   cur_pos.y += floor.normal.x * facing_dir * x4D0/x4D4
    // The timer is armed by ftColl_800763C0 when repeated same-attack combo_count reaches x4C4.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_80076528}
    batch->state.pos_x[idx] -= ny * facing_dir * speed;
    batch->state.pos_y[idx] += nx * facing_dir * speed;
  }

  timer--;
  batch->state.combo_push_timer_x2092[idx] = timer;
}

void physics_integrate(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    float grounded_player_nudge_x[MSL_MAX_PLAYERS] = {0.0f};
    float grounded_player_nudge_z[MSL_MAX_PLAYERS] = {0.0f};
    float guardsetoff_turnover_nudge_x[MSL_MAX_PLAYERS] = {0.0f};
    physics_compute_grounded_player_nudge(batch, bi, grounded_player_nudge_x,
                                          grounded_player_nudge_z);
    physics_compute_guardsetoff_turnover_player_nudge(batch, bi, guardsetoff_turnover_nudge_x);
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];
      const uint8_t is_damage_fly = physics_action_is_damage_fly(action_id);
      const uint8_t preserve_hitlag_exit_prev_pos =
          (uint8_t)(is_damage_fly && batch->state.hitlag_pre_timer[idx] != 0u &&
                    batch->state.hitlag[idx] == 0u);

      // Record pre-integration position for physics/collision rollback helpers.
      // Floor sweeps that need the frame-start CollData.prev_pos use floor_sweep_prev_pos_*.
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
      //
      // DamageFly hitlag-exit frames are the exception: ftCo_Damage_OnExitHitlag can move cur_pos
      // before Phys/Coll, while ft_80081DD4 still consumes the pre-callback CollData.prev_pos
      // cached before Fighter_8006A1BC's hitlag-exit callbacks. step.c seeds prev_pos_* for that
      // frame; do not overwrite it here with the post-ASDI position.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      if (!preserve_hitlag_exit_prev_pos) {
        batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
        batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];
      }
      batch->state.prev_on_ground[idx] = batch->state.on_ground[idx] ? 1 : 0;

      const float player_nudge_x = grounded_player_nudge_x[p] + guardsetoff_turnover_nudge_x[p];
      if (player_nudge_x != 0.0f) {
        batch->state.pos_x[idx] += player_nudge_x;
      }
      if (grounded_player_nudge_z[p] != 0.0f) {
        batch->state.pos_z[idx] += grounded_player_nudge_z[p];
      }

      // Hitlag freezes motion/physics advancement:
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate runs its main integration block only
      //   under `if (!fp->x2219_b5)`.
      if (batch->state.hitlag_started_frame[idx] != 0) {
        physics_apply_combo_push_timer(batch, c, idx);
        continue;
      }

      // Ledge grab cooldown timer (x2064_ledgeCooldown) decrements only when not in hitlag.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      if (batch->state.ledge_cooldown[idx] != 0) {
        batch->state.ledge_cooldown[idx]--;
      }

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const float vy_self_pre = batch->state.speed_y_self[idx];
      const int16_t action_frame = batch->state.action_frame[idx];
      const uint8_t is_passivewall = physics_action_is_passivewall(action_id);
      const uint8_t is_common_damage = physics_action_is_common_damage(action_id);
      const uint8_t damage_iasa_lockout = (is_damage_fly || is_common_damage)
                                              ? physics_damage_iasa_lockout_x221c_b6(batch, idx)
                                              : 0u;
      const uint8_t damage_uses_common_air_helper =
          ((is_damage_fly || is_common_damage) && !damage_iasa_lockout) ? 1u : 0u;
      const uint8_t shield_break_fly_uses_air_friction =
          (action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_FLY) ? 1u : 0u;

      // Thrown victims:
      // - Decomp thrown victim Phys/Coll callbacks are empty, and victim translation is driven by an
      //   accessory callback (ftCo_Thrown.c::ftCo_800DE508), not by self/KB integration.
      //
      // Keep physics deterministic by skipping self/KB position integration here; grab_attachment
      // will drive `pos_*` later in the frame.
      if (msl_action_is_thrown_victim(action_id)) {
        continue;
      }

      // CapturePulled*/CaptureWait*/CaptureDamage* victims:
      // - Decomp capture pulled/wait/damage victim Phys applies a per-frame translation delta
      //   (ftCo_Attack100.c::fn_800DAD18) and does not use self/KB integration as the driver.
      // - In this simulator, that delta is applied in grab_attachment_update_pre_collision() in a
      //   "motion-state Phys" slot before stage collision. Skip integration here to avoid double
      //   movement (integrate + delta) on those frames.
      if (msl_action_is_capture_pulled_wait_damage_victim(action_id)) {
        continue;
      }

      if (physics_is_pending_throw_release_victim(batch, bi, p)) {
        // Shared throw-release placeholder:
        // - ftCo_800DD724 consumes release and immediately calls ftCo_800DDDE4/ftCo_800DE7C0 in
        //   the thrower's Anim callback; the victim does not get an intervening generic Fall Phys
        //   drift frame before Damage* entry.
        // - This simulator temporarily marks the detached victim as Fall so same-frame item
        //   ordering can preempt the deferred throw hit. Keep that placeholder non-physical until
        //   throw_flow_update_post_items() applies the release damage/KB owner.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
        continue;
      }

      // ----------------------------
      // Air-only self-velocity update
      // ----------------------------
      //
      // Decomp ordering:
      // - Motion-state phys callbacks run inside `Fighter_procUpdate` before position integration.
      //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      // - Many airborne states call the common fall helper `ft_80084DB0`, which runs:
      //   (1) ftCommon_CheckFallFast, (2) ftCommon_Fall/FallFast (mutates fp->self_vel.y),
      //   (3) ftCommon_8007D268 (air drift accel via fp->x74_anim_vel.x).
      //   refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
      //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
      //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall / ftCommon_FallFast
      //
      // In GALE01, `fp->cur_pos` is then integrated using the updated self velocity in the same
      // proc. We therefore apply gravity/fastfall (and simplified EscapeAir decay) before
      // integrating `pos_*` so our one-step outputs are aligned with the in-engine ordering.
      if (!on_ground) {
        // Match-flow and cliff actions are treated as non-physical in this simplified core.
        if (!physics_is_match_flow_airborne(action_id)) {
          if (!physics_action_skip_common_air_helper_first_frame(
                  action_id, batch->state.prev_action_id[idx], action_frame)) {
            if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
              // Decomp: EscapeAir_Phys scales `self_vel` by `escapeair_decay` while
              // cmd_vars[0] (`cmd_skip_decay`) is clear; once the action script sets cmd_vars[0],
              // EscapeAir_Phys hands motion ownership to `ft_80084DB0`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
              // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
              //
              // Source of truth:
              // - data/moves/{fox,falco}.json moves["ftCo_SM_EscapeAir"]["events"] set_cmd_var(idx=0)
              if (!move_tables_escapeair_cmd0_active(batch->state.char_id[idx],
                                                     batch->state.anim_frame_f32[idx])) {
                batch->state.speed_air_x_self[idx] *= c->escapeair_decay;
                batch->state.speed_y_self[idx] *= c->escapeair_decay;
              } else {
                const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
                if (phys != NULL) {
                  const float stick_x = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
                  const float stick_y = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                  const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);
                  if (allow_fastfall) {
                    ftCommon_CheckFallFast(c, stick_y, vy_self_pre, &batch->state.fall_fast[idx],
                                           &batch->state.tilt_timer_y[idx]);
                  }
                  if (allow_fastfall && batch->state.fall_fast[idx]) {
                    batch->state.speed_y_self[idx] = -phys->fast_fall_velocity;
                  } else {
                    float next_vy = vy_self_pre - phys->grav;
                    if (next_vy < -phys->terminal_vel) {
                      next_vy = -phys->terminal_vel;
                    }
                    batch->state.speed_y_self[idx] = next_vy;
                  }
                  batch->state.speed_air_x_self[idx] = physics_apply_common_air_drift(
                      phys, c, action_id, batch->state.fallspecial_xc[idx], stick_x,
                      batch->state.speed_air_x_self[idx]);
                }
              }
            } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR) {
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                physics_apply_specialhi_hold_air(phys, action_frame,
                                                 &batch->state.speed_air_x_self[idx],
                                                 &batch->state.speed_y_self[idx]);
              }
            } else if (is_passivewall && batch->state.passivewall_timer[idx] == 0u) {
              // PassiveWall steady Phys:
              // - once timer reaches 0, PassiveWall_Anim has already written the launch velocity
              //   for PassiveWallJump (or horizontal push for PassiveWall), and the same step then
              //   runs PassiveWall_Phys: fastfall/fall + pure aerial friction (target_vel=0).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Phys
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Anim
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                const float stick_y = apply_deadzone(
                    stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre, &batch->state.fall_fast[idx],
                                             &batch->state.tilt_timer_y[idx]);
                if (batch->state.fall_fast[idx]) {
                  batch->state.speed_y_self[idx] = -phys->fast_fall_velocity;
                } else {
                  float next_vy = vy_self_pre - phys->grav;
                  if (next_vy < -phys->terminal_vel) {
                    next_vy = -phys->terminal_vel;
                  }
                  batch->state.speed_y_self[idx] = next_vy;
                }
                batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                    batch->state.speed_air_x_self[idx], phys->aerial_friction);
              }
            } else if (physics_shine_air_applies_fall_this_frame(
                           msl_char_params(batch->state.char_id[idx]), action_id,
                           batch->state.prev_action_id[idx], action_frame)) {
              // Aerial Reflector Phys: after the reflector gravityDelay expires, the callback uses
              // ftCommon_Fall with ftFox_DatAttrs.xAC rather than common character gravity.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
              //   ftFx_SpecialAirLwStart_Phys,ftFx_SpecialAirLwLoop_Phys,
              //   ftFx_SpecialAirLwHit_Phys,ftFx_SpecialAirLwTurn_Phys,
              //   ftFx_SpecialAirLwEnd_Phys}
              physics_apply_shine_air_fall(msl_char_params(batch->state.char_id[idx]),
                                           &batch->state.speed_y_self[idx]);
            } else if (damage_iasa_lockout || shield_break_fly_uses_air_friction) {
              // DamageFly/DamageFlyRoll/common Damage x221C_b6 path (`ft_80084EEC`): apply
              // gravity + terminal clamp and aerial friction (no fastfall latch, no drift accel
              // from stick).
              // ShieldBreakFly uses the same `ft_80084EEC` Phys callback, so the entry frame's
              // initial shield-break y velocity is gravity-adjusted before integration.
              //
              // Decomp scope:
              // - ftCo_Damage_Phys branches to ft_80084EEC while airborne and x221C_b6 is set,
              //   regardless of whether the common damage motion is DamageAir* or DamageHi/N/Lw*.
              // - ftCo_DamageFly_Phys and ftCo_DamageFlyRoll_Phys branch to ft_80084EEC when
              //   x221C_b6 is set.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
              //   ftCo_Damage_Phys,ftCo_DamageFly_Phys,ftCo_DamageFlyRoll_Phys
              // }
              // refs/melee/src/melee/ft/ft_081B.c::ft_80084EEC
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_ShieldBreakFly_Phys
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                float next_vy = vy_self_pre - phys->grav;
                if (next_vy < -phys->terminal_vel) {
                  next_vy = -phys->terminal_vel;
                }
                batch->state.speed_y_self[idx] = next_vy;
                if (shield_break_fly_uses_air_friction) {
                  batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                      batch->state.speed_air_x_self[idx], phys->aerial_friction);
                }
              }
            } else if (physics_action_use_pre_integration_common_air_gravity(action_id) ||
                       physics_damagefall_entry_uses_pre_integration_phys(
                           action_id, batch->state.prev_action_id[idx], action_frame) ||
                       damage_uses_common_air_helper) {
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                const uint8_t allow_fastfall =
                    (msl_action_allows_fastfall(action_id) || damage_uses_common_air_helper) ? 1u
                                                                                             : 0u;

                // Fastfall latch (ftCommon_CheckFallFast) uses the pre-gravity `self_vel.y`.
                // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
                if (allow_fastfall) {
                  const float stick_y = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                  (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre,
                                               &batch->state.fall_fast[idx],
                                               &batch->state.tilt_timer_y[idx]);
                }

                // Air gravity / terminal velocity / fastfall update.
                //
                // Decomp refs:
                // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
                // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
                float next_vy = vy_self_pre;
                if (allow_fastfall && batch->state.fall_fast[idx]) {
                  next_vy = -phys->fast_fall_velocity;
                } else {
                  next_vy -= phys->grav;
                  if (next_vy < -phys->terminal_vel) {
                    next_vy = -phys->terminal_vel;
                  }
                }
                batch->state.speed_y_self[idx] = next_vy;
              }
            }

            // ----------------------
            // Air horizontal drift/X
            // ----------------------
            //
            // Decomp:
            // - Common airborne helper (`ft_80084DB0`) calls `ftCommon_8007D268`, which computes an
            //   accel into fp->x74_anim_vel.x (ftCommon_8007D174) and then Fighter_procUpdate adds
            //   x74_anim_vel into self_vel before integrating position.
            //   refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
            //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D268 / ftCommon_8007D28C
            //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (self_vel += x74_anim_vel)
            //
            // - Fox/Falco Shine aerial states are state-specific: `ftCommon_8007CF58` (not drift
            //   from stick), still via x74_anim_vel.x.
            //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_Phys
            const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
            if (ch != NULL) {
              const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                                   c->lstick_deadzone_x);
              if (physics_action_is_shine_air(action_id)) {
                batch->state.speed_air_x_self[idx] = physics_apply_ftcommon_8007cf58_x_clamp(
                    ch, c, batch->state.speed_air_x_self[idx]);
              } else if (damage_iasa_lockout) {
                // DamageFly/DamageFlyRoll/common Damage x221C_b6 path (`ft_80084EEC`) uses
                // friction-only x update.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
                //   ftCo_Damage_Phys,ftCo_DamageFly_Phys,ftCo_DamageFlyRoll_Phys
                // }
                // refs/melee/src/melee/ft/ft_081B.c::ft_80084EEC
                batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                    batch->state.speed_air_x_self[idx], ch->aerial_friction);
              } else if (physics_action_uses_common_air_drift(action_id) ||
                         damage_uses_common_air_helper) {
                batch->state.speed_air_x_self[idx] = physics_apply_common_air_drift(
                    ch, c, action_id, batch->state.fallspecial_xc[idx], stick_x,
                    batch->state.speed_air_x_self[idx]);
              }
            }
          }
        }

        // Aerial Side-B start uses its own friction + delayed-gravity owner, not the generic
        // common airborne helper.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Phys
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_Fall,ftCommon_ApplyFrictionAir}
        // data/characters/{fox,falco}.json::{
        //   illusion_gravity_delay_start_frames,illusion_air_friction_start,
        //   illusion_fall_accel_start,terminal_vel
        // }
        if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                batch->state.speed_air_x_self[idx], ch->illusion_air_friction_start);
            if (batch->state.action_frame[idx] > (int16_t)ch->illusion_gravity_delay_start_frames) {
              float next_vy = batch->state.speed_y_self[idx] - ch->illusion_fall_accel_start;
              if (next_vy < -ch->terminal_vel) {
                next_vy = -ch->terminal_vel;
              }
              batch->state.speed_y_self[idx] = next_vy;
            }
          }
        }

        // Root-motion aerial side-B (Illusion/Phantasm) uses TransN-derived self velocity.
        // Decomp: ftFx_SpecialAirS_Phys calls `ft_80085134`, which sets fp->self_vel from
        // fp->x6A4_transNOffset.{y,z} (with facing_dir applied to z).
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Phys
        // refs/melee/src/melee/ft/ft_081B.c::ft_80085134
        if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S) {
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            float dxyz[3];
            if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                     batch->state.animation_index[idx]) &&
                physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
              batch->state.speed_air_x_self[idx] = dxyz[2] * facing_dir;
              batch->state.speed_y_self[idx] = dxyz[1];
            }
          }
        } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
          // Decomp: ftFx_SpecialAirSEnd_Phys applies air friction using ftFox_DatAttrs.x40 and
          // applies gravity after mv.fx.SpecialS.gravityDelay expires (x44 gate, x48 accel).
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Phys
          // refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs
          // data/characters/{fox,falco}.json::{
          //   illusion_air_friction,illusion_gravity_delay_end_frames,illusion_fall_accel_end,terminal_vel
          // }
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                batch->state.speed_air_x_self[idx], ch->illusion_air_friction);
            // Mapping note:
            // - decomp uses `if (gravityDelay != 0) --gravityDelay; else Fall(...)`.
            // - this core does not carry mv.fx.SpecialS.gravityDelay in state, so gate by
            //   action_frame age to preserve the same countdown boundary.
            if (batch->state.action_frame[idx] >= (int16_t)ch->illusion_gravity_delay_end_frames) {
              float next_vy = batch->state.speed_y_self[idx] - ch->illusion_fall_accel_end;
              if (next_vy < -ch->terminal_vel) {
                next_vy = -ch->terminal_vel;
              }
              batch->state.speed_y_self[idx] = next_vy;
            }
          }
        } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) {
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            physics_apply_specialhi_air_reverse_accel(
                batch, idx, ch, batch->state.action_frame[idx], &batch->state.speed_air_x_self[idx],
                &batch->state.speed_y_self[idx]);
          }
        } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_BOUND &&
                   batch->state.on_ground[idx] == 0) {
          // Decomp: airborne Firefox/Firebird rebound Phys runs `ft_800851C0` (vertical
          // self-velocity from `fp->x6A4_transNOffset.y`) and then `ftCommon_8007CF58`
          // (horizontal air friction/clamp via x74_anim_vel.x). This keeps rebound rows
          // root-motion-owned while airborne instead of continuing with the pre-bound downward
          // launch velocity.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiBound_Phys
          // refs/melee/src/melee/ft/ft_081B.c::ft_800851C0
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              batch->state.speed_y_self[idx] = dxyz[1];
            }
            batch->state.speed_air_x_self[idx] =
                physics_apply_ftcommon_8007cf58_x_clamp(ch, c, batch->state.speed_air_x_self[idx]);
          }
        }
      }

      // Knockback velocity contributes to position integration in addition to self velocity.
      //
      // Decomp: position integrates both self velocity and knockback velocity.
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate:
      //   `p_kb_vel = &fp->x8c_kb_vel;`
      //   `PSVECAdd(&fp->cur_pos, &selfVel, &fp->cur_pos); fp->cur_pos.x += p_kb_vel->x; fp->cur_pos.y += p_kb_vel->y;`
      //
      // Seed mapping: our `speed_{x,y}_attack` are Slippi post-frame `velocities.knockback_{x,y}`
      // (i.e. the decomp `fp->x8c_kb_vel.{x,y}` knockback velocity term), not a "self velocity".
      // - tools/slippi/make_dataset_from_slp.py (velocities.knockback_{x,y} → speed_{x,y}_attack)
      //
      // We include `speed_*_attack` in position integration (because it affects where the character is this frame),
      // but we exclude it from gravity/fastfall updates (which operate on `self_vel` only; knockback has its own
      // separate decay/physics paths in-engine, and is currently teacher-forced from the seed).
      float vx_self =
          on_ground ? batch->state.speed_ground_x_self[idx] : batch->state.speed_air_x_self[idx];
      if (on_ground) {
        // Grounded locomotion velocity update (single writer):
        // - Decomp: Phys callbacks like ft_80084F3C/ftWalkCommon_800E0060/ftCo_Dash_Phys/ftCo_Run_Phys
        //   compute a ground accel/friction step via ftCommon_ApplyFrictionGround or ftCommon_8007C98C.
        // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
        // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800E0060
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Phys
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Phys
        const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
        if (ch != NULL) {
          float gr_vel = batch->state.speed_ground_x_self[idx];
          float grounded_self_vel_for_frame = gr_vel;
          uint8_t use_grounded_self_vel_for_frame = 0u;
          const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                               c->lstick_deadzone_x);
          const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          const uint16_t prev_action_id = batch->state.prev_action_id[idx];

          // Root-motion grounded side-B (Illusion/Phantasm) uses a TransN-derived ground velocity.
          // Decomp: ftFx_SpecialS_Phys calls `ft_80085088` → `ft_800850E0`, which sets fp->gr_vel
          // from fp->x6A4_transNOffset.z * facing_dir when TransN motion is active.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_Phys
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80085088,ft_800850E0}
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S) {
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              gr_vel = dxyz[2] * facing_dir;
            }
          } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S_END) {
            // Decomp: ftFx_SpecialSEnd_Phys applies ground friction using ftFox_DatAttrs.x38.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Phys
            gr_vel += ground_friction_step_delta(gr_vel, ch->illusion_ground_friction);
          } else if (action_id == (uint16_t)MSL_ACT_ESCAPE_F ||
                     action_id == (uint16_t)MSL_ACT_ESCAPE_B) {
            // Roll (EscapeF/EscapeB): decomp-shaped ground phys helper chain.
            // Decomp: ftCo_Escape_Phys -> ft_80085004 -> ft_80085030.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Phys
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
            //
            // Root motion signal:
            // - Decomp ft_80085030 root-motion branch is gated by `if (fp->x594_b0)`, sourced
            //   from the per-msid ftData_80085FD4_ret.x10_b0 byte in data/anims/*.tracks.bin.
            //
            // Direction:
            // - Decomp uses fp->facing_dir1, but it is normally a copy of facing_dir (engine init
            //   path sets facing_dir1 = facing_dir).
            //   refs/melee/src/melee/ft/fighter.c:264 and :955 (fp->facing_dir1 = fp->facing_dir)
            // - We use batch->state.facing-derived `facing_dir`.
            float dxyz[3];
            if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                     batch->state.animation_index[idx]) &&
                physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              float facing_dir_roll = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
              // Motion-state entry ownership:
              // - Fighter_ChangeMotionState copies facing_dir -> facing_dir1.
              // - If Escape* was entered this frame, match that reset by using current facing.
              // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
              if (action_id != prev_action_id) {
                facing_dir_roll = facing_dir;
              }
              // Decomp: ft_80085030 "drive to TransN vel" via xE4 = transN_vel - gr_vel.
              // Setting gr_vel directly is equivalent after applying the accel step.
              // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
              gr_vel = dxyz[2] * facing_dir_roll;
            } else {
              // Decomp: ft_80085030 applies ftCommon_ApplyFrictionGround when not root-motion.
              // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround
              gr_vel += ground_friction_step_delta(gr_vel, ch->gr_friction);
            }
          } else if (action_id == (uint16_t)MSL_ACT_ESCAPE_N) {
            // Spotdodge (EscapeN): decomp ground friction helper.
            // Decomp: ftCo_EscapeN_Phys -> ft_80084F3C (high-speed friction mul).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Phys
            // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
            float friction = ch->gr_friction;
            if (msl_absf(gr_vel) > ch->walk_max_vel) {
              friction *= c->high_speed_friction_mul;
            }
            gr_vel += ground_friction_step_delta(gr_vel, friction);
          } else if (action_id == (uint16_t)MSL_ACT_ATTACK_DASH) {
            // Decomp: ftCo_AttackDash_Phys calls ft_80085030 with
            // p_ftCommonData->x50 * co_attrs.gr_friction and current facing_dir.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Phys
            // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
            float dxyz[3];
            if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                     batch->state.animation_index[idx]) &&
                physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              gr_vel = dxyz[2] * facing_dir;
            } else {
              gr_vel +=
                  ground_friction_step_delta(gr_vel, c->attackdash_friction_mul * ch->gr_friction);
            }
          } else if (action_id == (uint16_t)MSL_ACT_CATCH_DASH) {
            // Decomp: ftCo_CatchDash_Phys calls ft_80085030 with p_ftCommonData->x64
            // * co_attrs.gr_friction. ISO motion-state data for ftCo_SM_CatchDash has
            // x10_animCurrFlags bit0 clear, so ft_80085030 takes its friction fallback instead of
            // the TransN root-motion branch (`fp->x594_b0 == false`).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Phys
            // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
            // data/anims/{fox,falco}.tracks.bin ftCo_SM_CatchDash / Pl{Fx,Fc}.dat msid flag 0x00
            gr_vel += ground_friction_step_delta(gr_vel, c->catch_friction_mul * ch->gr_friction);
          } else if (action_id == (uint16_t)MSL_ACT_CATCH ||
                     action_id == (uint16_t)MSL_ACT_CATCH_PULL ||
                     action_id == (uint16_t)MSL_ACT_CATCH_WAIT ||
                     action_id == (uint16_t)MSL_ACT_CATCH_ATTACK ||
                     action_id == (uint16_t)MSL_ACT_CATCH_CUT) {
            // Decomp: grounded Catch/CatchPull/CatchWait/CatchAttack/CatchCut all use
            // ftCo_Catch_Phys, which applies p_ftCommonData->x64 * co_attrs.gr_friction before
            // ftCommon_ApplyGroundMovement.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
            //   ftCo_Catch_Phys,ftCo_CatchPull_Phys,ftCo_CatchWait_Phys,ftCo_CatchAttack_Phys,
            //   ftCo_CatchCut_Phys
            // }
            gr_vel += ground_friction_step_delta(gr_vel, c->catch_friction_mul * ch->gr_friction);
          } else if (msl_action_is_throw_owner(action_id)) {
            // Grounded ThrowF/B/Hi/Lw Phys all use ft_80085004 -> ft_80085030. The root-motion
            // branch is gated by the extracted ftData x10_b0 root-motion bit for the current
            // throw submotion; otherwise ft_80085030 falls back to ground friction.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
            //   ftCo_ThrowF_Phys,ftCo_ThrowB_Phys,ftCo_ThrowHi_Phys,ftCo_ThrowLw_Phys}
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
            // data/anims/{fox,falco}.tracks.bin SSANIMT1 root-motion flag
            float dxyz[3];
            if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                     batch->state.animation_index[idx]) &&
                physics_try_get_transn_delta_xyz_f32(
                    ch, batch->state.char_id[idx], batch->state.animation_index[idx],
                    physics_prev_anim_frame_f32(batch, idx), physics_cur_anim_frame_f32(batch, idx),
                    dxyz)) {
              float throw_facing_dir = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
              if (action_id != prev_action_id) {
                // Fighter_ChangeMotionState copies current facing into facing_dir1 on entry.
                // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
                throw_facing_dir = facing_dir;
              }
              gr_vel = dxyz[2] * throw_facing_dir;
            } else {
              gr_vel += ground_friction_step_delta(gr_vel, ch->gr_friction);
            }
          } else if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI) {
            // Decomp: ftFx_SpecialHi_Phys increments `mv.fx.SpecialHi.unk`, then applies ground
            // reverse friction x78 once `unk >= x70`.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHi_Phys
            if ((int16_t)(batch->state.action_frame[idx] + 1) >=
                (int16_t)ch->firefox_launch_reverse_accel_start_frames) {
              gr_vel += ground_friction_step_delta(gr_vel, ch->firefox_launch_reverse_accel);
            }
          } else if (physics_action_uses_ft_80084FA8(action_id)) {
            // ft_80084FA8 grounded Phys family:
            // - high-speed friction scale gate (walk_max_vel, p_ftCommonData->x6C)
            // - ft_80085030 root-motion branch only when `fp->x594_b0` is set for this motion,
            //   otherwise friction fallback even if the FigaTree has TransN tracks.
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80085030}
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
            float friction = ch->gr_friction;
            if (msl_absf(gr_vel) > ch->walk_max_vel) {
              friction *= c->high_speed_friction_mul;
            }
            float dxyz[3];
            if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                     batch->state.animation_index[idx]) &&
                physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              // Decomp root-motion gate:
              // - ft_80085030 takes the transN drive branch only when fp->x594_b0 is set.
              // - `msl_anim_uses_root_motion` is the extracted ftData x10_b0 flag for the current
              //   motion; do not infer the branch from TransN track presence alone.
              // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
              gr_vel = dxyz[2] * facing_dir;
            } else {
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            }
          } else if (physics_action_is_common_ground_friction_only(action_id)) {
            if (action_id == (uint16_t)MSL_ACT_REBOUND &&
                batch->state.rebound_ground_accel_2[idx] != 0.0f) {
              // Rebound's first Phys frame skips ft_80084F3C while mv.co.rebound.x0 is still live.
              // The queued xE8_ground_accel_2 from ftCommon_800804A0 applies to post-frame gr_vel
              // after movement for this frame has used the old ground speed.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Phys
              // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
              grounded_self_vel_for_frame = gr_vel;
              use_grounded_self_vel_for_frame = 1u;
              gr_vel += batch->state.rebound_ground_accel_2[idx];
              batch->state.rebound_ground_accel_2[idx] = 0.0f;
            } else {
              float friction = ch->gr_friction;
              if (msl_absf(gr_vel) > ch->walk_max_vel) {
                friction *= c->high_speed_friction_mul;
              }
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            }
          } else if (physics_action_is_walk(action_id)) {
            const float accel_mul = 1.0f;
            float walk_stick_x = stick_x;
            if (batch->state.walk_use_raw_input_once[idx]) {
              // Narrow raw-stick owner:
              // - ftWalkCommon_800E0060 uses raw fp->input.lstick.x.
              // - keep that scope to Wait_IASA rows that only admitted Walk on the raw-only lane,
              //   instead of widening every Walk physics tick.
              // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800E0060
              walk_stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
              batch->state.walk_use_raw_input_once[idx] = 0u;
            }
            float accel = walk_stick_x * ch->walk_init_vel * accel_mul;
            accel += (walk_stick_x > 0.0f ? +ch->walk_accel : -ch->walk_accel) * accel_mul;
            const float target = walk_stick_x * ch->walk_max_vel * accel_mul;
            if (target != 0.0f) {
              const float mult = gr_vel / target;
              if (mult > 0.0f && mult < 1.0f) {
                accel *= (1.0f - mult) * c->walk_accel_scale_mul;
              }
            }
            gr_vel += ground_accel_step_delta(gr_vel, accel, target, ch->gr_friction,
                                              ch->ground_max_horizontal_velocity);
          } else if (action_id == (uint16_t)MSL_ACT_DASH) {
            // Decomp Dash entry/Phys ownership:
            // - ftCo_Dash_Enter computes mv.co.dash.x0 and writes it through
            //   ftCommon_800804A0 (xE8_ground_accel_2 lane).
            // - Fighter_procUpdate then applies gr_vel += xE4 + xE8 before position integration.
            // - ftCo_Dash_Phys first frame consumes mv.co.dash.x0 without calling
            //   ftCommon_8007C98C accel.
            // - ftCo_Dash_Phys still calls ftCommon_ApplyGroundMovement before Fighter_procUpdate
            //   applies xE8_ground_accel_2, so the entry frame's self_vel/position use the old
            //   ground speed while the post-frame gr_vel reports the new dash speed.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Enter,ftCo_Dash_Phys}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
            // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
            if (prev_action_id != (uint16_t)MSL_ACT_DASH) {
              grounded_self_vel_for_frame = gr_vel;
              use_grounded_self_vel_for_frame = 1u;
              const float init_vel = facing_dir * ch->dash_initial_velocity;
              if ((gr_vel * facing_dir) < 0.0f) {
                // ftCo_Dash_Enter: if existing ground speed opposes facing, x0 = init_vel and the
                // Fighter_procUpdate xE8 add behaves as gr_vel += init_vel this frame.
                gr_vel += init_vel;
              } else {
                // Otherwise x0 = init_vel - gr_vel, so post-add resolves exactly to init_vel.
                gr_vel = init_vel;
              }
            } else {
              const float accel =
                  stick_x * ch->dash_run_acceleration_a +
                  (stick_x > 0.0f ? +ch->dash_run_acceleration_b : -ch->dash_run_acceleration_b);
              const float target = stick_x * ch->dash_run_terminal_velocity;
              const float friction = ch->gr_friction * c->run_friction_mul;
              gr_vel += ground_accel_step_delta(gr_vel, accel, target, friction,
                                                ch->ground_max_horizontal_velocity);
            }
          } else if (action_id == (uint16_t)MSL_ACT_RUN ||
                     action_id == (uint16_t)MSL_ACT_RUN_DIRECT) {
            const float accel_base =
                stick_x * ch->dash_run_acceleration_a +
                (stick_x > 0.0f ? +ch->dash_run_acceleration_b : -ch->dash_run_acceleration_b);
            float accel = accel_base;
            const float target = stick_x * ch->dash_run_terminal_velocity;
            if (target != 0.0f) {
              const float gr_frac = gr_vel / target;
              if (gr_frac > 0.0f && gr_frac < 1.0f) {
                accel *= (1.0f - gr_frac) * c->run_accel_scale_mul;
              }
            }
            const float friction = ch->gr_friction * c->run_friction_mul;
            gr_vel += ground_accel_step_delta(gr_vel, accel, target, friction,
                                              ch->ground_max_horizontal_velocity);
          } else if (action_id == (uint16_t)MSL_ACT_TURN_RUN) {
            const float accel =
                stick_x * ch->dash_run_acceleration_a +
                (stick_x > 0.0f ? +ch->dash_run_acceleration_b : -ch->dash_run_acceleration_b);
            const float target = stick_x * ch->dash_run_terminal_velocity;
            const float friction = ch->gr_friction * c->run_friction_mul;
            const float turnrun_accel_mul = (batch->state.facing_dir1[idx] < 0.0f) ? -1.0f : 1.0f;
            if (target == 0.0f) {
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            } else if ((turnrun_accel_mul * accel) < 0.0f) {
              // Decomp: TurnRun_Phys calls getAccelAndTarget, then only applies accel while
              // `mv.co.turnrun.accel_mul * accel < 0`; otherwise it falls back to grounded friction.
              // On TurnRun_Enter, accel_mul is initialized from the pre-turn facing_dir and x14 is
              // cleared. It is not current facing: TurnRun_Anim can flip facing before later Phys
              // callbacks continue accelerating toward the original opposite-stick target.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::{
              //   ftCo_TurnRun_Enter,ftCo_TurnRun_Phys}
              float accel_step = accel;
              if (accel_step > 0.0f) {
                if ((gr_vel + accel_step) > target) {
                  accel_step -= friction;
                  if ((gr_vel + accel_step) < target) {
                    accel_step = target - gr_vel;
                  }
                }
              } else if (accel_step < 0.0f) {
                if ((gr_vel + accel_step) < target) {
                  accel_step += friction;
                  if ((gr_vel + accel_step) > target) {
                    accel_step = target - gr_vel;
                  }
                }
              }
              gr_vel += accel_step;
            } else {
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            }
          } else if (action_id == (uint16_t)MSL_ACT_RUN_BRAKE) {
            const float friction = ch->gr_friction * c->run_friction_mul;
            gr_vel += ground_friction_step_delta(gr_vel, friction);
          }

          batch->state.speed_ground_x_self[idx] = gr_vel;
          vx_self = use_grounded_self_vel_for_frame ? grounded_self_vel_for_frame : gr_vel;
        }
      }
      if (on_ground) {
        // Decomp: grounded movement helpers always run ftCommon_ApplyGroundMovement, which writes
        // fp->self_vel.x from fp->gr_vel each frame after applying ground accel/friction.
        // Our seed/output `speed_air_x_self` lane maps fp->self_vel.x, so keep it synced on
        // grounded frames from the resolved ground velocity used for integration.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80085030,ft_800850E0}
        batch->state.speed_air_x_self[idx] = vx_self;
        // Common grounded friction-only phys callbacks (`ft_80084F3C`) do not advance vertical self
        // velocity while the fighter remains on the floor. Teacher-forced reseed can carry a stale
        // airborne `speed_y_self` into these grounded states; clear it before grounded position
        // integration so the frame stays floor-owned on Y.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Phys
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_Phys
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Phys
        // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
        if (physics_action_is_common_ground_friction_only(action_id)) {
          batch->state.speed_y_self[idx] = 0.0f;
        }
      }
      float atk_shield_kb_x = 0.0f;
      float atk_shield_kb_y = 0.0f;
      if (on_ground) {
        // Grounded attacker-on-shield pushback owner:
        // - Shield hit processing shapes `fp->xF4_ground_attacker_shield_kb_vel` from
        //   `defender.lightshield_amount * int_dmg`.
        // - Fighter_procUpdate decays that scalar through ftCommon_8007CE4C using
        //   `ft_GetGroundFrictionMultiplier(fp) * gr_friction * x3EC`, then projects it onto the
        //   current floor tangent via `x98_atk_shield_kb`.
        // - Position integration later adds `x98_atk_shield_kb` after self/KB velocity.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007CE4C,ftCommon_8007E2A4}
        const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
        float shield_kb = batch->state.attacker_shield_ground_kb_vel[idx];
        if (ch != NULL && shield_kb != 0.0f) {
          const float friction = batch->state.ground_friction_mul[idx] * ch->gr_friction *
                                 c->shield_attacker_ground_friction_mul;
          if (shield_kb < 0.0f) {
            shield_kb += friction;
            if (shield_kb > 0.0f) {
              shield_kb = 0.0f;
            }
          } else {
            shield_kb -= friction;
            if (shield_kb < 0.0f) {
              shield_kb = 0.0f;
            }
          }
          batch->state.attacker_shield_ground_kb_vel[idx] = shield_kb;
          const float floor_nx = batch->state.ground_normal_x[idx];
          const float floor_ny = (batch->state.ground_normal_y[idx] != 0.0f)
                                     ? batch->state.ground_normal_y[idx]
                                     : 1.0f;
          atk_shield_kb_x = floor_ny * shield_kb;
          atk_shield_kb_y = -floor_nx * shield_kb;
        }
      } else {
        batch->state.attacker_shield_ground_kb_vel[idx] = 0.0f;
      }

      physics_apply_knockback_decay(batch, idx, msl_char_params(batch->state.char_id[idx]), c,
                                    on_ground);

      const float vy_self = batch->state.speed_y_self[idx];
      const float vx_kb = batch->state.speed_x_attack[idx];
      const float vy_kb = batch->state.speed_y_attack[idx];

      // Position integration uses the (possibly-updated) self velocity plus the separate knockback
      // velocity term, matching GALE01 `Fighter_procUpdate` integration shape.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      batch->state.pos_x[idx] += vx_self;
      batch->state.pos_x[idx] += vx_kb;
      batch->state.pos_y[idx] += vy_self;
      batch->state.pos_y[idx] += vy_kb;
      batch->state.pos_x[idx] += atk_shield_kb_x;
      batch->state.pos_y[idx] += atk_shield_kb_y;
      physics_apply_combo_push_timer(batch, c, idx);

      // Post-integration gravity update for states we intentionally keep "seed-driven" for current
      // frame displacement (notably DamageFall; see helper docs above).
      if (!on_ground && !physics_is_match_flow_airborne(action_id) &&
          physics_action_use_post_integration_common_air_gravity(action_id) &&
          !physics_damagefall_entry_uses_pre_integration_phys(
              action_id, batch->state.prev_action_id[idx], action_frame)) {
        const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
        if (phys != NULL) {
          const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);
          if (allow_fastfall) {
            const float stick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]),
                                                 c->lstick_deadzone_y);
            (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre, &batch->state.fall_fast[idx],
                                         &batch->state.tilt_timer_y[idx]);
          }

          float next_vy = vy_self_pre;
          if (allow_fastfall && batch->state.fall_fast[idx]) {
            next_vy = -phys->fast_fall_velocity;
          } else {
            next_vy -= phys->grav;
            if (next_vy < -phys->terminal_vel) {
              next_vy = -phys->terminal_vel;
            }
          }
          batch->state.speed_y_self[idx] = next_vy;
        }
      }
    }
  }
}
