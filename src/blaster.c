#include "blaster.h"
#include "ids.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "attack_identity.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "dash_iasa.h"
#include "input_axis.h"
#include "laser_params.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "special_msids.h"
#include "throw_flow.h"

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_ID_FOX) || (char_id == (uint8_t)MSL_CHAR_ID_FALCO);
}

static inline uint8_t action_is_blaster(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_N_START:
    case MSL_ACT_FX_SPECIAL_N_LOOP:
    case MSL_ACT_FX_SPECIAL_N_END:
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t blaster_aircatchhit_enters_wait(const MslBatch* batch,
                                                      const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const float scale_y =
      batch->state.fighter_scale_y[idx] > 0.0f ? batch->state.fighter_scale_y[idx] : 1.0f;
  // ftCo_AirCatchHit_Coll shares ft_80082B1C's Wait/Landing velocity split.
  // refs/melee/src/melee/ft/ft_081B.c::{ftCo_AirCatchHit_Coll,ft_80082B1C}
  return msl_ftco_80082b1c_enters_wait(c, scale_y, batch->state.speed_y_self[idx]);
}

static inline uint8_t specialn_is_blaster_loop_requested(const MslBatch* batch, size_t idx) {
  // Decomp (GALE01): the SpecialN Start/Loop IASA callbacks set fp->mv.fx.SpecialN.isBlasterLoop
  // when:
  //   fp->cmd_vars[0] != 0 && (fp->input.x668 & HSD_PAD_B)
  // where fp->input.x668 is the per-frame pressed mask (rising edge).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNStart_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_IASA
  //
  // This simulator does not yet model mv.fx.SpecialN.isBlasterLoop directly. However, extracted
  // command data gives the cmd_vars[0] window, and the seed schema includes fp->x67D ("frames since
  // last B press", saturating at 0xFF). Infer the latch only when that B press happened during the
  // cmd_vars[0] window, rather than anywhere in the motion state.
  // data/moves/{fox,falco}.json specials_by_msid["<msid>"].events set_cmd_var(idx=0)
  if (batch == NULL) {
    return 0;
  }
  const uint32_t msid_u32 = batch->state.animation_index[idx];
  if (msid_u32 > 0xFFFFu) {
    return 0;
  }
  const int af = (int)batch->state.action_frame[idx];
  if (af < 0) {
    return 0;
  }
  const int x67d = (int)batch->state.x67D[idx];
  if (x67d < 0 || x67d > 255) {
    return 0;
  }
  if (x67d == 0xFF) {
    return 0;
  }
  if (x67d >= af) {
    return 0;
  }
  if ((batch->state.prev_input_buttons[idx] & (uint16_t)MSL_BUTTON_B) != 0u) {
    return 1u;
  }
  const int b_press_af = af - x67d;
  return move_tables_special_cmd0_active_at_frame(batch->state.char_id[idx], (uint16_t)msid_u32,
                                                  b_press_af);
}

static inline uint8_t action_allows_special_entry_ground(const MslBatch* batch, size_t idx,
                                                         uint16_t action_id) {
  // Spotdodge / roll IASA do not route through grounded special checks in decomp:
  // - EscapeN_IASA is empty.
  // - EscapeF_IASA / EscapeB_IASA only call ftCo_8009563C (item-throw family), not grounded
  //   special dispatch.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{
  //   ftCo_EscapeN_IASA,ftCo_EscapeF_IASA,ftCo_EscapeB_IASA
  // }
  // KneeBend IASA checks Attack100/Catch/AttackHi4 only and does not route into grounded special
  // dispatch.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
  // Grounded Attack* states gate their IASA on fp->allow_interrupt and only then delegate into
  // ftCo_Wait_IASA. They must not bypass that owner through the generic grounded-special gate here.
  // Grounded-attack special entry is modeled in localized locomotion callback bridges instead.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  //
  // Keep this narrowly scoped: other ground states (including shield) have non-empty IASA callbacks
  // and may allow specials depending on per-state input checks.
  if (msl_action_is_live_shield_family(action_id)) {
    // Decomp: these guard-family IASA callbacks do not route through the grounded special
    // dispatch; GuardSetOff_IASA is empty.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA,ftCo_GuardSetOff_IASA}
    return 0;
  }
  switch (action_id) {
    case (uint16_t)MSL_ACT_ESCAPE_N:
    case (uint16_t)MSL_ACT_ESCAPE_F:
    case (uint16_t)MSL_ACT_ESCAPE_B:
    case (uint16_t)MSL_ACT_KNEE_BEND:
      return 0;
    case (uint16_t)MSL_ACT_GUARD_OFF:
      // GuardOff_IASA runs the special/attack chain only while mv.co.guard.x1C is non-zero. x1C
      // is armed by ftCo_80094138 on powershield-active shield contact.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOff_IASA,ftCo_80094138}
      return (batch != NULL && batch->state.guard_special_enable_timer_x1c[idx] != 0u) ? 1u : 0u;
    case (uint16_t)MSL_ACT_ATTACK_DASH:
    case (uint16_t)MSL_ACT_ATTACK_S3_HI:
    case (uint16_t)MSL_ACT_ATTACK_S3_HI_S:
    case (uint16_t)MSL_ACT_ATTACK_S3_S:
    case (uint16_t)MSL_ACT_ATTACK_S3_LW_S:
    case (uint16_t)MSL_ACT_ATTACK_S3_LW:
    case (uint16_t)MSL_ACT_ATTACK_HI3:
    case (uint16_t)MSL_ACT_ATTACK_LW3:
      return 0;
    default:
      break;
  }
  if (!msl_action_is_ground_locomotion(action_id)) {
    return 0;
  }
  return 1;
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

static inline uint8_t damage_air_or_fly_allows_special_air_iasa(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Damage/DamageFly IASA split:
  // - Damage_IASA calls Fall_IASA_Inner only when !fp->x221C_b6.
  // - DamageFly_IASA calls DamageFall_IASA only when !fp->x221C_b6.
  // - Fall_IASA_Inner and DamageFall_IASA both route through ftCo_SpecialAir_CheckInput.
  // - x221C_b6 is set with the hitstun scalar on Damage entry and cleared when that scalar
  //   reaches zero. Modelplay / one-step seeds can expose an impossible stale-clear flag byte;
  //   keep the explicit hitstun counter as the same source lock so a victim cannot SpecialN out of
  //   active hitstun.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_IASA,ftCo_DamageFly_IASA,ftCo_8008DCE0,ftCo_8008F744}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN) != 0u) {
    return 0u;
  }
  return (batch->state.hitstun[idx] == 0u) ? 1u : 0u;
}

static inline uint8_t action_allows_special_entry_air(const MslBatch* batch, size_t idx,
                                                      uint16_t action_id) {
  // Decomp-special input ownership:
  // - Jump/Fall-family IASA owners route through ftCo_SpecialAir_CheckInput.
  // - AttackAir DO_IASA is intentionally excluded: it checks EscapeAir, item/aircatch, item throw,
  //   and JumpAerial branches, but does not call ftCo_SpecialAir_CheckInput.
  // - DamageFall_IASA also routes through ftCo_SpecialAir_CheckInput.
  // - Pass_IASA routes through the same aerial special gate after platform drop-through.
  // - FallSpecial_IASA does not; it only checks attack/item/jump-owned branches.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::DO_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_IASA
  if (throw_flow_release_source_blocks_iasa(batch, idx, action_id)) {
    return 0u;
  }
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
    case MSL_ACT_PASS:
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
      return 1u;
    default:
      break;
  }
  if (action_is_damage_air_or_fly_special_iasa(action_id)) {
    return damage_air_or_fly_allows_special_air_iasa(batch, idx);
  }
  return 0u;
}

