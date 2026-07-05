#include "spacie_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "coll_env_flags.h"
#include "common_specials.h"
#include "dash_iasa.h"
#include "input_axis.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "mpcoll_wall_ceil.h"
#include "msl_math.h"
#include "specialhi_pose.h"

// Fox/Falco special machines are keyed by extracted MotionState fx-kind rows. The numeric action
// range is character-local, so common callers ask this owner instead of branching on char ids.
uint16_t spacie_fx_kind_action(uint8_t char_id, uint8_t fx_kind) {
  return msl_motion_state_action_for_fx_kind(char_id, fx_kind);
}

uint8_t spacie_char_owns_fx_kind(uint8_t char_id, uint8_t fx_kind) {
  return spacie_fx_kind_action(char_id, fx_kind) != 0xFFFFu;
}

static inline uint8_t spacie_char_owns_sideb_machine(uint8_t char_id) {
  return spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_S_START);
}

static inline uint8_t spacie_char_owns_specialhi_machine(uint8_t char_id) {
  return spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD);
}

static inline float spacie_clamp_absf(float value, float max_abs) {
  if (value > max_abs) {
    return max_abs;
  }
  if (value < -max_abs) {
    return -max_abs;
  }
  return value;
}

static inline uint8_t spacie_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0u;
  }
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline uint8_t spacie_char_owns_b_special_dispatch(uint8_t char_id) {
  // Common Run/Dash IASA dispatches the character B-special family before appeal and terminal
  // RunBrake/jump tails. Use generated MotionState owner identity rather than Fox/Falco char-id
  // routing: the numeric special action range is character-local, and unsupported/future chars
  // carry no FX kind rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_SpecialS_CheckInput,ftCo_800D68C0}
  // data/motion_state/owners/*.bin::fx_special_kind_by_action
  return (uint8_t)(spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_N_START) ||
                   spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_S_START) ||
                   spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD) ||
                   spacie_char_owns_fx_kind(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_LW_START));
}

