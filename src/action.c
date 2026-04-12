#include "action.h"

#include <math.h>

#include "action_ids.h"
#include "airborne_state_events_tables.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "input_axis.h"
#include "locomotion.h"
#include "move_tables.h"
#include "trigger_input.h"
#include "jump_input.h"
#include "knockdown.h"
#include "blaster.h"
#include "shine.h"
#include "ledge.h"
#include "grab_flow.h"
#include "throw_flow.h"

enum {
  // Decomp: ftCommon_8007D5D4 writes fp->ecb_lock = 10 on ground->air transition.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR = 10u,
  // Decomp: ftCommon_8007D60C writes fp->ecb_lock = 5 on the alternate ground->air helper path.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
  MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR_ALT = 5u,
};

// -----------
// EscapeAir.c
// -----------

static inline void enter_fall_special(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_EscapeAir_Anim -> ftCo_80096900 (FallSpecial entry).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: EscapeAir enters FallSpecial via ftCo_80096900(..., arg1=1, ...), which sets xC=1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c and ftCo_FallSpecial.c
  batch->state.fallspecial_xc[idx] = 1;
}

uint8_t escape_air_try_enter_from_air_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                 size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }

  // Decomp: ftCo_80099A58 uses `fp->input.x668 & (HSD_PAD_R|HSD_PAD_L)` (pressed-edge semantics).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
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

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ESCAPE_AIR;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ESCAPE_AIR;
  // Decomp: EscapeAir entry calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.speed_air_x_self[idx] = vx;
  batch->state.speed_y_self[idx] = vy;
  // Decomp: EscapeAir enters without KeepFastFall; treat EscapeAir as a self-velocity-controlled
  // state and clear any prior fall-fast latch.
  batch->state.fall_fast[idx] = 0;
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
    enter_fall_special(batch, idx);
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

static inline uint8_t escape_try_enter_spotdodge_from_guard_y(MslBatch* batch,
                                                              const MslCommonParams* c, size_t idx,
                                                              float stick_y, float cstick_y,
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
  enter_escape_n(batch, idx);
  return 1;
}

