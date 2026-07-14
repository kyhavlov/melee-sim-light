#include "action.h"
#include "shields.h"
#include "falcon_specials.h"
#include "marth_specials.h"
#include "sheik_specials.h"

#include "ids.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "dash_iasa.h"
#include "ftcommon_ecb.h"
#include "fighter_script.h"
#include "input_axis.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "trigger_input.h"
#include "jump_input.h"
#include "knockdown.h"
#include "blaster.h"
#include "shine.h"
#include "ledge.h"
#include "grab_flow.h"
#include "guard_lifecycle.h"
#include "throw_flow.h"
#include "stage_collision.h"

// -----------
// EscapeAir.c
// -----------

static inline void enter_fall_special(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  // Decomp: ftCo_EscapeAir_Anim -> ftCo_80096900(..., allow_interrupt=false, ...).
  // ftCo_80096900 routes through inline0, which enters FallSpecial with Ft_MF_KeepFastFall.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = keep_fastfall;
  // Decomp: EscapeAir enters FallSpecial via ftCo_80096900(..., arg1=1, ...), which sets xC=1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c and ftCo_FallSpecial.c
  batch->state.fallspecial_xc[idx] = 1;
  batch->state.fallspecial_landing_lag[idx] =
      (c != NULL) ? c->landing_fall_special_lag_frames : 0.0f;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
}

uint8_t escape_air_try_enter_from_air_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                 size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }

  // Decomp: ftCo_80099A58 uses `fp->input.x668 & (HSD_PAD_R|HSD_PAD_L)` (pressed-edge semantics).
  // This is the physical L/R edge, not the separate HSD_PAD_LR macro lane. Analog trigger and
  // Z synthesize HSD_PAD_LR for guard/timers/capture, but do not set physical HSD_PAD_L/R.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
  // refs/melee/src/melee/ft/fighter.c::{
  //   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  if ((buttons_pressed & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R)) == 0) {
    return 0;
  }

  // Enter EscapeAir and set initial self velocity.
  //
  // Decomp:
  // - If ABS(lstick.x) < escapeair_deadzone.x && ABS(lstick.y) < escapeair_deadzone.y: self_vel=(0,0)
  // - Else: angle = atan2f(lstick.y, lstick.x); self_vel = escapeair_force * (cosf, sinf)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C (inlineA0)
  // Note: fp->input.lstick is already global-deadzoned before EscapeAir checks the EscapeAir-specific
  // deadzone. Apply the same global deadzone here before the EscapeAir-specific deadzone.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C (fp->input.lstick)
  const float raw_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  const float raw_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  const float stick_x = apply_deadzone(raw_x, c->lstick_deadzone_x);
  const float stick_y = apply_deadzone(raw_y, c->lstick_deadzone_y);
  float vx = 0.0f;
  float vy = 0.0f;
  if (!(msl_absf(stick_x) < c->escapeair_deadzone_x &&
        msl_absf(stick_y) < c->escapeair_deadzone_y)) {
    // Decomp angle helper: ftCommon_8007D9D4 is atan2f(y, x).
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D9D4
    const float ang = atan2f(stick_y, stick_x);
    vx = c->escapeair_force * cosf(ang);
    vy = c->escapeair_force * sinf(ang);
  }

  batch->state.speed_air_x_self[idx] = vx;
  batch->state.speed_y_self[idx] = vy;
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  // ftCo_80099A9C enters through Fighter_ChangeMotionState with no preservation flags and then
  // advances the destination animation immediately. The central entry boundary owns the complete
  // source side-effect bundle; EscapeAir has no stage- or prior-action-specific entry path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_ESCAPE_AIR, (uint32_t)MSL_SM_ESCAPE_AIR, 0u,
                      0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  return 1;
}

void escape_air_update(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return;
  }

  // Anim end -> FallSpecial.
  // Decomp: ftCo_EscapeAir_Anim checks ftAnim_IsFramesRemaining.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_ESCAPE_AIR);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    enter_fall_special(batch, c, idx);
  }
}

// ---------
// Escape.c
// ---------

static inline void escape_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = 0u;
}

static inline void rebound_wait_restore_ground_from_carried_floor(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.on_ground[idx] != 0u) {
    return;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  if (ground_id == 0xFFFFu || stage_collision_floor_line_index(stage_id, ground_id) < 0) {
    return;
  }

  // Rebound_Anim exits through ft_8008A2BC -> ft_8008A348. If the source fighter is GA_Air,
  // ft_8008A348 calls ftCommon_8007D7FC before Fighter_ChangeMotionState(Wait).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A348
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  msl_ftcommon_8007d6a4(batch, ch, idx);
}

static inline void enter_escape_n(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80099894 -> ftCo_800998EC (non-Yoshi path).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:248-269.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ESCAPE_N;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ESCAPE_N;
  // Decomp: ftCo_800998EC calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_800998EC
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline void enter_escape_roll(MslBatch* batch, size_t idx, uint16_t action_id) {
  // Decomp: ftCo_8009917C -> ftCo_800992A8 -> ftCo_80099314 (default fighters).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:58-88 and :104-120.
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (action_id == (uint16_t)MSL_ACT_ESCAPE_F)
                                          ? (uint32_t)MSL_SM_ESCAPE_F
                                          : (uint32_t)MSL_SM_ESCAPE_B;
  // Decomp: ftCo_80099314 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099314
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline uint8_t ucf_shielddrop_suppresses_spotdodge(const MslBatch* batch,
                                                          const MslCommonParams* c, size_t idx,
                                                          float stick_x, float stick_y,
                                                          float cstick_y, uint8_t tilt_timer_x) {
  if (batch == NULL || c == NULL || !batch->config.ucf_enabled) {
    return 0u;
  }
  if (cstick_y <= c->spotdodge_stick_y_threshold ||
      tilt_timer_x < c->escape_flick_tilt_max_frames || stick_y <= -0.8f) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    return 0u;
  }

  // UCF 0.84 Axe-method shield-drop spotdodge suppressor:
  // - The patch hooks the shared EscapeN entry and skips to the caller's false return when the
  //   current input is a rim-coordinate shield-drop attempt on a platform.
  // - C-stick down keeps spotdodge priority.
  // - Roll must be disabled (`stick_x_hold_time >= roll_stick_frames`).
  // - The Y gate is the patch-local `-.8000` threshold; stronger down still spotdodges.
  // refs/ucf/src/shielddrop/shielddrop.S
  // refs/ucf/src/pad_buffer/pad_buffer.cpp::is_rim_coord
  const float bias = 0.0001f;
  const int ix = (int)(msl_absf(stick_x) * 80.0f - bias) + 2;
  const int iy = (int)(msl_absf(stick_y) * 80.0f - bias) + 2;
  return (uint8_t)((ix * ix + iy * iy) > (80 * 80));
}

static inline uint8_t escape_try_enter_spotdodge_from_guard_y(MslBatch* batch,
                                                              const MslCommonParams* c, size_t idx,
                                                              float stick_x, float stick_y,
                                                              float cstick_y, uint8_t tilt_timer_x,
                                                              uint8_t tilt_timer_y) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Spotdodge (EscapeN) gate (Guard IASA path).
  // Decomp: ftCo_8009980C (stick.y + x671_timer_lstick_tilt_y) and cstick.y override path
  // (ftCo_800DF8E8), called from ftCo_GuardOn_IASA / ftCo_Guard_IASA / ftCo_GuardOff_IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF8E8
  const uint8_t want_spotdodge = ((stick_y <= c->spotdodge_stick_y_threshold &&
                                   tilt_timer_y < c->spotdodge_flick_tilt_max_frames) ||
                                  (cstick_y <= c->spotdodge_stick_y_threshold))
                                     ? 1
                                     : 0;
  if (!want_spotdodge) {
    return 0;
  }
  if (ucf_shielddrop_suppresses_spotdodge(batch, c, idx, stick_x, stick_y, cstick_y,
                                          tilt_timer_x)) {
    return 0;
  }
  enter_escape_n(batch, idx);
  return 1;
}

