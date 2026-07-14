#include "zelda_specials.h"

#include <math.h>

// Zelda fighter special owners live here instead of sheik_specials.c. Source anchors:
// refs/melee/src/melee/ft/chara/ftZelda/ftZd_Special{N,S,Hi,Lw}.c
// refs/melee/src/melee/ft/chara/ftZelda/types.h::ftZelda_DatAttrs
// Extracted attrs/scripts: data/characters/zelda.json, data/scripts/zelda.bin.
#include "action.h"
#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "common_specials.h"
#include "dash_iasa.h"
#include "grab_flow.h"
#include "fighter_script.h"
#include "ids.h"
#include "items.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "mpcoll_floor_skip.h"
#include "mp_coll.h"
#include "msl_math.h"
#include "sheik_specials.h"
#include "stage_collision.h"
#include "state_flags.h"
#include "trigger_input.h"

static inline float zd_stick_unit(int8_t v) { return (float)v * (1.0f / 80.0f); }

static inline float zd_deadzone(float v, float dz) { return (fabsf(v) < dz) ? 0.0f : v; }

static inline float zd_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}

static uint16_t zd_submotion(uint16_t action_id) {
  return msl_motion_state_submotion_id((uint8_t)MSL_CHAR_ID_ZELDA, action_id);
}

static uint8_t zd_anim_finished(const MslBatch* batch, size_t idx, uint16_t action_id) {
  const uint16_t msid = zd_submotion(action_id);
  const float end = msl_anim_end_frame((uint8_t)MSL_CHAR_ID_ZELDA, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]) >= end) ? 1u
                                                                                              : 0u;
}

static void zd_enter(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame,
                     float anim_rate) {
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] =
      (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[idx], action_id);
  msl_anim_timebase_enter(batch, idx, start_frame, anim_rate);
}