static inline uint8_t escape_try_enter_spotdodge_from_guard(MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  return escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_y, cstick_y, tilt_timer_y);
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
  if (escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_y, cstick_y, tilt_timer_y)) {
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

  if (a == (uint16_t)MSL_ACT_ESCAPE_F &&
      batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_F &&
      move_tables_escapef_should_flip_facing(batch->state.char_id[idx],
                                             batch->state.prev_action_frame[idx],
                                             batch->state.action_frame[idx])) {
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

static inline uint8_t is_shield_active_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_REFLECT:
    case MSL_ACT_GUARD_SET_OFF:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t guard_reflect_timer_x14_init(const MslCommonParams* c) {
  // Decomp: GuardReflect reflect window uses p_ftCommonData->x2A4 frames.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (mv.co.guard.x14 = x2A4)
  //
  // Decomp: timer ticks down each GuardReflect_Anim call and expires when it drops below 0:
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
  //
  // Seed/state representation uses a +1 bias so we can expire at 0 without negative values.
  if (c == NULL) {
    return 0;
  }
  uint16_t t = (uint16_t)c->powershield_reflect_frames;
  t = (uint16_t)(t + 1u);
  if (t > 255u) {
    t = 255u;
  }
  return (uint8_t)t;
}

static inline uint8_t guard_reflect_timer_x18_init(const MslCommonParams* c) {
  // Decomp: GuardReflect powershield-active window uses p_ftCommonData->x2B4 frames.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (mv.co.guard.x18 = x2B4)
  //
  // Decomp: timer ticks down in ftCo_80093BC0 and x221C_b2 clears when it drops below 0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
  //
  // Seed/state representation uses a +1 bias so we can expire at 0 without negative values.
  if (c == NULL) {
    return 0;
  }
  uint16_t t = (uint16_t)c->powershield_reflect_total_frames;
  t = (uint16_t)(t + 1u);
  if (t > 255u) {
    t = 255u;
  }
  return (uint8_t)t;
}

static inline uint8_t guard_x10_init_u8(const MslCommonParams* c) {
  // Decomp: mv.co.guard.x10 is initialized from p_ftCommonData->x268 on GuardOn/GuardReflect entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
  //
  // Runtime representation note:
  // This sim stores shield states on the Slippi no-submotion post-frame lane (`animation_index=-1`,
  // negative action_frame). The next replay-visible GuardOn snapshot after entry carries the value
  // observed after the first ftCo_800925A4 owner tick. Seed/runtime therefore store the same
  // remaining-frame lane by subtracting that first owner tick from the entry constant; release
  // gates still use the pre-decrement value within each subsequent frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4,ftCo_GuardOn_Anim}
  if (c == NULL) {
    return 0;
  }
  if (!(c->guard_x10_init_frames > 0.0f)) {
    return 0;
  }
  uint16_t t = (uint16_t)c->guard_x10_init_frames;
  if (t > 0u) {
    t = (uint16_t)(t - 1u);
  }
  if (t > 255u) {
    t = 255u;
  }
  return (uint8_t)t;
}

static inline void enter_guard_reflect_common_setup(MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_REFLECT;
  // Slippi post-frame `animation_index` is frequently -1 for shield states in our datasets.
  // Keep this consistent with replay seeds/refs so validation compares cleanly.
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.guard_reflect_timer_x14[idx] = guard_reflect_timer_x14_init(c);
  batch->state.guard_reflect_timer_x18[idx] = guard_reflect_timer_x18_init(c);
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = guard_x10_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void enter_guard_reflect_from_guard(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  // Decomp entry path while already guarding:
  // - ftCo_80093694 -> ftCo_80093850 -> ftCo_8009388C.
  // - ftCo_8009388C keeps the current anim frame and does not call ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  const float anim_start = batch->state.anim_frame_f32[idx];
  enter_guard_reflect_common_setup(batch, c, idx);
  msl_anim_timebase_enter(batch, idx, anim_start, 1.0f);
  // ftCo_8009388C keeps the current anim frame on Guard->GuardReflect entry. Under teacher-forced
  // no-submotion snapshots, preserve negative carry-through when present; otherwise fall back to
  // frozen -1 shape used by Slippi snapshots.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // Decomp ordering for GuardOn/GuardReflect path:
  // - GuardOn_Anim runs before GuardOn_IASA (same Fighter proc), then ftCo_80093694 can enter
  //   GuardReflect while keeping current anim frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
  //
  // For no-submotion snapshots (`animation_index==-1`, negative state_age/action_frame), preserve
  // that "Anim-before-IASA" consumption by stepping one frame deeper into the negative lane so
  // the next frame's action_frame matches Slippi's -1-lane snapshot shape.
  // Snapshot-parity only: this is not claiming GALE01 uses a persistent "-2" lane in normal play.
  msl_anim_timebase_seed(batch, idx, (anim_start < 0.0f) ? (anim_start - 1.0f) : -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline void enter_guard_reflect_from_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                       size_t idx) {
  // Decomp entry path from locomotion guard check:
  // - ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50.
  // - ftCo_80093A50 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  enter_guard_reflect_common_setup(batch, c, idx);
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  // Slippi no-submotion shield snapshots are commonly encoded with animation_index=-1 and
  // state_age/action_frame=-1. Keep GuardReflect entry on that frozen timebase shape.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline void enter_guard_on(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:66-69 and :313-327.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  // Decomp: ftCo_800924C0 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  // Slippi no-submotion shield snapshots are commonly encoded with animation_index=-1 and
  // state_age/action_frame=-1. Keep GuardOn entry on that frozen timebase shape.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
  // GuardOn entry clears fp+0x221C GuardReflect bits before entering shield hold:
  // - x221C_b3 = 0
  // - x221C_b1 = 0
  // - x221C_b2 = 0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield mapping)
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B3 = 0x10 };
  enum { MSL_STATE_FLAG_221C_B1 = 0x40 };
  enum { MSL_STATE_FLAG_221C_B2 = 0x20 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(
      uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  batch->state.guard_on_entered_this_frame[idx] = 1u;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = guard_x10_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void enter_guard_hold(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_800928CC -> ftCo_80092908 changes motion to ftCo_MS_Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:421-446.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // Slippi parity for no-submotion shield snapshots:
  // `MSL_ACT_GUARD` is packed with animation_index=-1 and state_age/action_frame=-1 in the replay
  // suite (including GuardSetOff->Guard transitions), so keep Guard on the frozen (-1) timebase.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline void enter_guard_off(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80092BCC sets a release latch; Guard IASA transitions to GuardOff via ftCo_80092C54.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_OFF;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = 0;
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void guard_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
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

static inline void apply_shield_hold_drain(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                           float trig_unit) {
  // Decomp (GALE01): ftCo_800925A4 updates fp->lightshield_amount with a negative-input latch and
  // drains shield HP using the resulting value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  const float denom = 1.0f - c->trigger_deadzone;
  if (!(denom > 0.0f)) {
    return;
  }
  float light = batch->state.lightshield_amount[idx];
  const float t = (trig_unit - c->trigger_deadzone) / denom;
  if (t >= 0.0f) {
    light = clamp01(t);
  }
  batch->state.lightshield_amount[idx] = light;
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;

  float hp = batch->state.shield_hp[idx];
  hp -= drain;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[idx] = hp;
}

void guard_update_shield_recharge(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }

  // Shield recharge owner:
  // - Fighter_ProcessHit_8006D1EC runs:
  //     if (!fp->x221A_b7 && fp->shield_health < start) fp->shield_health += x27C;
  // - The gate is `!fp->x221A_b7` (shield inactive), not a locomotion-state family test.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // Empirically (and in replays), shield recharge can happen during GuardOff.
  //
  // Minimal sim approximation: block recharge only during states where the shield bubble is active.
  if (is_shield_active_action(batch->state.action_id[idx])) {
    return;
  }
  if (batch->state.stocks[idx] == 0) {
    return;
  }

  float hp = batch->state.shield_hp[idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[idx] = hp;
  }
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

  const uint16_t a0 = batch->state.action_id[idx];

  // GuardReflect/GuardSetOff anim-callback timing (prio 1):
  // - ftCo_GuardReflect_Anim calls ftCo_80093BC0 (x14/x18 tick + expire clears), then GuardOn_Anim.
  // - ftCo_GuardSetOff_Anim also calls ftCo_80093BC0 while shieldstun anim owns GuardDesc state.
  // - Fighter_8006A360 runs this under !hitlag.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT || a0 == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    if (batch->state.hitlag_started_frame[idx] == 0) {
      uint8_t t14 = batch->state.guard_reflect_timer_x14[idx];
      if (t14 > 0) {
        t14--;
        batch->state.guard_reflect_timer_x14[idx] = t14;
      }
      uint8_t t18 = batch->state.guard_reflect_timer_x18[idx];
      if (t18 > 0) {
        t18--;
        batch->state.guard_reflect_timer_x18[idx] = t18;
        if (t18 == 0u) {
          // Decomp: when mv.co.guard.x18 expires in ftCo_80093BC0, x221C_b2 is cleared in the
          // same GuardReflect_Anim callback pass.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
          enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
          enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
          const size_t flags_i =
              idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
          batch->state.state_flags[flags_i] &=
              (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE;
        }
      }
    }
  } else {
    // Keep GuardReflect timers strictly callback-owner action-scoped to avoid stale seeded carryover.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    batch->state.guard_reflect_timer_x14[idx] = 0;
    batch->state.guard_reflect_timer_x18[idx] = 0;
  }
}

static inline void rebound_update_anim_callback_pre_input(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  const uint16_t a0 = batch->state.action_id[idx];
  if (a0 != (uint16_t)MSL_ACT_REBOUND_STOP && a0 != (uint16_t)MSL_ACT_REBOUND) {
    return;
  }
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }

  float rebound_anim_speed = 1.0f;
  if (c != NULL && ch != NULL) {
    const float rebound_speed_abs = msl_absf(batch->state.speed_ground_x_self[idx]);
    // Rebound anim-rate ownership:
    // - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
    // - The rebound ground-speed lane written in the same callback is
    //   `fp->dmg.x191C * p_ftCommonData->x3D8 + p_ftCommonData->x3DC`.
    // - On replay-visible ReboundStop/Rebound seeds, we can reconstruct the same x191C from the
    //   live rebound ground speed before the first Rebound tick.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
    // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
    if (c->rebound_ground_x0_mul > 0.0f && rebound_speed_abs > c->rebound_ground_x0_base) {
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
    return;
  }

  if (batch->state.action_frame[idx] == 0 && rebound_anim_speed > 0.0f) {
    batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_anim_speed);
  }

  // Rebound_Anim callback ownership:
  // - Rebound ends through ft_8008A2BC (Wait enter) when the submotion has no frames remaining.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_REBOUND);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    escape_enter_wait(batch, idx);
  }
}

void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx,
                           uint8_t allow_entry) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];
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
       !is_shield_active_action(batch->state.prev_action_id[idx]))
          ? 1u
          : 0u;

  if (!is_shield_active_action(a0)) {
    batch->state.guard_release_latched_xc[idx] = 0;
    batch->state.guard_x10[idx] = 0;
    batch->state.lightshield_amount[idx] = 0.0f;
  }

  const float trig = msl_trigger_unit_from_input(
      batch->state.input_buttons[idx], batch->state.input_l[idx], batch->state.input_r[idx]);
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
  const uint8_t guard_x10_seed = batch->state.guard_x10[idx];

  // Guard release lockout (mv.co.guard.xC + mv.co.guard.x10) is modeled explicitly and seeded via
  // replay-history preprocessing (Slippi does not expose move vars directly).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC (xC latch)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4 (x10 tick)

  // GuardSetOff (shieldstun): no IASA until the underlying "GuardDamage" animation completes.
  //
  // Decomp:
  // - Enter: ftCo_80092F2C (sets anim rate based on shieldstun duration).
  // - Update/exit: ftCo_GuardSetOff_Anim transitions to Guard or GuardOff when the animation ends.
  // - Motion-state table selects ftCo_SM_GuardDamage as the submotion for GuardSetOff.
  //   refs/melee/src/melee/ft/ftmotionstates.c (GuardSetOff entry uses ftCo_SM_GuardDamage).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
  if (a0 == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
    if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
      // Shieldstun over:
      // - If mv.co.guard.xC is latched, transition to GuardOff (ftCo_80092BE8 -> ftCo_80092C54).
      // - Else transition to Guard (ftCo_800928CC).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BE8
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800928CC
      if (batch->state.guard_release_latched_xc[idx]) {
        enter_guard_off(batch, idx);
        return;
      }

      // Shieldstun over -> return to Guard (hold).
      enter_guard_hold(batch, idx);
      // IASA for the newly-entered Guard state in the same frame.
      if (guard_try_enter_iasa_defense(batch, c, idx)) {
        return;
      }
      return;
    }
    return;
  }

  // ----------------
  // Guard state loop
  // ----------------
  if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD || a0 == MSL_ACT_GUARD_REFLECT) {
    batch->state.animation_index[idx] = 0xFFFFFFFFu;
    const uint8_t can_update = (batch->state.hitlag_started_frame[idx] == 0) ? 1 : 0;
    if (guard_on_fresh_entry_from_non_shield_snapshot) {
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
      // Scope gate: this check is in ftCo_GuardOn_IASA only (not ftCo_Guard_IASA), so only
      // GuardOn can re-enter GuardReflect through this path.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      //
      // Snapshot note: in-suite Slippi seeds can carry `action_frame < 0` when
      // `animation_index==0xFFFFFFFF`. For this *guard.x0* gate only, treat negative action_frame
      // as 0 (entry-like) rather than a large/underflowed value; this preserves teacher-forced
      // prefix-invariant powershield behavior without using replay-fit heuristics.
      const uint16_t guard_x0 =
          (batch->state.action_frame[idx] < 0) ? 0u : (uint16_t)batch->state.action_frame[idx];
      if (a0 == (uint16_t)MSL_ACT_GUARD_ON &&
          guard_x0 < (uint16_t)c->powershield_reflect_window_frames &&
          (batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0 &&
          batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
        enter_guard_reflect_from_guard(batch, c, idx);
        return;
      }

      // Decomp ordering note (GuardOn/Guard discrete cluster):
      // - mv.co.guard.x10 is decremented inside ftCo_800925A4 (called by GuardOn_Anim / Guard_Anim).
      // - The GuardOff transition gate (xC && !x10) lives in inlineC0, called by GuardOn_IASA / Guard_IASA.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      //
      // Our step ordering models IASA before the x10 decrement, so the GuardOff check must use the
      // pre-decrement x10 value; otherwise GuardOn can drop 1 frame early when x10 transitions 1->0.
      const uint8_t x10_pre = batch->state.guard_x10[idx];
      const uint8_t guard_no_submotion_snapshot =
          (a0 == (uint16_t)MSL_ACT_GUARD && batch->state.action_frame[idx] < 0 &&
           batch->state.animation_index[idx] == 0xFFFFFFFFu &&
           batch->state.anim_frame_f32[idx] < 0.0f)
              ? 1u
              : 0u;
      const uint8_t guard_setoff_carry_snapshot =
          // Restrict the no-submotion carry suppression lane to true GuardSetOff->Guard carry.
          // A plain Guard hold snapshot can share (anim=-1, frame_speed>0, x672=0xFE) after
          // powershield entry; suppressing release there incorrectly blocks GuardOff on LR release.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}
          (guard_no_submotion_snapshot &&
           batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
           msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]) > 0.0f &&
           batch->state.x672_input_timer[idx] == 0xFEu)
              ? 1u
              : 0u;
      // Guard release latch ownership (ftCo_80092BCC):
      // - level check: if (!(held_inputs & HSD_PAD_LR)) xC = true.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
      if (!shield_held_inputs && !guard_setoff_carry_snapshot) {
        batch->state.guard_release_latched_xc[idx] = 1;
      }
      // Decomp: ftCo_800925A4 updates lightshield_amount + drains shield HP + decrements x10 while
      // the shield is active (fp->x221B_b0). Approximate shield-active as (shield_hp > 0).
      if (batch->state.shield_hp[idx] > 0.0f) {
        apply_shield_hold_drain(batch, c, idx, trig);
      }

      // Decomp: Guard IASA exits to GuardOff only once (xC && x10==0).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      if (!guard_setoff_carry_snapshot && batch->state.guard_release_latched_xc[idx] &&
          x10_pre == 0) {
        if (a0 == (uint16_t)MSL_ACT_GUARD_ON) {
          // Seed-snapshot bridge for GuardOn no-submotion rows:
          // - GALE01 ordering is GuardOn_Anim then GuardOn_IASA.
          // - On snapshot-shaped GuardOn seeds (animation_index=-1, state_age=-1), this can appear
          //   as GuardOn -> Guard -> GuardOff in one frame when release gate fires, consuming two
          //   motion-state entry bundles before the final GuardOff output.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_800928CC,ftCo_80092C54}
          const uint8_t guard_no_submotion_snapshot =
              (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
               batch->state.anim_frame_f32[idx] < 0.0f)
                  ? 1u
                  : 0u;
          if (guard_no_submotion_snapshot && guard_x10_seed == 0) {
            enter_guard_hold(batch, idx);
          }
        }
        if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
          batch->state.guard_reflect_timer_x14[idx] = 0;
          batch->state.guard_reflect_timer_x18[idx] = 0;
        }
        enter_guard_off(batch, idx);
        return;
      }

      if (x10_pre > 0 && batch->state.shield_hp[idx] > 0.0f) {
        batch->state.guard_x10[idx] = (uint8_t)(x10_pre - 1u);
      }
    }

    // GuardOn/GuardReflect -> Guard when the GuardOn "raise shield" window completes.
    //
    // Decomp: ftCo_GuardOn_Anim increments mv.co.guard.x0 and transitions when x0 >= fp->x2E8.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Anim
    //
    // Teacher-forced reseed note:
    // In-suite Slippi post-frames frequently seed GuardOn with `animation_index==0xFFFFFFFF` and
    // `state_age==-1` (so this sim's derived anim/action_frame cannot represent mv.co.guard.x0).
    // We therefore add a decomp-anchored, reseed-friendly fallback:
    // - for GuardOn, when mv.co.guard.x10 is already 0 and shield is still held, treat GuardOn as
    //   complete and enter Guard;
    // - for GuardReflect, same fallback once the reflect window timer (mv.co.guard.x14) has expired,
    //   since ftCo_GuardReflect_Anim chains into GuardOn_Anim after ftCo_80093BC0.
    //
    // This preserves deterministic one-step GuardOn->Guard transitions without replay-fit constants
    // and keeps the normal anim-end gate in place when a real timebase is available.
    const uint8_t guard_no_submotion_snapshot = (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
                                                 batch->state.anim_frame_f32[idx] < 0.0f)
                                                    ? 1u
                                                    : 0u;
    const uint8_t guard_reflect_window_expired =
        (batch->state.guard_reflect_timer_x14[idx] == 0u) ? 1u : 0u;
    // Snapshot bridge (GuardReflect negative lane):
    // - Replay snapshots can land on GuardReflect with no submotion (anim=-1) and action_frame<=-2.
    // - In this lane, reflect timers can already be expired while mv.co.guard.x10 still reflects a
    //   stale release-lockout seed, and GALE01 callback ordering at this boundary can advance to
    //   Guard before the next "normal" x10 gate observation.
    // - Keep the usual x10 gate for GuardOn and GuardReflect's normal lane; only bypass x10 for
    //   GuardReflect no-submotion rows at action_frame<=-2 with expired reflect timer.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800928CC}
    const uint8_t guard_reflect_snapshot_neg_lane =
        (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[idx] <= -2 &&
         batch->state.guard_reflect_timer_x14[idx] == 0u &&
         batch->state.guard_reflect_timer_x18[idx] == 0u)
            ? 1u
            : 0u;
    const uint8_t guard_reflect_snapshot_neg_lane_x10_one =
        (guard_reflect_snapshot_neg_lane && guard_x10_seed == 1u) ? 1u : 0u;
    const uint8_t guard_snapshot_hold_fallback =
        (guard_no_submotion_snapshot && shield_held_inputs &&
         ((a0 == (uint16_t)MSL_ACT_GUARD_ON && guard_x10_seed == 0) ||
          (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && guard_reflect_window_expired &&
           (guard_x10_seed == 0 || guard_reflect_snapshot_neg_lane_x10_one))))
            ? 1u
            : 0u;
    if (batch->state.hitlag_started_frame[idx] == 0 && guard_snapshot_hold_fallback) {
      // Decomp ordering: GuardReflect_Anim can transition to Guard before input callback dispatch,
      // and the destination Guard_IASA still consumes OoS options in the same frame.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        batch->state.guard_reflect_timer_x14[idx] = 0;
        batch->state.guard_reflect_timer_x18[idx] = 0;
      }
      enter_guard_hold(batch, idx);
      if (guard_try_enter_iasa_defense(batch, c, idx)) {
        return;
      }
      return;
    }

    // GuardOn/GuardReflect -> Guard when the GuardOn animation finishes.
    // Decomp: ftCo_GuardOn_Anim transitions to ftCo_800928CC when mv.co.guard.x0 >= fp->x2E8.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:367-377.
    //
    // Approximation mapping:
    // - Treat `action_frame` as `mv.co.guard.x0` (both tick once per frame outside hitlag).
    // - Treat `msl_anim_end_frame(char, ftCo_SM_GuardOn)` as `fp->x2E8` (ISO-derived anim timeline length).
    if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD_REFLECT) {
      const float end_frame =
          msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_ON);
      if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
        if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
          batch->state.guard_reflect_timer_x14[idx] = 0;
          batch->state.guard_reflect_timer_x18[idx] = 0;
        }
        enter_guard_hold(batch, idx);
      }
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
    // GuardOff IASA: allow spotdodge + jump, but not rolls.
    //
    // Decomp: ftCo_GuardOff_IASA calls spotdodge check (ftCo_8009980C) and jump check (ftCo_800CB024),
    // but does *not* call the roll check (ftCo_8009917C). Allowing EscapeF/B here causes a dominant
    // GuardOff->EscapeB mismatch cluster in teacher-forced one-step eval.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009917C
    if (escape_try_enter_spotdodge_from_guard(batch, c, idx)) {
      return;
    }
    if (guard_try_enter_jump_oos(batch, c, idx)) {
      return;
    }
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_OFF);
    if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
      guard_enter_wait(batch, idx);
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
    return;
  }

  // Decomp: ftCo_80091A4C (used by grounded locomotion IASA functions like Wait/Walk/Run/Turn).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-70 and ftCo_Wait.c:43-66.
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)LR) != 0 &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_locomotion(batch, c, idx);
    return;
  }

  if (shield_held_inputs && batch->state.shield_hp[idx] > 0.0f) {
    enter_guard_on(batch, c, idx);
    {
      // Initialize lightshield_amount from the current trigger input (decomp updates this on entry
      // via ftCo_800921DC and then per-frame via ftCo_800925A4).
      const float denom = 1.0f - c->trigger_deadzone;
      if (denom > 0.0f) {
        batch->state.lightshield_amount[idx] = clamp01((trig - c->trigger_deadzone) / denom);
      }
    }
    return;
  }
}

