#include "locomotion.h"

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"

// Input axes in MslStateSoA are Melee-legalized via ucf_clamp_stick_i8:
// ucf.h: clamp_stickMax = 80 (HSD_PadClampCheck3).
enum { MSL_STICK_MAX_I8 = 80 };

// ftCo_JumpInput (refs/melee/src/melee/ft/chara/ftCommon/forward.h).
typedef enum MslJumpInput {
  MSL_JUMP_INPUT_NONE = 0,
  MSL_JUMP_INPUT_LSTICK = 1,
  MSL_JUMP_INPUT_CSTICK = 2,
  MSL_JUMP_INPUT_XY = 3,
} MslJumpInput;

static inline float msl_absf(float x) { return x < 0.0f ? -x : x; }

static inline float msl_signf(float x) { return x < 0.0f ? -1.0f : 1.0f; }

static inline float stick_i8_to_unit(int8_t v) { return (float)v / (float)MSL_STICK_MAX_I8; }

static inline float apply_deadzone(float v, float dz) {
  if (msl_absf(v) < dz) {
    return 0.0f;
  }
  return v;
}

static float apply_friction_ground(float gr_vel, float friction) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround + ApplyGroundMovement
  float accel = friction;
  if (msl_absf(accel) > msl_absf(gr_vel)) {
    accel = -gr_vel;
  } else if (gr_vel > 0.0f) {
    accel = -accel;
  }
  return gr_vel + accel;
}

static float apply_ground_accel(float gr_vel, float accel, float target_vel, float friction,
                                float ground_max_horizontal_velocity) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007C98C + ApplyGroundMovement
  if (target_vel == 0.0f) {
    return apply_friction_ground(gr_vel, friction);
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
  return gr_vel + a;
}

static float apply_friction_air(float air_vel_x, float friction) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionAir + ApplyAirMovement
  float accel = friction;
  if (msl_absf(accel) >= msl_absf(air_vel_x)) {
    accel = -air_vel_x;
  } else if (air_vel_x > 0.0f) {
    accel = -accel;
  }
  return air_vel_x + accel;
}

static float apply_air_accel(float vel, float accel, float target_vel, float friction,
                             float air_max_horizontal_velocity) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D174 + ApplyAirMovement
  if (target_vel == 0.0f) {
    return apply_friction_air(vel, friction);
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

static inline uint8_t action_is_walk(uint16_t a) {
  return (a == MSL_ACT_WALK_SLOW || a == MSL_ACT_WALK_MIDDLE || a == MSL_ACT_WALK_FAST) ? 1 : 0;
}

static inline uint8_t action_is_fall_like(uint16_t a) {
  switch (a) {
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_ground_locomotion(uint16_t a) {
  if (a == MSL_ACT_WAIT || action_is_walk(a) || a == MSL_ACT_TURN || a == MSL_ACT_TURN_RUN ||
      a == MSL_ACT_DASH || a == MSL_ACT_RUN || a == MSL_ACT_RUN_BRAKE || a == MSL_ACT_KNEE_BEND ||
      a == MSL_ACT_LANDING || a == MSL_ACT_LANDING_FALL_SPECIAL) {
    return 1;
  }
  return 0;
}

static inline uint8_t action_is_air_locomotion(uint16_t a) {
  if (a == MSL_ACT_JUMP_F || a == MSL_ACT_JUMP_B || a == MSL_ACT_JUMP_AERIAL_F ||
      a == MSL_ACT_JUMP_AERIAL_B || action_is_fall_like(a) || a == MSL_ACT_FALL_SPECIAL ||
      a == MSL_ACT_FALL_SPECIAL_F || a == MSL_ACT_FALL_SPECIAL_B || a == MSL_ACT_DAMAGE_FALL) {
    return 1;
  }
  return 0;
}

static inline uint16_t walk_action_from_speed(const MslCommonParams* c, const MslCharParams* ch,
                                              float gr_vel) {
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
  const float v = msl_absf(gr_vel);
  if (v >= (c->walk_fast_vel_mul * ch->walk_max_vel)) {
    return MSL_ACT_WALK_FAST;
  }
  if (v >= (c->walk_mid_vel_mul * ch->walk_max_vel)) {
    return MSL_ACT_WALK_MIDDLE;
  }
  return MSL_ACT_WALK_SLOW;
}

static inline uint32_t anim_for_walk_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_WALK_FAST:
      return (uint32_t)MSL_SM_WALK_FAST;
    case MSL_ACT_WALK_MIDDLE:
      return (uint32_t)MSL_SM_WALK_MIDDLE;
    case MSL_ACT_WALK_SLOW:
    default:
      return (uint32_t)MSL_SM_WALK_SLOW;
  }
}

static inline uint16_t jump_action_from_stick(const MslCommonParams* c, float stick_x,
                                              float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
  // (lstick.x * facing_dir) > -p_ftCommonData->x78 ? JumpF : JumpB
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? MSL_ACT_JUMP_F : MSL_ACT_JUMP_B;
}

static inline uint16_t jump_aerial_action_from_stick(const MslCommonParams* c, float stick_x,
                                                     float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? MSL_ACT_JUMP_AERIAL_F
                                                            : MSL_ACT_JUMP_AERIAL_B;
}

static inline uint8_t is_dash_flick(const MslCommonParams* c, float stick_x, float prev_stick_x) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  // Note: vanilla also gates this via `x670_timer_lstick_tilt_x < p_ftCommonData->x40`.
  const float ax = msl_absf(stick_x);
  const float ap = msl_absf(prev_stick_x);
  return (ax >= c->dash_flick_abs && ap < c->dash_flick_abs) ? 1 : 0;
}

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, float prev_stick_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  // Note: vanilla gates this via `x671_timer_lstick_tilt_y < p_ftCommonData->x74`.
  // In teacher-forced one-step eval we do not seed `x671_timer_lstick_tilt_y`, so we approximate that
  // timer with a simple edge check.
  return (stick_y >= c->tap_jump_threshold && prev_stick_y < c->tap_jump_threshold) ? 1 : 0;
}