static void zd_enter_wait(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static void zd_enter_fall(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static void zd_enter_landing_fallspecial(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
  const float end =
      msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
  const float lag = (ch != NULL) ? ch->zelda_farore_landing_lag_frames : 0.0f;
  msl_anim_timebase_enter(batch, idx, 0.0f,
                          (lag > 0.0f && end > 0.0f) ? ((end + 0.1f) / lag) : 1.0f);
  batch->state.fallspecial_landing_lag[idx] = lag;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
}

static void zd_enter_fallspecial(MslBatch* batch, const MslCharParams* ch, size_t idx, float lag,
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

static uint8_t zd_action_allows_ground_special(const MslBatch* batch, size_t idx, uint16_t a) {
  // Common grounded special dispatch owners:
  // - Wait/Walk/Dash/Run/Ottotto route through ftCo_SpecialS_CheckInput/ftCo_800D6824/
  //   ftCo_800D68C0 from their IASA callbacks.
  // - Squat_IASA uses the full grounded-special dispatcher; SquatWait/SquatRv are handled by the
  //   Down-B-only helper below.
  // - Attack* Wait-IASA owners are table-backed by MSLMSO01.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_{Wait,Walk,Dash,Run,Ottotto,Squat}.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6824,ftCo_800D68C0}
  // data/motion_state/owners/zelda.bin::MSLMSO01 GROUNDED_ATTACK_WAIT_IASA_SPECIALS
  switch (a) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_SQUAT:
    case MSL_ACT_DASH:
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
      if (msl_motion_state_common_class_has_fast(a,
                                                 MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS)) {
        const size_t flags_i =
            idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
        return (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT)
                   ? 1u
                   : 0u;
      }
      return 0u;
  }
}

static uint8_t zd_action_allows_ground_down_special(const MslBatch* batch, size_t idx, uint16_t a) {
  if (a == (uint16_t)MSL_ACT_SQUAT_WAIT || a == (uint16_t)MSL_ACT_SQUAT_RV) {
    // SquatWait_IASA and SquatRv_IASA call only ftCo_800D68C0 before lower-priority common
    // consumers, so crouch hold/reverse admits Zelda Down-B without also admitting neutral/up/side.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    return 1u;
  }
  return zd_action_allows_ground_special(batch, idx, a);
}

static uint8_t zd_action_allows_air_special(uint16_t a) {
  switch (a) {
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

static void zd_transform_cache_visible_twin_2218(MslBatch* batch, size_t idx) {
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.zelda_twin_state_flags_2218[idx] = batch->state.state_flags[flags_i];
}

static void zd_transform_set_live_2218(MslBatch* batch, size_t idx, uint8_t flags_2218) {
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] = flags_2218;
}

static void zd_enter_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t ground) {
  // ftZd_SpecialLw.c::{ftZd_SpecialLw_Enter,ftZd_SpecialAirLw_Enter} via
  // ftZelda_SpecialLw_StartAction_Helper.
  if (ch->zelda_transform_vel_x_divisor > 0.0f) {
    batch->state.speed_air_x_self[idx] /= ch->zelda_transform_vel_x_divisor;
    batch->state.speed_ground_x_self[idx] /= ch->zelda_transform_vel_x_divisor;
  }
  if (ch->zelda_transform_vel_y_divisor > 0.0f) {
    batch->state.speed_y_self[idx] /= ch->zelda_transform_vel_y_divisor;
  }
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  zd_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_ZD_SPECIAL_LW : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_LW, 0.0f,
           1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void zd_enter_specialn(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t ground) {
  // ftZd_SpecialN.c::{ftZd_SpecialN_Enter,ftZd_SpecialAirN_Enter}
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] =
      (!ground && ch->zelda_nayru_air_gravity_delay_frames > 0 &&
       ch->zelda_nayru_air_gravity_delay_frames < 255)
          ? (uint8_t)ch->zelda_nayru_air_gravity_delay_frames
          : 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  if (!ground) {
    batch->state.speed_y_self[idx] = 0.0f;
    if (ch->zelda_nayru_air_vel_x_divisor > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->zelda_nayru_air_vel_x_divisor;
    }
  }
  zd_enter(batch, idx, ground ? (uint16_t)MSL_ACT_ZD_SPECIAL_N : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_N,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void zd_enter_specials(MslBatch* batch, const MslCharParams* ch, size_t idx, uint8_t ground,
                              float stick_x) {
  // ftZd_SpecialS.c::{ftZd_SpecialS_Enter,ftZd_SpecialAirS_Enter}
  if (ground) {
    if ((stick_x > 0.0f) != (batch->state.facing[idx] != 0u)) {
      batch->state.facing[idx] = (uint8_t)(stick_x > 0.0f);
      batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    }
    ftco_specials_apply_grounded_sideb_doenter(batch, ch, idx);
  } else {
    const MslCommonParams* c = msl_common_params();
    if (c != NULL && stick_x * zd_facing_dir(batch, idx) < -c->special_side_reverse_threshold) {
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
      batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
      batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    }
    batch->state.speed_y_self[idx] = 0.0f;
  }
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] =
      (ch->zelda_din_release_min_frames > 0 && ch->zelda_din_release_min_frames < 255)
          ? (uint8_t)ch->zelda_din_release_min_frames
          : 0u;
  batch->state.special_cmd1[idx] =
      (ch->zelda_din_end_min_frames > 0 && ch->zelda_din_end_min_frames < 255)
          ? (uint8_t)ch->zelda_din_end_min_frames
          : 0u;
  batch->state.special_cmd2[idx] =
      (ch->zelda_din_air_gravity_delay_frames > 0 && ch->zelda_din_air_gravity_delay_frames < 255)
          ? (uint8_t)ch->zelda_din_air_gravity_delay_frames
          : 0u;
  batch->state.sheik_special_latch[idx] =
      (ch->zelda_din_release_hold_min_frames > 0 && ch->zelda_din_release_hold_min_frames < 255)
          ? (uint8_t)ch->zelda_din_release_hold_min_frames
          : 0u;
  zd_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_ZD_SPECIAL_S_START : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_START,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void zd_enter_farore_start1_then_freeze(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                               uint16_t action_id) {
  zd_enter(batch, idx, action_id, 35.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
  msl_anim_timebase_recompute_derived(batch, idx);
  batch->state.sheik_special_timer[idx] =
      (ch->zelda_farore_travel_frames > 0 && ch->zelda_farore_travel_frames < 255)
          ? (uint8_t)ch->zelda_farore_travel_frames
          : 0u;
  batch->state.jumps_left[idx] = 0u;
}

static void zd_farore_callback_stick(const MslBatch* batch, const MslCommonParams* c, size_t idx,
                                     float* out_sx, float* out_sy, float* out_mag) {
  const float dz_x = (c != NULL) ? c->lstick_deadzone_x : 0.0f;
  const float dz_y = (c != NULL) ? c->lstick_deadzone_y : 0.0f;
  float sx = zd_deadzone(zd_stick_unit(batch->state.prev_input_main_x[idx]), dz_x);
  float sy = zd_deadzone(zd_stick_unit(batch->state.prev_input_main_y[idx]), dz_y);
  float mag = sqrtf(sx * sx + sy * sy);
  if (mag > 1.0f) {
    mag = 1.0f;
  }
  *out_sx = sx;
  *out_sy = sy;
  *out_mag = mag;
}

static void zd_farore_enter_air_travel(MslBatch* batch, const MslCommonParams* c,
                                       const MslCharParams* ch, size_t idx) {
  float sx = 0.0f;
  float sy = 0.0f;
  float mag = 0.0f;
  zd_farore_callback_stick(batch, c, idx, &sx, &sy, &mag);
  if (mag <= ch->zelda_farore_stick_mag_min) {
    sx = 0.0f;
    sy = 1.0f;
    mag = 1.0f;
  } else if (fabsf(sx) > 0.001f) {
    batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
  }
  const float facing_dir = zd_facing_dir(batch, idx);
  const float angle = atan2f(sy, sx * facing_dir);
  const float speed =
      ch->zelda_farore_travel_speed_stick_mul * mag + ch->zelda_farore_travel_speed_base;
  batch->state.speed_air_x_self[idx] = facing_dir * speed * cosf(angle);
  batch->state.speed_y_self[idx] = speed * sinf(angle);
  zd_enter_farore_start1_then_freeze(batch, ch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_1);
}

static void zd_farore_enter_travel(MslBatch* batch, const MslCommonParams* c,
                                   const MslCharParams* ch, size_t idx, uint8_t ground) {
  if (!ground) {
    zd_farore_enter_air_travel(batch, c, ch, idx);
    return;
  }
  float sx = 0.0f;
  float sy = 0.0f;
  float mag = 0.0f;
  zd_farore_callback_stick(batch, c, idx, &sx, &sy, &mag);
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint8_t is_platform = mpcoll_is_on_platform(batch, bi, idx);
  const float floor_dot =
      batch->state.ground_normal_x[idx] * sx + batch->state.ground_normal_y[idx] * sy;
  if (mag >= ch->zelda_farore_stick_mag_min && floor_dot <= 0.0f && !is_platform) {
    batch->state.facing[idx] = (uint8_t)(sx >= 0.0f);
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
    const float facing_dir = zd_facing_dir(batch, idx);
    const float angle = atan2f(sy, sx * facing_dir);
    const float speed =
        ch->zelda_farore_travel_speed_stick_mul * mag + ch->zelda_farore_travel_speed_base;
    batch->state.speed_ground_x_self[idx] = facing_dir * speed * cosf(angle);
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
    zd_enter_farore_start1_then_freeze(batch, ch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_1);
    return;
  }
  if (is_platform && batch->state.ground_id[idx] != 0xFFFFu) {
    msl_mpcoll_update_floor_skip(batch, idx, batch->state.ground_id[idx]);
  }
  batch->state.on_ground[idx] = 0u;
  zd_farore_enter_air_travel(batch, c, ch, idx);
}

static void zd_enter_specialhi(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t ground) {
  // ftZd_SpecialHi.c::{ftZd_SpecialHi_Enter,ftZd_SpecialAirHi_Enter}
  batch->state.special_cmd0[idx] = 0u;
  batch->state.sheik_special_timer[idx] = 0u;
  batch->state.sheik_special_latch[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  if (ground) {
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    if (ch->zelda_farore_air_entry_vel_x_divisor > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->zelda_farore_air_entry_vel_x_divisor;
    }
    if (ch->zelda_farore_air_entry_vel_y_divisor > 0.0f) {
      batch->state.speed_y_self[idx] /= ch->zelda_farore_air_entry_vel_y_divisor;
    }
  }
  zd_enter(batch, idx,
           ground ? (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_0
                  : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_0,
           0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void zd_enter_farore_air_end(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float vx = batch->state.speed_air_x_self[idx];
  const float vy = batch->state.speed_y_self[idx];
  batch->state.speed_air_x_self[idx] = vx * ch->zelda_farore_end_vel_mul;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = vy * ch->zelda_farore_end_vel_mul;
  zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void zd_enter_farore_ground_end(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float vx = batch->state.speed_ground_x_self[idx];
  batch->state.speed_ground_x_self[idx] = vx * ch->zelda_farore_end_vel_mul;
  batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
  batch->state.speed_y_self[idx] = 0.0f;
  zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_HI, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static uint8_t zd_try_enter_b_special(MslBatch* batch, const MslCommonParams* c,
                                      const MslCharParams* ch, size_t idx, uint8_t ground) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.hitlag[idx] != 0u ||
      batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  const float sx = zd_deadzone(zd_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float sy = zd_deadzone(zd_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float ax = fabsf(sx);
  // Source dispatch ordering is side -> up -> down -> neutral for the full grounded lane, with
  // Dash admitting only Side-B, Run/RunDirect admitting all four, and crouch hold/reverse using
  // the Down-B-only lane above.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
  //   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6824,ftCo_800D68C0}
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_Special{N,S,Hi,Lw}.c
  if (ground) {
    if (a == (uint16_t)MSL_ACT_SQUAT_WAIT || a == (uint16_t)MSL_ACT_SQUAT_RV) {
      if (sy <= -c->special_stick_y_threshold && ax < c->special_stick_x_threshold_side) {
        zd_enter_speciallw(batch, ch, idx, 1u);
        return 1u;
      }
      return 0u;
    }
    if (!zd_action_allows_ground_special(batch, idx, a)) {
      if (!(a == (uint16_t)MSL_ACT_RUN_BRAKE &&
            (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
             batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT))) {
        return 0u;
      }
      if (ax >= c->special_stick_x_threshold_side) {
        zd_enter_specials(batch, ch, idx, 1u, sx);
        return 1u;
      }
      if (sy >= c->special_stick_y_threshold) {
        zd_enter_specialhi(batch, ch, idx, 1u);
        return 1u;
      }
      if (sy <= -c->special_stick_y_threshold) {
        zd_enter_speciallw(batch, ch, idx, 1u);
        return 1u;
      }
      zd_enter_specialn(batch, ch, idx, 1u);
      return 1u;
    }
    if (a == (uint16_t)MSL_ACT_DASH) {
      if (ax >= c->special_stick_x_threshold_side) {
        zd_enter_specials(batch, ch, idx, 1u, sx);
        dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
        batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
        return 1u;
      }
      return 0u;
    }
    if (a == (uint16_t)MSL_ACT_RUN || a == (uint16_t)MSL_ACT_RUN_DIRECT) {
      if (ax >= c->special_stick_x_threshold_side) {
        zd_enter_specials(batch, ch, idx, 1u, sx);
        return 1u;
      }
      if (sy >= c->special_stick_y_threshold) {
        zd_enter_specialhi(batch, ch, idx, 1u);
        return 1u;
      }
      if (sy <= -c->special_stick_y_threshold) {
        zd_enter_speciallw(batch, ch, idx, 1u);
        return 1u;
      }
      zd_enter_specialn(batch, ch, idx, 1u);
      return 1u;
    }
    if (ax >= c->special_stick_x_threshold_side) {
      zd_enter_specials(batch, ch, idx, 1u, sx);
      return 1u;
    }
    if (sy >= c->special_stick_y_threshold) {
      zd_enter_specialhi(batch, ch, idx, 1u);
      return 1u;
    }
    if (sy <= -c->special_stick_y_threshold) {
      zd_enter_speciallw(batch, ch, idx, 1u);
      return 1u;
    }
    zd_enter_specialn(batch, ch, idx, 1u);
    return 1u;
  }
  if (!zd_action_allows_air_special(a)) {
    return 0u;
  }
  if (sy >= c->special_stick_y_threshold) {
    zd_enter_specialhi(batch, ch, idx, 0u);
    return 1u;
  }
  if (sy <= -c->special_stick_y_threshold) {
    zd_enter_speciallw(batch, ch, idx, 0u);
    return 1u;
  }
  if (ax >= c->special_stick_x_threshold_side) {
    zd_enter_specials(batch, ch, idx, 0u, sx);
    return 1u;
  }
  zd_enter_specialn(batch, ch, idx, 0u);
  return 1u;
}

static uint8_t zd_try_enter_transform(MslBatch* batch, const MslCommonParams* c,
                                      const MslCharParams* ch, size_t idx, uint8_t ground) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.hitlag[idx] != 0u ||
      batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  if ((batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (ground) {
    if (!zd_action_allows_ground_down_special(batch, idx, a)) {
      return 0u;
    }
  } else if (!zd_action_allows_air_special(a)) {
    return 0u;
  }
  const float sy = zd_deadzone(zd_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (sy > -c->special_stick_y_threshold) {
    return 0u;
  }
  zd_enter_speciallw(batch, ch, idx, ground);
  return 1u;
}

uint8_t zelda_special_try_transform_iasa(MslBatch* batch, size_t idx, uint8_t ground) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return 0u;
  }
  return zd_try_enter_transform(batch, msl_common_params(),
                                msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA), idx, ground);
}

void zelda_special_enter_transform(MslBatch* batch, size_t idx, uint8_t ground) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA);
  if (ch == NULL) {
    return;
  }
  zd_enter_speciallw(batch, ch, idx, ground);
}

uint8_t zelda_special_try_ground_iasa(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA ||
      batch->state.on_ground[idx] == 0u) {
    return 0u;
  }
  return zd_try_enter_b_special(batch, msl_common_params(),
                                msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA), idx, 1u);
}

uint8_t zelda_special_try_air_iasa(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA ||
      batch->state.on_ground[idx] != 0u) {
    return 0u;
  }
  return zd_try_enter_b_special(batch, msl_common_params(),
                                msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA), idx, 0u);
}

static uint8_t zd_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
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
      zd_deadzone(zd_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      zd_deadzone(zd_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float facing_dir = zd_facing_dir(batch, idx);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  if (zd_try_enter_b_special(batch, c, ch, idx, 1u)) {
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

static void zd_update_specialn(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                               size_t idx, uint16_t a) {
  (void)c;
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_N:
    case MSL_ACT_ZD_SPECIAL_AIR_N: {
      const uint8_t cmd0 = (uint8_t)fighter_script_cmd_var(batch, idx, 0u);
      // ftZd_Special{Air}N_Anim creates ReflectDesc once when the script raises cmd_vars[0], then
      // keeps fp->reflecting live until the script clears cmd0. `2` is this sim's local
      // "descriptor already created" sentinel so repeated cmd0==1 frames do not recreate it.
      // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialN.c::{
      //   ftZd_SpecialN_Anim,ftZd_SpecialAirN_Anim}
      // data/scripts/zelda.bin::MSLFTSC1 SpecialN/SpecialAirN cmd_var(0) pulses
      if (cmd0 == 1u && batch->state.special_cmd0[idx] != 2u) {
        batch->state.special_cmd0[idx] = 2u;
        fighter_script_set_cmd_var(batch, idx, 0u, 2u);
      } else if (cmd0 == 0u) {
        batch->state.special_cmd0[idx] = 0u;
      }
      if (zd_anim_finished(batch, idx, a)) {
        if (a == (uint16_t)MSL_ACT_ZD_SPECIAL_N) {
          zd_enter_wait(batch, idx);
          (void)zd_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
        } else {
          zd_enter_fall(batch, idx);
        }
      }
    } break;
    default:
      break;
  }
}

static void zd_din_reset_timers(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  batch->state.sheik_special_timer[idx] =
      (ch->zelda_din_release_min_frames > 0 && ch->zelda_din_release_min_frames < 255)
          ? (uint8_t)ch->zelda_din_release_min_frames
          : 0u;
  batch->state.special_cmd1[idx] =
      (ch->zelda_din_end_min_frames > 0 && ch->zelda_din_end_min_frames < 255)
          ? (uint8_t)ch->zelda_din_end_min_frames
          : 0u;
  batch->state.special_cmd2[idx] =
      (ch->zelda_din_air_gravity_delay_frames > 0 && ch->zelda_din_air_gravity_delay_frames < 255)
          ? (uint8_t)ch->zelda_din_air_gravity_delay_frames
          : 0u;
  batch->state.sheik_special_latch[idx] =
      (ch->zelda_din_release_hold_min_frames > 0 && ch->zelda_din_release_hold_min_frames < 255)
          ? (uint8_t)ch->zelda_din_release_hold_min_frames
          : 0u;
}

static void zd_din_update_spawn_script(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const uint8_t cmd0 = (uint8_t)fighter_script_cmd_var(batch, idx, 0u);
  if (cmd0 == 1u) {
    (void)items_spawn_zelda_din_fire_article(batch, idx, ch->zelda_din_spawn_offset_x,
                                             ch->zelda_din_spawn_offset_y);
    fighter_script_clear_cmd_var(batch, idx, 0u);
    batch->state.special_cmd0[idx] = 0u;
  }
}

static uint8_t zd_din_apply_loop_iasa(MslBatch* batch, size_t idx, uint16_t a) {
  if (batch->state.sheik_special_latch[idx] != 0u) {
    batch->state.sheik_special_latch[idx]--;
  }
  if (batch->state.sheik_special_latch[idx] == 0u &&
      (batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
    zd_enter(batch, idx,
             a == (uint16_t)MSL_ACT_ZD_SPECIAL_S_LOOP ? (uint16_t)MSL_ACT_ZD_SPECIAL_S_END
                                                      : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_END,
             0.0f, 1.0f);
    return 1u;
  }
  return 0u;
}

static void zd_update_specials(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                               size_t idx, uint16_t a) {
  (void)c;
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_S_START:
    case MSL_ACT_ZD_SPECIAL_AIR_S_START:
      zd_din_update_spawn_script(batch, ch, idx);
      if (zd_anim_finished(batch, idx, a)) {
        zd_enter(batch, idx,
                 a == (uint16_t)MSL_ACT_ZD_SPECIAL_S_START
                     ? (uint16_t)MSL_ACT_ZD_SPECIAL_S_LOOP
                     : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_LOOP,
                 0.0f, 1.0f);
        batch->state.jumps_left[idx] = 0u;
        (void)zd_din_apply_loop_iasa(batch, idx, batch->state.action_id[idx]);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_S_LOOP:
    case MSL_ACT_ZD_SPECIAL_AIR_S_LOOP:
      zd_din_update_spawn_script(batch, ch, idx);
      if (batch->state.sheik_special_timer[idx] != 0u) {
        batch->state.sheik_special_timer[idx]--;
      }
      if (batch->state.special_cmd1[idx] != 0u) {
        batch->state.special_cmd1[idx]--;
      }
      if (zd_din_apply_loop_iasa(batch, idx, a) != 0u) {
        break;
      }
      if (items_zelda_din_fire_article_live(batch, idx) == 0u &&
          batch->state.sheik_special_timer[idx] == 0u && batch->state.special_cmd1[idx] == 0u) {
        zd_enter(batch, idx,
                 a == (uint16_t)MSL_ACT_ZD_SPECIAL_S_LOOP ? (uint16_t)MSL_ACT_ZD_SPECIAL_S_END
                                                          : (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_END,
                 0.0f, 1.0f);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_S_END:
      if (zd_anim_finished(batch, idx, a)) {
        zd_din_reset_timers(batch, ch, idx);
        zd_enter_wait(batch, idx);
        (void)zd_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_AIR_S_END:
      if (zd_anim_finished(batch, idx, a)) {
        zd_din_reset_timers(batch, ch, idx);
        if (ch->zelda_din_air_end_fallspecial_lag_frames == 0.0f) {
          zd_enter_fall(batch, idx);
        } else {
          zd_enter_fallspecial(batch, ch, idx, ch->zelda_din_air_end_fallspecial_lag_frames, 1.0f);
        }
      }
      break;
    default:
      break;
  }
}

static void zd_update_specialhi(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                                size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_HI_START_0:
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_0:
      if (zd_anim_finished(batch, idx, a)) {
        zd_farore_enter_travel(batch, c, ch, idx,
                               a == (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_0 ? 1u : 0u);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_HI_START_1:
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_1:
      if (batch->state.sheik_special_timer[idx] != 0u) {
        batch->state.sheik_special_timer[idx]--;
      }
      if (batch->state.sheik_special_timer[idx] == 0u) {
        if (a == (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_1) {
          zd_enter_farore_ground_end(batch, ch, idx);
        } else {
          zd_enter_farore_air_end(batch, ch, idx);
        }
      }
      break;
    case MSL_ACT_ZD_SPECIAL_HI:
      if (zd_anim_finished(batch, idx, a)) {
        zd_enter_wait(batch, idx);
        (void)zd_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, a);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_AIR_HI:
      batch->state.special_cmd0[idx] = fighter_script_cmd_var(batch, idx, 0u) != 0u ? 1u : 0u;
      if (zd_anim_finished(batch, idx, a)) {
        zd_enter_fallspecial(batch, ch, idx, ch->zelda_farore_landing_lag_frames,
                             ch->zelda_farore_fallspecial_mobility_mul);
      }
      break;
    default:
      break;
  }
}

static void zd_update_speciallw(MslBatch* batch, size_t idx, uint16_t a) {
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_LW_2:
      if (zd_anim_finished(batch, idx, a)) {
        zd_enter_wait(batch, idx);
      }
      break;
    case MSL_ACT_ZD_SPECIAL_AIR_LW_2:
      if (zd_anim_finished(batch, idx, a)) {
        zd_enter_fall(batch, idx);
      }
      break;
    default:
      break;
  }
}

void zelda_specials_update_transform_accessory4_for_fighter(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return;
  }
  const uint16_t a = batch->state.action_id[idx];
  if ((a != (uint16_t)MSL_ACT_ZD_SPECIAL_LW && a != (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_LW) ||
      !zd_anim_finished(batch, idx, a) || batch->state.frame_start_action_id[idx] != a) {
    return;
  }

  // The terminal Anim callback only installs ftZd_SpecialLw_8013AEAC into accessory4. The actual
  // twin activation therefore happens at proc priority 9, after Phys (4) and Coll (6). A same-frame
  // ground/air collision swap installs the transform-effect callback instead, replacing the pending
  // twin activation; requiring the action to survive from frame start models that callback overwrite
  // without another runtime lane.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_procMap,
  //   Fighter_8006C80C}
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::{ftZd_SpecialLw_Anim,
  //   ftZd_SpecialAirLw_Anim,ftZd_SpecialLw_8013AEAC,ftZd_SpecialLw_8013B1CC,
  //   ftZd_SpecialLw_8013B238}
  const MslCharParams* sk = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (sk == NULL) {
    return;
  }
  zd_transform_cache_visible_twin_2218(batch, idx);
  batch->state.char_id[idx] = (uint8_t)MSL_CHAR_ID_SHEIK;
  zd_enter(batch, idx,
           batch->state.on_ground[idx] ? (uint16_t)MSL_ACT_SK_SPECIAL_LW_2
                                       : (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW_2,
           sk->sheik_transform_finish_start_frame, 1.0f);
  zd_transform_set_live_2218(batch, idx, 0u);
}

uint8_t zelda_action_is_special(uint16_t action_id) {
  return (uint8_t)(action_id >= (uint16_t)MSL_ACT_ZD_SPECIAL_N &&
                   action_id <= (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_LW_2);
}

void zelda_specials_update_pre_physics_for_fighter(MslBatch* batch, const MslCommonParams* c,
                                                   const MslCharParams* ch, size_t idx,
                                                   uint8_t frame_start_owner) {
  const uint16_t a = batch->state.action_id[idx];
  if (!zelda_action_is_special(a)) {
    const uint8_t run_to_runbrake_special_owner =
        (uint8_t)(a == (uint16_t)MSL_ACT_RUN_BRAKE &&
                  (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
                   batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT));
    if (frame_start_owner != 0u || run_to_runbrake_special_owner != 0u) {
      (void)zd_try_enter_b_special(batch, c, ch, idx, batch->state.on_ground[idx] ? 1u : 0u);
    }
    return;
  }
  batch->state.animation_index[idx] = (uint32_t)zd_submotion(a);
  zd_update_specialn(batch, c, ch, idx, a);
  zd_update_specials(batch, c, ch, idx, a);
  zd_update_specialhi(batch, c, ch, idx, a);
  zd_update_speciallw(batch, idx, a);
}

static float zd_apply_air_friction(float vel, float friction) {
  float a = friction;
  if (fabsf(a) >= fabsf(vel)) {
    a = -vel;
  } else if (vel > 0.0f) {
    a = -a;
  }
  return vel + a;
}

static void zd_apply_common_fall(MslBatch* batch, const MslCharParams* ch, size_t idx, float grav,
                                 float terminal) {
  float vy = batch->state.speed_y_self[idx] - grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
  const MslCommonParams* c = msl_common_params();
  const float stick_x = zd_deadzone(zd_stick_unit(batch->state.input_main_x[idx]),
                                    c != NULL ? c->lstick_deadzone_x : 0.0f);
  const float target = stick_x * ch->air_drift_max;
  const float accel = stick_x * ch->air_drift_stick_mul +
                      ((stick_x >= 0.0f) ? ch->aerial_drift_base : -ch->aerial_drift_base);
  float vx = batch->state.speed_air_x_self[idx];
  if (target == 0.0f) {
    vx = zd_apply_air_friction(vx, ch->aerial_friction);
  } else {
    float a = accel;
    if (!(vx * a < 0.0f)) {
      if (a > 0.0f) {
        if (vx + a > target) {
          a = -ch->aerial_friction;
          if (vx + a < target) {
            a = target - vx;
          }
          if (vx + a > ch->air_max_horizontal_velocity) {
            a = ch->air_max_horizontal_velocity - vx;
          }
        }
      } else {
        if (vx + a < target) {
          a = ch->aerial_friction;
          if (vx + a > target) {
            a = target - vx;
          }
          if (vx + a < -ch->air_max_horizontal_velocity) {
            a = -ch->air_max_horizontal_velocity - vx;
          }
        }
      }
    }
    vx += a;
  }
  batch->state.speed_air_x_self[idx] = vx;
}

static void zd_apply_fall_only(MslBatch* batch, size_t idx, float grav, float terminal) {
  float vy = batch->state.speed_y_self[idx] - grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
}

static void zd_apply_attr_fall_friction_cef4(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                             float grav, float terminal) {
  float vy = batch->state.speed_y_self[idx] - grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
  batch->state.speed_air_x_self[idx] =
      zd_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
}

static void zd_apply_ground_friction_f3c(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const MslCommonParams* c = msl_common_params();
  float friction = ch->gr_friction;
  float v = batch->state.speed_ground_x_self[idx];
  if (fabsf(v) > ch->walk_max_vel && c != NULL) {
    friction *= c->high_speed_friction_mul;
  }
  if (v > 0.0f) {
    v = (v > friction) ? v - friction : 0.0f;
  } else if (v < 0.0f) {
    v = (v < -friction) ? v + friction : 0.0f;
  }
  batch->state.speed_ground_x_self[idx] = v;
  batch->state.speed_air_x_self[idx] = v;
}

uint8_t zelda_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA);
  if (ch == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_N:
    case MSL_ACT_ZD_SPECIAL_S_START:
    case MSL_ACT_ZD_SPECIAL_S_LOOP:
    case MSL_ACT_ZD_SPECIAL_S_END:
    case MSL_ACT_ZD_SPECIAL_HI_START_0:
    case MSL_ACT_ZD_SPECIAL_HI:
    case MSL_ACT_ZD_SPECIAL_LW:
    case MSL_ACT_ZD_SPECIAL_LW_2:
      zd_apply_ground_friction_f3c(batch, ch, idx);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_HI_START_1:
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_N:
      // ftZd_SpecialAirN_Phys delays gravity through mv.zd.specialn.x0, then uses the Zelda attr
      // gravity lane while ordinary air friction continues every frame.
      // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialN.c::ftZd_SpecialAirN_Phys
      if (batch->state.sheik_special_timer[idx] != 0u) {
        batch->state.sheik_special_timer[idx]--;
      } else {
        zd_apply_fall_only(batch, idx, ch->zelda_nayru_air_gravity, ch->terminal_vel);
      }
      batch->state.speed_air_x_self[idx] =
          zd_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_S_START:
    case MSL_ACT_ZD_SPECIAL_AIR_S_LOOP:
    case MSL_ACT_ZD_SPECIAL_AIR_S_END:
      // ftZd_SpecialAirS*_Phys delays air gravity through the Din air-gravity timer, then applies
      // Zelda Din's attr gravity with ordinary aerial friction.
      // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialS.c::{
      //   ftZd_SpecialAirSStart_Phys,ftZd_SpecialAirSLoop_Phys,ftZd_SpecialAirSEnd_Phys}
      if (batch->state.special_cmd2[idx] != 0u) {
        batch->state.special_cmd2[idx]--;
      } else {
        zd_apply_fall_only(batch, idx, ch->zelda_din_air_gravity, ch->terminal_vel);
      }
      batch->state.speed_air_x_self[idx] =
          zd_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_0:
      // Farore startup uses the character attr gravity/terminal pair before the travel state
      // freezes animation/velocity; this is distinct from the end-state drift clamp below.
      // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialHi.c::ftZd_SpecialAirHiStart_0_Phys
      zd_apply_common_fall(batch, ch, idx, ch->zelda_farore_start_air_gravity,
                           ch->zelda_farore_start_air_terminal_vel);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_1:
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI:
      // ftZd_SpecialAirHi_Phys switches from end-state damping to ordinary fall gravity after the
      // script writes cmd_vars[0]. The script lane is refreshed in the Anim/update pass because
      // Slippi does not serialize cmd_vars.
      // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialHi.c::ftZd_SpecialAirHi_Phys
      // data/scripts/zelda.bin::MSLFTSC1 SpecialAirHi cmd_var(0)
      if (batch->state.special_cmd0[idx] != 0u) {
        float vy = batch->state.speed_y_self[idx] - ch->grav;
        if (vy < -ch->terminal_vel) {
          vy = -ch->terminal_vel;
        }
        batch->state.speed_y_self[idx] = vy;
        const float max_x = ch->zelda_farore_air_end_drift_mul * ch->air_drift_max;
        if (batch->state.speed_air_x_self[idx] > max_x) {
          batch->state.speed_air_x_self[idx] = max_x;
        } else if (batch->state.speed_air_x_self[idx] < -max_x) {
          batch->state.speed_air_x_self[idx] = -max_x;
        }
      } else {
        batch->state.speed_y_self[idx] -= batch->state.speed_y_self[idx] * 0.1f;
        batch->state.speed_air_x_self[idx] =
            zd_apply_air_friction(batch->state.speed_air_x_self[idx], ch->aerial_friction);
      }
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_LW:
    case MSL_ACT_ZD_SPECIAL_AIR_LW_2:
      zd_apply_attr_fall_friction_cef4(batch, ch, idx, ch->zelda_transform_air_gravity,
                                       ch->zelda_transform_air_terminal_vel);
      return 1u;
    default:
      return 0u;
  }
}

uint8_t zelda_special_farore_air_travel_wallceil_end(MslBatch* batch, size_t idx, float nx,
                                                     float ny) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_1) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA);
  if (ch == NULL) {
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
      (90.0f + (float)ch->zelda_farore_wall_bounce_degrees) * (MSL_PI_F / 180.0f);
  if (!(angle > threshold)) {
    return 0u;
  }
  // ftZd_SpecialAirHiStart_1_Coll checks ceiling/walls during the travel collision callback. A
  // qualifying angle enters SpecialAirHi through the same end helper used by travel timer expiry,
  // scaling current travel velocity by ftZelda_DatAttrs::x54.
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialHi.c::{
  //   ftZd_SpecialAirHiStart_1_Coll,ftZd_SpecialHi_8013A7F4}
  // data/characters/zelda.json::{zelda_farore_wall_bounce_degrees,zelda_farore_end_vel_mul}
  zd_enter_farore_air_end(batch, ch, idx);
  return 1u;
}

uint8_t zelda_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  // Zelda Special{N,S,Hi,Lw}_Coll ground-to-air callbacks preserve the current subaction frame
  // while swapping to the aerial companion state. Farore travel uses zero anim rate in both
  // domains, matching the hidden travel timer owner rather than advancing animation.
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_Special{N,S,Hi,Lw}.c::*_Coll
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_N:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_N, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_S_START:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_START,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_S_LOOP:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_LOOP,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_S_END:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_END, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_HI_START_0:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_0,
               batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_HI_START_1:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_1,
               batch->state.anim_frame_f32[idx], 0.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_HI:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_LW:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_LW, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_LW_2:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_LW_2, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    default:
      return 0u;
  }
}

uint8_t zelda_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  // Zelda aerial special collision callbacks mirror the ground/air state pairs, except Farore end
  // lands into LandingFallSpecial with Zelda's Farore landing lag owner.
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_Special{N,S,Hi,Lw}.c::*_Coll
  switch (a) {
    case MSL_ACT_ZD_SPECIAL_AIR_N:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_N, batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_S_START:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_S_START, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_S_LOOP:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_S_LOOP, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_S_END:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_S_END, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_0:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_0,
               batch->state.anim_frame_f32[idx], 1.0f);
      batch->state.speed_air_x_self[idx] = 0.0f;
      batch->state.speed_ground_x_self[idx] = 0.0f;
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI_START_1:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_HI_START_1,
               batch->state.anim_frame_f32[idx], 0.0f);
      batch->state.speed_air_x_self[idx] = 0.0f;
      batch->state.speed_ground_x_self[idx] = 0.0f;
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_HI:
      zd_enter_landing_fallspecial(batch, msl_char_params_fast((uint8_t)MSL_CHAR_ID_ZELDA), idx);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_LW:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_LW, batch->state.anim_frame_f32[idx], 1.0f);
      return 1u;
    case MSL_ACT_ZD_SPECIAL_AIR_LW_2:
      zd_enter(batch, idx, (uint16_t)MSL_ACT_ZD_SPECIAL_LW_2, batch->state.anim_frame_f32[idx],
               1.0f);
      return 1u;
    default:
      return 0u;
  }
}
