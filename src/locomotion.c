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
#include "grab_flow.h"
#include "input.h"
#include "move_tables.h"
#include "input_axis.h"
#include "jump_input.h"
#include "special_msids.h"
#include "stage_collision.h"

static inline float msl_signf(float x) { return x < 0.0f ? -1.0f : 1.0f; }

static inline uint16_t walk_action_from_speed(const MslCommonParams* c, const MslCharParams* ch,
                                              float speed_ground_x_self);
static inline uint32_t anim_for_walk_action(uint16_t a);
static inline uint8_t is_dash_flick(const MslCommonParams* c, float stick_x, uint8_t tilt_timer_x);
static inline MslJumpInput jump_input_from_edges(const MslCommonParams* c, uint16_t buttons_pressed,
                                                 float stick_y, uint8_t tilt_timer_y);

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

static inline void enter_squat_immediate(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: ftCo_Squat_Enter does Fighter_ChangeMotionState(ftCo_MS_Squat) then immediately
  // calls ftAnim_8006EBA4, so the first steady post-entry frame is action_frame==1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline void enter_fall_special_from_specialhi(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: SpecialHi end states use ftCo_80096900(..., arg1=1, ...), which enters FallSpecial and
  // sets mv.co.fallspecial.xC=1.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiLanding_Coll,ftFx_SpecialHiFall_Anim
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fallspecial_xc[idx] = 1u;
}

static inline void specialhi_apply_air_launch_ownership(MslBatch* batch, size_t idx,
                                                        const MslCharParams* ch) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Decomp: ftFx_SpecialAirHi_Enter derives launch direction from current stick and
  // ftFox_DatAttrs.{x64,x88}, then overwrites self_vel using x74 launch speed.
  // - stickGetDir(..., 0.0f) is used for x64 magnitude gate.
  // - facing updates when |stick_x| > x88 before atan2f.
  // - rotateModel defaults to HALF_PI32 when below direction threshold.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Enter
  // refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs
  // Source keys: data/characters/{fox,falco}.json
  // - firefox_direction_stick_range_min
  // - firefox_launch_speed
  // - firefox_facing_stick_range_min
  const float stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  const float stick_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  const float abs_x = msl_absf(stick_x);
  const float abs_y = msl_absf(stick_y);

  float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  float launch_angle = 1.5707963705062866f;
  if ((abs_x + abs_y) >= ch->firefox_direction_stick_range_min) {
    if (abs_x > ch->firefox_facing_stick_range_min) {
      batch->state.facing[idx] = (uint8_t)(stick_x >= 0.0f);
      facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    }
    launch_angle = atan2f(stick_y, stick_x * facing_dir);
  }
  batch->state.speed_air_x_self[idx] = facing_dir * (ch->firefox_launch_speed * cosf(launch_angle));
  batch->state.speed_y_self[idx] = ch->firefox_launch_speed * sinf(launch_angle);
}