static inline float guard_x650_from_input(const MslCommonParams* c, uint16_t buttons, uint8_t l,
                                          uint8_t r) {
  // Fighter input synthesis writes the source trigger lane in this order:
  // - analog max(L, R), deadzoned by p_ftCommonData->x10;
  // - digital L/R forces held_inputs|=HSD_PAD_LR and x650=1.0f;
  // - held Z then forces held_inputs|=HSD_PAD_LR|HSD_PAD_A and x650=p_ftCommonData->x14.
  // GuardOn entry (`ftCo_800921DC`) and hold drain (`ftCo_800925A4`) consume that x650 value,
  // which differs from the boolean "shield is held" predicate.
  // refs/melee/src/melee/ft/fighter.c:1868-1892
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4}
  if ((buttons & (uint16_t)MSL_BUTTON_Z) != 0u && c != NULL) {
    return c->z_button_trigger_value;
  }
  return msl_trigger_unit_from_input(buttons, l, r);
}

static inline uint8_t escape_try_enter_spotdodge_from_guard(MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  return escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                                 tilt_timer_x, tilt_timer_y);
}

static inline uint8_t wait_iasa_try_enter_spotdodge_before_guard_impl(MslBatch* batch,
                                                                      const MslCommonParams* c,
                                                                      size_t idx,
                                                                      uint8_t use_hsd_lr_lane) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Wait_IASA checks ftCo_80099794 before ftCo_80091A4C guard entry. ftCo_80099794 is narrower
  // than Guard IASA's ftCo_8009980C: it requires held L/R plus the inlineB0 down-stick gate and
  // does not consume the c-stick spotdodge helper.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{
  //   ftCo_80099794,ftCo_80099894,ftCo_800998EC}
  const uint16_t lr = use_hsd_lr_lane ? (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)
                                      : (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R);
  const uint16_t buttons = batch->state.input_buttons[idx];
  if ((buttons & lr) == 0u) {
    if (!use_hsd_lr_lane) {
      return 0u;
    }
    // Fighter input synthesis maps analog trigger values past p_ftCommonData->x10 into
    // held_inputs & HSD_PAD_LR before callbacks consume ftCo_80099794.
    // refs/melee/src/melee/ft/fighter.c:1868-1890
    const float trig =
        msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
    if (trig <= c->trigger_deadzone) {
      return 0u;
    }
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(stick_y <= c->spotdodge_stick_y_threshold &&
        batch->state.tilt_timer_y[idx] < c->spotdodge_flick_tilt_max_frames)) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  return escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                                 batch->state.tilt_timer_x[idx],
                                                 batch->state.tilt_timer_y[idx]);
}

uint8_t wait_iasa_try_enter_spotdodge_before_guard(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  return wait_iasa_try_enter_spotdodge_before_guard_impl(batch, c, idx, 0u);
}

uint8_t wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(MslBatch* batch, const MslCommonParams* c,
                                                          size_t idx) {
  return wait_iasa_try_enter_spotdodge_before_guard_impl(batch, c, idx, 1u);
}

uint8_t escape_try_enter_from_guard(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);

  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  // Spotdodge (EscapeN) has priority over rolls in Guard IASA.
  if (escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                              tilt_timer_x, tilt_timer_y)) {
    return 1;
  }

  // Roll (EscapeF/EscapeB) chooses direction based on the triggering axis.
  // Decomp: ftCo_8009917C picks lstick.x if gated, else cstick.x (ftCo_800DF8B0), then chooses
  // EscapeF vs EscapeB based on stick_x * facing_dir.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:58-87 and refs/melee/src/melee/ft/ft_0DF1.c:224-244.
  float choose_x = 0.0f;
  uint8_t have_x = 0;
  if (msl_absf(stick_x) >= c->escape_stick_x_threshold &&
      tilt_timer_x < c->escape_flick_tilt_max_frames) {
    choose_x = stick_x;
    have_x = 1;
  } else if (msl_absf(cstick_x) >= c->escape_stick_x_threshold) {
    choose_x = cstick_x;
    have_x = 1;
  }
  if (have_x) {
    const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    const uint16_t roll_act =
        (choose_x * facing_dir) >= 0.0f ? (uint16_t)MSL_ACT_ESCAPE_F : (uint16_t)MSL_ACT_ESCAPE_B;
    enter_escape_roll(batch, idx, roll_act);
    return 1;
  }

  return 0;
}

void escape_update_grounded(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                            size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return;
  }

  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_ESCAPE_N && a != (uint16_t)MSL_ACT_ESCAPE_F &&
      a != (uint16_t)MSL_ACT_ESCAPE_B) {
    return;
  }

  uint32_t smid = 0xFFFFFFFFu;
  if (a == (uint16_t)MSL_ACT_ESCAPE_N) {
    smid = (uint32_t)MSL_SM_ESCAPE_N;
  } else if (a == (uint16_t)MSL_ACT_ESCAPE_F) {
    smid = (uint32_t)MSL_SM_ESCAPE_F;
  } else {
    smid = (uint32_t)MSL_SM_ESCAPE_B;
  }
  batch->state.animation_index[idx] = smid;

  if (a == (uint16_t)MSL_ACT_ESCAPE_F && fighter_script_take_throw_flag(batch, idx, 3u)) {
    // Decomp: Escape_Anim flips facing when ftCheckThrowB3 consumes the script-owned bit.
    // The EscapeF script emits set_throw_flags(hit_idx=0) at the extracted action-frame threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim
    // refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
    // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
    // data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"]["events"] set_throw_flags
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
  }

  // NOTE: Escape ground velocity updates are a single-writer in physics_integrate().
  // Decomp:
  // - EscapeF/B phys: ftCo_Escape_Phys -> ft_80085004 -> ft_80085030
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Phys
  //   refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
  // - EscapeN phys: ftCo_EscapeN_Phys -> ft_80084F3C
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Phys
  //   refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C

  // Anim end -> Wait.
  // Decomp: ftCo_Escape_Anim / ftCo_EscapeN_Anim.
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    if (a == (uint16_t)MSL_ACT_ESCAPE_F || a == (uint16_t)MSL_ACT_ESCAPE_B) {
      // Decomp: ftCo_Escape_Anim zeros gr_vel at end before entering Wait.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:154-162.
      batch->state.speed_ground_x_self[idx] = 0.0f;
    }
    escape_enter_wait(batch, idx);
  }
}

// --------
// Guard.c
// --------

static inline void guard_init_lightshield_from_current_input(MslBatch* batch,
                                                             const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const float denom = 1.0f - c->trigger_deadzone;
  if (!(denom > 0.0f)) {
    batch->state.lightshield_amount[idx] = 0.0f;
    return;
  }
  const float trig = guard_x650_from_input(c, batch->state.input_buttons[idx],
                                           batch->state.input_l[idx], batch->state.input_r[idx]);
  float light = (trig - c->trigger_deadzone) / denom;
  if (light < 0.0f) {
    light = 0.0f;
  } else if (light > 1.0f) {
    light = 1.0f;
  }
  batch->state.lightshield_amount[idx] = light;
}

static inline void enter_guard_reflect_common_setup(MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.guard_reflect_timer_x14[idx] = msl_guard_reflect_timer_x14_init(c);
  batch->state.guard_reflect_timer_x18[idx] = msl_guard_reflect_timer_x18_init(c);
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.guard_release_latched_xc[idx] = 0;
}

