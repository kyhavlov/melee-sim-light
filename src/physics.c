#include "physics.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"

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

static inline uint8_t physics_action_is_walk(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_WALK_SLOW || action_id == (uint16_t)MSL_ACT_WALK_MIDDLE ||
          action_id == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1
             : 0;
}

static inline uint8_t physics_action_is_common_ground_friction_only(uint16_t action_id) {
  // Decomp: these callbacks use `ft_80084F3C` (ground friction helper) in their Phys function.
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Phys
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_Phys
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_TURN:
    case MSL_ACT_TURN_RUN:
    case MSL_ACT_KNEE_BEND:
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

static inline uint8_t physics_action_is_shine_air(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
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
  if (!msl_action_allows_fastfall(action_id)) {
    return 0;
  }
  return (uint8_t)(action_id != (uint16_t)MSL_ACT_DAMAGE_FALL);
}

static inline uint8_t physics_action_use_post_integration_common_air_gravity(uint16_t action_id) {
  // Temporary v1 compatibility: update `speed_y_self` for next frame without affecting current
  // frame displacement (see note above).
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DAMAGE_FALL);
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
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Record pre-integration position for collision tests.
      // Ordering contract: stage_collision_apply() uses prev_pos_* captured here to perform
      // real "crossing" checks (pre vs post integration) without inferring prior position.
      batch->state.prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.prev_pos_y[idx] = batch->state.pos_y[idx];
      batch->state.prev_on_ground[idx] = batch->state.on_ground[idx] ? 1 : 0;

      // Hitlag freezes motion/physics advancement:
      // - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate runs its main integration block only
      //   under `if (!fp->x2219_b5)`.
      if (batch->state.hitlag[idx] != 0) {
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
              // Decomp: EscapeAir_Phys scales `self_vel` by `escapeair_decay` when cmd_skip_decay is
              // false; otherwise it calls `ft_80084DB0`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
              //
              // This sim currently does not model cmd_skip_decay, and always uses the decay path.
              batch->state.speed_air_x_self[idx] *= c->escapeair_decay;
              batch->state.speed_y_self[idx] *= c->escapeair_decay;
            } else if (physics_action_use_pre_integration_common_air_gravity(action_id)) {
              const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
              if (phys != NULL) {
                const uint8_t allow_fastfall = msl_action_allows_fastfall(action_id);

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
              } else if (physics_action_uses_common_air_drift(action_id)) {
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
          // Decomp: ftFx_SpecialAirSEnd_Phys applies air friction using ftFox_DatAttrs.x40.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Phys
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          if (ch != NULL) {
            batch->state.speed_air_x_self[idx] = air_apply_friction_step(
                batch->state.speed_air_x_self[idx], ch->illusion_air_friction);
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
              // Decomp: ft_80085030 "drive to TransN vel" via xE4 = transN_vel - gr_vel.
              // Setting gr_vel directly is equivalent after applying the accel step.
              // refs/melee/src/melee/ft/ft_081B.c::ft_80085030
              gr_vel = dxyz[2] * facing_dir;
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
            // Decomp: Dash entry initializes velocity in ftCo_Dash_Enter.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
            if (prev_action_id != (uint16_t)MSL_ACT_DASH) {
              gr_vel = facing_dir * ch->dash_initial_velocity;
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
          } else if (action_id == (uint16_t)MSL_ACT_RUN_BRAKE) {
            const float friction = ch->gr_friction * c->run_friction_mul;
            gr_vel += ground_friction_step_delta(gr_vel, friction);
          }

          batch->state.speed_ground_x_self[idx] = gr_vel;
          vx_self = gr_vel;
        }
      }
      const float vy_self = batch->state.speed_y_self[idx];
      const float vx = vx_self + batch->state.speed_x_attack[idx];
      const float vy_integrate = vy_self + batch->state.speed_y_attack[idx];

      // Position integration uses the (possibly-updated) self velocity plus the separate knockback
      // velocity term, matching GALE01 `Fighter_procUpdate` integration shape.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      batch->state.pos_x[idx] += vx;
      batch->state.pos_y[idx] += vy_integrate;

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
