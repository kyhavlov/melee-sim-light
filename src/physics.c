#include "physics.h"
#include "char_registry.h"
#include "falcon_specials.h"
#include "marth_specials.h"
#include "sheik_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "msl_math.h"
#include "stage_collision.h"
#include "specialhi_pose.h"
#include "state_flags.h"
#include "throw_flow.h"

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
  // Source ftCommon_8007C98C clamps toward the target velocity without applying a traction
  // correction when the fighter is already exactly at target. This matters for Dash at terminal
  // speed: held-stick Dash should preserve terminal gr_vel, while above-terminal rows still
  // clamp back down.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007C98C
  if (gr_vel == target_vel) {
    return 0.0f;
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

static inline float physics_ground_friction_mul_for_floor(const MslBatch* batch, size_t bi,
                                                          size_t idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  float friction_mul = batch->state.ground_friction_mul[idx];
  if (!(friction_mul > 0.0f)) {
    friction_mul = 1.0f;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id != 0xFFFFu) {
    // Source owner: ft_GetGroundFrictionMultiplier reads the current floor MapLine flags through
    // mpColl_8004CA6C -> mpLib_800569EC. The generated MSLSTG01 segment field is authoritative
    // when the stage artifact is loaded; the seed lane stays a compatibility fallback.
    // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004CA6C
    // refs/melee/src/melee/mp/mplib.c::mpLib_800569EC
    const float stage_mul =
        stage_collision_floor_ground_friction_mul(batch->state.stage_id[bi], ground_id);
    if (stage_mul > 0.0f) {
      friction_mul = stage_mul;
    }
  }
  return friction_mul;
}

static inline uint8_t physics_try_stage_floor_normal_for_current_line(const MslBatch* batch,
                                                                      size_t bi, size_t idx,
                                                                      float* nx_out,
                                                                      float* ny_out) {
  if (batch == NULL || nx_out == NULL || ny_out == NULL || bi >= (size_t)batch->batch_size) {
    return 0u;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (graph == NULL || line_idx < 0 || (size_t)line_idx >= graph->line_count) {
    return 0u;
  }
  MslStageFloorLine world = graph->lines[(size_t)line_idx];
  if (!stage_collision_floor_line_world(batch, (int)bi, &graph->lines[(size_t)line_idx], &world)) {
    return 0u;
  }
  const float dx = world.x1 - world.x0;
  const float dy = world.y1 - world.y0;
  float nx = -dy;
  float ny = dx;
  if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
    return 0u;
  }
  *nx_out = nx;
  *ny_out = ny;
  return 1u;
}

static inline uint8_t physics_action_uses_ground_kb_scalar_entry_projection(uint16_t action_id) {
  // Decomp: DownBound and neutral Passive floor-contact entries call ftCommon_8007CCE8, which
  // initializes hidden xF0_ground_kb_vel from x8c_kb_vel.x before rebuilding the public KB vector
  // from the current floor tangent. If collision then carries the fighter from a flat floor onto a
  // sloped floor before the next Fighter_procUpdate, the visible public vector can still be
  // horizontal while the source scalar remains x8c_kb_vel.x.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_80097D40,ftCo_8009794C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Enter
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CCE8
  return (uint8_t)((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
                    action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D ||
                    action_id == (uint16_t)MSL_ACT_PASSIVE)
                       ? 1u
                       : 0u);
}

static inline uint8_t physics_landing_from_common_damage_initializes_ground_kb_scalar_from_x(
    const MslBatch* batch, size_t idx, uint16_t action_id, float kb_y, float tangent_y) {
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_LANDING ||
      batch->state.action_frame[idx] > 1 || fabsf(kb_y) <= 0.000001f ||
      fabsf(tangent_y) <= 0.000001f) {
    return 0u;
  }
  // Common Damage -> Landing on a slope:
  // `ftCo_Damage_Coll` can enter `ftCo_Landing_Enter_Basic` through the common floor-contact path.
  // Landing entry calls `ftCommon_8007D7FC`, but does not call `ftCommon_8007CCE8`. On the next
  // Fighter_procUpdate ground branch, source sees `xF0_ground_kb_vel==0`, initializes it from the
  // still-visible airborne `x8c_kb_vel.x`, decays that scalar, then rebuilds `x8c_kb_vel` from the
  // floor tangent. Reseed exposes only the public vector, so dotting the airborne vector against the
  // slope would double-count the vertical component. Bound this to the first Landing frame after
  // generated common-Damage provenance; sustained Landing rows already carry projected public KB.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
  //   ftCo_Landing_Enter_Basic,ftCo_Landing_Enter}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007CCE8}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  return (uint8_t)((msl_motion_state_common_class_has_fast(batch->state.prev_action_id[idx],
                                                           MSL_MS_CLASS_DAMAGE_COMMON) != 0u ||
                    msl_motion_state_common_class_has_fast(batch->state.seed_prev_action_id[idx],
                                                           MSL_MS_CLASS_DAMAGE_COMMON) != 0u)
                       ? 1u
                       : 0u);
}

static inline float physics_ground_kb_scalar_for_decay(MslBatch* batch, size_t idx,
                                                       uint16_t action_id, float kb_x, float kb_y,
                                                       float tangent_x, float tangent_y) {
  if (batch != NULL && physics_action_uses_ground_kb_scalar_entry_projection(action_id) != 0u &&
      fabsf(kb_y) <= 0.000001f && fabsf(tangent_y) > 0.000001f) {
    return kb_x;
  }
  if (physics_landing_from_common_damage_initializes_ground_kb_scalar_from_x(
          batch, idx, action_id, kb_y, tangent_y) != 0u) {
    return kb_x;
  }
  return kb_x * tangent_x + kb_y * tangent_y;
}