typedef enum MslSpacieBSpecialKind {
  MSL_SPACIE_B_SPECIAL_NONE = 0,
  MSL_SPACIE_B_SPECIAL_NEUTRAL = 1,
  MSL_SPACIE_B_SPECIAL_SIDE = 2,
  MSL_SPACIE_B_SPECIAL_UP = 3,
} MslSpacieBSpecialKind;

static inline MslSpacieBSpecialKind resolve_spacie_b_special_kind(const MslCommonParams* c,
                                                                  uint8_t grounded, float stick_x,
                                                                  float stick_y) {
  if (c == NULL) {
    return MSL_SPACIE_B_SPECIAL_NONE;
  }
  // Decomp-special input order references:
  // - Grounded interrupt chains call SpecialS -> SpecialHi -> SpecialN -> SpecialLw.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_SpecialS_CheckInput,ftCo_Attack100_CheckInput,ftCo_800D67C4}
  // - Aerial interrupt helper checks Up -> Down -> Side -> Neutral.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  //
  // This resolver is intentionally scoped to Neutral/Side/Up routing; Down-B is owned by shine.c.
  // Grounded dispatch checks Side-B before the generic Up/N/Down chain, while aerial dispatch
  // checks Up -> Down -> Side -> Neutral.
  //
  // Ordering guarantee:
  // - action_update() runs shine_update_pre_physics() before blaster_update_pre_physics().
  // - Shine entry uses the same raw B-edge (`input_buttons_pressed`) and does not consume it.
  // So a grounded B-edge + side+down diagonal first gives Shine a chance to reject because
  // Side-B preempts Down-B, then this resolver enters Side-B.
  const float abs_x = stick_x < 0.0f ? -stick_x : stick_x;
  if (grounded) {
    if (abs_x >= c->special_stick_x_threshold_side) {
      return MSL_SPACIE_B_SPECIAL_SIDE;
    }
    if (stick_y >= c->special_stick_y_threshold) {
      return MSL_SPACIE_B_SPECIAL_UP;
    }
    if (stick_y <= -c->special_stick_y_threshold) {
      return MSL_SPACIE_B_SPECIAL_NONE;
    }
    return MSL_SPACIE_B_SPECIAL_NEUTRAL;
  }
  if (stick_y <= -c->special_stick_y_threshold) {
    return MSL_SPACIE_B_SPECIAL_NONE;
  }
  if (stick_y >= c->special_stick_y_threshold) {
    return MSL_SPACIE_B_SPECIAL_UP;
  }
  if (abs_x >= c->special_stick_x_threshold_side) {
    return MSL_SPACIE_B_SPECIAL_SIDE;
  }
  return MSL_SPACIE_B_SPECIAL_NEUTRAL;
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t specialn_end_try_destination_wait_forward_dash(MslBatch* batch,
                                                                     const MslCommonParams* c,
                                                                     size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  if (batch->state.input_buttons_pressed[idx] != 0u) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float facing_dir = batch->state.facing_dir1[idx] >= 0 ? 1.0f : -1.0f;
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  // Decomp callback order:
  // - ftFx_SpecialNEnd_Anim removes the blaster and calls ft_8008A2BC.
  // - The destination Wait_IASA later reaches ftCo_Dash_CheckInput before Squat/Turn/Walk.
  // - This slice only claims the buttonless forward-dash branch; earlier Wait_IASA button owners
  //   and the opposite-facing turn-smash branch stay with their own owner families.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
  if ((stick_x * facing_dir) < c->dash_flick_abs) {
    return 0u;
  }
  if (tilt_timer_x >= c->dash_flick_tilt_max_frames) {
    return 0u;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
  batch->state.dash_x4[idx] = 1u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftCo_Dash_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.tilt_timer_x[idx] = 0xFEu;
  return 1u;
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftFx_SpecialAirNEnd_Anim exits through ftCo_Fall_Enter when landing-lag attr x18 is 0;
  // ftCo_Fall_Enter uses Ft_MF_KeepFastFall.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNEnd_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside Fighter_ChangeMotionState)
  batch->state.fall_fast[idx] = keep_fastfall;
}

static inline uint8_t specialn_destination_fall_tap_jump(const MslCommonParams* c, float stick_y,
                                                         uint8_t tilt_timer_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1u : 0u;
}

static inline uint16_t specialn_destination_fall_airjump_action(const MslCommonParams* c,
                                                                float stick_x, float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? MSL_ACT_JUMP_AERIAL_F
                                                            : MSL_ACT_JUMP_AERIAL_B;
}

static inline void specialn_air_end_try_destination_fall_jump(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return;
  }
  if (batch->state.jumps_left[idx] == 0u) {
    return;
  }

  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const uint8_t has_jump =
      ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0u ||
       specialn_destination_fall_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx]) != 0u)
          ? 1u
          : 0u;
  if (has_jump == 0u) {
    return;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float facing_dir = batch->state.facing_dir1[idx] >= 0 ? 1.0f : -1.0f;
  const uint16_t act = specialn_destination_fall_airjump_action(c, stick_x, facing_dir);

  // Decomp ordering:
  // - ftFx_SpecialAirNEnd_Anim exits through ftCo_Fall_Enter when x18 landing lag is zero.
  // - The destination Fall IASA can run later in the same Fighter proc and consume the
  //   JumpAerial path.
  // - ftCo_JumpAerial_Enter_Basic sets aerial-jump velocity, x671=0xFE, decrements jumps left,
  //   and calls ftCommon_8007D5D4 (ECB lock).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNEnd_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = act == (uint16_t)MSL_ACT_JUMP_AERIAL_F
                                          ? (uint32_t)MSL_SM_JUMP_AERIAL_F
                                          : (uint32_t)MSL_SM_JUMP_AERIAL_B;
  batch->state.on_ground[idx] = 0u;
  batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
  batch->state.speed_y_self[idx] = ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.fall_fast[idx] = 0u;
  batch->state.jumps_left[idx]--;
  batch->state.ecb_lock_timer[idx] = 10u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_blaster_start(MslBatch* batch, size_t idx, const MslLaserParams* lp,
                                       uint8_t grounded) {
  if (lp == NULL) {
    return;
  }
  if (grounded) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_START;
    batch->state.animation_index[idx] = (uint32_t)lp->ground_start_msid;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START;
    batch->state.animation_index[idx] = (uint32_t)lp->air_start_msid;
    // Decomp: ftFx_SpecialAirN_Enter calls Fighter_ChangeMotionState(..., flags=0), i.e. no
    // Ft_MF_KeepFastFall. ChangeMotionState clears fp->fall_fast when KeepFastFall is absent.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirN_Enter
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    batch->state.fall_fast[idx] = 0u;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: both grounded and aerial SpecialN enter paths call ftAnim_8006EBA4 immediately after
  // ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialN_Enter,ftFx_SpecialAirN_Enter}
  msl_anim_timebase_defer_tick_once(batch, idx);

  // Decomp split:
  // - Grounded SpecialN enter clears gr_vel/self_vel.x/y/z.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter
  // - Aerial SpecialAirN enter does not clear self velocities.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirN_Enter
  if (grounded) {
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
  }
  // `speed_{x,y}_attack` are fp->x8c_kb_vel. Neither SpecialN enter path clears that lane:
  // grounded entry clears gr_vel/self_vel only after Fighter_ChangeMotionState, and aerial entry
  // leaves self/kb velocities intact. Preserve DamageFly/DamageFall knockback carry into blaster.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialN_Enter,ftFx_SpecialAirN_Enter}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
}

static inline void enter_side_special_start(MslBatch* batch, size_t idx, const MslCommonParams* c,
                                            const MslSpecialMsids* ms, const MslCharParams* ch,
                                            uint8_t grounded, float stick_x) {
  if (batch == NULL || c == NULL || ms == NULL || ch == NULL) {
    return;
  }
  const uint16_t source_action_id = batch->state.action_id[idx];
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  // Decomp: grounded/aerial Side-B entry reverses facing when lstick.x * facing_dir < -x220.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  if (stick_x * facing_dir < -c->special_side_reverse_threshold) {
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
  }
  if (grounded) {
    // Decomp: ftCo_SpecialS.c::doEnter first damps gr_vel through co_attrs.xB8 and
    // ft_GetGroundFrictionMultiplier, then the grounded Side-B entry divides gr_vel by
    // `x28_FOX_ILLUSION_GROUND_VEL_X` before entering SpecialSStart.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSStart_Enter
    // data/characters/{fox,falco}.json::{side_special_ground_entry_vel_mul,illusion_ground_vel_x}
    batch->state.speed_ground_x_self[idx] +=
        -(batch->state.speed_ground_x_self[idx] * (1.0f - ch->side_special_ground_entry_vel_mul)) *
        batch->state.ground_friction_mul[idx];
    if (ch->illusion_ground_vel_x > 0.0f) {
      batch->state.speed_ground_x_self[idx] /= ch->illusion_ground_vel_x;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_START;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
    if (source_action_id == (uint16_t)MSL_ACT_DASH) {
      // Dash_IASA falls through to its terminal gr_vel scalar even when the early Side-B branch
      // succeeds. Wait/Run/etc. grounded Side-B entries do not run this Dash callback tail.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
      dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
    }
  } else {
    // Decomp: aerial Side-B entry divides self_vel.x through the same attr before entering.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
    // data/characters/{fox,falco}.json::illusion_ground_vel_x
    if (ch->illusion_ground_vel_x > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->illusion_ground_vel_x;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_air_start;
    // Decomp: ftFx_SpecialAirSStart_Enter zeroes self_vel.y at aerial Side-B entry.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
    // Decomp: ftFx_SpecialAirSStart_Enter also consumes all jumps via x1968_jumpsUsed=max_jumps.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.jumps_left[idx] = 0u;
  }
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
}

static inline void enter_specialhi_hold(MslBatch* batch, size_t idx, const MslSpecialMsids* ms,
                                        const MslCharParams* ch, uint8_t grounded) {
  if (batch == NULL || ms == NULL || ch == NULL) {
    return;
  }
  if (grounded) {
    // Decomp: grounded Firefox charge entry preserves gr_vel through `x58_FOX_FIREFOX_VEL_X`.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHi_Enter
    // data/characters/{fox,falco}.json::firefox_hold_vel_x
    if (ch->firefox_hold_vel_x > 0.0f) {
      batch->state.speed_ground_x_self[idx] /= ch->firefox_hold_vel_x;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD;
    batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_hold;
  } else {
    // Decomp: aerial Firefox charge entry divides self_vel.x by the same attr, then zeroes
    // self_vel.y.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHiStart_Enter
    // data/characters/{fox,falco}.json::firefox_hold_vel_x
    if (ch->firefox_hold_vel_x > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->firefox_hold_vel_x;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR;
    batch->state.animation_index[idx] = (uint32_t)ms->specialhi_air_hold;
    // Decomp: ftFx_SpecialAirHiStart_Enter zeroes self_vel.y before entering HoldAir.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHiStart_Enter
    batch->state.speed_y_self[idx] = 0.0f;
  }
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: SpecialHi hold enters call ftAnim_8006EBA4 on the entered state.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHi_Enter,ftFx_SpecialAirHiStart_Enter}
  msl_anim_timebase_defer_tick_once(batch, idx);
}

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

uint8_t blaster_try_enter_ground_from_iasa_subset(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint8_t cid = batch->state.char_id[idx];
  if (!is_fox_falco(cid) || !batch->state.on_ground[idx]) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)MSL_BUTTON_B) == 0u) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(cid);
  const MslSpecialMsids* ms = msl_special_msids(cid);
  if (ch == NULL || ms == NULL) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  switch (resolve_spacie_b_special_kind(c, 1u, stick_x, stick_y)) {
    case MSL_SPACIE_B_SPECIAL_SIDE:
      enter_side_special_start(batch, idx, c, ms, ch, 1u, stick_x);
      return 1u;
    case MSL_SPACIE_B_SPECIAL_UP:
      enter_specialhi_hold(batch, idx, ms, ch, 1u);
      return 1u;
    case MSL_SPACIE_B_SPECIAL_NEUTRAL:
      enter_blaster_start(batch, idx, laser_params_get(cid), 1u);
      return 1u;
    default:
      return 0u;
  }
}

