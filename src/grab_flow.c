#include "grab_flow.h"

#include <assert.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "common_params.h"
#include "grab_attachment.h"
#include "input_axis.h"
#include "move_tables.h"
#include "trigger_input.h"

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

static inline void enter_wait_from_catch_end(MslBatch* batch, size_t idx) {
  // Catch/CatchDash Anim end -> Wait.
  //
  // Decomp:
  // - ftCo_Catch_Anim: if !ftAnim_IsFramesRemaining, call ft_8008A2BC.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_Anim
  // - ftCo_CatchDash_Anim: similar end gate, then ft_8008A2BC.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Anim
  // - ft_8008A2BC usually routes to ft_8008A348 -> Fighter_ChangeMotionState(ftCo_MS_Wait).
  //   refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_catch_wait_from_pull(MslBatch* batch, size_t oidx) {
  // Decomp: CatchPull_Anim enters CatchWait via fn_800DA1D8 (Fighter_ChangeMotionState to 0xD8).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA1D8
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_WAIT;
  batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH_WAIT;
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
}

static inline void enter_catch_wait_from_attack(MslBatch* batch, size_t oidx) {
  // Decomp: CatchAttack anim end calls fn_800DA2B0, which enters ftCo_MS_CatchWait (0xD8).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchAttack_Anim
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA2B0
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_WAIT;
  batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH_WAIT;
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
}

