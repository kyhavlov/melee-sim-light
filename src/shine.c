#include "shine.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "jump_input.h"
#include "hit_status_tables.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "special_msids.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

uint8_t shine_char_supports_reflector(uint8_t char_id) {
  // Decomp authority for the current v1 reflector lane:
  // - Fox and Falco own the ftFx_/ftFc_ SpecialLw motion-state families.
  // - The grounded AttackDash -> Wait_IASA -> ftCo_800D68C0 subset below only claims those
  //   spacie reflector enters, so gate the caller explicitly before any control-flow consume.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c
  // refs/melee/src/melee/ft/chara/ftFalco/ftFc_SpecialLw.c
  return is_fox_falco(char_id);
}

// Decomp: in ftFx_Init.c, the aerial SpecialAirLw* motion states are a contiguous block following
// the grounded SpecialLw* block (Start..Turn).
enum { MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA = 5 };

static inline uint8_t action_is_shine(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_LW_START:
    case MSL_ACT_FX_SPECIAL_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_LW_HIT:
    case MSL_ACT_FX_SPECIAL_LW_END:
    case MSL_ACT_FX_SPECIAL_LW_TURN:
    case MSL_ACT_FX_SPECIAL_AIR_LW_START:
    case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
    case MSL_ACT_FX_SPECIAL_AIR_LW_END:
    case MSL_ACT_FX_SPECIAL_AIR_LW_TURN:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_shine_ground(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t action_is_shine_air(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t shine_is_dash_flick(const MslCommonParams* c, float stick_x,
                                          uint8_t tilt_timer_x) {
  if (c == NULL) {
    return 0u;
  }
  const float ax = msl_absf(stick_x);
  // Decomp: Dash admission uses `ABS(lstick.x) >= p_ftCommonData->x3C` and
  // `x670_timer_lstick_tilt_x < p_ftCommonData->x40`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  return (ax >= c->dash_flick_abs && tilt_timer_x < c->dash_flick_tilt_max_frames) ? 1u : 0u;
}

static inline uint8_t action_allows_shine_entry_ground(uint16_t action_id) {
  // Decomp: specials (including SpecialLw / shine) are dispatched from per-state IASA callbacks via
  // `ftCo_800D68C0` ("special move input" dispatcher). If a state does not call into that chain,
  // SpecialLw cannot be entered regardless of input.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
  //
  // This simulator does not yet extract per-action IASA graphs / "can this state call ftCo_800D68C0"
  // from motion-state tables, so we keep SpecialLw entry narrowly scoped to a suite-covered subset
  // of grounded states whose IASA is known to route through ftCo_800D68C0 in decomp.
  //
  // Included examples (non-exhaustive):
  // - Wait/Walk/Turn: RETURN_IF(ftCo_800D68C0(gobj))
  //   refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Walk.c,ftCo_Turn.c}
  // - SquatWait: RETURN_IF(ftCo_800D68C0(gobj))
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c
  //
  // Explicit exclusions (decomp-anchored):
  // - EscapeN/F/B: no ftCo_800D68C0 in IASA (Escape IASA only checks ftCo_8009563C; EscapeN IASA is empty).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_Escape_IASA,ftCo_EscapeN_IASA}
  // - KneeBend: IASA checks Attack100, Catch, AttackHi4, then short-hop bookkeeping only.
  //   It does not route through grounded special dispatch.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
  // - LandingAir*: IASA is empty.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_IASA
  // - Landing / LandingFallSpecial: decomp does allow specials after landing-lag and allow_interrupt
  //   gates via ftCo_Landing_IASA -> ftCo_800D68C0. The runtime gate below handles normal Landing
  //   lag and the hidden LandingFallSpecial allow_interrupt lane explicitly.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  // - RunBrake: IASA checks fn_800CAF78, cmd_vars[0] turn-run, and ftCo_800D5FB0 only; it does
  //   not call ftCo_SpecialS_CheckInput or ftCo_800D68C0, so grounded SpecialLw cannot enter from
  //   RunBrake on the same frame.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
  // - GuardOn / Guard / GuardReflect do not route through ftCo_800D68C0, and GuardSetOff_IASA is
  //   empty. GuardOff is the shield-family state whose IASA can call ftCo_800D68C0.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //     ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardSetOff_IASA,ftCo_GuardReflect_IASA,
  //     ftCo_GuardOff_IASA}
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_TURN:
    case MSL_ACT_TURN_RUN:
    // Decomp: Dash IASA does not call ftCo_800D68C0 directly.
    // Dash special entry paths route through ftCo_SpecialS_CheckInput / ftCo_800D8A38 /
    // ftCo_80091A4C branches in ftCo_Dash_IASA, while grounded SpecialLw dispatch (ftCo_800D68C0)
    // is owned by states like Wait/Walk/Turn/Run/Squat family.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    case MSL_ACT_RUN:
    case MSL_ACT_RUN_DIRECT:
    case MSL_ACT_SQUAT:
    case MSL_ACT_SQUAT_WAIT:
    case MSL_ACT_SQUAT_RV:
    case MSL_ACT_GUARD_OFF:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t shine_entry_blocked_by_runbrake_squat_transition(const MslBatch* batch,
                                                                       size_t idx,
                                                                       uint16_t action_id) {
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_SQUAT) {
    return 0u;
  }
  // Decomp ordering: ftCo_RunBrake_IASA returns immediately after ftCo_800D5FB0 enters Squat.
  // The destination Squat IASA (`ftCo_800D68C0` special dispatch) does not run again in the same
  // input callback. This simulator runs shine_update_pre_physics after locomotion_update_pre, so
  // explicitly prevent the just-entered RunBrake->Squat frame from consuming the same B+down input.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
  return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_BRAKE) ? 1u : 0u;
}

static inline uint8_t shine_entry_allowed_by_run_to_runbrake_transition(const MslBatch* batch,
                                                                        size_t idx,
                                                                        uint16_t action_id) {
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_RUN_BRAKE) {
    return 0u;
  }
  // Run IASA checks SpecialS, attacks, ftCo_800D68C0, and other special/catch/guard branches
  // before the final RunBrake check. Because this sim's locomotion pass can enter RunBrake before
  // shine_update_pre_physics(), preserve the frame-start Run special-dispatch opportunity here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_CheckInput
  const uint16_t start = batch->state.prev_action_id[idx];
  return (start == (uint16_t)MSL_ACT_RUN || start == (uint16_t)MSL_ACT_RUN_DIRECT) ? 1u : 0u;
}

static inline uint8_t action_is_damage_air_or_fly_special_iasa(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t action_is_attackair_special_iasa(uint16_t action_id) {
  // Generated from MotionState callback symbols ftCo_AttackAir_* for the five common aerial
  // attacks. Exhaustive Fox/Falco equivalence is covered by tests/test_motion_state_owners_table.py.
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint8_t damage_air_or_fly_allows_special_air_iasa(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B6_HITSTUN = 0x02 };
  // Damage/DamageFly IASA split:
  // - Damage_IASA calls Fall_IASA_Inner only when !fp->x221C_b6.
  // - DamageFly_IASA calls DamageFall_IASA only when !fp->x221C_b6.
  // - Fall_IASA_Inner and DamageFall_IASA both route through ftCo_SpecialAir_CheckInput.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_IASA,ftCo_DamageFly_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  return ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B6_HITSTUN) == 0u) ? 1u : 0u;
}

static inline uint8_t action_allows_shine_entry_air(const MslBatch* batch, size_t idx,
                                                    uint16_t action_id) {
  // Decomp-special input ownership:
  // - Jump/Fall-family IASA owners route through ftCo_SpecialAir_CheckInput.
  // - AttackAir DO_IASA runs EscapeAir, then ftCo_800D7100 / ftCo_SpecialAir_CheckInput, then
  //   item / attack / jump branches, gated by script-owned allow_interrupt.
  // - DamageFall_IASA also routes through ftCo_SpecialAir_CheckInput.
  // - FallSpecial_IASA does not; it only checks attack/item/jump-owned branches.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::DO_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_IASA
  switch (action_id) {
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_DAMAGE_FALL:
      return 1u;
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
      // PassiveWall IASA checks ftCo_SpecialAir_CheckInput before AttackAir/item branches once
      // mv.co.passivewall.timer has expired.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
      //   ftCo_PassiveWall_IASA,inlineA0}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
      return (batch->state.passivewall_timer[idx] == 0u) ? 1u : 0u;
    default:
      break;
  }
  if (action_is_damage_air_or_fly_special_iasa(action_id)) {
    return damage_air_or_fly_allows_special_air_iasa(batch, idx);
  }
  if (action_is_attackair_special_iasa(action_id)) {
    return move_tables_attackair_allow_interrupt(batch->state.char_id[idx], action_id,
                                                 batch->state.anim_frame_f32[idx]);
  }
  return 0u;
}

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_turn_smash_from_wait_iasa(MslBatch* batch, size_t idx, float facing_dir) {
  // Decomp: ftCo_Wait_IASA reaches ftCo_Dash_CheckInput before ftCo_Turn_CheckInput; a dash-flick
  // opposite facing calls ftCo_Turn_Enter_Smash, which initializes Turn with frames_to_turn=0 and
  // ticks animation immediately.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
  batch->state.turn_has_turned[idx] = 0u;
  batch->state.turn_frames_to_turn[idx] = 0u;
  batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static inline void enter_turn_basic_from_wait_iasa(MslBatch* batch, size_t idx,
                                                   const MslCharParams* ch) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Decomp: if Wait_IASA reaches ftCo_Turn_CheckInput, ftCo_Turn_Enter_Basic keeps
  // frames_to_turn from character data and ticks animation immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
  //   ftCo_Turn_CheckInput,ftCo_Turn_Enter_Basic}
  // data/characters/{fox,falco}.json::turn_frames
  batch->state.turn_has_turned[idx] = 0u;
  batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
  batch->state.turn_x8[idx] = 0;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static inline uint16_t jump_aerial_action_from_stick(const MslCommonParams* c, float stick_x,
                                                     float facing_dir);

static inline void enter_jump_aerial_basic(MslBatch* batch, size_t idx, const MslCharParams* ch,
                                           const MslCommonParams* c, float stick_x,
                                           float facing_dir) {
  if (batch == NULL || ch == NULL || c == NULL) {
    return;
  }
  const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (act == (uint16_t)MSL_ACT_JUMP_AERIAL_F)
                                          ? (uint32_t)MSL_SM_JUMP_AERIAL_F
                                          : (uint32_t)MSL_SM_JUMP_AERIAL_B;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
  batch->state.speed_y_self[idx] = ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
  // Decomp: fp->x671_timer_lstick_tilt_y = 0xFE.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.fall_fast[idx] = 0;
  batch->state.jumps_left[idx]--;
  // Decomp: ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4 before ChangeMotionState.
  // When this entry is reached from SpecialAirLw Loop/Turn/End IASA, that same callback owns the
  // frame's input consumption; destination JumpAerial special dispatch must wait for the next frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwTurn_IASA,ftFx_SpecialAirLwEnd_Anim}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.ecb_lock_timer[idx] = 10u;
  batch->state.shine_jump_iasa_entered_this_frame[idx] = 1u;
}

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1 : 0;
}

static inline MslJumpInput jump_input_from_edges(const MslCommonParams* c, uint16_t buttons_pressed,
                                                 float stick_y, uint8_t tilt_timer_y) {
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) {
    return MSL_JUMP_INPUT_XY;
  }
  if (did_tap_jump(c, stick_y, tilt_timer_y)) {
    return MSL_JUMP_INPUT_LSTICK;
  }
  return MSL_JUMP_INPUT_NONE;
}

static inline uint16_t jump_aerial_action_from_stick(const MslCommonParams* c, float stick_x,
                                                     float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? (uint16_t)MSL_ACT_JUMP_AERIAL_F
                                                            : (uint16_t)MSL_ACT_JUMP_AERIAL_B;
}

static inline uint8_t stick_wants_speciallw(const MslCommonParams* c, int8_t main_y) {
  if (c == NULL) {
    return 0;
  }
  const float y = apply_deadzone(stick_i8_to_unit(main_y), c->lstick_deadzone_y);
  return (y <= -c->special_stick_y_threshold) ? 1u : 0u;
}

static inline uint8_t stick_wants_turn(const MslCommonParams* c, int8_t main_x, uint8_t facing) {
  if (c == NULL) {
    return 0;
  }
  const float x = apply_deadzone(stick_i8_to_unit(main_x), c->lstick_deadzone_x);
  const float facing_dir = facing ? 1.0f : -1.0f;
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_800C97A8
  return (x * facing_dir <= c->turn_stick_x_threshold) ? 1u : 0u;
}

static inline void enter_shine_ground_start(MslBatch* batch, size_t idx,
                                            const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_START;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftFx_SpecialLw_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState, before
  // later Phys/Coll/contact passes observe the new motion state's pose.
  //
  // This is combat-visible: vanilla TBK rec=1575's no-hit Shine Start uses the post-entry frame-1
  // hitbox pose, while still running frame-0 cmd-script hit-status effects on entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  msl_anim_timebase_tick_once(batch, idx);

  // Decomp: ftFx_SpecialLw_Enter / ftFx_SpecialAirLw_Enter call Fighter_ChangeMotionState(...)
  // and then immediately call ftAnim_8006EBA4(gobj), which runs the fighter cmd script for the
  // new motion state (ftAction_80073240) and can set move-induced hit status (fp->x1988) via
  // opcode 26 (ftColl_8007B62C) on the entry frame.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
  //
  // The general hurtbox_state overwrite rule in hurtboxes_refresh() defers x1988 on entry frames
  // for post-Anim transitions; Shine Start is a known exception with a decomp anchor, so apply
  // the table-derived hit status immediately here.
  uint8_t hit_status = 0;
  if (batch->debug_hit_status_override != NULL) {
    const uint8_t ov = batch->debug_hit_status_override[idx];
    if (ov != 0xFFu) {
      hit_status = ov;
    } else {
      (void)hit_status_get(batch->state.char_id[idx], (uint16_t)ms->speciallw_ground_start, 0u,
                           &hit_status);
    }
  } else {
    (void)hit_status_get(batch->state.char_id[idx], (uint16_t)ms->speciallw_ground_start, 0u,
                         &hit_status);
  }
  if (hit_status != 0u) {
    batch->state.hurtbox_state[idx] = hit_status;
  }
}

static inline void enter_shine_air_start(MslBatch* batch, size_t idx, const MslCharParams* ch,
                                         const MslSpecialMsids* ms) {
  if (ch != NULL) {
    // Decomp: ftFx_SpecialAirLw_Enter sets self_vel.y=0 and divides self_vel.x by momentum preserve.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    batch->state.speed_y_self[idx] = 0.0f;
    if (ch->reflector_momentum_preserve_x != 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->reflector_momentum_preserve_x;
    }
  }
  // Decomp: ftFx_SpecialAirLw_Enter uses Fighter_ChangeMotionState without Ft_MF_KeepFastFall, so
  // fp->fall_fast is cleared on aerial shine entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  batch->state.fall_fast[idx] = 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftFx_SpecialAirLw_Enter has the same immediate post-ChangeMotionState animation tick
  // as grounded Shine Start.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  msl_anim_timebase_tick_once(batch, idx);

  // See enter_shine_ground_start() for the decomp-backed "run cmd script on entry" exception.
  uint8_t hit_status = 0;
  if (batch->debug_hit_status_override != NULL) {
    const uint8_t ov = batch->debug_hit_status_override[idx];
    if (ov != 0xFFu) {
      hit_status = ov;
    } else {
      (void)hit_status_get(batch->state.char_id[idx], (uint16_t)ms->speciallw_air_start, 0u,
                           &hit_status);
    }
  } else {
    (void)hit_status_get(batch->state.char_id[idx], (uint16_t)ms->speciallw_air_start, 0u,
                         &hit_status);
  }
  if (hit_status != 0u) {
    batch->state.hurtbox_state[idx] = hit_status;
  }
}

static inline void shine_release_setvars(MslBatch* batch, size_t idx, const MslCharParams* ch) {
  // Decomp: ftFox_SpecialLw_SetVars initializes releaseLag/isRelease on shine enter.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFox_SpecialLw_SetVars
  batch->state.shine_release_lag[idx] = ch->reflector_release_lag_frames;
  batch->state.shine_is_release[idx] = 0u;
}

void shine_enter_ground_start_from_iasa(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t cid = batch->state.char_id[idx];
  if (!is_fox_falco(cid)) {
    return;
  }
  const MslCharParams* ch = msl_char_params(cid);
  const MslSpecialMsids* ms = msl_special_msids(cid);
  if (ch == NULL || ms == NULL) {
    return;
  }
  // Grounded IASA delegate helper:
  // - states that route through ftCo_800D68C0 can consume directly into Reflector start.
  // - use the same decomp-backed entry path as shine_update_pre_physics().
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  shine_release_setvars(batch, idx, ch);
  enter_shine_ground_start(batch, idx, ms);
}

static inline void shine_release_latch_anim(MslBatch* batch, size_t idx, uint16_t held) {
  // Decomp: Start/Loop/Turn/Hit anim callbacks latch isRelease when B is not held.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLwStart_Anim,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Anim}
  if ((held & (uint16_t)MSL_BUTTON_B) == 0) {
    batch->state.shine_is_release[idx] = 1u;
  }
}

static inline void shine_release_tick_anim(MslBatch* batch, size_t idx, uint16_t held) {
  // Decomp: Loop/Turn/Hit anim callbacks:
  // - latch isRelease when B is not held
  // - decrement releaseLag while > 0
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Anim}
  shine_release_latch_anim(batch, idx, held);
  if (batch->state.shine_release_lag[idx] > 0u) {
    batch->state.shine_release_lag[idx] = (uint8_t)(batch->state.shine_release_lag[idx] - 1u);
  }
}