static inline void enter_guard_reflect_install_descriptor_flags(MslBatch* batch, size_t idx) {
  // Both GuardReflect entry paths install ReflectDesc after Fighter_ChangeMotionState has cleared
  // the prior descriptors. x14/x18 then own b1/b2 expiry in ftCo_80093BC0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |=
      (uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  const size_t flags_2218_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_2218_i] |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
}

static inline void enter_guard_reflect_from_guard(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  // Decomp entry path while already guarding:
  // - ftCo_80093694 -> ftCo_80093850 -> ftCo_8009388C.
  // - This already-shielding path is the GuardOn-origin provenance used by the final-x14
  //   ShieldDesc handoff.
  // - ftCo_8009388C keeps the current anim frame and does not call ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  const float anim_start = batch->state.anim_frame_f32[idx];
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_GUARD_REFLECT, UINT32_MAX,
                      (uint32_t)(MSL_MOTION_ENTRY_SKIP_ANIM | MSL_MOTION_ENTRY_KEEP_GFX),
                      anim_start, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  enter_guard_reflect_common_setup(batch, c, idx);
  batch->state.guard_reflect_origin_guardon[idx] = 1u;
  // Fighter_ChangeMotionState clears the ordinary ShieldDesc before ftColl_CreateReflectHit
  // installs ReflectDesc on this already-guarding path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  msl_guard_set_shield_desc_active(batch, idx, 0u);
  enter_guard_reflect_install_descriptor_flags(batch, idx);
}

static inline void enter_guard_reflect_from_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                       size_t idx) {
  // Decomp entry path from locomotion guard check:
  // - ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50.
  // - ftCo_80093A50 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_GUARD_REFLECT, UINT32_MAX,
                      (uint32_t)MSL_MOTION_ENTRY_SKIP_ANIM, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  enter_guard_reflect_common_setup(batch, c, idx);
  batch->state.guard_reflect_origin_guardon[idx] = 0u;
  batch->state.guard_anim_counter_x0[idx] = 0u;
  batch->state.guard_x10[idx] = msl_guard_x10_raw_init_u8(c);
  batch->state.guard_tilt_x8[idx] = 10u;
  batch->state.guard_tilt_x4[idx] = 0.0f;
  // ftCo_80093A50 reaches ftCo_800921DC after current input has been published; locomotion
  // powershield entry initializes the live lightshield latch from that same input.x650.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_800921DC}
  guard_init_lightshield_from_current_input(batch, c, idx);
  // Direct locomotion entry recreates the ordinary ShieldDesc before installing ReflectDesc.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  msl_guard_set_shield_desc_active(batch, idx, 1u);
  enter_guard_reflect_install_descriptor_flags(batch, idx);
  // ftCo_800921DC initializes x8/x4 and immediately calls ftCo_80091E78; that call first runs
  // ftCo_80091BC4 on the current controller sample before it blends the persistent JObj tree.
  // Keep the hidden guard recurrence and the pose update in the same source callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80091E78}
  shields_guard_anim_update_tilt(batch, idx);
  (void)anim_pose_live_guard_apply(batch, idx, 0.0f);
}

static inline uint8_t dash_iasa_guard_admission_reaches_terminal_scalar(
    const MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t action_id_start,
    float action_anim_frame_start) {
  if (batch == NULL || c == NULL || action_id_start != (uint16_t)MSL_ACT_DASH) {
    return 0u;
  }
  // Dash IASA's early x4 branch checks SpecialS/item/catchdash/AttackS4/EscapeF and then jumps to
  // block_42; guard admission helpers are only called from the mid/late branches. Fighter
  // callbacks see `cur_anim_frame` after the Anim callback has advanced the timebase for the
  // current frame, so compare the callback-time frame rather than the seed post-frame value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  const float frame_step = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
  const float callback_anim_frame = action_anim_frame_start + frame_step;
  if (batch->state.dash_x4[idx] != 0u && callback_anim_frame <= c->dash_iasa_x44) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t dash_iasa_try_enter_opposite_checkinput_turn_before_guard(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t action_id_start) {
  if (batch == NULL || c == NULL || action_id_start != (uint16_t)MSL_ACT_DASH) {
    return 0u;
  }

  const float cur_anim_frame = batch->state.anim_frame_f32[idx];
  if (batch->state.dash_x4[idx] != 0u && cur_anim_frame <= c->dash_iasa_x44) {
    return 0u;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  if ((stick_x * facing_dir) >= 0.0f) {
    return 0u;
  }

  // Decomp: the mid Dash_IASA branch calls ftCo_Dash_CheckInput before ftCo_80091AD8, and
  // the late branch calls ftCo_Dash_CheckInput before ftCo_80091A4C. The opposite-facing
  // x3C/x40 path enters Turn and consumes the callback before GuardOn/GuardReflect can start.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
  //   ftCo_Dash_IASA,ftCo_Dash_CheckInput
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80091A4C}
  if (msl_absf(stick_x) < c->dash_flick_abs ||
      batch->state.tilt_timer_x[idx] >= c->dash_flick_tilt_max_frames) {
    return 0u;
  }

  batch->state.turn_has_turned[idx] = 0;
  batch->state.turn_frames_to_turn[idx] = 0;
  batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  dash_iasa_apply_root_motion_exit_gr_vel_clamp(
      batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
  return 1u;
}

static inline uint8_t dash_iasa_try_enter_a_tap_jump_after_attack_s4_miss(MslBatch* batch,
                                                                          const MslCommonParams* c,
                                                                          size_t idx) {
  if (batch == NULL || c == NULL || batch->state.jumps_left[idx] == 0u) {
    return 0u;
  }
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_A) == 0u) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  if (msl_absf(stick_x) >= c->dash_flick_abs &&
      batch->state.tilt_timer_x[idx] < c->dash_flick_tilt_max_frames) {
    return 0u;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (stick_y < c->tap_jump_threshold ||
      batch->state.tilt_timer_y[idx] >= c->tap_jump_tilt_max_frames) {
    return 0u;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_LSTICK;
  batch->state.kneebend_is_short_hop[idx] = 0u;
  return 1u;
}

static inline void enter_guard_on(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                  uint8_t entered_via_wait_callback) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:66-69 and :313-327.
  (void)entered_via_wait_callback;
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  // ftCo_800924C0 uses Ft_MF_SkipAnim. The explicit ftAnim_8006EBA4 that follows is a no-op with
  // anim_id=-1, leaving the source cur_anim_frame at anim_start-frame_speed (-1 for this entry).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_GUARD_ON, UINT32_MAX,
                      (uint32_t)MSL_MOTION_ENTRY_SKIP_ANIM, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  // GuardOn entry clears fp+0x221C GuardReflect bits before entering shield hold:
  // - x221C_b3 = 0
  // - x221C_b1 = 0
  // - x221C_b2 = 0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield mapping)
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(
      uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  // ftCo_800924C0 creates the ordinary ShieldDesc after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_80092450}
  msl_guard_set_shield_desc_active(batch, idx, 1u);
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_anim_counter_x0[idx] = 0u;
  batch->state.guard_x10[idx] = msl_guard_x10_raw_init_u8(c);
  batch->state.guard_reflect_timer_x14[idx] = 0u;
  batch->state.guard_reflect_timer_x18[idx] = 0u;
  batch->state.guard_reflect_origin_guardon[idx] = 0u;
  // ftCo_800924C0 reaches ftCo_800921DC after current input has been published, so every
  // GuardOn entry family initializes the latch from the same input.x650 owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_800921DC}
  guard_init_lightshield_from_current_input(batch, c, idx);
  shields_guard_anim_update_tilt(batch, idx);
  (void)anim_pose_live_guard_apply(batch, idx, 0.0f);
}

uint8_t wait_iasa_try_guard_after_callback(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL || batch->state.shield_hp[idx] <= 0.0f) {
    return 0u;
  }
  // Decomp callback bridge:
  // - Several Anim callbacks enter grounded Wait via ft_8008A2BC / ft_8008A348.
  // - The destination Wait_IASA can then run in the same Fighter proc. Preserve the command order
  //   up to guard: pre-guard attack/special/catch commands block this helper, spotdodge comes
  //   before ftCo_80091A4C, and guard/powershield consumes only the source HSD_PAD_LR lane.
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint16_t pre_guard_buttons = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_Z);
  if ((pressed & pre_guard_buttons) != 0u) {
    return 0u;
  }
  const float cstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  if (cstick_x != 0.0f || cstick_y != 0.0f) {
    return 0u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
    return 1u;
  }

  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((pressed & (uint16_t)LR) != 0u &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_locomotion(batch, c, idx);
    return 1u;
  }

  const float trig =
      msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  const uint8_t shield_held_inputs =
      (((buttons & (uint16_t)LR) != 0u) || trig > c->trigger_deadzone) ? 1u : 0u;
  if (shield_held_inputs) {
    enter_guard_on(batch, c, idx, 1u);
    return 1u;
  }
  return 0u;
}