static inline void enter_capture_wait_from_pulled(MslBatch* batch, size_t vidx) {
  // Decomp: CatchPull->CatchWait entry calls fn_800DB6C8 on the victim gobj, which enters
  // CaptureWait (hi/lw) based on the current capture variant.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA1D8
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DB6C8
  const uint16_t a = batch->state.action_id[vidx];
  if (a == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_HI;
  } else if (a == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_LW;
  } else {
    return;
  }
  // Decomp: CapturePulled* -> CaptureWait* transition uses Fighter_ChangeMotionState; this entry
  // path applies the immediate ftAnim tick (ftAnim_8006EBA4) on the new motion state.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DB6C8
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  msl_anim_timebase_enter_with_policy(batch, vidx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline uint8_t enter_capture_damage_from_wait(MslBatch* batch, size_t vidx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t va = batch->state.action_id[vidx];
  if (va == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI) {
    // Decomp: CaptureWaitHi victim enters CaptureDamageHi (0xE1) via ftCo_800DC284.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DC284
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_DAMAGE_HI;
  } else if (va == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
    // Decomp: CaptureWaitLw victim enters CaptureDamageLw (0xE4) via ftCo_800DC3A4.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DC3A4
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_DAMAGE_LW;
  } else {
    return 0u;
  }
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  return 1u;
}

static inline uint8_t enter_capture_wait_from_damage(MslBatch* batch, size_t vidx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t va = batch->state.action_id[vidx];
  if (va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI) {
    // Decomp: CaptureDamageHi anim end calls fn_800DB790 -> CaptureWaitHi (0xE0).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureDamageHi_Anim
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_HI;
  } else if (va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
    // Decomp: CaptureDamageLw anim end calls fn_800DBAE4 -> CaptureWaitLw (0xE3).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureDamageLw_Anim
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_LW;
  } else {
    return 0u;
  }
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  return 1u;
}

static inline void enter_catch_attack_from_wait(MslBatch* batch, size_t oidx) {
  // Decomp: CatchWait IASA checks pressed A in fn_800DA4C0 and enters CatchAttack via fn_800DA4FC.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DA4C0,fn_800DA4FC}
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_ATTACK;
  batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH_ATTACK;
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
}

static inline uint8_t catch_input_a_pressed_edge(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  // Decomp tie-down:
  // - Catch_CheckInput and CatchWait pummel check read `fp->input.x668 & HSD_PAD_A`.
  // Sim inference:
  // - We treat Z as satisfying the Catch-check predicate because Z is "grab" on controller and
  //   Slippi provides raw button bits, not the internal held_inputs/x668 representation.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_Catch_CheckInput,fn_800DA4C0}
  return ((pressed & (uint16_t)MSL_BUTTON_A) != 0 || (pressed & (uint16_t)MSL_BUTTON_Z) != 0) ? 1u
                                                                                              : 0u;
}

static inline uint8_t catch_input_lr_held(const MslBatch* batch, const MslCommonParams* c,
                                          size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint16_t buttons = batch->state.input_buttons[idx];
  // Sim inference: treat held Z as satisfying the LR-held half of Catch_CheckInput for the same
  // reason as catch_input_a_pressed_edge above (raw Slippi buttons vs internal held_inputs).
  if ((buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0) {
    return 1u;
  }

  const float trig =
      msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  return (trig >= c->trigger_deadzone) ? 1u : 0u;
}

static inline void enter_catch_motion_state(MslBatch* batch, size_t idx, uint16_t action_id,
                                            uint32_t submotion) {
  if (batch == NULL) {
    return;
  }

  // Decomp: ftCo_800D8C54 is the common Catch/CatchDash enter helper.
  // - Clears fp->x74_anim_vel.{x,y,z}
  // - Clears fp->mv.co.catch.x0
  // - Fighter_ChangeMotionState(..., msid, ..., anim_start=0.0f, anim_speed=1.0f)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
  // Sim mapping note:
  // - `speed_{x,y}_attack` are transient additive velocity lanes consumed by physics integration.
  //   Clear them on catch entry so stale knockback/attack carry does not leak into grab startup.
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = submotion;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

uint8_t grab_flow_try_enter_catch_from_iasa(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Decomp (Catch_CheckInput) for the catch-enter subset:
  // - Requires held_inputs & HSD_PAD_LR and pressed-edge A (input.x668 & HSD_PAD_A).
  // - On success calls ftCo_800D8C54(..., ftCo_MS_Catch=0xD4).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_Catch_CheckInput
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8C54
  if (!catch_input_a_pressed_edge(batch, idx)) {
    return 0u;
  }
  if (!catch_input_lr_held(batch, c, idx)) {
    return 0u;
  }

  enter_catch_motion_state(batch, idx, (uint16_t)MSL_ACT_CATCH, (uint32_t)MSL_SM_CATCH);
  return 1u;
}

uint8_t grab_flow_try_enter_catchdash_from_iasa(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Decomp (ftCo_800D8A38) for the CatchDash-enter subset:
  // - Requires held_inputs & HSD_PAD_LR and pressed-edge A (input.x668 & HSD_PAD_A).
  // - On success calls ftCo_800D8C54(..., ftCo_MS_CatchDash).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8A38
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
  //
  // NOTE(v1-domain):
  // - ftCo_800D8A38 also gates through fn_800D8E94/fn_800D952C before input checks.
  // - For Fox/Falco-only v1, those gates are effectively pass-through; keep the source pointers
  //   and model the input+enter shape directly.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800D8E94,fn_800D952C}
  if (!catch_input_a_pressed_edge(batch, idx)) {
    return 0u;
  }
  if (!catch_input_lr_held(batch, c, idx)) {
    return 0u;
  }

  enter_catch_motion_state(batch, idx, (uint16_t)MSL_ACT_CATCH_DASH, (uint32_t)MSL_SM_CATCH_DASH);
  return 1u;
}

static inline uint32_t throw_owner_submotion(uint16_t throw_action) {
  switch (throw_action) {
    case (uint16_t)MSL_ACT_THROW_F:
      return (uint32_t)MSL_SM_THROW_F;
    case (uint16_t)MSL_ACT_THROW_B:
      return (uint32_t)MSL_SM_THROW_B;
    case (uint16_t)MSL_ACT_THROW_HI:
      return (uint32_t)MSL_SM_THROW_HI;
    case (uint16_t)MSL_ACT_THROW_LW:
      return (uint32_t)MSL_SM_THROW_LW;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint16_t throw_victim_action(uint16_t throw_action) {
  switch (throw_action) {
    case (uint16_t)MSL_ACT_THROW_F:
      return (uint16_t)MSL_ACT_THROWN_F;
    case (uint16_t)MSL_ACT_THROW_B:
      return (uint16_t)MSL_ACT_THROWN_B;
    case (uint16_t)MSL_ACT_THROW_HI:
      return (uint16_t)MSL_ACT_THROWN_HI;
    case (uint16_t)MSL_ACT_THROW_LW:
      return (uint16_t)MSL_ACT_THROWN_LW;
    default:
      return 0xFFFFu;
  }
}

static inline uint32_t throw_victim_submotion(uint16_t thrown_action) {
  switch (thrown_action) {
    case (uint16_t)MSL_ACT_THROWN_F:
      return (uint32_t)MSL_SM_THROWN_F;
    case (uint16_t)MSL_ACT_THROWN_B:
      return (uint32_t)MSL_SM_THROWN_B;
    case (uint16_t)MSL_ACT_THROWN_HI:
      return (uint32_t)MSL_SM_THROWN_HI;
    case (uint16_t)MSL_ACT_THROWN_LW:
      return (uint32_t)MSL_SM_THROWN_LW;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint16_t catch_wait_throw_action_from_inputs(const MslBatch* batch,
                                                           const MslCommonParams* c, size_t oidx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  const float facing_dir = batch->state.facing[oidx] ? 1.0f : -1.0f;

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[oidx]), c->lstick_deadzone_x);
  const float stick_x_prev =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[oidx]), c->lstick_deadzone_x);
  const float cstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[oidx]), c->lstick_deadzone_x);
  const float cstick_x_prev =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_c_x[oidx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[oidx]), c->lstick_deadzone_y);
  const float stick_y_prev =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[oidx]), c->lstick_deadzone_y);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[oidx]), c->lstick_deadzone_y);
  const float cstick_y_prev =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_c_y[oidx]), c->lstick_deadzone_y);

  // CatchWait throw selection priority and edge checks are decomp-shaped from ftCo_800DD1E4:
  // 1) L-stick X edge across x98
  // 2) C-stick X edge across x98
  // 3) L-stick/C-stick up edge across attackhi3_stick_threshold_y
  // 4) L-stick down edge across xB0, or C-stick low hold (prev<=xB0 && cur<=xB0)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD1E4
  // refs/melee/src/melee/ft/ft_0DF1.c::{ftCo_800DF7F4,ftCo_800DF844,ftCo_800DF878}
  const float smash_thresh = c->smash_stick_threshold;
  const uint8_t lstick_x_edge = ((stick_x_prev < smash_thresh && stick_x >= smash_thresh) ||
                                 (stick_x_prev > -smash_thresh && stick_x <= -smash_thresh))
                                    ? 1u
                                    : 0u;
  if (lstick_x_edge) {
    return (stick_x * facing_dir > 0.0f) ? (uint16_t)MSL_ACT_THROW_F : (uint16_t)MSL_ACT_THROW_B;
  }

  const uint8_t cstick_x_edge = ((cstick_x_prev < smash_thresh && cstick_x >= smash_thresh) ||
                                 (cstick_x_prev > -smash_thresh && cstick_x <= -smash_thresh))
                                    ? 1u
                                    : 0u;
  if (cstick_x_edge) {
    return (cstick_x * facing_dir > 0.0f) ? (uint16_t)MSL_ACT_THROW_F : (uint16_t)MSL_ACT_THROW_B;
  }

  const uint8_t lstick_y_up_edge =
      (stick_y_prev < c->attack_hi3_stick_threshold_y && stick_y >= c->attack_hi3_stick_threshold_y)
          ? 1u
          : 0u;
  const uint8_t cstick_y_up_edge = (cstick_y_prev < c->attack_hi3_stick_threshold_y &&
                                    cstick_y >= c->attack_hi3_stick_threshold_y)
                                       ? 1u
                                       : 0u;
  if (lstick_y_up_edge || cstick_y_up_edge) {
    return (uint16_t)MSL_ACT_THROW_HI;
  }

  const uint8_t lstick_y_down_edge =
      (stick_y_prev > c->attack_lw3_stick_threshold_y && stick_y <= c->attack_lw3_stick_threshold_y)
          ? 1u
          : 0u;
  const uint8_t cstick_y_down_hold = (cstick_y_prev <= c->attack_lw3_stick_threshold_y &&
                                      cstick_y <= c->attack_lw3_stick_threshold_y)
                                         ? 1u
                                         : 0u;
  if (lstick_y_down_edge || cstick_y_down_hold) {
    return (uint16_t)MSL_ACT_THROW_LW;
  }

  return 0u;
}