static inline void physics_apply_knockback_decay(MslBatch* batch, size_t bi, size_t idx,
                                                 const MslCharParams* ch, const MslCommonParams* c,
                                                 uint16_t action_id, uint8_t on_ground) {
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
    // Ground friction multiplier ownership:
    // - supported-stage runtime uses generated MSLSTG01 floor material data;
    // - `ground_friction_mul` remains the compatibility seed lane when a floor table is unavailable.
    // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
    const float nx = batch->state.ground_normal_x[idx];
    const float ny = batch->state.ground_normal_y[idx];
    const float tangent_x = ny;
    const float tangent_y = -nx;
    float ground_kb =
        physics_ground_kb_scalar_for_decay(batch, idx, action_id, kb_x, kb_y, tangent_x, tangent_y);
    const float friction = physics_ground_friction_mul_for_floor(batch, bi, idx) * ch->gr_friction *
                           c->ground_kb_friction_mul;

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

static inline uint8_t physics_action_is_common_ground_friction_only(uint8_t char_id,
                                                                    uint16_t action_id) {
  {
    // Char-special rows: ownership comes from the extracted MotionState row identity, not
    // the shared numeric range. Spacie SpecialSStart + grounded Reflector rows use the
    // common ground-friction helper; every other char's specials are owned by their own
    // module (marth_specials_phys) or the generic path.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSStart_Phys
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c (grounded Reflector Phys family)
    const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
    if (fx_kind != (uint8_t)MSL_FX_KIND_NONE) {
      return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_START ||
                       (fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
                        fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_LW_TURN));
    }
    if (char_id == (uint8_t)MSL_CHAR_ID_SHEIK) {
      // Grounded Sheik Chain Phys is the common ground-friction helper for Start/held/End.
      // Keep this source-owned motion-state slice here so seeded gr_vel decays instead of dragging
      // the attached Chain article during the held whip.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialSStart_Phys,ftSk_SpecialS_Phys,ftSk_SpecialSEnd_Phys}
      if (action_id == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ||
          action_id == (uint16_t)MSL_ACT_SK_SPECIAL_S ||
          action_id == (uint16_t)MSL_ACT_SK_SPECIAL_S_END) {
        return 1u;
      }
    }
    if (action_id >= 341u) {
      return 0u;
    }
  }
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

  // Our ISO-derived SSANIM01 v5 artifacts store per-frame TransN translation as a tail (x,y,z);
  // approximate the per-frame TransN offset as a finite difference between the previous and current
  // animation frames.
  // - tools/extraction/extract_fighter_anims.py (SSANIM01 v5 + per-frame TransN tail)
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
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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

static inline uint8_t physics_action_uses_common_air_drift(uint8_t char_id, uint16_t action_id) {
  // Decomp: the common helper `ft_80084DB0` calls `ftCommon_8007D268` to compute x drift.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  //
  // EscapeAir is special-cased: when cmd_skip_decay is false, EscapeAir scales self_vel and does
  // not call `ft_80084DB0` in-engine.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return 0;
  }
  if (action_id == (uint16_t)MSL_ACT_CAPTURE_JUMP) {
    // CaptureJump Phys calls ftCommon_8007D268 directly after ftCommon_Fall. It gets common air
    // drift without inheriting ft_80084DB0's fastfall latch.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureJump_Phys
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D268
    return 1;
  }
  return msl_action_allows_fastfall(char_id, action_id);
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

static inline uint8_t physics_shine_air_applies_fall_this_frame(
    uint8_t char_id, const MslCharParams* ch, const MslCommonParams* c, uint16_t action_id,
    uint16_t prev_action_id, uint16_t seed_prev_action_id, int16_t action_frame,
    float speed_y_self) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  if (ch == NULL || fx_kind < (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START ||
      fx_kind > (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_TURN) {
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
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) {
    return (uint8_t)(action_frame >= (int16_t)ch->reflector_gravity_delay_frames);
  }
  const uint8_t prev_was_air_start = (msl_motion_state_fx_special_kind(char_id, prev_action_id) ==
                                      (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START)
                                         ? 1u
                                         : 0u;
  const uint8_t seed_prev_was_air_start =
      (msl_motion_state_fx_special_kind(char_id, seed_prev_action_id) ==
       (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START)
          ? 1u
          : 0u;
  if (prev_was_air_start != 0u && fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START &&
      action_frame <= 0) {
    return 0u;
  }
  if (c != NULL && seed_prev_was_air_start != 0u &&
      fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_LOOP && action_frame <= 1 &&
      speed_y_self == c->pass_vel_y) {
    // Grounded Reflector platform-pass path:
    // `ftFx_SpecialLwStart_Pass` uses `ftCo_8009A184` to enter SpecialAirLwStart from the grounded
    // Start state, so the hidden `mv.fx.SpecialLw.gravityDelay` set by
    // `ftFox_SpecialLw_SetVars` has not been decremented by aerial Start Phys as many times as a
    // direct aerial Reflector entry. The pass path also writes the common pass vertical velocity;
    // use that source value to distinguish the delayed platform-pass Loop from ordinary aerial
    // Loop rows whose countdown has expired.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    //   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLwStart_Phys,ftFx_SpecialAirLwLoop_Phys}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A184
    // data/common/ft_common_data.json::pass_vel_y
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

static inline uint8_t physics_action_is_shine_air(uint8_t char_id, uint16_t action_id) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return (fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START &&
          fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t physics_action_is_damage_fly(uint16_t action_id) {
  // Generated from decomp MotionState callback symbols:
  // - ftCo_DamageFly_* for DamageFlyHi/N/Lw/Top/Roll
  // - ftCo_FlyReflect_* for wall/ceiling reflect follow-up states
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t physics_action_is_common_damage(uint16_t action_id) {
  // Generated from decomp MotionState callback symbols:
  // - ftCo_Damage_* for DamageHi/N/Lw/Air
  // - ftCo_DownDamage_* for DownDamageU/D, whose Phys delegates to common Damage Phys
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Phys
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_COMMON);
}

static inline uint8_t physics_action_is_grounded_common_damage_phys(uint16_t action_id) {
  if (msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_COMMON) == 0u) {
    return 0u;
  }
  // Grounded ftCo_Damage_Phys uses ft_80084F3C for the common DamageHi/N/Lw/Air family.
  // DownDamage also delegates to ftCo_Damage_Phys in source, but this core handles the downed
  // Phys/Anim state machine in knockdown.c so do not run the generic ground-friction writer twice.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Phys
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
  return (uint8_t)(action_id != (uint16_t)MSL_ACT_DOWN_DAMAGE_U &&
                   action_id != (uint16_t)MSL_ACT_DOWN_DAMAGE_D);
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
  return msl_state_flags_221c_hitstun_at(batch->state.state_flags, idx);
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

static inline uint8_t physics_damagefall_seed_allow_interrupt(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  return (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) ? 1u
                                                                                            : 0u;
}

static inline uint8_t physics_damagefall_source_fastfall_latch(const MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               size_t idx) {
  if (batch == NULL || c == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_DAMAGE_FALL ||
      batch->state.fall_fast[idx] != 0u || !(batch->state.speed_y_self[idx] < 0.0f)) {
    return 0u;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(stick_y <= -c->fastfall_stick_threshold) ||
      !(batch->state.tilt_timer_y[idx] < c->fastfall_tilt_max_frames)) {
    return 0u;
  }
  // DamageFall fastfall latch order:
  // - DamageFall_Phys delegates to ft_80084DB0.
  // - ft_80084DB0 calls ftCommon_CheckFallFast before ftCommon_Fall/FallFast.
  // - Fighter_procUpdate then integrates cur_pos with the newly latched fastfall self_vel.y.
  // Keep this narrower than the older steady DamageFall compatibility bridge: it only flips rows
  // whose current input/timer state proves the source fastfall latch fires this frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Phys
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  return 1u;
}

static inline uint8_t physics_replay_rollout_advanced_past_reseed(const MslBatch* batch, int bi) {
  if (batch == NULL || batch->replay_rollout_reseeded == NULL ||
      batch->replay_rollout_reseeded[bi] == 0u || batch->replay_rollout_seed_frame_id == NULL) {
    return 0u;
  }
  return (batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi]) ? 1u : 0u;
}

static inline uint8_t physics_action_use_pre_integration_common_air_gravity(const MslBatch* batch,
                                                                            int bi, size_t idx,
                                                                            uint16_t action_id) {
  // Decomp: many common airborne action states call `ft_80084DB0` from their phys callbacks, which
  // runs `ftCommon_CheckFallFast` + `ftCommon_Fall/FallFast` (mutating `self_vel.y`) before
  // `Fighter_procUpdate` integrates `cur_pos`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  //
  // However, in the v1 teacher-forced reseed loop some steady DamageFall rows still carry a
  // seed-driven current-frame displacement surface. Applying the common fall helper before
  // integration for every such row can cause match-flow blastzone false positives (stocks
  // decremented) vs the replay reference.
  //
  // Repro (suite): `AttachedGoodNaturedGuanaco.msl` record=2959 p=0 (DamageFall) crosses FD blast
  // bottom and triggers `MSL_ACT_DEAD_DOWN` when we apply common gravity pre-integration.
  //
  // QGD's post-hitstun DamageFall source row carries the replay-exposed `fp+0x2218` allow-interrupt
  // lane after `DamageFall_IASA` is active. For that terminal lane, use the decomp callback order
  // (`DamageFall_Phys -> ft_80084DB0`) before integration; keep the older bridge for steady rows
  // without that source lane until the remaining DamageFall seed surface is closed.
  //
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Phys
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  // Note:
  // - Common airborne Damage states (for example DamageAir2) route through ftCo_Damage_Phys,
  //   which calls ft_80084DB0 when x221C_b6 is clear.
  // - DamageFly is handled via its own x221C_b6-gated branch below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys
  if (!msl_action_allows_fastfall(batch->state.char_id[idx], action_id)) {
    return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
                     action_id == (uint16_t)MSL_ACT_CAPTURE_JUMP);
  }
  if (action_id == (uint16_t)MSL_ACT_DAMAGE_FALL) {
    // Direct teacher-forced seeds may still need the legacy seed-displacement bridge above, but
    // once a replay rollout has advanced past the exact reseed row, DamageFall is live runtime
    // state again and its Phys callback owns source-order gravity before position integration.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Phys
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    if (physics_replay_rollout_advanced_past_reseed(batch, bi)) {
      return 1u;
    }
    return (uint8_t)(physics_damagefall_seed_allow_interrupt(batch, idx) ||
                     physics_damagefall_source_fastfall_latch(batch, msl_common_params(), idx));
  }
  return 1u;
}

static inline uint8_t physics_action_use_post_integration_common_air_gravity(const MslBatch* batch,
                                                                             size_t idx,
                                                                             uint16_t action_id) {
  // Temporary v1 compatibility: update `speed_y_self` for next frame without affecting current
  // frame displacement (see note above).
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL &&
                   !physics_damagefall_seed_allow_interrupt(batch, idx));
}

static inline uint8_t physics_damagefall_entry_uses_pre_integration_phys(uint16_t action_id,
                                                                         uint16_t prev_action_id,
                                                                         int16_t action_frame) {
  // DamageFly_Anim can enter DamageFall via ftCo_80090780 before the same Fighter_procUpdate
  // reaches Phys. CliffWait timeout uses the same ftCo_80090780 destination path from IASA after
  // CliffWait_Anim decrements mv.co.cliff.x4 to zero. The destination DamageFall_Phys calls
  // ft_80084DB0, so gravity mutates self_vel.y before Fighter_procUpdate integrates cur_pos on
  // that entry frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::{ftCo_CliffWait_Anim,ftCo_8009A9AC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{ftCo_80090780,ftCo_DamageFall_Phys}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL && action_frame <= 0 &&
                   (physics_action_is_damage_fly(prev_action_id) ||
                    prev_action_id == (uint16_t)MSL_ACT_CLIFF_WAIT));
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
  // Decomp: Escape entry (`ftCo_80099314`) and grounded CliffClimb/CliffAttack/CliffEscape option
  // entries set `fp->x221D_b5 = true`; common grounded fighter-overlap nudge (`ftCommon_8007E0E4`)
  // skips the self `ftCommon_8007DD7C` pass while that bit is set. The peer can still nudge away
  // because `ftCommon_8007DD7C` does not filter the other fighter on x221D_b5.
  //
  // Source ordering boundary:
  // - Fighter_8006A360 runs Anim + ftCommon_8007E0E4 before the later IASA/input proc.
  // - Same-frame Guard_IASA -> Escape* and CliffWait_IASA -> Cliff option entries set x221D_b5
  //   after the current frame's common nudge pass, so only continuous frames suppress the self pass
  //   here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099314
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
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
  if (current_escape && prev_escape) {
    return 1u;
  }

  const uint8_t current_cliff_option = (action_id == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW ||
                                        action_id == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
                                        action_id == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW ||
                                        action_id == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
                                        action_id == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW ||
                                        action_id == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK)
                                           ? 1u
                                           : 0u;
  const uint8_t prev_cliff_option = (prev_action_id == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW ||
                                     prev_action_id == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
                                     prev_action_id == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW ||
                                     prev_action_id == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
                                     prev_action_id == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW ||
                                     prev_action_id == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK)
                                        ? 1u
                                        : 0u;
  return (uint8_t)(current_cliff_option && prev_cliff_option);
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

static inline uint8_t physics_floor_line_contains_x(const MslStageFloorLine* line, float x) {
  if (line == NULL) {
    return 0u;
  }
  const float min_x = line->x0 < line->x1 ? line->x0 : line->x1;
  const float max_x = line->x0 > line->x1 ? line->x0 : line->x1;
  return (uint8_t)(x >= min_x && x <= max_x);
}

static inline uint8_t physics_floor_line_contains_or_connects_to_nudged_x(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx, float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  MslStageFloorLine world = *line;
  (void)stage_collision_floor_line_world(batch, bi, line, &world);
  if (physics_floor_line_contains_x(&world, x)) {
    return 1u;
  }
  const int adj[2] = {line->prev, line->next};
  for (size_t i = 0; i < 2; i++) {
    const int adj_idx = adj[i];
    if (adj_idx < 0 || (size_t)adj_idx >= g->line_count) {
      continue;
    }
    const MslStageFloorLine* other = &g->lines[(size_t)adj_idx];
    MslStageFloorLine other_world = *other;
    (void)stage_collision_floor_line_world(batch, bi, other, &other_world);
    if (physics_floor_line_contains_x(&other_world, x)) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t physics_action_uses_generated_b2dc_edge_snap_callback(uint16_t action_id) {
  if (msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_FT800827A0_EDGE_SNAP_COLL)) {
    // Generated MSLMSO01 owner for grounded callbacks that route to `ft_800827A0`, directly or
    // through wrappers such as `ft_80084104` / `ft_800841B8`. Common player nudge runs before
    // Fighter_procUpdate and the Coll callback, so these actions may consume an outward x450 nudge
    // through the source endpoint-snap path instead of dropping it at the current floor span.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104,ft_800841B8}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 FT800827A0_EDGE_SNAP_COLL)
    return 1u;
  }
  return 0u;
}

static inline uint8_t physics_action_uses_ft80084280_ottotto_edge_callback(uint16_t action_id) {
  if (msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_LANDING_AIR_COLL)) {
    return 1u;
  }
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_RUN_BRAKE:
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t physics_action_uses_player_nudge_ft80083f88_ground_to_air_coll(
    const MslBatch* batch, size_t idx, uint16_t action_id) {
  // Generated MSLMSO01 marks all grounded collision callbacks whose decomp bodies call
  // `ft_80083F88(gobj)`. The retained runtime owner here consumes only audited subsets whose
  // callback is known to allow ground-to-air after common xF8 player nudge:
  // - KneeBend: SquatRv/grounded jump entry can carry the frame-start xF8 nudge to a facing floor
  //   edge, and KneeBend_Coll then lets `ft_80082708 -> mpColl_8004B108` decide ground-to-air.
  // - DownWait/DownStand: the downed wait/stand collision callbacks share the same
  //   `ft_80083F88` chain, so overlap separation at a ledge is not clipped away before the source
  //   floor-loss test runs.
  // - Passive: neutral tech recovery calls the same `ft_80083F88` wrapper; the nudge direction is
  //   not constrained to facing before `mpColl_8004B108` can enter Fall.
  // - Squat: destination Wait IASA can enter Squat before Phys/Coll on terminal downed/passive
  //   frames, and Squat_Coll also calls `ft_80083F88`. The same callback ordering applies when
  //   terminal EscapeN_Anim enters Wait through ft_8008A2BC and the destination Wait_IASA
  //   immediately enters Squat through ftCo_800D5FB0; this is bounded to the first destination
  //   Squat frame with EscapeN frame-start provenance.
  // Other ft_80083F88 callback families remain table-visible but are not runtime-closed by this
  // helper; a broader all-class nudge edge admission caused unrelated Battlefield float drift.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
  //   ftCo_DownWait_Coll,ftCo_DownStand_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Coll
  const uint8_t passive_source_squat =
      (batch != NULL && action_id == (uint16_t)MSL_ACT_SQUAT &&
       (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE))
          ? 1u
          : 0u;
  const uint8_t escape_n_terminal_source_squat =
      (batch != NULL && action_id == (uint16_t)MSL_ACT_SQUAT &&
       batch->state.action_frame[idx] <= 1 &&
       (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_N ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_N))
          ? 1u
          : 0u;
  const uint8_t audited_action =
      (uint8_t)(action_id == (uint16_t)MSL_ACT_KNEE_BEND || passive_source_squat ||
                escape_n_terminal_source_squat || action_id == (uint16_t)MSL_ACT_PASSIVE ||
                action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
                action_id == (uint16_t)MSL_ACT_DOWN_WAIT_D ||
                action_id == (uint16_t)MSL_ACT_DOWN_STAND_U ||
                action_id == (uint16_t)MSL_ACT_DOWN_STAND_D);
  return (uint8_t)(audited_action && msl_motion_state_common_class_has_fast(
                                         action_id, MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL));
}

static inline uint8_t physics_action_uses_common_damage_floor_loss_nudge(uint16_t action_id) {
  if (physics_action_is_common_damage(action_id) == 0u) {
    return 0u;
  }
  // Common Damage_Coll ground branch:
  // - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate.
  // - Fighter_procUpdate applies xF8_playerNudgeVel.x before the later Coll callback.
  // - ftCo_Damage_Coll then calls ft_800848DC, whose ft_80082708/mpColl_8004B108 path observes
  //   the already-nudged floor edge and can enter MissFoot.
  //
  // DownDamage has a separate downed callback family, so keep this owner on the common Damage
  // ftCo_Damage_Coll family only.
  //
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
  return (uint8_t)(action_id != (uint16_t)MSL_ACT_DOWN_DAMAGE_U &&
                   action_id != (uint16_t)MSL_ACT_DOWN_DAMAGE_D);
}

static inline uint8_t physics_nudge_reaches_facing_edge(const MslStageFloorGraph* g, int line_idx,
                                                        float pos_x, float nudge_x,
                                                        uint8_t facing_right) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || nudge_x == 0.0f) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  const float nudged_x = pos_x + nudge_x;
  if (facing_right) {
    return (uint8_t)(nudge_x > 0.0f && nudged_x >= line->x1);
  }
  return (uint8_t)(nudge_x < 0.0f && nudged_x <= line->x0);
}

static inline uint8_t physics_nudge_exits_floor_span(const MslStageFloorGraph* g, int line_idx,
                                                     float pos_x, float nudge_x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || nudge_x == 0.0f) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  const float nudged_x = pos_x + nudge_x;
  return (uint8_t)((nudge_x > 0.0f && nudged_x >= line->x1) ||
                   (nudge_x < 0.0f && nudged_x <= line->x0));
}

static inline uint8_t physics_nudge_exits_guard_missfoot_edge(const MslStageFloorGraph* g,
                                                              int line_idx, float pos_x,
                                                              float nudge_x, uint8_t facing_right) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || nudge_x == 0.0f) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  const float left = (line->x0 < line->x1) ? line->x0 : line->x1;
  const float right = (line->x0 > line->x1) ? line->x0 : line->x1;
  const float nudged_x = pos_x + nudge_x;
  if (facing_right) {
    return (uint8_t)(nudge_x < 0.0f && nudged_x <= left);
  }
  return (uint8_t)(nudge_x > 0.0f && nudged_x >= right);
}