static inline MslJumpInput jump_input_from_edges(const MslCommonParams* c, uint16_t buttons_pressed,
                                                 float stick_y, float prev_stick_y) {
  if (buttons_pressed & (uint16_t)MSL_BUTTON_XY) {
    return MSL_JUMP_INPUT_XY;
  }
  if (did_tap_jump(c, stick_y, prev_stick_y)) {
    return MSL_JUMP_INPUT_LSTICK;
  }
  return MSL_JUMP_INPUT_NONE;
}

static inline uint32_t submotion_for_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_WAIT:
      return (uint32_t)MSL_SM_WAIT1_0;
    case MSL_ACT_WALK_SLOW:
      return (uint32_t)MSL_SM_WALK_SLOW;
    case MSL_ACT_WALK_MIDDLE:
      return (uint32_t)MSL_SM_WALK_MIDDLE;
    case MSL_ACT_WALK_FAST:
      return (uint32_t)MSL_SM_WALK_FAST;
    case MSL_ACT_TURN:
      return (uint32_t)MSL_SM_TURN;
    case MSL_ACT_TURN_RUN:
      return (uint32_t)MSL_SM_TURN_RUN;
    case MSL_ACT_DASH:
      return (uint32_t)MSL_SM_DASH;
    case MSL_ACT_RUN:
      return (uint32_t)MSL_SM_RUN;
    case MSL_ACT_RUN_BRAKE:
      return (uint32_t)MSL_SM_RUN_BRAKE;
    case MSL_ACT_KNEE_BEND:
      return (uint32_t)MSL_SM_KNEE_BEND;
    case MSL_ACT_JUMP_F:
      return (uint32_t)MSL_SM_JUMP_F;
    case MSL_ACT_JUMP_B:
      return (uint32_t)MSL_SM_JUMP_B;
    case MSL_ACT_JUMP_AERIAL_F:
      return (uint32_t)MSL_SM_JUMP_AERIAL_F;
    case MSL_ACT_JUMP_AERIAL_B:
      return (uint32_t)MSL_SM_JUMP_AERIAL_B;
    case MSL_ACT_FALL:
      return (uint32_t)MSL_SM_FALL;
    case MSL_ACT_FALL_F:
      return (uint32_t)MSL_SM_FALL_F;
    case MSL_ACT_FALL_B:
      return (uint32_t)MSL_SM_FALL_B;
    case MSL_ACT_FALL_AERIAL:
      return (uint32_t)MSL_SM_FALL_AERIAL;
    case MSL_ACT_FALL_AERIAL_F:
      return (uint32_t)MSL_SM_FALL_AERIAL_F;
    case MSL_ACT_FALL_AERIAL_B:
      return (uint32_t)MSL_SM_FALL_AERIAL_B;
    case MSL_ACT_FALL_SPECIAL:
      return (uint32_t)MSL_SM_FALL_SPECIAL;
    case MSL_ACT_FALL_SPECIAL_F:
      return (uint32_t)MSL_SM_FALL_SPECIAL_F;
    case MSL_ACT_FALL_SPECIAL_B:
      return (uint32_t)MSL_SM_FALL_SPECIAL_B;
    case MSL_ACT_DAMAGE_FALL:
      return (uint32_t)MSL_SM_DAMAGE_FALL;
    case MSL_ACT_LANDING:
      return (uint32_t)MSL_SM_LANDING;
    case MSL_ACT_LANDING_FALL_SPECIAL:
      return (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline void apply_air_drift(const MslCharParams* ch, float stick_x, float* io_air_x) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D28C + ApplyAirMovement
  if (io_air_x == NULL) {
    return;
  }
  float accel_scaling = stick_x * ch->air_drift_stick_mul;
  float accel_flat = stick_x > 0.0f ? +ch->aerial_drift_base : -ch->aerial_drift_base;
  const float target = stick_x * ch->air_drift_max;
  *io_air_x = apply_air_accel(*io_air_x, accel_scaling + accel_flat, target, ch->aerial_friction,
                              ch->air_max_horizontal_velocity);
}