uint8_t spacie_run_iasa_has_b_special_intent(uint8_t char_id, uint16_t buttons_pressed) {
  if (!spacie_char_owns_b_special_dispatch(char_id)) {
    return 0u;
  }
  return ((buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
}

void spacie_dash_iasa_enter_grounded_side_special_start(MslBatch* batch, size_t idx,
                                                        const MslCommonParams* c,
                                                        const MslSpecialMsids* ms,
                                                        const MslCharParams* ch, float stick_x) {
  if (batch == NULL || !spacie_char_owns_sideb_machine(batch->state.char_id[idx])) {
    // The char owns no SpecialS machine rows (the numeric action-id range is shared; see
    // char_registry.h).
    return;
  }
  if (c == NULL || ms == NULL || ch == NULL) {
    return;
  }
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  // Dash_IASA can enter grounded Side-B through ftCo_SpecialS_CheckInput before the later guard
  // path. Match the shared Side-B enter velocity/facing owner used by the regular special
  // dispatcher, then apply Dash_IASA's terminal gr_vel scalar because the Dash callback resumes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
  //   ftCo_SpecialS_CheckInput,doEnter}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSStart_Enter
  // data/characters/{fox,falco}.json::{side_special_ground_entry_vel_mul,illusion_ground_vel_x}
  if (stick_x * facing_dir < -c->special_side_reverse_threshold) {
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
  }
  ftco_specials_apply_grounded_sideb_doenter(batch, ch, idx);
  if (ch->illusion_ground_vel_x > 0.0f) {
    batch->state.speed_ground_x_self[idx] /= ch->illusion_ground_vel_x;
  }
  batch->state.action_id[idx] =
      spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_S_START);
  batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
  batch->state.fall_fast[idx] = 0u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
}

static inline void spacie_side_special_reset_ghost_ring_on_main_entry(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp owner:
  // - ftFx_SpecialS_Enter / ftFx_SpecialAirS_Enter call ftFox_SpecialS_SetVars on main entry.
  // - ftFox_SpecialS_SetVars initializes ghostEffectPos[0..3] = cur_pos before the main-state
  //   Phys callbacks begin advancing the ring.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Enter,ftFx_SpecialAirS_Enter,ftFox_SpecialS_SetVars}
  batch->state.illusion_ghost_pos0_x[idx] = batch->state.pos_x[idx];
  batch->state.illusion_ghost_pos0_y[idx] = batch->state.pos_y[idx];
  batch->state.illusion_ghost_pos1_x[idx] = batch->state.pos_x[idx];
  batch->state.illusion_ghost_pos1_y[idx] = batch->state.pos_y[idx];
  batch->state.illusion_ghost_pos2_x[idx] = batch->state.pos_x[idx];
  batch->state.illusion_ghost_pos2_y[idx] = batch->state.pos_y[idx];
}

void spacie_side_special_ground_to_air_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t grounded_action) {
  if (batch == NULL || !spacie_char_owns_sideb_machine(batch->state.char_id[idx])) {
    return;
  }
  if (ms == NULL || ch == NULL) {
    return;
  }
  // Decomp owner:
  // - ftFx_SpecialSStart_GroundToAir / ftFx_SpecialS_GroundToAir call ftCommon_8007D60C before
  //   preserving the current animation frame in the matching aerial motion-state.
  // - ftCommon_8007D60C consumes all jumps, clears gr_vel, and sets ecb_lock=5.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialSStart_GroundToAir,ftFx_SpecialS_GroundToAir}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
  batch->state.jumps_left[idx] = 0u;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.ecb_lock_timer[idx] = 5u;
  const uint8_t grounded_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], grounded_action);
  if (grounded_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_START) {
    batch->state.action_id[idx] =
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START);
    batch->state.animation_index[idx] = (uint32_t)ms->specials_air_start;
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    batch->state.action_id[idx] =
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S);
    batch->state.animation_index[idx] = (uint32_t)ms->specials_air_main;
  }
  msl_anim_timebase_enter(batch, idx, batch->state.anim_frame_f32[idx], 1.0f);
}

void spacie_side_special_air_to_ground_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t air_action) {
  if (batch == NULL || !spacie_char_owns_sideb_machine(batch->state.char_id[idx])) {
    return;
  }
  if (ms == NULL || ch == NULL) {
    return;
  }
  // Decomp owner:
  // - ftFx_SpecialAirSStart_AirToGround / ftFx_SpecialAirS_AirToGround call ftCommon_8007D7FC
  //   before preserving the current animation frame in the matching grounded motion-state.
  // - ftCommon_8007D7FC/ftCommon_8007D6A4 restore grounded jump ownership and copy self_vel.x
  //   into gr_vel (clamped).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialAirSStart_AirToGround,ftFx_SpecialAirS_AirToGround}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  batch->state.on_ground[idx] = 1u;
  batch->state.jumps_left[idx] = ch->max_jumps;
  batch->state.fall_fast[idx] = 0u;
  batch->state.speed_ground_x_self[idx] =
      spacie_clamp_absf(batch->state.speed_air_x_self[idx], ch->ground_max_horizontal_velocity);
  const uint8_t air_kind = msl_motion_state_fx_special_kind(batch->state.char_id[idx], air_action);
  if (air_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START) {
    batch->state.action_id[idx] =
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_S_START);
    batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
  } else {
    batch->state.action_id[idx] =
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_S);
    batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_main;
  }
  msl_anim_timebase_enter(batch, idx, batch->state.anim_frame_f32[idx], 1.0f);
}