static inline uint8_t enter_throw_from_wait(MslBatch* batch, int bi, int owner_p, size_t oidx,
                                            uint16_t throw_action) {
  if (batch == NULL) {
    return 0u;
  }

  const int num_players = (int)batch->config.num_players;
  int victim_p = -1;
  for (int p = 0; p < num_players; p++) {
    if (p == owner_p) {
      continue;
    }
    const size_t vidx = msl_idx_player(bi, p);
    if (batch->state.stocks[vidx] == 0) {
      continue;
    }
    if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
      continue;
    }
    if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
      continue;
    }
    victim_p = p;
    break;
  }
  if (victim_p < 0) {
    return 0u;
  }

  const uint16_t thrown_action = throw_victim_action(throw_action);
  const uint32_t owner_sm = throw_owner_submotion(throw_action);
  const uint32_t victim_sm = throw_victim_submotion(thrown_action);
  if (thrown_action == 0xFFFFu || owner_sm == 0xFFFFFFFFu || victim_sm == 0xFFFFFFFFu) {
    return 0u;
  }

  const size_t vidx = msl_idx_player(bi, victim_p);
  batch->state.action_id[oidx] = throw_action;
  batch->state.animation_index[oidx] = owner_sm;
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
  // Safety contract: this immediate tick is *only* valid for CatchWait throw-entry because
  // decomp's ftCo_800DD398 does Fighter_ChangeMotionState + immediate ftAnim_8006EBA4 in the same
  // callback. Generic motion-state entries must not do this extra tick because step.c already runs
  // the per-frame anim advance once via anim_timebase_update_pre_input().
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  assert(batch->state.action_id[oidx] >= (uint16_t)MSL_ACT_THROW_F &&
         batch->state.action_id[oidx] <= (uint16_t)MSL_ACT_THROW_LW);
  // Decomp: throw entry helper ftCo_800DD398 calls Fighter_ChangeMotionState, then immediately
  // ftAnim_8006EBA4 in the same frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  msl_anim_timebase_tick_once(batch, oidx);

  // TODO(slice2-throw-attachment): state-machine parity here is improved (CatchWait->Throw* and
  // victim CaptureWait->Thrown*), but positional attachment/throw offsets are still approximate.
  // Current suite max pos offenders on otherwise action-id-correct throw-entry chains include:
  // - GracefulAttachedTurtle.msl record 2507 (p0),
  // - QuerulousGrandDinosaur.msl record 9138 (p0).
  // Keep this transition logic and attachment kinematics work separated in future slices.
  batch->state.action_id[vidx] = thrown_action;
  batch->state.animation_index[vidx] = victim_sm;
  // Decomp: Thrown entry copies victim facing from thrower before installing/accessing the
  // per-frame thrown accessory callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  batch->state.facing[vidx] = batch->state.facing[oidx];
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  // Safety contract matches the thrower-side guard above: this extra tick mirrors
  // ftCo_800DE3FC's immediate ftAnim_8006EBA4 and must not be generalized to arbitrary entries.
  assert(batch->state.action_id[vidx] >= (uint16_t)MSL_ACT_THROWN_F &&
         batch->state.action_id[vidx] <= (uint16_t)MSL_ACT_THROWN_LW);
  // Decomp: thrown-victim entry helper also performs an immediate ftAnim_8006EBA4 after
  // Fighter_ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  msl_anim_timebase_tick_once(batch, vidx);
  grab_attachment_recompute_offsets_for_thrown_entry(batch, bi, victim_p, owner_p);
  return 1u;
}