static inline void enter_guard_hold(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_800928CC -> ftCo_80092908 changes motion to ftCo_MS_Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:421-446.
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_GUARD, UINT32_MAX,
                      (uint32_t)MSL_MOTION_ENTRY_SKIP_ANIM, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  // ftCo_80092908 recreates ShieldDesc after entering Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092908
  msl_guard_set_shield_desc_active(batch, idx, 1u);
  (void)anim_pose_live_guard_apply(batch, idx, 1.0f);
}

static inline void enter_guard_off(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80092BCC sets a release latch; Guard IASA transitions to GuardOff via ftCo_80092C54.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509.
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_GUARD_OFF, (uint32_t)MSL_SM_GUARD_OFF, 0u,
                      0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  // GuardOff entry only changes motion state; its reset clears the old ShieldDesc.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092C54
  msl_guard_set_shield_desc_active(batch, idx, 0u);
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_anim_counter_x0[idx] = 0u;
  batch->state.guard_x10[idx] = 0;
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void enter_shield_break_fly(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  // Shield depletion during GuardOn/Guard/GuardReflect Anim calls ftCo_800925A4, clears shield
  // active flags, enters ShieldBreakFly through ftCo_80098B20, and immediately ticks the animation.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_80098B20
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_SHIELD_BREAK_FLY;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_SHIELD_BREAK_FLY;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.on_ground[idx] = 0u;
  // ftCommon_8007D5D4 flips `ground_or_air` and locks ECB, but it does not clear the previous
  // floor line id; Slippi still exposes the Guard floor id on the break-entry post-frame.
  // ftCo_80098B20 then calls ftColl_8007B62C(..., 2), making the break state intangible through
  // the replay-visible merged hit-status lane.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C
  batch->state.jumps_left[idx] =
      (ch != NULL && ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  batch->state.script_hit_status_x1988[idx] = 2u;
  batch->state.hurtbox_state[idx] = 2u;
  msl_ftcommon_lock_ecb_8007d5d4(batch, idx);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.speed_y_self[idx] = (ch != NULL) ? ch->shield_break_initial_velocity : 0.0f;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_anim_counter_x0[idx] = 0u;
  batch->state.guard_x10[idx] = 0;
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.lightshield_amount[idx] = 0.0f;
  msl_guard_set_shield_desc_active(batch, idx, 0u);
}

static inline void guard_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_guard_set_shield_desc_active(batch, idx, 0u);
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
}

static inline void shieldbreak_enter_stand(MslBatch* batch, size_t idx, uint16_t source_action) {
  if (batch == NULL) {
    return;
  }
  // ShieldBreakDown_Anim enters ShieldBreakStandU/D when its animation ends; the destination side
  // follows the source down motion, and the transition keeps collision-animation hit status.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_ShieldBreakDown_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
  const uint8_t up = (source_action == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U) ? 1u : 0u;
  batch->state.action_id[idx] =
      up ? (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_U : (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_D;
  batch->state.animation_index[idx] =
      up ? (uint32_t)MSL_SM_SHIELD_BREAK_STAND_U : (uint32_t)MSL_SM_SHIELD_BREAK_STAND_D;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.colanim_hit_status_x198c[idx] = 2u;
  batch->state.hurtbox_state[idx] = 2u;
}

static inline float furafura_timer_init(const MslBatch* batch, const MslCommonParams* c,
                                        size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0.0f;
  }
  // Decomp: ftCo_80099010 initializes the shared fp->grab_timer from percent.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  float percent_term = c->furafura_timer_percent_base - batch->state.percent[idx];
  if (percent_term < 0.0f) {
    percent_term = 0.0f;
  }
  return percent_term + c->furafura_timer_base;
}

static inline void shieldbreak_enter_furafura(MslBatch* batch, const MslCommonParams* c,
                                              size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // ShieldBreakStand_Anim enters Furafura through ftCo_80099010; this resets shield health and
  // initializes the common grab_timer lane used by Furafura_Anim.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_ShieldBreakStand_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FURAFURA;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FURAFURA;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.shield_hp[idx] = c->shield_break_reset_health;
  batch->state.capture_grab_timer[idx] = furafura_timer_init(batch, c, idx);
  // Furafura entry does not keep ShieldBreakStand's collision-animation hit status:
  // ftCo_80099010 changes motion with only SkipModel | SkipMatAnim, while ShieldBreakStand used
  // KeepColAnimHitStatus | SkipColAnim. Clear the hidden x198C timer/status lanes along with the
  // replay-visible hurtbox state on the destination row.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  batch->state.colanim_hit_status_x198c[idx] = 0u;
  batch->state.colanim_timer_x1990[idx] = 0u;
  batch->state.colanim_timer_x1994[idx] = 0u;
  batch->state.hurtbox_state[idx] = 0u;
}

static inline uint8_t furafura_grab_mash_active(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  float stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  float stick_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  stick_x = apply_deadzone(stick_x, c->lstick_deadzone_x);
  stick_y = apply_deadzone(stick_y, c->lstick_deadzone_y);

  int8_t next_x = batch->state.grab_mash_stick_x_sign[idx];
  int8_t next_y = batch->state.grab_mash_stick_y_sign[idx];
  if (stick_x < -c->grab_mash_stick_threshold) {
    next_x = -1;
  } else if (stick_x > c->grab_mash_stick_threshold) {
    next_x = 1;
  }
  if (stick_y < -c->grab_mash_stick_threshold) {
    next_y = -1;
  } else if (stick_y > c->grab_mash_stick_threshold) {
    next_y = 1;
  }
  if (batch->state.grab_mash_stick_x_sign[idx] != next_x ||
      batch->state.grab_mash_stick_y_sign[idx] != next_y) {
    batch->state.grab_mash_stick_x_sign[idx] = next_x;
    batch->state.grab_mash_stick_y_sign[idx] = next_y;
    return 1u;
  }
  return 0u;
}

static inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline uint8_t guard_try_enter_jump_oos(MslBatch* batch, const MslCommonParams* c,
                                               size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Guard jump OoS decomp path is ftCo_800CB024:
  // - ftCo_Jump_CheckInput (tap jump or XY),
  // - then c-stick-up check (ftCo_800DF910).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA,ftCo_GuardOff_IASA
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF910
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  MslJumpInput jump_input = MSL_JUMP_INPUT_NONE;
  if (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) {
    jump_input = MSL_JUMP_INPUT_LSTICK;
  } else if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) {
    jump_input = MSL_JUMP_INPUT_XY;
  } else if (cstick_y >= c->tap_jump_threshold) {
    jump_input = MSL_JUMP_INPUT_CSTICK;
  }
  if (jump_input == MSL_JUMP_INPUT_NONE) {
    return 0;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.kneebend_jump_input[idx] = (uint8_t)jump_input;
  batch->state.kneebend_is_short_hop[idx] = 0;
  return 1;
}

static inline uint8_t guard_try_enter_iasa_defense(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Decomp GuardOn/Guard/GuardReflect IASA order:
  // item throw -> spotdodge -> roll -> catch -> jump -> taunt.
  // This sim currently models the shield-defense subset (spotdodge/roll + catch + jump) in this order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA
  // }
  if (escape_try_enter_from_guard(batch, c, idx)) {
    return 1;
  }
  if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
    return 1;
  }
  if (guard_try_enter_jump_oos(batch, c, idx)) {
    return 1;
  }
  return 0;
}

static inline uint8_t apply_shield_hold_drain(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                              float trig_unit,
                                              uint8_t preserve_lightshield_amount) {
  // Decomp (GALE01): ftCo_800925A4 updates fp->lightshield_amount with a negative-input latch and
  // drains shield HP using the resulting value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  const float denom = 1.0f - c->trigger_deadzone;
  if (!(denom > 0.0f)) {
    return 0u;
  }
  float light = batch->state.lightshield_amount[idx];
  if (!preserve_lightshield_amount) {
    const float t = (trig_unit - c->trigger_deadzone) / denom;
    if (t >= 0.0f) {
      light = clamp01(t);
    }
  }
  batch->state.lightshield_amount[idx] = light;
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;

  float hp = batch->state.shield_hp[idx];
  hp -= drain;
  if (hp < 0.0f) {
    hp = 0.0f;
    batch->state.shield_hp[idx] = hp;
    return 1u;
  }
  batch->state.shield_hp[idx] = hp;
  return 0u;
}

void guard_update_shield_recharge(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Empirically (and in replays), shield recharge can happen during GuardOff; the source gate is
  // the live shield-active bit, resolved by the shared Guard lifecycle helper.
  if (msl_guard_lifecycle_blocks_shield_recharge(batch, idx)) {
    return;
  }
  msl_guard_lifecycle_apply_shield_recharge(batch, c, idx);
}

static inline void action_update_shield_recharge_post_state(MslBatch* batch,
                                                            const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      guard_update_shield_recharge(batch, c, idx);
    }
  }
}

