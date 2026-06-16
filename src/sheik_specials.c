#include "sheik_specials.h"

#include <math.h>

#include "action.h"
#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "grab_flow.h"
#include "ids.h"
#include "items.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "mpcoll_floor_skip.h"
#include "mpcoll_wall_ceil.h"
#include "msl_math.h"
#include "move_tables.h"
#include "stage_collision.h"
#include "trigger_input.h"

static inline float sk_stick_unit(int8_t v) { return (float)v * (1.0f / 80.0f); }

static inline float sk_deadzone(float v, float dz) { return (fabsf(v) < dz) ? 0.0f : v; }

static inline float sk_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}

static uint16_t sk_submotion(uint16_t action_id) {
  return msl_motion_state_submotion_id((uint8_t)MSL_CHAR_ID_SHEIK, action_id);
}

static uint8_t sk_anim_finished(const MslBatch* batch, size_t idx, uint16_t action_id) {
  const uint16_t msid = sk_submotion(action_id);
  const float end = msl_anim_end_frame((uint8_t)MSL_CHAR_ID_SHEIK, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]) >= end) ? 1u
                                                                                              : 0u;
}

static uint8_t sk_timer_saturating_inc(uint8_t x) {
  return x == UINT8_MAX ? UINT8_MAX : (uint8_t)(x + 1u);
}

static void sk_enter(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame,
                     float anim_rate) {
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (uint32_t)sk_submotion(action_id);
  msl_anim_timebase_enter(batch, idx, start_frame, anim_rate);
}

static uint8_t sk_try_enter_b_special(MslBatch* batch, const MslCommonParams* c,
                                      const MslCharParams* ch, size_t idx, uint8_t ground);

static void sk_enter_wait(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static uint8_t sk_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch, size_t idx,
                                                               uint16_t source_action) {
  if (batch == NULL || c == NULL || ch == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT || batch->state.on_ground[idx] == 0u) {
    return 0u;
  }

  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float stick_x =
      sk_deadzone(sk_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      sk_deadzone(sk_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float facing_dir = sk_facing_dir(batch, idx);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  // Grounded Sheik special Anim callbacks that call ft_8008A2BC enter Wait through ft_8008A348.
  // The destination Wait_IASA can still run in the same Fighter_procUpdate pass, so terminal
  // Needle, Chain, and Vanish exits must not serialize a bare Wait for one frame.
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Special{N,S,Hi}.c
  if (sk_try_enter_b_special(batch, c, ch, idx, 1u)) {
    return 1u;
  }
  if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
    return 1u;
  }
  if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                            stick_y, tilt_timer_x, tilt_timer_y,
                                                            facing_dir)) {
    return 1u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
    return 1u;
  }
  guard_update_grounded(batch, c, idx, /*allow_entry=*/1u);
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT) {
    return 1u;
  }
  return locomotion_wait_iasa_locomotion_subset_try_enter(
      batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y,
      facing_dir, source_action);
}