static inline void spacie_specialhi_apply_air_launch_ownership(MslBatch* batch, size_t idx,
                                                               const MslCharParams* ch) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Decomp: ftFx_SpecialHiHold{Air}_Anim runs in Fighter_8006A360 (proc priority 1), before
  // Fighter_Spaghetti_8006AD10 installs current-frame input (priority 3). Therefore the anim-end
  // ftFx_SpecialAirHi_Enter launch reads the pre-input `fp->input.lstick` snapshot.
  //
  // ftFx_SpecialAirHi_Enter derives launch direction from `fp->input.lstick` and
  // ftFox_DatAttrs.{x64,x88}, then overwrites self_vel using x74 launch speed.
  // It also consumes all jumps through `x1968_jumpsUsed = co_attrs.max_jumps` on aerial launch
  // entry; Slippi post-frame stores the inverse `jumps_left`, so the sim lane writes zero here.
  // - Read `prev_input_main_*` here because step.c loads that lane as the pre-input snapshot for
  //   prio-1 Anim callback ownership.
  // - facing updates when |stick_x| > x88 before atan2f.
  // - rotateModel defaults to HALF_PI32 when below direction threshold.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}
  // refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs
  // Source keys: data/characters/{fox,falco}.json
  // - firefox_direction_stick_range_min
  // - firefox_launch_speed
  // - firefox_facing_stick_range_min
  // data/common/ft_common_data.json
  // - lstick_deadzone_x
  // - lstick_deadzone_y
  const MslCommonParams* c = msl_common_params();
  float stick_x = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
  float stick_y = stick_i8_to_unit(batch->state.prev_input_main_y[idx]);
  if (c != NULL) {
    stick_x = apply_deadzone(stick_x, c->lstick_deadzone_x);
    stick_y = apply_deadzone(stick_y, c->lstick_deadzone_y);
  }
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
  msl_specialhi_rotate_model_set(batch, idx, launch_angle);
  batch->state.jumps_left[idx] = 0u;
}

static inline uint8_t spacie_specialhi_try_ground_launch_from_hold(MslBatch* batch, size_t idx,
                                                                   const MslCharParams* ch) {
  if (batch == NULL || ch == NULL) {
    return 0u;
  }
  // This is the grounded branch of the same Hold/HoldAir Anim callback as
  // spacie_specialhi_apply_air_launch_ownership(), so use the pre-input stick snapshot here too.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_AirToGround}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}
  const float stick_x = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
  const float stick_y = stick_i8_to_unit(batch->state.prev_input_main_y[idx]);
  const float abs_x = msl_absf(stick_x);
  const float abs_y = msl_absf(stick_y);

  // Decomp: grounded Hold/HoldAir only takes the grounded launch branch when:
  // - |stick_x| + |stick_y| >= x64 direction threshold
  // - angle(floor.normal, stick_vec) >= PI/2
  // - ftCo_8009A134 (platform pass-through) is false
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_AirToGround
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_IsOnPlatform
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  if (mpcoll_is_on_platform(batch, bi, idx)) {
    return 0u;
  }
  if ((abs_x + abs_y) < ch->firefox_direction_stick_range_min) {
    return 0u;
  }

  const float nx = batch->state.ground_normal_x[idx];
  const float ny = batch->state.ground_normal_y[idx];
  if ((nx * stick_x + ny * stick_y) > 0.0f) {
    return 0u;
  }

  // Decomp: ftCommon_UpdateFacing uses the sign of lstick.x without any additional threshold.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UpdateFacing
  batch->state.facing[idx] = (uint8_t)(stick_x >= 0.0f);
  batch->state.speed_ground_x_self[idx] =
      ch->firefox_launch_speed * (batch->state.facing[idx] ? 1.0f : -1.0f);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  msl_specialhi_rotate_model_set(batch, idx, 0.0f);
  return 1u;
}

static inline float spacie_specialhi_collision_angle_to_self_vel(float normal_x, float normal_y,
                                                                 float vel_x, float vel_y) {
  const float n_mag = sqrtf(normal_x * normal_x + normal_y * normal_y);
  const float v_mag = sqrtf(vel_x * vel_x + vel_y * vel_y);
  if (!(n_mag > 0.0f) || !(v_mag > 0.0f)) {
    return MSL_PI_F;
  }
  float dot = (normal_x * vel_x + normal_y * vel_y) / (n_mag * v_mag);
  if (dot > 1.0f) {
    dot = 1.0f;
  } else if (dot < -1.0f) {
    dot = -1.0f;
  }
  return acosf(dot);
}