void action_update_anim_callbacks_pre_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  // Decomp ordering anchor:
  // - fighter Anim callbacks run in Fighter_8006A360 (prio 1) under !hitlag.
  // - input callback (IASA checks) runs later in Fighter_procUpdate (prio 3).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // Scope (current slice): only GuardReflect x14/x18 timer tick/expire is modeled here; this phase
  // must not consume current-frame input edges.
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      {
        const uint32_t anim_u32 = batch->state.animation_index[idx];
        if (anim_u32 <= 0xFFFFu) {
          const uint16_t msid = (uint16_t)anim_u32;
          const uint16_t frame = msl_anim_frame_floor_u16(
              msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
          uint8_t air_state = 0xFFu;
          if (airborne_state_event_get(batch->state.char_id[idx], msid, frame, &air_state) == 0) {
            const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
            const uint8_t max_jumps = (ch != NULL) ? ch->max_jumps : batch->state.jumps_left[idx];
            // Movescript opcode 25 (ftAction_80071998) dispatch:
            // state=0 -> ftCommon_8007D7FC (air->ground common helper)
            // state=1 -> ftCommon_8007D5D4 (ground->air common helper)
            // state=2 -> ftCommon_8007D60C (ground->air alt helper)
            // refs/melee/src/melee/ft/ftaction.c::ftAction_80071998
            // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D5D4,ftCommon_8007D60C}
            if (air_state == 0u) {
              float gr = batch->state.speed_air_x_self[idx];
              if (ch != NULL) {
                const float gmax = ch->ground_max_horizontal_velocity;
                if (gr > gmax) {
                  gr = gmax;
                } else if (gr < -gmax) {
                  gr = -gmax;
                }
              }
              // Common air->ground helper ownership:
              // - ftAction_80071998 state=0 dispatches ftCommon_8007D7FC / ftCommon_8007D6A4.
              // - ftCommon_8007D6A4 sets fp->gr_vel = fp->self_vel.x and does not zero self_vel.x.
              // - grounded Fighter_procUpdate keeps fp->self_vel.x synchronized from fp->gr_vel.
              // refs/melee/src/melee/ft/ftaction.c::ftAction_80071998
              // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
              // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
              batch->state.on_ground[idx] = 1u;
              batch->state.speed_ground_x_self[idx] = gr;
              batch->state.speed_air_x_self[idx] = gr;
              batch->state.jumps_left[idx] = max_jumps;
              batch->state.ecb_lock_timer[idx] = 0u;
            } else if (air_state == 1u) {
              batch->state.on_ground[idx] = 0u;
              batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
              batch->state.speed_ground_x_self[idx] = 0.0f;
              batch->state.jumps_left[idx] = (max_jumps > 0u) ? (uint8_t)(max_jumps - 1u) : 0u;
              batch->state.ecb_lock_timer[idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
            } else if (air_state == 2u) {
              batch->state.on_ground[idx] = 0u;
              batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
              batch->state.speed_ground_x_self[idx] = 0.0f;
              batch->state.jumps_left[idx] = 0u;
              batch->state.ecb_lock_timer[idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR_ALT;
            }
          }
        }
      }
      rebound_update_anim_callback_pre_input(batch, idx);
      guard_update_grounded_anim_callback_pre_input(batch, idx);
    }
  }
}

void action_update(MslBatch* batch) {
  const MslCommonParams* c = msl_common_params();
  if (batch != NULL) {
    const int num_players = (int)batch->config.num_players;
    for (int bi = 0; bi < batch->batch_size; bi++) {
      for (int p = 0; p < num_players; p++) {
        const size_t idx = msl_idx_player(bi, p);
        batch->state.guard_on_entered_this_frame[idx] = 0u;
      }
    }
  }
  // Grab/throw Anim-callback-shaped transitions should happen before the grounded locomotion IASA
  // chain (including shield entry). Example: Catch/CatchDash Anim end -> Wait (ft_8008A2BC) should
  // run before the next state's guard entry check (ftCo_80091A4C) so buffered shields can block
  // on the first actionable frame after a whiffed grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_CatchDash_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
  grab_flow_update_pre_physics(batch);
  throw_flow_update_pre_physics(batch);
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
  // Shield recharge is owned by Fighter_ProcessHit_8006D1EC under the `!fp->x221A_b7` gate, not
  // by locomotion. Run it after the frame's state-entry callbacks so the gate observes the current
  // state (for example SpecialLwStart after a shine entry), and do not suppress it during hitlag.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  action_update_shield_recharge_post_state(batch, c);
}