void grab_flow_on_catch_connect(MslBatch* batch, int bi, int owner_p, int victim_p) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  if (owner_p < 0 || owner_p >= num_players || victim_p < 0 || victim_p >= num_players ||
      owner_p == victim_p) {
    return;
  }

  const size_t oidx = msl_idx_player(bi, owner_p);
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (batch->state.stocks[oidx] == 0 || batch->state.stocks[vidx] == 0) {
    return;
  }

  const uint16_t owner_act = batch->state.action_id[oidx];
  const float owner_anim_start = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[oidx]);
  // Catch/CatchDash connect -> CatchPull/CatchDashPull.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Coll,ftCo_CatchDash_Coll}
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  if (owner_act == (uint16_t)MSL_ACT_CATCH) {
    batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_PULL;
    batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH;
  } else if (owner_act == (uint16_t)MSL_ACT_CATCH_DASH) {
    batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_DASH_PULL;
    batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH_DASH;
  } else {
    return;
  }
  // Decomp: fn_800D9CE8 installs CatchPull/CatchDashPull with anim_start=fp->cur_anim_frame
  // (preserve current catch timeline instead of restarting from frame 0).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  msl_anim_timebase_enter(batch, oidx, owner_anim_start, 1.0f);

  // Catch connect victim entry: grounded owner uses CapturePulledLw, airborne owner uses
  // CapturePulledHi.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  if (batch->state.on_ground[oidx] != 0) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_LW;
  } else {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_HI;
  }
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  // Decomp: capture-pulled entry helper immediately ticks anim once via ftAnim_8006EBA4.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAA10
  assert(batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI ||
         batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW);
  msl_anim_timebase_tick_once(batch, vidx);

  // Decomp has a single victim_gobj pointer per owner; keep exactly one attached victim link.
  for (int p = 0; p < num_players; p++) {
    const size_t pidx = msl_idx_player(bi, p);
    if (p != victim_p && (int)batch->state.grab_owner_port[pidx] == owner_p) {
      batch->state.grab_owner_port[pidx] = 0xFFu;
    }
  }
  batch->state.grab_owner_port[vidx] = (uint8_t)owner_p;
}