static inline uint8_t should_fastfall(const MslCommonParams* c, float stick_y, float prev_stick_y,
                                      float vy) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast (tilt timer omitted; edge only)
  if (!(vy < 0.0f)) {
    return 0;
  }
  if (!(stick_y <= -c->fastfall_stick_threshold && prev_stick_y > -c->fastfall_stick_threshold)) {
    return 0;
  }
  return 1;
}

void locomotion_update_pre(MslBatch* batch) {
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

      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      // Advance anim frame (simple +1; Slippi state_age is floored).
      // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 gates `ftAnim_8006EBA4(gobj)` under
      //   `if (!fp->x2219_b5)`.
      const int16_t af = batch->state.action_frame[idx];
      if (af < INT16_MAX) {
        batch->state.action_frame[idx] = (int16_t)(af + 1);
      }

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      float stick_x =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      float prev_stick_x = apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]),
                                          c->lstick_deadzone_x);
      float prev_stick_y = apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]),
                                          c->lstick_deadzone_y);
      float cstick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);

      const uint16_t buttons = batch->state.input_buttons[idx];
      const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];

      const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

      uint16_t action_id = batch->state.action_id[idx];

      // -------------------------
      // Ground locomotion updates
      // -------------------------
      if (on_ground) {
        // Clear KneeBend-only internals when not in KneeBend.
        if (action_id != MSL_ACT_KNEE_BEND) {
          batch->state.kneebend_jump_input[idx] = 0;
          batch->state.kneebend_is_short_hop[idx] = 0;
        }
        if (action_id != MSL_ACT_TURN && action_id != MSL_ACT_TURN_RUN) {
          batch->state.turn_has_turned[idx] = 0;
        }

        // WAIT entry transitions (minimal locomotion-only IASA chain):
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        //   - ftCo_Jump_CheckInput
        //   - ftCo_Dash_CheckInput
        //   - ftCo_Turn_CheckInput
        //   - ftCo_Walk_CheckInput
        if (action_id == MSL_ACT_WAIT) {
          // Jump -> KneeBend.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
          const MslJumpInput j_in = jump_input_from_edges(c, buttons_pressed, stick_y, prev_stick_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            batch->state.action_frame[idx] = 0;
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (is_dash_flick(c, stick_x, prev_stick_x)) {
            // Dash flick.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
            if ((stick_x * facing_dir) < 0.0f) {
              // Dash flick opposite-facing triggers Turn (smash-turn path in vanilla).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:41-43 (ftCo_Turn_Enter_Smash)
              // Smash-turn flips facing immediately (`frames_to_turn = 0` in ftCo_Turn_Enter_Smash).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:175 (ftCo_Turn_Enter_Smash)
              batch->state.facing[idx] = batch->state.facing[idx] ? 0 : 1;
              batch->state.turn_has_turned[idx] = 1;
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
              batch->state.action_frame[idx] = 0;
              action_id = (uint16_t)MSL_ACT_TURN;
            } else {
              // Enter Dash.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter (init_vel)
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
              batch->state.action_frame[idx] = 0;
              batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
              action_id = (uint16_t)MSL_ACT_DASH;
            }
          } else if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
            // Turn.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_CheckInput
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
          batch->state.action_frame[idx] = 0;
          batch->state.turn_has_turned[idx] = 0;
          action_id = (uint16_t)MSL_ACT_TURN;
        } else if (msl_absf(stick_x) >= c->walk_stick_threshold) {
            // Walk.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
            const uint16_t want = walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
            batch->state.action_id[idx] = want;
            batch->state.animation_index[idx] = anim_for_walk_action(want);
            batch->state.action_frame[idx] = 0;
            action_id = want;
          }
        }

        // Turn frame: flip facing once at turn_frames.
        if (action_id == MSL_ACT_TURN || action_id == MSL_ACT_TURN_RUN) {
          if (!batch->state.turn_has_turned[idx] &&
              (uint8_t)batch->state.action_frame[idx] == ch->turn_frames) {
            batch->state.facing[idx] = batch->state.facing[idx] ? 0 : 1;
            batch->state.turn_has_turned[idx] = 1;
          }
        }

        // Ground movement physics for specific states.
        if (action_id == MSL_ACT_WAIT || action_id == MSL_ACT_TURN ||
            action_id == MSL_ACT_TURN_RUN || action_id == MSL_ACT_KNEE_BEND ||
            action_id == MSL_ACT_LANDING || action_id == MSL_ACT_LANDING_FALL_SPECIAL) {
          float friction = ch->gr_friction;
          if (msl_absf(batch->state.speed_ground_x_self[idx]) > ch->walk_max_vel) {
            friction *= c->high_speed_friction_mul;
          }
          batch->state.speed_ground_x_self[idx] =
              apply_friction_ground(batch->state.speed_ground_x_self[idx], friction);
        } else if (action_is_walk(action_id)) {
          // Walk accel.
          // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800E0060 (accel + ftCommon_8007C98C)
          const float accel_mul = 1.0f;
          float accel = stick_x * ch->walk_init_vel * accel_mul;
          if (stick_x != 0.0f) {
            accel += (stick_x > 0.0f ? +ch->walk_accel : -ch->walk_accel) * accel_mul;
          }
          const float target = stick_x * ch->walk_max_vel * accel_mul;
          if (target != 0.0f) {
            const float mult = batch->state.speed_ground_x_self[idx] / target;
            if (mult > 0.0f && mult < 1.0f) {
              accel *= (1.0f - mult) * c->walk_accel_scale_mul;
            }
          }
          batch->state.speed_ground_x_self[idx] =
              apply_ground_accel(batch->state.speed_ground_x_self[idx], accel, target,
                                 ch->gr_friction, ch->ground_max_horizontal_velocity);

          // Exit walk if stick released.
          if (msl_absf(stick_x) < c->walk_stick_threshold) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            // Leave action_frame as-is; Wait loops.
            action_id = (uint16_t)MSL_ACT_WAIT;
          } else {
            // Update walk type without resetting action_frame (ftWalkCommon_800DFEC8 keeps phase).
            const uint16_t want =
                walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
            if (want != action_id) {
              batch->state.action_id[idx] = want;
              batch->state.animation_index[idx] = anim_for_walk_action(want);
              action_id = want;
            }
          }
        } else if (action_id == MSL_ACT_DASH || action_id == MSL_ACT_RUN ||
                   action_id == MSL_ACT_RUN_BRAKE) {
          // Dash/run accel: refs/melee/src/melee/ft/inlines.h::getAccelAndTarget + ftCommon_8007C98C
          const float accel =
              stick_x * ch->dash_run_acceleration_a +
              (stick_x > 0.0f ? +ch->dash_run_acceleration_b : -ch->dash_run_acceleration_b);
          const float target = stick_x * ch->dash_run_terminal_velocity;
          const float friction = ch->gr_friction * c->run_friction_mul;
          batch->state.speed_ground_x_self[idx] =
              apply_ground_accel(batch->state.speed_ground_x_self[idx], accel, target, friction,
                                 ch->ground_max_horizontal_velocity);

          // Run braking if stick released.
          if (action_id == MSL_ACT_RUN && (stick_x * facing_dir) < c->run_stick_x_threshold) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_RUN_BRAKE;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_RUN_BRAKE;
            batch->state.action_frame[idx] = 0;
            action_id = (uint16_t)MSL_ACT_RUN_BRAKE;
          }
        }

        // KneeBend -> Jump
        if (action_id == MSL_ACT_KNEE_BEND) {
          // Latch short hop state (ftCo_KneeBend_Check_ShortHop).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:46
          if (!batch->state.kneebend_jump_input[idx]) {
            // Best-effort teacher-forcing reseed:
            // If we are reseeded mid-KneeBend (jump squat), we do not have the entry-frame history needed
            // to know the original JumpInput source (XY vs tap-jump). We infer it to approximate
            // `ftCo_KneeBend_Check_ShortHop` behavior. This is not a decomp-backed engine state; it is a
            // known limitation of one-step teacher forcing.
            const uint16_t prev_buttons = batch->state.prev_input_buttons[idx];
            if (prev_buttons & (uint16_t)MSL_BUTTON_XY) {
              batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_XY;
            } else if (did_tap_jump(c, stick_y, prev_stick_y)) {
              batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_LSTICK;
            } else if (buttons & (uint16_t)MSL_BUTTON_XY) {
              batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_XY;
            } else {
              batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_LSTICK;
            }
          }
          if (!batch->state.kneebend_is_short_hop[idx]) {
            const uint8_t j_in = batch->state.kneebend_jump_input[idx];
            if (j_in == (uint8_t)MSL_JUMP_INPUT_XY) {
              if (!(buttons & (uint16_t)MSL_BUTTON_XY)) {
                batch->state.kneebend_is_short_hop[idx] = 1;
              }
            } else if (j_in == (uint8_t)MSL_JUMP_INPUT_LSTICK) {
              if (stick_y < c->tap_jump_release_threshold) {
                batch->state.kneebend_is_short_hop[idx] = 1;
              }
            } else if (j_in == (uint8_t)MSL_JUMP_INPUT_CSTICK) {
              if (cstick_y < c->tap_jump_release_threshold) {
                batch->state.kneebend_is_short_hop[idx] = 1;
              }
            }
          }

          if ((uint8_t)batch->state.action_frame[idx] >= ch->jump_startup_frames) {
            const uint8_t is_short = batch->state.kneebend_is_short_hop[idx] ? 1 : 0;
            const uint8_t full = (uint8_t)(!is_short);

            const uint16_t jump_act = jump_action_from_stick(c, stick_x, facing_dir);
            batch->state.action_id[idx] = jump_act;
            batch->state.animation_index[idx] = (uint32_t)submotion_for_action(jump_act);
            batch->state.action_frame[idx] = 0;
            batch->state.on_ground[idx] = 0;

            // Ground-to-air momentum + jump impulse (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110)
            const float base_x =
                batch->state.speed_ground_x_self[idx] * ch->ground_to_air_jump_momentum_multiplier;
            float h_vel = base_x + stick_x * ch->jump_h_initial_velocity;
            const float h_max = ch->jump_h_max_velocity;
            if (msl_absf(h_vel) > h_max) {
              h_vel = msl_signf(h_vel) * h_max;
            }
            batch->state.speed_air_x_self[idx] = h_vel;
            batch->state.speed_ground_x_self[idx] = 0.0f;
            batch->state.speed_y_self[idx] =
                full ? ch->jump_v_initial_velocity : ch->hop_v_initial_velocity;

            // Consume ground jump.
            if (batch->state.jumps_left[idx] > 0) {
              batch->state.jumps_left[idx]--;
            }
            action_id = jump_act;
          }
        }

        // Dash -> Run/Wait based on animation end + stick.
        if (action_id == MSL_ACT_DASH) {
          const uint32_t anim = batch->state.animation_index[idx];
          if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
            const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim);
            if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
              const float stick_f = stick_x * facing_dir;
              if (stick_f >= c->run_stick_x_threshold) {
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_RUN;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_RUN;
                batch->state.action_frame[idx] = 0;
              } else if (msl_absf(stick_x) >= c->walk_stick_threshold) {
                const uint16_t want =
                    walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
                batch->state.action_id[idx] = want;
                batch->state.animation_index[idx] = anim_for_walk_action(want);
                batch->state.action_frame[idx] = 1;
              } else {
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
                // Wait loops; keep action_frame.
              }
            }
          }
        }

        // RunBrake -> Wait when animation ends.
        if (action_id == MSL_ACT_RUN_BRAKE) {
          const uint32_t anim = batch->state.animation_index[idx];
          if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
            const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim);
            if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
              // Wait loops.
            }
          }
        }

        continue;
      }

      // ----------------------
      // Air locomotion updates
      // ----------------------
      if (!msl_action_is_air_locomotion(action_id)) {
        continue;
      }

      // Aerial jump (double jump) entry.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
      if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) || did_tap_jump(c, stick_y, prev_stick_y)) {
        if (batch->state.jumps_left[idx] > 0 &&
            action_id != MSL_ACT_JUMP_AERIAL_F && action_id != MSL_ACT_JUMP_AERIAL_B) {
          const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
          batch->state.action_id[idx] = act;
          batch->state.animation_index[idx] = submotion_for_action(act);
          batch->state.action_frame[idx] = 0;
          batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
          batch->state.speed_y_self[idx] = ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
          batch->state.jumps_left[idx]--;
          action_id = act;
        }
      }

      // Air drift.
      apply_air_drift(ch, stick_x, &batch->state.speed_air_x_self[idx]);

      // Fastfall edge.
      if (should_fastfall(c, stick_y, prev_stick_y, batch->state.speed_y_self[idx])) {
        batch->state.speed_y_self[idx] = -ch->fast_fall_velocity;
      }

      // Jump -> Fall when animation ends (and for aerial jumps too).
      if (action_id == MSL_ACT_JUMP_F || action_id == MSL_ACT_JUMP_B ||
          action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B) {
        const uint32_t anim = batch->state.animation_index[idx];
        if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
          const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim);
          if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
            batch->state.action_frame[idx] = 0;
          }
        }
      }
    }
  }
}

