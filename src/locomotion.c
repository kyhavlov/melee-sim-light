#include "locomotion.h"

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "action.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input.h"
#include "move_tables.h"
#include "input_axis.h"
#include "jump_input.h"

static inline float msl_signf(float x) { return x < 0.0f ? -1.0f : 1.0f; }

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  // Decomp gates on "frames remaining" (joint track remaining). In this sim we approximate via a
  // simple end-frame comparison on the sanitized float timebase.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline uint16_t landing_air_action_from_attackair(uint16_t a) {
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
      return (uint16_t)MSL_ACT_LANDING_AIR_N;
    case MSL_ACT_ATTACK_AIR_F:
      return (uint16_t)MSL_ACT_LANDING_AIR_F;
    case MSL_ACT_ATTACK_AIR_B:
      return (uint16_t)MSL_ACT_LANDING_AIR_B;
    case MSL_ACT_ATTACK_AIR_HI:
      return (uint16_t)MSL_ACT_LANDING_AIR_HI;
    case MSL_ACT_ATTACK_AIR_LW:
      return (uint16_t)MSL_ACT_LANDING_AIR_LW;
    default:
      return (uint16_t)MSL_ACT_LANDING;
  }
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
      a == MSL_ACT_LANDING || a == MSL_ACT_LANDING_FALL_SPECIAL || a == MSL_ACT_LANDING_AIR_N ||
      a == MSL_ACT_LANDING_AIR_F || a == MSL_ACT_LANDING_AIR_B || a == MSL_ACT_LANDING_AIR_HI ||
      a == MSL_ACT_LANDING_AIR_LW || a == MSL_ACT_ESCAPE_F || a == MSL_ACT_ESCAPE_B ||
      a == MSL_ACT_ESCAPE_N) {
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

static inline uint8_t action_is_attackair(uint16_t a) {
  return (a == MSL_ACT_ATTACK_AIR_N || a == MSL_ACT_ATTACK_AIR_F || a == MSL_ACT_ATTACK_AIR_B ||
          a == MSL_ACT_ATTACK_AIR_HI || a == MSL_ACT_ATTACK_AIR_LW)
             ? 1
             : 0;
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

static inline uint8_t is_dash_flick(const MslCommonParams* c, float stick_x, uint8_t tilt_timer_x) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  const float ax = msl_absf(stick_x);
  return (ax >= c->dash_flick_abs && tilt_timer_x < c->dash_flick_tilt_max_frames) ? 1 : 0;
}

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1 : 0;
}