static void sk_enter_fall(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static void sk_enter_landing(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static void sk_enter_landing_fallspecial(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fallspecial_landing_lag[idx] =
      (ch != NULL) ? ch->sheik_vanish_landing_lag_frames : 0.0f;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
}

static void sk_enter_fallspecial(MslBatch* batch, const MslCharParams* ch, size_t idx, float lag,
                                 float mobility) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fallspecial_xc[idx] = 1u;
  batch->state.fallspecial_landing_lag[idx] = lag;
  batch->state.fallspecial_mobility_mul[idx] = mobility;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 1u;
  (void)ch;
}

static void sk_enter_vanish_air_end(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float vx = batch->state.speed_air_x_self[idx];
  const float vy = batch->state.speed_y_self[idx];
  const float mul = (ch != NULL) ? ch->sheik_vanish_end_vel_mul : 0.0f;
  batch->state.speed_air_x_self[idx] = vx * mul;
  batch->state.speed_ground_x_self[idx] = vx * mul;
  batch->state.speed_y_self[idx] = vy * mul;
  sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static uint8_t sk_action_allows_ground_special(const MslBatch* batch, size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_SQUAT:
    case MSL_ACT_RUN:
    case MSL_ACT_RUN_DIRECT:
    case MSL_ACT_OTTOTTO:
    case MSL_ACT_OTTOTTO_WAIT:
      return 1u;
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL: {
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      if (ch == NULL || batch->state.anim_frame_f32[idx] < (float)ch->landing_lag_frames) {
        return 0u;
      }
      if (a == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
          batch->state.landing_fallspecial_allow_interrupt[idx] == 0u) {
        return 0u;
      }
      return 1u;
    }
    default:
      return 0u;
  }
}

static uint8_t sk_action_allows_air_special(uint16_t a) {
  switch (a) {
    // ftCo_Jump_IASA runs ftCo_SpecialAir_CheckInput before item throw, EscapeAir, AttackAir, and
    // JumpAerial checks, so ground-jump air states can enter Sheik aerial specials on a fresh B
    // edge before falling.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_DAMAGE_FALL:
    case MSL_ACT_PASS:
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
      return 1u;
    default:
      return 0u;
  }
}

static void sk_enter_specialn(MslBatch* batch, size_t idx, uint8_t ground) {
  // ftSk_SpecialN.c::doEnter seeds one stored needle when fv.sk.x0 is zero.
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  if (batch->state.sheik_needle_count[idx] == 0u) {
    batch->state.sheik_needle_count[idx] = 1u;
  }
  sk_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_SK_SPECIAL_N_START : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_START,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void sk_enter_specials(MslBatch* batch, size_t idx, uint8_t ground, float stick_x) {
  if (ground) {
    if ((stick_x > 0.0f) != (batch->state.facing[idx] != 0u)) {
      batch->state.facing[idx] = (uint8_t)(stick_x > 0.0f);
      batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    }
  } else {
    batch->state.speed_y_self[idx] = 0.0f;
  }
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  sk_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_SK_SPECIAL_S_START : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void sk_vanish_callback_stick(const MslBatch* batch, size_t idx, float* out_sx,
                                     float* out_sy, float* out_mag) {
  float sx = sk_stick_unit(batch->state.prev_input_main_x[idx]);
  float sy = sk_stick_unit(batch->state.prev_input_main_y[idx]);
  float mag = sqrtf(sx * sx + sy * sy);
  if (mag > 1.0f) {
    mag = 1.0f;
  }
  if (out_sx != NULL) {
    *out_sx = sx;
  }
  if (out_sy != NULL) {
    *out_sy = sy;
  }
  if (out_mag != NULL) {
    *out_mag = mag;
  }
}

static void sk_vanish_enter_air_travel(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  float sx = 0.0f;
  float sy = 0.0f;
  float mag = 0.0f;
  sk_vanish_callback_stick(batch, idx, &sx, &sy, &mag);
  if (mag <= ch->sheik_vanish_stick_mag_min) {
    sx = 0.0f;
    sy = 1.0f;
    mag = 1.0f;
  } else if (fabsf(sx) > 0.001f) {
    batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
  }
  const float facing_dir = sk_facing_dir(batch, idx);
  const float angle = atan2f(sy, sx * facing_dir);
  const float speed =
      ch->sheik_vanish_travel_speed_stick_mul * mag + ch->sheik_vanish_travel_speed_base;
  batch->state.speed_air_x_self[idx] = facing_dir * speed * cosf(angle);
  batch->state.speed_y_self[idx] = speed * sinf(angle);
  batch->state.sheik_special_timer[idx] =
      (ch->sheik_vanish_travel_frames > 0 && ch->sheik_vanish_travel_frames < 255)
          ? (uint8_t)ch->sheik_vanish_travel_frames
          : 0u;
  sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1, 35.0f, 0.0f);
  (void)items_spawn_sheik_vanish_smoke_article(batch, idx);
  msl_anim_timebase_tick_once(batch, idx);
}

static void sk_vanish_enter_travel(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                   uint8_t ground) {
  if (!ground) {
    sk_vanish_enter_air_travel(batch, ch, idx);
    return;
  }
  float sx = 0.0f;
  float sy = 0.0f;
  float mag = 0.0f;
  sk_vanish_callback_stick(batch, idx, &sx, &sy, &mag);
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint8_t is_platform = mpcoll_is_on_platform(batch, bi, idx);
  const float floor_dot =
      batch->state.ground_normal_x[idx] * sx + batch->state.ground_normal_y[idx] * sy;
  if (mag >= ch->sheik_vanish_stick_mag_min && floor_dot <= 0.0f && !is_platform) {
    // Grounded Vanish Start0 only takes ground travel when the callback-visible stick points into
    // the floor half-space and ftCo_8009A134 does not consume a platform pass-through.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_80113838
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    batch->state.facing[idx] = (uint8_t)(sx >= 0.0f);
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    const float facing_dir = sk_facing_dir(batch, idx);
    const float angle = atan2f(sy, sx * facing_dir);
    const float speed =
        ch->sheik_vanish_travel_speed_stick_mul * mag + ch->sheik_vanish_travel_speed_base;
    batch->state.speed_ground_x_self[idx] = facing_dir * speed * cosf(angle);
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.sheik_special_timer[idx] =
        (ch->sheik_vanish_travel_frames > 0 && ch->sheik_vanish_travel_frames < 255)
            ? (uint8_t)ch->sheik_vanish_travel_frames
            : 0u;
    sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_1, 35.0f, 0.0f);
    (void)items_spawn_sheik_vanish_smoke_article(batch, idx);
    msl_anim_timebase_tick_once(batch, idx);
    return;
  }
  if (is_platform && batch->state.ground_id[idx] != 0xFFFFu) {
    msl_mpcoll_update_floor_skip(batch, idx, batch->state.ground_id[idx]);
  }
  batch->state.on_ground[idx] = 0u;
  sk_vanish_enter_air_travel(batch, ch, idx);
}

static void sk_enter_specialhi(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t ground) {
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  if (!ground) {
    batch->state.speed_y_self[idx] = ch->sheik_vanish_air_entry_vel_y;
  }
  sk_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_0
                  : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_0,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void sk_enter_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t ground) {
  // ftSk_SpecialLw.c::{ftSk_SpecialLw_Enter,ftSk_SpecialAirLw_Enter}
  if (ch->sheik_transform_vel_x_divisor > 0.0f) {
    batch->state.speed_air_x_self[idx] /= ch->sheik_transform_vel_x_divisor;
    batch->state.speed_ground_x_self[idx] /= ch->sheik_transform_vel_x_divisor;
  }
  if (ch->sheik_transform_vel_y_divisor > 0.0f) {
    batch->state.speed_y_self[idx] /= ch->sheik_transform_vel_y_divisor;
  }
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  sk_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_SK_SPECIAL_LW : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW, 0.0f,
           1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static uint8_t sk_hsd_lr_edge(const MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  enum { LRZ = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R | (uint16_t)MSL_BUTTON_Z };
  const uint16_t prev_buttons = batch->state.prev_input_buttons[idx];
  const uint16_t cur_buttons = batch->state.input_buttons[idx];
  const float prev_trigger = msl_trigger_unit_from_input(
      prev_buttons, batch->state.prev_input_l[idx], batch->state.prev_input_r[idx]);
  const float cur_trigger = msl_trigger_unit_from_input(cur_buttons, batch->state.input_l[idx],
                                                        batch->state.input_r[idx]);
  const uint8_t prev_lr =
      (((prev_buttons & (uint16_t)LRZ) != 0u) || prev_trigger > c->trigger_deadzone) ? 1u : 0u;
  const uint8_t cur_lr =
      (((cur_buttons & (uint16_t)LRZ) != 0u) || cur_trigger > c->trigger_deadzone) ? 1u : 0u;
  return (uint8_t)(cur_lr != 0u && prev_lr == 0u);
}

static uint8_t sk_try_enter_b_special(MslBatch* batch, const MslCommonParams* c,
                                      const MslCharParams* ch, size_t idx, uint8_t ground) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.hitlag[idx] != 0u ||
      batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  const uint8_t b_edge =
      ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
  const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
  if (ground) {
    if (a == (uint16_t)MSL_ACT_KNEE_BEND) {
      if (batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_KNEE_BEND || !up_b_present) {
        return 0u;
      }
      // KneeBend_IASA does not run the ordinary grounded B-special chain. It calls
      // ftCo_Attack100_CheckInput, whose only special path is ftData_SpecialHi when fp->x686 == 0.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100_CheckInput
      sk_enter_specialhi(batch, ch, idx, 1u);
      return 1u;
    }
    if (!b_edge) {
      return 0u;
    }
    if (!sk_action_allows_ground_special(batch, idx, a)) {
      return 0u;
    }
  } else {
    if (!b_edge || !sk_action_allows_air_special(a)) {
      return 0u;
    }
  }
  const float sx = sk_deadzone(sk_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float sy = sk_deadzone(sk_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float ax = fabsf(sx);
  if (ground) {
    // ftCo_Wait_IASA order is SpecialS -> SpecialHi -> SpecialN -> SpecialLw, but the latter
    // three are entered through hidden x686/x689/x687 latch callsites. Until those seed/live lanes
    // are promoted, use the live B-edge stick owner only to choose among the Sheik entry funcs.
    if (ax >= c->special_stick_x_threshold_side) {
      sk_enter_specials(batch, idx, 1u, sx);
      return 1u;
    }
    if (sy >= c->special_stick_y_threshold) {
      sk_enter_specialhi(batch, ch, idx, 1u);
      return 1u;
    }
    if (sy <= -c->special_stick_y_threshold) {
      sk_enter_speciallw(batch, ch, idx, 1u);
      return 1u;
    }
    sk_enter_specialn(batch, idx, 1u);
    return 1u;
  }
  // ftCo_SpecialAir_CheckInput order: Up -> Down -> Side -> Neutral.
  if (sy >= c->special_stick_y_threshold) {
    sk_enter_specialhi(batch, ch, idx, 0u);
    return 1u;
  }
  if (sy <= -c->special_stick_y_threshold) {
    sk_enter_speciallw(batch, ch, idx, 0u);
    return 1u;
  }
  if (ax >= c->special_stick_x_threshold_side) {
    sk_enter_specials(batch, idx, 0u, sx);
    return 1u;
  }
  if ((float)batch->state.x676_x[idx] < c->special_neutral_reverse_threshold) {
    const uint8_t facing = batch->state.facing[idx] ? 1u : 0u;
    const uint8_t x2228_b7 = batch->state.x2228_b7[idx] ? 1u : 0u;
    if ((facing == 0u && x2228_b7 == 1u) || (facing == 1u && x2228_b7 == 0u)) {
      batch->state.facing[idx] = facing ? 0u : 1u;
      batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    }
  }
  sk_enter_specialn(batch, idx, 0u);
  return 1u;
}

uint8_t sheik_special_try_landing_iasa(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      batch->state.on_ground[idx] == 0u) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_LANDING && a != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    return 0u;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (c == NULL || ch == NULL || batch->state.anim_frame_f32[idx] < (float)ch->landing_lag_frames) {
    return 0u;
  }
  // Decomp: ftCo_Landing_IASA checks the landing lag and allow-interrupt gates, then runs
  // SpecialS, Attack100, SpecialN, and SpecialLw before grounded attack and guard checks.
  // LandingAir IASA is empty, so this helper is intentionally limited to Landing-family actions.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Special{N,S,Hi,Lw}.c
  return sk_try_enter_b_special(batch, c, ch, idx, 1u);
}

static void sk_update_specialn_loop_iasa(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                         uint16_t a) {
  // ftSk_Special{Air}NStart_Anim enters Loop on animation end. The destination Loop IASA can then
  // run in the same Fighter proc and immediately enter End when B is released.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
  //   ftSk_SpecialNStart_Anim,ftSk_SpecialAirNStart_Anim,ftSk_SpecialNLoop_IASA,
  //   ftSk_SpecialAirNLoop_IASA}
  if (batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_B) {
    if (sk_hsd_lr_edge(batch, c, idx) != 0u) {
      sk_enter(batch, idx,
               a == (uint16_t)MSL_ACT_SK_SPECIAL_N_LOOP ? (uint16_t)MSL_ACT_SK_SPECIAL_N_CANCEL
                                                        : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_CANCEL,
               0.0f, 1.0f);
    }
  } else {
    batch->state.sheik_special_timer[idx] = 0u;
    sk_enter(batch, idx,
             a == (uint16_t)MSL_ACT_SK_SPECIAL_N_LOOP ? (uint16_t)MSL_ACT_SK_SPECIAL_N_END
                                                      : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_END,
             0.0f, 1.0f);
  }
}

static void sk_update_specialn(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                               size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_SK_SPECIAL_N_START:
    case MSL_ACT_SK_SPECIAL_AIR_N_START:
      if (sk_anim_finished(batch, idx, a)) {
        const uint16_t loop = a == (uint16_t)MSL_ACT_SK_SPECIAL_N_START
                                  ? (uint16_t)MSL_ACT_SK_SPECIAL_N_LOOP
                                  : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_LOOP;
        sk_enter(batch, idx, loop, 0.0f, 1.0f);
        sk_update_specialn_loop_iasa(batch, c, idx, loop);
      }
      break;
    case MSL_ACT_SK_SPECIAL_N_LOOP:
    case MSL_ACT_SK_SPECIAL_AIR_N_LOOP:
      sk_update_specialn_loop_iasa(batch, c, idx, a);
      break;
    case MSL_ACT_SK_SPECIAL_N_CANCEL:
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_wait(batch, idx);
        (void)sk_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
      }
      break;
    case MSL_ACT_SK_SPECIAL_AIR_N_CANCEL:
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_fall(batch, idx);
      }
      break;
    case MSL_ACT_SK_SPECIAL_N_END:
    case MSL_ACT_SK_SPECIAL_AIR_N_END: {
      const uint8_t t = batch->state.sheik_special_timer[idx];
      if ((t == 2u || t == 5u || t == 8u || t == 11u || t == 14u || t == 17u) &&
          batch->state.sheik_needle_count[idx] != 0u) {
        batch->state.sheik_needle_count[idx]--;
        batch->state.sheik_special_latch[idx] = 1u;
      } else {
        batch->state.sheik_special_latch[idx] = 0u;
      }
      if (t != UINT8_MAX) {
        batch->state.sheik_special_timer[idx] = (uint8_t)(t + 1u);
      }
      if (sk_anim_finished(batch, idx, a)) {
        if (a == (uint16_t)MSL_ACT_SK_SPECIAL_N_END) {
          sk_enter_wait(batch, idx);
          (void)sk_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
        } else {
          sk_enter_fall(batch, idx);
        }
      }
    } break;
    default:
      break;
  }
}