uint8_t blaster_try_enter_ground_from_wait_iasa(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id != (uint16_t)MSL_ACT_WAIT && action_id != (uint16_t)MSL_ACT_SQUAT) {
    return 0u;
  }
  return blaster_try_enter_ground_from_iasa_subset(batch, c, idx);
}

uint8_t blaster_try_enter_ground_specialhi_from_kneebend_iasa(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_KNEE_BEND ||
      batch->state.on_ground[idx] == 0u) {
    return 0u;
  }
  const uint8_t cid = batch->state.char_id[idx];
  if (!is_fox_falco(cid)) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)MSL_BUTTON_B) == 0u) {
    return 0u;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (stick_y < c->special_stick_y_threshold) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(cid);
  const MslSpecialMsids* ms = msl_special_msids(cid);
  if (ch == NULL || ms == NULL) {
    return 0u;
  }
  // Source owner:
  // - Fighter_UnkIncrementCounters_8006ABEC resets fp->x686 when ftCo_800D6928 sees a B+Up edge.
  // - ftCo_KneeBend_IASA immediately calls ftCo_Attack100_CheckInput, which enters ftData_SpecialHi
  //   only when x686 == 0. Do not admit Side/Neutral/Down-B from this KneeBend path.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkIncrementCounters_8006ABEC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_800D6928,ftCo_Attack100_CheckInput}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
  enter_specialhi_hold(batch, idx, ms, ch, 1u);
  return 1u;
}