static inline void guard_update_grounded_anim_callback_pre_input(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const uint16_t action = batch->state.action_id[idx];
  const uint8_t anim_runs = batch->state.hitlag_started_frame[idx] == 0u ? 1u : 0u;

  // GuardReflect/GuardSetOff anim-callback timing (prio 1):
  // - ftCo_GuardReflect_Anim calls ftCo_80093BC0 (x14/x18 tick + expire clears), then GuardOn_Anim.
  // - ftCo_GuardSetOff_Anim also calls ftCo_80093BC0 while shieldstun anim owns GuardDesc state.
  // - Fighter_8006A360 runs this under !hitlag.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (action == (uint16_t)MSL_ACT_GUARD_REFLECT || action == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    if (anim_runs != 0u) {
      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      const size_t flags_2218_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      // ftCo_80093BC0 clears the one-frame b3 flag before ticking either descriptor timer.
      batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;
      if ((batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221C_B1) != 0u) {
        uint8_t t14 = batch->state.guard_reflect_timer_x14[idx];
        if (t14 > 0u) {
          t14--;
          batch->state.guard_reflect_timer_x14[idx] = t14;
        }
        if (t14 == 0u) {
          // Runtime stores x14 with a +1 bias, so zero is source x14 < 0. ftCo_80093BC0 clears
          // ReflectDesc ownership and immediately recreates the ordinary ShieldDesc through
          // ftCo_80092450; it does not change motion state.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
          batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
          batch->state.state_flags[flags_2218_i] &=
              (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
          msl_guard_set_shield_desc_active(batch, idx, 1u);
        }
      }
      if ((batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
        uint8_t t18 = batch->state.guard_reflect_timer_x18[idx];
        if (t18 > 0u) {
          t18--;
          batch->state.guard_reflect_timer_x18[idx] = t18;
        }
        if (t18 == 0u) {
          // Runtime stores x18 with the same +1 bias as x14. Zero therefore already represents
          // the source timer below zero, and ftCo_80093BC0 clears x221C_b2 on this callback even
          // when the post-frame seed begins at that expired value.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
          batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
      }

      if (action == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
        // GuardSetOff_Anim does not tail-call GuardOn_Anim after x14 expiry. Its remaining work is
        // the GuardDamage animation-end transition and shield scale publication.
        const float end =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
        if (end > 0.0f && batch->state.anim_frame_f32[idx] >= end) {
          if (batch->state.guard_release_latched_xc[idx] != 0u) {
            enter_guard_off(batch, idx);
          } else {
            enter_guard_hold(batch, idx);
          }
        }
        return;
      }
    }
  } else {
    // Keep GuardReflect timers strictly callback-owner action-scoped to avoid stale seeded carryover.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    batch->state.guard_reflect_timer_x14[idx] = 0;
    batch->state.guard_reflect_timer_x18[idx] = 0;
    batch->state.guard_reflect_origin_guardon[idx] = 0u;
  }

  if (anim_runs == 0u) {
    return;
  }

  if (action == (uint16_t)MSL_ACT_GUARD_ON || action == (uint16_t)MSL_ACT_GUARD ||
      action == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardOn/Guard/GuardReflect Anim all run ftCo_800925A4 before the input callback. The
    // callback first increments mv.co.guard.x0, then publishes lightshield, drain, and x10 from
    // the live pre-input fp->input.x650 snapshot.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardOn_Anim,ftCo_Guard_Anim,ftCo_GuardReflect_Anim,ftCo_800925A4}
    if (batch->state.guard_anim_counter_x0[idx] < UINT16_MAX) {
      batch->state.guard_anim_counter_x0[idx]++;
    }
    const size_t flags_221b_i =
        idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    const uint8_t shield_desc_active = (batch->state.state_flags[flags_221b_i] &
                                        (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u
                                           ? 1u
                                           : 0u;
    if (shield_desc_active != 0u) {
      // ftCo_800925A4 owns lightshield publication, shield drain, and x10 decrement only while
      // the ordinary ShieldDesc is live. GuardReflect can temporarily own ReflectDesc alone;
      // those callback frames still increment x0 and update the guard pose, but they do not burn
      // shield or the release lockout.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_800925A4,ftCo_GuardReflect_Anim,ftCo_80093BC0}
      const float trig = guard_x650_from_input(
          c, batch->state.input_buttons[idx], batch->state.input_l[idx], batch->state.input_r[idx]);
      if (apply_shield_hold_drain(batch, c, idx, trig, 0u) != 0u) {
        enter_shield_break_fly(batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
        return;
      }
      if (batch->state.guard_x10[idx] > 0u) {
        batch->state.guard_x10[idx] = (uint8_t)(batch->state.guard_x10[idx] - 1u);
      }
    }
    shields_guard_anim_update_tilt(batch, idx);

    if (action == (uint16_t)MSL_ACT_GUARD_ON || action == (uint16_t)MSL_ACT_GUARD_REFLECT) {
      const float end = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_ON);
      if (end > 0.0f && (float)batch->state.guard_anim_counter_x0[idx] >= end) {
        enter_guard_hold(batch, idx);
      } else if (end > 0.0f) {
        (void)anim_pose_live_guard_apply(batch, idx,
                                         (float)batch->state.guard_anim_counter_x0[idx] / end);
      }
    } else {
      (void)anim_pose_live_guard_apply(batch, idx, 1.0f);
    }
  }
}

static inline void rebound_update_anim_callback_pre_input(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const uint16_t a0 = batch->state.action_id[idx];
  if (a0 != (uint16_t)MSL_ACT_REBOUND_STOP && a0 != (uint16_t)MSL_ACT_REBOUND) {
    return;
  }
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }

  float rebound_anim_speed = msl_f32_from_q16_16(batch->state.rebound_anim_rate_fp_q16_16[idx]);
  if (!(rebound_anim_speed > 0.0f)) {
    rebound_anim_speed = 1.0f;
  }
  if (c != NULL && ch != NULL) {
    const float source_x0 = (batch->state.rebound_ground_accel_2[idx] != 0.0f)
                                ? batch->state.rebound_ground_accel_2[idx]
                                : batch->state.speed_ground_x_self[idx];
    const float rebound_speed_abs = msl_absf(source_x0);
    // Rebound anim-rate ownership:
    // - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
    // - Runtime clank entry carries that exact hidden rate. Replay-facing ReboundStop hitlag-tail
    //   seeds may still only have xE8, so fall back to reconstructing x191C from the queued
    //   `mv.co.rebound.x0` lane before it is consumed.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
    // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
    if (batch->state.rebound_anim_rate_fp_q16_16[idx] <= 0 && c->rebound_ground_x0_mul > 0.0f &&
        rebound_speed_abs > c->rebound_ground_x0_base) {
      const float rebound_x191c =
          (rebound_speed_abs - c->rebound_ground_x0_base) / c->rebound_ground_x0_mul;
      if (rebound_x191c > 0.0f) {
        rebound_anim_speed = (ch->rebound_anim_numerator_frames + 0.1f) / rebound_x191c;
      }
    }
  }

  if (a0 == (uint16_t)MSL_ACT_REBOUND_STOP) {
    // ReboundStop_Anim callback ownership:
    // - ftCo_ReboundStop_Anim immediately calls ftCo_80099E44.
    // - ftCo_80099E44 enters Rebound through Fighter_ChangeMotionState.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    //   ftCo_ReboundStop_Anim,ftCo_80099E44
    // }
    // refs/melee/src/melee/ft/chara/ftCommon/forward.h::{
    //   ftCo_MS_ReboundStop,ftCo_MS_Rebound,ftCo_SM_Rebound
    // }
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBOUND;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_REBOUND;
    msl_anim_timebase_enter(batch, idx, 0.0f, rebound_anim_speed);
    batch->state.rebound_anim_rate_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_anim_speed);
    return;
  }

  if (batch->state.action_frame[idx] == 0 && rebound_anim_speed > 0.0f &&
      batch->state.rebound_anim_rate_fp_q16_16[idx] > 0) {
    batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_anim_speed);
  }

  // Rebound_Anim callback ownership:
  // - Rebound ends through ft_8008A2BC (Wait enter) when the submotion has no frames remaining.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_REBOUND);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    rebound_wait_restore_ground_from_carried_floor(batch, idx);
    escape_enter_wait(batch, idx);
  }
}