static void sk_update_specials(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                               size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_SK_SPECIAL_S_START:
    case MSL_ACT_SK_SPECIAL_AIR_S_START: {
      // Source `mv.sk.specials.x0` is an int; the lite runtime stores only the threshold-relevant
      // byte, so saturate instead of wrapping during long held Chain sequences.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
      const uint8_t t = sk_timer_saturating_inc(batch->state.sheik_special_timer[idx]);
      batch->state.sheik_special_timer[idx] = t;
      if ((float)t == ch->sheik_chain_spawn_frame) {
        (void)items_spawn_sheik_chain_article(batch, idx);
      }
      if ((float)t > ch->sheik_chain_start_end_frame) {
        sk_enter(batch, idx,
                 a == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ? (uint16_t)MSL_ACT_SK_SPECIAL_S
                                                           : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S,
                 0.0f, 1.0f);
        batch->state.sheik_special_timer[idx] = 0u;
      }
    } break;
    case MSL_ACT_SK_SPECIAL_S:
    case MSL_ACT_SK_SPECIAL_AIR_S: {
      // Source `mv.sk.specials.x0` continues past the release threshold while B is held. Preserve
      // the threshold state in the compact runtime lane without uint8 wraparound.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialS_Anim,ftSk_SpecialAirS_Anim}
      const uint8_t t = sk_timer_saturating_inc(batch->state.sheik_special_timer[idx]);
      batch->state.sheik_special_timer[idx] = t;
      if ((float)t > ch->sheik_chain_release_min_frames &&
          batch->state.sheik_special_latch[idx] != 0u) {
        sk_enter(batch, idx,
                 a == (uint16_t)MSL_ACT_SK_SPECIAL_S ? (uint16_t)MSL_ACT_SK_SPECIAL_S_END
                                                     : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END,
                 0.0f, 1.0f);
        batch->state.sheik_special_timer[idx] = 0u;
        batch->state.sheik_special_latch[idx] = 0u;
      } else if ((batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
        // ftSk_SpecialS_IASA writes x4 after the Anim callback has already tested it, so a current
        // B release is consumed by the next active Chain frame, not the current one.
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
        //   ftSk_SpecialS_Anim,ftSk_SpecialS_IASA,ftSk_SpecialAirS_Anim,ftSk_SpecialAirS_IASA}
        batch->state.sheik_special_latch[idx] = 1u;
      }
    } break;
    case MSL_ACT_SK_SPECIAL_S_END: {
      const uint8_t t = sk_timer_saturating_inc(batch->state.sheik_special_timer[idx]);
      batch->state.sheik_special_timer[idx] = t;
      if ((float)t == ch->sheik_chain_destroy_frame) {
        (void)items_destroy_sheik_chain_article(batch, idx);
      }
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_wait(batch, idx);
        (void)sk_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
      }
    } break;
    case MSL_ACT_SK_SPECIAL_AIR_S_END: {
      const uint8_t t = sk_timer_saturating_inc(batch->state.sheik_special_timer[idx]);
      batch->state.sheik_special_timer[idx] = t;
      if ((float)t == ch->sheik_chain_destroy_frame) {
        (void)items_destroy_sheik_chain_article(batch, idx);
      }
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_fall(batch, idx);
      }
    } break;
    default:
      break;
  }
}