uint8_t blaster_try_enter_air_from_iasa_subset(MslBatch* batch, const MslCommonParams* c,
                                               size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint8_t cid = batch->state.char_id[idx];
  if (!is_fox_falco(cid) || batch->state.on_ground[idx]) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)MSL_BUTTON_B) == 0u) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(cid);
  const MslSpecialMsids* ms = msl_special_msids(cid);
  if (ch == NULL || ms == NULL) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  switch (resolve_spacie_b_special_kind(c, 0u, stick_x, stick_y)) {
    case MSL_SPACIE_B_SPECIAL_SIDE:
      enter_side_special_start(batch, idx, c, ms, ch, 0u, stick_x);
      return 1u;
    case MSL_SPACIE_B_SPECIAL_UP:
      enter_specialhi_hold(batch, idx, ms, ch, 0u);
      return 1u;
    case MSL_SPACIE_B_SPECIAL_NEUTRAL:
      enter_blaster_start(batch, idx, laser_params_get(cid), 0u);
      return 1u;
    default:
      return 0u;
  }
}

static inline void blaster_update_active_timeline_player(MslBatch* batch, const MslCommonParams* c,
                                                         const MslCharParams* ch,
                                                         const MslLaserParams* lp, size_t idx,
                                                         uint8_t cid) {
  if (batch == NULL || c == NULL || ch == NULL || lp == NULL) {
    return;
  }

  const uint16_t action_id = batch->state.action_id[idx];
  if (!action_is_blaster(action_id)) {
    return;
  }

  // Ensure SpecialN/SpecialAirN always has a valid msid-backed animation_index.
  // This keeps the ECB/collision substrate stable under reseed and removes the need for
  // teacher-forcing guards in post-collision landing logic.
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_N_START:
      batch->state.animation_index[idx] = (uint32_t)lp->ground_start_msid;
      break;
    case MSL_ACT_FX_SPECIAL_N_LOOP:
      batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
      break;
    case MSL_ACT_FX_SPECIAL_N_END:
      batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
      batch->state.animation_index[idx] = (uint32_t)lp->air_start_msid;
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
      batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
      batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
      break;
    default:
      break;
  }

  // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }

  const float anim_frame_f32 = batch->state.anim_frame_f32[idx];
  const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_N_START:
      if (anim_finished(cid, lp->ground_start_msid, anim_frame_f32)) {
        const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_LOOP;
        batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
        // Decomp: ftFx_SpecialNStart_Anim transitions via Fighter_ChangeMotionState without
        // Ft_MF_KeepFastFall, so fp->fall_fast is cleared on entry.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNStart_Anim
        // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside ChangeMotionState)
        batch->state.fall_fast[idx] = 0;
        if (had_fastfall != 0u) {
          batch->state.tilt_timer_y[idx] = 0xFEu;
        }
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      break;
    case MSL_ACT_FX_SPECIAL_N_LOOP:
      if (anim_finished(cid, lp->ground_loop_msid, anim_frame_f32)) {
        const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
        if (specialn_is_blaster_loop_requested(batch, idx)) {
          // Loop -> Loop: request another shot cycle.
          // Decomp: Loop restarts itself via Fighter_ChangeMotionState without KeepFastFall, then
          // ftFx_SpecialN_OnChangeAction calls ft_800892A0 before the shot accessory callback.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
          //   ftFx_SpecialNLoop_Anim,ftFx_SpecialN_OnChangeAction,ftFx_SpecialN_CreateBlasterShot}
          // refs/melee/src/melee/ft/ft_0881.c::ft_800892A0
          batch->state.fall_fast[idx] = 0;
          if (had_fastfall != 0u) {
            batch->state.tilt_timer_y[idx] = 0xFEu;
          }
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          attack_identity_restart_same_move_ft_800892A0(batch, idx);
        } else {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_END;
          batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
          // Decomp: Loop -> End uses Fighter_ChangeMotionState without KeepFastFall.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
          batch->state.fall_fast[idx] = 0;
          if (had_fastfall != 0u) {
            batch->state.tilt_timer_y[idx] = 0xFEu;
          }
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      }
      break;
    case MSL_ACT_FX_SPECIAL_N_END:
      if (anim_finished(cid, lp->ground_end_msid, anim_frame_f32)) {
        enter_wait(batch, idx);
        (void)specialn_end_try_destination_wait_forward_dash(batch, c, idx);
      }
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
      if (anim_finished(cid, lp->air_start_msid, anim_frame_f32)) {
        const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP;
        batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
        // Decomp: ftFx_SpecialAirNStart_Anim transitions via Fighter_ChangeMotionState without
        // Ft_MF_KeepFastFall, so fp->fall_fast is cleared on entry.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Anim
        // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside ChangeMotionState)
        batch->state.fall_fast[idx] = 0;
        if (had_fastfall != 0u) {
          batch->state.tilt_timer_y[idx] = 0xFEu;
        }
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
      if (anim_finished(cid, lp->air_loop_msid, anim_frame_f32)) {
        const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
        if (specialn_is_blaster_loop_requested(batch, idx)) {
          // Decomp: Aerial loop restarts itself via Fighter_ChangeMotionState without KeepFastFall.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
          batch->state.fall_fast[idx] = 0;
          if (had_fastfall != 0u) {
            batch->state.tilt_timer_y[idx] = 0xFEu;
          }
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          attack_identity_restart_same_move_ft_800892A0(batch, idx);
        } else {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END;
          batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
          // Decomp: Aerial loop -> end uses Fighter_ChangeMotionState without KeepFastFall.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
          batch->state.fall_fast[idx] = 0;
          if (had_fastfall != 0u) {
            batch->state.tilt_timer_y[idx] = 0xFEu;
          }
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      }
      break;
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
      if (anim_finished(cid, lp->air_end_msid, anim_frame_f32)) {
        if (on_ground) {
          enter_wait(batch, idx);
        } else {
          enter_fall(batch, idx);
          specialn_air_end_try_destination_fall_jump(batch, c, ch, idx);
        }
      }
      break;
    default:
      break;
  }
}

void blaster_update_anim_callbacks_pre_input(MslBatch* batch) {
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
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(cid);
      const MslLaserParams* lp = laser_params_get(cid);
      blaster_update_active_timeline_player(batch, c, ch, lp, idx, cid);
    }
  }
}