void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx,
                           uint8_t allow_entry) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];
  const float a0_anim_frame = batch->state.anim_frame_f32[idx];
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  const uint8_t guard_on_fresh_entry_from_non_shield_snapshot =
      // Decomp ownership: input callbacks run once per fighter per frame (Fighter_procUpdate).
      // When Wait/Damage IASA enters GuardOn via ftCo_80091A4C -> ftCo_800924C0, GuardOn_IASA must
      // not consume the same input again in that frame.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0,ftCo_GuardOn_IASA}
      // Scope gate: GuardOn entry from a non-shield owner has already consumed this frame's
      // callback lane, so suppress immediate re-consume regardless of jump-button edge source.
      //
      // This now also covers the grounded Damage_IASA Z-bridge in src/knockdown.c; keeping the
      // broader fresh-entry suppression is decomp-shaped once the earlier Wait_IASA attack owners
      // are modeled ahead of guard in that grounded damage subset.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
      (a0 == (uint16_t)MSL_ACT_GUARD_ON && batch->state.action_frame[idx] < 0 &&
       batch->state.animation_index[idx] == 0xFFFFFFFFu &&
       msl_guard_action_entered_this_frame(batch, idx, (uint16_t)MSL_ACT_GUARD_ON) != 0u &&
       !msl_guard_lifecycle_action_has_shield_callback(batch->state.prev_action_id[idx]))
          ? 1u
          : 0u;
  const uint8_t guardreflect_fresh_entry_from_this_callback =
      // Source ordering: GuardReflect entry helpers are reached from IASA during
      // Fighter_procUpdate, after Fighter_8006A360 has already run this frame's Anim callback.
      // If an earlier simulated owner in this same input-callback pass entered GuardReflect, do
      // not let a later shared guard pass immediately run GuardReflect_Anim/GuardOn_Anim and drain
      // shield HP one source frame early. Seeded replay rows keep this marker clear, so their
      // normal next-frame GuardReflect drain still runs.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_8009388C,ftCo_80093A50,ftCo_GuardReflect_Anim,ftCo_800925A4}
      (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[idx] < 0 &&
       batch->state.animation_index[idx] == 0xFFFFFFFFu &&
       msl_guard_action_entered_this_frame(batch, idx, (uint16_t)MSL_ACT_GUARD_REFLECT) != 0u)
          ? 1u
          : 0u;

  if (!msl_guard_lifecycle_action_has_shield_callback(a0)) {
    batch->state.guard_release_latched_xc[idx] = 0;
    batch->state.guard_anim_counter_x0[idx] = 0u;
    batch->state.guard_x10[idx] = 0;
    batch->state.lightshield_amount[idx] = 0.0f;
    batch->state.guard_entry_via_dash_91ad8[idx] = 0u;
  }
  batch->state.guard_reflect_entry_dash_terminal_scalar[idx] = 0u;

  const float trig = guard_x650_from_input(c, batch->state.input_buttons[idx],
                                           batch->state.input_l[idx], batch->state.input_r[idx]);
  // Decomp uses held_inputs & HSD_PAD_LR for guard entry/release ownership.
  // Keep this aligned with the input owner that builds the sim's LR-held lane:
  // - digital L/R,
  // - trigger past the common deadzone,
  // - Z-mapped LR lane used by input.x668 / held_inputs plumbing.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092BCC}
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10_Inner1
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  const uint16_t held_buttons = batch->state.input_buttons[idx];
  const uint8_t shield_held_inputs =
      (((held_buttons & (uint16_t)(LR | MSL_BUTTON_Z)) != 0u) || (trig > c->trigger_deadzone)) ? 1u
                                                                                               : 0u;
  // Guard release lockout (mv.co.guard.xC + mv.co.guard.x10) is modeled explicitly and seeded via
  // replay-history preprocessing (Slippi does not expose move vars directly).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC (xC latch)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4 (x10 tick)

  // GuardSetOff_Anim ran in the priority-1 phase. If it did not transition there, its IASA
  // callback is empty and no current-input work remains.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_GuardSetOff_IASA}
  if (a0 == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    return;
  }

  // ----------------
  // Guard state loop
  // ----------------
  if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD || a0 == MSL_ACT_GUARD_REFLECT) {
    batch->state.animation_index[idx] = 0xFFFFFFFFu;
    const uint8_t can_update = (batch->state.hitlag_started_frame[idx] == 0) ? 1 : 0;
    uint8_t guard_reflect_from_guard_pending = 0u;
    if (guard_on_fresh_entry_from_non_shield_snapshot ||
        guardreflect_fresh_entry_from_this_callback) {
      return;
    }

    if (can_update) {
      // Powershield / GuardReflect entry (while guarding).
      //
      // Decomp: ftCo_80093694:
      //   if (fp->mv.co.guard.x0 < p_ftCommonData->x2A0 &&
      //       fp->input.x668 & (HSD_PAD_R | HSD_PAD_L) &&
      //       fp->x672_input_timer_counter < p_ftCommonData->x2A0)
      //     ftCo_80093850(gobj);
      //
      // mv.co.guard.x0 is now persistent source state, independent of Slippi's no-submotion
      // animation fields. The generated class remains only for the narrow replay-seed x672 phase
      // that is hidden at a fresh grounded-locomotion GuardOn boundary.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      const uint8_t guardon_frame_start_x672_seed =
          (a0 == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.guard_anim_counter_x0[idx] <
               (uint16_t)c->powershield_reflect_window_frames &&
           batch->state.x672_input_timer_frame_start[idx] <= 1u &&
           !msl_guard_lifecycle_action_has_shield_callback(batch->state.seed_prev_action_id[idx]) &&
           // MSLMSO01 separates this frame-start x672 powershield bridge from the broader fresh
           // GuardOn item ShieldDesc owner. Landing_IASA can publish ShieldDesc, but its follow-up
           // GuardOn_IASA consumes live x672 rather than this replay frame-start lane.
           // data/motion_state/owners/*.bin::MSLMSO01 GUARDON_FRAME_START_X672_IASA
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
           msl_motion_state_common_class_has_fast(batch->state.seed_prev_action_id[idx],
                                                  MSL_MS_CLASS_GUARDON_FRAME_START_X672_IASA))
              ? 1u
              : 0u;
      const uint8_t guardon_x672_for_reflect = guardon_frame_start_x672_seed
                                                   ? batch->state.x672_input_timer_frame_start[idx]
                                                   : batch->state.x672_input_timer[idx];
      if (a0 == (uint16_t)MSL_ACT_GUARD_ON &&
          batch->state.guard_anim_counter_x0[idx] <
              (uint16_t)c->powershield_reflect_window_frames &&
          (batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0 &&
          guardon_x672_for_reflect < c->powershield_reflect_window_frames) {
        guard_reflect_from_guard_pending = 1u;
      }

      // The priority-1 owner has already run ftCo_800925A4, so this IASA callback observes the
      // post-decrement x10 exactly as inlineC0 does.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      const uint8_t x10_pre = batch->state.guard_x10[idx];
      // Guard release latch ownership (ftCo_80092BCC):
      // - level check: if (!(held_inputs & HSD_PAD_LR)) xC = true.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
      if (!shield_held_inputs) {
        batch->state.guard_release_latched_xc[idx] = 1;
      }
      // Decomp: Guard IASA exits to GuardOff only once (xC && x10==0).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      if (batch->state.guard_release_latched_xc[idx] && x10_pre == 0u) {
        enter_guard_off(batch, idx);
        return;
      }

      // Decomp: inlineC0 decrements mv.co.guard.x1C only when GuardOn/Guard/GuardReflect IASA
      // does not exit to GuardOff. GuardOff_IASA then uses the non-zero timer to allow the full
      // special/attack chain after a powershield shield contact.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_GuardOff_IASA}
      if (batch->state.guard_special_enable_timer_x1c[idx] > 0u) {
        batch->state.guard_special_enable_timer_x1c[idx] =
            (uint8_t)(batch->state.guard_special_enable_timer_x1c[idx] - 1u);
      }
    }

    if (guard_reflect_from_guard_pending &&
        batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON) {
      // Decomp callback order is GuardOn_Anim then GuardOn_IASA, so same-frame GuardReflect entry
      // from GuardOn must observe the already-applied GuardOn drain/x10 owner work before IASA
      // consumes the LR edge.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
      enter_guard_reflect_from_guard(batch, c, idx);
      return;
    }

    // Shield defensive options (grounded).
    //
    // Decomp call site: ftCo_GuardOn_IASA / ftCo_Guard_IASA.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:393-409 and :454-469.
    //
    // Minimal OoS subset in decomp IASA order (defensive options): spotdodge/roll then jump.
    if (guard_try_enter_iasa_defense(batch, c, idx)) {
      if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        batch->state.guard_reflect_timer_x14[idx] = 0;
        batch->state.guard_reflect_timer_x18[idx] = 0;
      }
      return;
    }
    return;
  }

  // GuardOff: wait for animation end then go back to Wait.
  if (a0 == MSL_ACT_GUARD_OFF) {
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
    // GuardOff IASA: when mv.co.guard.x1C is live, decomp tries the special/attack chain before
    // the spotdodge/jump fallback. Specials are modeled in the later B-special passes, so do not
    // let the fallback consume B-press rows first.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_IASA
    const uint8_t guardoff_special_chain_pending =
        (batch->state.guard_special_enable_timer_x1c[idx] != 0u &&
         (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) != 0u)
            ? 1u
            : 0u;
    // GuardOff fallback IASA: allow spotdodge + jump, but not rolls.
    //
    // Decomp: ftCo_GuardOff_IASA calls spotdodge check (ftCo_8009980C) and jump check (ftCo_800CB024),
    // but does *not* call the roll check (ftCo_8009917C). Allowing EscapeF/B here causes a dominant
    // GuardOff->EscapeB mismatch cluster in teacher-forced one-step eval.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009917C
    if (!guardoff_special_chain_pending) {
      if (escape_try_enter_spotdodge_from_guard(batch, c, idx)) {
        return;
      }
      if (guard_try_enter_jump_oos(batch, c, idx)) {
        return;
      }
    }
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_OFF);
    if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
      guard_enter_wait(batch, idx);
      {
        // GuardOff_Anim can enter Wait before this frame's input callback dispatch. If the
        // destination Wait_IASA reaches ftCo_80091A4C, held shield enters GuardOn/GuardReflect on
        // the same source frame. Keep this at the anim-end handoff and require the earlier
        // Wait_IASA command families to be absent so we do not turn attacks, specials, catch, or
        // spotdodge into guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
        const uint16_t pre_guard_buttons = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_Z);
        const uint8_t cstick_command =
            (apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x) !=
                 0.0f ||
             apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y) !=
                 0.0f)
                ? 1u
                : 0u;
        const float stick_y =
            apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
        const uint8_t spotdodge_before_guard =
            (stick_y < -c->crouch_stick_threshold && shield_held_inputs) ? 1u : 0u;
        const uint8_t wait_iasa_can_reach_guard =
            (shield_held_inputs && batch->state.shield_hp[idx] > 0.0f &&
             (batch->state.input_buttons[idx] & pre_guard_buttons) == 0u &&
             (batch->state.input_buttons_pressed[idx] & pre_guard_buttons) == 0u &&
             !cstick_command && !spotdodge_before_guard)
                ? 1u
                : 0u;
        if (wait_iasa_can_reach_guard) {
          if ((batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0u &&
              batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
            enter_guard_reflect_from_locomotion(batch, c, idx);
          } else {
            enter_guard_on(batch, c, idx, 1u);
          }
        }
      }
      return;
    }
    return;
  }

  // ----------------
  // Guard entry gate
  // ----------------
  if (!allow_entry) {
    return;
  }

  // Dash IASA: early shield-hold forces EscapeF.
  //
  // Decomp:
  // - ftCo_Dash_IASA calls ftCo_80099264 when fp->cur_anim_frame <= p_ftCommonData->x48.
  // - ftCo_80099264 enters EscapeF if (held_inputs & HSD_PAD_LR).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099264
  //
  // Approximation:
  // - Keep decomp ordering/gating: ftCo_80099264 is only reached in Dash IASA early branch
  //   (dash.x4 != 0 && cur_anim_frame <= p_ftCommonData->x44), then checks held_inputs&LR.
  // - Use extracted p_ftCommonData->x44/x48 via common params (dash_iasa_x44/dash_iasa_x48).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  if (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u && shield_held_inputs &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x44 &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x48) {
    enter_escape_roll(batch, idx, (uint16_t)MSL_ACT_ESCAPE_F);
    dash_iasa_apply_root_motion_exit_gr_vel_clamp(
        batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
    dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
    return;
  }

  if (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x44) {
    // Decomp: the early Dash_IASA branch (`dash.x4 != 0 && cur_anim_frame <= x44`) checks
    // SpecialS/item/CatchDash/AttackS4/EscapeF and then reaches block_42 without calling the guard
    // helper. Guard/GuardReflect admission starts in the later Dash_IASA branches. The narrow
    // A+tap-jump case below is the block_42 path after AttackS4_8008C114 misses the side-smash
    // threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    if (dash_iasa_try_enter_a_tap_jump_after_attack_s4_miss(batch, c, idx)) {
      return;
    }
    return;
  }

  if (dash_iasa_try_enter_opposite_checkinput_turn_before_guard(batch, c, idx, a0)) {
    return;
  }

  // Decomp: ftCo_80091A4C (used by grounded locomotion IASA functions like Wait/Walk/Run/Turn).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-70 and ftCo_Wait.c:43-66.
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)LR) != 0 &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_locomotion(batch, c, idx);
    if (dash_iasa_guard_admission_reaches_terminal_scalar(batch, c, idx, a0, a0_anim_frame)) {
      dash_iasa_apply_root_motion_exit_gr_vel_clamp(
          batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
      dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
      const float frame_step = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
      batch->state.guard_reflect_entry_dash_terminal_scalar[idx] =
          (a0 == (uint16_t)MSL_ACT_DASH && a0_anim_frame <= (c->dash_iasa_x44 + frame_step)) ? 1u
                                                                                             : 0u;
    }
    return;
  }

  if (shield_held_inputs && batch->state.shield_hp[idx] > 0.0f) {
    const uint8_t entered_via_dash_91ad8 =
        (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u &&
         batch->state.anim_frame_f32[idx] <= c->dash_iasa_x4c)
            ? 1u
            : 0u;
    enter_guard_on(batch, c, idx, 0u);
    batch->state.guard_entry_via_dash_91ad8[idx] = entered_via_dash_91ad8;
    if (dash_iasa_guard_admission_reaches_terminal_scalar(batch, c, idx, a0, a0_anim_frame)) {
      dash_iasa_apply_root_motion_exit_gr_vel_clamp(
          batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
      dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
    }
    return;
  }
}