static void sk_update_specialhi(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                                size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_SK_SPECIAL_HI_START_0:
    case MSL_ACT_SK_SPECIAL_AIR_HI_START_0:
      if (sk_anim_finished(batch, idx, a)) {
        sk_vanish_enter_travel(batch, ch, idx,
                               a == (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_0 ? 1u : 0u);
      }
      break;
    case MSL_ACT_SK_SPECIAL_HI_START_1:
    case MSL_ACT_SK_SPECIAL_AIR_HI_START_1:
      if (batch->state.sheik_special_timer[idx] != 0u) {
        batch->state.sheik_special_timer[idx]--;
      }
      if (batch->state.sheik_special_timer[idx] == 0u) {
        if (a == (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_1) {
          const float vx = batch->state.speed_ground_x_self[idx];
          const float vy = batch->state.speed_y_self[idx];
          batch->state.speed_ground_x_self[idx] = vx * ch->sheik_vanish_end_vel_mul;
          batch->state.speed_air_x_self[idx] = vx * ch->sheik_vanish_end_vel_mul;
          batch->state.speed_y_self[idx] = vy * ch->sheik_vanish_end_vel_mul;
          sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_HI, 0.0f, 1.0f);
        } else {
          sk_enter_vanish_air_end(batch, ch, idx);
        }
      }
      break;
    case MSL_ACT_SK_SPECIAL_HI:
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_wait(batch, idx);
        (void)sk_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
      }
      break;
    case MSL_ACT_SK_SPECIAL_AIR_HI:
      // Vanish end script sets cmd_vars[0] at frame 9. ftSk_SpecialAirHi_Phys uses that source
      // bit to switch from travel-velocity damping to common FallBasic gravity. Slippi does not
      // serialize cmd_vars, so one-step reseeds and free-running rows refresh this lane from the
      // extracted MSLFTSC1 script timeline.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Phys
      // data/scripts/sheik.bin::MSLFTSC1 specials_by_msid[312] set_cmd_var(idx=0,value=1)
      batch->state.special_cmd0[idx] = move_tables_special_cmd_var_value_at_frame(
          batch->state.char_id[idx], sk_submotion(a), 0u, batch->state.anim_frame_f32[idx]);
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_fallspecial(batch, ch, idx, ch->sheik_vanish_landing_lag_frames,
                             ch->sheik_vanish_fallspecial_mobility_mul);
      }
      break;
    default:
      break;
  }
}