static inline uint8_t physics_action_uses_guard_player_nudge_floor_loss(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t physics_action_uses_downwait_player_nudge_floor_loss(const MslBatch* batch,
                                                                           size_t idx,
                                                                           uint16_t action_id) {
  return (uint8_t)((action_id == (uint16_t)MSL_ACT_SQUAT ||
                    action_id == (uint16_t)MSL_ACT_PASSIVE ||
                    action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
                    action_id == (uint16_t)MSL_ACT_DOWN_WAIT_D ||
                    action_id == (uint16_t)MSL_ACT_DOWN_STAND_U ||
                    action_id == (uint16_t)MSL_ACT_DOWN_STAND_D) &&
                   physics_action_uses_player_nudge_ft80083f88_ground_to_air_coll(batch, idx,
                                                                                  action_id));
}

static inline uint16_t physics_common_overlap_nudge_source_action(const MslBatch* batch,
                                                                  size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_entry_via_wait_callback[idx] != 0u) {
    // Source order:
    // - Fighter_8006A360 runs Anim callback, then ftCommon_8007E0E4 common overlap nudge.
    // - Fighter_procUpdate runs the input/IASA callback later; a destination Wait row can then
    //   enter GuardOn through ftCo_80091A4C after the nudge pass.
    // Use the source-visible Wait callback owner for the overlap gate, while leaving the current
    // action as GuardOn for the later Phys/Coll callbacks.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    return (uint16_t)MSL_ACT_WAIT;
  }
  return action_id;
}

