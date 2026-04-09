#include "physics.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "move_tables.h"
#include "stage_collision.h"
#include "state_flags.h"

static inline uint8_t physics_action_skip_common_air_helper_first_frame(uint16_t action_id,
                                                                        int16_t action_frame) {
  // Decomp:
  // - ftCo_Jump_Phys_Inner skips `ft_80084DB0` on the first frame after entering JumpF/B.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Phys_Inner
  // - ftCo_CliffJump2_Phys skips the common fall helper on the first frame.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Phys
  if (action_frame > 0) {
    return 0;
  }
  switch (action_id) {
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return 1;
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

static inline void physics_apply_specialhi_air_reverse_accel(const MslCharParams* ch,
                                                             uint8_t facing, int16_t action_frame,
                                                             float* io_vel_x, float* io_vel_y) {
  if (ch == NULL || io_vel_x == NULL || io_vel_y == NULL) {
    return;
  }
  // Decomp: ftFx_SpecialAirHi_Phys increments `mv.fx.SpecialHi.unk`, then subtracts the x78
  // reverse-accel vector projected along `rotateModel` once `unk >= x70`.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Phys
  //
  // Inference from source: this core does not persist `rotateModel` separately during launch, so
  // derive the launch axis from the current self-velocity direction. Across uninterrupted launch
  // frames, the decomp reverse-accel step remains colinear with that direction.
  if ((int16_t)(action_frame + 1) < (int16_t)ch->firefox_launch_reverse_accel_start_frames) {
    return;
  }

  const float facing_dir = facing ? 1.0f : -1.0f;
  float dir_x = (*io_vel_x) * facing_dir;
  float dir_y = *io_vel_y;
  const float mag = sqrtf(dir_x * dir_x + dir_y * dir_y);
  if (!(mag > 0.0f)) {
    return;
  }
  dir_x /= mag;
  dir_y /= mag;
  *io_vel_x -= facing_dir * (ch->firefox_launch_reverse_accel * dir_x);
  *io_vel_y -= ch->firefox_launch_reverse_accel * dir_y;
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
  // - ftCo_AttackS4_Phys also calls ft_80084FA8 for all AttackS4* variants.
  // - PassiveStandF/B Phys calls ft_80084FA8.
  // - CliffClimb/Attack/Escape quick grounded Phys paths share ftCo_CliffClimb_Phys, which calls
  //   ft_80084FA8 once the option has reached the stage.
  // refs/melee/src/melee/ft/ftmotionstates.c (Attack11/12/13 entries)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
  switch (action_id) {
    case MSL_ACT_ATTACK_11:
    case MSL_ACT_ATTACK_12:
    case MSL_ACT_ATTACK_13:
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
    case MSL_ACT_PASSIVE_STAND_F:
    case MSL_ACT_PASSIVE_STAND_B:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
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
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t physics_try_get_transn_delta_xyz(const MslCharParams* ch, uint8_t char_id,
                                                       uint32_t msid_u32, float anim_frame_f32,
                                                       float out_delta_xyz[3]) {
  if (ch == NULL || out_delta_xyz == NULL) {
    return 0;
  }
  if (!(msid_u32 <= 0xFFFFu)) {
    return 0;
  }
  const uint16_t msid = (uint16_t)msid_u32;

  // Our ISO-derived SSANIM01 v3 artifacts store per-frame TransN translation as a tail (x,y,z);
  // approximate the per-frame TransN offset as a finite difference between adjacent frames.
  // - tools/extraction/extract_fighter_anims.py (SSANIM01 v3 + per-frame TransN tail)
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80085030 (consumer of fp->x6A4_transNOffset.{y,z})
  const uint16_t f_cur = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(anim_frame_f32));
  const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;

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

static inline uint8_t physics_action_is_shine_air(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t physics_action_is_damage_fly(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t physics_action_is_common_damage(uint16_t action_id) {
  switch (action_id) {
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
      return 1u;
    default:
      return 0u;
  }
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

static inline float physics_apply_shine_air_x_clamp(const MslCharParams* ch,
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

static inline uint8_t physics_action_is_attackdash_knockdown_overlap_owner(uint16_t action_id,
                                                                            uint16_t other_action) {
  const uint8_t other_is_knockdown =
      (other_action == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
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

static inline void physics_compute_guardsetoff_turnover_player_nudge(
    MslBatch* batch, int bi, float out_nudge_x[MSL_MAX_PLAYERS]) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || out_nudge_x == NULL) {
    return;
  }

  // Grounded fighter-overlap nudge constants (`p_ftCommonData->x450` / x454):
  // - ftCommon_8007DD7C accumulates +/-x450 on horizontal overlap.
  // - This reduced 2D subset only models the horizontal lane for Fox/Falco FD rollout parity.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  // refs/melee/src/melee/ft/types.h::ftCommonData (+0x450/+0x454)
  const float player_nudge_x_step = 0.30000001192092896f;

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
    if (!physics_action_is_guardsetoff_turnover_owner(batch->state.action_id[idx],
                                                      batch->state.prev_action_id[idx])) {
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
  const float player_nudge_x_step = 0.30000001192092896f;

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

      const int self_line =
          stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
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

        const float other_center_x =
            batch->state.prev_pos_x[oidx] + other->pushbox_x * (float)batch->state.facing_dir1[oidx];
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
    case MSL_ACT_REBIRTH:
    case MSL_ACT_REBIRTH_WAIT:
    case MSL_ACT_ENTRY:
    case MSL_ACT_ENTRY_START:
    case MSL_ACT_ENTRY_END:
    // Cliff / ledge actions are updated via dedicated callbacks in-engine and should not receive
    // generic gravity/fastfall updates in this simplified core.
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
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
    float guardsetoff_turnover_nudge_x[MSL_MAX_PLAYERS] = {0.0f};
    physics_compute_guardsetoff_turnover_player_nudge(batch, bi, guardsetoff_turnover_nudge_x);
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Record pre-integration position for collision tests.
      // Ordering contract: stage_collision_apply() uses prev_pos_* captured here to perform
      // real "crossing" checks (pre vs post integration) without inferring prior position.
      batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];
      batch->state.prev_on_ground[idx] = batch->state.on_ground[idx] ? 1 : 0;

      if (guardsetoff_turnover_nudge_x[p] != 0.0f) {
        batch->state.pos_x[idx] += guardsetoff_turnover_nudge_x[p];
      }

      // Hitlag freezes motion/physics advancement:
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate runs its main integration block only
      //   under `if (!fp->x2219_b5)`.
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      // Ledge grab cooldown timer (x2064_ledgeCooldown) decrements only when not in hitlag.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      if (batch->state.ledge_cooldown[idx] != 0) {
        batch->state.ledge_cooldown[idx]--;
      }

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const uint16_t action_id = batch->state.action_id[idx];
      const float vy_self_pre = batch->state.speed_y_self[idx];
      const int16_t action_frame = batch->state.action_frame[idx];
      const uint8_t is_damage_fly = physics_action_is_damage_fly(action_id);
      const uint8_t is_common_damage = physics_action_is_common_damage(action_id);
      const uint8_t damage_iasa_lockout =
          (is_damage_fly || is_common_damage) ? physics_damage_iasa_lockout_x221c_b6(batch, idx)
                                              : 0u;
      const uint8_t damage_uses_common_air_helper =
          ((is_damage_fly || is_common_damage) && !damage_iasa_lockout) ? 1u : 0u;

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
          if (!physics_action_skip_common_air_helper_first_frame(action_id, action_frame)) {
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
            } else if (damage_iasa_lockout) {
              // DamageFly/DamageFlyRoll/common Damage x221C_b6 path (`ft_80084EEC`): apply
              // gravity + terminal clamp and aerial friction (no fastfall latch, no drift accel
              // from stick).
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
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                float next_vy = vy_self_pre - phys->grav;
                if (next_vy < -phys->terminal_vel) {
                  next_vy = -phys->terminal_vel;
                }
                batch->state.speed_y_self[idx] = next_vy;
              }
            } else if (physics_action_use_pre_integration_common_air_gravity(action_id) ||
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
                batch->state.speed_air_x_self[idx] =
                    physics_apply_shine_air_x_clamp(ch, c, batch->state.speed_air_x_self[idx]);
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

        // Root-motion aerial side-B (Illusion/Phantasm) uses TransN-derived self velocity.
        // Decomp: ftFx_SpecialAirS_Phys calls `ft_80085134`, which sets fp->self_vel from
        // fp->x6A4_transNOffset.{y,z} (with facing_dir applied to z).
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Phys
        // refs/melee/src/melee/ft/ft_081B.c::ft_80085134
        if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S) {
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 batch->state.anim_frame_f32[idx], dxyz)) {
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
                ch, batch->state.facing[idx], batch->state.action_frame[idx],
                &batch->state.speed_air_x_self[idx], &batch->state.speed_y_self[idx]);
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
                                                 batch->state.anim_frame_f32[idx], dxyz)) {
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
            // - Decomp ft_80085030 root-motion branch is gated by `if (fp->x594_b0)`.
            // - This sim does not model fp->x594_b0; for this movement-only core, treat "TransN
            //   present for (msid, frame)" as the decomp-shaped proxy.
            //
            // Direction:
            // - Decomp uses fp->facing_dir1, but it is normally a copy of facing_dir (engine init
            //   path sets facing_dir1 = facing_dir).
            //   refs/melee/src/melee/ft/fighter.c:264 and :955 (fp->facing_dir1 = fp->facing_dir)
            // - We use batch->state.facing-derived `facing_dir`.
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 batch->state.anim_frame_f32[idx], dxyz)) {
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
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 batch->state.anim_frame_f32[idx], dxyz)) {
              gr_vel = dxyz[2] * facing_dir;
            } else {
              gr_vel += ground_friction_step_delta(gr_vel,
                                                   c->attackdash_friction_mul * ch->gr_friction);
            }
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
            // - ft_80085030 root-motion branch (transNOffset.z * facing_dir) or friction fallback
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80085030}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
            float friction = ch->gr_friction;
            if (msl_absf(gr_vel) > ch->walk_max_vel) {
              friction *= c->high_speed_friction_mul;
            }
            float dxyz[3];
            const uint8_t landing_to_attack11_entry = (action_id == (uint16_t)MSL_ACT_ATTACK_11 &&
                                                       prev_action_id == (uint16_t)MSL_ACT_LANDING)
                                                          ? 1u
                                                          : 0u;
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 batch->state.anim_frame_f32[idx], dxyz) &&
                !landing_to_attack11_entry) {
              // Decomp root-motion gate:
              // - ft_80085030 takes the transN drive branch only when fp->x594_b0 is set.
              // - this core does not carry x594_b0; keep Landing->Attack11 entry on the friction
              //   fallback so landing-momentum rows do not spuriously zero `gr_vel` on jab entry.
              // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
              gr_vel = dxyz[2] * facing_dir;
            } else {
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            }
          } else if (physics_action_is_common_ground_friction_only(action_id)) {
            float friction = ch->gr_friction;
            if (msl_absf(gr_vel) > ch->walk_max_vel) {
              friction *= c->high_speed_friction_mul;
            }
            gr_vel += ground_friction_step_delta(gr_vel, friction);
          } else if (physics_action_is_walk(action_id)) {
            const float accel_mul = 1.0f;
            float accel = stick_x * ch->walk_init_vel * accel_mul;
            accel += (stick_x > 0.0f ? +ch->walk_accel : -ch->walk_accel) * accel_mul;
            const float target = stick_x * ch->walk_max_vel * accel_mul;
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
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Enter,ftCo_Dash_Phys}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
            // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
            if (prev_action_id != (uint16_t)MSL_ACT_DASH) {
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
            if (target == 0.0f) {
              gr_vel += ground_friction_step_delta(gr_vel, friction);
            } else if ((facing_dir * accel) < 0.0f) {
              // Decomp: TurnRun_Phys calls getAccelAndTarget, then only applies accel while
              // `mv.co.turnrun.accel_mul * accel < 0`; otherwise it falls back to grounded friction.
              // On TurnRun_Enter, accel_mul is initialized from the pre-turn facing_dir and x14 is
              // cleared.
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
          vx_self = gr_vel;
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

      // Post-integration gravity update for states we intentionally keep "seed-driven" for current
      // frame displacement (notably DamageFall; see helper docs above).
      if (!on_ground && !physics_is_match_flow_airborne(action_id) &&
          physics_action_use_post_integration_common_air_gravity(action_id)) {
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