static inline uint8_t shine_release_should_end(const MslBatch* batch, size_t idx) {
  // Decomp: ftFx_SpecialLwHit_Check gates End when (releaseLag <= 0 && isRelease).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Check
  return (batch->state.shine_release_lag[idx] == 0u && batch->state.shine_is_release[idx] != 0u)
             ? 1u
             : 0u;
}

static inline void enter_shine_ground_loop(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_LOOP;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_air_loop(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_ground_end(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_END;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_air_end(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_ground_turn(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  // Decomp: ftFx_SpecialLwTurn_Check enters the turn motion state and flips facing on entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwTurn_Check
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;  // Turn uses Loop msid.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
}

static inline void enter_shine_air_turn(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;  // Turn uses Loop msid.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
}

void shine_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(cid);
      const MslSpecialMsids* ms = msl_special_msids(cid);
      if (c == NULL || ch == NULL || ms == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      uint8_t shine_entered_this_frame = 0u;
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
      const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
      const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
      const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
      const float stick_x =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      const uint8_t landing_specials_open =
          (a == (uint16_t)MSL_ACT_LANDING &&
           batch->state.anim_frame_f32[idx] >= (float)ch->landing_lag_frames)
              ? 1u
              : ((a == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
                  batch->state.landing_fallspecial_allow_interrupt[idx] != 0u)
                     ? 1u
                     : 0u);

      // Entry (minimal): B press + down stick from basic locomotion.
      if (!action_is_shine(a) && (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0 &&
          stick_wants_speciallw(c, batch->state.input_main_y[idx])) {
        if (on_ground) {
          const uint8_t grounded_side_special_preempts_down_special =
              // Grounded special dispatch order checks SpecialS before SpecialLw:
              // - ftCo_Landing_IASA: SpecialS, Attack100, SpecialHi, SpecialN, then SpecialLw.
              // - Wait/Turn/Walk/Squat families route through the same Attack100 special helpers.
              // Therefore a grounded B+side+down diagonal consumes Side-B before Shine; aerial
              // dispatch keeps its separate Up/Down/Side/Neutral order in ftCo_SpecialAir_CheckInput.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
              //   ftCo_SpecialS_CheckInput,ftCo_800D68C0}
              (fabsf(stick_x) >= c->special_stick_x_threshold_side) ? 1u : 0u;
          if (!grounded_side_special_preempts_down_special &&
              (action_allows_shine_entry_ground(a) || landing_specials_open ||
               shine_entry_allowed_by_run_to_runbrake_transition(batch, idx, a)) &&
              !shine_entry_blocked_by_runbrake_squat_transition(batch, idx, a)) {
            // Turn IASA ownership:
            // - ftCo_Turn_IASA temporarily flips fp->facing_dir to mv.co.turn.facing_after before
            //   ftCo_800D68C0 consumes grounded special input, then restores it only on the
            //   fallthrough path.
            // - Reflector start entered from Turn therefore inherits facing_after on the consume path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
            if (a == (uint16_t)MSL_ACT_TURN && !batch->state.turn_has_turned[idx]) {
              batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
            }
            shine_release_setvars(batch, idx, ch);
            enter_shine_ground_start(batch, idx, ms);
            shine_entered_this_frame = 1u;
          }
        } else {
          if (action_allows_shine_entry_air(batch, idx, a)) {
            shine_release_setvars(batch, idx, ch);
            enter_shine_air_start(batch, idx, ch, ms);
            shine_entered_this_frame = 1u;
          }
        }
      }

      const uint16_t a2 = batch->state.action_id[idx];
      if (!action_is_shine(a2)) {
        continue;
      }

      // Keep animation_index stable for shine states (avoid seed carry-through).
      switch (a2) {
        case MSL_ACT_FX_SPECIAL_LW_START:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
          break;
        case MSL_ACT_FX_SPECIAL_LW_LOOP:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
          break;
        case MSL_ACT_FX_SPECIAL_LW_HIT:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_hit;
          break;
        case MSL_ACT_FX_SPECIAL_LW_END:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
          break;
        case MSL_ACT_FX_SPECIAL_LW_TURN:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_START:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_hit;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_END:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_TURN:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
          break;
        default:
          break;
      }

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      const float anim_frame_f32 = batch->state.anim_frame_f32[idx];
      // Decomp ordering: SpecialLw Anim callbacks run in Fighter_8006A360 (prio 1) before
      // Fighter_procUpdate applies current-frame input (prio 3). Latch B-release from the
      // pre-input snapshot while leaving entry/IASA checks on current input below.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
      //   ftFx_SpecialLwStart_Anim,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,
      //   ftFx_SpecialLwHit_Anim}
      const uint16_t held = batch->state.prev_input_buttons[idx];

      // Decomp ordering: if an Anim callback transitions into Loop (e.g. Start->Loop or Turn->Loop),
      // the destination state's IASA can run later in the same frame. Allow one follow-up pass.
      uint16_t a_work = a2;
      for (int it = 0; it < 2; it++) {
        switch (a_work) {
          case MSL_ACT_FX_SPECIAL_LW_START:
            if (shine_entered_this_frame == 0u) {
              // Decomp: Start_Anim latches isRelease only; releaseLag is not decremented in Start.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Anim
              shine_release_latch_anim(batch, idx, held);
            }
            if (anim_finished(cid, ms->speciallw_ground_start, anim_frame_f32)) {
              if (on_ground) {
                enter_shine_ground_loop(batch, idx, ms);
              } else {
                enter_shine_air_loop(batch, idx, ms);
              }
              a_work = batch->state.action_id[idx];
              continue;
            }
            break;
          case MSL_ACT_FX_SPECIAL_AIR_LW_START:
            if (shine_entered_this_frame == 0u) {
              // Decomp: SpecialAirLwStart_Anim latches isRelease only.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwStart_Anim
              shine_release_latch_anim(batch, idx, held);
            }
            if (anim_finished(cid, ms->speciallw_air_start, anim_frame_f32)) {
              if (on_ground) {
                enter_shine_ground_loop(batch, idx, ms);
              } else {
                enter_shine_air_loop(batch, idx, ms);
              }
              a_work = batch->state.action_id[idx];
              continue;
            }
            break;
          case MSL_ACT_FX_SPECIAL_LW_LOOP: {
            // Slippi post-frame alignment: gate Loop->End from the reseeded lag value before
            // decrementing for this simulated frame, so teacher-forced one-step mirrors when the
            // post snapshot crosses the release boundary.
            shine_release_latch_anim(batch, idx, held);
            if (shine_release_should_end(batch, idx)) {
              enter_shine_ground_end(batch, idx, ms);
              break;
            }
            if (batch->state.shine_release_lag[idx] > 0u) {
              batch->state.shine_release_lag[idx] =
                  (uint8_t)(batch->state.shine_release_lag[idx] - 1u);
            }
            if (stick_wants_turn(c, batch->state.input_main_x[idx], batch->state.facing[idx])) {
              enter_shine_ground_turn(batch, idx, ms);
              break;
            }
            // Jump cancel -> KneeBend (grounded).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwLoop_IASA
            const MslJumpInput j_in =
                jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
            if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
              batch->state.kneebend_is_short_hop[idx] = 0;
              break;
            }
          } break;
          case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP: {
            shine_release_latch_anim(batch, idx, held);
            if (shine_release_should_end(batch, idx)) {
              enter_shine_air_end(batch, idx, ms);
              break;
            }
            if (batch->state.shine_release_lag[idx] > 0u) {
              batch->state.shine_release_lag[idx] =
                  (uint8_t)(batch->state.shine_release_lag[idx] - 1u);
            }
            if (stick_wants_turn(c, batch->state.input_main_x[idx], batch->state.facing[idx])) {
              enter_shine_air_turn(batch, idx, ms);
              break;
            }
            // Jump cancel -> JumpAerial (aerial).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_IASA
            if (((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) ||
                did_tap_jump(c, stick_y, tilt_timer_y)) {
              if (batch->state.jumps_left[idx] > 0) {
                enter_jump_aerial_basic(batch, idx, ch, c, stick_x, facing_dir);
                break;
              }
            }
          } break;
          case MSL_ACT_FX_SPECIAL_LW_HIT:
            shine_release_tick_anim(batch, idx, held);
            if (anim_finished(cid, ms->speciallw_ground_hit, anim_frame_f32)) {
              if (shine_release_should_end(batch, idx)) {
                enter_shine_ground_end(batch, idx, ms);
              } else {
                enter_shine_ground_loop(batch, idx, ms);
                a_work = batch->state.action_id[idx];
                continue;
              }
            }
            break;
          case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
            shine_release_tick_anim(batch, idx, held);
            if (anim_finished(cid, ms->speciallw_air_hit, anim_frame_f32)) {
              if (shine_release_should_end(batch, idx)) {
                enter_shine_air_end(batch, idx, ms);
              } else {
                enter_shine_air_loop(batch, idx, ms);
                a_work = batch->state.action_id[idx];
                continue;
              }
            }
            break;
          case MSL_ACT_FX_SPECIAL_LW_TURN: {
            shine_release_tick_anim(batch, idx, held);
            const int16_t tf = (int16_t)ch->reflector_turn_frames;
            const int16_t af = batch->state.action_frame[idx];
            if (tf > 0 && af >= (int16_t)(tf - 1)) {
              if (shine_release_should_end(batch, idx)) {
                enter_shine_ground_end(batch, idx, ms);
              } else {
                enter_shine_ground_loop(batch, idx, ms);
                a_work = batch->state.action_id[idx];
                continue;
              }
            }
          } break;
          case MSL_ACT_FX_SPECIAL_AIR_LW_TURN: {
            shine_release_tick_anim(batch, idx, held);
            const int16_t tf = (int16_t)ch->reflector_turn_frames;
            const int16_t af = batch->state.action_frame[idx];
            if (tf > 0 && af >= (int16_t)(tf - 1)) {
              if (shine_release_should_end(batch, idx)) {
                enter_shine_air_end(batch, idx, ms);
              } else {
                enter_shine_air_loop(batch, idx, ms);
                a_work = batch->state.action_id[idx];
                continue;
              }
            }
          } break;
          case MSL_ACT_FX_SPECIAL_LW_END:
            if (anim_finished(cid, ms->speciallw_ground_end, anim_frame_f32)) {
              // Decomp callback order:
              // - ftFx_SpecialLwEnd_Anim calls ftCommon_8007DB24 then ftCommon_8007D92C.
              // - grounded ftCommon_8007D92C resolves to Wait via ft_8008A2BC.
              // - destination Wait_IASA can then admit Dash or backward-flick TurnSmash on the
              //   same frame through ftCo_Dash_CheckInput, or ordinary Turn through
              //   ftCo_Turn_CheckInput.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwEnd_Anim
              // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DB24,ftCommon_8007D92C}
              // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Dash.c,ftCo_Turn.c}
              enter_wait(batch, idx);
              if (shine_is_dash_flick(c, stick_x, tilt_timer_x)) {
                if ((stick_x * facing_dir) < 0.0f) {
                  enter_turn_smash_from_wait_iasa(batch, idx, facing_dir);
                } else {
                  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                  batch->state.dash_x4[idx] = 1u;
                  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                  // Decomp: ftCo_Dash_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
                  msl_anim_timebase_tick_once(batch, idx);
                  batch->state.tilt_timer_x[idx] = 0xFEu;
                }
              } else if (stick_wants_turn(c, batch->state.input_main_x[idx],
                                          batch->state.facing[idx])) {
                enter_turn_basic_from_wait_iasa(batch, idx, ch);
              }
            }
            break;
          case MSL_ACT_FX_SPECIAL_AIR_LW_END:
            if (anim_finished(cid, ms->speciallw_air_end, anim_frame_f32)) {
              if (on_ground) {
                enter_wait(batch, idx);
              } else {
                enter_fall(batch, idx);
                // Decomp: aerial Shine End also calls ftCommon_8007D92C, which enters Fall; the
                // destination Fall_IASA can consume double-jump input later in the same fighter proc.
                // Keep this slice to JumpAerial, the replay-real F20 residual covered here.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwEnd_Anim
                // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_CheckInput
                if ((((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) ||
                     did_tap_jump(c, stick_y, tilt_timer_y)) &&
                    batch->state.jumps_left[idx] > 0) {
                  enter_jump_aerial_basic(batch, idx, ch, c, stick_x, facing_dir);
                }
              }
            }
            break;
          default:
            break;
        }
        break;
      }
    }
  }
}

void shine_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslSpecialMsids* ms = msl_special_msids(cid);
      if (ms == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
      const float cur_frame = batch->state.anim_frame_f32[idx];

      if (action_is_shine_ground(a) && !on_ground) {
        // Ground -> air: preserve anim frame.
        batch->state.action_id[idx] =
            (uint16_t)(a + (uint16_t)MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA);
        // Remap msid per-state.
        const uint16_t a2 = batch->state.action_id[idx];
        if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_HIT) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_hit;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
        }
        // Decomp: every grounded Shine ground->air collision handler calls ftCommon_8007D5D4
        // before Fighter_ChangeMotionState. That helper sets x1968_jumpsUsed=1; Slippi exposes
        // the inverse jumps-left lane, so preserve max_jumps-1 across this handoff.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialLwStart_GroundToAir,ftFx_SpecialLwLoop_GroundToAir,
        //   ftFx_SpecialLwHit_GroundToAir,ftFx_SpecialLwEnd_GroundToAir,
        //   ftFx_SpecialLwTurn_GroundToAir}
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        const MslCharParams* ch = msl_char_params(cid);
        if (ch != NULL) {
          batch->state.jumps_left[idx] = (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
        }
        msl_anim_timebase_enter(batch, idx, cur_frame, 1.0f);
      } else if (action_is_shine_air(a) && on_ground) {
        // Air -> ground: preserve anim frame.
        const MslCharParams* ch = msl_char_params(cid);
        batch->state.action_id[idx] =
            (uint16_t)(a - (uint16_t)MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA);
        const uint16_t a2 = batch->state.action_id[idx];
        if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_LOOP) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_HIT) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_hit;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_END) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
        }
        // Decomp: every SpecialAirLw* AirToGround handler calls ftCommon_8007D7FC before
        // Fighter_ChangeMotionState, refreshing grounded jump ownership on the landing frame.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialAirLwStart_AirToGround,ftFx_SpecialAirLwLoop_AirToGround,
        //   ftFx_SpecialAirLwHit_AirToGround,ftFx_SpecialAirLwEnd_AirToGround,
        //   ftFx_SpecialAirLwTurn_AirToGround}
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D7FC
        if (ch != NULL) {
          batch->state.jumps_left[idx] = ch->max_jumps;
        }
        msl_anim_timebase_enter(batch, idx, cur_frame, 1.0f);
      }
    }
  }
}