static uint8_t sk_vanish_wallceil_normal(const MslBatch* batch, size_t idx, float* nx, float* ny) {
  if (batch == NULL || nx == NULL || ny == NULL) {
    return 0u;
  }
  const uint32_t env = batch->state.coll_env_flags[idx];
  if ((env & (uint32_t)MSL_COLLIDE_CEILING_MASK) != 0u && batch->state.ceiling_id[idx] != 0xFFFFu) {
    *nx = batch->state.ceiling_normal_x[idx];
    *ny = batch->state.ceiling_normal_y[idx];
    return 1u;
  }
  if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u || batch->state.wall_kind[idx] == 1u) {
    *nx = batch->state.wall_normal_x[idx];
    *ny = batch->state.wall_normal_y[idx];
    return 1u;
  }
  if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK) != 0u || batch->state.wall_kind[idx] == 2u) {
    *nx = batch->state.wall_normal_x[idx];
    *ny = batch->state.wall_normal_y[idx];
    return 1u;
  }
  return 0u;
}

uint8_t sheik_special_try_vanish_travel_wallceil_end(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ch == NULL) {
    return 0u;
  }
  float nx = 0.0f;
  float ny = 0.0f;
  if (!sk_vanish_wallceil_normal(batch, idx, &nx, &ny)) {
    return 0u;
  }
  const float vx = batch->state.speed_air_x_self[idx];
  const float vy = batch->state.speed_y_self[idx];
  const float n_mag = sqrtf(nx * nx + ny * ny);
  const float v_mag = sqrtf(vx * vx + vy * vy);
  if (!(n_mag > 0.0f) || !(v_mag > 0.0f)) {
    return 0u;
  }
  float dot = (nx * vx + ny * vy) / (n_mag * v_mag);
  if (dot > 1.0f) {
    dot = 1.0f;
  } else if (dot < -1.0f) {
    dot = -1.0f;
  }
  const float angle = acosf(dot);
  const float threshold =
      (90.0f + (float)ch->sheik_vanish_wall_bounce_degrees) * (MSL_PI_F / 180.0f);
  if (!(angle > threshold)) {
    return 0u;
  }

  // ftSk_SpecialAirHiStart_1_Coll checks ceiling, left wall, then right wall. A qualifying
  // collision angle calls ftSk_SpecialHi_80113F68, entering SpecialAirHi and scaling the stored
  // travel velocity by ftSeakAttributes::x54.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
  //   ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialHi_80113F68}
  // Source keys: data/characters/sheik.json::sheik_vanish_wall_bounce_degrees,
  // data/characters/sheik.json::sheik_vanish_end_vel_mul.
  sk_enter_vanish_air_end(batch, ch, idx);
  return 1u;
}