static inline uint8_t physics_guard_entry_from_wait_nudge_can_feed_floor_loss(
    const MslBatch* batch, const MslStageFloorGraph* g, int line_idx, size_t idx, float nudge_x) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.guard_entry_via_wait_callback[idx] == 0u) {
    return 0u;
  }
  // Source order bridge for this single callback window:
  // - ftCommon_8007E0E4 writes xF8_playerNudgeVel while the just-entered destination Wait callback
  //   is still the source owner.
  // - Fighter_procUpdate later admits GuardOn through Wait_IASA, applies the already-written xF8
  //   nudge, then GuardOn_Coll can consume the resulting floor loss through ft_800845B4.
  // This is not a generic GuardOn nudge rule; it requires the same-frame Wait-callback marker and
  // only admits nudges that actually leave the current floor span.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_procMap}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_GuardOn_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_800845B4
  return physics_nudge_exits_floor_span(g, line_idx, batch->state.pos_x[idx], nudge_x);
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

static inline uint8_t physics_knockdown_is_same_frame_damage_publication(const MslBatch* batch,
                                                                         size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id != (uint16_t)MSL_ACT_DOWN_BOUND_U && action_id != (uint16_t)MSL_ACT_DOWN_BOUND_D) {
    return 0u;
  }
  const uint16_t seed_prev = batch->state.seed_prev_action_id[idx];
  return (uint8_t)((physics_action_is_damage_fly(seed_prev) ||
                    seed_prev == (uint16_t)MSL_ACT_DAMAGE_FALL) &&
                   batch->state.action_frame[idx] <= 1);
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

    const MslCharParams* self = msl_char_params_fast(batch->state.char_id[idx]);
    if (self == NULL) {
      continue;
    }

    const int self_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    if (self_line < 0) {
      continue;
    }

    if (!physics_action_suppresses_self_player_nudge_x221d_b5(batch->state.action_id[idx],
                                                              batch->state.prev_action_id[idx])) {
      const uint16_t source_action = physics_common_overlap_nudge_source_action(batch, idx);
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
        if ((physics_action_is_attackdash_knockdown_overlap_owner(batch->state.action_id[idx],
                                                                  other_action_for_nudge) &&
             physics_knockdown_is_same_frame_damage_publication(batch, oidx)) ||
            (physics_action_is_attackdash_knockdown_overlap_owner(other_action_for_nudge,
                                                                  batch->state.action_id[idx]) &&
             physics_knockdown_is_same_frame_damage_publication(batch, idx))) {
          // Source order:
          // - Fighter_8006A360 computes ftCommon_8007E0E4/x450 before Fighter_procUpdate and before
          //   this frame's DamageFly/DamageFall collision can publish DownBound.
          // - Stale DownWait/DownBound peers are already source-visible and must stay on the
          //   ordinary common nudge path. Only same-frame damage->DownBound publications are absent
          //   at the source common-nudge phase.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_80090984
          continue;
        }

        if ((physics_action_is_attackdash_knockdown_overlap_owner(batch->state.action_id[idx],
                                                                  other_action_for_nudge) ||
             physics_action_is_attackdash_knockdown_overlap_owner(other_action_for_nudge,
                                                                  batch->state.action_id[idx])) &&
            !stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
            !stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[oidx])) {
          // The retained source-completion here is the live transformed-floor span owner above.
          // Non-transformed AttackDash-vs-downed overlap rows remain out of this common-nudge
          // approximation until their callback-local source phase is modeled separately.
          continue;
        }

        const MslCharParams* other = msl_char_params_fast(batch->state.char_id[oidx]);
        if (other == NULL) {
          continue;
        }

        const int other_line =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[oidx]);
        const uint8_t floor_adjacent =
            physics_floor_lines_adjacent_or_equal(floor_graph, self_line, other_line);
        if (!floor_adjacent) {
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
        const uint8_t nudge_x_admitted =
            (uint8_t)(floor_adjacent &&
                      (physics_floor_line_contains_or_connects_to_nudged_x(
                           batch, bi, floor_graph, self_line, batch->state.pos_x[idx] + nudge_x) ||
                       (physics_action_uses_generated_b2dc_edge_snap_callback(source_action) &&
                        physics_nudge_exits_floor_span(floor_graph, self_line,
                                                       batch->state.pos_x[idx], nudge_x)) ||
                       (physics_action_uses_ft80084280_ottotto_edge_callback(source_action) &&
                        physics_nudge_reaches_facing_edge(floor_graph, self_line,
                                                          batch->state.pos_x[idx], nudge_x,
                                                          batch->state.facing[idx])) ||
                       (physics_action_uses_player_nudge_ft80083f88_ground_to_air_coll(
                            batch, idx, source_action) &&
                        (physics_nudge_reaches_facing_edge(floor_graph, self_line,
                                                           batch->state.pos_x[idx], nudge_x,
                                                           batch->state.facing[idx]) ||
                         (physics_action_uses_downwait_player_nudge_floor_loss(batch, idx,
                                                                               source_action) &&
                          physics_nudge_exits_floor_span(floor_graph, self_line,
                                                         batch->state.pos_x[idx], nudge_x)))) ||
                       (physics_action_uses_common_damage_floor_loss_nudge(source_action) &&
                        physics_nudge_exits_floor_span(floor_graph, self_line,
                                                       batch->state.pos_x[idx], nudge_x)) ||
                       (physics_action_uses_guard_player_nudge_floor_loss(source_action) &&
                        physics_nudge_exits_guard_missfoot_edge(floor_graph, self_line,
                                                                batch->state.pos_x[idx], nudge_x,
                                                                batch->state.facing[idx])) ||
                       physics_guard_entry_from_wait_nudge_can_feed_floor_loss(
                           batch, floor_graph, self_line, idx, nudge_x)));
        if (!nudge_x_admitted) {
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
        ((batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD ||
          (batch->state.action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
           batch->state.action_frame[idx] <= 1)) &&
         batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
            ? physics_action_is_guardsetoff_turnover_owner(batch->state.action_id[idx],
                                                           batch->state.seed_prev_action_id[idx])
            : 0u;
    // GuardSetOff -> Guard -> EscapeN/Catch same-proc turnover:
    // - GuardSetOff_Anim can promote to Guard in Fighter_8006A360.
    // - The common overlap pass (`ftCommon_8007E0E4`) then runs before Fighter_procUpdate's Guard
    //   IASA can enter EscapeN through `ftCo_8009980C` or Catch through `ftCo_Catch_CheckInput`.
    // - Preserve exactly that source-owned nudge; ordinary/stale EscapeN rows stay outside this
    //   helper.
    // - The same callback phase can continue through Guard/Wait IASA into KneeBend after the
    //   common overlap pass. The first replay-visible KneeBend row still carries
    //   seed_prev_action_id=GuardSetOff, while the x450 nudge was owned before KneeBend's current
    //   Phys/Coll callbacks.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Enter
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_8009980C,ftCo_800998EC}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    const uint8_t guardsetoff_turnover_escape_n_from_guard =
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_N &&
         batch->state.action_frame[idx] <= 1 &&
         batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF)
            ? 1u
            : 0u;
    const uint8_t guardsetoff_turnover_catch_from_guard =
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_CATCH &&
         batch->state.action_frame[idx] <= 0 &&
         (batch->state.frame_start_action_id[idx] == (uint16_t)MSL_ACT_GUARD ||
          batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD) &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF)
            ? 1u
            : 0u;
    if (!guardsetoff_turnover_from_prev && !guardsetoff_turnover_from_promoted_seed_prev &&
        !guardsetoff_turnover_escape_n_from_guard && !guardsetoff_turnover_catch_from_guard) {
      continue;
    }

    const MslCharParams* self = msl_char_params_fast(batch->state.char_id[idx]);
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

      const MslCharParams* other = msl_char_params_fast(batch->state.char_id[oidx]);
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

      const MslCharParams* self = msl_char_params_fast(batch->state.char_id[idx]);
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

        const MslCharParams* other = msl_char_params_fast(batch->state.char_id[oidx]);
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