void blaster_update_pre_physics(MslBatch* batch) {
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
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }
      const MslCommonParams* c = msl_common_params();
      const MslSpecialMsids* ms = msl_special_msids(cid);
      if (c == NULL || ms == NULL) {
        continue;
      }
      const MslLaserParams* lp = laser_params_get(cid);

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // Entry: route grounded/aerial B-special to Side/Up/Neutral.
      // Down-B is entered in shine_update_pre_physics() earlier in action_update() ordering.
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      if (!action_is_blaster(a) && (pressed & (uint16_t)MSL_BUTTON_B) != 0) {
        uint8_t allow = 0;
        if (on_ground) {
          // Landing special-case: Landing lag actions should not be interruptible until their IASA
          // gate allows it.
          //
          // Decomp:
          // - Normal Landing IASA returns early while fp->cur_anim_frame < fp->co_attrs.normal_landing_lag,
          //   then checks specials including SpecialN (ftCo_800D6824).
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D6824
          // - LandingFallSpecial sets allow_interrupt=false, so the same IASA path blocks all interrupts.
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic
          // - LandingAir* IASA is empty, so those landing-lag actions cannot be interrupted at all.
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_IASA
          allow = action_allows_special_entry_ground(batch, idx, a);
          if (allow) {
            switch (a) {
              case (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL:
              case (uint16_t)MSL_ACT_LANDING_AIR_N:
              case (uint16_t)MSL_ACT_LANDING_AIR_F:
              case (uint16_t)MSL_ACT_LANDING_AIR_B:
              case (uint16_t)MSL_ACT_LANDING_AIR_HI:
              case (uint16_t)MSL_ACT_LANDING_AIR_LW:
                allow = 0;
                break;
              case (uint16_t)MSL_ACT_LANDING: {
                // `anim_timebase_update_pre_input()` runs before input processing, matching decomp
                // prio 1 (Anim) before prio 3 (Input). Use post-advance cur_anim_frame here so the
                // special becomes available on the correct actionable frame.
                const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
                allow = (cur >= (float)ch->landing_lag_frames) ? 1u : 0u;
              } break;
              default:
                break;
            }
          }
        } else {
          // SpecialLw's own IASA can consume a jump into JumpAerial in this same fighter proc.
          // Decomp does not then run the destination JumpAerial special dispatcher on the same input
          // edge, so leave B-special routing to the next frame for those Shine-owned handoffs.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
          //   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwTurn_IASA,ftFx_SpecialAirLwEnd_Anim}
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          if (batch->state.shine_jump_iasa_entered_this_frame[idx] == 0u &&
              action_allows_special_entry_air(batch, idx, a)) {
            // Decomp routing: DamageFall IASA delegates directly to ftCo_SpecialAir_CheckInput, so
            // B-special selection (Neutral/Side/Up) follows the same resolver used by standard air
            // locomotion once B is pressed.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
            allow = ((a == (uint16_t)MSL_ACT_PASSIVE_WALL ||
                      a == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP) &&
                     batch->state.passivewall_timer[idx] != 0u)
                        ? 0u
                        : 1u;
          }
        }
        if (allow) {
          const float stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                               c->lstick_deadzone_x);
          const float stick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]),
                                               c->lstick_deadzone_y);
          const MslSpacieBSpecialKind kind =
              resolve_spacie_b_special_kind(c, on_ground, stick_x, stick_y);
          switch (kind) {
            case MSL_SPACIE_B_SPECIAL_SIDE:
              enter_side_special_start(batch, idx, c, ms, ch, on_ground, stick_x);
              break;
            case MSL_SPACIE_B_SPECIAL_UP:
              // Decomp: Dash IASA has a dedicated grounded Side-B branch, but it does not call the
              // generic Up/Neutral special dispatchers. This simulator runs special dispatch after
              // locomotion, so frame-start Walk/Wait rows may already have become Dash; keep Side-B
              // available below, but block Up-B/Neutral-B from the current Dash/RunBrake owner.
              // Turn_IASA has the same split for Up/Neutral: it calls SpecialS and SpecialLw
              // dispatchers, then attacks/catch, but never ftCo_800D6824. Keep Neutral/Up-B out of
              // steady Turn while preserving Side-B here and Shine in shine_update_pre_physics().
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
              if (a != (uint16_t)MSL_ACT_DASH && a != (uint16_t)MSL_ACT_RUN_BRAKE &&
                  a != (uint16_t)MSL_ACT_TURN) {
                enter_specialhi_hold(batch, idx, ms, ch, on_ground);
              }
              break;
            case MSL_SPACIE_B_SPECIAL_NEUTRAL:
              if (lp != NULL) {
                if (on_ground && (a == (uint16_t)MSL_ACT_DASH || a == (uint16_t)MSL_ACT_RUN_BRAKE ||
                                  a == (uint16_t)MSL_ACT_TURN)) {
                  break;
                }
                if (!on_ground) {
                  // Decomp aerial neutral-B reversal gate:
                  // - ftCo_SpecialAir_CheckInput flips facing for neutral-B when:
                  //     x676_x < p_ftCommonData->x224 &&
                  //     ((facing_dir==-1 && x2228_b7==1) || (facing_dir==+1 && x2228_b7==0))
                  // - x2228_b7 is owned by Fighter's input-counter update block on fresh X entries.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
                  // refs/melee/src/melee/ft/fighter.c:1921-1925,1946-1950
                  if ((float)batch->state.x676_x[idx] < c->special_neutral_reverse_threshold) {
                    const uint8_t facing = batch->state.facing[idx] ? 1u : 0u;  // 1:+1, 0:-1
                    const uint8_t x2228_b7 = batch->state.x2228_b7[idx] ? 1u : 0u;
                    if ((facing == 0u && x2228_b7 == 1u) || (facing == 1u && x2228_b7 == 0u)) {
                      batch->state.facing[idx] = facing ? 0u : 1u;
                      batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
                    }
                  }
                }
                enter_blaster_start(batch, idx, lp, on_ground);
              }
              break;
            case MSL_SPACIE_B_SPECIAL_NONE:
            default:
              break;
          }
        }
      }

      // Update the active SpecialN timeline and handle Start/Loop/End transitions.
      const uint16_t a2 = batch->state.action_id[idx];
      if (!action_is_blaster(a2)) {
        continue;
      }

      // Ensure SpecialN/SpecialAirN always has a valid msid-backed animation_index.
      // This keeps the ECB/collision substrate stable under reseed and removes the need for
      // teacher-forcing guards in post-collision landing logic.
      switch (a2) {
        case MSL_ACT_FX_SPECIAL_N_START:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_start_msid;
          break;
        case MSL_ACT_FX_SPECIAL_N_LOOP:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
          break;
        case MSL_ACT_FX_SPECIAL_N_END:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_START:
          batch->state.animation_index[idx] = (uint32_t)lp->air_start_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
          batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_END:
          batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
          break;
        default:
          break;
      }

      const float anim_frame_f32 = batch->state.anim_frame_f32[idx];

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      switch (a2) {
        case MSL_ACT_FX_SPECIAL_N_START:
          if (anim_finished(cid, lp->ground_start_msid, anim_frame_f32)) {
            const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_LOOP;
            batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
            // Decomp: ftFx_SpecialNStart_Anim transitions via Fighter_ChangeMotionState without
            // Ft_MF_KeepFastFall, so fp->fall_fast is cleared on entry.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNStart_Anim
            // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside ChangeMotionState)
            //
            // Decomp continuity: ftCommon_CheckFallFast writes fp->x671_timer_lstick_tilt_y=0xFE when
            // fastfall latches. If this motion change clears fastfall while it was active, keep x671 in
            // the held-tilt sentinel band so fastfall does not immediately re-latch on the next frame.
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
            batch->state.fall_fast[idx] = 0;
            if (had_fastfall != 0u) {
              batch->state.tilt_timer_y[idx] = 0xFEu;
            }
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          }
          break;
        case MSL_ACT_FX_SPECIAL_N_LOOP:
          if (anim_finished(cid, lp->ground_loop_msid, anim_frame_f32)) {
            if (specialn_is_blaster_loop_requested(batch, idx)) {
              const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
              // Loop -> Loop: request another shot cycle.
              // Decomp: Loop restarts itself via Fighter_ChangeMotionState without KeepFastFall.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
              batch->state.fall_fast[idx] = 0;
              if (had_fastfall != 0u) {
                batch->state.tilt_timer_y[idx] = 0xFEu;
              }
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              attack_identity_restart_same_move_ft_800892A0(batch, idx);
            } else {
              const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_END;
              batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
              // Decomp: Loop -> End uses Fighter_ChangeMotionState without KeepFastFall.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
              batch->state.fall_fast[idx] = 0;
              if (had_fastfall != 0u) {
                batch->state.tilt_timer_y[idx] = 0xFEu;
              }
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_N_END:
          if (anim_finished(cid, lp->ground_end_msid, anim_frame_f32)) {
            enter_wait(batch, idx);
            (void)specialn_end_try_destination_wait_forward_dash(batch, c, idx);
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_START:
          if (anim_finished(cid, lp->air_start_msid, anim_frame_f32)) {
            const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP;
            batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
            // Decomp: ftFx_SpecialAirNStart_Anim transitions via Fighter_ChangeMotionState without
            // Ft_MF_KeepFastFall, so fp->fall_fast is cleared on entry.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Anim
            // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside ChangeMotionState)
            batch->state.fall_fast[idx] = 0;
            if (had_fastfall != 0u) {
              batch->state.tilt_timer_y[idx] = 0xFEu;
            }
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
          if (anim_finished(cid, lp->air_loop_msid, anim_frame_f32)) {
            if (specialn_is_blaster_loop_requested(batch, idx)) {
              const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
              // Decomp: Aerial loop restarts itself via Fighter_ChangeMotionState without KeepFastFall.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
              batch->state.fall_fast[idx] = 0;
              if (had_fastfall != 0u) {
                batch->state.tilt_timer_y[idx] = 0xFEu;
              }
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              attack_identity_restart_same_move_ft_800892A0(batch, idx);
            } else {
              const uint8_t had_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END;
              batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
              // Decomp: Aerial loop -> end uses Fighter_ChangeMotionState without KeepFastFall.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
              batch->state.fall_fast[idx] = 0;
              if (had_fastfall != 0u) {
                batch->state.tilt_timer_y[idx] = 0xFEu;
              }
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_END:
          if (anim_finished(cid, lp->air_end_msid, anim_frame_f32)) {
            if (on_ground) {
              enter_wait(batch, idx);
            } else {
              enter_fall(batch, idx);
              specialn_air_end_try_destination_fall_jump(batch, c, ch, idx);
            }
          }
          break;
        default:
          break;
      }
    }
  }
}

void blaster_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }

      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1u : 0u;
      if (was_ground || !now_ground) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      if (a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START &&
          a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP &&
          a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END) {
        continue;
      }

      // If the seed doesn't provide a valid animation_index, the ECB/collision substrate may snap
      // in unrealistic ways. We enforce a valid msid-backed animation_index in blaster_update_pre_physics,
      // so post-collision landing can be driven purely by ground contact.

      // Landing transition for aerial SpecialN.
      //
      // Decomp:
      // - ftFx_SpecialAirN*_Coll uses ftCo_AirCatchHit_Coll, which runs the ft_80082B1C
      //   self-vel.y threshold and enters Wait for gentle contacts, Landing_Enter_Basic otherwise.
      //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
      //   refs/melee/src/melee/ft/ft_081B.c::{ftCo_AirCatchHit_Coll,ft_80082B1C}
      //
      // Policy:
      // - Use Wait/Landing (not LandingAir*): this matches ftCo_AirCatchHit_Coll.
      // - Preserve horizontal momentum by syncing air X -> ground X; ftCommon_8007D6A4 does not
      //   clear self_vel.x on landing entry, and Slippi exposes both lanes on the destination
      //   Wait/Landing frame.
      // - Refresh jumps on grounded transition (fp->x1968_jumpsUsed = 0).
      //   refs/melee/src/melee/ft/ftcommon.c:556-573
      const MslCommonParams* c = msl_common_params();
      const uint16_t land_action = blaster_aircatchhit_enters_wait(batch, c, idx)
                                       ? (uint16_t)MSL_ACT_WAIT
                                       : (uint16_t)MSL_ACT_LANDING;
      const float landing_self_vel_x = batch->state.speed_air_x_self[idx];
      batch->state.speed_ground_x_self[idx] = landing_self_vel_x;
      batch->state.speed_air_x_self[idx] = landing_self_vel_x;
      batch->state.fall_fast[idx] = 0;
      batch->state.jumps_left[idx] = ch->max_jumps;

      batch->state.action_id[idx] = land_action;
      batch->state.animation_index[idx] = land_action == (uint16_t)MSL_ACT_WAIT
                                              ? (uint32_t)MSL_SM_WAIT1_0
                                              : (uint32_t)MSL_SM_LANDING;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    }
  }
}