uint8_t sheik_special_vanish_air_start1_platform_pass_active(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ch == NULL || ch->sheik_vanish_travel_frames <= 0 ||
      !(ch->sheik_vanish_ground_contact_min_frames > 0.0f)) {
    return 0u;
  }
  const int travel_frames = ch->sheik_vanish_travel_frames;
  const int remaining_after_anim = (int)batch->state.sheik_special_timer[idx];
  const int collision_tick = travel_frames - remaining_after_anim;
  if (collision_tick < 0) {
    return 0u;
  }
  // ftSk_SpecialAirHiStart_1_Coll increments mv.sk.specialhi.xC, then accepted platform floor
  // contact consumes ftCo_8009A134/mpUpdateFloorSkip while xC is still below
  // ftSeakAttributes::x3C. The hidden x0 travel timer and xC are initialized together by
  // ftSk_SpecialHi_80113A30, so after the Anim callback has decremented x0, `x38 - x0` is the
  // callback-visible xC value for this collision pass.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
  //   ftSk_SpecialHi_80113A30,ftSk_SpecialAirHiStart_1_Anim,
  //   ftSk_SpecialAirHiStart_1_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
  return ((float)collision_tick < ch->sheik_vanish_ground_contact_min_frames) ? 1u : 0u;
}

static void sk_update_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_SK_SPECIAL_LW:
    case MSL_ACT_SK_SPECIAL_AIR_LW:
      if (sk_anim_finished(batch, idx, a)) {
        // Minimal Stage-0 Zelda policy: model the source transform finish action boundary using
        // Sheik's private finish states, without exposing Zelda as a public registry character.
        sk_enter(batch, idx,
                 a == (uint16_t)MSL_ACT_SK_SPECIAL_LW ? (uint16_t)MSL_ACT_SK_SPECIAL_LW_2
                                                      : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW_2,
                 ch->sheik_transform_finish_start_frame, 1.0f);
      }
      break;
    case MSL_ACT_SK_SPECIAL_LW_2:
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_wait(batch, idx);
      }
      break;
    case MSL_ACT_SK_SPECIAL_AIR_LW_2:
      if (sk_anim_finished(batch, idx, a)) {
        sk_enter_fall(batch, idx);
      }
      break;
    default:
      break;
  }
}