void locomotion_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1 : 0;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1 : 0;

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];

      if (!was_ground && now_ground) {
        // Only implement landing state selection for locomotion air states for now.
        // For aerial attacks/specials/etc, the correct landing lag state depends on motion state tables
        // and additional internal flags we don't yet seed.
        if (!action_is_air_locomotion(a)) {
          continue;
        }

        // Landed this frame.
        // Transfer air X to ground X so friction/traction apply next frame.
        batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
        batch->state.speed_air_x_self[idx] = 0.0f;

        // Reset jumps on landing (common case for Fox/Falco).
        batch->state.jumps_left[idx] = ch->max_jumps;

        // Enter landing action based on current fall type.
        uint16_t land = (uint16_t)MSL_ACT_LANDING;
        if (a == MSL_ACT_FALL_SPECIAL || a == MSL_ACT_FALL_SPECIAL_F ||
            a == MSL_ACT_FALL_SPECIAL_B || a == MSL_ACT_LANDING_FALL_SPECIAL) {
          land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
        }

        batch->state.action_id[idx] = land;
        batch->state.animation_index[idx] = submotion_for_action(land);
        batch->state.action_frame[idx] = 0;
      } else if (was_ground && !now_ground) {
        // Only force Ground->Air transitions for ground locomotion states for now.
        // Many non-locomotion ground states transition into specific aerial variants (e.g. FallSpecial),
        // which we do not model yet; forcing Fall here causes large action_id regressions.
        if (!action_is_ground_locomotion(a)) {
          continue;
        }

        // Walked/ran off the ground: carry grounded X velocity into air.
        batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
        batch->state.speed_ground_x_self[idx] = 0.0f;

        // Ground locomotion -> Fall when no longer grounded.
        // Decomp refs for the common collision helpers:
        // - refs/melee/src/melee/ft/ft_081B.c:1066 (`ft_80084280`) (Wait/Walk/etc)
        // - refs/melee/src/melee/ft/ft_081B.c:1114 (`ft_800844EC`) (Dash/Run) which may enter StopWall via
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopWall.c:20 (`ftCo_8009EDA4`)
        //
        // Note: we don't yet model wall hug / StopWall, so we conservatively enter Fall here.
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
        batch->state.action_frame[idx] = 0;
      }
    }
  }
}