static inline void shieldbreak_update_anim_callback_pre_input(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint16_t a0 = batch->state.action_id[idx];
  if (a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U && a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D &&
      a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_U &&
      a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_D && a0 != (uint16_t)MSL_ACT_FURAFURA) {
    return;
  }
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }
  if (a0 == (uint16_t)MSL_ACT_FURAFURA) {
    // Furafura_Anim keeps shield health pinned to x280, decrements fp->grab_timer, applies mash,
    // and exits to Wait through ft_8008A2BC when the timer expires.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_Furafura_Anim
    // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    batch->state.shield_hp[idx] = c->shield_break_reset_health;
    float timer = batch->state.capture_grab_timer[idx];
    if (!(timer > 0.0f)) {
      timer = furafura_timer_init(batch, c, idx);
      const int16_t af = batch->state.action_frame[idx];
      if (af > 0) {
        timer -= (float)af * c->furafura_timer_decrement;
      }
    }
    timer -= c->furafura_timer_decrement;
    if (furafura_grab_mash_active(batch, c, idx)) {
      timer -= c->furafura_mash_decrement;
    }
    batch->state.capture_grab_timer[idx] = timer;
    if (timer <= 0.0f) {
      guard_enter_wait(batch, idx);
      batch->state.capture_grab_timer[idx] = 0.0f;
    }
    return;
  }

  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const float end = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
  if (!(end > 0.0f) || msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]) < end) {
    return;
  }

  if (a0 == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U || a0 == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D) {
    shieldbreak_enter_stand(batch, idx, a0);
  } else {
    shieldbreak_enter_furafura(batch, c, idx);
  }
}