void spacie_specialhi_apply_collision_facing_dir(MslBatch* batch, const MslCharParams* ch,
                                                 size_t idx) {
  if (batch == NULL || !spacie_char_owns_specialhi_machine(batch->state.char_id[idx])) {
    return;
  }
  if (ch == NULL ||
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
    return;
  }

  const uint32_t env = batch->state.coll_env_flags[idx];
  float nx = 0.0f;
  float ny = 0.0f;
  if ((env & (uint32_t)MSL_COLLIDE_CEILING_MASK) != 0u) {
    nx = batch->state.ceiling_normal_x[idx];
    ny = batch->state.ceiling_normal_y[idx];
  } else if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u ||
             batch->state.wall_kind[idx] == 1u) {
    nx = batch->state.wall_normal_x[idx];
    ny = batch->state.wall_normal_y[idx];
  } else if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK) != 0u ||
             batch->state.wall_kind[idx] == 2u) {
    nx = batch->state.wall_normal_x[idx];
    ny = batch->state.wall_normal_y[idx];
  } else if ((env & (uint32_t)MSL_COLLIDE_FLOOR_MASK) != 0u) {
    nx = batch->state.ground_normal_x[idx];
    ny = batch->state.ground_normal_y[idx];
  } else {
    return;
  }

  const float vx = batch->state.speed_air_x_self[idx];
  const float vy = batch->state.speed_y_self[idx];
  const float angle = spacie_specialhi_collision_angle_to_self_vel(nx, ny, vx, vy);
  const float threshold = (90.0f + ch->firefox_bound_angle_degrees) * (MSL_PI_F / 180.0f);
  if (!(angle < threshold)) {
    return;
  }

  // Decomp: after the SpecialAirHi floor/wall/ceiling angle predicate,
  // ftFx_SpecialAirHi_Coll writes `fp->facing_dir = sign(fp->self_vel.x)` and recomputes
  // rotateModel from self_vel.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
  // Source key: data/characters/{fox,falco}.json::firefox_bound_angle_degrees
  batch->state.facing[idx] = (uint8_t)(vx >= 0.0f);
  msl_specialhi_rotate_model_set_from_velocity(batch, idx);
}