static inline uint8_t spacie_specialhi_update(MslBatch* batch, size_t idx, uint8_t char_id,
                                              const MslSpecialMsids* ms, uint8_t on_ground) {
  if (batch == NULL || ms == NULL) {
    return 0;
  }
  uint16_t a = batch->state.action_id[idx];
  // Decomp collision wrappers for Hold/HoldAir keep the same logical state when crossing
  // ground/air, preserving current animation frame.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHold_GroundToAir,ftFx_SpecialHiHoldAir_AirToGround}
  if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD && !on_ground) {
    a = (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR;
    batch->state.action_id[idx] = a;
  } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR && on_ground) {
    a = (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD;
    batch->state.action_id[idx] = a;
  }

  switch (a) {
    case MSL_ACT_FX_SPECIAL_HI_HOLD:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_hold;
      if (anim_finished(char_id, ms->specialhi_ground_hold, batch->state.anim_frame_f32[idx])) {
        // Decomp: both Hold and HoldAir transition into launch state strictly on anim end.
        // There is no input-hold gate here (IASA handlers are empty).
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        //   ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,
        //   ftFx_SpecialHiHold_IASA,ftFx_SpecialHiHoldAir_IASA
        // }
        //
        // Decomp launch enter (`ftFx_SpecialAirHi_Enter`) overwrites self velocity from
        // launch parameters/angle; hold-state carry-through velocity is not preserved.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Enter
        //
        // Runtime bridge (narrow): clear hold-air carry velocity on Hold/HoldAir -> launch
        // transition so launch state does not retain stale downward drift. Keep full launch vector
        // derivation TODO-bound to explicit SpecialHi attrs (`x64/x74/x88`) extraction.
        batch->state.action_id[idx] =
            on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI : (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
        if (!on_ground) {
          const MslCharParams* ch = msl_char_params(char_id);
          if (ch != NULL) {
            specialhi_apply_air_launch_ownership(batch, idx, ch);
          } else {
            batch->state.speed_air_x_self[idx] = 0.0f;
            batch->state.speed_y_self[idx] = 0.0f;
          }
        }
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_HOLD_AIR:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_air_hold;
      if (anim_finished(char_id, ms->specialhi_air_hold, batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] =
            on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI : (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
        if (!on_ground) {
          const MslCharParams* ch = msl_char_params(char_id);
          if (ch != NULL) {
            specialhi_apply_air_launch_ownership(batch, idx, ch);
          } else {
            batch->state.speed_air_x_self[idx] = 0.0f;
            batch->state.speed_y_self[idx] = 0.0f;
          }
        }
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      // Decomp: SpecialHi ground collision can transition into SpecialAirHi while preserving frame.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHi_GroundToAir
      if (!on_ground) {
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
        a = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
      }
      // Decomp: launch phase decrements travelFrames and transitions into end states when it expires.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHi_Anim,ftFx_SpecialAirHi_Anim}
      //
      // Seed-bridge approximation:
      // Slippi post-frames do not expose mv.fx.SpecialHi.travelFrames directly. Use the launch
      // submotion end frame as the transition gate so action-time progression remains data-driven
      // (replace with explicit travelFrames seed when available).
      if (anim_finished(char_id, ms->specialhi_ground_main, batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] =
            on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING
                      : (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL;
        batch->state.animation_index[idx] =
            on_ground ? (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING
                      : (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      // Decomp: SpecialHi and SpecialAirHi share ftFx_SM_SpecialHi (same launch submotion id).
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      if (anim_finished(char_id, ms->specialhi_ground_main, batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] =
            on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING
                      : (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL;
        batch->state.animation_index[idx] =
            on_ground ? (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING
                      : (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_LANDING:
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING;
      // Decomp: ftFx_SpecialHiLanding_Coll falls back to FallSpecial when the state becomes airborne.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Coll
      if (!on_ground) {
        enter_fall_special_from_specialhi(batch, idx);
        return 1;
      }
      // Decomp: ftFx_SpecialHiLanding_Anim transitions to Wait on anim end.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
      if (anim_finished(char_id, (uint16_t)MSL_SM_FX_SPECIAL_HI_LANDING,
                        batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_FALL:
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
      // Decomp: ftFx_SpecialHiFall_Anim transitions to FallSpecial on anim end.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Anim
      if (anim_finished(char_id, (uint16_t)MSL_SM_FX_SPECIAL_HI_FALL,
                        batch->state.anim_frame_f32[idx])) {
        enter_fall_special_from_specialhi(batch, idx);
      }
      return 1;
    default:
      return 0;
  }
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

static inline uint32_t attackair_submotion_from_action(uint16_t a) {
  // Decomp: AttackAir motion states map to `ftCo_Submotion` AttackAir* animations.
  // Source of truth: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
      return (uint32_t)MSL_SM_ATTACK_AIR_N;
    case MSL_ACT_ATTACK_AIR_F:
      return (uint32_t)MSL_SM_ATTACK_AIR_F;
    case MSL_ACT_ATTACK_AIR_B:
      return (uint32_t)MSL_SM_ATTACK_AIR_B;
    case MSL_ACT_ATTACK_AIR_HI:
      return (uint32_t)MSL_SM_ATTACK_AIR_HI;
    case MSL_ACT_ATTACK_AIR_LW:
      return (uint32_t)MSL_SM_ATTACK_AIR_LW;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint8_t landing_contact_y_bridge_matches_source(uint16_t source_act,
                                                              uint16_t source_action_frame,
                                                              uint16_t prev_source_act,
                                                              uint16_t land_act) {
  const uint8_t landing_basic_prev =
      (prev_source_act == (uint16_t)MSL_ACT_JUMP_F || prev_source_act == (uint16_t)MSL_ACT_JUMP_B ||
       prev_source_act == (uint16_t)MSL_ACT_FALL ||
       prev_source_act == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
       prev_source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
       prev_source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
       prev_source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END)
          ? 1u
          : 0u;

  if (source_act == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_LW) {
    return 1u;
  }
  if (source_act == (uint16_t)MSL_ACT_ATTACK_AIR_F) {
    if (land_act == (uint16_t)MSL_ACT_LANDING) {
      return 1u;
    }
    if (land_act == (uint16_t)MSL_ACT_LANDING_AIR_F && source_action_frame >= 16u) {
      // Narrow runtime slice: late-window AttackAirF -> LandingAirF rows only.
      // Frame gate tie-down (ISO-extracted script events):
      // - data/moves/fox.json   moves["ftCo_SM_AttackAirF"]["events"] contains create_hitbox at frame 16.
      // - data/moves/falco.json moves["ftCo_SM_AttackAirF"]["events"] contains create_hitbox at frame 16.
      return 1u;
    }
  }

  // Additional ft_80082B1C -> Landing_Enter_Basic callback family:
  // - Fall collision callback path (Fall -> Landing), including lanes that have already entered
  //   Landing before this resolver (source_act==Landing with prev_source_act==Fall).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // - Jump ground-collision callback path (JumpF/B -> Landing).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
  // - Aerial jump collision callback path (JumpAerialB -> Landing in observed suite rows).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
  // - Fox/Falco Blaster aerial collision path via AirCatchHit (SpecialAirN* -> Landing).
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //     ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll
  //   }
  //   refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  if (land_act == (uint16_t)MSL_ACT_LANDING &&
      (source_act == (uint16_t)MSL_ACT_FALL || source_act == (uint16_t)MSL_ACT_JUMP_F ||
       source_act == (uint16_t)MSL_ACT_JUMP_B ||
       source_act == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END ||
       ((source_act == (uint16_t)MSL_ACT_LANDING) && landing_basic_prev))) {
    return 1u;
  }

  return 0u;
}

static inline uint8_t landing_contact_is_ledge_floor(const MslBatch* batch, size_t idx,
                                                     size_t bi) {
  if (batch == NULL || !batch->state.on_ground[idx]) {
    return 0u;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  return g->lines[(size_t)line_idx].is_ledge ? 1u : 0u;
}

static inline uint8_t attackair_cstick_edge(const MslCommonParams* c, int8_t prev_cx,
                                            int8_t prev_cy, int8_t cx, int8_t cy) {
  // Decomp: ftCo_800DF478 is a C-stick *edge* (threshold crossing) helper used by AttackAir input
  // checks. It compares current vs prior C-stick against p_ftCommonData->xDC/xE0.
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF478
  if (c == NULL) {
    return 0;
  }
  const float prev_x = stick_i8_to_unit(prev_cx);
  const float prev_y = stick_i8_to_unit(prev_cy);
  const float x = stick_i8_to_unit(cx);
  const float y = stick_i8_to_unit(cy);
  if ((fabsf(prev_x) < c->attackair_stick_deadzone_x &&
       fabsf(x) >= c->attackair_stick_deadzone_x) ||
      (fabsf(prev_y) < c->attackair_stick_deadzone_y &&
       fabsf(y) >= c->attackair_stick_deadzone_y)) {
    return 1;
  }
  return 0;
}

static inline uint16_t attackair_action_from_stick(const MslCommonParams* c, float stick_x,
                                                   float stick_y, float facing_dir) {
  // Decomp: ftCo_AttackAir_GetMsidFromCStick chooses AttackAirN vs directional attacks based on
  // (xDC/xE0) deadzones and the stick angle threshold (x20 radians).
  // This is a common ftCo_* path (not Fox/Falco-specific).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_GetMsidFromCStick
  if (c == NULL) {
    return (uint16_t)MSL_ACT_ATTACK_AIR_N;
  }
  if (fabsf(stick_x) < c->attackair_stick_deadzone_x &&
      fabsf(stick_y) < c->attackair_stick_deadzone_y) {
    return (uint16_t)MSL_ACT_ATTACK_AIR_N;
  }
  // Decomp angle helper uses ABS(stick.x):
  // - ftCo_AttackAir_GetMsidFromCStick computes `stick_angle` via ftCo_Get{L,C}StickAngle.
  // - ftCo_Get{L,C}StickAngle is `atan2(stick.y, ABS(stick.x))`.
  // This shape is shared by common ftCo_* stick-angle gates.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCo_GetLStickAngle,ftCo_GetCStickAngle}
  const float ang = atan2f(stick_y, msl_absf(stick_x));
  if (ang > c->attack_angle_threshold_radians) {
    return (uint16_t)MSL_ACT_ATTACK_AIR_HI;
  }
  if (ang < -c->attack_angle_threshold_radians) {
    return (uint16_t)MSL_ACT_ATTACK_AIR_LW;
  }
  return (stick_x * facing_dir) >= 0.0f ? (uint16_t)MSL_ACT_ATTACK_AIR_F
                                        : (uint16_t)MSL_ACT_ATTACK_AIR_B;
}

static inline uint8_t attackair_try_enter_from_air_locomotion(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              size_t idx) {
  // Decomp: ftCo_AttackAir_CheckInput enters AttackAir when either:
  // - A is pressed-edge this frame, or
  // - the C-stick crosses its directional threshold this frame (ftCo_800DF478).
  // It then enters via Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall, ...) and immediately runs
  // ftAnim_8006EBA4 on the new motion state.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint16_t a0 = batch->state.action_id[idx];
  if (!msl_action_is_air_locomotion(a0)) {
    return 0;
  }
  // Special fall should not be interruptible into aerial attacks.
  if (a0 == (uint16_t)MSL_ACT_FALL_SPECIAL || a0 == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
      a0 == (uint16_t)MSL_ACT_FALL_SPECIAL_B) {
    return 0;
  }

  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t c_edge =
      attackair_cstick_edge(c, batch->state.prev_input_c_x[idx], batch->state.prev_input_c_y[idx],
                            batch->state.input_c_x[idx], batch->state.input_c_y[idx]);
  if ((pressed & (uint16_t)MSL_BUTTON_A) == 0 && !c_edge) {
    return 0;
  }

  float stick_x = 0.0f;
  float stick_y = 0.0f;
  if (c_edge) {
    stick_x = stick_i8_to_unit(batch->state.input_c_x[idx]);
    stick_y = stick_i8_to_unit(batch->state.input_c_y[idx]);
  } else {
    stick_x =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
    stick_y =
        apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  }
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

  const uint16_t act = attackair_action_from_stick(c, stick_x, stick_y, facing_dir);
  const uint32_t smid = attackair_submotion_from_action(act);
  if (smid == 0xFFFFFFFFu) {
    return 0;
  }

  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = smid;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
  msl_anim_timebase_defer_tick_once(batch, idx);
  return 1;
}

static inline uint8_t cstick_side_smash_edge(const MslCommonParams* c, const MslBatch* batch,
                                             size_t idx) {
  if (c == NULL || batch == NULL) {
    return 0u;
  }
  // C-stick side-smash edge helper.
  // Decomp: refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF1C8
  const float prev_x = stick_i8_to_unit(batch->state.prev_input_c_x[idx]);
  const float cur_x = stick_i8_to_unit(batch->state.input_c_x[idx]);
  return (msl_absf(prev_x) < c->cstick_smash_threshold && msl_absf(cur_x) >= c->cstick_smash_threshold)
             ? 1u
             : 0u;
}

static inline uint8_t cstick_up_smash_edge(const MslCommonParams* c, const MslBatch* batch,
                                           size_t idx) {
  if (c == NULL || batch == NULL) {
    return 0u;
  }
  // C-stick up-smash edge helper.
  // Decomp: refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF2D8
  const float prev_y = stick_i8_to_unit(batch->state.prev_input_c_y[idx]);
  const float cur_y = stick_i8_to_unit(batch->state.input_c_y[idx]);
  return (prev_y < c->attack_hi4_stick_threshold_y && cur_y >= c->attack_hi4_stick_threshold_y)
             ? 1u
             : 0u;
}

static inline uint8_t cstick_down_smash_edge(const MslCommonParams* c, const MslBatch* batch,
                                             size_t idx) {
  if (c == NULL || batch == NULL) {
    return 0u;
  }
  // C-stick down-smash edge helper.
  // Decomp: refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF3A8
  const float prev_y = stick_i8_to_unit(batch->state.prev_input_c_y[idx]);
  const float cur_y = stick_i8_to_unit(batch->state.input_c_y[idx]);
  return (prev_y > c->attack_lw4_stick_threshold_y && cur_y <= c->attack_lw4_stick_threshold_y)
             ? 1u
             : 0u;
}

static inline uint16_t grounded_a_attack_select_action(const MslCommonParams* c,
                                                       uint16_t buttons_pressed,
                                                       uint8_t cstick_side_edge,
                                                       uint8_t cstick_up_edge,
                                                       uint8_t cstick_down_edge, float stick_x,
                                                       float stick_y, uint8_t tilt_timer_x,
                                                       uint8_t tilt_timer_y, float facing_dir,
                                                       uint8_t allow_attack_dash,
                                                       uint8_t allow_tilts) {
  if (c == NULL) {
    return 0xFFFFu;
  }
  // Common grounded A-attack input selection from ftCo_* IASA chains:
  // - Dash/Run/RunDirect reach AttackDash via ftCo_AttackDash_CheckInput.
  // - Wait/Walk/Turn/Squat* reach smashes/tilts/jab via Attack{S4,Hi4,Lw4,S3,Hi3,Lw3,1} checks.
  // - C-stick smash edges route through ft_0DF1 helpers.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Dash.c,ftCo_Run.c,ftCo_RunDirect.c,ftCo_Wait.c,ftCo_Walk.c,ftCo_Turn.c}
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
  // refs/melee/src/melee/ft/ft_0DF1.c::{ftCo_800DF1C8,ftCo_800DF2D8,ftCo_800DF3A8}
  const uint8_t a_pressed = ((buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u) ? 1u : 0u;
  if (allow_attack_dash && a_pressed) {
    return (uint16_t)MSL_ACT_ATTACK_DASH;
  }
  if (!allow_tilts) {
    return 0xFFFFu;
  }

  if (cstick_side_edge || (a_pressed && msl_absf(stick_x) >= c->dash_flick_abs &&
                           tilt_timer_x < c->dash_flick_tilt_max_frames)) {
    return (uint16_t)MSL_ACT_ATTACK_S4_S;
  }
  if (cstick_up_edge || (a_pressed && stick_y >= c->attack_hi4_stick_threshold_y &&
                         tilt_timer_y < c->attack_hi4_tilt_max_frames)) {
    return (uint16_t)MSL_ACT_ATTACK_HI4;
  }
  if (cstick_down_edge || (a_pressed && stick_y <= c->attack_lw4_stick_threshold_y &&
                           tilt_timer_y < c->attack_lw4_tilt_max_frames)) {
    return (uint16_t)MSL_ACT_ATTACK_LW4;
  }
  if (!a_pressed) {
    return 0xFFFFu;
  }

  const float ang = atan2f(stick_y, msl_absf(stick_x));
  const float stick_f = stick_x * facing_dir;
  if (stick_f >= c->attack_s3_stick_threshold_x &&
      msl_absf(ang) < c->attack_angle_threshold_radians) {
    return (uint16_t)MSL_ACT_ATTACK_S3;
  }
  if (stick_y >= c->attack_hi3_stick_threshold_y && ang > c->attack_angle_threshold_radians) {
    return (uint16_t)MSL_ACT_ATTACK_HI3;
  }
  if (stick_y <= c->attack_lw3_stick_threshold_y && ang < -c->attack_angle_threshold_radians) {
    return (uint16_t)MSL_ACT_ATTACK_LW3;
  }
  return (uint16_t)MSL_ACT_ATTACK_11;
}

static inline uint32_t grounded_attack_submotion_from_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_11:
      return (uint32_t)MSL_SM_ATTACK_11;
    case MSL_ACT_ATTACK_12:
      return (uint32_t)MSL_SM_ATTACK_12;
    case MSL_ACT_ATTACK_13:
      return (uint32_t)MSL_SM_ATTACK_13;
    case MSL_ACT_ATTACK_DASH:
      return (uint32_t)MSL_SM_ATTACK_DASH;
    case MSL_ACT_ATTACK_S3_HI:
      return (uint32_t)MSL_SM_ATTACK_S3_HI;
    case MSL_ACT_ATTACK_S3_HI_S:
      return (uint32_t)MSL_SM_ATTACK_S3_HI_S;
    case MSL_ACT_ATTACK_S3_S:
      return (uint32_t)MSL_SM_ATTACK_S3;
    case MSL_ACT_ATTACK_S3_LW_S:
      return (uint32_t)MSL_SM_ATTACK_S3_LW_S;
    case MSL_ACT_ATTACK_S3_LW:
      return (uint32_t)MSL_SM_ATTACK_S3_LW;
    case MSL_ACT_ATTACK_HI3:
      return (uint32_t)MSL_SM_ATTACK_HI3;
    case MSL_ACT_ATTACK_LW3:
      return (uint32_t)MSL_SM_ATTACK_LW3;
    case MSL_ACT_ATTACK_S4_HI:
      return (uint32_t)MSL_SM_ATTACK_S4_HI;
    case MSL_ACT_ATTACK_S4_HI_S:
      return (uint32_t)MSL_SM_ATTACK_S4_HI_S;
    case MSL_ACT_ATTACK_S4_S:
      return (uint32_t)MSL_SM_ATTACK_S4;
    case MSL_ACT_ATTACK_S4_LW_S:
      return (uint32_t)MSL_SM_ATTACK_S4_LW_S;
    case MSL_ACT_ATTACK_S4_LW:
      return (uint32_t)MSL_SM_ATTACK_S4_LW;
    case MSL_ACT_ATTACK_HI4:
      return (uint32_t)MSL_SM_ATTACK_HI4;
    case MSL_ACT_ATTACK_LW4:
      return (uint32_t)MSL_SM_ATTACK_LW4;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline uint8_t action_is_attack_s3_family(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t grounded_a_attack_try_enter_from_iasa(MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx,
                                                            uint16_t buttons_pressed,
                                                            float stick_x, float stick_y,
                                                            uint8_t tilt_timer_x,
                                                            uint8_t tilt_timer_y,
                                                            float facing_dir,
                                                            uint8_t allow_attack_dash,
                                                            uint8_t allow_tilts) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint8_t c_side_edge = cstick_side_smash_edge(c, batch, idx);
  const uint8_t c_up_edge = cstick_up_smash_edge(c, batch, idx);
  const uint8_t c_down_edge = cstick_down_smash_edge(c, batch, idx);
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_A) == 0 && c_side_edge == 0u && c_up_edge == 0u &&
      c_down_edge == 0u) {
    return 0;
  }

  const uint16_t act =
      grounded_a_attack_select_action(c, buttons_pressed, c_side_edge, c_up_edge, c_down_edge,
                                      stick_x, stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                      allow_attack_dash, allow_tilts);
  const uint32_t sm = grounded_attack_submotion_from_action(act);
  if (sm == 0xFFFFFFFFu) {
    return 0;
  }

  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = sm;
  // Decomp attack enters call Fighter_ChangeMotionState(..., anim_start=0, anim_speed=1) then
  // ftAnim_8006EBA4. This slice keeps the common enter timebase call and state-local update logic.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
  return 1;
}

static inline uint8_t grounded_attack_update(MslBatch* batch, size_t idx, uint8_t char_id) {
  if (batch == NULL) {
    return 0;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  const uint32_t forced_sm = grounded_attack_submotion_from_action(action_id);
  if (forced_sm == 0xFFFFFFFFu) {
    return 0;
  }

  // Keep attack submotion stable while in the motion state.
  // refs/melee/src/melee/ft/ftmotionstates.c (Attack* motion-state table rows)
  uint32_t sm = forced_sm;
  batch->state.animation_index[idx] = sm;
  if (sm == 0xFFFFFFFFu || sm > 0xFFFFu) {
    return 1;
  }

  if (anim_finished(char_id, (uint16_t)sm, batch->state.anim_frame_f32[idx])) {
    // Decomp grounded attack anim callbacks resolve to Wait on animation end.
    // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  }
  return 1;
}

static inline uint8_t grounded_attack_try_iasa_subset(MslBatch* batch, const MslCommonParams* c,
                                                      const MslCharParams* ch, size_t idx,
                                                      uint16_t buttons, uint16_t buttons_pressed,
                                                      float stick_x, float stick_y,
                                                      uint8_t tilt_timer_x, uint8_t tilt_timer_y,
                                                      float facing_dir) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }

  // Decomp shape:
  // - Grounded Attack* input callbacks gate on fp->allow_interrupt.
  // - Most of those callbacks then delegate into ftCo_Wait_IASA (or an equivalent superset path).
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  //
  // Current sim scope: grounded locomotion subset (attacks/jump/squat/dash/turn/walk) only.
  if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                            tilt_timer_x, tilt_timer_y, facing_dir, 0, 1)) {
    return 1u;
  }

  const MslJumpInput j_in = jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
  if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
    batch->state.kneebend_is_short_hop[idx] = 0;
    return 1u;
  }

  if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0u && (buttons & (uint16_t)MSL_BUTTON_B) == 0u &&
      stick_y < -c->crouch_stick_threshold) {
    enter_squat_immediate(batch, idx);
    return 1u;
  }

  if (is_dash_flick(c, stick_x, tilt_timer_x)) {
    if ((stick_x * facing_dir) < 0.0f) {
      batch->state.turn_has_turned[idx] = 0;
      batch->state.turn_frames_to_turn[idx] = 0;
      // Decomp: ftCo_Turn_Enter_Smash sets mv.co.turn.x8 = facing_dir.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
      batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      msl_anim_timebase_tick_once(batch, idx);
      return 1u;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
    batch->state.dash_x4[idx] = 1u;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    batch->state.tilt_timer_x[idx] = 0xFEu;
    return 1u;
  }

  if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
    batch->state.turn_has_turned[idx] = 0;
    batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
    batch->state.turn_x8[idx] = 0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    return 1u;
  }

  if (msl_absf(stick_x) >= c->walk_stick_threshold) {
    const uint16_t want = walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
    batch->state.action_id[idx] = want;
    batch->state.animation_index[idx] = anim_for_walk_action(want);
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    return 1u;
  }

  return 0u;
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
      a == MSL_ACT_SQUAT || a == MSL_ACT_SQUAT_WAIT || a == MSL_ACT_SQUAT_RV ||
      a == MSL_ACT_LANDING ||
      a == MSL_ACT_LANDING_FALL_SPECIAL || a == MSL_ACT_LANDING_AIR_N ||
      a == MSL_ACT_LANDING_AIR_F || a == MSL_ACT_LANDING_AIR_B || a == MSL_ACT_LANDING_AIR_HI ||
      a == MSL_ACT_LANDING_AIR_LW || a == MSL_ACT_ESCAPE_F || a == MSL_ACT_ESCAPE_B ||
      a == MSL_ACT_ESCAPE_N || a == MSL_ACT_ATTACK_11 || a == MSL_ACT_ATTACK_12 ||
      a == MSL_ACT_ATTACK_13 || a == MSL_ACT_ATTACK_DASH || action_is_attack_s3_family(a) ||
      a == MSL_ACT_ATTACK_HI3 || a == MSL_ACT_ATTACK_LW3 || a == MSL_ACT_ATTACK_S4_HI ||
      a == MSL_ACT_ATTACK_S4_HI_S || a == MSL_ACT_ATTACK_S4_S ||
      a == MSL_ACT_ATTACK_S4_LW_S || a == MSL_ACT_ATTACK_S4_LW || a == MSL_ACT_ATTACK_HI4 ||
      a == MSL_ACT_ATTACK_LW4) {
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

static inline uint8_t kneebend_try_enter_attack_hi4_from_iasa(MslBatch* batch,
                                                               const MslCommonParams* c, size_t idx,
                                                               uint16_t buttons_pressed,
                                                               float stick_y) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  // KneeBend IASA supports AttackHi4 interrupt (NoD0 variant: no y-tilt timer gate) via:
  // - A press + lstick.y >= xCC, or
  // - c-stick up smash edge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_CheckInputNoD0
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF2D8
  const uint8_t a_up_smash =
      ((buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u && stick_y >= c->attack_hi4_stick_threshold_y)
          ? 1u
          : 0u;
  if (!a_up_smash && !cstick_up_smash_edge(c, batch, idx)) {
    return 0u;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_HI4;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_HI4;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
  return 1u;
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
    case MSL_ACT_SQUAT:
      return (uint32_t)MSL_SM_SQUAT;
    case MSL_ACT_SQUAT_WAIT:
      return (uint32_t)MSL_SM_SQUAT_WAIT;
    case MSL_ACT_SQUAT_RV:
      return (uint32_t)MSL_SM_SQUAT_RV;
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
    case MSL_ACT_FX_SPECIAL_HI_LANDING:
      return (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING;
    case MSL_ACT_FX_SPECIAL_HI_FALL:
      return (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
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
    case MSL_ACT_ATTACK_DASH:
      return (uint32_t)MSL_SM_ATTACK_DASH;
    case MSL_ACT_ATTACK_S3_HI:
      return (uint32_t)MSL_SM_ATTACK_S3_HI;
    case MSL_ACT_ATTACK_S3_HI_S:
      return (uint32_t)MSL_SM_ATTACK_S3_HI_S;
    case MSL_ACT_ATTACK_S3_S:
      return (uint32_t)MSL_SM_ATTACK_S3;
    case MSL_ACT_ATTACK_S3_LW_S:
      return (uint32_t)MSL_SM_ATTACK_S3_LW_S;
    case MSL_ACT_ATTACK_S3_LW:
      return (uint32_t)MSL_SM_ATTACK_S3_LW;
    case MSL_ACT_ATTACK_HI3:
      return (uint32_t)MSL_SM_ATTACK_HI3;
    case MSL_ACT_ATTACK_LW3:
      return (uint32_t)MSL_SM_ATTACK_LW3;
    case MSL_ACT_ATTACK_S4_HI:
      return (uint32_t)MSL_SM_ATTACK_S4_HI;
    case MSL_ACT_ATTACK_S4_HI_S:
      return (uint32_t)MSL_SM_ATTACK_S4_HI_S;
    case MSL_ACT_ATTACK_S4_S:
      return (uint32_t)MSL_SM_ATTACK_S4;
    case MSL_ACT_ATTACK_S4_LW_S:
      return (uint32_t)MSL_SM_ATTACK_S4_LW_S;
    case MSL_ACT_ATTACK_S4_LW:
      return (uint32_t)MSL_SM_ATTACK_S4_LW;
    case MSL_ACT_ATTACK_HI4:
      return (uint32_t)MSL_SM_ATTACK_HI4;
    case MSL_ACT_ATTACK_LW4:
      return (uint32_t)MSL_SM_ATTACK_LW4;
    default:
      return 0xFFFFFFFFu;
  }
}

static inline void enter_landing_action_from_air(MslBatch* batch, const MslCharParams* ch,
                                                 size_t idx, size_t bi, uint16_t source_act,
                                                 uint16_t land_act) {
  if (batch == NULL || ch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();

  // Landed this frame.
  // Decomp grounding keeps self_vel.x and gr_vel aligned on ground entry:
  // - ftCommon_8007D6A4 sets `fp->gr_vel = fp->self_vel.x` (does not zero self_vel.x).
  //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
  // - Ground update keeps `fp->self_vel.x` synced from `fp->gr_vel` each frame.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // Keep both seed/output lanes synchronized at landing entry:
  // - speed_ground_x_self <-> fp->gr_vel
  // - speed_air_x_self <-> fp->self_vel.x
  const float landing_self_vel_x = batch->state.speed_air_x_self[idx];
  batch->state.speed_ground_x_self[idx] = landing_self_vel_x;
  batch->state.speed_air_x_self[idx] = landing_self_vel_x;
  // AttackAir landing-entry compatibility bridge:
  // - AttackAir_Coll resolves floor contact, then transitions through
  //   ftCo_LandingAir_EnterWithLag / ftCo_Landing_Enter_Basic.
  // - Keep root Y aligned to the collision floor contact point for this narrow callback family.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  //
  // TODO(decomp-coll-lane): remove once mpColl ECB interpolation/ownership is fully modeled and
  // AttackAir landing-entry rows no longer require contact-y reconciliation.
  uint8_t apply_contact_y_bridge =
      (batch->state.on_ground[idx] &&
       landing_contact_y_bridge_matches_source(source_act, batch->state.action_frame[idx],
                                               batch->state.prev_action_id[idx], land_act))
          ? 1u
          : 0u;
  if (apply_contact_y_bridge && source_act == (uint16_t)MSL_ACT_FALL &&
      landing_contact_is_ledge_floor(batch, idx, bi)) {
    // Keep edge/walk-off positioning owned by mpColl on ledge floor segments.
    // Decomp shape:
    // - Fall collision callback routes through ft_80082B1C.
    // - mpColl floor-edge snap/ownership is handled in mpColl_8004A45C_Floor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
    apply_contact_y_bridge = 0u;
  }
  if (apply_contact_y_bridge) {
    batch->state.pos_y[idx] = batch->state.ground_contact_y[idx];
  }

  batch->state.fall_fast[idx] = 0;
  // Decomp: grounding transitions clear ECB lock via ftCommon_UnlockECB.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D6A4,ftCommon_UnlockECB}
  batch->state.ecb_lock_timer[idx] = 0u;

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

      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];
      const MslSpecialMsids* ms = msl_special_msids(cid);

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
        if (action_id != MSL_ACT_TURN) {
          batch->state.turn_has_turned[idx] = 0;
          batch->state.turn_frames_to_turn[idx] = 0;
          batch->state.turn_x8[idx] = 0;
        }
        if (action_id != MSL_ACT_RUN && action_id != MSL_ACT_RUN_DIRECT) {
          batch->state.run_x0[idx] = 0;
        }

        // Squat/SquatWait/SquatRv updates.
        //
        // Decomp:
        // - ftCo_Squat_Anim transitions on anim end:
        //   - hold down -> ftCo_SquatWait_Enter
        //   - else -> ft_8008A2BC (Wait enter)
        // - ftCo_SquatWait_CheckInput / IASA keeps SquatWait while (lstick.y < -x90),
        //   and enters SquatRv when (lstick.y > -x94).
        // - ftCo_SquatRv_Anim exits to Wait on anim end.
        // Squat threshold source:
        // - c->crouch_stick_threshold is p_ftCommonData->x90 loaded from
        //   data/common/ft_common_data.json via src/common_params.c.
        // - c->crouch_release_stick_threshold is p_ftCommonData->x94 loaded from
        //   data/common/ft_common_data.json via src/common_params.c.
        // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Squat.c,ftCo_SquatWait.c,ftCo_SquatRv.c}
        if (action_id == MSL_ACT_SQUAT) {
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT;
          if (anim_finished(cid, (uint16_t)MSL_SM_SQUAT, batch->state.anim_frame_f32[idx])) {
            if (stick_y < -c->crouch_stick_threshold) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_WAIT;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              action_id = (uint16_t)MSL_ACT_SQUAT_WAIT;
            } else {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              action_id = (uint16_t)MSL_ACT_WAIT;
            }
          }
        }
        if (action_id == MSL_ACT_SQUAT_WAIT) {
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_WAIT;
        }
        if (action_id == MSL_ACT_SQUAT_RV) {
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_RV;
          if (anim_finished(cid, (uint16_t)MSL_SM_SQUAT_RV, batch->state.anim_frame_f32[idx])) {
            // Decomp: ftCo_SquatRv_Anim -> ft_8008A2BC (Wait enter).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_Anim
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_WAIT;
          }
        }

        // Squat/SquatWait/SquatRv IASA subset.
        //
        // Decomp:
        // - Squat_IASA checks attacks, guard, jump.
        // - SquatWait_IASA checks attacks/guard/jump, then Dash, then SquatRv.
        // - SquatRv_IASA checks attacks/guard/jump and Walk (no Dash check).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
        if (action_id == MSL_ACT_SQUAT || action_id == MSL_ACT_SQUAT_WAIT ||
            action_id == MSL_ACT_SQUAT_RV) {
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y,
                                                    facing_dir, 0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
            const MslJumpInput j_in =
                jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
            if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
              batch->state.kneebend_is_short_hop[idx] = 0;
              action_id = (uint16_t)MSL_ACT_KNEE_BEND;
            } else if (action_id == MSL_ACT_SQUAT_WAIT && is_dash_flick(c, stick_x, tilt_timer_x)) {
              // Decomp Dash_CheckInput path from SquatWait IASA.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
              if ((stick_x * facing_dir) < 0.0f) {
                batch->state.turn_has_turned[idx] = 0;
                batch->state.turn_frames_to_turn[idx] = 0;
                batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                msl_anim_timebase_tick_once(batch, idx);
                action_id = (uint16_t)MSL_ACT_TURN;
              } else {
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                batch->state.dash_x4[idx] = 1u;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                msl_anim_timebase_tick_once(batch, idx);
                batch->state.tilt_timer_x[idx] = 0xFEu;
                action_id = (uint16_t)MSL_ACT_DASH;
              }
            } else if (action_id == MSL_ACT_SQUAT_WAIT &&
                       stick_y > -c->crouch_release_stick_threshold) {
              // Decomp: ftCo_SquatWait_IASA performs SquatRv_CheckInput after Dash_CheckInput.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_CheckInput
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT_RV;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_RV;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              action_id = (uint16_t)MSL_ACT_SQUAT_RV;
            } else if (action_id == MSL_ACT_SQUAT_RV &&
                       (stick_x * facing_dir) >= c->walk_stick_threshold) {
              // Decomp: ftCo_SquatRv_IASA delegates to ftCo_Walk_CheckInput.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
              // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFC70
              const uint16_t want =
                  walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
              batch->state.action_id[idx] = want;
              batch->state.animation_index[idx] = anim_for_walk_action(want);
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              msl_anim_timebase_tick_once(batch, idx);
              action_id = want;
            }
          }
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

        if (spacie_specialhi_update(batch, idx, cid, ms, 1u)) {
          continue;
        }

        // Fox/Falco side special (Illusion/Phantasm): keep animation_index stable and model
        // Anim-end transitions before Phys, matching Fighter_procUpdate callback ordering.
        //
        // Decomp:
        // - ftFx_SpecialS_Anim transitions to ftFx_SpecialSEnd_Enter when frames are exhausted.
        //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialS_Anim,ftFx_SpecialSEnd_Enter}
        // - Fighter_procUpdate order (Anim before Phys):
        //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        if (ms != NULL) {
          // Keep animation_index stable for side special states (avoid seed carry-through).
          switch (action_id) {
            case MSL_ACT_FX_SPECIAL_S_START:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
              break;
            case MSL_ACT_FX_SPECIAL_S:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_main;
              break;
            case MSL_ACT_FX_SPECIAL_S_END:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_end;
              break;
            default:
              break;
          }

          // Start -> Main on anim completion.
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S_START &&
              anim_finished(cid, ms->specials_ground_start, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_main;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_S;
          }

          // Main -> End on anim completion (Anim callback).
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S &&
              anim_finished(cid, ms->specials_ground_main, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_END;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_end;
            // Decomp: ftFx_SpecialSEnd_Enter sets fp->gr_vel = da->x34 * facing_dir.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Enter
            batch->state.speed_ground_x_self[idx] = ch->illusion_ground_end_vel_x * facing_dir;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_S_END;
          }

          // Main -> End on pressed-edge B (IASA callback).
          //
          // Decomp: ftFx_SpecialS_IASA checks `fp->input.x668 & HSD_PAD_B` (pressed-edge B) and
          // enters SpecialSEnd when B is pressed during the dash portion.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_IASA
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S &&
              (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_END;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_end;
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Enter
            batch->state.speed_ground_x_self[idx] = ch->illusion_ground_end_vel_x * facing_dir;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_S_END;
          }

          // End -> Wait on anim completion.
          // Decomp: ftFx_SpecialSEnd_Anim calls ft_8008A2BC when frames are exhausted.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Anim
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S_END &&
              anim_finished(cid, ms->specials_ground_end, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_WAIT;
          }
        }

        // TurnRun -> Run/Wait on anim completion (Anim step).
        //
        // Decomp:
        // - TurnRun_Anim enters Run via fn_800CA644 when the animation ends, else goes to Wait.
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
        if (action_id == MSL_ACT_TURN_RUN &&
            anim_finished(cid, (uint16_t)MSL_SM_TURN_RUN, batch->state.anim_frame_f32[idx])) {
          if ((stick_x * facing_dir) >= c->run_stick_x_threshold) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_RUN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_RUN;
            // fn_800CA644 passes p_ftCommonData->x430 into ftCo_Run_Enter (mv.co.run.x0 init).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Enter_Full
            {
              int init = (int)c->run_x0_init_x430;
              if (init < 0) {
                init = 0;
              } else if (init > 255) {
                init = 255;
              }
              batch->state.run_x0[idx] = (uint8_t)init;
            }
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_RUN;
          } else {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            batch->state.run_x0[idx] = 0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_WAIT;
          }
        }

        // Turn: decomp `frames_to_turn` countdown + flip on 0 (Anim step).
        // Only tick if Turn was already active at frame start (avoid flip on same-frame entry).
        if (action_id_start == MSL_ACT_TURN) {
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 (ftCo_Turn_Anim_Inner)
          if (batch->state.turn_frames_to_turn[idx] > 0) {
            batch->state.turn_frames_to_turn[idx]--;
          } else if (!batch->state.turn_has_turned[idx]) {
            batch->state.turn_has_turned[idx] = 1;
            turn_just_turned = 1;
            batch->state.facing[idx] = batch->state.facing[idx] ? 0 : 1;
            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          }
          // Turn_Anim exits to Wait on animation end.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
          if (action_id == MSL_ACT_TURN &&
              anim_finished(cid, (uint16_t)MSL_SM_TURN, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_WAIT;
          }
        }

        // Run: decomp `mv.co.run.x0` countdown in Run_Anim.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
        if (action_id_start == MSL_ACT_RUN || action_id_start == MSL_ACT_RUN_DIRECT) {
          if (batch->state.run_x0[idx] > 0) {
            batch->state.run_x0[idx]--;
          }
        }

        // Grounded attack state updates (Anim before IASA in Fighter_procUpdate).
        //
        // Decomp:
        // - Grounded Attack* motion states run *_Anim before *_IASA each frame.
        // - *_Anim resolves to Wait when the attack animation finishes.
        // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
        if (grounded_attack_update(batch, idx, cid)) {
          action_id = batch->state.action_id[idx];
          if (grounded_attack_submotion_from_action(action_id) != 0xFFFFFFFFu) {
            // AttackDash IASA has an extra pre-gate (`ftCo_800D8AE0`) before Wait_IASA delegation.
            // Keep AttackDash on the existing path until that gate is modeled explicitly.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
            // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
            if (action_id == (uint16_t)MSL_ACT_ATTACK_DASH) {
              continue;
            }

            const uint8_t allow_interrupt = move_tables_grounded_attack_allow_interrupt(
                cid, action_id, batch->state.anim_frame_f32[idx]);

            // Decomp: grounded Attack* IASA gates on fp->allow_interrupt before delegating to
            // grounded interrupt checks (typically Wait IASA path).
            // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
            if (allow_interrupt &&
                grounded_attack_try_iasa_subset(batch, c, ch, idx, buttons, buttons_pressed, stick_x,
                                                stick_y, tilt_timer_x, tilt_timer_y, facing_dir)) {
              action_id = batch->state.action_id[idx];
            }

            if (grounded_attack_submotion_from_action(action_id) != 0xFFFFFFFFu) {
              continue;
            }
          }
        }

        // Guard core loop (entry/hold/exit). Keep this before locomotion IASA (e.g. Wait->Jump/Dash).
        // Decomp call site example: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c:43-66.
        uint8_t allow_guard_entry = 0;
        if (action_id == MSL_ACT_WAIT || action_is_walk(action_id) || action_id == MSL_ACT_TURN ||
            action_id == MSL_ACT_TURN_RUN || action_id == MSL_ACT_DASH ||
            action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_BRAKE ||
            action_id == MSL_ACT_RUN_DIRECT || action_id == MSL_ACT_SQUAT ||
            action_id == MSL_ACT_SQUAT_WAIT || action_id == MSL_ACT_SQUAT_RV) {
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
        // If Escape ended this frame, allow the destination state's IASA to run in the same frame.
        // Decomp ordering: Anim callback can change motion state before the frame's input_cb dispatch.
        // - Escape end: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_Escape_Anim,ftCo_EscapeN_Anim}
        // - Destination Wait IASA includes ftCo_Squat_CheckInput:
        //   refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Squat.c}
        // Squat threshold source:
        // - c->crouch_stick_threshold is p_ftCommonData->x90 loaded from
        //   data/common/ft_common_data.json via src/common_params.c.
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        if (action_id == MSL_ACT_ESCAPE_N || action_id == MSL_ACT_ESCAPE_F ||
            action_id == MSL_ACT_ESCAPE_B) {
          escape_update_grounded(batch, c, ch, idx);
          action_id = batch->state.action_id[idx];
          if (action_id == MSL_ACT_ESCAPE_N || action_id == MSL_ACT_ESCAPE_F ||
              action_id == MSL_ACT_ESCAPE_B) {
            continue;
          }
        }

        // WAIT entry transitions (minimal locomotion-only IASA chain):
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        //   - ftCo_AttackS3_CheckInput / ftCo_AttackHi3_CheckInput / ftCo_AttackLw3_CheckInput
        //   - ftCo_Jump_CheckInput
        //   - ftCo_Dash_CheckInput
        //   - ftCo_Turn_CheckInput
        //   - ftCo_Walk_CheckInput
        if (action_id == MSL_ACT_WAIT) {
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y,
                                                    facing_dir, 0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
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
            } else if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0 &&
                       (action_id_start != MSL_ACT_WAIT || (buttons & (uint16_t)MSL_BUTTON_B) == 0) &&
                       stick_y < -c->crouch_stick_threshold) {
              // Decomp: ftCo_Wait_IASA -> ftCo_Squat_CheckInput -> ftCo_Squat_Enter.
              // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Squat.c}
              // Squat threshold source:
              // - c->crouch_stick_threshold is p_ftCommonData->x90 loaded from
              //   data/common/ft_common_data.json via src/common_params.c.
              enter_squat_immediate(batch, idx);
              action_id = (uint16_t)MSL_ACT_SQUAT;
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
                // Decomp: ftCo_Turn_Enter_Smash sets mv.co.turn.x8 = facing_dir.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
                batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
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
                batch->state.dash_x4[idx] = 1u;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                // Decomp: ftCo_Dash_Enter calls ftAnim_8006EBA4 immediately after ChangeMotionState.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
                msl_anim_timebase_tick_once(batch, idx);
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
              // Decomp: ftCo_Walk_Enter delegates to ftWalkCommon_800DFCA4, which calls
              // ftAnim_8006EBA4 immediately after Fighter_ChangeMotionState.
              // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFCA4
              msl_anim_timebase_tick_once(batch, idx);
              action_id = want;
            }
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
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
              batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
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
              batch->state.dash_x4[idx] = 1u;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
              msl_anim_timebase_tick_once(batch, idx);
              // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
              batch->state.tilt_timer_x[idx] = 0xFEu;
              action_id = (uint16_t)MSL_ACT_DASH;
            }
          } else if (batch->state.anim_frame_f32[idx] <
                         (msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]) +
                          (float)ch->landing_lag_frames) &&
                     stick_y < -c->crouch_stick_threshold) {
            // Landing IASA crouch: Landing -> SquatWait on the first interruptible frame when holding down.
            //
            // Decomp:
            // - ftCo_Landing_IASA first gates interrupts:
            //     RETURN_IF(fp->cur_anim_frame < landing_lag)
            // - Then it only checks squat on the *first* interruptible tick:
            //     RETURN_IF((fp->cur_anim_frame < (fp->frame_speed_mul + landing_lag)) &&
            //               ftCo_SquatWait_CheckInput(gobj))
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
            //
            // Sim mapping:
            // - `anim_timebase_update_pre_input()` advances `anim_frame_f32` for this frame before
            //   locomotion IASA runs (step.c ordering). So the first interruptible tick corresponds
            //   to `anim_frame_f32 == landing_lag_frames` (with positive `frame_speed_mul`), and the
            //   decomp's `< (frame_speed_mul + landing_lag)` window becomes:
            //     landing_lag_frames <= anim_frame_f32 < landing_lag_frames + frame_speed_mul
            //
            // Squat input:
            // - ftCo_SquatWait_CheckInput enters SquatWait when (lstick.y < -p_ftCommonData->x90).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_CheckInput
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_WAIT;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            continue;
          } else if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
            // Turn.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_CheckInput
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
            batch->state.turn_x8[idx] = 0;
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
            // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFCA4
            msl_anim_timebase_tick_once(batch, idx);
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
        if (action_id == MSL_ACT_TURN && action_id_start == MSL_ACT_TURN) {
          const float turn_attack_facing_dir =
              batch->state.turn_has_turned[idx] ? facing_dir : -facing_dir;
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                     stick_y, tilt_timer_x, tilt_timer_y,
                                                     turn_attack_facing_dir, 0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
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
                batch->state.turn_x8[idx] = 0;
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                // Decomp: Turn->Dash uses ftCo_Dash_Enter(gobj, 0), so mv.co.dash.x4 = 0.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
                batch->state.dash_x4[idx] = 0u;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
                msl_anim_timebase_tick_once(batch, idx);
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
            // Note: `turn_just_turned` is tracked only when we flip has_turned in the Turn Anim
            // countdown (ftCo_Turn_Anim_Inner).
            const float facing_after_dir =
                batch->state.turn_has_turned[idx] ? facing_dir : -facing_dir;

            // Decomp latch: fn_800C9C2C sets mv.co.turn.x8 and it persists until overwritten.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
            if ((stick_x * facing_after_dir) >= c->dash_flick_abs &&
                tilt_timer_x < c->dash_flick_tilt_max_frames) {
              batch->state.turn_x8[idx] = (int8_t)(facing_after_dir > 0.0f ? 1 : -1);
            }

            if (turn_just_turned && batch->state.turn_x8[idx] != 0 &&
                (stick_x * facing_after_dir) >= c->dash_flick_abs) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
              // Decomp: Turn->Dash uses ftCo_Dash_Enter(gobj, 0), so mv.co.dash.x4 = 0.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
              batch->state.dash_x4[idx] = 0u;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
              msl_anim_timebase_tick_once(batch, idx);
              // Decomp: fp->x670_timer_lstick_tilt_x = 0xFE;
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:62
              batch->state.tilt_timer_x[idx] = 0xFEu;
              action_id = (uint16_t)MSL_ACT_DASH;
            }
          }
        }
        }

        // TurnRun IASA (minimal): Jump.
        //
        // Decomp: TurnRun_IASA only checks fn_800CAF78 (jump -> KneeBend).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
        if (action_id == MSL_ACT_TURN_RUN && action_id_start == MSL_ACT_TURN_RUN) {
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

        // Walk IASA (current): Jump, Dash, then crouch.
        //
        // Decomp: ftCo_Walk_IASA calls ftCo_Jump_CheckInput, ftCo_Dash_CheckInput, then
        // ftCo_800D5FB0 (Squat check/enter).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start)) {
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                     stick_y, tilt_timer_x, tilt_timer_y,
                                                     facing_dir, 0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
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
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
                batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
                msl_anim_timebase_tick_once(batch, idx);
                action_id = (uint16_t)MSL_ACT_TURN;
              } else {
                batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                batch->state.dash_x4[idx] = 1u;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
                msl_anim_timebase_tick_once(batch, idx);
                batch->state.tilt_timer_x[idx] = 0xFEu;
                action_id = (uint16_t)MSL_ACT_DASH;
              }
            } else if (stick_y < -c->crouch_stick_threshold) {
              // Decomp: ftCo_800D5FB0 enters Squat from Walk IASA on down input.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
              enter_squat_immediate(batch, idx);
              action_id = (uint16_t)MSL_ACT_SQUAT;
            }
          }
        }

        // Walk exit gate (ftWalkCommon_800DFEC8): leave walk when stick is released below threshold.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start) &&
            msl_absf(stick_x) < c->walk_stick_threshold) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_WAIT;
        }

        // Walk type update without resetting action_frame (ftWalkCommon_800DFEC8 keeps phase).
        // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start) &&
            msl_absf(stick_x) >= c->walk_stick_threshold) {
          const uint16_t want =
              walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
          if (want != batch->state.action_id[idx]) {
            batch->state.action_id[idx] = want;
            batch->state.animation_index[idx] = anim_for_walk_action(want);
            action_id = want;
          }
        }

        // Run IASA (minimal): Jump.
        //
        // Decomp: ftCo_Run_IASA/ftCo_RunDirect_IASA call fn_800CAF78 (Jump -> KneeBend).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
        if ((action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_DIRECT) &&
            action_id_start == action_id) {
          // Run/RunDirect IASA catch-dash check before jump/attack/run-brake branches.
          // Decomp: ftCo_Run_IASA and ftCo_RunDirect_IASA call ftCo_800D8A38.
          // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Run.c,ftCo_RunDirect.c}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8A38
          if (grab_flow_try_enter_catchdash_from_iasa(batch, c, idx)) {
            continue;
          }

          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y,
                                                    facing_dir, 1, 0)) {
            action_id = batch->state.action_id[idx];
          } else {
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
        }

        // Run -> TurnRun when stick is pushed opposite.
        //
        // Decomp:
        // - ftCo_Run_IASA enters TurnRun via fn_800C9D40 when mv.co.run.x0 <= 0.
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9D40
        if ((action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_DIRECT) &&
            action_id_start == action_id && batch->state.run_x0[idx] == 0 &&
            (stick_x * facing_dir) <= c->turn_run_stick_x_threshold) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN_RUN;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN_RUN;
          // Decomp: TurnRun_Enter doesn't init mv.co.run.x0 (Run state only); clear for safety.
          batch->state.run_x0[idx] = 0;
          // Decomp: fn_800C9D40 passes anim_start=0.0.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9D40
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_TURN_RUN;
        }

        // Run -> RunBrake when stick released.
        // Decomp: ftCo_Run_IASA ends with ftCo_RunBrake_CheckInput.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_CheckInput
        if ((action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_DIRECT) &&
            action_id_start == action_id && batch->state.run_x0[idx] == 0 &&
            msl_absf(stick_x) < c->run_stick_x_threshold) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_RUN_BRAKE;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_RUN_BRAKE;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_RUN_BRAKE;
        }

        // RunBrake IASA (minimal): Jump + Squat.
        //
        // Decomp:
        // - ftCo_RunBrake_IASA runs:
        //   - fn_800CAF78 (jump)
        //   - (cmd_vars[0] && fn_800C9CEC) (TurnRun path; not modeled yet)
        //   - ftCo_800D5FB0 (Squat check/enter)
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
        if (action_id == MSL_ACT_RUN_BRAKE && action_id_start == MSL_ACT_RUN_BRAKE) {
          const MslJumpInput j_in =
              jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (stick_y < -c->crouch_stick_threshold) {
            enter_squat_immediate(batch, idx);
            action_id = (uint16_t)MSL_ACT_SQUAT;
          }
        }

        // Dash IASA (dash-dance / dashback start): allow smash-turn from Dash on a flick opposite-facing.
        // Decomp:
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
        // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
        if (action_id == MSL_ACT_DASH && action_id_start == MSL_ACT_DASH) {
          // Dash IASA catch-dash check before jump/turn/run branches.
          // Decomp: ftCo_Dash_IASA calls ftCo_800D8A38 in all three timing branches.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8A38
          if (grab_flow_try_enter_catchdash_from_iasa(batch, c, idx)) {
            continue;
          }

          const float cur_anim_frame = batch->state.anim_frame_f32[idx];
          const uint8_t dash_x4 = batch->state.dash_x4[idx];
          const uint8_t dash_iasa_early_x4 =
              (dash_x4 != 0u && cur_anim_frame <= c->dash_iasa_x44) ? 1u : 0u;
          if (cur_anim_frame <= c->dash_iasa_x4c &&
              grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y,
                                                    facing_dir, 1, 0)) {
            // Decomp: AttackDash input is only checked in Dash IASA while cur_anim_frame <= x4C.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
            action_id = batch->state.action_id[idx];
          } else {
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
            if (!dash_iasa_early_x4 && cur_anim_frame <= c->dash_iasa_x4c) {
              if ((stick_x * facing_dir) < 0.0f && is_dash_flick(c, stick_x, tilt_timer_x)) {
                // Dash flick opposite-facing triggers Turn (smash-turn path in vanilla).
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:41-43 (ftCo_Turn_Enter_Smash)
                batch->state.turn_has_turned[idx] = 0;
                batch->state.turn_frames_to_turn[idx] = 0;
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
                batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
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
                // Decomp: fn_800CA5F0 enters Run with arg0=0.0, so mv.co.run.x0 starts at 0.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA5F0
                batch->state.run_x0[idx] = 0;
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                action_id = (uint16_t)MSL_ACT_RUN;
              }
            }
            }
          }
        }

        // KneeBend -> Jump
        if (action_id == MSL_ACT_KNEE_BEND) {
          // KneeBend IASA catch check (JC grab) before the jump transition.
          //
          // Decomp ordering:
          // - ftCo_KneeBend_IASA calls ftCo_Catch_CheckInput before short-hop/jump progression.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
          if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
            continue;
          }
          if (kneebend_try_enter_attack_hi4_from_iasa(batch, c, idx, buttons_pressed, stick_y)) {
            continue;
          }

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

            // KneeBend->Jump happens in Anim before the frame's input update, so ftCo_Jump_Enter
            // reads the prior frame's fp->input.lstick.x.
            //
            // Common ftCo_* call chain / ordering (not Fox/Falco-specific):
            // - ftCo_KneeBend_Anim -> ftCo_Jump_Enter
            // - anim_cb runs in Fighter_procUpdate
            // - input update + input_cb run later in Fighter_procInterrupt
            //
            // Sim mapping:
            // - input_apply() has already loaded both prev_input_t and input_t for this step.
            // - `prev_input_main_x` corresponds to prior-frame fp->input.lstick.x.
            // - Fighter_Spaghetti_8006AD10 applies common lstick deadzone before ftCo_Jump_Enter reads
            //   fp->input.lstick.x, so apply c->lstick_deadzone_x here.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
            // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procInterrupt}
            // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
            const float jump_stick_x = apply_deadzone(
                stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
            const uint16_t jump_act = jump_action_from_stick(c, jump_stick_x, facing_dir);
            batch->state.action_id[idx] = jump_act;
            batch->state.animation_index[idx] = (uint32_t)submotion_for_action(jump_act);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.on_ground[idx] = 0;
            // Decomp: ftCo_Jump_Enter calls ftCommon_8007D5D4 (sets fp->ecb_lock=10 and lock flag).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
            batch->state.ecb_lock_timer[idx] = 10u;

            // Ground-to-air momentum + jump impulse (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110)
            const float base_x =
                batch->state.speed_ground_x_self[idx] * ch->ground_to_air_jump_momentum_multiplier;
            float h_vel = base_x + jump_stick_x * ch->jump_h_initial_velocity;
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

            // Allow airdodge (EscapeAir) to trigger on the first airborne frame after takeoff.
            //
            // This matters for wavedash-style inputs: KneeBend enters Jump during Anim, and Jump's
            // IASA can immediately enter EscapeAir on the same frame when L/R is pressed.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
            if (escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
              continue;
            }
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
              if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0 &&
                  (action_id_start != MSL_ACT_WAIT || (buttons & (uint16_t)MSL_BUTTON_B) == 0) &&
                  stick_y < -c->crouch_stick_threshold) {
                // Decomp ordering: Dash anim-end -> Wait, then Wait IASA can enter Squat.
                // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Dash.c,ftCo_Wait.c,ftCo_Squat.c}
                // Squat threshold source:
                // - c->crouch_stick_threshold is p_ftCommonData->x90 loaded from
                //   data/common/ft_common_data.json via src/common_params.c.
                enter_squat_immediate(batch, idx);
                action_id = (uint16_t)MSL_ACT_SQUAT;
              }
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
              if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0 &&
                  (action_id_start != MSL_ACT_WAIT || (buttons & (uint16_t)MSL_BUTTON_B) == 0) &&
                  stick_y < -c->crouch_stick_threshold) {
                // Decomp ordering: RunBrake anim-end -> Wait, then Wait IASA can enter Squat.
                // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_RunBrake.c,ftCo_Wait.c,ftCo_Squat.c}
                // Squat threshold source:
                // - c->crouch_stick_threshold is p_ftCommonData->x90 loaded from
                //   data/common/ft_common_data.json via src/common_params.c.
                enter_squat_immediate(batch, idx);
              }
            }
          }
        }

        continue;
      }

      // Air / non-ground: shield recharge can still occur (decomp checks shield-active flag, not ground/air).
      guard_update_shield_recharge(batch, c, idx);

      if (spacie_specialhi_update(batch, idx, cid, ms, 0u)) {
        continue;
      }

      // Fox/Falco side special (Illusion/Phantasm) air states: keep animation_index stable and
      // model Anim-end transitions before Phys, matching Fighter_procUpdate callback ordering.
      //
      // Decomp:
      // - ftFx_SpecialAirSStart_Anim transitions to ftFx_SpecialAirS_Enter when frames are exhausted.
      // - ftFx_SpecialAirS_Anim transitions to ftFx_SpecialAirSEnd_Enter when frames are exhausted.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSStart_Anim,ftFx_SpecialAirS_Anim}
      if (ms != NULL) {
        if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
          // Keep animation_index stable for side special air states (avoid seed carry-through).
          switch (action_id) {
            case MSL_ACT_FX_SPECIAL_AIR_S_START:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_air_start;
              break;
            case MSL_ACT_FX_SPECIAL_AIR_S:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_air_main;
              break;
            case MSL_ACT_FX_SPECIAL_AIR_S_END:
              batch->state.animation_index[idx] = (uint32_t)ms->specials_air_end;
              break;
            default:
              break;
          }

          // Start -> Main on anim completion.
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START &&
              anim_finished(cid, ms->specials_air_start, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_air_main;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S;
          }

          // Main -> End on anim completion.
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
              anim_finished(cid, ms->specials_air_main, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_air_end;
            // Decomp: ftFx_SpecialAirSEnd_Enter sets fp->self_vel.x = da->x3C * facing_dir;
            //         fp->self_vel.y = 0.0f.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Enter
            batch->state.speed_air_x_self[idx] = ch->illusion_air_end_vel_x * facing_dir;
            batch->state.speed_y_self[idx] = 0.0f;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END;
          }

          // Main -> End on pressed-edge B (IASA callback).
          //
          // Decomp: ftFx_SpecialAirS_IASA checks `fp->input.x668 & HSD_PAD_B` (pressed-edge B) and
          // enters SpecialAirSEnd when B is pressed during the dash portion.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_IASA
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
              (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_air_end;
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Enter
            batch->state.speed_air_x_self[idx] = ch->illusion_air_end_vel_x * facing_dir;
            batch->state.speed_y_self[idx] = 0.0f;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END;
          }

          continue;
        }
      }

      // ----------------------
      // Air locomotion updates
      // ----------------------
      uint8_t is_air_loco = msl_action_is_air_locomotion(action_id) ? 1 : 0;
      // DamageFall has its own IASA chain (ftCo_DamageFall_IASA), including JumpAerial input
      // (ftCo_800CB870). This simulator models that subset in the shared airborne IASA block below,
      // so include DamageFall in the local gate here.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
      if (action_id == (uint16_t)MSL_ACT_DAMAGE_FALL) {
        is_air_loco = 1;
      }
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

        // Aerial attack (AttackAir*) entry from eligible air locomotion states.
        // Decomp: ftCo_AttackAir_CheckInput is consulted from IASA in common aerial states.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
        if (attackair_try_enter_from_air_locomotion(batch, c, idx)) {
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
            // Decomp: ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
            batch->state.ecb_lock_timer[idx] = 10u;
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
              // Decomp: ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
              // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
              batch->state.ecb_lock_timer[idx] = 10u;
              action_id = act;
            }
          }
        }
      }

      // Jump -> Fall state family when animation ends.
      // Decomp:
      // - Ground jumps call Fall_Enter from Jump_Anim.
      // - Aerial jumps call FallAerial_Enter from JumpAerial_Anim.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Anim
      if (action_id == MSL_ACT_JUMP_F || action_id == MSL_ACT_JUMP_B ||
          action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B) {
        const uint32_t anim = batch->state.animation_index[idx];
        if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
          if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                            batch->state.anim_frame_f32[idx])) {
            if (action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_AERIAL;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_AERIAL;
            } else {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
            }
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

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0) {
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
        if (a == (uint16_t)MSL_ACT_LANDING &&
            landing_contact_y_bridge_matches_source(a, batch->state.action_frame[idx],
                                                    batch->state.prev_action_id[idx],
                                                    (uint16_t)MSL_ACT_LANDING)) {
          // Compatibility: some post-collision callback lanes can already be in Landing before this
          // locomotion transition resolver runs. Preserve floor-contact Y for the same decomp-owned
          // Jump/SpecialAirN collision families used by the landing bridge helper above.
          batch->state.pos_y[idx] = batch->state.ground_contact_y[idx];
        }

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
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
          // Decomp: ftFx_SpecialAirSEnd_Coll enters LandingFallSpecial on ground contact.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
          land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL) {
          // Decomp: ftFx_SpecialHiFall_Coll -> ftFx_SpecialHiFall_Enter transitions to
          // SpecialHiLanding with anim_start=13 and immediate anim tick.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
          //   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiFall_Enter
          // }
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING;
          msl_anim_timebase_enter_with_policy(batch, idx, 13.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
          continue;
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
          enter_landing_action_from_air(batch, ch, idx, (size_t)bi, a, land);
        }
      } else if (was_ground && !now_ground) {
        batch->state.fall_fast[idx] = 0;

        // Only force Ground->Air transitions for ground locomotion states for now.
        // Many non-locomotion ground states transition into specific aerial variants (e.g. FallSpecial),
        // which we do not model yet; forcing Fall here causes large action_id regressions.
        if (!action_is_ground_locomotion(a)) {
          // Decomp: ftFx_SpecialHiLanding_Coll enters FallSpecial when no longer grounded.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Coll
          if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING) {
            enter_fall_special_from_specialhi(batch, idx);
          }
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
        batch->state.ecb_lock_timer[idx] = 10u;

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