void action_update_anim_callback_pre_input_fighter(const MslFighterCallbackContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const MslCommonParams* c = msl_common_params();
  // Decomp ordering anchor:
  // - fighter Anim callbacks run in Fighter_8006A360 (prio 1) under !hitlag.
  // - input callback (IASA checks) runs later in Fighter_procUpdate (prio 3).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // Scope (current slice): only GuardReflect x14/x18 timer tick/expire is modeled here; this phase
  // must not consume current-frame input edges.
  const int bi = ctx->bi;
  const int p = ctx->p;
  const size_t idx = ctx->idx;
  // Gameplay command opcodes execute causally in fighter_script_advance immediately after the
  // AObj step. Action callbacks consume the resulting live state; they do not query the script
  // timeline a second time.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071998,ftAction_80073240}
  locomotion_update_anim_callback_pre_input_fighter(batch, bi, p);
  blaster_update_anim_callback_pre_input_fighter(batch, bi, p);
  rebound_update_anim_callback_pre_input(batch, idx);
  shieldbreak_update_anim_callback_pre_input(batch, c, idx);
  guard_update_grounded_anim_callback_pre_input(batch, idx);
  grab_flow_update_anim_callback_pre_input(batch, bi, p);
  throw_flow_update_anim_callback_pre_input(batch, bi, p);
}

void action_update(MslBatch* batch) {
  const MslCommonParams* c = msl_common_params();
  if (batch != NULL) {
    const int num_players = (int)batch->config.num_players;
    for (int bi = 0; bi < batch->batch_size; bi++) {
      for (int p = 0; p < num_players; p++) {
        const size_t idx = msl_idx_player(bi, p);
        batch->state.guard_entry_via_dash_91ad8[idx] = 0u;
        batch->state.guard_reflect_entry_dash_terminal_scalar[idx] = 0u;
        batch->state.shine_jump_iasa_entered_this_frame[idx] = 0u;
      }
    }
  }
  // Grab/throw Anim-callback-shaped transitions should happen before the grounded locomotion IASA
  // chain (including shield entry). Example: Catch/CatchDash Anim end -> Wait (ft_8008A2BC) should
  // run before the next state's guard entry check (ftCo_80091A4C) so buffered shields can block
  // on the first actionable frame after a whiffed grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_CatchDash_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
  if (batch != NULL) {
    const int players = (int)batch->config.num_players;
    for (int bi = 0; bi < batch->batch_size; bi++) {
      for (int p = 0; p < players; p++) {
        grab_flow_update_iasa(batch, bi, p);
      }
    }
  }
  // Run knockdown/damage Anim+IASA before generic locomotion so DamageFly->DamageFall transitions
  // can feed same-frame DamageFall IASA (e.g. ftCo_800CB870 jump check) in locomotion.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Anim,ftCo_DamageFlyRoll_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  knockdown_update_pre_physics(batch);
  locomotion_update_pre(batch);
  ledge_update_pre_physics(batch);
  // Keep Shine before Blaster so Down-B owns B-edge + down-stick entry; blaster resolver is
  // intentionally Neutral/Side/Up-only and relies on this ordering.
  shine_update_pre_physics(batch);
  blaster_update_pre_physics(batch);
  marth_specials_update_pre_physics(batch);
  sheik_specials_update_pre_physics(batch);
  falcon_specials_update_pre_physics(batch);
  // Shield recharge is owned by Fighter_ProcessHit_8006D1EC under the `!fp->x221A_b7` gate, not
  // by locomotion. Run it after the frame's state-entry callbacks so the gate observes the current
  // state (for example SpecialLwStart after a shine entry), and do not suppress it during hitlag.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  action_update_shield_recharge_post_state(batch, c);
}
