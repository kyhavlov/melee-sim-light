#include "action.h"

#include <math.h>

#include "action_ids.h"
#include "anim_table.h"
#include "buttons.h"
#include "input_axis.h"
#include "locomotion.h"

// -----------
// EscapeAir.c
// -----------

static inline void enter_fall_special(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_EscapeAir_Anim -> ftCo_80096900 (FallSpecial entry).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  batch->state.action_frame[idx] = 0;
  // Keep anim_frame_f32 coherent with action_frame on action enters (approx policy; see SPEC.md).
  batch->state.anim_frame_f32[idx] = 0.0f;
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
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
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
  if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
    enter_fall_special(batch, idx);
  }
}

// ---------
// Escape.c
// ---------

static float escape_apply_friction_ground(float gr_vel, float friction) {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround
  float accel = friction;
  if (msl_absf(accel) > msl_absf(gr_vel)) {
    accel = -gr_vel;
  } else if (gr_vel > 0.0f) {
    accel = -accel;
  }
  return gr_vel + accel;
}

static inline void escape_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void enter_escape_n(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80099894 -> ftCo_800998EC (non-Yoshi path).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:248-269.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ESCAPE_N;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ESCAPE_N;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void enter_escape_roll(MslBatch* batch, size_t idx, uint16_t action_id) {
  // Decomp: ftCo_8009917C -> ftCo_800992A8 -> ftCo_80099314 (default fighters).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:58-88 and :104-120.
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (action_id == (uint16_t)MSL_ACT_ESCAPE_F)
                                          ? (uint32_t)MSL_SM_ESCAPE_F
                                          : (uint32_t)MSL_SM_ESCAPE_B;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
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

  // Spotdodge (EscapeN) has priority over roll checks in Guard IASA.
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA
  const uint8_t want_spotdodge = ((stick_y <= c->spotdodge_stick_y_threshold &&
                                   tilt_timer_y < c->spotdodge_flick_tilt_max_frames) ||
                                  (cstick_y <= c->spotdodge_stick_y_threshold))
                                     ? 1
                                     : 0;
  if (want_spotdodge) {
    enter_escape_n(batch, idx);
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

  // Physics (approx): apply ground friction each frame.
  //
  // Decomp:
  // - EscapeF/B phys: ftCo_Escape_Phys -> ft_80085004 -> ft_80085030 (friction unless root-motion).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:188-198 and refs/melee/src/melee/ft/ft_081B.c:1424-1449
  // - EscapeN phys: ftCo_EscapeN_Phys -> ft_80084F3C (uses high-speed friction mul).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:288-297 and refs/melee/src/melee/ft/ft_081B.c:1398-1423
  float friction = ch->gr_friction;
  if (a == (uint16_t)MSL_ACT_ESCAPE_N) {
    if (msl_absf(batch->state.speed_ground_x_self[idx]) > ch->walk_max_vel) {
      friction *= c->high_speed_friction_mul;
    }
  }
  batch->state.speed_ground_x_self[idx] =
      escape_apply_friction_ground(batch->state.speed_ground_x_self[idx], friction);

  // Anim end -> Wait.
  // Decomp: ftCo_Escape_Anim / ftCo_EscapeN_Anim.
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
  if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
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

static inline float trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return trigger_u8_to_unit(m);
}

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

static inline void enter_guard_reflect(MslBatch* batch, size_t idx) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-65 and :763-798.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_REFLECT;
  // Slippi post-frame `animation_index` is frequently -1 for shield states in our datasets.
  // Keep this consistent with replay seeds/refs so validation compares cleanly.
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void enter_guard_on(MslBatch* batch, size_t idx) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:66-69 and :313-327.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void enter_guard_hold(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_800928CC -> ftCo_80092908 changes motion to ftCo_MS_Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:421-446.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void enter_guard_off(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80092BCC sets a release latch; Guard IASA transitions to GuardOff via ftCo_80092C54.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_OFF;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
}

static inline void guard_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.action_frame[idx] = 0;
  batch->state.anim_frame_f32[idx] = 0.0f;
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

static inline void apply_shield_hold_drain(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                           float trig_unit) {
  // Decomp:
  // - fp->lightshield_amount = (x650 - x10)/(1-x10) with a negative check.
  // - fp->shield_health -= x278 * (light*(x2F0-x2EC) + x2EC); clamp at 0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:333-350.
  //
  // We do not model the "reuse previous lightshield amount when negative" latch yet; under the
  // trigger_deadzone (x10) gate, trig_unit should be >= x10 while shielding.
  const float denom = 1.0f - c->trigger_deadzone;
  if (denom <= 0.0f) {
    return;
  }
  const float light = clamp01((trig_unit - c->trigger_deadzone) / denom);
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

  // Decomp:
  // if (!fp->x221A_b7) {
  //   if (fp->shield_health < p_ftCommonData->x260_startShieldHealth) {
  //     fp->shield_health += p_ftCommonData->x27C;
  //     fp->shield_health = min(fp->shield_health, p_ftCommonData->x260_startShieldHealth);
  //   }
  // }
  // refs/melee/src/melee/ft/fighter.c:2804-2811.
  //
  // Decomp gating is on `!fp->x221A_b7` (shield active), *not* on motion state alone.
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

void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx,
                           uint8_t allow_entry) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];
  const float trig = trigger_unit_from_input(batch->state.input_buttons[idx],
                                             batch->state.input_l[idx], batch->state.input_r[idx]);

  // `held_inputs & HSD_PAD_LR` behavior for shielding uses the trigger deadzone (x10).
  // Decomp usage: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:46-55.
  const uint8_t shield_held = (trig >= c->trigger_deadzone) ? 1 : 0;

  // ----------------
  // Guard state loop
  // ----------------
  if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD || a0 == MSL_ACT_GUARD_REFLECT) {
    batch->state.animation_index[idx] = 0xFFFFFFFFu;
    if (!shield_held) {
      enter_guard_off(batch, idx);
      return;
    }

    apply_shield_hold_drain(batch, c, idx, trig);

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
      if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
        enter_guard_hold(batch, idx);
      }
    }

    // Shield defensive options (grounded): spotdodge / rolls.
    // Decomp call site: ftCo_GuardOn_IASA / ftCo_Guard_IASA.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:393-409 and :454-469.
    if (escape_try_enter_from_guard(batch, c, idx)) {
      return;
    }
    return;
  }

  // GuardOff: wait for animation end then go back to Wait.
  if (a0 == MSL_ACT_GUARD_OFF) {
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_OFF);
    if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
      guard_enter_wait(batch, idx);
      return;
    }
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
    return;
  }

  // ----------------
  // Guard entry gate
  // ----------------
  if (!allow_entry) {
    return;
  }

  // Decomp: ftCo_80091A4C (used by grounded locomotion IASA functions like Wait/Walk/Run/Turn).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-70 and ftCo_Wait.c:43-66.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)LR) != 0 &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect(batch, idx);
    return;
  }

  if (shield_held && batch->state.shield_hp[idx] > 0.0f) {
    enter_guard_on(batch, idx);
    return;
  }
}

void action_update(MslBatch* batch) { locomotion_update_pre(batch); }