void sheik_specials_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int n = batch->batch_size;
  const int num_players = msl_batch_num_players(batch);
  for (int bi = 0; bi < n; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
        continue;
      }
      const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
      if (ch == NULL || c == NULL) {
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      if (!sheik_action_is_special(a)) {
        if (sk_try_enter_b_special(batch, c, ch, idx, batch->state.on_ground[idx] ? 1u : 0u)) {
          continue;
        }
        continue;
      }
      batch->state.animation_index[idx] = (uint32_t)sk_submotion(a);
      sk_update_specialn(batch, c, ch, idx, a);
      sk_update_specials(batch, c, ch, idx, a);
      sk_update_specialhi(batch, c, ch, idx, a);
      sk_update_speciallw(batch, ch, idx, a);
    }
  }
}

static float sk_apply_air_friction(float vel, float friction) {
  float a = friction;
  if (fabsf(a) >= fabsf(vel)) {
    a = -vel;
  } else if (vel > 0.0f) {
    a = -a;
  }
  return vel + a;
}

static void sk_apply_common_fall(MslBatch* batch, const MslCharParams* ch, size_t idx, float grav,
                                 float terminal) {
  float vy = batch->state.speed_y_self[idx] - grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
  const float stick_x = sk_stick_unit(batch->state.input_main_x[idx]);
  const float target = stick_x * ch->air_drift_max;
  const float accel = stick_x * ch->air_drift_stick_mul +
                      ((stick_x >= 0.0f) ? ch->aerial_drift_base : -ch->aerial_drift_base);
  float vx = batch->state.speed_air_x_self[idx];
  if (fabsf(stick_x) < 0.001f) {
    vx = sk_apply_air_friction(vx, ch->aerial_friction);
  } else {
    vx += accel;
    if ((accel > 0.0f && vx > target) || (accel < 0.0f && vx < target)) {
      vx = target;
    }
  }
  batch->state.speed_air_x_self[idx] = vx;
}