void grab_flow_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int owner_p = 0; owner_p < num_players; owner_p++) {
      const size_t oidx = msl_idx_player(bi, owner_p);
      if (batch->state.hitlag_started_frame[oidx] != 0) {
        continue;
      }

      uint16_t oa = batch->state.action_id[oidx];
      uint8_t owner_catch_attack_ended = 0u;

      if (oa == (uint16_t)MSL_ACT_WAIT && c != NULL) {
        // Decomp: Wait IASA runs ftCo_Catch_CheckInput before the grounded locomotion checks.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_Catch_CheckInput
        if (grab_flow_try_enter_catch_from_iasa(batch, c, oidx)) {
          continue;
        }
      }

      // Catch/CatchDash Anim end -> Wait should happen before guard entry checks later in this
      // frame, so buffered shield inputs become active immediately on the first actionable frame
      // after a whiffed grab.
      //
      // Decomp: ftCo_Catch_IASA and ftCo_CatchDash_IASA are empty. On anim end, Catch/CatchDash
      // transition via ft_8008A2BC to a neutral state (usually ftCo_MS_Wait), and then the normal
      // grounded interrupt checks (including shield via ftCo_80091A4C) can run later.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_CatchDash_Anim}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_IASA,ftCo_CatchDash_IASA}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
      if (oa == (uint16_t)MSL_ACT_CATCH || oa == (uint16_t)MSL_ACT_CATCH_DASH) {
        const uint32_t msid_u32 = batch->state.animation_index[oidx];
        if (msid_u32 <= 0xFFFFu) {
          const uint16_t msid = (uint16_t)msid_u32;
          if (anim_finished(batch->state.char_id[oidx], msid, batch->state.anim_frame_f32[oidx])) {
            enter_wait_from_catch_end(batch, oidx);
            oa = batch->state.action_id[oidx];
          }
        }
      }

      if (oa == (uint16_t)MSL_ACT_CATCH_PULL || oa == (uint16_t)MSL_ACT_CATCH_DASH_PULL) {
        if (move_tables_catchpull_should_enter_wait(batch->state.char_id[oidx], oa,
                                                    batch->state.anim_frame_f32[oidx])) {
          enter_catch_wait_from_pull(batch, oidx);

          // Sync victim motion state (CapturePulled* -> CaptureWait*) for all victims owned by this
          // grabber.
          for (int victim_p = 0; victim_p < num_players; victim_p++) {
            const size_t vidx = msl_idx_player(bi, victim_p);
            if (batch->state.hitlag_started_frame[vidx] != 0) {
              continue;
            }
            if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
              continue;
            }
            enter_capture_wait_from_pulled(batch, vidx);
          }
          oa = batch->state.action_id[oidx];
        }
      }

      if (oa == (uint16_t)MSL_ACT_CATCH_ATTACK) {
        if (move_tables_catchattack_grabbed_hit_active(batch->state.char_id[oidx],
                                                       batch->state.anim_frame_f32[oidx])) {
          // Decomp: CatchAttack's grabbed-only hitbox drives CaptureWait* -> CaptureDamage*.
          // Keep victim selection deterministic by scanning ports in ascending order.
          for (int victim_p = 0; victim_p < num_players; victim_p++) {
            const size_t vidx = msl_idx_player(bi, victim_p);
            if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
              continue;
            }
            if (enter_capture_damage_from_wait(batch, vidx)) {
              break;
            }
          }
        }

        const uint32_t msid_u32 = batch->state.animation_index[oidx];
        const uint16_t msid =
            (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : (uint16_t)MSL_SM_CATCH_ATTACK;
        if (anim_finished(batch->state.char_id[oidx], msid, batch->state.anim_frame_f32[oidx])) {
          enter_catch_wait_from_attack(batch, oidx);
          oa = batch->state.action_id[oidx];
          owner_catch_attack_ended = 1u;
        }
      }

      // CaptureDamage* anim-end transitions are owned by the grabber/victim linkage.
      // Keep the iteration owner-deterministic: scan victim ports in ascending order.
      for (int victim_p = 0; victim_p < num_players; victim_p++) {
        const size_t vidx = msl_idx_player(bi, victim_p);
        if (batch->state.hitlag_started_frame[vidx] != 0) {
          continue;
        }
        if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
          continue;
        }
        const uint16_t va = batch->state.action_id[vidx];
        if (va != (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI &&
            va != (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
          continue;
        }
        if (owner_catch_attack_ended) {
          (void)enter_capture_wait_from_damage(batch, vidx);
          continue;
        }
        const uint32_t vmsid_u32 = batch->state.animation_index[vidx];
        uint16_t vmsid = 0u;
        if (vmsid_u32 <= 0xFFFFu) {
          vmsid = (uint16_t)vmsid_u32;
        } else if (va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI) {
          vmsid = (uint16_t)MSL_SM_CAPTURE_DAMAGE_HI;
        } else {
          vmsid = (uint16_t)MSL_SM_CAPTURE_DAMAGE_LW;
        }
        if (anim_finished(batch->state.char_id[vidx], vmsid, batch->state.anim_frame_f32[vidx])) {
          (void)enter_capture_wait_from_damage(batch, vidx);
        }
      }

      if (oa != (uint16_t)MSL_ACT_CATCH_WAIT || c == NULL) {
        continue;
      }

      // CatchWait IASA ordering: pummel check before throw check.
      // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchWait_IASA
      if (catch_input_a_pressed_edge(batch, oidx)) {
        enter_catch_attack_from_wait(batch, oidx);
        continue;
      }

      const uint16_t throw_action = catch_wait_throw_action_from_inputs(batch, c, oidx);
      if (throw_action == 0u) {
        continue;
      }
      (void)enter_throw_from_wait(batch, bi, owner_p, oidx, throw_action);
    }
  }
}