uint8_t spacie_specialhi_update(MslBatch* batch, size_t idx, uint8_t char_id,
                                const MslSpecialMsids* ms, uint8_t on_ground) {
  if (!spacie_char_owns_specialhi_machine(char_id)) {
    return 0u;
  }
  if (batch == NULL || ms == NULL) {
    return 0u;
  }
  uint16_t a = batch->state.action_id[idx];
  // Decomp collision wrappers for Hold/HoldAir keep the same logical state when crossing
  // ground/air, preserving current animation frame.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHold_GroundToAir,ftFx_SpecialHiHoldAir_AirToGround}
  const uint8_t a_kind_pre = msl_motion_state_fx_special_kind(char_id, a);
  if (a_kind_pre == (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD && !on_ground) {
    a = spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR);
    batch->state.action_id[idx] = a;
  } else if (a_kind_pre == (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR && on_ground) {
    a = spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD);
    batch->state.action_id[idx] = a;
  }

  switch (msl_motion_state_fx_special_kind(char_id, a)) {
    case MSL_FX_KIND_SPECIAL_HI_HOLD:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_hold;
      if (spacie_anim_finished(char_id, ms->specialhi_ground_hold,
                               batch->state.anim_frame_f32[idx])) {
        if (on_ground && spacie_specialhi_try_ground_launch_from_hold(
                             batch, idx, msl_char_params_fast(char_id))) {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI);
        } else {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI);
          if (on_ground) {
            batch->state.on_ground[idx] = 0u;
            batch->state.speed_ground_x_self[idx] = 0.0f;
            batch->state.ecb_lock_timer[idx] = 5u;
          }
          const MslCharParams* ch = msl_char_params_fast(char_id);
          if (ch != NULL) {
            spacie_specialhi_apply_air_launch_ownership(batch, idx, ch);
          } else {
            batch->state.speed_air_x_self[idx] = 0.0f;
            batch->state.speed_y_self[idx] = 0.0f;
          }
        }
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_HI_HOLD_AIR:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_air_hold;
      if (spacie_anim_finished(char_id, ms->specialhi_air_hold, batch->state.anim_frame_f32[idx])) {
        if (on_ground && spacie_specialhi_try_ground_launch_from_hold(
                             batch, idx, msl_char_params_fast(char_id))) {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_HI);
        } else {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI);
          if (on_ground) {
            batch->state.on_ground[idx] = 0u;
            batch->state.speed_ground_x_self[idx] = 0.0f;
            batch->state.ecb_lock_timer[idx] = 5u;
          }
          const MslCharParams* ch = msl_char_params_fast(char_id);
          if (ch != NULL) {
            spacie_specialhi_apply_air_launch_ownership(batch, idx, ch);
          } else {
            batch->state.speed_air_x_self[idx] = 0.0f;
            batch->state.speed_y_self[idx] = 0.0f;
          }
        }
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_HI:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      if (!on_ground) {
        a = spacie_fx_kind_action(char_id, (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI);
        batch->state.action_id[idx] = a;
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      }
      {
        const MslCharParams* ch_hi = msl_char_params_fast(char_id);
        const uint8_t launch_done_hi =
            (ch_hi != NULL && ch_hi->firefox_launch_duration_frames > 0u)
                ? (batch->state.action_frame[idx] >= (int16_t)ch_hi->firefox_launch_duration_frames)
                : spacie_anim_finished(char_id, ms->specialhi_ground_main,
                                       batch->state.anim_frame_f32[idx]);
        if (launch_done_hi) {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, on_ground ? (uint8_t)MSL_FX_KIND_SPECIAL_HI_LANDING
                                                       : (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL);
          batch->state.animation_index[idx] =
              msl_motion_state_submotion_id(char_id, batch->state.action_id[idx]);
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_AIR_HI:
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      {
        const MslCharParams* ch_air_hi = msl_char_params_fast(char_id);
        const uint8_t launch_done_air_hi =
            (ch_air_hi != NULL && ch_air_hi->firefox_launch_duration_frames > 0u)
                ? (batch->state.action_frame[idx] >=
                   (int16_t)ch_air_hi->firefox_launch_duration_frames)
                : spacie_anim_finished(char_id, ms->specialhi_ground_main,
                                       batch->state.anim_frame_f32[idx]);
        if (launch_done_air_hi) {
          batch->state.action_id[idx] =
              spacie_fx_kind_action(char_id, on_ground ? (uint8_t)MSL_FX_KIND_SPECIAL_HI_LANDING
                                                       : (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL);
          batch->state.animation_index[idx] =
              msl_motion_state_submotion_id(char_id, batch->state.action_id[idx]);
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_HI_LANDING:
      batch->state.animation_index[idx] = msl_motion_state_submotion_id(char_id, a);
      if (!on_ground) {
        const MslCharParams* ch = msl_char_params_fast(char_id);
        msl_locomotion_enter_fall_special_via_ftco_80096900(
            batch, idx, 1u, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f, 1u);
        return 1u;
      }
      if (spacie_anim_finished(char_id, msl_motion_state_submotion_id(char_id, a),
                               batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        return 0u;
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_HI_FALL:
      batch->state.animation_index[idx] = msl_motion_state_submotion_id(char_id, a);
      if (spacie_anim_finished(char_id, msl_motion_state_submotion_id(char_id, a),
                               batch->state.anim_frame_f32[idx])) {
        const MslCharParams* ch = msl_char_params_fast(char_id);
        msl_locomotion_enter_fall_special_via_ftco_80096900(
            batch, idx, 1u, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f, 1u);
      }
      return 1u;
    case MSL_FX_KIND_SPECIAL_HI_BOUND:
      batch->state.animation_index[idx] = msl_motion_state_submotion_id(char_id, a);
      if (!on_ground && spacie_anim_finished(char_id, msl_motion_state_submotion_id(char_id, a),
                                             batch->state.anim_frame_f32[idx])) {
        const MslCharParams* ch = msl_char_params_fast(char_id);
        msl_locomotion_enter_fall_special_via_ftco_80096900(
            batch, idx, 1u, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f, 1u);
      }
      return 1u;
    default:
      return 0u;
  }
}

uint8_t ftfx_specials_anim_update(MslBatch* batch, const MslSpecialMsids* ms,
                                  const MslCharParams* ch, size_t idx, uint8_t on_ground,
                                  uint16_t buttons_pressed, float facing_dir,
                                  uint16_t* action_id_io) {
  if (batch == NULL || ms == NULL || ch == NULL || action_id_io == NULL) {
    return 0u;
  }

  const uint8_t start_kind =
      on_ground ? (uint8_t)MSL_FX_KIND_SPECIAL_S_START : (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START;
  const uint8_t main_kind =
      on_ground ? (uint8_t)MSL_FX_KIND_SPECIAL_S : (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S;
  const uint8_t end_kind =
      on_ground ? (uint8_t)MSL_FX_KIND_SPECIAL_S_END : (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END;
  const uint16_t start_msid = on_ground ? ms->specials_ground_start : ms->specials_air_start;
  const uint16_t main_msid = on_ground ? ms->specials_ground_main : ms->specials_air_main;
  const uint16_t end_msid = on_ground ? ms->specials_ground_end : ms->specials_air_end;

  uint8_t kind = msl_motion_state_fx_special_kind(batch->state.char_id[idx], *action_id_io);
  if (kind != start_kind && kind != main_kind && kind != end_kind) {
    return 0u;
  }

  // Fox/Falco side special (Illusion/Phantasm): keep animation_index stable and model Anim-end
  // transitions before Phys, matching Fighter_procUpdate callback ordering. Ground and air rows
  // are the same source machine with different MotionState rows, terminal velocities, and terminal
  // exit action.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFx_SpecialSEnd_Enter,ftFx_SpecialAirSEnd_Enter}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  if (kind == start_kind) {
    batch->state.animation_index[idx] = (uint32_t)start_msid;
  } else if (kind == main_kind) {
    batch->state.animation_index[idx] = (uint32_t)main_msid;
  } else {
    batch->state.animation_index[idx] = (uint32_t)end_msid;
  }

  if (kind == start_kind && spacie_anim_finished(batch->state.char_id[idx], start_msid,
                                                 batch->state.anim_frame_f32[idx])) {
    *action_id_io = spacie_fx_kind_action(batch->state.char_id[idx], main_kind);
    batch->state.action_id[idx] = *action_id_io;
    batch->state.animation_index[idx] = (uint32_t)main_msid;
    spacie_side_special_reset_ghost_ring_on_main_entry(batch, idx);
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    kind = main_kind;
  }

  if (kind == main_kind && (spacie_anim_finished(batch->state.char_id[idx], main_msid,
                                                 batch->state.anim_frame_f32[idx]) ||
                            (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u)) {
    batch->state.action_id[idx] = spacie_fx_kind_action(batch->state.char_id[idx], end_kind);
    batch->state.animation_index[idx] = (uint32_t)end_msid;
    if (on_ground) {
      batch->state.speed_ground_x_self[idx] = spacie_clamp_absf(
          ch->illusion_ground_end_vel_x * facing_dir, ch->dash_run_terminal_velocity);
    } else {
      batch->state.speed_air_x_self[idx] = ch->illusion_air_end_vel_x * facing_dir;
      batch->state.speed_y_self[idx] = 0.0f;
    }
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    *action_id_io = batch->state.action_id[idx];
    kind = end_kind;
  }

  if (kind == end_kind &&
      spacie_anim_finished(batch->state.char_id[idx], end_msid, batch->state.anim_frame_f32[idx])) {
    if (on_ground) {
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      *action_id_io = (uint16_t)MSL_ACT_WAIT;
    } else {
      msl_locomotion_enter_fall_special_via_ftco_80096900(
          batch, idx, 1u, ch != NULL ? (float)ch->illusion_landing_lag_frames : 0.0f, 1u);
      *action_id_io = (uint16_t)MSL_ACT_FALL_SPECIAL;
    }
  }
  return 1u;
}

uint8_t spacie_specialhi_hold_air_ground_contact(MslBatch* batch, const MslSpecialMsids* ms,
                                                 const MslCharParams* ch, size_t idx) {
  if (batch == NULL || ms == NULL || ch == NULL) {
    return 0u;
  }
  if (msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]) !=
      (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR) {
    return 0u;
  }
  // ftFx_SpecialHiHoldAir_Coll consumes ft_CheckGroundAndLedge and immediately routes through
  // ftFx_SpecialHiHoldAir_AirToGround. The AirToGround handler calls ftCommon_8007D7FC, then
  // changes to the grounded hold motion while preserving the current animation frame.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialHiHoldAir_AirToGround}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  batch->state.action_id[idx] =
      spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD);
  batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_hold;
  batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
  return 1u;
}

uint8_t spacie_side_special_air_contact_to_ground(MslBatch* batch, const MslSpecialMsids* ms,
                                                  const MslCharParams* ch, size_t idx,
                                                  uint16_t action_id) {
  if (batch == NULL || ms == NULL) {
    return 0u;
  }
  const uint8_t kind = msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id);
  if (kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START) {
    spacie_side_special_air_to_ground_transition(
        batch, ms, ch, idx,
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START));
    return 1u;
  }
  if (kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S) {
    spacie_side_special_air_to_ground_transition(
        batch, ms, ch, idx,
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S));
    return 1u;
  }
  return 0u;
}

uint8_t spacie_side_special_ground_floor_loss_to_air(MslBatch* batch, const MslSpecialMsids* ms,
                                                     const MslCharParams* ch, size_t idx,
                                                     uint16_t action_id) {
  if (batch == NULL || ms == NULL) {
    return 0u;
  }
  const uint8_t kind = msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id);
  if (kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_START) {
    spacie_side_special_ground_to_air_transition(
        batch, ms, ch, idx,
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_S_START));
    return 1u;
  }
  if (kind == (uint8_t)MSL_FX_KIND_SPECIAL_S) {
    spacie_side_special_ground_to_air_transition(
        batch, ms, ch, idx,
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_S));
    return 1u;
  }
  return 0u;
}

void spacie_enter_specialhi_bound_from_airhi_collision(MslBatch* batch, const MslCharParams* ch,
                                                       size_t idx) {
  if (batch == NULL || !spacie_char_owns_specialhi_machine(batch->state.char_id[idx])) {
    return;
  }
  if (ch == NULL) {
    return;
  }
  // Decomp: SpecialAirHi collision can enter the rebound motion state through
  // ftFx_SpecialHiBound_Enter. The motion-state handler calls ftAnim_8006EBA4 immediately and does
  // not convert the fighter to grounded; Bound_Phys/Coll continue to branch on ground_or_air.
  // After motion entry it scales horizontal self velocity by ftFox_DatAttrs.x84.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}
  // Source key: data/characters/{fox,falco}.json `firefox_bound_vel_x`.
  batch->state.action_id[idx] =
      spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_HI_BOUND);
  batch->state.animation_index[idx] =
      msl_motion_state_submotion_id(batch->state.char_id[idx], batch->state.action_id[idx]);
  batch->state.on_ground[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.speed_air_x_self[idx] *= ch->firefox_bound_vel_x;
}