uint8_t sheik_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ch == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case MSL_ACT_SK_SPECIAL_AIR_HI_START_0:
      sk_apply_common_fall(batch, ch, idx, ch->sheik_vanish_start_air_gravity,
                           ch->sheik_vanish_start_air_terminal_vel);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_HI:
      if (batch->state.special_cmd0[idx] != 0u) {
        sk_apply_common_fall(batch, ch, idx, ch->grav, ch->terminal_vel);
        const float max_x = ch->sheik_vanish_air_end_drift_mul * ch->air_drift_max;
        if (batch->state.speed_air_x_self[idx] > max_x) {
          batch->state.speed_air_x_self[idx] = max_x;
        } else if (batch->state.speed_air_x_self[idx] < -max_x) {
          batch->state.speed_air_x_self[idx] = -max_x;
        }
      } else {
        batch->state.speed_y_self[idx] -= batch->state.speed_y_self[idx] * 0.1f;
        batch->state.speed_air_x_self[idx] =
            sk_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
      }
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_LW:
    case MSL_ACT_SK_SPECIAL_AIR_LW_2:
      sk_apply_common_fall(batch, ch, idx, ch->sheik_transform_air_gravity,
                           ch->sheik_transform_air_terminal_vel);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_S_START:
      if (batch->state.special_cmd0[idx] != 0u) {
        sk_apply_common_fall(batch, ch, idx, ch->grav, ch->terminal_vel);
      }
      batch->state.speed_air_x_self[idx] =
          sk_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_N_START:
    case MSL_ACT_SK_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_SK_SPECIAL_AIR_N_CANCEL:
    case MSL_ACT_SK_SPECIAL_AIR_N_END:
    case MSL_ACT_SK_SPECIAL_AIR_S:
    case MSL_ACT_SK_SPECIAL_AIR_S_END:
      sk_apply_common_fall(batch, ch, idx, ch->grav, ch->terminal_vel);
      return 1u;
    default:
      return 0u;
  }
}

uint8_t sheik_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case MSL_ACT_SK_SPECIAL_N_START:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_START,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_N_LOOP:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_LOOP,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_S_START:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_S:
      // ftSk_SpecialS_Coll calls ft_800827A0; on floor loss it enters grounded retract
      // (`ftSk_SpecialS_80111DF8`) rather than swapping to active aerial Chain.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialS_Coll,ftSk_SpecialS_80111DF8}
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_S_END, 0.0f, 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_S_END:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_HI_START_0:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_0,
               batch->state.anim_frame_f32[idx], 1.0f);
      // ftSk_SpecialHiStart_0_Coll can floor-loss into ftSk_SpecialHi_80113324, which preserves
      // the Start0 animation frame and installs the same fn_80112ED8 smoke accessory callback used
      // by normal travel entry.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
      //   ftSk_SpecialHiStart_0_Coll,ftSk_SpecialHi_80113324,fn_80112ED8}
      (void)items_spawn_sheik_vanish_smoke_article(batch, idx);
      return 1u;
    case MSL_ACT_SK_SPECIAL_HI_START_1:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1,
               batch->state.anim_frame_f32[idx], 0.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_HI:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_LW:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_LW_2:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW_2, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    default:
      return 0u;
  }
}

uint8_t sheik_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case MSL_ACT_SK_SPECIAL_AIR_N_START:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_N_START, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_N_LOOP:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_N_LOOP, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_N_CANCEL:
    case MSL_ACT_SK_SPECIAL_AIR_N_END:
      sk_enter_landing(batch, idx);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_S_START:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_S_START, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_S:
      // ftSk_SpecialAirS_Coll calls ft_80081D0C; accepted floor contact enters aerial retract
      // (`ftSk_SpecialS_80111EB4`) rather than the grounded active Chain state.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialAirS_Coll,ftSk_SpecialS_80111EB4}
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END, 0.0f, 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_S_END:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_S_END, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_HI_START_0:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_0,
               batch->state.anim_frame_f32[idx], 1.0f);
      // ftSk_SpecialAirHiStart_0_Coll can ground into ftSk_SpecialHi_80113390, which also
      // installs fn_80112ED8 at the preserved Start0 frame.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
      //   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialHi_80113390,fn_80112ED8}
      (void)items_spawn_sheik_vanish_smoke_article(batch, idx);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_HI_START_1:
      // ftSk_SpecialAirHi_Coll enters the grounded travel state after the common collision path
      // accepts a floor contact. Pass-through early-contact rejection is owned by mpColl before
      // this callback; do not reinterpret the x3C timer as a post-contact runtime gate here.
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_HI_START_1,
               batch->state.anim_frame_f32[idx], 0.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_HI:
      sk_enter_landing_fallspecial(batch, msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK), idx);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_LW:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_LW, batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_SK_SPECIAL_AIR_LW_2:
      sk_enter(batch, idx, (uint16_t)MSL_ACT_SK_SPECIAL_LW_2, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    default:
      return 0u;
  }
}