static inline MslJumpInput jump_input_from_edges(const MslCommonParams* c, uint16_t buttons_pressed,
                                                 float stick_y, uint8_t tilt_timer_y) {
  if (buttons_pressed & (uint16_t)MSL_BUTTON_XY) {
    return MSL_JUMP_INPUT_XY;
  }
  if (did_tap_jump(c, stick_y, tilt_timer_y)) {
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
    case MSL_ACT_LANDING_AIR_N:
      return (uint32_t)MSL_SM_LANDING_AIR_N;
    case MSL_ACT_LANDING_AIR_F:
      return (uint32_t)MSL_SM_LANDING_AIR_F;
    case MSL_ACT_LANDING_AIR_B:
      return (uint32_t)MSL_SM_LANDING_AIR_B;
    case MSL_ACT_LANDING_AIR_HI:
      return (uint32_t)MSL_SM_LANDING_AIR_HI;
    case MSL_ACT_LANDING_AIR_LW:
      return (uint32_t)MSL_SM_LANDING_AIR_LW;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline void enter_landing_action_from_air(MslBatch* batch, const MslCharParams* ch,
                                                 size_t idx, uint16_t land_act) {
  if (batch == NULL || ch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();

  // Landed this frame.
  // Transfer air X to ground X so friction/traction apply next frame.
  batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
  batch->state.speed_air_x_self[idx] = 0.0f;

  batch->state.fall_fast[idx] = 0;

  // Jump refresh is tied to explicit landing-enter transitions only (not raw on_ground flips).
  //
  // Decomp: ftCommon_8007D6A4 sets `fp->x1968_jumpsUsed = 0` when the fighter becomes grounded,
  // which refreshes jumps remaining back to max_jumps.
  // refs/melee/src/melee/ft/ftcommon.c:556-573
  //
  // Slippi post-frame `jumps` is "jumps left" (see Recording/SendGamePostFrame.asm), so:
  // jumps_left = max_jumps - jumps_used.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  batch->state.jumps_left[idx] = ch->max_jumps;

  batch->state.action_id[idx] = land_act;
  batch->state.animation_index[idx] = submotion_for_action(land_act);

  // Decomp: Fighter_ChangeMotionState sets:
  // - fp->frame_speed_mul = anim_speed
  // - fp->cur_anim_frame = anim_start - fp->frame_speed_mul
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
  //
  // LandingAir additionally calls ftAnim_SetAnimRate to adjust fp->frame_speed_mul without
  // adjusting fp->cur_anim_frame. refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
  //
  // LandingFallSpecial passes a scaled anim_speed directly. refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
  const uint8_t cid = batch->state.char_id[idx];
  if (land_act == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    const float landing_lag = (c != NULL) ? c->landing_fall_special_lag_frames : 0.0f;
    const float end_frame = msl_anim_end_frame(cid, (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
    const float speed =
        (landing_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / landing_lag) : 1.0f;
    msl_anim_timebase_enter(batch, idx, 0.0f, speed);
  } else {
    // Most motion states enter with anim_speed=1.0 and anim_start=0.0.
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

    // LandingAir*: set anim rate so the timeline finishes in `lag` frames.
    if (land_act == (uint16_t)MSL_ACT_LANDING_AIR_N ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_F ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_B ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_HI ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_LW) {
      uint8_t lag_frames = 0;
      switch (land_act) {
        case (uint16_t)MSL_ACT_LANDING_AIR_N:
          lag_frames = ch->landing_airn_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_F:
          lag_frames = ch->landing_airf_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_B:
          lag_frames = ch->landing_airb_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_HI:
          lag_frames = ch->landing_airhi_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_LW:
          lag_frames = ch->landing_airlw_lag_frames;
          break;
        default:
          lag_frames = 0;
          break;
      }

      float lag = (float)lag_frames;
      uint8_t did_lcancel = 0;
      // Decomp: landing lag is divided when x67F < p_ftCommonData->xE4.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
      if (c != NULL && lag > 0.0f && batch->state.lr_press_timer[idx] < c->lcancel_window_frames) {
        did_lcancel = 1;
        const float div_lag = lag / c->lcancel_lag_div;
        int int_lag = (int)div_lag;
        if (int_lag == 0) {
          int_lag = 1;
        }
        lag = (float)int_lag;
      }

      // Slippi post-frame `l_cancel` is a 1-frame status emitted on LandingAir* entry:
      // - 0: not applicable / no lag landing.
      // - 1: successful L-cancel.
      // - 2: missed L-cancel.
      //
      // Decomp tie-down for the success condition: fp->x67F < p_ftCommonData->xE4.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
      // refs/melee/src/melee/ft/fighter.c:2078-2086 (x67F update; see src/input.c)
      batch->state.l_cancel[idx] = (lag_frames > 0) ? (uint8_t)(did_lcancel ? 1 : 2) : 0;

      const uint32_t sm = submotion_for_action(land_act);
      const float end_frame = (sm <= 0xFFFFu) ? msl_anim_end_frame(cid, (uint16_t)sm) : 0.0f;
      if (lag > 0.0f && end_frame > 0.0f) {
        const float rate = (end_frame + 0.1f) / lag;
        msl_anim_timebase_set_rate(batch, idx, rate);
      }
    }
  }
}

void locomotion_update_pre(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Decomp ordering: anim/script timebase advances before input callbacks.
  // In this sim, anim_timebase_update_pre_input() advances batch->state.anim_frame_f32 and
  // batch->state.action_frame earlier in the frame (see src/step.c).

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];

      float stick_x;
      float stick_y;
      float cstick_y;
      uint16_t buttons;
      uint16_t buttons_pressed;
      uint8_t tilt_timer_x;
      uint8_t tilt_timer_y;
	      uint8_t on_ground;
	      float facing_dir;
	      uint16_t action_id;
	      uint8_t turn_just_turned = 0;

      stick_x =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      cstick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);

      buttons = batch->state.input_buttons[idx];
      buttons_pressed = batch->state.input_buttons_pressed[idx];
      // x670/x671 are updated in src/input.c::input_apply (single source of truth). Locomotion only
      // applies per-action overrides (0xFE) later in the frame.
      tilt_timer_x = batch->state.tilt_timer_x[idx];
      tilt_timer_y = batch->state.tilt_timer_y[idx];

      on_ground = batch->state.on_ground[idx] ? 1 : 0;
      facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

      action_id = batch->state.action_id[idx];
      const uint16_t action_id_start = action_id;

      // -------------------------
      // Ground locomotion updates
      // -------------------------
      if (on_ground) {
        // Decomp: fall_fast is typically cleared on ground motion state changes unless KeepFastFall
        // is requested. Our locomotion model doesn't keep it across grounded frames.
        batch->state.fall_fast[idx] = 0;

        // Clear KneeBend-only internals when not in KneeBend.
        if (action_id != MSL_ACT_KNEE_BEND) {
          batch->state.kneebend_jump_input[idx] = 0;
          batch->state.kneebend_is_short_hop[idx] = 0;
        }
        if (action_id != MSL_ACT_TURN && action_id != MSL_ACT_TURN_RUN) {
          batch->state.turn_has_turned[idx] = 0;
          batch->state.turn_frames_to_turn[idx] = 0;
        }

        // Landing states -> Wait on completion (Anim step).
        //
        // Decomp references:
        // - LandingAir uses a scaled animation rate but shares the same anim-end gate:
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c (ftCo_LandingAir_Anim -> ftCo_Landing_Anim)
        // - LandingFallSpecial uses a scaled animation rate (p_ftCommonData->x344) and the same
        //   anim-end gate:
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c and ftCo_EscapeAir.c
        if (action_id == MSL_ACT_LANDING || action_id == MSL_ACT_LANDING_FALL_SPECIAL ||
            action_id == MSL_ACT_LANDING_AIR_N || action_id == MSL_ACT_LANDING_AIR_F ||
            action_id == MSL_ACT_LANDING_AIR_B || action_id == MSL_ACT_LANDING_AIR_HI ||
            action_id == MSL_ACT_LANDING_AIR_LW) {
          const uint32_t sm = submotion_for_action(action_id);
          if (sm <= 0xFFFFu && anim_finished(cid, (uint16_t)sm, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_WAIT;
          }
        }

	        // Turn: decomp `frames_to_turn` countdown + flip on 0 (Anim step).
	        // Only tick if Turn was already active at frame start (avoid flip on same-frame entry).
	        if (action_id_start == MSL_ACT_TURN || action_id_start == MSL_ACT_TURN_RUN) {
	          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 (ftCo_Turn_Anim_Inner)
	          if (batch->state.turn_frames_to_turn[idx] > 0) {
	            batch->state.turn_frames_to_turn[idx]--;
	          } else if (!batch->state.turn_has_turned[idx]) {
	            batch->state.turn_has_turned[idx] = 1;
	            turn_just_turned = 1;
	            batch->state.facing[idx] = batch->state.facing[idx] ? 0 : 1;
	            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
	          }
	        }

        // Guard core loop (entry/hold/exit). Keep this before locomotion IASA (e.g. Wait->Jump/Dash).
        // Decomp call site example: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c:43-66.
        uint8_t allow_guard_entry = 0;
        if (action_id == MSL_ACT_WAIT || action_is_walk(action_id) || action_id == MSL_ACT_TURN ||
            action_id == MSL_ACT_TURN_RUN || action_id == MSL_ACT_DASH ||
            action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_BRAKE ||
            action_id == MSL_ACT_RUN_DIRECT) {
          allow_guard_entry = 1;
        }
        // Landing IASA: allow guard only after the landing lag gate.
        // Decomp: ftCo_Landing_IASA gates interrupts on landing lag frames, then runs the common
        // grounded interrupt checks (including shield via ftCo_80091A4C).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
        if (action_id == MSL_ACT_LANDING &&
            batch->state.action_frame[idx] >= (int16_t)ch->landing_lag_frames) {
          allow_guard_entry = 1;
        }
        guard_update_grounded(batch, c, idx, allow_guard_entry);
        action_id = batch->state.action_id[idx];

        // Shield recharge after guard state updates/entry so we don't recharge on the same frame
        // we begin shielding (decomp gates on `!fp->x221A_b7`, not on pre-entry action_id).
        // refs/melee/src/melee/ft/fighter.c:2803-2812.
        guard_update_shield_recharge(batch, c, idx);

        // Escape actions (from shield): friction + end->Wait.
        // Keep this before other grounded IASA so Escape->Wait doesn't chain into Wait IASA
        // in the same frame.
        if (action_id == MSL_ACT_ESCAPE_N || action_id == MSL_ACT_ESCAPE_F ||
            action_id == MSL_ACT_ESCAPE_B) {
          escape_update_grounded(batch, c, ch, idx);
          continue;
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
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (is_dash_flick(c, stick_x, tilt_timer_x)) {
            // Dash flick.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
            if ((stick_x * facing_dir) < 0.0f) {
              // Dash flick opposite-facing triggers Turn (smash-turn path in vanilla).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:41-43 (ftCo_Turn_Enter_Smash)
              // Decomp:
              // - ftCo_Turn_Enter_Smash sets `frames_to_turn = 0.0f`.
              // - ftCo_Turn_Anim_Inner handles the actual flip based on that countdown.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 and :170-190
              batch->state.turn_has_turned[idx] = 0;
              batch->state.turn_frames_to_turn[idx] = 0;
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // Decomp: ftCo_Turn_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
              msl_anim_timebase_tick_once(batch, idx);
              action_id = (uint16_t)MSL_ACT_TURN;
            } else {
              // Enter Dash.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter (init_vel)
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // Decomp: ftCo_Dash_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
              msl_anim_timebase_tick_once(batch, idx);
              batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
              // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
              batch->state.tilt_timer_x[idx] = 0xFEu;
              action_id = (uint16_t)MSL_ACT_DASH;
            }
          } else if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
            // Turn.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_CheckInput
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            // Decomp: ftCo_Turn_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
            msl_anim_timebase_tick_once(batch, idx);
            action_id = (uint16_t)MSL_ACT_TURN;
          } else if (msl_absf(stick_x) >= c->walk_stick_threshold) {
            // Walk.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
            const uint16_t want =
                walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
            batch->state.action_id[idx] = want;
            batch->state.animation_index[idx] = anim_for_walk_action(want);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = want;
          }
        }

        // Landing IASA (minimal): after the landing lag gate, allow the same grounded locomotion
        // options we support from Wait (jump/dash/turn/walk), in the same relative order.
        //
        // Decomp: ftCo_Landing_IASA calls the common grounded interrupt checks after the lag gate.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
        if (action_id == MSL_ACT_LANDING &&
            batch->state.anim_frame_f32[idx] >= (float)ch->landing_lag_frames) {
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (is_dash_flick(c, stick_x, tilt_timer_x)) {
            // Dash flick.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
            if ((stick_x * facing_dir) < 0.0f) {
              // Dash flick opposite-facing triggers Turn (smash-turn path in vanilla).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:41-43 (ftCo_Turn_Enter_Smash)
              batch->state.turn_has_turned[idx] = 0;
              batch->state.turn_frames_to_turn[idx] = 0;
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
              msl_anim_timebase_tick_once(batch, idx);
              action_id = (uint16_t)MSL_ACT_TURN;
            } else {
              // Enter Dash.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter (init_vel)
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
              msl_anim_timebase_tick_once(batch, idx);
              batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
              // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
              batch->state.tilt_timer_x[idx] = 0xFEu;
              action_id = (uint16_t)MSL_ACT_DASH;
            }
          } else if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
            // Turn.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_CheckInput
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
            msl_anim_timebase_tick_once(batch, idx);
            action_id = (uint16_t)MSL_ACT_TURN;
          } else if (msl_absf(stick_x) >= c->walk_stick_threshold) {
            // Walk.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
            const uint16_t want =
                walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
            batch->state.action_id[idx] = want;
            batch->state.animation_index[idx] = anim_for_walk_action(want);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = want;
          }
        }

		        // Turn IASA (minimal): Jump and Dash.
		        //
		        // Decomp:
		        // - ftCo_Turn_IASA calls ftCo_Jump_CheckInput.
		        // - Then it runs a Turn->Dash gate via:
		        //   - fn_800C9C2C (sets mv.co.turn.x8 when a dash-flick toward mv.co.turn.facing_after
		        //     occurs within x40 frames), and
		        //   - (just_turned && x8) latch to enter Dash when the turn completes.
		        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
		        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
	        if ((action_id == MSL_ACT_TURN || action_id == MSL_ACT_TURN_RUN) &&
	            (action_id_start == MSL_ACT_TURN || action_id_start == MSL_ACT_TURN_RUN)) {
	          const MslJumpInput j_in =
	              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
	          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
	            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
	            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
	            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
	            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
	            batch->state.kneebend_is_short_hop[idx] = 0;
	            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
	          } else if (action_id == MSL_ACT_TURN) {
	            // UCF dashback patch (UCF 0.84): on AS_Turn anim frame 2, if vanilla x-smash conditions
	            // hold and the UCF xsmash intent heuristic passes, allow dashback.
	            //
	            // refs/ucf/src/dashback/dashback.cpp
	            // refs/ucf/include/ucf/pad_buffer.h::check_ucf_xsmash
	            // refs/ucf/include/melee/asm/player.h (.set Player.input.stick_x_hold_time, 0x670)
	            // Decomp timing:
	            // - x670 is updated in Fighter_Spaghetti_8006AD10 (prio 3).
	            //   refs/melee/src/melee/ft/fighter.c:1903-1955
	            // - The motion state's "input_cb" (IASA) is invoked afterward, in the same proc.
	            //   refs/melee/src/melee/ft/fighter.c:2117-2119
	            // So `tilt_timer_x` here corresponds to the post-input-update x670 value.
	            if (batch->config.ucf_enabled) {
	              const float af = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
	              if (af == 2.0f && msl_absf(stick_x) >= c->dash_flick_abs && tilt_timer_x < 2u &&
	                  msl_ucf_check_xsmash(&batch->state, idx)) {
	                const uint8_t face = (stick_x >= 0.0f) ? 1u : 0u;
	                batch->state.facing[idx] = face;
	                facing_dir = face ? 1.0f : -1.0f;
		                batch->state.turn_has_turned[idx] = 0;
		                batch->state.turn_frames_to_turn[idx] = 0;
		                batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
		                batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
		                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
		                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
		                msl_anim_timebase_tick_once(batch, idx);
		                batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
		                // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
		                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
		                batch->state.tilt_timer_x[idx] = 0xFEu;
		                action_id = (uint16_t)MSL_ACT_DASH;
	              }
	            }

		            // Vanilla Turn->Dash enter path (decomp-shaped).
		            //
		            // Decomp:
		            // - fn_800C9C2C sets mv.co.turn.x8 when:
		            //   (lstick.x * facing_after >= x3C) && (x670 < x40).
		            // - ftCo_Turn_IASA enters Dash when:
		            //   (just_turned && x8) && (lstick.x * facing_after >= x3C).
		            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
		            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
		            //
		            // Note: our seed schema does not include mv.co.turn.just_turned or mv.co.turn.x8.
		            // - `turn_just_turned` is tracked only when we flip has_turned in the Turn Anim
		            //   countdown (ftCo_Turn_Anim_Inner).
		            const float facing_after_dir =
		                batch->state.turn_has_turned[idx] ? facing_dir : -facing_dir;

		            const uint8_t effective_just_turned = turn_just_turned;

		            uint8_t x8_nonzero = 0;
		            if (batch->state.turn_frames_to_turn[idx] == 0) {
		              // Smash-turn entry sets mv.co.turn.x8 = facing (nonzero), without the x670<x40 gate.
		              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
		              x8_nonzero = 1;
		            } else if ((stick_x * facing_after_dir) >= c->dash_flick_abs &&
		                       tilt_timer_x < c->dash_flick_tilt_max_frames) {
		              // fn_800C9C2C would set mv.co.turn.x8 here.
		              x8_nonzero = 1;
		            }

		            if (effective_just_turned && x8_nonzero &&
		                (stick_x * facing_after_dir) >= c->dash_flick_abs) {
			              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
			              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
			              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
			              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
			              msl_anim_timebase_tick_once(batch, idx);
			              batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
			              // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
			              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
			              batch->state.tilt_timer_x[idx] = 0xFEu;
			              action_id = (uint16_t)MSL_ACT_DASH;
		            }
		          }
		        }

        // Walk IASA (minimal): Jump and Dash.
        //
        // Decomp: ftCo_Walk_IASA calls ftCo_Jump_CheckInput then ftCo_Dash_CheckInput.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start)) {
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (is_dash_flick(c, stick_x, tilt_timer_x)) {
            // Dash flick: forward -> Dash, backward -> Turn (smash-turn).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
            if ((stick_x * facing_dir) < 0.0f) {
	              batch->state.turn_has_turned[idx] = 0;
	              batch->state.turn_frames_to_turn[idx] = 0;
	              batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
	              batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
	              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
	              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
	              msl_anim_timebase_tick_once(batch, idx);
	              action_id = (uint16_t)MSL_ACT_TURN;
	            } else {
	              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
	              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
	              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
	              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
	              msl_anim_timebase_tick_once(batch, idx);
	              batch->state.speed_ground_x_self[idx] = facing_dir * ch->dash_initial_velocity;
	              batch->state.tilt_timer_x[idx] = 0xFEu;
	              action_id = (uint16_t)MSL_ACT_DASH;
	            }
          }
        }

        // Run IASA (minimal): Jump.
        //
        // Decomp: ftCo_Run_IASA/ftCo_RunDirect_IASA call fn_800CAF78 (Jump -> KneeBend).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
        if ((action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_DIRECT) &&
            action_id_start == action_id) {
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          }
        }

        // Dash IASA (dash-dance / dashback start): allow smash-turn from Dash on a flick opposite-facing.
        // Decomp:
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
        if (action_id == MSL_ACT_DASH && action_id_start == MSL_ACT_DASH) {
          // Dash -> KneeBend (Jump).
          //
          // Decomp: Dash IASA can enter KneeBend via fn_800CAF78.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else {
            const float cur_anim_frame = batch->state.anim_frame_f32[idx];
            if (cur_anim_frame <= c->dash_iasa_x4c) {
              if ((stick_x * facing_dir) < 0.0f && is_dash_flick(c, stick_x, tilt_timer_x)) {
                // Dash flick opposite-facing triggers Turn (smash-turn path in vanilla).
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:41-43 (ftCo_Turn_Enter_Smash)
                batch->state.turn_has_turned[idx] = 0;
                batch->state.turn_frames_to_turn[idx] = 0;
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
                msl_anim_timebase_tick_once(batch, idx);
                action_id = (uint16_t)MSL_ACT_TURN;
              }
            }

            // Dash -> Run when cmd_var[0] enables the late IASA chain and stick is held forward.
            //
            // Decomp:
            // - ftCo_Dash_IASA reaches a shared "block_42" chain which gates on fp->cmd_vars[0] then
            //   calls fn_800CA5F0 (Run enter check).
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA5F0
            //
            // Source of truth for fp->cmd_vars[0] timing:
            // - data/moves/{fox,falco}.json moves["ftCo_SM_Dash"]["events"] set_cmd_var(idx=0).
            if (action_id == MSL_ACT_DASH && move_tables_dash_cmd0_active(cid, cur_anim_frame)) {
              const float stick_f = stick_x * facing_dir;
              if (stick_f >= c->run_stick_x_threshold) {
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_RUN;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_RUN;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                action_id = (uint16_t)MSL_ACT_RUN;
              }
            }
          }
        }

        // Ground movement physics for specific states.
        if (action_id == MSL_ACT_WAIT || action_id == MSL_ACT_TURN ||
            action_id == MSL_ACT_TURN_RUN || action_id == MSL_ACT_KNEE_BEND ||
            action_id == MSL_ACT_LANDING || action_id == MSL_ACT_LANDING_FALL_SPECIAL ||
            action_id == MSL_ACT_LANDING_AIR_N || action_id == MSL_ACT_LANDING_AIR_F ||
            action_id == MSL_ACT_LANDING_AIR_B || action_id == MSL_ACT_LANDING_AIR_HI ||
            action_id == MSL_ACT_LANDING_AIR_LW || action_id == MSL_ACT_GUARD_ON ||
            action_id == MSL_ACT_GUARD || action_id == MSL_ACT_GUARD_OFF ||
            action_id == MSL_ACT_GUARD_SET_OFF || action_id == MSL_ACT_GUARD_REFLECT) {
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
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
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
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_RUN_BRAKE;
          }
        }

        // KneeBend -> Jump
        if (action_id == MSL_ACT_KNEE_BEND) {
          // Latch short hop state (ftCo_KneeBend_Check_ShortHop).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:46
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

          if (batch->state.action_frame[idx] >= (int16_t)ch->jump_startup_frames) {
            const uint8_t is_short = batch->state.kneebend_is_short_hop[idx] ? 1 : 0;
            const uint8_t full = (uint8_t)(!is_short);

            const uint16_t jump_act = jump_action_from_stick(c, stick_x, facing_dir);
            batch->state.action_id[idx] = jump_act;
            batch->state.animation_index[idx] = (uint32_t)submotion_for_action(jump_act);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
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
            // Decomp: fp->x671_timer_lstick_tilt_y = 0xFE;
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:148
            batch->state.tilt_timer_y[idx] = 0xFEu;
            tilt_timer_y = 0xFEu;

            // Consume ground jump by setting jumps_used = 1 on takeoff.
            //
            // Decomp:
            // - ftCo_KneeBend_Anim -> ftCo_Jump_Enter
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:22-38
            // - ftCo_Jump_Enter calls ftCommon_8007D5D4, which sets `fp->x1968_jumpsUsed = 1`
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:158-170
            //   refs/melee/src/melee/ft/ftcommon.c:525-535
            //
            // Slippi post-frame `jumps` is "jumps left", so: jumps_left = max_jumps - jumps_used.
            batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;
            action_id = jump_act;
          }
        }

        // Dash Anim end -> Wait.
        //
        // Decomp: ftCo_Dash_Anim enters ftCo_MS_Wait via ft_8008A2BC when no frames remain.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Anim
        // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
        if (action_id == MSL_ACT_DASH) {
          const uint32_t anim = batch->state.animation_index[idx];
          if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
            if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                              batch->state.anim_frame_f32[idx])) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              action_id = (uint16_t)MSL_ACT_WAIT;
            }
          }
        }

        // RunBrake -> Wait when animation ends.
        if (action_id == MSL_ACT_RUN_BRAKE) {
          const uint32_t anim = batch->state.animation_index[idx];
          if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
            if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                              batch->state.anim_frame_f32[idx])) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            }
          }
        }

        continue;
      }

      // Air / non-ground: shield recharge can still occur (decomp checks shield-active flag, not ground/air).
      guard_update_shield_recharge(batch, c, idx);

      // ----------------------
      // Air locomotion updates
      // ----------------------
      uint8_t is_air_loco = msl_action_is_air_locomotion(action_id) ? 1 : 0;
      uint8_t is_attack_air = action_is_attackair(action_id) ? 1 : 0;
      if (!is_air_loco && !is_attack_air && action_id != (uint16_t)MSL_ACT_ESCAPE_AIR) {
        continue;
      }

      // AttackAir Anim step runs before IASA in GALE01.
      //
      // Decomp:
      // - ftCo_AttackAir_Anim: if !ftAnim_IsFramesRemaining -> ftCo_Fall_Enter
      // - ftCo_AttackAir_IASA: gated by fp->allow_interrupt (script-driven)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
      if (is_attack_air) {
        const uint32_t anim = batch->state.animation_index[idx];
        if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
          if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                            batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FALL;
            is_attack_air = 0;
            is_air_loco = 1;
          }
        }
      }

      // EscapeAir per-frame update (decay + anim-end -> FallSpecial).
      if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
        escape_air_update(batch, c, idx);
        action_id = batch->state.action_id[idx];
        if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
          continue;
        }
        // Transitioned into an air locomotion state (FallSpecial). Continue with drift below.
      } else if (is_air_loco) {
        // Air dodge (EscapeAir) entry from eligible airborne locomotion states.
        //
        // Decomp entry check: ftCo_80099A58 (L/R press) is called from IASA in many aerial states,
        // but EscapeAir is not allowed from FallSpecial.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
        const uint8_t allow_escape_air =
            (action_id == MSL_ACT_JUMP_F || action_id == MSL_ACT_JUMP_B ||
             action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B ||
             action_id == MSL_ACT_DAMAGE_FALL || action_is_fall_like(action_id))
                ? 1
                : 0;
        if (allow_escape_air && escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
          continue;
        }

        // Aerial jump (double jump) entry.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
        if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) || did_tap_jump(c, stick_y, tilt_timer_y)) {
          if (batch->state.jumps_left[idx] > 0 && action_id != MSL_ACT_JUMP_AERIAL_F &&
              action_id != MSL_ACT_JUMP_AERIAL_B) {
            const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
            batch->state.action_id[idx] = act;
            batch->state.animation_index[idx] = submotion_for_action(act);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
            batch->state.speed_y_self[idx] =
                ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
            // Decomp: fp->x671_timer_lstick_tilt_y = 0xFE;
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
            batch->state.tilt_timer_y[idx] = 0xFEu;
            batch->state.fall_fast[idx] = 0;
            tilt_timer_y = 0xFEu;
            batch->state.jumps_left[idx]--;
            action_id = act;
          }
        }
      } else if (is_attack_air) {
        const uint8_t allow_interrupt =
            move_tables_attackair_allow_interrupt(cid, action_id, batch->state.anim_frame_f32[idx]);

        // Limited subset of DO_IASA for AttackAir* (only what we currently model):
        // - EscapeAir (airdodge)
        // - JumpAerial (double jump)
        //
        // Decomp: DO_IASA gated by fp->allow_interrupt.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
        if (allow_interrupt) {
          if (escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
            continue;
          }

          // Aerial jump (double jump) entry.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
          if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) ||
              did_tap_jump(c, stick_y, tilt_timer_y)) {
            if (batch->state.jumps_left[idx] > 0) {
              const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
              batch->state.action_id[idx] = act;
              batch->state.animation_index[idx] = submotion_for_action(act);
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
              batch->state.speed_y_self[idx] =
                  ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
              // Decomp: fp->x671_timer_lstick_tilt_y = 0xFE;
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
              batch->state.tilt_timer_y[idx] = 0xFEu;
              batch->state.fall_fast[idx] = 0;
              tilt_timer_y = 0xFEu;
              batch->state.jumps_left[idx]--;
              action_id = act;
            }
          }
        }
      }

      // Jump -> Fall when animation ends (and for aerial jumps too).
      if (action_id == MSL_ACT_JUMP_F || action_id == MSL_ACT_JUMP_B ||
          action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B) {
        const uint32_t anim = batch->state.animation_index[idx];
        if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
          if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                            batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          }
        }
      }

      continue;
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
        // Grounding transition: enter landing actions for supported airborne motion states.
        //
        // AttackAir collision callback is responsible for choosing LandingAir* vs auto-cancel Landing
        // based on fp->cmd_vars[0] (set by the move's command script).
        //
        // Decomp:
        // - AttackAir_Coll -> ft_80082C74(..., ftCo_LandingAir_EnterWithLag)
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // - ftCo_LandingAir_EnterWithLag checks fp->cmd_vars[0] to pick LandingAir* vs Landing_Enter_Basic.
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
        // - cmd_vars[0] is written by the command script via ftAction_80071820 (set_cmd_var).
        //   refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
        //
        // This sim derives cmd_vars[0] from extracted command-script timelines:
        // data/moves/{fox,falco}.json moves["ftCo_SM_AttackAir*"]["events"] set_cmd_var(idx=0).
        uint16_t land = 0;
        if (action_is_attackair(a)) {
          const uint8_t lag_enabled = move_tables_attackair_cmd0_active(
              batch->state.char_id[idx], a, batch->state.anim_frame_f32[idx]);
          land = lag_enabled ? landing_air_action_from_attackair(a) : (uint16_t)MSL_ACT_LANDING;
        } else if (a == (uint16_t)MSL_ACT_ESCAPE_AIR) {
          // EscapeAir: EscapeAir_Coll -> ft_80082C74(..., ftCo_80099D70) -> ftCo_LandingFallSpecial_Enter(..., x344)
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
          land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
        }

        // Locomotion-only fallback: fall states land into Landing/LandingFallSpecial.
        if (land == 0 && action_is_air_locomotion(a)) {
          land = (uint16_t)MSL_ACT_LANDING;
          if (a == MSL_ACT_FALL_SPECIAL || a == MSL_ACT_FALL_SPECIAL_F ||
              a == MSL_ACT_FALL_SPECIAL_B || a == MSL_ACT_LANDING_FALL_SPECIAL) {
            land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
          }
        }

        // Only refresh jumps / enter a landing action when we actually take a landing transition.
        if (land != 0) {
          enter_landing_action_from_air(batch, ch, idx, land);
        }
      } else if (was_ground && !now_ground) {
        batch->state.fall_fast[idx] = 0;

        // Only force Ground->Air transitions for ground locomotion states for now.
        // Many non-locomotion ground states transition into specific aerial variants (e.g. FallSpecial),
        // which we do not model yet; forcing Fall here causes large action_id regressions.
        if (!action_is_ground_locomotion(a)) {
          continue;
        }

        // Ground -> Air (walk off / lose ground) consumes the ground jump (jumps_used = 1).
        //
        // Decomp:
        // - Common path for "walk off ledge" transitions: ftCo_Fall_Enter calls ftCommon_8007D5D4 if
        //   starting from GA_Ground.
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c:51-74
        //   refs/melee/src/melee/ft/ftcommon.c:525-535
        batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;

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
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
    }
  }
}
