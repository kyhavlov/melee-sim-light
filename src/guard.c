#include "guard.h"

#include "action_ids.h"
#include "anim_table.h"
#include "buttons.h"
#include "escape.h"

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
}

static inline void enter_guard_on(MslBatch* batch, size_t idx) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:66-69 and :313-327.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.action_frame[idx] = 0;
}

static inline void enter_guard_hold(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_800928CC -> ftCo_80092908 changes motion to ftCo_MS_Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:421-446.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.action_frame[idx] = 0;
}

static inline void enter_guard_off(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80092BCC sets a release latch; Guard IASA transitions to GuardOff via ftCo_80092C54.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_OFF;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
  batch->state.action_frame[idx] = 0;
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.action_frame[idx] = 0;
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

void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx, uint8_t allow_entry) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];
  const float trig = trigger_unit_from_input(batch->state.input_buttons[idx], batch->state.input_l[idx],
                                             batch->state.input_r[idx]);

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
      const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_ON);
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
    const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_OFF);
    if (end_frame > 0.0f && ((float)batch->state.action_frame[idx] >= end_frame)) {
      enter_wait(batch, idx);
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