static inline uint8_t physics_damagefly_hitlag_exit_terminal_ledge_endpoint_owner(
    const MslBatch* batch, size_t idx) {
  if (batch == NULL || !physics_action_is_damage_fly(batch->state.action_id[idx]) ||
      batch->state.hitlag_pre_timer[idx] == 0u || batch->state.hitlag[idx] != 0u ||
      batch->state.ground_id[idx] == 0xFFFFu || batch->state.speed_y_attack[idx] > 0.0f) {
    return 0u;
  }

  const int bi = (int)(idx / (size_t)batch->config.num_players);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (!line->fighter_solid || !line->is_ledge || line->is_platform ||
      stage_collision_floor_line_has_platform_transform(stage_id, line->segment_i)) {
    return 0u;
  }

  enum { MSL_MPLIB_ENDPOINT_EXTENSION_UNITS = 1 };
  const float root_x = batch->state.floor_sweep_prev_pos_x[idx];
  if (!isfinite(root_x)) {
    return 0u;
  }

  // Same source owner as timers.c::damagefly_hitlag_exit_terminal_ledge_endpoint_owner:
  // the hitlag-exit DamageFly callback consumes a terminal ledge-floor endpoint through
  // `ft_80081DD4 -> mpColl_800473CC`, so this callback row updates gravity/KB decay but does not
  // publish the lateral/root displacement term across the ledge seam. Upward KB rows are not this
  // owner: source leaves the floor through ordinary DamageFly integration before any DownBound
  // publication.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/{mpcoll.c,mplib.c}::{mpColl_800473CC,mpLib_8004ED5C}
  // data/stages/bin/*.bin::MSLSTG01 floor ledge/link metadata
  const uint8_t near_prev_endpoint =
      (uint8_t)(line->prev < 0 &&
                fabsf(root_x - line->x0) <= (float)MSL_MPLIB_ENDPOINT_EXTENSION_UNITS);
  const uint8_t near_next_endpoint =
      (uint8_t)(line->next < 0 &&
                fabsf(root_x - line->x1) <= (float)MSL_MPLIB_ENDPOINT_EXTENSION_UNITS);
  return (uint8_t)(near_prev_endpoint || near_next_endpoint);
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

static inline void physics_apply_pass_floor_skip_x671_owner(const MslCommonParams* c,
                                                            MslBatch* batch, size_t idx,
                                                            uint16_t action_id,
                                                            int16_t action_frame, float stick_y) {
  if (c == NULL || batch == NULL || action_id != (uint16_t)MSL_ACT_PASS) {
    return;
  }
  if (action_frame < 0 || action_frame > (int16_t)(c->floor_skip_frames + 1u)) {
    return;
  }
  const float prev_stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  if (stick_y > -c->fastfall_stick_threshold || prev_stick_y > -c->fastfall_stick_threshold) {
    return;
  }

  // Pass/floor-skip entry ownership:
  // - ftCo_8009A228 / ftCo_8009A184 call ftCommon_8007D5D4, enter Pass or a pass-through aerial
  //   motion with Ft_MF_None, call mpUpdateFloorSkip, then write fp->x671_timer_lstick_tilt_y =
  //   0xFE.
  // - The public replay seed can reconstruct the Pass action and platform floor-skip, but its
  //   x671-style tilt timer is controller-history derived. Reapply the source entry side effect for
  //   the short Pass ownership window so held-down platform-drop input does not relatch fastfall
  //   before Pass_Anim exits through ftCo_Fall_Enter.
  // - Require current and previous stick-down samples so a real release/re-press inside Pass can
  //   still create a fresh x671=0 edge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{
  //   ftCo_8009A184,ftCo_8009A228,ftCo_Pass_Anim,ftCo_Pass_Phys}
  // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip}
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.fall_fast[idx] = 0u;
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

      // Character-special Phys callbacks own their self-velocity update when active; the position
      // integration below still runs.
      const uint8_t marth_special_owned_vel = marth_specials_phys(batch, idx);
      const uint8_t sheik_special_owned_vel = sheik_specials_phys(batch, idx);
      const uint8_t falcon_special_owned_vel = falcon_specials_phys(batch, idx);

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

      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        // Compatibility pending-release placeholder:
        // - ftCo_800DD724 consumes release and immediately calls ftCo_800DDDE4/ftCo_800DE7C0 in
        //   the thrower's Anim callback; the victim does not get an intervening generic Fall Phys
        //   drift frame before Damage* entry.
        // - Normal runtime release damage now runs in that Anim callback path. Seed/reseed
        //   compatibility latches may still expose the detached victim as Fall; keep that
        //   placeholder non-physical until throw_flow_update_post_items() consumes it.
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
      if (!on_ground && !marth_special_owned_vel && !sheik_special_owned_vel &&
          !falcon_special_owned_vel) {
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
                const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
                if (phys != NULL) {
                  const float stick_x = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
                  const float stick_y = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                  const uint8_t allow_fastfall =
                      msl_action_allows_fastfall(batch->state.char_id[idx], action_id);
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
            } else if (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                       (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR) {
              // Owner identity from the extracted MotionState table (the row whose anim
              // callback is ftFx_SpecialHiHoldAir_Anim), not a raw action-id + char-family
              // predicate (see motion_state_owners.h MslMsFxSpecialKind).
              const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
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
              const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
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
                           batch->state.char_id[idx],
                           msl_char_params_fast(batch->state.char_id[idx]), c, action_id,
                           batch->state.prev_action_id[idx], batch->state.seed_prev_action_id[idx],
                           action_frame, batch->state.speed_y_self[idx])) {
              // Aerial Reflector Phys: after the reflector gravityDelay expires, the callback uses
              // ftCommon_Fall with ftFox_DatAttrs.xAC rather than common character gravity.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
              //   ftFx_SpecialAirLwStart_Phys,ftFx_SpecialAirLwLoop_Phys,
              //   ftFx_SpecialAirLwHit_Phys,ftFx_SpecialAirLwTurn_Phys,
              //   ftFx_SpecialAirLwEnd_Phys}
              physics_apply_shine_air_fall(msl_char_params_fast(batch->state.char_id[idx]),
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
              const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
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
            } else if (physics_action_use_pre_integration_common_air_gravity(batch, bi, idx,
                                                                             action_id) ||
                       physics_damagefall_entry_uses_pre_integration_phys(
                           action_id, batch->state.prev_action_id[idx], action_frame) ||
                       damage_uses_common_air_helper) {
              const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
              if (phys != NULL) {
                const uint8_t allow_fastfall =
                    (msl_action_allows_fastfall(batch->state.char_id[idx], action_id) ||
                     damage_uses_common_air_helper)
                        ? 1u
                        : 0u;

                // Fastfall latch (ftCommon_CheckFallFast) uses the pre-gravity `self_vel.y`.
                // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
                if (allow_fastfall) {
                  const float stick_y = apply_deadzone(
                      stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
                  physics_apply_pass_floor_skip_x671_owner(c, batch, idx, action_id, action_frame,
                                                           stick_y);
                  (void)ftCommon_CheckFallFast(c, stick_y, vy_self_pre,
                                               &batch->state.fall_fast[idx],
                                               &batch->state.tilt_timer_y[idx]);
                }

                // Air gravity / terminal velocity / fastfall update.
                //
                // Decomp refs:
                // - Gravity/terminal: refs/melee/src/melee/ft/ftcommon.c::ftCommon_Fall
                // - Fastfall: refs/melee/src/melee/ft/ftcommon.c::ftCommon_FallFast (called via ft_80084DB0)
                // - FallSpecial_Phys calls ftCommon_Fall with `ca->terminal_vel` when
                //   mv.co.fallspecial.xC != 0, but with `ca->fast_fall_velocity` when xC == 0.
                //   This is not the public fall_fast bit; xC==0 can publish the fastfall terminal
                //   while `fall_fast` remains clear.
                //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Phys
                float next_vy = vy_self_pre;
                if (allow_fastfall && batch->state.fall_fast[idx]) {
                  next_vy = -phys->fast_fall_velocity;
                } else {
                  next_vy -= phys->grav;
                  float terminal = phys->terminal_vel;
                  if (physics_action_is_fall_special_like(action_id) &&
                      batch->state.fallspecial_xc[idx] == 0u) {
                    terminal = phys->fast_fall_velocity;
                  }
                  if (next_vy < -terminal) {
                    next_vy = -terminal;
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
            const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
            if (ch != NULL) {
              const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                                   c->lstick_deadzone_x);
              if (physics_action_is_shine_air(batch->state.char_id[idx], action_id)) {
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
              } else if (physics_action_uses_common_air_drift(batch->state.char_id[idx],
                                                              action_id) ||
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
        if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START)) {
          const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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
        if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S)) {
          const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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
        } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                    (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END)) {
          // Decomp: ftFx_SpecialAirSEnd_Phys applies air friction using ftFox_DatAttrs.x40 and
          // applies gravity after mv.fx.SpecialS.gravityDelay expires (x44 gate, x48 accel).
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Phys
          // refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs
          // data/characters/{fox,falco}.json::{
          //   illusion_air_friction,illusion_gravity_delay_end_frames,illusion_fall_accel_end,terminal_vel
          // }
          const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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
        } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                    (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI)) {
          const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
          if (ch != NULL) {
            physics_apply_specialhi_air_reverse_accel(
                batch, idx, ch, batch->state.action_frame[idx], &batch->state.speed_air_x_self[idx],
                &batch->state.speed_y_self[idx]);
          }
        } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                    (uint8_t)MSL_FX_KIND_SPECIAL_HI_BOUND) &&
                   batch->state.on_ground[idx] == 0) {
          // Decomp: airborne Firefox/Firebird rebound Phys runs `ft_800851C0` (vertical
          // self-velocity from `fp->x6A4_transNOffset.y`) and then `ftCommon_8007CF58`
          // (horizontal air friction/clamp via x74_anim_vel.x). This keeps rebound rows
          // root-motion-owned while airborne instead of continuing with the pre-bound downward
          // launch velocity.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiBound_Phys
          // refs/melee/src/melee/ft/ft_081B.c::ft_800851C0
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58
          const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
          if (ch != NULL) {
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              batch->state.speed_y_self[idx] = dxyz[1];
              if (dxyz[1] > ch->terminal_vel && batch->state.jumps_left[idx] == 0u) {
                // SpecialHiBound's rebound motion remains GA_Air, but source rebound common-air
                // bookkeeping has refreshed x1968_jumpsUsed to one before the visible rebound
                // launch TransN impulse. Slippi records the inverse `jumps_left`, so the first
                // upward root-motion tick large enough to exceed the character's ordinary fall
                // terminal velocity publishes max_jumps - 1 instead of leaving the launch-consumed
                // zero-jump state in place; tiny floor-bias/root settle deltas remain unchanged.
                // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                //   ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Phys}
                batch->state.jumps_left[idx] =
                    (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
              }
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
      // - validation replay-buffer seed derivation
      //   (velocities.knockback_{x,y} -> speed_{x,y}_attack)
      //
      // We include `speed_*_attack` in position integration (because it affects where the character is this frame),
      // but we exclude it from gravity/fastfall updates (which operate on `self_vel` only; knockback has its own
      // separate decay/physics paths in-engine, and is currently teacher-forced from the seed).
      const uint8_t downbound_ground_phys_before_floor_loss =
          (uint8_t)(batch->state.frame_start_on_ground[idx] != 0u && on_ground == 0u &&
                    (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
                     action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D));
      const uint8_t ground_phys_for_frame =
          (uint8_t)(on_ground || downbound_ground_phys_before_floor_loss);
      float vx_self = ground_phys_for_frame ? batch->state.speed_ground_x_self[idx]
                                            : batch->state.speed_air_x_self[idx];
      float grounded_self_vel_y_for_frame = 0.0f;
      uint8_t use_grounded_self_vel_y_for_frame = 0u;
      if (ground_phys_for_frame) {
        // Grounded locomotion velocity update (single writer):
        // - Decomp: Phys callbacks like ft_80084F3C/ftWalkCommon_800E0060/ftCo_Dash_Phys/ftCo_Run_Phys
        //   compute a ground accel/friction step via ftCommon_ApplyFrictionGround or ftCommon_8007C98C.
        // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
        // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800E0060
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Phys
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Phys
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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
          if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
               (uint8_t)MSL_FX_KIND_SPECIAL_S)) {
            float dxyz[3];
            if (physics_try_get_transn_delta_xyz(ch, batch->state.char_id[idx],
                                                 batch->state.animation_index[idx],
                                                 physics_prev_anim_frame_f32(batch, idx),
                                                 physics_cur_anim_frame_f32(batch, idx), dxyz)) {
              gr_vel = dxyz[2] * facing_dir;
            }
          } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                      (uint8_t)MSL_FX_KIND_SPECIAL_S_END)) {
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
          } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                      (uint8_t)MSL_FX_KIND_SPECIAL_HI)) {
            // Decomp: ftFx_SpecialHi_Phys increments `mv.fx.SpecialHi.unk`, then applies ground
            // reverse friction x78 once `unk >= x70`.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHi_Phys
            if ((int16_t)(batch->state.action_frame[idx] + 1) >=
                (int16_t)ch->firefox_launch_reverse_accel_start_frames) {
              gr_vel += ground_friction_step_delta(gr_vel, ch->firefox_launch_reverse_accel);
            }
          } else if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
                     (action_id >= (uint16_t)MSL_ACT_CA_SPECIAL_HI &&
                      action_id <= (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW)) {
            // Falcon Dive grounded rows (Special(Air)Hi wind-up before the script's
            // set_airborne_state@14, plus transient HiCatch/Throw0 grounded frames): none of
            // the source Phys callbacks touch gr_vel or the ground accel lanes, so NO ground
            // friction applies. Special(Air)Hi_Phys additionally owns the frame's projected
            // self velocity (TransN rebase + mv.ca.specialhi.vel), which the falcon phys hook
            // has already written into the air-x lane.
            // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_Phys,
            //   ftCa_SpecialHiCatch_Phys,ftCa_SpecialHiThrow0_Phys}
            if (action_id == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                action_id == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI) {
              grounded_self_vel_for_frame = batch->state.speed_air_x_self[idx];
              grounded_self_vel_y_for_frame = batch->state.speed_y_self[idx];
              use_grounded_self_vel_for_frame = 1u;
              use_grounded_self_vel_y_for_frame = 1u;
            }
          } else if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
                     (action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
                      action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END ||
                      action_id == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END ||
                      action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR)) {
            // Falcon Kick grounded Phys family. gr_vel stays unscaled (source
            // Inline_Friction scales the frame's projected self_vel AFTER
            // ApplyGroundMovement, not fp->gr_vel), so the on-hit slowdown rides
            // grounded_self_vel_for_frame.
            // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
            //   ftCa_SpecialLw_Phys,ftCa_SpecialLwEnd_Phys,ftCa_SpecialAirLwEnd_Phys,
            //   ftCa_SpecialLwEndAir_Phys,ftCa_Special_Inline_Friction}
            // refs/melee/src/melee/ft/ft_084E.c::{ft_80085088,ft_800850E0,ft_80084F3C}
            const uint16_t fc_msid = falcon_special_submotion(action_id);
            const float fc_frame = physics_cur_anim_frame_f32(batch, idx);
            uint8_t scaled = 0u;
            if (action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
                action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR) {
              // ft_80085088 -> ft_800850E0: TransN root motion when the extracted x10_b0 flag
              // is set for the motion, plain gr_friction step otherwise (no high-speed mul).
              float dxyz[3];
              if (physics_action_anim_uses_root_motion(batch->state.char_id[idx],
                                                       batch->state.animation_index[idx]) &&
                  physics_try_get_transn_delta_xyz(
                      ch, batch->state.char_id[idx], batch->state.animation_index[idx],
                      physics_prev_anim_frame_f32(batch, idx), fc_frame, dxyz)) {
                gr_vel = dxyz[2] * facing_dir;
              } else {
                gr_vel += ground_friction_step_delta(gr_vel, ch->gr_friction);
              }
              scaled = (uint8_t)(action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW);
            } else {
              // LwEnd (358) / AirLwEnd (360): the script's cmd_vars[2] window selects the
              // per-family traction friction; otherwise ft_80084F3C (high-speed mul).
              // data/moves/falcon.json::specials_by_msid.{312,314} set_cmd_var(idx=2)
              if (move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], fc_msid,
                                                                2u, fc_frame) != 0u) {
                const float traction = (action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END)
                                           ? ch->falcon_speciallw_ground_traction
                                           : ch->falcon_speciallw_air_landing_traction;
                gr_vel += ground_friction_step_delta(gr_vel, traction * ch->gr_friction);
              } else {
                float friction = ch->gr_friction;
                if (msl_absf(gr_vel) > ch->walk_max_vel) {
                  friction *= c->high_speed_friction_mul;
                }
                gr_vel += ground_friction_step_delta(gr_vel, friction);
              }
              scaled = (uint8_t)(action_id == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END);
            }
            if (scaled) {
              const float fc_f = batch->state.falcon_speciallw_friction[idx];
              grounded_self_vel_for_frame = gr_vel * ((fc_f > 0.0f) ? fc_f : 1.0f);
              use_grounded_self_vel_for_frame = 1u;
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
          } else if (physics_action_is_common_ground_friction_only(batch->state.char_id[idx],
                                                                   action_id) ||
                     physics_action_is_grounded_common_damage_phys(action_id)) {
            if ((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
                 action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
                (physics_action_is_damage_fly(batch->state.seed_prev_action_id[idx]) ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_FALL)) {
              // DownBound has already run its source Phys callback in knockdown_update_pre_physics.
              // On the first frame after DamageFly_Coll or DamageFall_Coll enters DownBound,
              // routing the row through this generic ground-friction bucket would apply
              // ft_80084F3C twice.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
              //   ftCo_DamageFly_Coll,ftCo_80090184}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{
              //   ftCo_DamageFall_Coll,ftCo_80090984}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Phys
            } else if (action_id == (uint16_t)MSL_ACT_REBOUND &&
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
              const float jump_stick_y = apply_deadzone(
                  stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
              const float prev_jump_stick_y = apply_deadzone(
                  stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
              const uint8_t dash_frame2_jump_edge =
                  ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_XY) != 0u ||
                   (prev_jump_stick_y < c->tap_jump_threshold &&
                    jump_stick_y >= c->tap_jump_threshold))
                      ? 1u
                      : 0u;
              if (action_id == (uint16_t)MSL_ACT_KNEE_BEND &&
                  batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DASH &&
                  batch->state.seed_prev_action_frame[idx] == 1 &&
                  batch->state.action_frame[idx] <= 0 && dash_frame2_jump_edge != 0u) {
                // Dash frame-2 XY jump -> KneeBend source velocity handoff:
                // `ftCo_Dash_IASA` reaches `fn_800CAF78` from the early Dash branch after Dash
                // Phys has already owned the terminal `gr_vel`. The destination `KneeBend_Phys`
                // applies `ft_80084F3C` to that bounded source speed on the same callback. Keep
                // this to the frame-2 jump edge; later Dash, Run, and generic first-frame KneeBend
                // rows retain the ordinary friction path.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_IASA,ftCo_Dash_Phys}
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
                const float terminal = ch->dash_run_terminal_velocity;
                if (gr_vel > terminal) {
                  gr_vel = terminal;
                } else if (gr_vel < -terminal) {
                  gr_vel = -terminal;
                }
              }
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
            if (prev_action_id != (uint16_t)MSL_ACT_DASH ||
                batch->state.dash_entered_this_frame[idx] != 0u) {
              // Dash_IASA can call ftCo_Dash_Enter(gobj, 1) from an existing Dash. The action id
              // does not change, but source still seeds mv.co.dash.x0 and ftCo_Dash_Phys consumes
              // the entry lane before Fighter_procUpdate applies xE8 to post-frame gr_vel.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
              //   ftCo_Dash_CheckInput,ftCo_Dash_Enter,ftCo_Dash_Phys}
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
              batch->state.dash_entered_this_frame[idx] = 0u;
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
          } else if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                      (uint8_t)MSL_FX_KIND_SPECIAL_HI_LANDING)) {
            // Grounded Firefox/Firebird end Phys applies the character x7C ground momentum
            // friction, then calls the common ground movement helper. Since x7C is greater than
            // the small landing gr_vels in this family, the next frame often clears gr_vel to 0.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Phys
            // refs/melee/src/melee/ft/ftcommon.c::{
            //   ftCommon_ApplyFrictionGround,ftCommon_ApplyGroundMovement}
            gr_vel += ground_friction_step_delta(gr_vel, ch->firefox_ground_momentum_end);
          }

          batch->state.speed_ground_x_self[idx] = gr_vel;
          vx_self = use_grounded_self_vel_for_frame ? grounded_self_vel_for_frame : gr_vel;
        }
      }
      if (ground_phys_for_frame) {
        // Decomp: grounded movement helpers always run ftCommon_ApplyGroundMovement, which projects
        // scalar gr_vel onto CollData.floor.normal before Fighter_procUpdate integrates position.
        // Our `speed_ground_x_self` lane is fp->gr_vel; `speed_air_x_self` / `speed_y_self` are the
        // world-space fp->self_vel components. The floor normal is seeded from CollData.floor.index
        // and refreshed by mpColl after the frame's floor pass.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80085030,ft_800850E0}
        //
        // DownBound nuance:
        // - ftCo_DownBound_Phys runs `ft_80084F3C` before ftCo_DownBound_Coll can publish a
        //   ground-to-air floor-loss result through `ft_80082708`.
        // - The frame's self-velocity has already been projected from `gr_vel`, so do not fall
        //   back to stale airborne self_vel.x just because the post-collision row is GA_Air.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Phys,ftCo_DownBound_Coll}
        float floor_nx = batch->state.ground_normal_x[idx];
        float floor_ny = batch->state.ground_normal_y[idx];
        if ((action_id == (uint16_t)MSL_ACT_LANDING ||
             action_id == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) &&
            batch->state.ground_id[idx] != 0xFFFFu) {
          float stage_floor_nx = 0.0f;
          float stage_floor_ny = 1.0f;
          if (physics_try_stage_floor_normal_for_current_line(batch, bi, idx, &stage_floor_nx,
                                                              &stage_floor_ny) &&
              (fabsf(stage_floor_nx - floor_nx) > 0.000001f ||
               fabsf(stage_floor_ny - floor_ny) > 0.000001f)) {
            // Basic Landing and LandingFallSpecial share the common grounded Phys callback. Source
            // projects `fp->gr_vel` through the current CollData.floor.normal during
            // `ftCommon_ApplyGroundMovement`; when replay reseed exposes the current floor id
            // before the matching normal lane, use the generated MSLSTG01 line normal for that
            // same current floor.
            // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_LandingFallSpecial
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Phys
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
            // data/stages/bin/*.bin::MSLSTG01 floor endpoints
            floor_nx = stage_floor_nx;
            floor_ny = stage_floor_ny;
          }
        }
        if (floor_nx == 0.0f && floor_ny == 0.0f) {
          floor_ny = 1.0f;
        }
        const float vx_world = floor_ny * vx_self;
        const float vy_world =
            use_grounded_self_vel_y_for_frame ? grounded_self_vel_y_for_frame : -floor_nx * vx_self;
        batch->state.speed_air_x_self[idx] = vx_world;
        batch->state.speed_y_self[idx] = vy_world;
        vx_self = vx_world;
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
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        float shield_kb = batch->state.attacker_shield_ground_kb_vel[idx];
        if (ch != NULL && shield_kb != 0.0f) {
          const float friction = physics_ground_friction_mul_for_floor(batch, bi, idx) *
                                 ch->gr_friction * c->shield_attacker_ground_friction_mul;
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

      physics_apply_knockback_decay(batch, bi, idx, msl_char_params_fast(batch->state.char_id[idx]),
                                    c, action_id, on_ground);

      const float vy_self = batch->state.speed_y_self[idx];
      const float vx_kb = batch->state.speed_x_attack[idx];
      const float vy_kb = batch->state.speed_y_attack[idx];
      const uint8_t damagefly_terminal_ledge_endpoint_owner =
          physics_damagefly_hitlag_exit_terminal_ledge_endpoint_owner(batch, idx);
      const uint8_t sheik_vanish_start1_platform_entry =
          (uint8_t)(batch->state.ground_id[idx] != 0xFFFFu &&
                    stage_collision_floor_line_is_platform(batch->state.stage_id[bi],
                                                           batch->state.ground_id[idx]) != 0u);
      // Sheik grounded Vanish Start0 -> AirHiStart1 soft-platform source handoff:
      // ftSk_SpecialAirHiStart_0_Coll can enter travel from a ground/platform callback through
      // ftSk_SpecialHi_80113390 / ftSk_SpecialHi_80113A30. The entry frame publishes the newly
      // computed travel self_vel and applies horizontal displacement, while platform floor-loss
      // keeps the vertical root on the callback floor until the following travel frame. Main-floor
      // upward travel integrates vertical displacement immediately.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
      //   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialHi_80113390,ftSk_SpecialHi_80113A30}
      // data/stages/*.bin::MSLSTG01 floor line platform flags
      const uint8_t sheik_vanish_ground_start_to_air_travel_vertical_defer =
          (uint8_t)(batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
                    action_id == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1 &&
                    batch->state.frame_start_action_id[idx] ==
                        (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_0 &&
                    batch->state.frame_start_on_ground[idx] != 0u &&
                    sheik_vanish_start1_platform_entry != 0u);
      // Grounded Falcon Dive wind-up publishes SpecialHi_Phys' world self_vel.y from the TransN
      // root delta, but the grounded collision root stays floor-clamped until the script's
      // set_airborne_state handoff. Do not integrate that visible Y lane into cur_pos while the
      // fighter is still grounded.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_Phys,
      //   ftCa_SpecialHi_Coll}
      // data/moves/falcon.json::specials_by_msid.{307,308} set_airborne_state@14
      const uint8_t falcon_specialhi_ground_vertical_defer =
          (uint8_t)(batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
                    (action_id == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                     action_id == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI) &&
                    on_ground != 0u);
      // Position integration uses the (possibly-updated) self velocity plus the separate knockback
      // velocity term, matching GALE01 `Fighter_procUpdate` integration shape.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      batch->state.pos_x[idx] += vx_self;
      if (!damagefly_terminal_ledge_endpoint_owner) {
        batch->state.pos_x[idx] += vx_kb;
      }
      if (!sheik_vanish_ground_start_to_air_travel_vertical_defer &&
          !falcon_specialhi_ground_vertical_defer) {
        batch->state.pos_y[idx] += vy_self;
      }
      if (!damagefly_terminal_ledge_endpoint_owner &&
          !sheik_vanish_ground_start_to_air_travel_vertical_defer &&
          !falcon_specialhi_ground_vertical_defer) {
        batch->state.pos_y[idx] += vy_kb;
      }
      batch->state.pos_x[idx] += atk_shield_kb_x;
      batch->state.pos_y[idx] += atk_shield_kb_y;
      physics_apply_combo_push_timer(batch, c, idx);

      // Post-integration gravity update for states we intentionally keep "seed-driven" for current
      // frame displacement (notably DamageFall; see helper docs above).
      if (!on_ground && !physics_is_match_flow_airborne(action_id) &&
          physics_action_use_post_integration_common_air_gravity(batch, idx, action_id) &&
          !physics_action_use_pre_integration_common_air_gravity(batch, bi, idx, action_id) &&
          !physics_damagefall_entry_uses_pre_integration_phys(
              action_id, batch->state.prev_action_id[idx], action_frame)) {
        const MslCharParams* phys = msl_char_params_fast(batch->state.char_id[idx]);
        if (phys != NULL) {
          const uint8_t allow_fastfall =
              msl_action_allows_fastfall(batch->state.char_id[idx], action_id);
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
