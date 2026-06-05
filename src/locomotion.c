#include "locomotion.h"

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "action.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "attack_id_tables.h"
#include "attack_identity.h"
#include "buttons.h"
#include "blaster.h"
#include "char_params.h"
#include "combat.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "dash_iasa.h"
#include "escapeair_collision_owner.h"
#include "grab_flow.h"
#include "input.h"
#include "input_axis.h"
#include "instance_id.h"
#include "jump_input.h"
#include "move_tables.h"
#include "motion_state_owners.h"
#include "mpcoll_floor_skip.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_wall_ceil.h"
#include "msl_math.h"
#include "physics.h"
#include "shine.h"
#include "special_msids.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "trigger_input.h"
#include "throw_flow.h"

static inline float msl_signf(float x) { return x < 0.0f ? -1.0f : 1.0f; }

static inline void locomotion_consume_deadupstar_effect_prefix_before_wait(MslBatch* batch, int bi,
                                                                           int player) {
  if (batch == NULL || bi < 0 || player <= 0 || player > (int)MSL_MAX_PLAYERS) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (player > num_players) {
    return;
  }
  for (int p = 0; p < player; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
      continue;
    }
    // DeadUpStar_Anim's phase-1 completion spawns async effect kind 0x42D before later players'
    // Fighter_procUpdate callbacks run. That dispatch creates generator 0x121; the particle
    // generator layer owns HSD_Randf/HSD_Rand consumers before a later terminal Wait callback can
    // call getAnimID's HSD_Randi(100). Replay-frame seeds expose only the frame-boundary seed, so
    // this bounded player-order prefix is modeled at the Wait site instead of broad global effect
    // RNG reconstruction.
    // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
    // refs/melee/src/sysdolphin/baselib/particle.c
    combat_rng_consume_step_site(batch, bi, MSL_RNG_SITE_DEAD_UP_STAR_EFFECT_PREFIX);
    combat_rng_consume_step_site(batch, bi, MSL_RNG_SITE_DEAD_UP_STAR_EFFECT_PREFIX);
  }
}

static inline uint16_t choose_wait_anim_variant(MslBatch* batch, int bi, const MslCharParams* ch,
                                                uint16_t current_anim) {
  const uint8_t rng_owner =
      (batch != NULL && batch->rollout_clock_rng_owned != NULL && bi >= 0 && bi < batch->batch_size)
          ? batch->rollout_clock_rng_owned[bi]
          : (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
  if (rng_owner != (uint8_t)MSL_ROLLOUT_CLOCK_HSD_RAND_STREAM &&
      rng_owner != (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED) {
    // Only source-owned HSD streams, or replay-frame seeds whose same-frame prefix sites have been
    // modeled by the caller, can select a fresh idle animation here.
    return current_anim;
  }
  if (ch == NULL || ch->wait_anim_choice_count == 0u) {
    (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTWAIT_ANIM_VARIANT, 100);
    return current_anim;
  }

  const uint8_t allow_same = (current_anim == 2u || current_anim == 31u) ? 1u : 0u;
  uint16_t fallback = current_anim;
  for (uint8_t attempt = 0; attempt < ch->wait_anim_choice_count; attempt++) {
    const int32_t sample =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTWAIT_ANIM_VARIANT, 100) + 1;
    int32_t accum = 0;
    uint16_t chosen = current_anim;
    for (uint8_t i = 0; i < ch->wait_anim_choice_count; i++) {
      accum += (int32_t)ch->wait_anim_choice_weights[i];
      if (sample <= accum) {
        chosen = ch->wait_anim_choice_msids[i];
        break;
      }
    }
    if (attempt == 0u) {
      fallback = chosen;
    }
    if (allow_same || chosen != current_anim) {
      return chosen;
    }
  }

  // Source loops until non-current when the current idle is not Wait1_0/SquatWait. Keep the hot
  // path bounded; Fox/Falco tables have two choices, so this fallback preserves the owner shape.
  for (uint8_t i = 0; i < ch->wait_anim_choice_count; i++) {
    const uint16_t candidate = ch->wait_anim_choice_msids[i];
    if (candidate != current_anim) {
      return candidate;
    }
  }
  return fallback;
}

static inline uint16_t walk_action_from_speed(const MslCommonParams* c, const MslCharParams* ch,
                                              float speed_ground_x_self);
static inline uint32_t anim_for_walk_action(uint16_t a);
static inline uint8_t is_dash_flick(const MslCommonParams* c, float stick_x, uint8_t tilt_timer_x);
static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y);
static inline uint8_t walk_check_input_accepts(const MslCommonParams* c, float stick_x,
                                               float facing_dir);
static inline uint8_t spacie_speciallw_pressed(const MslCommonParams* c, uint8_t char_id,
                                               uint16_t buttons_pressed, float stick_y);
static inline uint8_t spacie_speciallw_wait_iasa_pressed_edge(const MslCommonParams* c,
                                                              uint8_t char_id,
                                                              uint16_t buttons_pressed,
                                                              float stick_x, float stick_y);
static inline uint8_t jump_enter_pre_input_tilt_y_after_input(const MslCommonParams* c,
                                                              float stick_y, float prev_stick_y);
static inline MslJumpInput jump_input_from_edges(const MslCommonParams* c, uint16_t buttons_pressed,
                                                 float stick_y, uint8_t tilt_timer_y);
static inline uint16_t jump_action_from_stick(const MslCommonParams* c, float stick_x,
                                              float facing_dir);
static inline void locomotion_apply_jump_enter_ground_to_air(MslBatch* batch, size_t idx);
static inline uint8_t locomotion_try_enter_jump_aerial_iasa(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint8_t jump_input, float stick_x, float facing_dir, uint8_t block_from_jump_aerial);
static inline uint32_t submotion_for_action(uint16_t a);
static inline uint8_t run_iasa_has_spacie_b_special_intent(uint8_t char_id,
                                                           uint16_t buttons_pressed);
static inline uint8_t wait_iasa_locomotion_subset_try_enter(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint16_t buttons, uint16_t buttons_pressed, float stick_x, float stick_y, uint8_t tilt_timer_x,
    uint8_t tilt_timer_y, float facing_dir, uint16_t action_id_start);
static inline void enter_fall_from_grounded_floor_loss(MslBatch* batch, const MslCharParams* ch,
                                                       size_t idx);

static inline float clamp_absf(float value, float max_abs) {
  if (value > max_abs) {
    return max_abs;
  }
  if (value < -max_abs) {
    return -max_abs;
  }
  return value;
}

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  // Source-policy representation: decomp gates on "frames remaining" (joint track remaining).
  // MSL's extracted animation tables expose the terminal frame, so runtime uses the matching
  // sanitized timebase end-frame comparison.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline void grounded_attack_carry_allow_interrupt(MslBatch* batch, size_t idx) {
  // Source-callback ownership for fp+0x2218 bit0:
  // grounded Attack* IASA callbacks first test command-owned fp->allow_interrupt, then may enter a
  // non-attack destination through Wait_IASA in the same fighter proc. The destination post-frame
  // still serializes the already-live allow_interrupt bit, so write it at the transition site.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,
  //   ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
}

static inline void landing_entry_carry_raw_allow_interrupt_from_source(MslBatch* batch, size_t idx,
                                                                       uint16_t source_action,
                                                                       uint16_t land_action) {
  if (batch == NULL || land_action != (uint16_t)MSL_ACT_LANDING) {
    return;
  }
  if (move_tables_attackair_allow_interrupt(batch->state.char_id[idx], source_action,
                                            batch->state.anim_frame_f32[idx]) == 0u) {
    return;
  }
  // Raw fp+0x2218 bit0 carry on basic Landing entry:
  // - AttackAir scripts set fp->allow_interrupt through ftAction_80071950.
  // - ftCo_AttackAir_Coll can enter ftCo_Landing_Enter_Basic on auto-cancel floor contact.
  // - ftCo_Landing_Enter_Basic sets mv.co.landing.allow_interrupt=true, but does not clear the
  //   already-live raw fp->allow_interrupt bit before Slippi serializes state_flags[0].
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
  //   ftCo_Landing_Enter,ftCo_Landing_Enter_Basic}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
}

static inline void dash_iasa_enter_grounded_side_special_start(MslBatch* batch, size_t idx,
                                                               const MslCommonParams* c,
                                                               const MslSpecialMsids* ms,
                                                               const MslCharParams* ch,
                                                               float stick_x) {
  if (batch == NULL || c == NULL || ms == NULL || ch == NULL) {
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
  batch->state.speed_ground_x_self[idx] +=
      -(batch->state.speed_ground_x_self[idx] * (1.0f - ch->side_special_ground_entry_vel_mul)) *
      batch->state.ground_friction_mul[idx];
  if (ch->illusion_ground_vel_x > 0.0f) {
    batch->state.speed_ground_x_self[idx] /= ch->illusion_ground_vel_x;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_START;
  batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
  batch->state.fall_fast[idx] = 0u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
}

static inline uint8_t action_preserves_cliff_ledge_floor_owner(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
    case MSL_ACT_ESCAPE_AIR:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint16_t source_x668_button_edges_with_z_a(uint16_t buttons_pressed) {
  // Decomp: Fighter_Spaghetti maps raw Z into held HSD_PAD_A before building input.x668. Common
  // callbacks that read `fp->input.x668 & HSD_PAD_A` therefore see a fresh Z edge as an A edge,
  // while the public `input_buttons_pressed` lane stays in raw Slippi button space.
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  return ((buttons_pressed & (uint16_t)MSL_BUTTON_Z) != 0u)
             ? (uint16_t)(buttons_pressed | (uint16_t)MSL_BUTTON_A)
             : buttons_pressed;
}

static inline uint8_t dash_anim_end_try_enter_wait_ft_8008A2BC(MslBatch* batch, size_t idx,
                                                               uint16_t action_id_start) {
  if (batch == NULL || action_id_start != (uint16_t)MSL_ACT_DASH ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_DASH) {
    return 0u;
  }

  const uint32_t anim = batch->state.animation_index[idx];
  if (anim == 0xFFFFFFFFu || anim > 0xFFFFu) {
    return 0u;
  }
  if (!anim_finished(batch->state.char_id[idx], (uint16_t)anim, batch->state.anim_frame_f32[idx])) {
    return 0u;
  }

  // Decomp callback order:
  // - Fighter_8006A360 runs the current motion state's Anim callback before IASA.
  // - ftCo_Dash_Anim enters Wait through ft_8008A2BC when the Dash animation has no frames
  //   remaining.
  // - The destination Wait_IASA then runs later in the same fighter proc and owns any followup
  //   grounded selector transition, so both motion entries run the shared Fighter_ChangeMotionState
  //   identity bundle (ft_800895E0/x2073) in order.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Anim
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  return 1u;
}

static inline void side_special_reset_ghost_ring_on_main_entry(MslBatch* batch, size_t idx) {
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

static inline void side_special_ground_to_air_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                         const MslCharParams* ch, size_t idx,
                                                         uint16_t grounded_action) {
  if (batch == NULL || ms == NULL || ch == NULL) {
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
  if (grounded_action == (uint16_t)MSL_ACT_FX_SPECIAL_S_START) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_air_start;
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_air_main;
  }
  msl_anim_timebase_enter(batch, idx, batch->state.anim_frame_f32[idx], 1.0f);
}

static inline void side_special_air_to_ground_transition(MslBatch* batch, const MslSpecialMsids* ms,
                                                         const MslCharParams* ch, size_t idx,
                                                         uint16_t air_action) {
  if (batch == NULL || ms == NULL || ch == NULL) {
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
      clamp_absf(batch->state.speed_air_x_self[idx], ch->ground_max_horizontal_velocity);
  if (air_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_START;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_start;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S;
    batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_main;
  }
  msl_anim_timebase_enter(batch, idx, batch->state.anim_frame_f32[idx], 1.0f);
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

static inline void enter_squat_wait_from_anim_end(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: both ftCo_Squat_Anim and ftCo_AttackLw3_Anim call ftCo_800D638C on anim-end, which
  // enters SquatWait (ftCo_MS_SquatWait).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_800D638C
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_WAIT;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t squat_wait_try_dash_or_rv(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx, float stick_x, float stick_y,
                                                uint8_t tilt_timer_x, float facing_dir) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  if (is_dash_flick(c, stick_x, tilt_timer_x)) {
    // Decomp: SquatWait_IASA checks Dash_CheckInput before SquatRv_CheckInput.
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
    } else {
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
      batch->state.dash_x4[idx] = 1u;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      msl_anim_timebase_tick_once(batch, idx);
      batch->state.tilt_timer_x[idx] = 0xFEu;
    }
    return 1u;
  }
  if (stick_y > -c->crouch_release_stick_threshold) {
    // Decomp: SquatRv_CheckInput enters SquatRv when lstick.y > -x94.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_CheckInput
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_SQUAT_RV;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_SQUAT_RV;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    return 1u;
  }
  return 0u;
}

static inline void enter_fall_special_via_ftco_80096900(MslBatch* batch, size_t idx,
                                                        float landing_lag) {
  if (batch == NULL) {
    return;
  }
  // Decomp: SpecialHi and aerial SpecialS end states use ftCo_80096900(..., arg1=1, unk=true),
  // which enters FallSpecial, sets mv.co.fallspecial.xC=1, and consumes remaining jumps:
  // - grounded source state: inline0 calls ftCommon_8007D60C (x1968_jumpsUsed=max_jumps);
  // - airborne source state: inline0 calls ftCommon_UseAllJumps when unk=true.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiLanding_Coll,ftFx_SpecialHiFall_Anim
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D60C,ftCommon_UseAllJumps}
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // ftCo_80096900 enters FallSpecial with Ft_MF_KeepFastFall. The generic motion-table flags do not
  // encode this callsite-specific flag, so restore the pre-entry fastfall bit after the shared
  // Fighter_ChangeMotionState bundle.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::inline0
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
  batch->state.fall_fast[idx] = keep_fastfall;
  batch->state.fallspecial_xc[idx] = 1u;
  batch->state.fallspecial_landing_lag[idx] = landing_lag;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 1u;
  batch->state.jumps_left[idx] = 0u;
}

static inline void enter_specialhi_bound_from_airhi_collision(MslBatch* batch,
                                                              const MslCharParams* ch, size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // Decomp: SpecialAirHi collision can enter the rebound motion state through
  // ftFx_SpecialHiBound_Enter. The motion-state handler calls ftAnim_8006EBA4 immediately and does
  // not convert the fighter to grounded; Bound_Phys/Coll continue to branch on ground_or_air.
  // After motion entry it scales horizontal self velocity by ftFox_DatAttrs.x84.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}
  // Source key: data/characters/{fox,falco}.json `firefox_bound_vel_x`.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_BOUND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_BOUND;
  batch->state.on_ground[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.speed_air_x_self[idx] *= ch->firefox_bound_vel_x;
}

static inline uint8_t ft_check_ground_and_ledge_collision_contact(const MslBatch* batch,
                                                                  size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint32_t flags = batch->state.coll_env_flags[idx];
  if ((flags & (uint32_t)MSL_COLLIDE_FLOOR_MASK) != 0u) {
    return 1u;
  }
  return 0u;
}

static inline void specialhi_apply_air_launch_ownership(MslBatch* batch, size_t idx,
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

static inline uint8_t specialhi_try_ground_launch_from_hold(MslBatch* batch, size_t idx,
                                                            const MslCharParams* ch) {
  if (batch == NULL || ch == NULL) {
    return 0u;
  }
  // This is the grounded branch of the same Hold/HoldAir Anim callback as
  // specialhi_apply_air_launch_ownership(), so use the pre-input stick snapshot here too.
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

static inline float specialhi_collision_angle_to_self_vel(float normal_x, float normal_y,
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

static inline void specialhi_apply_collision_facing_dir(MslBatch* batch, const MslCharParams* ch,
                                                        size_t idx) {
  if (batch == NULL || ch == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) {
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
  const float angle = specialhi_collision_angle_to_self_vel(nx, ny, vx, vy);
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
        // Decomp: both Hold and HoldAir transition into launch strictly on anim end. Grounded
        // launch selection is owned by ftFx_SpecialAirHi_AirToGround; otherwise launch enters via
        // ftFx_SpecialAirHi_Enter after a ground->air common transition.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        //   ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_AirToGround,
        //   ftFx_SpecialAirHi_Enter
        // }
        if (on_ground &&
            specialhi_try_ground_launch_from_hold(batch, idx, msl_char_params(char_id))) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI;
        } else {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
          if (on_ground) {
            // Decomp: the aerial launch fallback from grounded SpecialHiHold calls
            // ftCommon_8007D60C before ftFx_SpecialAirHi_Enter. That helper switches
            // ground_or_air to Air and installs the ECB lock, but it does not clear
            // CollData.floor.index; the carried floor remains visible to the same frame's
            // SpecialAirHi_Coll / mpColl pass.
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiHold_Anim,ftFx_SpecialAirHi_Enter}
            batch->state.on_ground[idx] = 0u;
            batch->state.speed_ground_x_self[idx] = 0.0f;
            batch->state.ecb_lock_timer[idx] = 5u;
          }
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
        if (on_ground &&
            specialhi_try_ground_launch_from_hold(batch, idx, msl_char_params(char_id))) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI;
        } else {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI;
          if (on_ground) {
            // Same ftCommon_8007D60C floor-index carry as grounded SpecialHiHold. HoldAir can
            // become grounded through the collision callback earlier in the frame, then launch
            // through the grounded anim-end fallback without clearing CollData.floor.index.
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
            batch->state.on_ground[idx] = 0u;
            batch->state.speed_ground_x_self[idx] = 0.0f;
            batch->state.ecb_lock_timer[idx] = 5u;
          }
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
      // Data bridge (ISO-extracted): ftFox_DatAttrs.x70 stores launch travel duration.
      // refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs
      // data/characters/{fox,falco}.json::firefox_launch_duration_frames
      const MslCharParams* ch_hi = msl_char_params(char_id);
      const uint8_t launch_done_hi =
          (ch_hi != NULL && ch_hi->firefox_launch_duration_frames > 0u)
              ? (batch->state.action_frame[idx] >= (int16_t)ch_hi->firefox_launch_duration_frames)
              : anim_finished(char_id, ms->specialhi_ground_main, batch->state.anim_frame_f32[idx]);
      if (launch_done_hi) {
        batch->state.action_id[idx] = on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING
                                                : (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL;
        batch->state.animation_index[idx] = on_ground ? (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING
                                                      : (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      // Decomp: SpecialHi and SpecialAirHi share ftFx_SM_SpecialHi (same launch submotion id).
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c
      batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_main;
      const MslCharParams* ch_air_hi = msl_char_params(char_id);
      const uint8_t launch_done_air_hi =
          (ch_air_hi != NULL && ch_air_hi->firefox_launch_duration_frames > 0u)
              ? (batch->state.action_frame[idx] >=
                 (int16_t)ch_air_hi->firefox_launch_duration_frames)
              : anim_finished(char_id, ms->specialhi_ground_main, batch->state.anim_frame_f32[idx]);
      if (launch_done_air_hi) {
        batch->state.action_id[idx] = on_ground ? (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING
                                                : (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL;
        batch->state.animation_index[idx] = on_ground ? (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING
                                                      : (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_LANDING:
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING;
      // Decomp: ftFx_SpecialHiLanding_Coll falls back to FallSpecial when the state becomes airborne.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Coll
      if (!on_ground) {
        const MslCharParams* ch = msl_char_params(char_id);
        enter_fall_special_via_ftco_80096900(
            batch, idx, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f);
        return 1;
      }
      // Decomp: ftFx_SpecialHiLanding_Anim transitions to Wait on anim end.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
      if (anim_finished(char_id, (uint16_t)MSL_SM_FX_SPECIAL_HI_LANDING,
                        batch->state.anim_frame_f32[idx])) {
        // Decomp ordering: ftFx_SpecialHiLanding_Anim enters Wait during the Anim callback, then the
        // destination Wait input callback can run later in the same Fighter proc. Return 0 after the
        // motion change so the grounded locomotion IASA tail below can consume Walk/Squat/Turn.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        return 0;
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_FALL:
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_FALL;
      // Decomp: ftFx_SpecialHiFall_Anim transitions to FallSpecial on anim end.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Anim
      if (anim_finished(char_id, (uint16_t)MSL_SM_FX_SPECIAL_HI_FALL,
                        batch->state.anim_frame_f32[idx])) {
        const MslCharParams* ch = msl_char_params(char_id);
        enter_fall_special_via_ftco_80096900(
            batch, idx, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f);
      }
      return 1;
    case MSL_ACT_FX_SPECIAL_HI_BOUND:
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_BOUND;
      // Decomp: ftFx_SpecialHiBound_Anim enters FallSpecial on anim end while airborne and consumes
      // all jumps (`x1968_jumpsUsed = max_jumps`).
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiBound_Anim
      if (!on_ground && anim_finished(char_id, (uint16_t)MSL_SM_FX_SPECIAL_HI_BOUND,
                                      batch->state.anim_frame_f32[idx])) {
        const MslCharParams* ch = msl_char_params(char_id);
        enter_fall_special_via_ftco_80096900(
            batch, idx, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f);
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

static inline uint8_t landing_contact_y_owner_matches_source(uint16_t source_act,
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
       source_act == (uint16_t)MSL_ACT_JUMP_B || source_act == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
       source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END ||
       ((source_act == (uint16_t)MSL_ACT_LANDING) && landing_basic_prev))) {
    return 1u;
  }

  return 0u;
}

static inline uint8_t landing_action_owns_root_floor_snap(uint16_t land_act) {
  switch (land_act) {
    case (uint16_t)MSL_ACT_WAIT:
    case (uint16_t)MSL_ACT_LANDING:
    case (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL:
    case (uint16_t)MSL_ACT_LANDING_AIR_N:
    case (uint16_t)MSL_ACT_LANDING_AIR_F:
    case (uint16_t)MSL_ACT_LANDING_AIR_B:
    case (uint16_t)MSL_ACT_LANDING_AIR_HI:
    case (uint16_t)MSL_ACT_LANDING_AIR_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t landing_contact_is_ledge_floor(const MslBatch* batch, size_t idx, size_t bi) {
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

static inline float landing_root_y_from_mpcoll_contact(const MslBatch* batch, size_t idx,
                                                       size_t bi) {
  // Decomp owner:
  // - mpLib_8004DD90_Floor applies a +0.0001 root/floor bias to its returned correction.
  // - Some lite mpColl helper paths store the floor plane in `ground_contact_y`; direct DD90 paths
  //   store the already-biased projected root. Normalize the scratch lane before Landing* entry so
  //   the post-collision state does not apply the DD90 bias twice.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  static const float k_mplib_floor_y_bias = 0.0001f;
  if (batch == NULL || batch->state.ground_id[idx] == 0xFFFFu) {
    return batch != NULL ? batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias : 0.0f;
  }

  const uint32_t stage_id = batch->state.stage_id[bi];
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  if (stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
      stage_collision_floor_line_height_platform_state_is_source_trusted(
          batch, (int)bi, batch->state.ground_id[idx])) {
    // grIzumi height-platform collision publishes the transformed floor plane through the current
    // mpColl result; Landing* entry still owns the final mpLib_8004DD90_Floor root bias.
    //
    // Source/data owner:
    // - grIzumi refreshes height-platform JObjs before the fighter map callback.
    // - MSLSTG01 platform_transforms(kind=height) identifies the line as a live grIzumi height
    //   owner, and `stage_fod_platform_height_source` marks the current sparse-seed contact/source
    //   lane as trusted for this frame.
    // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  MslStageFloorLine world = {0};
  if (!stage_collision_floor_line_world(batch, (int)bi, &g->lines[(size_t)line_idx], &world)) {
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  float floor_y = world.y0;
  if (fabsf(world.x1 - world.x0) > 0.0001f) {
    floor_y = world.y0 + ((world.y1 - world.y0) * (batch->state.pos_x[idx] - world.x0) /
                          (world.x1 - world.x0));
  }

  const float contact_y = batch->state.ground_contact_y[idx];
  const float dist_to_floor = fabsf(contact_y - floor_y);
  const float dist_to_biased_floor = fabsf(contact_y - (floor_y + k_mplib_floor_y_bias));
  if (dist_to_biased_floor < dist_to_floor) {
    return contact_y;
  }
  if (dist_to_floor <= k_mplib_floor_y_bias) {
    return contact_y + k_mplib_floor_y_bias;
  }
  return contact_y;
}

static inline uint8_t action_uses_common_air_walljump_callback(uint16_t a) {
  // These common air states route their Coll callbacks through ft_081B helpers that call
  // ftWallJump_8008169C after the floor callback declines.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_COMMON_AIR_WALLJUMP_COLL);
}

static inline void align_passivewalljump_entry_x(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t cid = batch->state.char_id[idx];
  const uint16_t ecb_frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  MslEcbWorldPoints ecb = {0};
  msl_ecb_world_points_sample(&ecb, cid, batch->state.animation_index[idx], ecb_frame, facing_dir,
                              batch->state.pos_x[idx], batch->state.pos_y[idx], 0u);

  float transn[3] = {0.0f, 0.0f, 0.0f};
  if (anim_pose_get_transn(cid, (uint16_t)MSL_SM_PASSIVE_WALL_JUMP, 0u, transn) != 0) {
    transn[2] = 0.0f;
  }
  const MslCharParams* ch = msl_char_params(cid);
  const float model_scaling =
      (ch != NULL && isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling
                                                                              : 1.0f;
  const float scale_y =
      (batch->state.fighter_scale_y[idx] > 0.0f) ? batch->state.fighter_scale_y[idx] : 1.0f;
  // ftCo_800C1E64 consumes fp->x68C_transNPos.z after Fighter_ChangeMotionState has run the
  // TransN JObj through ftAnim_8006E054. That path scales raw TransN by ftCommon_GetModelScale
  // before the wall-entry root snap.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E054
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800869D4
  const float transn_z = transn[2] * scale_y * model_scaling;

  const uint32_t env = batch->state.coll_env_flags[idx];
  if ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u) {
    const float anchor_x =
        (batch->state.wall_id[idx] != 0xFFFFu) ? batch->state.wall_contact_x[idx] : ecb.left_x;
    batch->state.pos_x[idx] = anchor_x + transn_z * facing_dir;
  } else if ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) {
    batch->state.pos_x[idx] = ecb.right_x + transn_z * facing_dir;
  }
}

static inline uint8_t try_common_air_walljump_post_collision(MslBatch* batch,
                                                             const MslCommonParams* c,
                                                             const MslCharParams* ch, size_t idx,
                                                             uint16_t a) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  if (!action_uses_common_air_walljump_callback(a)) {
    return 0u;
  }
  const uint32_t env = batch->state.coll_env_flags[idx];
  uint8_t right_hug = (env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) ? 1u : 0u;
  uint8_t left_hug = (env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) ? 1u : 0u;
  const int8_t seeded_wall_side = batch->state.walljump_wall_side_i8[idx];
  if (!right_hug && !left_hug && batch->state.walljump_seed_phase_valid[idx]) {
    // One-step replay bridge only: Slippi exposes the hidden walljump timer/side but not all
    // CollData wall-hug phase needed by the target callback. clear_seed_owned_transients_post_frame
    // drops this authority after the reseeded step, so rollout carry still requires live WallHug.
    // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    if (seeded_wall_side < 0) {
      right_hug = 1u;
    } else if (seeded_wall_side > 0) {
      left_hug = 1u;
    }
  }
  if (!right_hug && !left_hug) {
    // Decomp: ftWallJump_8008169C's outer gate is the current CollData WallHug bit. Hidden timer
    // state may satisfy the later timer/side checks, but it must not synthesize current wall hug.
    // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    batch->state.walljump_input_timer[idx] = 254u;
    return 0u;
  }
  if (!(ch->walljump_setup_x_delta_threshold > 0.0f) || !(c->walljump_input_window_frames > 0.0f)) {
    return 0u;
  }

  // Immediate ftWallJump setup branch:
  // - when the wall-side changed or the hidden timer is stale, ftWallJump measures
  //   ABS(fp->pos_delta.x - wall_pos.x) against fp->co_attrs.x148 and seeds
  //   wall_jump_input_timer=0 for this same callback.
  // - The final branch then requires stick-away and x670 freshness before entering
  //   PassiveWallJump through ftCo_800C1E64(..., p_ftCommonData->x774, ...).
  // Reseeds can carry the hidden multi-frame timer/side when Slippi-visible history proves it;
  // otherwise runtime starts the timer only from the immediate decomp setup branch above.
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
  // refs/melee/src/melee/ft/types.h (co_attrs.x148, x670_timer_lstick_tilt_x)
  const int8_t wall_side = right_hug ? (int8_t)-1 : (int8_t)1;
  uint8_t input_timer = batch->state.walljump_input_timer[idx];
  if (input_timer < 254u && seeded_wall_side == wall_side) {
    input_timer = (input_timer < 253u) ? (uint8_t)(input_timer + 1u) : 254u;
  } else {
    // Decomp: ftWallJump_8008169C compares fp->pos_delta.x, which Fighter_8006A360 updates at
    // frame start from the previous post-frame position before current physics/mpColl projection.
    // Using the post-collision position here lets wall projection itself satisfy co_attrs.x148 and
    // starts the walljump phase one callback early on FD side-wall rows.
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    const float prev_post_x = isfinite(batch->state.floor_sweep_prev_pos_x[idx])
                                  ? batch->state.floor_sweep_prev_pos_x[idx]
                                  : batch->state.prev_pos_x[idx];
    const float pos_delta_x = batch->state.prev_pos_x[idx] - prev_post_x;
    if (!(fabsf(pos_delta_x) > ch->walljump_setup_x_delta_threshold)) {
      batch->state.walljump_input_timer[idx] = 254u;
      return 0u;
    }
    input_timer = 0u;
    batch->state.walljump_wall_side_i8[idx] = wall_side;
  }
  batch->state.walljump_input_timer[idx] = input_timer;
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const uint8_t stick_away = right_hug ? (uint8_t)(stick_x >= c->walljump_stick_x_threshold)
                                       : (uint8_t)(stick_x <= -c->walljump_stick_x_threshold);
  if (!(input_timer < (uint8_t)c->walljump_input_window_frames) || !stick_away ||
      !((float)batch->state.tilt_timer_x[idx] < c->walljump_tilt_x_max_frames)) {
    return 0u;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_PASSIVE_WALL_JUMP;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.hitstun[idx] = 0u;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.passivewall_timer[idx] = (uint8_t)c->walljump_startup_timer_frames;
  batch->state.passivewall_jump_latch[idx] = 0u;
  batch->state.tilt_timer_x[idx] = 0xFEu;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.colanim_timer_x1990[idx] = c->colanim_passivewall_x1990_frames;
  batch->state.colanim_hit_status_x198c[idx] = 2u;
  batch->state.hurtbox_state[idx] = 2u;
  if (right_hug) {
    batch->state.facing[idx] = 1u;
  } else {
    batch->state.facing[idx] = 0u;
  }
  batch->state.walljump_input_timer[idx] = 254u;
  align_passivewalljump_entry_x(batch, idx);
  return 1u;
}

static inline uint8_t attackair_cstick_edge(const MslCommonParams* c, int8_t prev_cx,
                                            int8_t prev_cy, int8_t cx, int8_t cy) {
  // Decomp: ftCo_800DF478 is a C-stick *edge* (threshold crossing) helper used by AttackAir input
  // checks. It compares current vs prior C-stick against p_ftCommonData->xDC/xE0.
  //
  // Input lane ownership:
  // - Fighter proc input preprocessing zeros cstick axes inside p_ftCommonData->x0/x4 before
  //   IASA callbacks run.
  // - ftCo_800DF478 then reads fp->input.cstick{1,0} from that deadzoned lane.
  // refs/melee/src/melee/ft/fighter.c (input deadzone preprocessing at fp->input.cstick.*)
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF478
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF478
  if (c == NULL) {
    return 0;
  }
  const float prev_x = apply_deadzone(stick_i8_to_unit(prev_cx), c->lstick_deadzone_x);
  const float prev_y = apply_deadzone(stick_i8_to_unit(prev_cy), c->lstick_deadzone_y);
  const float x = apply_deadzone(stick_i8_to_unit(cx), c->lstick_deadzone_x);
  const float y = apply_deadzone(stick_i8_to_unit(cy), c->lstick_deadzone_y);
  if ((fabsf(prev_x) < c->attackair_stick_deadzone_x &&
       fabsf(x) >= c->attackair_stick_deadzone_x) ||
      (fabsf(prev_y) < c->attackair_stick_deadzone_y &&
       fabsf(y) >= c->attackair_stick_deadzone_y)) {
    return 1;
  }
  return 0;
}

static inline uint8_t locomotion_kneebend_prepass_attackair_input_active(const MslBatch* batch,
                                                                         const MslCommonParams* c,
                                                                         size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  // In an active catch-connect window, only the Jump-family AttackAir IASA subpath needs to run in
  // the global KneeBend Anim prepass. Plain JumpF/B rows can be resolved by the later catch-connect
  // owner; broadening them through the prepass changes ordinary Catch/CatchDash victim outcomes.
  // Source path: ftCo_KneeBend_Anim -> ftCo_Jump_Enter -> ftCo_Jump_IASA ->
  // ftCo_AttackAir_CheckInput.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_KneeBend.c,ftCo_Jump.c,ftCo_AttackAir.c}
  if (attackair_cstick_edge(c, batch->state.prev_input_c_x[idx], batch->state.prev_input_c_y[idx],
                            batch->state.input_c_x[idx], batch->state.input_c_y[idx]) != 0u) {
    return 1u;
  }
  const uint16_t pressed =
      source_x668_button_edges_with_z_a(batch->state.input_buttons_pressed[idx]);
  return ((pressed & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_Z)) != 0u) ? 1u : 0u;
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

uint8_t locomotion_attackair_try_enter_from_air_iasa(MslBatch* batch, const MslCommonParams* c,
                                                     size_t idx) {
  // Common airborne AttackAir IASA owner:
  // - ftCo_AttackAir_CheckInput consumes A-pressed or c-stick edge and enters AttackAir directly
  //   via Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall, ...), preserving airborne momentum.
  // - This helper is reused by Fall/Jump/JumpAerial callers in locomotion and by DamageFly's
  //   post-lockout DamageFall_IASA-shaped lane.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
  //   ftCo_AttackAir_CheckItemThrowInput,ftCo_AttackAir_EnterFromMsid
  // }
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint16_t a0 = batch->state.action_id[idx];
  // Special fall should not be interruptible into aerial attacks.
  if (a0 == (uint16_t)MSL_ACT_FALL_SPECIAL || a0 == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
      a0 == (uint16_t)MSL_ACT_FALL_SPECIAL_B) {
    return 0;
  }

  const uint16_t pressed =
      source_x668_button_edges_with_z_a(batch->state.input_buttons_pressed[idx]);
  const uint8_t c_edge =
      attackair_cstick_edge(c, batch->state.prev_input_c_x[idx], batch->state.prev_input_c_y[idx],
                            batch->state.input_c_x[idx], batch->state.input_c_y[idx]);
  // Decomp input synthesis maps raw Z into held HSD_PAD_A before x668 edge construction; every
  // caller of ftCo_AttackAir_CheckItemThrowInput reads `fp->input.x668 & HSD_PAD_A`, so the
  // Z-as-A lane is common to Fall, Jump, JumpAerial, DamageFall, and related airborne IASA owners.
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckItemThrowInput
  const uint8_t attack_pressed = ((pressed & (uint16_t)MSL_BUTTON_A) != 0u) ? 1u : 0u;
  if (!attack_pressed && !c_edge) {
    return 0;
  }

  float stick_x = 0.0f;
  float stick_y = 0.0f;
  if (c_edge) {
    // Decomp: ftCo_AttackAir_GetMsidFromCStick consumes preprocessed fp->input.cstick lanes (same
    // deadzone-preprocessing owner as ftCo_800DF478).
    // refs/melee/src/melee/ft/fighter.c (input deadzone preprocessing at fp->input.cstick.*)
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_GetMsidFromCStick
    stick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
    stick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
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
  // ftCo_AttackAir_EnterFromMsid clears fp->allow_interrupt before Fighter_ChangeMotionState.
  // Keep the raw Slippi fp+0x2218 bit in sync even when the same frame's ProcessHit overwrites the
  // visible action with Damage* before state_flags.c can sample the transient AttackAir state.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t)~MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp entry immediately calls `ftAnim_8006EBA4`, so the same frame's Phys/Coll callbacks
  // see the first AttackAir pose/ECB rather than the raw frame-0 motion-entry pose.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
  msl_anim_timebase_tick_once(batch, idx);
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
  return (msl_absf(prev_x) < c->cstick_smash_threshold &&
          msl_absf(cur_x) >= c->cstick_smash_threshold)
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

static inline uint16_t grounded_a_attack_select_action(
    const MslCommonParams* c, uint16_t buttons_pressed, uint8_t cstick_side_edge,
    uint8_t cstick_up_edge, uint8_t cstick_down_edge, float stick_x, float stick_y,
    uint8_t tilt_timer_x, uint8_t tilt_timer_y, float facing_dir, uint8_t allow_attack_dash,
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
  // Decomp input synthesis maps raw Z into `held_inputs |= HSD_PAD_LR | HSD_PAD_A` before
  // `input.x668` is built. Grounded Attack* checks read `fp->input.x668 & HSD_PAD_A`, so a fresh Z
  // edge is also an A-edge for these selectors. This is especially visible in SquatWait_IASA, which
  // has no Catch check before AttackLw3 and therefore lets Z+down enter down-tilt before GuardOn.
  // refs/melee/src/melee/ft/fighter.c:1868-1896
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_CheckInput
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
  const uint8_t a_pressed =
      ((buttons_pressed & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_Z)) != 0u) ? 1u : 0u;
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
    // Decomp AttackS3 decideAngle split:
    // - after the forward+|angle|<x20 gate, decideAngle chooses:
    //   angle > x9C -> AttackS3Hi
    //   angle > xA0 -> AttackS3HiS
    //   angle < xA8 -> AttackS3Lw
    //   angle < xA4 -> AttackS3LwS
    //   else AttackS3S
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::{
    //   ftCo_AttackS3_CheckInput,decideAngle
    // }
    if (ang > c->attack_s3_hi_angle_radians) {
      return (uint16_t)MSL_ACT_ATTACK_S3_HI;
    }
    if (ang > c->attack_s3_hi_s_angle_radians) {
      return (uint16_t)MSL_ACT_ATTACK_S3_HI_S;
    }
    if (ang < c->attack_s3_lw_angle_radians) {
      return (uint16_t)MSL_ACT_ATTACK_S3_LW;
    }
    if (ang < c->attack_s3_lw_s_angle_radians) {
      return (uint16_t)MSL_ACT_ATTACK_S3_LW_S;
    }
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
    case MSL_ACT_ATTACK_100_START:
      return (uint32_t)MSL_SM_ATTACK_100_START;
    case MSL_ACT_ATTACK_100_LOOP:
      return (uint32_t)MSL_SM_ATTACK_100_LOOP;
    case MSL_ACT_ATTACK_100_END:
      return (uint32_t)MSL_SM_ATTACK_100_END;
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
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_ATTACK_S3);
}

static inline uint8_t action_is_attack_s4_family(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_ATTACK_S4);
}

static inline uint8_t grounded_attack_wait_iasa_specials_action(uint16_t action_id) {
  // Generated IASA-owner subset for grounded Attack* callbacks whose source path reaches the
  // Wait_IASA grounded-special/attack preamble after `fp->allow_interrupt`.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_ATTACK_WAIT_IASA_SPECIALS)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
  return msl_motion_state_common_class_has(action_id,
                                           MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS);
}

static inline uint8_t grounded_attack_wait_iasa_interrupt_dest_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_TURN:
    case MSL_ACT_DASH:
    case MSL_ACT_SQUAT:
    case MSL_ACT_KNEE_BEND:
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_ESCAPE_N:
    case MSL_ACT_CATCH:
    case MSL_ACT_CATCH_DASH:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t grounded_attack_wait_iasa_locomotion_action(uint16_t action_id) {
  // Generated IASA-owner subset for grounded Attack* callbacks whose source path can delegate into
  // Wait_IASA's jump/dash/squat/turn/walk tail after `fp->allow_interrupt`.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  return msl_motion_state_common_class_has(action_id,
                                           MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION);
}

static inline uint8_t grounded_attack_wait_iasa_catch_guard_action(uint16_t action_id) {
  // Generated IASA-owner subset for grounded Attack* callbacks whose source path reaches
  // Wait_IASA's catch/guard checks without the specialized AttackDash/AttackHi3/AttackS4 branches
  // handled separately below.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class
  // GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack13_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
  return msl_motion_state_common_class_has(action_id,
                                           MSL_MS_CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD);
}

static inline uint8_t locomotion_has_opponent_active_catch_connect_window(const MslBatch* batch,
                                                                          int bi, int self_p,
                                                                          int num_players) {
  if (batch == NULL) {
    return 0u;
  }
  // Main per-player KneeBend IASA still needs the opponent catch window predicate when a
  // startup-complete KneeBend was not consumed by the global Anim prepass. The prepass itself must
  // not use this as a suppressor: Fighter_8006A360 runs a victim's startup-complete KneeBend Anim
  // callback before a later Fighter_UnkProcessGrab_8006CA5C catch-connect callback can install
  // CapturePulled*. Engine-dump QGD 5247 shows KneeBend -> JumpF -> AttackAirHi before the opposing
  // CatchDash owner enters CatchDashPull.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkProcessGrab_8006CA5C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Coll
  for (int op = 0; op < num_players; op++) {
    if (op == self_p) {
      continue;
    }
    const size_t oidx = msl_idx_player(bi, op);
    const uint16_t oa = batch->state.action_id[oidx];
    if (oa == (uint16_t)MSL_ACT_CATCH_DASH) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t locomotion_try_kneebend_startup_complete_jump_prepass(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, int bi, int p,
    int num_players) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  if (batch->state.on_ground[idx] == 0u ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_KNEE_BEND) {
    return 0u;
  }

  const uint8_t startup_complete =
      (batch->state.action_frame[idx] >= (int16_t)ch->jump_startup_frames) ? 1u : 0u;
  if (!startup_complete) {
    return 0u;
  }
  const uint8_t active_catch_window =
      locomotion_has_opponent_active_catch_connect_window(batch, bi, p, num_players);
  if (active_catch_window != 0u &&
      locomotion_kneebend_prepass_attackair_input_active(batch, c, idx) == 0u) {
    return 0u;
  }

  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

  // Decomp callback-phase ordering:
  // - ftCo_KneeBend_Anim consumes the startup-complete Jump enter in the anim callback phase.
  // - Grounded locomotion IASA chains (Wait/Turn/Dash/Walk) are processed later.
  // Running this Jump-ready KneeBend subset as a pre-pass keeps same-frame global
  // Fighter_ChangeMotionState instance_id consumption aligned before later grounded IASA enters.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{ftCo_KneeBend_Anim,ftCo_KneeBend_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Turn.c,ftCo_Dash.c,ftCo_Walk.c}

  const uint8_t is_short = batch->state.kneebend_is_short_hop[idx] ? 1u : 0u;
  const uint8_t full = (uint8_t)(!is_short);
  const float iasa_stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float iasa_stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float iasa_prev_stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  const uint8_t jump_enter_post_input_tilt_timer_y =
      jump_enter_pre_input_tilt_y_after_input(c, iasa_stick_y, iasa_prev_stick_y);
  const uint16_t iasa_buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float jump_stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
  const uint16_t jump_act = jump_action_from_stick(c, jump_stick_x, facing_dir);
  locomotion_apply_jump_enter_ground_to_air(batch, idx);
  batch->state.action_id[idx] = jump_act;
  batch->state.animation_index[idx] = (uint32_t)submotion_for_action(jump_act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // ftCo_800CB110 scales fp->self_vel.x, not fp->gr_vel. On sloped floors those lanes can differ:
  // Slippi's speed_air_x_self carries self_vel.x while speed_ground_x_self carries gr_vel.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110
  const float base_x =
      batch->state.speed_air_x_self[idx] * ch->ground_to_air_jump_momentum_multiplier;
  float h_vel = base_x + jump_stick_x * ch->jump_h_initial_velocity;
  const float h_max = ch->jump_h_max_velocity;
  if (msl_absf(h_vel) > h_max) {
    h_vel = msl_signf(h_vel) * h_max;
  }
  batch->state.speed_air_x_self[idx] = h_vel;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = full ? ch->jump_v_initial_velocity : ch->hop_v_initial_velocity;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;

  if (escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
    return 1u;
  }
  if (locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
    return 1u;
  }
  const uint8_t jump_aerial_input =
      ((iasa_buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0u ||
       did_tap_jump(c, iasa_stick_y, jump_enter_post_input_tilt_timer_y))
          ? 1u
          : 0u;
  if (locomotion_try_enter_jump_aerial_iasa(batch, c, ch, idx, jump_aerial_input, iasa_stick_x,
                                            facing_dir, 1u)) {
    return 1u;
  }
  batch->state.tilt_timer_y[idx] = jump_enter_post_input_tilt_timer_y;
  return 1u;
}

static inline void dash_to_kneebend_apply_terminal_handoff(MslBatch* batch, const MslCharParams* ch,
                                                           size_t idx, float facing_dir) {
  if (batch == NULL || ch == NULL) {
    return;
  }

  // Dash -> KneeBend velocity handoff:
  // - Dash IASA reaches KneeBend through fn_800CAF78.
  // - Dash Phys owns the dash/run target velocity through getAccelAndTarget.
  // - KneeBend entry does not reset `gr_vel`; its Phys callback then applies ft_80084F3C friction.
  //
  // Keep Dash's target-speed ownership through the same-frame IASA transition before KneeBend's
  // grounded friction runs. Engine-dump rollout confirmation:
  // frame 87->88 of rerun11 enters KneeBend with Dash terminal gr_vel, not the still-super-terminal
  // burst speed from Dash frame 3. A controlled partial-stick variant of that probe (`joystickX=0.5`
  // on the jump input frame) still enters KneeBend at the full Dash terminal gr_vel, not at the
  // stick-scaled Dash Phys target. This is the narrow Dash IASA -> KneeBend bridge; non-Dash
  // KneeBend entries keep existing speed.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_IASA,ftCo_Dash_Phys}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
  const float target = ch->dash_run_terminal_velocity;
  float gr_vel = batch->state.speed_ground_x_self[idx];
  if ((gr_vel * facing_dir) > target) {
    gr_vel = target * facing_dir;
    batch->state.speed_ground_x_self[idx] = gr_vel;
    batch->state.speed_air_x_self[idx] = gr_vel;
  }
}

static inline void enter_fall_keep_fastfall_ftco_fall_enter(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // Decomp: ftCo_Fall_Enter calls Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall, ...), so
  // fp->fall_fast persists across this motion change.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside Fighter_ChangeMotionState)
  batch->state.fall_fast[idx] = keep_fastfall;
}

static inline void ftco_fall_enter_clamp_air_drift_x(MslBatch* batch, const MslCharParams* ch,
                                                     size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  // ftCo_Fall_Enter calls ftCommon_ClampAirDrift after Fighter_ChangeMotionState. Keep this helper
  // on the source owner that currently reaches the validated anim-end path; other local FallEnter
  // wrappers need separate phase-order proof before sharing it.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ClampAirDrift
  float air_x = batch->state.speed_air_x_self[idx];
  if (air_x > ch->air_drift_max) {
    air_x = ch->air_drift_max;
  } else if (air_x < -ch->air_drift_max) {
    air_x = -ch->air_drift_max;
  }
  batch->state.speed_air_x_self[idx] = air_x;
}

static inline uint8_t grounded_a_attack_try_enter_from_iasa(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t buttons_pressed, float stick_x,
    float stick_y, uint8_t tilt_timer_x, uint8_t tilt_timer_y, float facing_dir,
    uint8_t allow_attack_dash, uint8_t allow_tilts) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint8_t c_side_edge = cstick_side_smash_edge(c, batch, idx);
  const uint8_t c_up_edge = cstick_up_smash_edge(c, batch, idx);
  const uint8_t c_down_edge = cstick_down_smash_edge(c, batch, idx);
  const uint16_t attack_button_pressed_mask = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_Z);
  if ((buttons_pressed & attack_button_pressed_mask) == 0 && c_side_edge == 0u && c_up_edge == 0u &&
      c_down_edge == 0u) {
    return 0;
  }

  const uint16_t act = grounded_a_attack_select_action(
      c, buttons_pressed, c_side_edge, c_up_edge, c_down_edge, stick_x, stick_y, tilt_timer_x,
      tilt_timer_y, facing_dir, allow_attack_dash, allow_tilts);
  const uint32_t sm = grounded_attack_submotion_from_action(act);
  if (sm == 0xFFFFFFFFu) {
    return 0;
  }

  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = sm;
  if (act == (uint16_t)MSL_ACT_ATTACK_S4_HI || act == (uint16_t)MSL_ACT_ATTACK_S4_HI_S ||
      act == (uint16_t)MSL_ACT_ATTACK_S4_S || act == (uint16_t)MSL_ACT_ATTACK_S4_LW_S ||
      act == (uint16_t)MSL_ACT_ATTACK_S4_LW) {
    // Decomp: ftCo_AttackS4_CheckInput / ftCo_AttackS4_8008C114 route through decideFighter,
    // which assigns `fp->facing_dir = stick_x_sign` before entering the chosen AttackS4* motion.
    // AttackS4_CheckInput checks A+lstick before the C-stick side-smash helper, so when both are
    // true the current control-stick sign owns facing.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::{
    //   ftCo_AttackS4_CheckInput,ftCo_AttackS4_8008C114,decideFighter
    // }
    const uint8_t a_side_smash =
        (((buttons_pressed & attack_button_pressed_mask) != 0u) &&
         msl_absf(stick_x) >= c->dash_flick_abs && tilt_timer_x < c->dash_flick_tilt_max_frames)
            ? 1u
            : 0u;
    const float smash_stick_x =
        a_side_smash ? stick_x
                     : (c_side_edge ? stick_i8_to_unit(batch->state.input_c_x[idx]) : stick_x);
    batch->state.facing[idx] = (uint8_t)(smash_stick_x >= 0.0f);
  }
  // AttackDash enter helper clears mv.co.attackdash.x0 on motion-state entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::doEnter
  if (act == (uint16_t)MSL_ACT_ATTACK_DASH) {
    batch->state.attackdash_x0[idx] = 0;
  }
  if (act == (uint16_t)MSL_ACT_ATTACK_11 || act == (uint16_t)MSL_ACT_ATTACK_12 ||
      act == (uint16_t)MSL_ACT_ATTACK_13) {
    batch->state.jab_x0[idx] = 0;
    batch->state.jab_rapid_count[idx] = 0;
  } else if (act != (uint16_t)MSL_ACT_ATTACK_100_LOOP) {
    batch->state.attack100_x0[idx] = 0;
    batch->state.attack100_x4[idx] = 0;
  }
  // Decomp attack enters call Fighter_ChangeMotionState(..., anim_start=0, anim_speed=1) then
  // ftAnim_8006EBA4. This slice keeps the common enter timebase call and state-local update logic.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
  return 1;
}

static inline uint8_t dash_early_attack_s4_try_enter_from_iasa(MslBatch* batch,
                                                               const MslCommonParams* c, size_t idx,
                                                               uint16_t buttons_pressed,
                                                               float stick_x, float facing_dir) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Early Dash IASA takes ftCo_AttackS4_8008C114 before the later AttackDash branch:
  // - A-button entry checks current stick against facing direction with p_ftCommonData->x3C.
  // - C-stick entry uses the side-smash edge helper and assigns facing from the C-stick sign.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::{
  //   ftCo_AttackS4_8008C114,checkFacingDir,decideFighter,doEnter}
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF1C8
  const uint8_t c_side_edge = cstick_side_smash_edge(c, batch, idx);
  const uint8_t a_forward = (((buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u) &&
                             (stick_x * facing_dir) >= c->dash_flick_abs)
                                ? 1u
                                : 0u;
  if (!c_side_edge && !a_forward) {
    return 0u;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_S4_S;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_S4;
  if (a_forward) {
    batch->state.facing[idx] = (uint8_t)(facing_dir >= 0.0f);
  } else if (c_side_edge) {
    const float cstick_x = stick_i8_to_unit(batch->state.input_c_x[idx]);
    batch->state.facing[idx] = (uint8_t)(cstick_x >= 0.0f);
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_defer_tick_once(batch, idx);
  return 1u;
}

uint8_t locomotion_grounded_a_attack_try_enter_from_wait_iasa(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t buttons_pressed, float stick_x,
    float stick_y, uint8_t tilt_timer_x, uint8_t tilt_timer_y, float facing_dir) {
  // Wait_IASA attack owner subset shared by other grounded callback bridges:
  // - ftCo_Wait_IASA checks smashes/tilts/jab before guard/jump/dash/squat/turn/walk.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  return grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                               tilt_timer_x, tilt_timer_y, facing_dir, 0u, 1u);
}

static inline uint8_t attackhi3_wait_attack_try_enter(MslBatch* batch, const MslCommonParams* c,
                                                      size_t idx, uint16_t buttons_pressed,
                                                      float stick_x, float stick_y,
                                                      uint8_t tilt_timer_x, uint8_t tilt_timer_y,
                                                      float facing_dir) {
  return locomotion_grounded_a_attack_try_enter_from_wait_iasa(
      batch, c, idx, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y, facing_dir);
}

static inline uint8_t grounded_attack_update(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                             uint8_t char_id, uint16_t buttons_pressed,
                                             float stick_x, float stick_y, uint8_t tilt_timer_x,
                                             uint8_t tilt_timer_y, float facing_dir) {
  if (batch == NULL) {
    return 0;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  const uint32_t forced_sm = grounded_attack_submotion_from_action(action_id);
  if (forced_sm == 0xFFFFFFFFu) {
    return 0;
  }
  // fp+0x2340 AttackDash lane is action-local; keep it zeroed off-lane.
  // refs/melee/src/melee/ft/chara/ftCommon/types.h
  if (action_id != (uint16_t)MSL_ACT_ATTACK_DASH) {
    batch->state.attackdash_x0[idx] = 0;
  }
  if (action_id != (uint16_t)MSL_ACT_ATTACK_11 && action_id != (uint16_t)MSL_ACT_ATTACK_12 &&
      action_id != (uint16_t)MSL_ACT_ATTACK_13) {
    batch->state.jab_x0[idx] = 0;
    batch->state.jab_rapid_count[idx] = 0;
  }
  if (action_id != (uint16_t)MSL_ACT_ATTACK_100_LOOP) {
    batch->state.attack100_x0[idx] = 0;
    batch->state.attack100_x4[idx] = 0;
  }

  // Keep attack submotion stable while in the motion state.
  // refs/melee/src/melee/ft/ftmotionstates.c (Attack* motion-state table rows)
  uint32_t sm = forced_sm;
  batch->state.animation_index[idx] = sm;
  if (sm == 0xFFFFFFFFu || sm > 0xFFFFu) {
    return 1;
  }

  if (action_id == (uint16_t)MSL_ACT_ATTACK_100_LOOP) {
    // Attack100Loop_IASA latches A pressed/released into mv.co.attack100.x4. The pre-input Anim
    // callback consumes script-owned throw_flags_b3 checkpoints before this current-frame input
    // edge can rescue a loop cycle.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_IASA
    const uint16_t buttons_released =
        (uint16_t)(batch->state.prev_input_buttons[idx] & ~batch->state.input_buttons[idx]);
    if ((buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u ||
        (buttons_released & (uint16_t)MSL_BUTTON_A) != 0u) {
      batch->state.attack100_x4[idx] = 1u;
    }
    return 1u;
  }
  if (action_id == (uint16_t)MSL_ACT_ATTACK_100_START ||
      action_id == (uint16_t)MSL_ACT_ATTACK_100_END) {
    return 1u;
  }
  if (anim_finished(char_id, (uint16_t)sm, batch->state.anim_frame_f32[idx])) {
    const uint8_t allow_interrupt_on_anim_end = move_tables_grounded_attack_allow_interrupt(
        char_id, action_id, batch->state.anim_frame_f32[idx]);
    if (action_id == (uint16_t)MSL_ACT_ATTACK_LW3) {
      // Decomp: AttackLw3_Anim exits through ftCo_800D638C (SquatWait). The later input callback
      // dispatches destination SquatWait_IASA in the same Fighter proc, whose order is:
      // specials/attacks -> guard -> jump -> dash -> SquatRv.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::{ftCo_800D638C,ftCo_SquatWait_IASA}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_CheckInput
      enter_squat_wait_from_anim_end(batch, idx);
      if (allow_interrupt_on_anim_end) {
        grounded_attack_carry_allow_interrupt(batch, idx);
      }
      if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                                tilt_timer_x, tilt_timer_y, facing_dir, 0u, 1u)) {
        return 1;
      }
      {
        const uint16_t action_before_guard = batch->state.action_id[idx];
        guard_update_grounded(batch, c, idx, 1u);
        if (batch->state.action_id[idx] != action_before_guard) {
          return 1;
        }
      }
      const MslJumpInput j_in = jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
      if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
        batch->state.kneebend_is_short_hop[idx] = 0;
        return 1;
      }
      (void)squat_wait_try_dash_or_rv(batch, c, idx, stick_x, stick_y, tilt_timer_x, facing_dir);
    } else {
      // Decomp grounded attack anim callbacks resolve to Wait on animation end.
      // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_Attack1.c}
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      if (allow_interrupt_on_anim_end) {
        grounded_attack_carry_allow_interrupt(batch, idx);
      }
    }
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
  const uint16_t action_id = batch->state.action_id[idx];

  // Wait_IASA special subset for the grounded-attack families above:
  // - SpecialS -> SpecialHi -> SpecialN -> SpecialLw are checked before grounded attack restarts.
  // - Down-B remains owned by shine.c; this helper admits the Neutral/Side/Up subset only.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6824,ftCo_800D68C0}
  if (grounded_attack_wait_iasa_specials_action(action_id) &&
      blaster_try_enter_ground_from_iasa_subset(batch, c, idx)) {
    return 1u;
  }
  // Decomp shape:
  // - Grounded Attack* input callbacks gate on fp->allow_interrupt.
  // - After the grounded special subset above, the same Wait_IASA delegation can still reach the
  //   grounded attack restarts and then the locomotion tail.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                            tilt_timer_x, tilt_timer_y, facing_dir, 0, 1)) {
    return 1u;
  }
  if (action_id == (uint16_t)MSL_ACT_ATTACK_LW3) {
    // AttackLw3 has a specialized IASA, not the generic Wait-style chain: it has no guard path,
    // and its crouch terminal is owned by AttackLw3_Anim -> ftCo_800D638C at animation end. Keep
    // the locomotion tail that is present in the source after the AttackLw3 x0 replay latch, but
    // do not let the shared helper's Squat/Guard bridge preempt the terminal AttackLw3 crouch
    // handoff.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::{
    //   ftCo_AttackLw3_Anim,ftCo_AttackLw3_IASA}
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_AttackLw3.s::ftCo_AttackLw3_IASA
    const MslJumpInput j_in = jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
    if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
      batch->state.kneebend_is_short_hop[idx] = 0;
      return 1u;
    }
    if (stick_y < -c->crouch_stick_threshold) {
      // AttackLw3_IASA reaches ftCo_Squat_CheckInput before Turn/Walk. This local helper does not
      // model the AttackLw3 crouch handoff because AttackLw3_Anim also owns the terminal
      // ftCo_800D638C path; stop here instead of letting a down-held row fall through into a later
      // locomotion owner.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
      return 0u;
    }
    if (is_dash_flick(c, stick_x, tilt_timer_x)) {
      if ((stick_x * facing_dir) < 0.0f) {
        batch->state.turn_has_turned[idx] = 0;
        batch->state.turn_frames_to_turn[idx] = 0;
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
    if (walk_check_input_accepts(c, stick_x, facing_dir)) {
      const uint16_t want = walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
      batch->state.action_id[idx] = want;
      batch->state.animation_index[idx] = anim_for_walk_action(want);
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
      msl_anim_timebase_tick_once(batch, idx);
      return 1u;
    }
    return 0u;
  }
  return wait_iasa_locomotion_subset_try_enter(batch, c, ch, idx, buttons, buttons_pressed, stick_x,
                                               stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                               batch->state.action_id[idx]);
}

static inline uint8_t wait_iasa_locomotion_subset_try_enter(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint16_t buttons, uint16_t buttons_pressed, float stick_x, float stick_y, uint8_t tilt_timer_x,
    uint8_t tilt_timer_y, float facing_dir, uint16_t action_id_start) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  // Decomp Wait_IASA locomotion tail:
  // - after catch / specials / grounded attacks / guard, Wait_IASA checks:
  //   Jump -> Dash -> Squat -> Turn -> Walk.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  const MslJumpInput j_in = jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
  if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
    batch->state.kneebend_is_short_hop[idx] = 0;
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

  if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0u &&
      (action_id_start != (uint16_t)MSL_ACT_WAIT || (buttons & (uint16_t)MSL_BUTTON_B) == 0u) &&
      stick_y < -c->crouch_stick_threshold) {
    enter_squat_immediate(batch, idx);
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

  if (walk_check_input_accepts(c, stick_x, facing_dir)) {
    const uint16_t want = walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
    batch->state.action_id[idx] = want;
    batch->state.animation_index[idx] = anim_for_walk_action(want);
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    msl_anim_timebase_tick_once(batch, idx);
    return 1u;
  }

  return 0u;
}

static inline uint8_t common_appeal_try_enter_from_grounded_iasa(MslBatch* batch, size_t idx,
                                                                 uint16_t buttons_pressed) {
  if (batch == NULL) {
    return 0u;
  }
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_D_UP) == 0u) {
    return 0u;
  }

  // Common grounded taunt admission:
  // - Wait/Walk/Turn/Squat/Landing/Ottotto IASA callbacks call ftCo_800DE9D8 after guard and
  //   before jump/dash/locomotion.
  // - Dash and Run/RunDirect also call ftCo_800DE9D8, but use this helper from their branch-local
  //   IASA paths so their earlier attack/guard/special priority remains source-shaped.
  // - ftCo_800DE9B8 gates on `fp->input.x668 & HSD_PAD_DPADUP`, i.e. the pressed-edge lane.
  // - ftCo_800DEAE8 selects AppealSR unless the fighter faces left and the left animation exists.
  //   For Fox/Falco FD, extracted anim data has no common AppealSL anim (end_frame=0), so left
  //   facing common taunts still enter AppealSR.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::{
  //   ftCo_800DE9B8,ftCo_800DE9D8,ftCo_800DEAE8}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  const uint8_t facing_left = batch->state.facing[idx] ? 0u : 1u;
  const uint8_t have_left_anim =
      (msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_APPEAL_SL) > 0.0f) ? 1u : 0u;
  const uint8_t use_left = (uint8_t)(facing_left && have_left_anim);
  batch->state.action_id[idx] =
      use_left ? (uint16_t)MSL_ACT_APPEAL_SL : (uint16_t)MSL_ACT_APPEAL_SR;
  batch->state.animation_index[idx] =
      use_left ? (uint32_t)MSL_SM_APPEAL_SL : (uint32_t)MSL_SM_APPEAL_SR;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  // ftCo_800DEAE8 clears fp->allow_interrupt before Fighter_ChangeMotionState. The replay-visible
  // lane for that state is state_flags[0] bit 0x80 (fp+0x2218_b7); clear it on Appeal entry so
  // stale seeded grounded-attack interrupt state cannot leak onto the taunt destination.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_800DEAE8
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t)~MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
  return 1u;
}

static inline uint8_t common_appeal_update(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id != (uint16_t)MSL_ACT_APPEAL_SR && action_id != (uint16_t)MSL_ACT_APPEAL_SL) {
    return 0u;
  }
  const uint16_t sm = (action_id == (uint16_t)MSL_ACT_APPEAL_SR) ? (uint16_t)MSL_SM_APPEAL_SR
                                                                 : (uint16_t)MSL_SM_APPEAL_SL;
  batch->state.animation_index[idx] = (uint32_t)sm;
  // Decomp: common AppealS Anim exits through ft_8008A2BC when no frames remain.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_Anim
  if (anim_finished(batch->state.char_id[idx], sm, batch->state.anim_frame_f32[idx])) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  }
  // AppealS_IASA is gated by fp->allow_interrupt, but the current Fox/Falco script artifact has no
  // AppealSR/SL command timeline entries and therefore no data-backed allow_interrupt event for
  // this state. Retained source-policy: common Appeal rows stay anim-end-only unless extracted
  // MSLFTSC1 data owns an allow_interrupt event.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_IASA
  // data/scripts/{fox,falco}.bin (MSLFTSC1): no entries for msid 239/240.
  return 1u;
}

void locomotion_update_anim_callbacks_pre_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag[idx] != 0u) {
        continue;
      }
      const uint16_t action_id = batch->state.action_id[idx];
      const uint8_t char_id = batch->state.char_id[idx];
      if (action_id == (uint16_t)MSL_ACT_ATTACK_100_START) {
        // Attack100Start_Anim transitions to Attack100Loop during Fighter_8006A360, before
        // Fighter_procUpdate runs the current-frame IASA callback.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Start_Anim
        if (anim_finished(char_id, (uint16_t)MSL_SM_ATTACK_100_START,
                          batch->state.anim_frame_f32[idx])) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_100_LOOP;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_100_LOOP;
          batch->state.attack100_x0[idx] = 0u;
          batch->state.attack100_x4[idx] = 0u;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      } else if (action_id == (uint16_t)MSL_ACT_ATTACK_100_LOOP) {
        // Attack100Loop_Anim consumes the script-owned throw_flags_b3 checkpoint before the IASA
        // callback can latch current-frame A input into mv.co.attack100.x4.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim
        const float cur_anim = batch->state.anim_frame_f32[idx];
        const float rate = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
        if (cur_anim >= 0.0f && cur_anim < rate) {
          batch->state.attack100_x0[idx] = 1u;
          // Attack100Loop_Anim refreshes attack identity at the loop restart before script hitboxes
          // are interpreted: ft_800892A0 bumps x206C for the same move id and ft_80089824 refreshes
          // the action-state instance bookkeeping. The x206C bump is required so repeated rapid-jab
          // hits can enter the stale queue as separate same-move instances.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim
          // refs/melee/src/melee/ft/ft_0881.c::ft_800892A0
          // refs/melee/src/melee/ft/ft_0892.c::ft_80089824
          attack_identity_restart_same_move_ft_800892A0(batch, idx);
        }
        if (move_tables_attack100_loop_end_check_crossed(
                char_id, batch->state.prev_action_frame[idx], batch->state.action_frame[idx])) {
          if (batch->state.attack100_x0[idx] != 0u && batch->state.attack100_x4[idx] == 0u) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_100_END;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_100_END;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          } else {
            batch->state.attack100_x4[idx] = 0u;
          }
        }
      } else if (action_id == (uint16_t)MSL_ACT_ATTACK_100_END) {
        // Attack100End_Anim resolves through ft_8008A2BC when the ending animation finishes.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100End_Anim
        if (anim_finished(char_id, (uint16_t)MSL_SM_ATTACK_100_END,
                          batch->state.anim_frame_f32[idx])) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      } else if (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                 action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) {
        const uint32_t anim = batch->state.animation_index[idx];
        if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu &&
            anim_finished(char_id, (uint16_t)anim, batch->state.anim_frame_f32[idx])) {
          // JumpAerial_Anim runs before current-frame IASA. On terminal aerial-jump frames it
          // enters FallAerial with Ft_MF_None, clearing fp->fall_fast before FallAerial_IASA can
          // immediately consume AttackAir/EscapeAir input in the same Fighter_procUpdate.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
          //   ftCo_JumpAerial_Anim,ftCo_JumpAerial_IASA}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_Enter
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_AERIAL;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_AERIAL;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        }
      }
    }
  }
}

uint8_t locomotion_wait_iasa_locomotion_subset_try_enter(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint16_t buttons, uint16_t buttons_pressed, float stick_x, float stick_y, uint8_t tilt_timer_x,
    uint8_t tilt_timer_y, float facing_dir, uint16_t action_id_start) {
  // Public wrapper for same-proc callback bridges that enter Wait-like states before the normal
  // locomotion pass reaches this helper, e.g. Cliff option anim-end via ftCommon_8007D92C.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  return wait_iasa_locomotion_subset_try_enter(batch, c, ch, idx, buttons, buttons_pressed, stick_x,
                                               stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                               action_id_start);
}

static inline uint8_t grounded_attack_try_jab_chain_subset(MslBatch* batch, const MslCharParams* ch,
                                                           size_t idx, uint8_t char_id,
                                                           uint16_t action_id_start,
                                                           uint16_t action_id, uint16_t buttons,
                                                           float script_frame) {
  if (batch == NULL) {
    return 0u;
  }
  if (action_id_start != (uint16_t)MSL_ACT_ATTACK_11 &&
      action_id_start != (uint16_t)MSL_ACT_ATTACK_12) {
    return 0u;
  }
  if (action_id != (uint16_t)MSL_ACT_ATTACK_11 && action_id != (uint16_t)MSL_ACT_ATTACK_12) {
    return 0u;
  }
  // Decomp: Attack11_IASA and Attack12_IASA execute checkAttack12/checkAttack13 outside the
  // fp->allow_interrupt gate. Those callbacks belong to motion states that were active at frame
  // start; Attack11 entry via checkAttack11 clears mv.co.attack1.x0 and does not reuse the same
  // A edge for jab-chain intent in the entry frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,ftCo_Attack12_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::checkAttack11
  //
  // checkAttack12/checkAttack13 gates:
  // - requires fp->x2218_b1 ("set jab combo" command timeline),
  // - consumes A-latched intent (mv.co.attack1.x0), where input.x668 is the source edge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
  //
  // Runtime ownership in this lane is command-timeline driven from extracted move events.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
  // data/moves/{fox,falco}.json moves["ftCo_SM_Attack11"]["events"] set_jab_combo
  //
  // Rapid-jab pre-gate:
  // - Attack11/12/13 IASA calls ftCo_Attack_800D6A50 before normal jab-chain checks.
  // - That helper increments fp->x1A54 while A is pressed/released, then enters Attack100Start when
  //   x2218_b2 is active and the per-character co_attrs.rapid_jab_window threshold is reached.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,ftCo_Attack12_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
  // data/characters/{fox,falco}.json::rapid_jab_window
  // data/moves/{fox,falco}.json moves["ftCo_SM_Attack12"]["events"] set_jab_rapid
  const uint16_t source_prev_buttons =
      source_x668_button_edges_with_z_a(batch->state.prev_input_buttons[idx]);
  const uint16_t source_buttons = source_x668_button_edges_with_z_a(buttons);
  const uint16_t source_buttons_pressed = (uint16_t)(source_buttons & ~source_prev_buttons);
  const uint16_t buttons_released = (uint16_t)(source_prev_buttons & ~source_buttons);
  if (((source_buttons_pressed | buttons_released) & (uint16_t)MSL_BUTTON_A) != 0u &&
      batch->state.jab_rapid_count[idx] < 255u) {
    batch->state.jab_rapid_count[idx] = (uint8_t)(batch->state.jab_rapid_count[idx] + 1u);
  }
  if (ch != NULL && ch->rapid_jab_window > 0u &&
      batch->state.jab_rapid_count[idx] >= ch->rapid_jab_window &&
      move_tables_jab_rapid_active(char_id, action_id, script_frame)) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_100_START;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_100_START;
    batch->state.jab_x0[idx] = 0u;
    batch->state.attack100_x0[idx] = 0u;
    batch->state.attack100_x4[idx] = 0u;
    const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
    batch->state.state_flags[flags_i] &=
        (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                             MSL_STATE_FLAG_2218_B2);
    // Attack100Start entry goes through ftCo_800D6B00, which calls ftAnim_8006EBA4
    // immediately after Fighter_ChangeMotionState; the first visible start row is frame 1.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D6B00
    msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
    return 1u;
  }
  const uint8_t jab_combo_active = move_tables_jab_combo_active(char_id, action_id, script_frame);
  // Source input.x668 already folds raw Z into the A bit before Attack11/12 IASA reads it, so the
  // jab intent latch uses the same synthesized edge lane as grounded attack selectors.
  // refs/melee/src/melee/ft/fighter.c:1868-1896
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
  if ((source_buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u) {
    batch->state.jab_x0[idx] = 1u;
  }
  if (batch->state.jab_x0[idx] == 0u || jab_combo_active == 0u) {
    return 0u;
  }

  // doAttack12Normal/doAttack13 clear allow_interrupt and x2218_b1 on entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{doAttack12Normal,doAttack13}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1);
  batch->state.jab_x0[idx] = 0u;
  if (action_id == (uint16_t)MSL_ACT_ATTACK_11) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_12;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_12;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_ATTACK_13;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_ATTACK_13;
  }
  // doAttack12Normal/doAttack13 perform Fighter_ChangeMotionState only (no local ftAnim_8006EBA4),
  // so these transitions enter at frame 0 and do not take an immediate local anim tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{doAttack12Normal,doAttack13}
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  return 1u;
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

static inline uint8_t action_is_phase3_common_grounded_collision_owner(uint16_t a) {
  // Phase 3 common grounded Coll callbacks are generated from MotionState callback identity. Keep
  // this gate narrow: grounded attacks, catch/throw/capture, downed/damage, item, and bespoke
  // specials stay on their retained owners even when they share lower-level mpColl helpers.
  // refs/melee/src/melee/ft/ft_081B.c common grounded wrappers
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class2_bits
  return (uint8_t)(msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_GROUNDED_COLL) ||
                   msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL) ||
                   msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_GROUNDED_B2DC_COLL) ||
                   msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_GROUNDED_B4B0_COLL));
}

static inline uint8_t action_is_ground_locomotion(uint16_t a) {
  if (action_is_phase3_common_grounded_collision_owner(a) ||
      msl_motion_state_common_class_has(a, MSL_MS_CLASS_LANDING_AIR_COLL)) {
    return 1;
  }
  if (a == MSL_ACT_ESCAPE_F || a == MSL_ACT_ESCAPE_B || a == MSL_ACT_ESCAPE_N ||
      a == MSL_ACT_ATTACK_11 || a == MSL_ACT_ATTACK_12 || a == MSL_ACT_ATTACK_13 ||
      a == MSL_ACT_ATTACK_DASH || action_is_attack_s3_family(a) || a == MSL_ACT_ATTACK_HI3 ||
      a == MSL_ACT_ATTACK_LW3 || a == MSL_ACT_ATTACK_S4_HI || a == MSL_ACT_ATTACK_S4_HI_S ||
      a == MSL_ACT_ATTACK_S4_S || a == MSL_ACT_ATTACK_S4_LW_S || a == MSL_ACT_ATTACK_S4_LW ||
      a == MSL_ACT_ATTACK_HI4 || a == MSL_ACT_ATTACK_LW4 || a == MSL_ACT_GUARD_REFLECT) {
    return 1;
  }
  return 0;
}

static inline uint8_t action_is_grounded_guard_state(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_GUARD_ON || a == (uint16_t)MSL_ACT_GUARD ||
                   a == (uint16_t)MSL_ACT_GUARD_OFF || a == (uint16_t)MSL_ACT_GUARD_SET_OFF ||
                   a == (uint16_t)MSL_ACT_GUARD_REFLECT);
}

static inline uint8_t grounded_guard_state_allows_platform_pass_iasa(const MslBatch* batch,
                                                                     size_t idx, uint16_t a) {
  if (a == (uint16_t)MSL_ACT_GUARD_ON || a == (uint16_t)MSL_ACT_GUARD ||
      a == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 1u;
  }
  if (a == (uint16_t)MSL_ACT_GUARD_OFF) {
    // GuardOff_IASA only reaches ftCo_8009A080 while mv.co.guard.x1C is live; GuardSetOff_IASA is
    // empty and must not share this platform-pass owner.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardOff_IASA,ftCo_GuardSetOff_IASA}
    return (batch != NULL && batch->state.guard_special_enable_timer_x1c[idx] != 0u) ? 1u : 0u;
  }
  return 0u;
}

static inline uint8_t guardon_powershield_reflect_preempts_platform_pass(const MslBatch* batch,
                                                                         const MslCommonParams* c,
                                                                         size_t idx, uint16_t a) {
  if (batch == NULL || c == NULL || a != (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }

  // GuardOn IASA source order:
  //   inlineC0 -> ftCo_80093694 -> ... -> ftCo_8009A080(platform pass)
  // `ftCo_80093694` enters GuardReflect when the GuardOn raise-shield timer is still inside
  // p_ftCommonData->x2A0 and the current input has an L/R edge. The platform-pass precheck below
  // is a sim ordering convenience, so it must yield to this earlier source owner.
  //
  // Snapshot note matches action.c::guard_update_grounded: no-submotion GuardOn rows can expose
  // action_frame < 0, but this guard.x0 predicate should treat that as the entry-like 0 frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_IASA,ftCo_80093694}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  const uint16_t guard_x0 =
      (batch->state.action_frame[idx] < 0) ? 0u : (uint16_t)batch->state.action_frame[idx];
  return (uint8_t)(guard_x0 < (uint16_t)c->powershield_reflect_window_frames &&
                   (batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0u &&
                   batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames);
}

static inline uint8_t guardon_fresh_entry_from_non_shield_skips_current_iasa(const MslBatch* batch,
                                                                             size_t idx,
                                                                             uint16_t a) {
  if (batch == NULL || a != (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }
  if (!(batch->state.action_frame[idx] < 0 && batch->state.animation_index[idx] == UINT32_MAX)) {
    return 0u;
  }
  const uint16_t prev = batch->state.prev_action_id[idx];
  // Decomp owner:
  // - A non-shield callback can enter GuardOn through ftCo_80091A4C in the current Fighter proc.
  // - That input callback has already been consumed, so the newborn no-submotion GuardOn row must
  //   not immediately run GuardOn_IASA's spotdodge/roll/jump/pass tail.
  // Keep this in sync with action.c::guard_update_grounded's fresh-entry suppression.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0,ftCo_GuardOn_IASA}
  return (uint8_t)(prev != (uint16_t)MSL_ACT_GUARD_ON && prev != (uint16_t)MSL_ACT_GUARD &&
                   prev != (uint16_t)MSL_ACT_GUARD_REFLECT &&
                   prev != (uint16_t)MSL_ACT_GUARD_SET_OFF);
}

static inline uint8_t input_hsd_pad_lr_held_lane(const MslBatch* batch, const MslCommonParams* c,
                                                 size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Decomp source owner:
  // - ftCo_8009A080 tests `fp->input.held_inputs & HSD_PAD_LR`.
  // - Fighter input synthesis maps digital L/R, Z, and analog trigger past deadzone into the
  //   held LR lane before Guard IASA consumes platform pass.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  const uint16_t buttons = batch->state.input_buttons[idx];
  const float trig =
      msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  return (uint8_t)(((buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u ||
                    trig > c->trigger_deadzone)
                       ? 1u
                       : 0u);
}

static inline uint8_t guard_floor_loss_should_missfoot(const MslBatch* batch, size_t idx,
                                                       uint32_t stage_id) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return 0u;
  }
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  MslStageFloorLine world = {0};
  (void)stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &world);
  const MslStageFloorLine* line = &world;
  const float left = (line->x0 < line->x1) ? line->x0 : line->x1;
  const float right = (line->x0 > line->x1) ? line->x0 : line->x1;
  const float x = batch->state.pos_x[idx];
  const uint8_t facing_right = batch->state.facing[idx] ? 1u : 0u;
  // Decomp: GuardOn/Guard/GuardOff/GuardReflect Coll callbacks call ft_800845B4, which routes
  // mpColl ledge-slip floor loss to ftCo_8009F39C only when the open endpoint is behind the
  // fighter's facing direction; otherwise it falls normally.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_Coll,ftCo_Guard_Coll,ftCo_GuardOff_Coll,ftCo_GuardReflect_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_800845B4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  if (x < left && facing_right) {
    return 1u;
  }
  if (x > right && !facing_right) {
    return 1u;
  }
  return 0u;
}

static inline void enter_missfoot_from_ground_floor_loss(MslBatch* batch, const MslCharParams* ch,
                                                         size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_MISS_FOOT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_MISS_FOOT;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.on_ground[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0u;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  if (batch->state.speed_air_x_self[idx] > ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = ch->air_drift_max;
  } else if (batch->state.speed_air_x_self[idx] < -ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = -ch->air_drift_max;
  }
  batch->state.speed_ground_x_self[idx] = 0.0f;
}

static inline uint8_t ottotto_edge_matches_facing(const MslBatch* batch, int bi, uint16_t ground_id,
                                                  uint8_t facing, float pos_x) {
  const uint32_t stage_id = (batch != NULL && bi >= 0) ? batch->state.stage_id[(size_t)bi] : 0u;
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (line_idx < 0) {
    return 1u;
  }
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || (size_t)line_idx >= g->line_count) {
    return 1u;
  }
  MslStageFloorLine world = {0};
  (void)stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &world);
  const MslStageFloorLine* line = &world;

  // Ottotto_Coll chooses the checked floor endpoint from facing_dir: right endpoint when facing
  // right, left endpoint when facing left. Gate direct edge admission the same way so sliding past
  // the opposite endpoint while facing away becomes the common Fall path instead of teeter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_Coll
  return facing ? (uint8_t)(pos_x >= line->x1) : (uint8_t)(pos_x <= line->x0);
}

static inline uint8_t dash_turn_floor_loss_is_source_facing_stage_ledge(const MslBatch* batch,
                                                                        int bi, uint16_t ground_id,
                                                                        uint8_t dash_source_facing,
                                                                        float pos_x) {
  if (batch == NULL || bi < 0) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  // The Turn_IASA -> Dash floor-loss carry is only needed when Dash_Coll is losing a stage ledge
  // floor in the source-facing direction. Do not apply it to generic floor edges: those use the
  // replay-visible facing after Turn has published, and widening this to all floor-loss rows
  // changes existing Yoshi/Dream Land validation paths.
  // data/stages/bin/*.bin::MSLSTG01 ledge floor ids
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
  const int ledge_side = dash_source_facing ? 1 : 0;
  const MslStageFloorLine* ledge_floor = stage_collision_get_ledge_floor_line(stage_id, ledge_side);
  if (ledge_floor == NULL || ledge_floor->segment_i != ground_id) {
    return 0u;
  }
  return ottotto_edge_matches_facing(batch, bi, ground_id, dash_source_facing, pos_x);
}

static inline uint8_t ottotto_edge_point_for_facing(const MslBatch* batch, int bi,
                                                    uint16_t ground_id, uint8_t facing,
                                                    float* x_out, float* y_out) {
  const uint32_t stage_id = (batch != NULL && bi >= 0) ? batch->state.stage_id[(size_t)bi] : 0u;
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (line_idx < 0) {
    return 0u;
  }
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  MslStageFloorLine world = {0};
  (void)stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &world);
  const MslStageFloorLine* line = &world;
  if (facing) {
    if (x_out != NULL) {
      *x_out = line->x1;
    }
    if (y_out != NULL) {
      *y_out = line->y1;
    }
  } else {
    if (x_out != NULL) {
      *x_out = line->x0;
    }
    if (y_out != NULL) {
      *y_out = line->y0;
    }
  }
  return 1u;
}

static inline float ottotto_edge_root_y_for_entry(const MslBatch* batch, int bi, size_t idx,
                                                  uint16_t ground_id, float edge_y) {
  // Decomp owner:
  // - ft_80084280 consumes the current CollData floor result and then ftCo_8009A3C8/8009A410
  //   enters Ottotto. On FoD height-platform rows, grIzumi has already refreshed the live JObj floor
  //   into mpLib before the fighter callback; the current grounded root Y is therefore the
  //   source-owned biased floor root for this callback. Do not recompute and bias the transformed
  //   endpoint again, or sparse source-trusted height-platform rows gain an extra tiny floor offset
  //   that can later miss a landing threshold.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  if (batch != NULL && bi >= 0 && bi < batch->batch_size &&
      stage_collision_floor_line_has_height_platform_transform(batch->state.stage_id[(size_t)bi],
                                                               ground_id) &&
      stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, ground_id)) {
    return batch->state.pos_y[idx];
  }
  return edge_y + 0.0001f;
}

static inline uint8_t locomotion_floor_lines_adjacent_or_equal(const MslStageFloorGraph* g, int a,
                                                               int b) {
  if (g == NULL || a < 0 || b < 0 || (size_t)a >= g->line_count || (size_t)b >= g->line_count) {
    return 0u;
  }
  if (a == b) {
    return 1u;
  }
  const MslStageFloorLine* la = &g->lines[(size_t)a];
  return (uint8_t)(la->prev == b || la->next == b);
}

static inline float ottotto_floor_loss_player_nudge_x(const MslBatch* batch,
                                                      const MslCommonParams* c, int bi, int p) {
  if (batch == NULL || c == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return 0.0f;
  }
  const size_t idx = msl_idx_player(bi, p);
  if (batch->state.stocks[idx] == 0u || batch->state.hitlag_started_frame[idx] != 0u ||
      msl_action_is_grabbed_victim(batch->state.action_id[idx])) {
    return 0.0f;
  }

  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
  if (floor_graph == NULL) {
    return 0.0f;
  }
  const int self_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  if (self_line < 0) {
    return 0.0f;
  }

  const MslCharParams* self = msl_char_params(batch->state.char_id[idx]);
  if (self == NULL) {
    return 0.0f;
  }

  float nudge_x = 0.0f;
  const float self_center_x =
      batch->state.pos_x[idx] + self->pushbox_x * (float)batch->state.facing_dir1[idx];
  const int num_players = (int)batch->config.num_players;
  for (int q = 0; q < num_players; q++) {
    if (q == p) {
      continue;
    }
    const size_t oidx = msl_idx_player(bi, q);
    if (batch->state.stocks[oidx] == 0u || batch->state.on_ground[oidx] == 0u ||
        batch->state.hitlag_started_frame[oidx] != 0u ||
        msl_action_is_grabbed_victim(batch->state.action_id[oidx])) {
      continue;
    }
    const MslCharParams* other = msl_char_params(batch->state.char_id[oidx]);
    if (other == NULL) {
      continue;
    }
    const int other_line = stage_collision_floor_line_index(stage_id, batch->state.ground_id[oidx]);
    if (!locomotion_floor_lines_adjacent_or_equal(floor_graph, self_line, other_line)) {
      continue;
    }

    const float other_center_x =
        batch->state.pos_x[oidx] + other->pushbox_x * (float)batch->state.facing_dir1[oidx];
    const float delta_x = self_center_x - other_center_x;
    if (msl_absf(delta_x) >= self->pushbox_y + other->pushbox_y) {
      continue;
    }
    if (delta_x < 0.0f) {
      nudge_x -= c->player_nudge_x;
    } else if (delta_x > 0.0f) {
      nudge_x += c->player_nudge_x;
    } else if (q < p) {
      nudge_x -= c->player_nudge_x;
    } else {
      nudge_x += c->player_nudge_x;
    }
  }

  return nudge_x;
}

static inline uint8_t action_uses_ottotto_edge_callback(uint16_t a) {
  if (msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_GROUNDED_B4B0_COLL)) {
    // Phase 3 common ft_80084280/mpColl_8004B4B0 owner. The generated callback bit covers Wait,
    // Walk*, RunBrake, Landing, and LandingFallSpecial without a local action-id scan.
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class2_bits
    return 1u;
  }
  if (msl_motion_state_common_class_has(a, MSL_MS_CLASS_LANDING_AIR_COLL)) {
    // Decomp: ftCo_LandingAir_Coll delegates to ftCo_Landing_Coll, which calls ft_80084280.
    // That common collision callback lets ftCo_8009A3C8 consume Collide_Edge and enter Ottotto
    // before the generic Fall handoff.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    return 1u;
  }
  return 0u;
}

static inline uint8_t ft80084280_ottotto_edge_admits(const MslBatch* batch,
                                                     const MslCommonParams* c, int bi, int p,
                                                     uint16_t action_id) {
  if (batch == NULL || c == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players || !action_uses_ottotto_edge_callback(action_id)) {
    return 0u;
  }

  const size_t idx = msl_idx_player(bi, p);
  if ((batch->state.coll_env_flags[idx] & (uint32_t)MSL_COLLIDE_EDGE) == 0u) {
    return 0u;
  }

  // Decomp: ft_80084280 first routes inward xF8_playerNudgeVel through ft_800827A0 instead of
  // ft_80084280_inline. That branch uses mpColl_8004B2DC / mpColl_8004A45C_Floor, which does not
  // set Collide_Edge for ftCo_8009A3C8.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
  const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
  const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
  if (nudge_x != 0.0f && nudge_x * facing_sign < 0.0f) {
    return 0u;
  }

  const int line_idx = stage_collision_floor_line_index(batch->state.stage_id[(size_t)bi],
                                                        batch->state.ground_id[idx]);
  if (line_idx < 0) {
    return 0u;
  }
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(batch->state.stage_id[(size_t)bi]);
  if (g == NULL || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  MslStageFloorLine world = {0};
  (void)stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &world);
  const MslStageFloorLine* line = &world;

  // Decomp: ft_80084280_inline sets coll->lstick_x and calls mpColl_8004B4B0. Its edge fallback
  // (`mpColl_8004A678_Floor`) admits teeter only when the fighter has crossed the endpoint they
  // are facing and the stick is not held hard outward (left edge: x > -0.75, right edge: x < 0.75).
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280_inline
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor}
  const float stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  if (batch->state.facing[idx]) {
    return (uint8_t)(batch->state.pos_x[idx] >= line->x1 && stick_x < 0.75f);
  }
  return (uint8_t)(batch->state.pos_x[idx] <= line->x0 && stick_x > -0.75f);
}

static inline uint8_t action_is_air_locomotion(uint16_t a) {
  if (msl_motion_state_common_class2_has(a, MSL_MS_CLASS2_COMMON_AIRBORNE_COLL)) {
    // Phase 3 common airborne Coll callbacks: Fall/FallSpecial/Jump/JumpAerial/Pass/MissFoot and
    // CliffJump2 route through the generated common owner. AttackAir, EscapeAir, damage, item, and
    // bespoke specials remain explicit retained owners.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090,ft_800831CC,ft_800835B0}
    // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class2_bits
    return 1;
  }
  if (msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_FALL_COLL)) {
    return 1;
  }
  return 0;
}

static inline uint8_t action_uses_ft80082b1c_basic_landing_callback(uint8_t char_id, uint16_t a) {
  // Generated from decomp MotionState collision callback symbols:
  // - Fall_Coll -> ft_800831CC(..., ft_80082B1C)
  // - Jump/JumpAerial_Coll -> ft_800835B0(..., ft_80082B1C)
  // - CliffJump2_Coll -> ft_800835B0(..., ft_80082B1C)
  // - Fox/Falco SpecialAirN* collision callbacks -> ft_80082B1C
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_800831CC,ft_800835B0}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
  return msl_motion_state_class_has(char_id, a, MSL_MS_CLASS_FT80082B1C_BASIC_LANDING_COLL);
}

static inline uint16_t ft80082b1c_basic_landing_action(const MslBatch* batch,
                                                       const MslCommonParams* c, size_t idx,
                                                       uint16_t source_act) {
  if (!action_uses_ft80082b1c_basic_landing_callback(batch->state.char_id[idx], source_act)) {
    return (uint16_t)MSL_ACT_LANDING;
  }
  // ft_80082B1C keeps gentle floor contact in the neutral grounded state when vertical self
  // velocity is above the scaled threshold, otherwise it enters Landing_Enter_Basic.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  const float scale_y = (batch != NULL && batch->state.fighter_scale_y[idx] > 0.0f)
                            ? batch->state.fighter_scale_y[idx]
                            : 1.0f;
  return msl_ftco_80082b1c_enters_wait(c, scale_y, batch->state.speed_y_self[idx])
             ? (uint16_t)MSL_ACT_WAIT
             : (uint16_t)MSL_ACT_LANDING;
}

static inline uint8_t action_is_catch_start_floor_loss(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_CATCH || a == (uint16_t)MSL_ACT_CATCH_DASH) ? 1u : 0u;
}

static inline void enter_fall_from_grounded_floor_loss(MslBatch* batch, const MslCharParams* ch,
                                                       size_t idx) {
  if (batch == NULL || ch == NULL) {
    return;
  }

  // Ground -> Air floor-loss transitions consume the ground jump (jumps_used = 1).
  //
  // Decomp:
  // - ft_80084104 calls ftCo_Fall_Enter when the grounded collision helper reports no floor.
  // - ft_800845B4 leaves the grounded motion on floor loss, entering MissFoot on ledge-slip flags
  //   or Fall otherwise. Explicit ledge-slip owners enter MissFoot before this generic Fall branch.
  // - ftCo_Fall_Enter calls ftCommon_8007D5D4 if starting from GA_Ground. GuardSetOff can also
  //   route through ft_80084104 while SDI is enabled.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0;
  batch->state.ecb_lock_timer[idx] = 10u;
  // `ftCommon_8007D5D4` locks CollData_X130, but this generic grounded floor-loss path does not
  // expose a fresh source desired-ECB owner. Keep desired-bottom ownership clear here; explicit
  // producers such as JumpAerial entry, Damage launch, and replay seed lanes populate the owner
  // separately before later floor/wall callbacks consume it.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;

  // ftCo_Fall_Enter clamps self_vel.x through ftCommon_ClampAirDrift after the motion change.
  // It does not copy the current gr_vel into self_vel.x; ftCommon_8007D5D4 then clears gr_vel.
  // Grounded Phys callbacks can update fp->gr_vel for the post-frame state while the movement
  // self_vel for the current frame remains the old projected value, so floor-loss Fall must source
  // the air velocity from the self-velocity lane, not the post-Phys ground scalar.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_ClampAirDrift,ftCommon_8007D5D4}
  float air_x = batch->state.speed_air_x_self[idx];
  if (air_x > ch->air_drift_max) {
    air_x = ch->air_drift_max;
  } else if (air_x < -ch->air_drift_max) {
    air_x = -ch->air_drift_max;
  }
  batch->state.speed_air_x_self[idx] = air_x;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.on_ground[idx] = 0u;

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t action_is_attackair(uint16_t a) {
  // Generated from decomp MotionState callback symbols ftCo_AttackAir_* for the five common
  // aerial attacks.
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint8_t action_is_grounded_specialn_ft80083f88_floor_loss(uint8_t char_id,
                                                                        uint16_t action_id) {
  enum { MSL_FT_MOVE_ID_SPECIAL_N = 18u };
  // Grounded Fox/Falco SpecialN collision callbacks call ft_80083F88, which routes floor loss
  // through ft_80082708/mpColl_8004B108 and enters Fall. Use generated callback ownership plus the
  // decomp-derived MotionState move_id table for the grounded SpecialN distinction; the broader
  // FT80083F88 class also contains downed/passive callbacks whose floor-loss transitions are owned
  // by separate downed/damage source families.
  //
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialNStart_Coll,ftFx_SpecialNLoop_Coll,ftFx_SpecialNEnd_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class FT80083F88_GROUND_TO_AIR_COLL)
  // data/attack_id/move_id/{fox,falco}.bin (MotionState.move_id == FtMoveId_SpecialN)
  return (uint8_t)(msl_motion_state_class_has(char_id, action_id,
                                              MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL) &&
                   attack_id_move_id_from_action(char_id, action_id) ==
                       (uint16_t)MSL_FT_MOVE_ID_SPECIAL_N);
}

static inline uint16_t attackair_landing_action_for_contact(const MslBatch* batch, size_t idx,
                                                            uint16_t a) {
  if (!action_is_attackair(a)) {
    return 0u;
  }
  // AttackAir_Coll dispatches through ft_80082C74 even on same-frame hitlag contacts; the callback
  // then lets ftCo_LandingAir_EnterWithLag choose LandingAir* vs autocancel Landing from
  // fp->cmd_vars[0]. cmd_vars[0] is modeled from extracted MSLFTSC1 set_cmd_var timelines.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  const uint8_t lag_enabled = move_tables_attackair_cmd0_active(batch->state.char_id[idx], a,
                                                                batch->state.anim_frame_f32[idx]);
  return lag_enabled ? landing_air_action_from_attackair(a) : (uint16_t)MSL_ACT_LANDING;
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

static inline void walk_change_type_ftWalkCommon_800DFEC8(MslBatch* batch, size_t idx,
                                                          uint16_t target_walk_action) {
  if (batch == NULL) {
    return;
  }
  const uint16_t cur_action = batch->state.action_id[idx];
  if (!action_is_walk(cur_action) || !action_is_walk(target_walk_action) ||
      cur_action == target_walk_action) {
    return;
  }

  const uint8_t cid = batch->state.char_id[idx];
  const uint32_t cur_sm = anim_for_walk_action(cur_action);
  const uint32_t dst_sm = anim_for_walk_action(target_walk_action);
  if (cur_sm > 0xFFFFu || dst_sm > 0xFFFFu) {
    return;
  }

  const float cur_cycle = msl_anim_end_frame(cid, (uint16_t)cur_sm);
  const float dst_cycle = msl_anim_end_frame(cid, (uint16_t)dst_sm);
  if (!(cur_cycle > 0.0f) || !(dst_cycle > 0.0f)) {
    return;
  }

  // Decomp walk-type change ownership (ftWalkCommon_800DFEC8 -> ftCo_Walk_Enter):
  // - preserve phase by remapping cur_anim_frame across walk cycle lengths,
  // - then call Walk_Enter(arg8=final_anim_frame), which runs Fighter_ChangeMotionState with
  //   anim_start=arg8 and immediate ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/ftwalkcommon.c::{ftWalkCommon_800DFEC8,ftWalkCommon_800DFCA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
  const float init_anim_frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const int quotient = (int)(init_anim_frame / cur_cycle);  // PPC fctiwz truncation shape.
  const float adjusted = init_anim_frame - (cur_cycle * (float)quotient);
  // Decomp stores the remapped phase as s32 before passing it to Walk_Enter(arg8):
  // final_animFrame = frame * (adjusted_animFrame / float_result)
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFEC8
  const int32_t final_anim_frame = (int32_t)(dst_cycle * (adjusted / cur_cycle));

  batch->state.action_id[idx] = target_walk_action;
  batch->state.animation_index[idx] = dst_sm;
  msl_anim_timebase_enter(batch, idx, (float)final_anim_frame, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static inline float walk_anim_rate_divisor_for_action(const MslCharParams* ch,
                                                      uint16_t walk_action) {
  if (ch == NULL) {
    return 0.0f;
  }
  switch (walk_action) {
    case MSL_ACT_WALK_SLOW:
      return ch->slow_walk_max;
    case MSL_ACT_WALK_MIDDLE:
      return ch->mid_walk_point;
    case MSL_ACT_WALK_FAST:
      return ch->fast_walk_min;
    default:
      return 0.0f;
  }
}

static inline void walk_anim_rate_ftWalkCommon_800DFDDC(MslBatch* batch, const MslCharParams* ch,
                                                        size_t idx, uint16_t walk_action,
                                                        float facing_dir) {
  if (batch == NULL || ch == NULL || !action_is_walk(walk_action)) {
    return;
  }
  // Decomp walk anim-rate ownership (ftCo_Walk_Anim -> ftWalkCommon_800DFDDC):
  // - callback computes local `mv_x0`, then:
  //     if (mv_x0 * facing_dir <= 0) anim_rate = 0;
  //     else anim_rate = ABS(mv_x0) / walk_divisor.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
  const float mv_x0 = batch->state.speed_ground_x_self[idx];
  float anim_rate = 0.0f;
  if ((mv_x0 * facing_dir) > 0.0f) {
    const float denom = walk_anim_rate_divisor_for_action(ch, walk_action);
    if (denom > 0.0f) {
      anim_rate = msl_absf(mv_x0) / denom;
    }
  }
  batch->state.walk_anim_source_vel[idx] = mv_x0;
  msl_anim_timebase_set_rate(batch, idx, anim_rate);
}

static inline void run_anim_rate_ftCo_Run_Anim(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                               uint16_t run_action) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  if (run_action != (uint16_t)MSL_ACT_RUN && run_action != (uint16_t)MSL_ACT_RUN_DIRECT) {
    return;
  }
  if (!(ch->run_animation_scaling > 0.0f)) {
    return;
  }
  // Decomp Run anim-rate ownership:
  // - ftCo_Run_Anim / ftCo_RunDirect_Anim call ftAnim_SetAnimRate(ABS(gr_vel) / run_animation_scaling).
  // - This runs in Fighter_procUpdate motion callbacks after the frame's ftAnim tick, so it owns
  //   the *next* frame's advancement rate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_Anim
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_SetAnimRate
  const float vx = batch->state.speed_ground_x_self[idx];
  const float rate = msl_absf(vx) / ch->run_animation_scaling;
  batch->state.run_anim_source_vel[idx] = vx;
  msl_anim_timebase_set_rate(batch, idx, rate);
}

static inline uint16_t jump_action_from_stick(const MslCommonParams* c, float stick_x,
                                              float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
  // (lstick.x * facing_dir) > -p_ftCommonData->x78 ? JumpF : JumpB
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? MSL_ACT_JUMP_F : MSL_ACT_JUMP_B;
}

static inline void locomotion_apply_jump_enter_ground_to_air(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Ground-to-air and air-jump common entry paths route through ftCommon_8007D5D4 before the
  // destination motion-state change. That call locks CollData_X130 and preserves the current
  // desired_ecb.bottom for later mpColl_LoadECB_inline calls; JumpAerial from Fall therefore keeps
  // the Fall callback's desired bottom while its current/top/side ECB refreshes from JumpAerial.
  // Keep jump/pass impulse, self-velocity, and jumps-used ownership in callers because each entry
  // owns different followup lanes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  const uint8_t char_id = batch->state.char_id[idx];
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  MslEcbWorldPoints desired_ecb = {0};
  msl_ecb_world_points_sample(&desired_ecb, char_id, anim, frame, facing_dir,
                              batch->state.pos_x[idx], batch->state.pos_y[idx], 0u);
  msl_ecb_world_points_preserve_locked_desired_bottom_rel_y(
      &desired_ecb, batch->state.pos_x[idx], batch->state.pos_y[idx],
      batch->state.coll_desired_ecb_bottom_valid[idx],
      batch->state.coll_desired_ecb_bottom_rel_y[idx]);
  batch->state.coll_desired_ecb_bottom_rel_y[idx] = desired_ecb.bottom_rel_y;
  batch->state.coll_desired_ecb_top_rel_y[idx] = desired_ecb.top_rel_y;
  batch->state.coll_desired_ecb_left_rel_x[idx] = desired_ecb.left_rel_x;
  batch->state.coll_desired_ecb_right_rel_x[idx] = desired_ecb.right_rel_x;
  batch->state.coll_desired_ecb_side_rel_y[idx] = desired_ecb.side_rel_y;
  batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] = 1u;
  batch->state.on_ground[idx] = 0u;
  batch->state.pos_z[idx] = 0.0f;
  batch->state.ecb_lock_timer[idx] = 10u;
}

static inline uint8_t common_pass_input_gate(const MslBatch* batch, const MslCommonParams* c,
                                             size_t idx, float stick_y, uint8_t tilt_timer_y) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t ground_id = batch->state.ground_id[idx];
  return (uint8_t)(ground_id != 0xFFFFu &&
                   stage_collision_floor_line_is_platform(stage_id, ground_id) &&
                   stick_y <= -c->pass_stick_threshold && tilt_timer_y < c->pass_tilt_max_frames);
}

static inline uint8_t squat_pass_countdown_consume_gate(const MslBatch* batch,
                                                        const MslCommonParams* c, size_t idx,
                                                        float stick_y, uint8_t tilt_timer_y) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id) ||
      stick_y > -c->pass_stick_threshold) {
    return 0u;
  }
  if (tilt_timer_y < c->pass_tilt_max_frames) {
    return 1u;
  }

  // Source separates the arming gate from the countdown consumer:
  // - ftCo_80099F9C requires x671 < x468 only when it sets mv.co.pass.x0/x4.
  // - ftCo_Squat_IASA_inline later decrements x4 and enters Pass without rechecking x671.
  // For teacher-forced seeds where mv.co.pass is not serialized, a still-held down input can have
  // advanced by the x470 countdown frames by the time Squat consumes the armed pass owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline
  const uint16_t consume_tilt_window =
      (uint16_t)c->pass_tilt_max_frames + (uint16_t)c->floor_skip_frames;
  return (uint8_t)((uint16_t)tilt_timer_y < consume_tilt_window);
}

static inline uint8_t guardsetoff_platform_edge_floor_loss_before_destination_iasa(
    const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF ||
      batch->state.hitlag[idx] != 0u) {
    return 0u;
  }
  if (!anim_finished(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_DAMAGE,
                     batch->state.anim_frame_f32[idx])) {
    return 0u;
  }

  const size_t bi = idx / (size_t)MSL_MAX_PLAYERS;
  const uint32_t stage_id = batch->state.stage_id[bi];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    return 0u;
  }
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (line_idx < 0 || g == NULL || (size_t)line_idx >= g->line_count) {
    return 0u;
  }

  MslStageFloorLine world = {0};
  (void)stage_collision_floor_line_world(batch, (int)bi, &g->lines[(size_t)line_idx], &world);
  const float gr_vel = batch->state.speed_ground_x_self[idx];
  if (!isfinite(gr_vel) || gr_vel == 0.0f) {
    return 0u;
  }
  const float next_root_x = batch->state.pos_x[idx] + gr_vel;

  // GuardSetOff collision owner:
  // - ftCo_GuardSetOff_Anim can finish shieldstun, but GuardSetOff_Coll still routes through the
  //   grounded collision helper for the source callback pass.
  // - On a soft-platform edge, `ft_80084104`/`ft_800845B4` can leave ground before any destination
  //   Guard IASA defensive option publishes a roll. Check the post-Phys root against the generated
  //   platform endpoint; stable in-span rows continue to the existing destination Guard IASA path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardSetOff_Anim,ftCo_GuardSetOff_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
  // data/stages/*.json (MSLSTG01 floor endpoint metadata)
  if (gr_vel > 0.0f) {
    return (uint8_t)(next_root_x > world.x1);
  }
  return (uint8_t)(next_root_x < world.x0);
}

static inline void common_pass_enter(MslBatch* batch, const MslCommonParams* c,
                                     const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return;
  }
  // Soft-platform Pass entry:
  // - ftCo_8009A228 calls ftCommon_8007D5D4, ftCommon_ClampAirDrift, writes self_vel.y=x46C,
  //   enters ftCo_MS_Pass, and calls mpUpdateFloorSkip.
  // - The floor-skip itself is consumed by mpcoll_ground.c via the carried platform ground_id.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A228,ftCo_Pass_Anim}
  // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
  const uint16_t ground_id = batch->state.ground_id[idx];
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  if (mpcoll_is_on_platform(batch, bi, idx)) {
    msl_mpcoll_update_floor_skip(batch, idx, ground_id);
  }
  locomotion_apply_jump_enter_ground_to_air(batch, idx);
  if (batch->state.speed_air_x_self[idx] > ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = ch->air_drift_max;
  } else if (batch->state.speed_air_x_self[idx] < -ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = -ch->air_drift_max;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_PASS;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_PASS;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = c->pass_vel_y;
  batch->state.jumps_left[idx] = ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0u;
  // Fighter_ChangeMotionState(..., Ft_MF_None) clears fp->fall_fast on Pass entry; ftCo_8009A228
  // then writes x671=0xFE so the held-down pass input cannot immediately relatch fastfall.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A228
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  batch->state.fall_fast[idx] = 0u;
  batch->state.tilt_timer_y[idx] = 0xFEu;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint16_t jump_aerial_action_from_stick(const MslCommonParams* c, float stick_x,
                                                     float facing_dir) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
  return (stick_x * facing_dir) > -c->jump_back_x_threshold ? MSL_ACT_JUMP_AERIAL_F
                                                            : MSL_ACT_JUMP_AERIAL_B;
}

static inline uint8_t locomotion_try_enter_jump_aerial_iasa(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint8_t jump_input, float stick_x, float facing_dir, uint8_t block_from_jump_aerial) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return 0u;
  }
  if (!jump_input) {
    return 0u;
  }
  if (batch->state.jumps_left[idx] == 0u) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (block_from_jump_aerial && (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                                 action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B)) {
    return 0u;
  }

  const uint16_t act = jump_aerial_action_from_stick(c, stick_x, facing_dir);
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t entry_ground_id = batch->state.ground_id[idx];
  const uint8_t entry_ground_is_soft_or_transform =
      (entry_ground_id != 0xFFFFu &&
       (stage_collision_floor_line_is_platform(stage_id, entry_ground_id) ||
        stage_collision_floor_line_has_platform_transform(stage_id, entry_ground_id)))
          ? 1u
          : 0u;
  locomotion_apply_jump_enter_ground_to_air(batch, idx);
  if (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u) {
    // ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4 before the motion-state change. The
    // source owner is a live air-jump CollData_X130 lock, not a teacher-forced seed lane; retain
    // that identity from the current floor metadata so the JumpAerial callback and any same-proc
    // EscapeAir handoff keep consuming the locked desired bottom through mpColl_LoadECB_inline.
    // Do not classify from stage-wide platform presence: a stale hard-floor ground id on a platform
    // stage is not soft-platform source authority.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
    batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
        msl_escapeair_locked_bottom_owner_for_live_jumpaerial_entry(
            entry_ground_is_soft_or_transform);
  }
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = submotion_for_action(act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.speed_air_x_self[idx] = stick_x * ch->air_jump_h_multiplier;
  batch->state.speed_y_self[idx] = ch->jump_v_initial_velocity * ch->air_jump_v_multiplier;
  // Decomp: fp->x671_timer_lstick_tilt_y = 0xFE.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
  batch->state.tilt_timer_y[idx] = 0xFEu;
  batch->state.fall_fast[idx] = 0;
  batch->state.jumps_left[idx]--;
  return 1u;
}

static inline uint8_t run_iasa_has_spacie_b_special_intent(uint8_t char_id,
                                                           uint16_t buttons_pressed) {
  if (char_id != 1u && char_id != 22u) {
    return 0u;
  }
  return ((buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
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

static inline uint8_t walk_check_input_accepts(const MslCommonParams* c, float stick_x,
                                               float facing_dir) {
  // ftCo_Walk_CheckInput delegates to ftWalkCommon_800DFC70, which admits only same-facing
  // horizontal input: fp->input.lstick.x * fp->facing_dir >= p_ftCommonData->x24.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFC70
  return ((stick_x * facing_dir) >= c->walk_stick_threshold) ? 1u : 0u;
}

static inline uint8_t spacie_speciallw_pressed(const MslCommonParams* c, uint8_t char_id,
                                               uint16_t buttons_pressed, float stick_y) {
  // Fox/Falco aerial common IASA checks ftCo_SpecialAir_CheckInput before ftCo_80099A58
  // (EscapeAir). The simulator's Reflector owner runs after locomotion, so local locomotion IASA
  // source-order guards must leave B+down reflector input unconsumed for that later source owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
  return (c != NULL && shine_char_supports_reflector(char_id) &&
          (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u &&
          stick_y <= -c->special_stick_y_threshold)
             ? 1u
             : 0u;
}

static inline uint8_t spacie_speciallw_wait_iasa_pressed_edge(const MslCommonParams* c,
                                                              uint8_t char_id,
                                                              uint16_t buttons_pressed,
                                                              float stick_x, float stick_y) {
  // Wait-style grounded B-special dispatch reaches ftCo_800D68C0. Its source gate is fp->x687,
  // refreshed before callbacks only when ftCo_800D688C sees the B pressed-edge plus down-stick.
  // Keep Side-B precedence by requiring the side-special x threshold to be absent.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D688C,ftCo_800D68C0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkIncrementCounters_8006ABEC
  return (c != NULL && shine_char_supports_reflector(char_id) &&
          (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u &&
          stick_y <= -c->special_stick_y_threshold &&
          fabsf(stick_x) < c->special_stick_x_threshold_side)
             ? 1u
             : 0u;
}

static inline uint8_t jump_enter_pre_input_tilt_y_after_input(const MslCommonParams* c,
                                                              float stick_y, float prev_stick_y) {
  if (c == NULL) {
    return 0xFEu;
  }
  // ftCo_KneeBend_Anim -> ftCo_Jump_Enter writes x671=0xFE before Fighter_Spaghetti refreshes
  // input history. The same-frame input pass only overwrites that transient on a fresh
  // stick-threshold crossing; held-up rows remain 0xFE.
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_KneeBend.c,ftCo_Jump.c}
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
  if (stick_y >= c->lstick_tilt_y_thresh) {
    return prev_stick_y >= c->lstick_tilt_y_thresh ? 0xFEu : 0u;
  }
  if (stick_y <= -c->lstick_tilt_y_thresh) {
    return prev_stick_y <= -c->lstick_tilt_y_thresh ? 0xFEu : 0u;
  }
  return 0xFEu;
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

static inline MslJumpInput jump_input_from_fn_800CAF78(const MslCommonParams* c,
                                                       uint16_t buttons_pressed, float stick_y,
                                                       uint8_t tilt_timer_y) {
  if (buttons_pressed & (uint16_t)MSL_BUTTON_XY) {
    return MSL_JUMP_INPUT_XY;
  }
  // Dash/Run/RunBrake/TurnRun IASA call fn_800CAF78, not ftCo_Jump_GetInput.
  // fn_800CAF78 uses p_ftCommonData->x80 for the stick-y threshold.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
  return (stick_y >= c->dash_run_jump_stick_y_threshold &&
          tilt_timer_y < c->tap_jump_tilt_max_frames)
             ? MSL_JUMP_INPUT_LSTICK
             : MSL_JUMP_INPUT_NONE;
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
  const uint8_t a_up_smash = ((buttons_pressed & (uint16_t)MSL_BUTTON_A) != 0u &&
                              stick_y >= c->attack_hi4_stick_threshold_y)
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
    case MSL_ACT_PASS:
      return (uint32_t)MSL_SM_PASS;
    case MSL_ACT_MISS_FOOT:
      return (uint32_t)MSL_SM_MISS_FOOT;
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
  // Landing-entry root-Y ownership:
  // - Airborne collision callbacks resolve floor contact before entering Landing / LandingAir* /
  //   LandingFallSpecial through ftCommon_8007D7FC + Fighter_ChangeMotionState.
  // - mpLib_8004DD90_Floor projects onto the owning floor line and applies the grounded +0.0001
  //   bias; replay-visible post-frame rows therefore own the collision floor root position on the
  //   destination landing state, not the pre-contact airborne root.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{ftCo_Landing_Enter_Basic,ftCo_LandingFallSpecial_Enter}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  uint8_t apply_contact_y_owner =
      (batch->state.on_ground[idx] && landing_action_owns_root_floor_snap(land_act)) ? 1u : 0u;
  if (apply_contact_y_owner && source_act == (uint16_t)MSL_ACT_FALL &&
      landing_contact_is_ledge_floor(batch, idx, bi)) {
    // Keep edge/walk-off positioning owned by mpColl on ledge floor segments.
    // Decomp shape:
    // - Fall collision callback routes through ft_80082B1C.
    // - mpColl floor-edge snap/ownership is handled in mpColl_8004A45C_Floor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
    apply_contact_y_owner = 0u;
  }
  if (apply_contact_y_owner) {
    batch->state.pos_y[idx] = landing_root_y_from_mpcoll_contact(batch, idx, bi);
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
  landing_entry_carry_raw_allow_interrupt_from_source(batch, idx, source_act, land_act);
  if (land_act == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    // Decomp: LandingFallSpecial carries mv.co.landing.allow_interrupt from its entry helper.
    // EscapeAir_Coll passes false; FallSpecial_Coll forwards the FallSpecial source flag. This
    // runtime path models the suite-owned sources we currently enter explicitly.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    const uint8_t fallspecial_allow = batch->state.landing_fallspecial_allow_interrupt[idx];
    batch->state.landing_fallspecial_allow_interrupt[idx] =
        (source_act == (uint16_t)MSL_ACT_FALL_SPECIAL ||
         source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
         source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_B)
            ? fallspecial_allow
            : 0u;
  } else {
    batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
  }

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
    float speed = 1.0f;
    if (source_act == (uint16_t)MSL_ACT_ESCAPE_AIR ||
        source_act == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
      // Source-specific LandingFallSpecial rate:
      // - EscapeAir_Coll enters LandingFallSpecial with p_ftCommonData->x344.
      // - SpecialAirSEnd_Coll enters LandingFallSpecial with the Illusion/Phantasm landing lag.
      // - FallSpecial_Coll forwards mv.co.fallspecial.landing_lag written by ftCo_80096900.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
      speed = (landing_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / landing_lag) : 1.0f;
    } else if (source_act == (uint16_t)MSL_ACT_FALL_SPECIAL ||
               source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
               source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_B) {
      const float source_lag = batch->state.fallspecial_landing_lag[idx];
      speed = (source_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / source_lag) : 1.0f;
    }
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

static inline uint8_t shieldbreak_down_faces_up(const MslBatch* batch, size_t idx,
                                                uint16_t action_id) {
  // Decomp: `ftCo_80098E3C` chooses ShieldBreakDownU/D via `ftCo_80097570`, which samples the
  // live HipN matrix and checks row[1][1] (or row[1][2] under the alternate orientation flag).
  // The alternate flag is not exposed in current RL1 seed state; for Fox/Falco ShieldBreakFly/Fall
  // rows in the suite, the source path uses the ordinary HipN y-axis predicate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_80098E3C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097570
  enum { MSL_FTPART_HIP_N = 4 };
  uint16_t msid = 0u;
  if (action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_FLY) {
    msid = (uint16_t)MSL_SM_SHIELD_BREAK_FLY;
  } else if (action_id == (uint16_t)MSL_ACT_SHIELD_BREAK_FALL) {
    msid = (uint16_t)MSL_SM_SHIELD_BREAK_FALL;
  } else {
    return 1u;
  }

  float m[12] = {0};
  if (anim_pose_get_collision_matrix_f32(batch, idx, msid, batch->state.anim_frame_f32[idx],
                                         (uint16_t)MSL_FTPART_HIP_N, m) == 0) {
    return (uint8_t)(m[5] > 0.0f);
  }
  return 1u;
}

static inline void enter_shieldbreak_down_from_floor_contact(MslBatch* batch,
                                                             const MslCharParams* ch, size_t idx,
                                                             uint16_t prev_action) {
  // ShieldBreakFly/Fall floor contact:
  // - `ft_80082C74(..., ftCo_80098E3C)` dispatches the landing callback.
  // - `ftCo_80098E3C` refreshes grounded bookkeeping through `ftCommon_8007D7FC` when entered from
  //   air, then changes motion to ShieldBreakDownU/D without an immediate animation tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_ShieldBreakFly_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFall.c::ftCo_ShieldBreakFall_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_80098E3C
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  float gr = batch->state.speed_air_x_self[idx];
  if (ch != NULL) {
    const float gmax = ch->ground_max_horizontal_velocity;
    if (gr > gmax) {
      gr = gmax;
    } else if (gr < -gmax) {
      gr = -gmax;
    }
    batch->state.jumps_left[idx] = ch->max_jumps;
  }
  batch->state.on_ground[idx] = 1u;
  batch->state.speed_ground_x_self[idx] = gr;
  batch->state.speed_air_x_self[idx] = gr;
  batch->state.ecb_lock_timer[idx] = 0u;
  batch->state.hurtbox_state[idx] = 2u;

  const uint8_t down_u = shieldbreak_down_faces_up(batch, idx, prev_action);
  batch->state.action_id[idx] =
      down_u ? (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U : (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D;
  batch->state.animation_index[idx] =
      down_u ? (uint32_t)MSL_SM_SHIELD_BREAK_DOWN_U : (uint32_t)MSL_SM_SHIELD_BREAK_DOWN_D;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
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
      (void)locomotion_try_kneebend_startup_complete_jump_prepass(batch, c, ch, bi, p, num_players);
    }

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
      uint8_t grounded_attack_input_callback_consumed = 0u;

      // Compatibility pending-release bridge guard:
      // - Normal runtime throw release detaches/damages the victim in the thrower's Anim callback.
      //   Seed/reseed pending latches can still install a temporary Fall bridge consumed by
      //   throw_flow_update_post_items().
      // - In decomp, throw release/hit runs inside Throw Anim callback (`ftCo_800DD724` ->
      //   `ftCo_800DDDE4`) before normal victim locomotion IASA has a chance to consume aerial
      //   jump/attack inputs on that same release frame.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        continue;
      }

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

        if (common_appeal_update(batch, idx)) {
          continue;
        }

        // Squat/SquatWait/SquatRv updates.
        //
        // Decomp:
        // - ftCo_Squat_Anim transitions on anim end through ftCo_800D638C into SquatWait.
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
            enter_squat_wait_from_anim_end(batch, idx);
            action_id = (uint16_t)MSL_ACT_SQUAT_WAIT;
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
          const uint8_t speciallw_preempts_squat_iasa =
              // Squat/SquatWait IASA checks grounded special dispatch before AttackLw*/AttackS*
              // branches and before the delayed platform-pass helper. Shine entry is owned by the
              // later grounded-special phase in this runtime; this source-order policy prevents
              // lower-priority Squat consumers from taking B+down before that owner runs.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
              (spacie_speciallw_pressed(c, cid, buttons_pressed, stick_y) &&
               fabsf(stick_x) < c->special_stick_x_threshold_side)
                  ? 1u
                  : 0u;
          if (action_id == MSL_ACT_SQUAT &&
              blaster_try_enter_ground_from_wait_iasa(batch, c, idx)) {
            action_id = batch->state.action_id[idx];
          } else if (!speciallw_preempts_squat_iasa &&
                     grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                           stick_y, tilt_timer_x, tilt_timer_y,
                                                           facing_dir, 0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
            const uint16_t action_before_guard = batch->state.action_id[idx];
            guard_update_grounded(batch, c, idx, 1u);
            action_id = batch->state.action_id[idx];
            if (action_id != action_before_guard) {
              continue;
            }

            const MslJumpInput j_in =
                jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
            if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
              batch->state.kneebend_is_short_hop[idx] = 0;
              action_id = (uint16_t)MSL_ACT_KNEE_BEND;
            } else if (action_id == MSL_ACT_SQUAT &&
                       batch->state.action_frame[idx] > ((int16_t)c->floor_skip_frames + 1) &&
                       !speciallw_preempts_squat_iasa &&
                       squat_pass_countdown_consume_gate(batch, c, idx, stick_y, tilt_timer_y)) {
              // Squat platform pass:
              // ftCo_80099F9C arms mv.co.pass.x4 with p_ftCommonData->x470, then Squat_Anim
              // enters Pass once the countdown reaches zero while still on a platform.
              // SquatWait_IASA calls ftCo_80099F9C but does not call ftCo_Squat_IASA_inline, so
              // an armed countdown is only consumed by the Squat callback owner.
              // Model that hidden countdown with the current Squat action age. The arm helper
              // returns before Squat_IASA_inline decrements x4, and the simulator's action_frame
              // has already advanced for this step. Therefore x470==2 first admits Pass after one
              // extra held frame beyond the raw countdown.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
              common_pass_enter(batch, c, ch, idx);
              action_id = (uint16_t)MSL_ACT_PASS;
            } else if (action_id == MSL_ACT_SQUAT_WAIT &&
                       squat_wait_try_dash_or_rv(batch, c, idx, stick_x, stick_y, tilt_timer_x,
                                                 facing_dir)) {
              action_id = batch->state.action_id[idx];
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

        const uint8_t replay_wait_rng_owner =
            (batch->rollout_clock_rng_owned != NULL &&
             batch->rollout_clock_rng_owned[bi] == (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED)
                ? 1u
                : 0u;
        const uint32_t wait_anim = replay_wait_rng_owner != 0u ? batch->state.animation_index[idx]
                                                               : (uint32_t)MSL_SM_WAIT1_0;
        if (action_id == MSL_ACT_WAIT && wait_anim <= 0xFFFFu &&
            anim_finished(cid, (uint16_t)wait_anim, batch->state.anim_frame_f32[idx])) {
          // Wait_Anim does not simply let the AObj loop carry the visible frame past the end.
          // It calls ftCo_8008A7A8, which restarts the current/selected wait subanimation through
          // ftCo_8008A6D8 / ftAnim_8006EBE8. Character WaitStruct tables provide the weighted
          // submotion choices; getAnimID consumes HSD_Randi(100)+1 for the source selection.
          // This is not Fighter_ChangeMotionState, so it must not run the motion-entry identity
          // bundle (`ft_800895E0` / `plAttack_80037B08`) or bump fp->x2088.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Anim
          // refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,ftCo_8008A6D8,getAnimID}
          // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
          if (replay_wait_rng_owner != 0u) {
            locomotion_consume_deadupstar_effect_prefix_before_wait(batch, bi, p);
          }
          batch->state.animation_index[idx] =
              choose_wait_anim_variant(batch, bi, ch, (uint16_t)batch->state.animation_index[idx]);
          msl_anim_timebase_restart(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_WAIT;
        }

        {
          const uint16_t before_specialhi = action_id;
          if (spacie_specialhi_update(batch, idx, cid, ms, 1u)) {
            continue;
          }
          if (batch->state.action_id[idx] != before_specialhi) {
            action_id = batch->state.action_id[idx];
          }
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
            side_special_reset_ghost_ring_on_main_entry(batch, idx);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            action_id = (uint16_t)MSL_ACT_FX_SPECIAL_S;
          }

          // Main -> End on anim completion (Anim callback).
          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_S &&
              anim_finished(cid, ms->specials_ground_main, batch->state.anim_frame_f32[idx])) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_S_END;
            batch->state.animation_index[idx] = (uint32_t)ms->specials_ground_end;
            // Decomp:
            // - ftFx_SpecialSEnd_Enter writes fp->gr_vel = da->x34 * facing_dir.
            // - Fighter_ChangeMotionState then clamps gr_vel to co_attrs.dash_run_terminal_velocity
            //   when the new motion does not carry root-motion ownership (`!fp->x594_b0` path).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Enter
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            batch->state.speed_ground_x_self[idx] = clamp_absf(
                ch->illusion_ground_end_vel_x * facing_dir, ch->dash_run_terminal_velocity);
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
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            batch->state.speed_ground_x_self[idx] = clamp_absf(
                ch->illusion_ground_end_vel_x * facing_dir, ch->dash_run_terminal_velocity);
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
          // Hidden TurnRun exit microphase:
          // - Decomp routes animation end through fn_800CA644, then ft_8008A2BC -> Wait when the
          //   Run gate fails; the same-frame destination Wait_IASA can then consume current input
          //   into Dash/Squat/Turn/Walk.
          // - TurnRun_Anim is a prio-1 Anim callback and reads the pre-input `fp->input.lstick`;
          //   Fighter_Spaghetti_8006AD10 installs current-frame input later in proc priority.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
          // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_Spaghetti_8006AD10}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          const float turnrun_anim_stick_x = apply_deadzone(
              stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
          const float turnrun_entry_facing_dir = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
          const float turnrun_exit_facing_dir =
              (turnrun_entry_facing_dir == facing_dir) ? -facing_dir : facing_dir;
          const uint8_t turnrun_exit_prefers_run =
              ((turnrun_anim_stick_x * turnrun_exit_facing_dir) >= c->run_stick_x_threshold) ? 1u
                                                                                             : 0u;
          if (turnrun_exit_prefers_run) {
            // Decomp: ftCo_TurnRun_Enter copies entry `fp->facing_dir` into `fp->facing_dir1`.
            // ftCo_TurnRun_Anim can later flip current facing before the animation-end Run gate.
            // If the visible current facing still matches the entry copy, emulate the pending
            // cmd_vars[1]/x14 final pivot; otherwise use the already-exposed current facing.
            //
            // Runtime scope: this block is only the final TurnRun animation-end handoff. Mid-state
            // TurnRun pose/pause remains represented by the extracted animation/root motion.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
            batch->state.facing[idx] = (uint8_t)(turnrun_exit_facing_dir > 0.0f);
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
        // Same-frame Turn entry from an earlier callback (for example Damage_Anim -> Wait ->
        // Wait_IASA -> Turn) has already consumed ftAnim_8006EBA4 for this source frame. Do not
        // run Turn_Anim's facing flip until the next fighter proc.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
        const uint8_t turn_entered_after_frame_start =
            (batch->state.action_frame[idx] <= 1 &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_TURN)
                ? 1u
                : 0u;
        if (action_id_start == MSL_ACT_TURN && turn_entered_after_frame_start == 0u) {
          const int16_t turn_first_steady_postflip_af = (int16_t)(ch->turn_frames + 2);
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88 (ftCo_Turn_Anim_Inner)
          // Basic-Turn first steady post-flip facing reconstruction:
          // - ftCo_Turn_Anim_Inner flips facing once `frames_to_turn` expires, and locomotion runs
          //   after the frame's anim tick has already advanced action_frame/anim_frame.
          // - On reseeded basic-Turn rows (`x8==0`) that already carry `has_turned=1`, the stale
          //   facing shows up on the first steady post-flip frame rather than the immediate flip
          //   tick. Reconstruct only that one-frame lane. Smash-turn rows whose x8 already points
          //   to the post-flip direction keep native x8 ownership.
          // - In this sim ordering, that steady post-flip row is `turn_frames + 2`: one tick to
          //   flip, then one more tick because locomotion observes post-Anim action_frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
          //   ftCo_Turn_Enter,ftCo_Turn_Enter_Basic,ftCo_Turn_Anim_Inner,ftCo_Turn_Enter_Smash}
          // data/characters/{fox,falco}.json turn_frames
          if (batch->state.turn_has_turned[idx] && batch->state.turn_frames_to_turn[idx] == 0u &&
              batch->state.turn_x8[idx] == 0 &&
              batch->state.action_frame[idx] == turn_first_steady_postflip_af &&
              batch->state.speed_ground_x_self[idx] == 0.0f) {
            batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          }
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
          run_anim_rate_ftCo_Run_Anim(batch, ch, idx, action_id_start);
        }

        // Grounded attack state updates (Anim before IASA in Fighter_procUpdate).
        //
        // Decomp:
        // - Grounded Attack* motion states run *_Anim before *_IASA each frame.
        // - *_Anim resolves to Wait when the attack animation finishes.
        // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_Attack1.c}
        if (grounded_attack_update(batch, c, idx, cid, buttons_pressed, stick_x, stick_y,
                                   tilt_timer_x, tilt_timer_y, facing_dir)) {
          action_id = batch->state.action_id[idx];
          if (action_id == (uint16_t)MSL_ACT_WAIT) {
            // Grounded attack anim-end -> Wait destination bridge:
            // - Grounded Attack* _Anim callbacks resolve to Wait on the frame the motion finishes.
            // - The destination Wait ordering checks grounded specials, Catch, and attacks before
            //   ftCo_80091A4C (guard), so same-frame command restarts must be admitted before the
            //   shared pre-pass guard loop runs later in this frame.
            // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Attack1.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackLw3.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c,ftCo_AttackDash.c}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
            if (blaster_try_enter_ground_from_wait_iasa(batch, c, idx)) {
              continue;
            }
            if (action_id_start != (uint16_t)MSL_ACT_ATTACK_DASH &&
                spacie_speciallw_wait_iasa_pressed_edge(c, cid, buttons_pressed, stick_x,
                                                        stick_y)) {
              shine_enter_ground_start_from_iasa(batch, idx);
              continue;
            }
            if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
              continue;
            }
            if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(
                    batch, c, idx, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y,
                    facing_dir)) {
              continue;
            }
            if ((action_id_start == (uint16_t)MSL_ACT_ATTACK_S4_HI ||
                 action_id_start == (uint16_t)MSL_ACT_ATTACK_S4_HI_S ||
                 action_id_start == (uint16_t)MSL_ACT_ATTACK_S4_S ||
                 action_id_start == (uint16_t)MSL_ACT_ATTACK_S4_LW_S ||
                 action_id_start == (uint16_t)MSL_ACT_ATTACK_S4_LW)) {
              const float trig_unit =
                  msl_trigger_unit_from_input(batch->state.input_buttons[idx],
                                              batch->state.input_l[idx], batch->state.input_r[idx]);
              const float raw_stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
              // Narrow AttackS4 anim-end -> Wait bridge:
              // - grounded AttackS4 _Anim resolves to Wait before the frame's input callback
              // - the destination row can still reach ftCo_Walk_CheckInput on the raw same-facing
              //   lane from ftWalkCommon_800DFC70
              // - keep this scoped to the raw-only lane so ordinary Wait / AttackDash delegation
              //   stays on the replay-proven deadzoned path
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::{
              //   ftCo_AttackS4_Anim,ftCo_AttackS4_IASA}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
              // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFC70
              if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0u &&
                  (buttons &
                   (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z | MSL_BUTTON_B)) == 0u &&
                  trig_unit <= c->trigger_deadzone && msl_absf(stick_x) < c->walk_stick_threshold &&
                  (raw_stick_x * facing_dir) >= c->walk_stick_threshold) {
                const uint16_t want =
                    walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
                batch->state.action_id[idx] = want;
                batch->state.animation_index[idx] = anim_for_walk_action(want);
                msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                msl_anim_timebase_tick_once(batch, idx);
                batch->state.walk_use_raw_input_once[idx] = 1u;
                continue;
              }
            }
          }
          if (grounded_attack_submotion_from_action(action_id) != 0xFFFFFFFFu) {
            const float grounded_attack_script_frame = batch->state.anim_frame_f32[idx];
            if (grounded_attack_try_jab_chain_subset(batch, ch, idx, cid, action_id_start,
                                                     action_id, buttons,
                                                     grounded_attack_script_frame)) {
              action_id = batch->state.action_id[idx];
            }

            const uint8_t allow_interrupt = move_tables_grounded_attack_allow_interrupt(
                cid, action_id, batch->state.anim_frame_f32[idx]);

            uint8_t attackdash_pregate_consumed = 0u;
            if (action_id == (uint16_t)MSL_ACT_ATTACK_DASH) {
              // AttackDash IASA pre-gate (ftCo_800D8AE0):
              // - if (held_inputs & HSD_PAD_LR and mv.co.attackdash.x0>0) => enter CatchDash via
              //   ftCo_800D8C54.
              // - else decrement x0 when x0>0.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
              // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_800D8AE0,ftCo_800D8C54}
              if ((buttons & ((uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R)) != 0u &&
                  batch->state.attackdash_x0[idx] > 0) {
                grab_flow_enter_catchdash_from_attackdash_pregate(batch, idx);
                action_id = batch->state.action_id[idx];
                attackdash_pregate_consumed = 1u;
              } else if (batch->state.attackdash_x0[idx] > 0) {
                batch->state.attackdash_x0[idx] = (int16_t)(batch->state.attackdash_x0[idx] - 1);
              }
            }
            // Decomp: grounded Attack* IASA gates on fp->allow_interrupt before delegating to
            // grounded interrupt checks (typically Wait IASA path).
            // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
            const float prev_stick_x_attackdash_wait = apply_deadzone(
                stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
            const uint8_t attackdash_wait_same_facing_hold =
                ((stick_x * facing_dir) > 0.0f &&
                 (prev_stick_x_attackdash_wait * facing_dir) > 0.0f &&
                 msl_absf(stick_x) >= c->walk_stick_threshold &&
                 msl_absf(prev_stick_x_attackdash_wait) >= c->walk_stick_threshold)
                    ? 1u
                    : 0u;
            const uint8_t attackhi3_wait_current_walk_hold =
                ((action_id == (uint16_t)MSL_ACT_ATTACK_HI3) && (stick_x * facing_dir) > 0.0f &&
                 msl_absf(stick_x) >= c->walk_stick_threshold)
                    ? 1u
                    : 0u;
            const uint8_t grounded_attack_wait_locomotion_input =
                (grounded_attack_wait_iasa_locomotion_action(action_id) &&
                 (is_dash_flick(c, stick_x, tilt_timer_x) ||
                  msl_absf(stick_x) >= c->walk_stick_threshold))
                    ? 1u
                    : 0u;
            const uint8_t grounded_attack_wait_attack_input =
                (grounded_attack_wait_iasa_locomotion_action(action_id) &&
                 (cstick_side_smash_edge(c, batch, idx) || cstick_up_smash_edge(c, batch, idx) ||
                  cstick_down_smash_edge(c, batch, idx)))
                    ? 1u
                    : 0u;
            const uint8_t grounded_attack_wait_jump_input =
                (grounded_attack_wait_iasa_locomotion_action(action_id) &&
                 jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y) !=
                     MSL_JUMP_INPUT_NONE)
                    ? 1u
                    : 0u;
            const uint8_t attackdash_wait_iasa_enabled =
                // Decomp: AttackDash_IASA delegates to Wait_IASA whenever allow_interrupt is true
                // and the Attack100 pre-gate did not consume.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
                (action_id == (uint16_t)MSL_ACT_ATTACK_DASH) ? 1u
                : ((buttons_pressed & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_XY)) !=
                       0u ||
                   stick_y < -c->crouch_stick_threshold || attackdash_wait_same_facing_hold ||
                   attackhi3_wait_current_walk_hold || grounded_attack_wait_locomotion_input ||
                   grounded_attack_wait_attack_input || grounded_attack_wait_jump_input ||
                   (stick_x * facing_dir) <= c->turn_stick_x_threshold)
                    ? 1u
                    : 0u;
            const uint8_t attackdash_guard_iasa_enabled =
                // Decomp ordering in ftCo_AttackDash_IASA delegates into Wait-style checks where
                // guard entry (ftCo_80091A4C) is evaluated before crouch/squat checks.
                // Keep GuardOn admission ungated by crouch-threshold narrowing that we apply to the
                // generic grounded_attack_try_iasa_subset bridge.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
                (action_id == (uint16_t)MSL_ACT_ATTACK_DASH) ? 1u : attackdash_wait_iasa_enabled;
            uint8_t attackdash_guard_iasa_consumed = 0u;
            if (!attackdash_pregate_consumed && allow_interrupt && attackdash_guard_iasa_enabled &&
                action_id == (uint16_t)MSL_ACT_ATTACK_DASH) {
              // Decomp ordering for AttackDash IASA delegation:
              // - ftCo_AttackDash_IASA delegates into the Wait-style interrupt checks.
              // - In ftCo_Wait_IASA, guard entry (ftCo_80091A4C) is checked before
              //   jump/dash/squat/turn/walk checks.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              //
              // Identity continuity note:
              // entering an intermediate locomotion state before GuardOn can consume an extra
              // Fighter_ChangeMotionState identity bundle (ft_800895E0 / instance_id bump lane).
              // Keep the AttackDash guard lane aligned to Wait_IASA ordering to avoid that spillover.
              // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
              // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
              const uint16_t act_before_guard = batch->state.action_id[idx];
              guard_update_grounded(batch, c, idx, 1u);
              action_id = batch->state.action_id[idx];
              if (act_before_guard == (uint16_t)MSL_ACT_ATTACK_DASH &&
                  action_id != (uint16_t)MSL_ACT_ATTACK_DASH) {
                attackdash_guard_iasa_consumed = 1u;
                if (grounded_attack_wait_iasa_interrupt_dest_action(action_id)) {
                  grounded_attack_carry_allow_interrupt(batch, idx);
                }
              }
            }
            uint8_t grounded_attack_guard_iasa_consumed = 0u;
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                allow_interrupt && action_id == (uint16_t)MSL_ACT_ATTACK_HI3) {
              // Decomp ordering for AttackHi3 IASA:
              // - ftCo_AttackHi3_IASA gates on fp->allow_interrupt then delegates into
              //   ftCo_Wait_IASA.
              // - ftCo_Wait_IASA checks smashes/tilts/jab before ftCo_80091A4C (GuardOn).
              // - Preserve the existing AttackHi3->GuardOn owner only after the attack subset has
              //   had a chance to consume the row.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              if (attackhi3_wait_attack_try_enter(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                                  tilt_timer_x, tilt_timer_y, facing_dir)) {
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              }
            }
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                action_id == (uint16_t)MSL_ACT_ATTACK_HI3) {
              // Decomp ordering for AttackHi3 IASA:
              // - ftCo_AttackHi3_IASA gates on fp->allow_interrupt then delegates into
              //   ftCo_Wait_IASA.
              // - ftCo_Wait_IASA checks guard entry (ftCo_80091A4C) before jump / dash / squat /
              //   turn / walk.
              // Keep this scoped to AttackHi3 until the other grounded-attack guard families are
              // triaged; the broader lane spills into unrelated states.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
              const uint16_t act_before_guard = batch->state.action_id[idx];
              guard_update_grounded(batch, c, idx, 1u);
              action_id = batch->state.action_id[idx];
              if (act_before_guard != action_id) {
                grounded_attack_guard_iasa_consumed = 1u;
                if (grounded_attack_wait_iasa_interrupt_dest_action(action_id)) {
                  grounded_attack_carry_allow_interrupt(batch, idx);
                }
              }
            }
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                action_is_attack_s4_family(action_id)) {
              // Decomp ordering for AttackS4 IASA:
              // - ftCo_AttackS4_IASA gates on fp->allow_interrupt.
              // - Its preamble checks specials and grounded attacks before Catch/Guard.
              // - Catch and ftCo_80091A4C guard entry then run before the jump/dash/squat/turn/walk
              //   tail.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
              const uint8_t speciallw_preempts_attacks4_guard =
                  spacie_speciallw_wait_iasa_pressed_edge(c, cid, buttons_pressed, stick_x,
                                                          stick_y);
              if (speciallw_preempts_attacks4_guard) {
                // AttackS4's source IASA preamble reaches the grounded special dispatcher before
                // Catch/Guard. The Side/Hi/Neutral subset above lives in blaster.c; Reflector is
                // owned by shine.c and must consume B+down before same-frame shield input can enter
                // GuardOn. The source predicate is the x687 timer, refreshed from the B pressed-edge
                // plus down-stick in Fighter_UnkIncrementCounters_8006ABEC before the IASA callback.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D688C,ftCo_800D68C0}
                // refs/melee/src/melee/ft/fighter.c::Fighter_UnkIncrementCounters_8006ABEC
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
                shine_enter_ground_start_from_iasa(batch, idx);
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              } else if (grounded_attack_wait_iasa_specials_action(action_id) &&
                         blaster_try_enter_ground_from_iasa_subset(batch, c, idx)) {
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              } else if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed,
                                                               stick_x, stick_y, tilt_timer_x,
                                                               tilt_timer_y, facing_dir, 0, 1)) {
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              } else if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              } else {
                const uint16_t act_before_guard = batch->state.action_id[idx];
                guard_update_grounded(batch, c, idx, 1u);
                action_id = batch->state.action_id[idx];
                if (act_before_guard != action_id) {
                  grounded_attack_guard_iasa_consumed = 1u;
                }
              }
              if (grounded_attack_guard_iasa_consumed &&
                  grounded_attack_wait_iasa_interrupt_dest_action(action_id)) {
                grounded_attack_carry_allow_interrupt(batch, idx);
              }
            }
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                grounded_attack_wait_iasa_catch_guard_action(action_id)) {
              // Decomp ordering for grounded attacks that delegate into ftCo_Wait_IASA:
              // - AttackS3/AttackHi4/AttackLw4 call ftCo_Wait_IASA directly when
              //   fp->allow_interrupt is true.
              // - AttackS4 is handled just above because its source callback checks special/attack
              //   before catch/guard.
              // - Attack13 enters ftCo_Wait_IASA after its Attack_800D6A50 gate.
              // Keep AttackLw3 and Attack11/12 out: their specialized callbacks do not expose
              // this guard branch.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack13_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
                action_id = batch->state.action_id[idx];
                grounded_attack_guard_iasa_consumed = 1u;
              } else {
                const uint16_t act_before_guard = batch->state.action_id[idx];
                guard_update_grounded(batch, c, idx, 1u);
                action_id = batch->state.action_id[idx];
                if (act_before_guard != action_id) {
                  grounded_attack_guard_iasa_consumed = 1u;
                }
              }
              if (grounded_attack_guard_iasa_consumed &&
                  grounded_attack_wait_iasa_interrupt_dest_action(action_id)) {
                grounded_attack_carry_allow_interrupt(batch, idx);
              }
            }
            const uint8_t attackdash_specials_has_input =
                // Decomp ordering for grounded AttackDash IASA delegation:
                // - ftCo_AttackDash_IASA delegates to ftCo_Wait_IASA after its pre-gates.
                // - ftCo_Wait_IASA checks ftCo_SpecialS_CheckInput before Squat/Turn/Walk.
                // - ftCo_SpecialS_CheckInput consumes held-B rows once ABS(lstick.x) >=
                //   p_ftCommonData->x218.
                // AttackDash needs this local preemption because its source callback directly
                // delegates into Wait_IASA; other grounded-attack callbacks are handled by their
                // family-specific IASA branches above.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
                //   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
                // data/common/ft_common_data.json: special_stick_x_threshold_side
                (action_id == (uint16_t)MSL_ACT_ATTACK_DASH &&
                 (buttons & (uint16_t)MSL_BUTTON_B) != 0u &&
                 msl_absf(stick_x) >= c->special_stick_x_threshold_side)
                    ? 1u
                    : 0u;
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                action_id == (uint16_t)MSL_ACT_ATTACK_DASH && shine_char_supports_reflector(cid) &&
                (buttons_pressed & (uint16_t)MSL_BUTTON_B) != 0u &&
                !attackdash_specials_has_input && stick_y <= -c->special_stick_y_threshold) {
              // Decomp AttackDash IASA delegation:
              // - ftCo_AttackDash_IASA gates on fp->allow_interrupt, then delegates to Wait_IASA.
              // - Wait_IASA runs ftCo_SpecialS_CheckInput first, then grounded special dispatcher
              //   ftCo_800D68C0 before catch/guard/jump/dash/squat/turn/walk.
              // - Reflector entry is owned by ftCo_800D68C0 for grounded Fox/Falco rows only, so
              //   non-spacies must not consume this branch before the rest of Wait_IASA ordering.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
              // refs/melee/src/melee/ft/chara/ftFalco/ftFc_SpecialLw.c
              // data/common/ft_common_data.json: special_stick_y_threshold
              shine_enter_ground_start_from_iasa(batch, idx);
              action_id = batch->state.action_id[idx];
              grounded_attack_carry_allow_interrupt(batch, idx);
            }
            if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                action_id == (uint16_t)MSL_ACT_ATTACK_DASH &&
                (buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0u &&
                (buttons & (uint16_t)MSL_BUTTON_B) != 0u && !attackdash_specials_has_input &&
                stick_y < -c->crouch_stick_threshold) {
              // Decomp ordering for AttackDash IASA:
              // - ftCo_AttackDash_IASA delegates to ftCo_Wait_IASA after the AttackDash-specific
              //   pre-gates.
              // - Wait_IASA routes through ftCo_SpecialS_CheckInput before Squat.
              // - ftCo_SpecialS_CheckInput requires B plus horizontal stick magnitude (ABS(x) >=
              //   p_ftCommonData->x218), so only held-B rows below that extracted side-special
              //   threshold can still fall through to Squat.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
              //   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
              // data/common/ft_common_data.json: special_stick_x_threshold_side
              enter_squat_immediate(batch, idx);
              action_id = batch->state.action_id[idx];
              grounded_attack_carry_allow_interrupt(batch, idx);
            } else if (!attackdash_pregate_consumed && !attackdash_guard_iasa_consumed &&
                       !grounded_attack_guard_iasa_consumed && allow_interrupt &&
                       attackdash_wait_iasa_enabled && !attackdash_specials_has_input &&
                       grounded_attack_try_iasa_subset(batch, c, ch, idx, buttons, buttons_pressed,
                                                       stick_x, stick_y, tilt_timer_x, tilt_timer_y,
                                                       facing_dir)) {
              action_id = batch->state.action_id[idx];
              grounded_attack_input_callback_consumed = 1u;
              if (grounded_attack_wait_iasa_interrupt_dest_action(action_id)) {
                grounded_attack_carry_allow_interrupt(batch, idx);
              }
            }

            if (grounded_attack_submotion_from_action(action_id) != 0xFFFFFFFFu) {
              continue;
            }
          }
        }

        if (dash_anim_end_try_enter_wait_ft_8008A2BC(batch, idx, action_id_start)) {
          action_id = batch->state.action_id[idx];
        }

        // Guard core loop (entry/hold/exit). Keep this before the common grounded locomotion IASA
        // (e.g. Wait->Jump/Dash), but preserve Dash_IASA's CatchDash ownership before any shield
        // entry can preempt it on the same frame.
        //
        // Decomp ordering:
        // - ftCo_Dash_IASA calls ftCo_800D8A38 (CatchDash) before any guard-owned path.
        // - Our shared guard_update_grounded() pass runs earlier than the explicit Dash_IASA block
        //   below, so replay-real Dash+grab rows need a narrow pre-guard CatchDash bridge here.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8A38
        if (action_id == MSL_ACT_DASH && action_id_start == MSL_ACT_DASH &&
            grab_flow_try_enter_catchdash_from_iasa(batch, c, idx)) {
          continue;
        }
        if (action_id == MSL_ACT_DASH && action_id_start == MSL_ACT_DASH &&
            batch->state.anim_frame_f32[idx] <= c->dash_iasa_x4c &&
            (buttons & (uint16_t)MSL_BUTTON_B) != 0u &&
            msl_absf(stick_x) >= c->special_stick_x_threshold_side) {
          // Dash_IASA checks ftCo_SpecialS_CheckInput before item/CatchDash/AttackDash and before
          // the later ftCo_80091AD8 guard path. Shield held on the same frame does not preempt the
          // source Side-B owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
          //   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
          dash_iasa_enter_grounded_side_special_start(batch, idx, c, ms, ch, stick_x);
          continue;
        }

        // Catch-before-guard bridge for grounded IASA owners that delegate into Wait ordering.
        //
        // Decomp:
        // - Wait_IASA / Walk_IASA / Turn_IASA check ftCo_Catch_CheckInput before ftCo_80091A4C.
        // - Landing_IASA runs the same grounded interrupt subset after the landing-lag gate.
        // - Squat_IASA checks ftCo_Catch_CheckInput before ftCo_80091A4C, but SquatWait/SquatRv do
        //   not.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
        if ((action_id == MSL_ACT_WAIT || action_is_walk(action_id) || action_id == MSL_ACT_TURN ||
             action_id == MSL_ACT_SQUAT ||
             (action_id == MSL_ACT_LANDING &&
              batch->state.anim_frame_f32[idx] >= (float)ch->landing_lag_frames)) &&
            grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
          continue;
        }

        // Plain Wait_IASA runs grounded specials and grounded attacks before guard.
        //
        // Keep this narrow to steady-state Wait rows only:
        // - generic destination-Wait handoffs (Escape*, Catch, SpecialHiLanding, etc.) still use
        //   their own localized bridges so we do not re-open the earlier destination-Wait shield
        //   regression surface.
        // - plain Wait rows should still honor the decomp ordering where SpecialS/Hi/N/Lw and
        //   then AttackS4/Hi4/Lw4, tilts, and jab beat guard on the same frame.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        if (action_id == MSL_ACT_WAIT && blaster_try_enter_ground_from_wait_iasa(batch, c, idx)) {
          continue;
        }
        if (action_id == MSL_ACT_WAIT &&
            grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                                  tilt_timer_x, tilt_timer_y, facing_dir, 0, 1)) {
          continue;
        }
        if (action_id == MSL_ACT_WAIT &&
            wait_iasa_try_enter_spotdodge_before_guard(batch, c, idx)) {
          // LandingAir/Landing anim-end can enter Wait in the Anim callback phase, then dispatch
          // Wait_IASA in the same frame. Wait_IASA runs ftCo_80099794 before ftCo_80091A4C guard
          // entry; Walk/Turn/Squat do not take this pre-guard helper.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          continue;
        }
        // Steady-state Walk_IASA also runs grounded attacks before guard.
        //
        // Keep this narrow to walk rows that were already in Walk at frame start:
        // - walk end/retarget handling stays in the dedicated Walk block later in this function
        // - destination-Walk handoffs are still owned by their local bridges
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start) &&
            grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x, stick_y,
                                                  tilt_timer_x, tilt_timer_y, facing_dir, 0, 1)) {
          continue;
        }

        // Steady-state Turn_IASA also runs grounded attacks before guard.
        //
        // Decomp:
        // - ftCo_Turn_IASA temporarily flips fp->facing_dir to mv.co.turn.facing_after before the
        //   grounded attack checks, then restores it before ftCo_80091A4C when the turn has not
        //   completed yet.
        // - Attack* checks therefore still beat guard on the same Turn row.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
        if (action_id == MSL_ACT_TURN && action_id_start == MSL_ACT_TURN) {
          uint8_t turn_attack_facing_flipped = 0u;
          if (!batch->state.turn_has_turned[idx]) {
            batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
            turn_attack_facing_flipped = 1u;
          }
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
            continue;
          }
          if (turn_attack_facing_flipped) {
            batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          }
        }

        // Guard core loop (entry/hold/exit).
        // Decomp call site example: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c:43-66.
        uint8_t turn_analog_guard_facing_flipped = 0u;
        if (action_id == MSL_ACT_TURN && action_id_start == MSL_ACT_TURN &&
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_LANDING &&
            batch->state.action_frame[idx] <= 2 && !batch->state.turn_has_turned[idx] &&
            (stick_x * facing_dir) <= c->turn_stick_x_threshold &&
            (buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) == 0u &&
            msl_trigger_unit_from_input(batch->state.input_buttons[idx], batch->state.input_l[idx],
                                        batch->state.input_r[idx]) > c->trigger_deadzone) {
          // Landing -> Turn -> analog GuardOn hidden facing handoff:
          // - Turn_IASA normally restores `fp->facing_dir` before the guard checks, but replay-real
          //   first-frame Landing->Turn analog-shield rows can expose the destination GuardOn with
          //   the turn-facing lane when the current stick still satisfies the source turn threshold.
          // - Keep digital LR/Z powershield rows on the ordinary GuardReflect path; those controls
          //   stay seed-facing.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80091AD8}
          batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
          facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          turn_analog_guard_facing_flipped = 1u;
        }
        uint8_t allow_guard_entry = 0;
        if (!grounded_attack_input_callback_consumed &&
            (action_id == MSL_ACT_WAIT || action_is_walk(action_id) || action_id == MSL_ACT_TURN ||
             action_id == MSL_ACT_TURN_RUN || action_id == MSL_ACT_DASH ||
             action_id == MSL_ACT_RUN || action_id == MSL_ACT_RUN_DIRECT ||
             action_id == MSL_ACT_SQUAT || action_id == MSL_ACT_SQUAT_WAIT ||
             action_id == MSL_ACT_SQUAT_RV)) {
          // A grounded Attack* IASA callback that enters a locomotion destination has consumed this
          // frame's input callback. The destination state does not immediately run its own guard
          // entry owner in the same Fighter_procUpdate pass.
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          allow_guard_entry = 1;
        }
        const uint8_t guard_platform_pass_pending =
            (grounded_guard_state_allows_platform_pass_iasa(batch, idx, action_id) &&
             !guardon_fresh_entry_from_non_shield_skips_current_iasa(batch, idx, action_id) &&
             !guardon_powershield_reflect_preempts_platform_pass(batch, c, idx, action_id) &&
             input_hsd_pad_lr_held_lane(batch, c, idx) &&
             common_pass_input_gate(batch, c, idx, stick_y, tilt_timer_y))
                ? 1u
                : 0u;
        const uint16_t action_id_before_guard_update = action_id;
        if (guardsetoff_platform_edge_floor_loss_before_destination_iasa(batch, idx)) {
          // Keep GuardSetOff live for its source collision callback below. Entering Fall here would
          // run Fall physics one frame early; the decomp owner is the later GuardSetOff_Coll pass.
          //
          // Source identity side effect:
          // - The platform-edge GuardSetOff_Coll path routes through ft_80084104 before
          //   ftCo_Fall_Enter. On replay-real source rows this callback-local edge path advances
          //   the shared plAttack_80037B08 counter once before the later Fall motion entry writes
          //   fp->x2088. Model only that existing edge-loss owner so ordinary GuardSetOff -> Guard
          //   or GuardOff exits do not consume hidden instance ids.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80084104
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
          instance_id_counter_consume_plAttack_80037B08(batch, idx);
          continue;
        }
        guard_update_grounded(batch, c, idx, allow_guard_entry);
        action_id = batch->state.action_id[idx];
        if (turn_analog_guard_facing_flipped && action_id == MSL_ACT_TURN) {
          batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
          facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
        }
        const uint8_t guardsetoff_anim_destination_guard_pass_pending =
            (action_id_before_guard_update == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
             action_id == (uint16_t)MSL_ACT_GUARD && input_hsd_pad_lr_held_lane(batch, c, idx) &&
             common_pass_input_gate(batch, c, idx, stick_y, tilt_timer_y))
                ? 1u
                : 0u;
        if ((guard_platform_pass_pending || guardsetoff_anim_destination_guard_pass_pending) &&
            grounded_guard_state_allows_platform_pass_iasa(batch, idx, action_id) &&
            common_pass_input_gate(batch, c, idx, stick_y, tilt_timer_y)) {
          // Guard/GuardOn/GuardReflect platform pass:
          // Decomp orders the shield IASA tail as defensive options first, then
          // ftCo_8009A080. Keep platform pass after guard_update_grounded(), which owns
          // spotdodge/roll/catch/jump, the GuardOn powershield-reflect branch, and GuardSetOff_Anim
          // promotion into destination Guard before that destination Guard_IASA tail.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA,ftCo_GuardSetOff_Anim}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_8009980C,ftCo_8009917C}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
          common_pass_enter(batch, c, ch, idx);
          continue;
        }
        if ((action_id == MSL_ACT_WAIT || action_is_walk(action_id) || action_id == MSL_ACT_TURN ||
             action_id == MSL_ACT_SQUAT || action_id == MSL_ACT_SQUAT_WAIT ||
             action_id == MSL_ACT_SQUAT_RV) &&
            common_appeal_try_enter_from_grounded_iasa(batch, idx, buttons_pressed)) {
          // Common Appeal admission is the ftCo_800DE9D8 tail, not generic locomotion:
          // catch / special / attack / guard have already had priority, and jump/dash/locomotion
          // remain below it. Supported decomp callers here are Wait/Walk/Turn/Squat/SquatWait/
          // SquatRv. Dash/Run have different branch-local priority gates and are handled in the
          // Dash_IASA / Run_IASA sections below.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_{Wait,Walk,Turn,Squat}.c
          continue;
        }

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
          if (action_id == MSL_ACT_WAIT &&
              wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
            // Escape* anim-end -> destination Wait pre-guard spotdodge:
            // - EscapeN_IASA itself is empty, but Escape*_Anim can enter Wait before the current
            //   fighter proc finishes. The source-owned destination slice needed here is the
            //   ftCo_80099794 defensive check before GuardOn and before Squat.
            // - Keep specials/attacks out of this Escape destination slice: Escape* IASA does not
            //   call ftCo_800D68C0, and the existing held-B special helpers are only valid for
            //   states whose decomp IASA actually reaches the grounded special dispatcher.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{
            //   ftCo_Escape_Anim,ftCo_EscapeN_Anim,ftCo_EscapeN_IASA,ftCo_80099794}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
            // refs/melee/src/melee/ft/fighter.c:1868-1890
            action_id = batch->state.action_id[idx];
          }
          if (action_id == MSL_ACT_WAIT) {
            guard_update_grounded(batch, c, idx, 1u);
            action_id = batch->state.action_id[idx];
          }
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
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
            action_id = batch->state.action_id[idx];
          } else if (wait_iasa_locomotion_subset_try_enter(
                         batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y,
                         tilt_timer_x, tilt_timer_y, facing_dir, action_id_start)) {
            action_id = batch->state.action_id[idx];
          }
        }

        // Ottotto grounded special IASA:
        // - ftCo_Ottotto_IASA checks SpecialS, then the shared grounded B-special dispatchers,
        //   before catch/attack/guard/jump/dash/turn/walk. Reuse the same table/data-backed
        //   spacie special helpers as Wait-style IASA instead of adding a Teeter-only branch.
        // - Reflector remains owned by the grounded ftCo_800D68C0 helper only when the B+down
        //   pressed-edge lane is active and Side-B does not preempt it.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
        //   ftCo_SpecialS_CheckInput,ftCo_800D6824,ftCo_800D68C0}
        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          if (blaster_try_enter_ground_from_iasa_subset(batch, c, idx)) {
            action_id = batch->state.action_id[idx];
            continue;
          }
          if (spacie_speciallw_wait_iasa_pressed_edge(c, batch->state.char_id[idx], buttons_pressed,
                                                      stick_x, stick_y)) {
            shine_enter_ground_start_from_iasa(batch, idx);
            action_id = batch->state.action_id[idx];
            continue;
          }
        }

        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          // Ottotto / OttottoWait catch IASA:
          // ftCo_Ottotto_IASA checks ftCo_Catch_CheckInput before grounded A-attacks and guard.
          // The catch helper owns the raw Slippi Z -> internal held LR + x668 A mapping.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
          if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
            action_id = batch->state.action_id[idx];
            continue;
          }
        }

        // Ottotto grounded A-attack IASA:
        // - ftCo_Ottotto_IASA checks grounded A-attack inputs before guard/jump/dash/turn/walk.
        // - ftCo_OttottoWait_IASA shares the same grounded input owner, so teeter wait can enter
        //   smash/tilt/jab/dash-attack without first returning to Wait.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_OttottoWait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_CheckInput
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_CheckInput
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_CheckInput
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack1_CheckInput
        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
            action_id = batch->state.action_id[idx];
            continue;
          }
        }

        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          // Ottotto / OttottoWait guard IASA:
          // ftCo_Ottotto_IASA calls ftCo_80091A4C after grounded attacks and before appeal/jump/
          // dash/crouch/turn/walk. That function owns GuardOn entry, so shield during teeter must
          // be admitted through the same guard path as Wait/Walk/Turn.
          //
          // Platform/drop intent exception: digital L/R with an outward/opposite edge stick at a
          // platform edge is the same input family as floor-loss/drop-through once shielding is
          // live. Ottotto itself has not entered the Guard callback yet, so do not consume that
          // edge case as GuardOn here; leave it to the existing floor-loss/pass-through path.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
          const uint16_t before_guard = action_id;
          if (ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx], batch->state.pos_x[idx]) &&
              !((buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R)) != 0u &&
                (stick_x * facing_dir) < 0.0f)) {
            guard_update_grounded(batch, c, idx, 1u);
            action_id = batch->state.action_id[idx];
            if (action_id != before_guard) {
              continue;
            }
          }
        }

        if ((action_id == (uint16_t)MSL_ACT_OTTOTTO ||
             action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            common_appeal_try_enter_from_grounded_iasa(batch, idx, buttons_pressed)) {
          // Ottotto/OttottoWait IASA also calls ftCo_800DE9D8 before Jump/Dash/Turn/Walk.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
          continue;
        }

        // Ottotto / OttottoWait jump IASA:
        // - ftCo_Ottotto{,Wait}_IASA routes through ftCo_Jump_CheckInput before Dash/Turn/Walk.
        // - Keep jump entry independent from the earlier grounded A-attack owner above because the
        //   source callback checks catch/attack/guard/appeal before reaching Jump.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
        //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
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

        if ((action_id == (uint16_t)MSL_ACT_OTTOTTO ||
             action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            stick_y < -c->crouch_stick_threshold) {
          // Ottotto / OttottoWait crouch IASA:
          // - ftCo_Ottotto_IASA runs ftCo_800D5FB0 after Jump and Dash.
          // - ftCo_800D5FB0 wraps ftCo_Squat_CheckInput, which enters Squat when
          //   input.lstick.y < -p_ftCommonData->x90.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{
          //   ftCo_Squat_CheckInput,ftCo_800D5FB0,ftCo_Squat_Enter}
          enter_squat_immediate(batch, idx);
          action_id = (uint16_t)MSL_ACT_SQUAT;
        }

        // Ottotto / OttottoWait dash-flick bridge:
        // - ftCo_Ottotto{,Wait}_IASA routes through ftCo_Dash_CheckInput before Turn/Walk.
        // - ftCo_Dash_CheckInput enters TurnSmash on opposite-facing flicks and Dash on same-facing
        //   flicks, so keep both branches here once Ottotto admission itself is owned.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
        //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
        if ((action_id == (uint16_t)MSL_ACT_OTTOTTO ||
             action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            is_dash_flick(c, stick_x, tilt_timer_x)) {
          if ((stick_x * facing_dir) < 0.0f) {
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = 0;
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
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
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:59-62
            msl_anim_timebase_tick_once(batch, idx);
            batch->state.tilt_timer_x[idx] = 0xFEu;
            action_id = (uint16_t)MSL_ACT_DASH;
          }
        }

        if (action_id == (uint16_t)MSL_ACT_OTTOTTO || action_id == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          if ((stick_x * facing_dir) <= c->turn_stick_x_threshold) {
            // Ottotto / OttottoWait ordinary turn IASA:
            // - ftCo_Ottotto_IASA checks ftCo_Turn_CheckInput after Dash and crouch.
            // - ftCo_Turn_Enter_Basic calls ftAnim_8006EBA4 immediately, so the same step emits
            //   Turn action_frame 1 rather than a zero-frame placeholder.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
            //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
            //   ftCo_Turn_CheckInput,ftCo_Turn_Enter_Basic}
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = ch->turn_frames;
            batch->state.turn_x8[idx] = 0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            msl_anim_timebase_tick_once(batch, idx);
            action_id = (uint16_t)MSL_ACT_TURN;
          } else if ((stick_x * facing_dir) >= c->ottotto_walk_stick_x_threshold) {
            // Ottotto / OttottoWait walk IASA:
            // ftCo_Walk_CheckInput_Ottotto adds p_ftCommonData->x474 before the ordinary
            // ftWalkCommon_800DFC70 walk entry predicate.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
            //   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput_Ottotto
            // data/common/ft_common_data.json: ottotto_walk_stick_x_threshold
            const uint16_t want =
                walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
            batch->state.action_id[idx] = want;
            batch->state.animation_index[idx] = anim_for_walk_action(want);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            msl_anim_timebase_tick_once(batch, idx);
            action_id = want;
          }
        }

        if ((action_id_start == (uint16_t)MSL_ACT_OTTOTTO ||
             action_id_start == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            action_id == (uint16_t)MSL_ACT_TURN) {
          // Frame-start Ottotto overlap nudge before IASA Turn edge loss:
          // - Fighter_8006A360 runs ftCommon_8007E0E4 before the Ottotto IASA callback.
          // - Ottotto_IASA can then enter Turn from held/opposite X.
          // - Fighter_procUpdate applies the already-computed xF8_playerNudgeVel.x before the
          //   entered motion's collision callback observes floor loss.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Coll
          const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
          const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
          if (nudge_x * facing_sign > 0.0f &&
              ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx],
                                          batch->state.pos_x[idx] + nudge_x)) {
            batch->state.pos_x[idx] += nudge_x;
          }
        }

        if (action_id == (uint16_t)MSL_ACT_OTTOTTO &&
            anim_finished(batch->state.char_id[idx], (uint16_t)MSL_SM_OTTOTTO,
                          batch->state.anim_frame_f32[idx])) {
          // Ottotto Anim callback enters OttottoWait when the teeter animation has no frames
          // remaining. Fighter proc order runs Anim before IASA, but this late callback still fixes
          // reseeded one-step rows where the source action is steady Ottotto at the terminal frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_Anim,ftCo_8009A6B8}
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_OTTOTTO_WAIT;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_OTTOTTO_WAIT;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_OTTOTTO_WAIT;
        }

        // Landing IASA (minimal): after the landing lag gate, allow the same grounded locomotion
        // options we support from Wait (jump/dash/turn/walk), in the same relative order.
        //
        // Decomp: ftCo_Landing_IASA calls the common grounded interrupt checks after the lag gate.
        // LandingFallSpecial shares this IASA callback in the motion-state table, gated by
        // mv.co.landing.allow_interrupt carried from ftCo_LandingFallSpecial_Enter.
        // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_LandingFallSpecial
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
        //   ftCo_Landing_IASA,ftCo_LandingFallSpecial_Enter}
        const uint8_t landing_iasa_owner =
            (action_id == MSL_ACT_LANDING ||
             (action_id == MSL_ACT_LANDING_FALL_SPECIAL &&
              batch->state.landing_fallspecial_allow_interrupt[idx] != 0u))
                ? 1u
                : 0u;
        if (landing_iasa_owner &&
            batch->state.anim_frame_f32[idx] >= (float)ch->landing_lag_frames) {
          // Landing IASA includes grounded attack checks before Jump/Dash/Turn/Walk.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
          // Landing IASA attack admission:
          // - ftCo_Landing_IASA runs grounded attack checks in the Wait_IASA subset before
          //   jump/dash/turn/walk.
          // - A-edge is a valid trigger in that grounded attack check path; do not pre-filter it.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
            // Landing IASA ordering:
            // - ftCo_Landing_IASA runs grounded attack checks first, then ftCo_80091A4C (guard),
            //   then the remaining grounded interrupt subset (jump/dash/turn/walk).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
            guard_update_grounded(batch, c, idx, 1u);
            action_id = batch->state.action_id[idx];
            if (action_id == MSL_ACT_GUARD_ON || action_id == MSL_ACT_GUARD ||
                action_id == MSL_ACT_GUARD_REFLECT || action_id == MSL_ACT_GUARD_SET_OFF ||
                action_id == MSL_ACT_GUARD_OFF) {
              continue;
            }
            if ((action_id == MSL_ACT_LANDING || action_id == MSL_ACT_LANDING_FALL_SPECIAL) &&
                common_appeal_try_enter_from_grounded_iasa(batch, idx, buttons_pressed)) {
              continue;
            }
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
            } else if (walk_check_input_accepts(c, stick_x, facing_dir)) {
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
        }

        // Turn IASA (minimal): Jump and Dash.
        //
        // Decomp:
        // - ftCo_Turn_IASA temporarily flips fp->facing_dir before the grounded attack checks, so
        //   any consumed Attack* entry inherits `mv.co.turn.facing_after`.
        // - ftCo_Turn_IASA calls ftCo_Jump_CheckInput.
        // - Then it runs a Turn->Dash gate via:
        //   - fn_800C9C2C (sets mv.co.turn.x8 when a dash-flick toward mv.co.turn.facing_after
        //     occurs within x40 frames), and
        //   - (just_turned && x8) latch to enter Dash when the turn completes.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
        if (action_id == MSL_ACT_TURN && action_id_start == MSL_ACT_TURN) {
          uint8_t turn_attack_facing_flipped = 0u;
          uint8_t turn_facing_after = batch->state.facing[idx];
          if (!batch->state.turn_has_turned[idx]) {
            turn_facing_after = batch->state.facing[idx] ? 0u : 1u;
            batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
            facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
            turn_attack_facing_flipped = 1u;
          }
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
            action_id = batch->state.action_id[idx];
          } else {
            if (turn_attack_facing_flipped) {
              batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
              facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
            }
            const MslJumpInput j_in =
                jump_input_from_edges(c, buttons_pressed, stick_y, tilt_timer_y);
            if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              // Turn->KneeBend hidden-facing microphase:
              // ftCo_Turn_IASA temporarily exposes mv.co.turn.facing_after before the attack
              // checks, restores facing, then calls ftCo_Jump_CheckInput. Replay-visible first
              // Turn tick jump entries carry the hidden result through a Turn-only explicit lane
              // instead of weakening the normal facing owner for later Turn phases.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
              const uint8_t turn_kb_face = batch->state.turn_kneebend_facing_override[idx];
              uint8_t turn_kb_source_face_valid = 0u;
              uint8_t turn_kb_source_face = batch->state.facing[idx];
              if (turn_attack_facing_flipped && turn_kb_face >= 1u && turn_kb_face <= 2u) {
                turn_kb_source_face = (turn_kb_face == 2u) ? 1u : 0u;
                turn_kb_source_face_valid = 1u;
                batch->state.turn_kneebend_facing_override[idx] = 0u;
              } else if (turn_attack_facing_flipped &&
                         batch->state.turn_frames_to_turn[idx] + 1u == ch->turn_frames &&
                         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_WAIT &&
                         j_in == MSL_JUMP_INPUT_XY &&
                         batch->state.input_main_y[idx] >
                             (int8_t)(-c->smash_stick_threshold * (float)MSL_STICK_MAX_I8) &&
                         stick_i8_to_unit(batch->state.prev_input_main_y[idx]) >
                             -c->tap_jump_release_threshold &&
                         batch->replay_rollout_reseeded != NULL &&
                         batch->replay_rollout_reseeded[bi] != 0u) {
                // Source-owned Turn -> KneeBend hidden-facing lane:
                // `ftCo_Turn_IASA` exposes `mv.co.turn.facing_after` before the Jump_CheckInput
                // branch, so first-tick XY button jumps from Wait-owned Turn can carry that
                // hidden facing. Exact one-step rows are seed-owned and must use the explicit
                // `turn_kneebend_facing_override_u8` lane; this fallback reconstructs only
                // free-running replay rollout segments after the hidden lane was not serialized at
                // the reseed boundary. L-stick tap-jump rows stay on the restored-facing path.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
                turn_kb_source_face = turn_facing_after;
                turn_kb_source_face_valid = 1u;
              }
              if (turn_kb_source_face_valid) {
                batch->state.facing[idx] = turn_kb_source_face;
                facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
              }
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
              jump_input_from_fn_800CAF78(c, buttons_pressed, stick_y, tilt_timer_y);
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
          // Decomp ordering: Walk_Anim (ftWalkCommon_800DFDDC anim-rate ownership) runs before
          // Walk_IASA in Fighter_procUpdate.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::{ftCo_Walk_Anim,ftCo_Walk_IASA}
          walk_anim_rate_ftWalkCommon_800DFDDC(batch, ch, idx, action_id, facing_dir);
          if (grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    0, 1)) {
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

        // Walk exit gate before walk-type retarget (ft_8008A244 -> ft_8008A2BC):
        // leave Walk when stick is opposite-facing or below the walk threshold.
        // refs/melee/src/melee/ft/ft_0892.c::ft_8008A244
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start) &&
            ((stick_x * facing_dir) < 0.0f || msl_absf(stick_x) < c->walk_stick_threshold)) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          action_id = (uint16_t)MSL_ACT_WAIT;
        }

        // Walk type update without resetting action_frame (ftWalkCommon_800DFEC8 keeps phase).
        // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
        if (action_is_walk(action_id) && action_is_walk(action_id_start) &&
            walk_check_input_accepts(c, stick_x, facing_dir)) {
          const uint16_t want =
              walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
          if (want != batch->state.action_id[idx]) {
            walk_change_type_ftWalkCommon_800DFEC8(batch, idx, want);
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
                                                    stick_y, tilt_timer_x, tilt_timer_y, facing_dir,
                                                    1, 0)) {
            if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ATTACK_DASH) {
              // Run/RunDirect IASA enters AttackDash before Phys in the same fighter proc.
              // ftCo_AttackDash_SetMv0 also seeds fp+0x2340 from p_ftCommonData->x68 so
              // ftCo_800D8AE0 can turn held L/R into a CatchDash during the next AttackDash IASA.
              // This simulator's locomotion IASA pass is post-physics, so restore the missed
              // `ftCo_AttackDash_Phys -> ft_80085030` velocity handoff on the entry frame.
              // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Run.c,ftCo_RunDirect.c}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
              //   ftCo_AttackDash_SetMv0,ftCo_AttackDash_Phys}
              batch->state.attackdash_x0[idx] = (int16_t)c->attackdash_x0_init_frames;
              batch->state.anim_defer_tick_once[idx] = 0u;
              msl_anim_timebase_tick_once(batch, idx);
              (void)physics_apply_attackdash_entry_phys_now(batch, idx, facing_dir);
            }
            action_id = batch->state.action_id[idx];
          } else if (!run_iasa_has_spacie_b_special_intent(cid, buttons_pressed) &&
                     common_appeal_try_enter_from_grounded_iasa(batch, idx, buttons_pressed)) {
            // Run/RunDirect IASA calls ftCo_800DE9D8 after attack/catch/guard and before
            // fn_800CAF78 jump, TurnRun, and RunBrake checks.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
            continue;
          } else {
            const MslJumpInput j_in =
                jump_input_from_fn_800CAF78(c, buttons_pressed, stick_y, tilt_timer_y);
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
            msl_absf(stick_x) < c->run_stick_x_threshold &&
            !run_iasa_has_spacie_b_special_intent(batch->state.char_id[idx], buttons_pressed)) {
          // Decomp order: ftCo_Run_IASA checks the B-special dispatchers before the terminal
          // RunBrake check. The simulator runs special handlers after locomotion, so leave Run
          // intact on a B-special edge and let the later special owner consume without an extra
          // RunBrake Fighter_ChangeMotionState / ft_800895E0 bump.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_SpecialS_CheckInput,ftCo_800D68C0}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_CheckInput
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
        //   - (cmd_vars[0] && fn_800C9CEC) (TurnRun path)
        //   - ftCo_800D5FB0 (Squat check/enter)
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
        if (action_id == MSL_ACT_RUN_BRAKE && action_id_start == MSL_ACT_RUN_BRAKE) {
          const float cur_anim_frame = batch->state.anim_frame_f32[idx];
          const MslJumpInput j_in =
              jump_input_from_fn_800CAF78(c, buttons_pressed, stick_y, tilt_timer_y);
          if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
            batch->state.kneebend_is_short_hop[idx] = 0;
            action_id = (uint16_t)MSL_ACT_KNEE_BEND;
          } else if (batch->state.runbrake_cmd0[idx] != 0u &&
                     (stick_x * facing_dir) <= c->turn_run_stick_x_threshold) {
            // Decomp: RunBrake IASA enters TurnRun via fn_800C9CEC only while cmd_vars[0] is enabled
            // by the RunBrake command script; TurnRun_Enter preserves the current anim frame.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9CEC
            // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
            // Source of truth:
            // - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0).
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN_RUN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN_RUN;
            batch->state.runbrake_cmd0[idx] = 0u;
            msl_anim_timebase_enter(batch, idx, cur_anim_frame, 1.0f);
            action_id = (uint16_t)MSL_ACT_TURN_RUN;
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
          uint8_t dash_checkinput_entered_dash = 0u;
          if (dash_iasa_early_x4 && dash_early_attack_s4_try_enter_from_iasa(
                                        batch, c, idx, buttons_pressed, stick_x, facing_dir)) {
            action_id = batch->state.action_id[idx];
          } else if (cur_anim_frame <= c->dash_iasa_x4c &&
                     grounded_a_attack_try_enter_from_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                           stick_y, tilt_timer_x, tilt_timer_y,
                                                           facing_dir, 1, 0)) {
            // Decomp: AttackDash input is only checked in Dash IASA while cur_anim_frame <= x4C.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
            if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ATTACK_DASH) {
              // Source ordering is Dash_IASA -> AttackDash enter -> AttackDash_Phys in one
              // Fighter_procUpdate. This pass runs after physics, so apply the entry tick and
              // root-motion Phys handoff here instead of leaving AttackDash stationary for one
              // rollout frame.
              // ftCo_AttackDash_SetMv0 seeds fp+0x2340 from p_ftCommonData->x68 before the Phys
              // handoff; ftCo_800D8AE0 then uses that countdown for boost-grab CatchDash.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
              //   doEnter,ftCo_AttackDash_Phys}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8AE0
              batch->state.attackdash_x0[idx] = (int16_t)c->attackdash_x0_init_frames;
              batch->state.anim_defer_tick_once[idx] = 0u;
              msl_anim_timebase_tick_once(batch, idx);
              (void)physics_apply_attackdash_entry_phys_now(batch, idx, facing_dir);
            }
            action_id = batch->state.action_id[idx];
          } else if (!dash_iasa_early_x4 && cur_anim_frame <= c->dash_iasa_x4c &&
                     (stick_x * facing_dir) < 0.0f && msl_absf(stick_x) >= c->dash_flick_abs &&
                     tilt_timer_x < c->dash_flick_tilt_max_frames) {
            // Decomp: the mid Dash IASA branch calls ftCo_Dash_CheckInput before guard/jump, and
            // its opposite-facing x3C/x40 path enters Turn immediately.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
            //   ftCo_Dash_IASA,ftCo_Dash_CheckInput
            // }
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
            batch->state.turn_has_turned[idx] = 0;
            batch->state.turn_frames_to_turn[idx] = 0;
            batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:62-64
            msl_anim_timebase_tick_once(batch, idx);
            dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
            dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
            action_id = (uint16_t)MSL_ACT_TURN;
          } else {
            // Dash -> Common Appeal, then KneeBend (Jump).
            //
            // Decomp: Dash IASA reaches block_42 after catch/attack/guard and calls
            // ftCo_800DE9D8 before fn_800CAF78.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_800DE9D8
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
            if (!run_iasa_has_spacie_b_special_intent(cid, buttons_pressed) &&
                common_appeal_try_enter_from_grounded_iasa(batch, idx, buttons_pressed)) {
              continue;
            }
            const MslJumpInput j_in =
                jump_input_from_fn_800CAF78(c, buttons_pressed, stick_y, tilt_timer_y);
            if (j_in != MSL_JUMP_INPUT_NONE && batch->state.jumps_left[idx] > 0) {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              batch->state.kneebend_jump_input[idx] = (uint8_t)j_in;
              batch->state.kneebend_is_short_hop[idx] = 0;
              dash_to_kneebend_apply_terminal_handoff(batch, ch, idx, facing_dir);
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
                  dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
                  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
                  action_id = (uint16_t)MSL_ACT_TURN;
                } else if ((stick_x * facing_dir) > 0.0f && cur_anim_frame <= c->dash_iasa_x4c &&
                           cur_anim_frame + msl_f32_from_q16_16(
                                                batch->state.frame_speed_mul_fp_q16_16[idx]) >
                               c->dash_iasa_x4c &&
                           is_dash_flick(c, stick_x, tilt_timer_x)) {
                  // Boundary phase: source Dash IASA sees fp->cur_anim_frame after the prio-1
                  // animation callback tick. A replay seed serialized at exactly x4C with a normal
                  // frame-speed tick has crossed into the late `ftCo_Dash_CheckInput` branch by
                  // the time IASA runs, so a same-facing fresh flick re-enters Dash instead of
                  // falling through to the Run gate.
                  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
                  //   ftCo_Dash_IASA,ftCo_Dash_CheckInput,ftCo_Dash_Enter}
                  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                  batch->state.dash_x4[idx] = 1u;
                  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                  msl_anim_timebase_tick_once(batch, idx);
                  batch->state.tilt_timer_x[idx] = 0xFEu;
                  dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
                  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
                  dash_checkinput_entered_dash = 1u;
                  action_id = (uint16_t)MSL_ACT_DASH;
                }
              } else if (cur_anim_frame > c->dash_iasa_x4c &&
                         is_dash_flick(c, stick_x, tilt_timer_x)) {
                // Late Dash IASA still routes through ftCo_Dash_CheckInput before guard / jump /
                // run checks. That helper enters TurnSmash on opposite-facing flicks and re-enters
                // Dash on same-facing flicks via ftCo_Dash_Enter(gobj, 1).
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
                //   ftCo_Dash_IASA,ftCo_Dash_CheckInput
                // }
                if ((stick_x * facing_dir) < 0.0f) {
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
                  batch->state.turn_has_turned[idx] = 0;
                  batch->state.turn_frames_to_turn[idx] = 0;
                  batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
                  batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
                  batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
                  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                  msl_anim_timebase_tick_once(batch, idx);
                  dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
                  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
                  action_id = (uint16_t)MSL_ACT_TURN;
                } else {
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
                  batch->state.action_id[idx] = (uint16_t)MSL_ACT_DASH;
                  batch->state.animation_index[idx] = (uint32_t)MSL_SM_DASH;
                  batch->state.dash_x4[idx] = 1u;
                  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
                  msl_anim_timebase_tick_once(batch, idx);
                  batch->state.tilt_timer_x[idx] = 0xFEu;
                  dash_iasa_apply_root_motion_exit_gr_vel_clamp(batch, ch, idx);
                  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
                  dash_checkinput_entered_dash = 1u;
                  action_id = (uint16_t)MSL_ACT_DASH;
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
              if (action_id == MSL_ACT_DASH && dash_checkinput_entered_dash == 0u &&
                  move_tables_dash_cmd0_active(cid, cur_anim_frame)) {
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
          const uint8_t startup_complete =
              (batch->state.action_frame[idx] >= (int16_t)ch->jump_startup_frames) ? 1u : 0u;
          const uint8_t opponent_active_catch_window =
              locomotion_has_opponent_active_catch_connect_window(batch, bi, p, num_players);
          const uint8_t fresh_kneebend_from_prior_callback =
              (action_id_start != (uint16_t)MSL_ACT_KNEE_BEND ||
               batch->state.guard_jump_oos_entered_this_frame[idx])
                  ? 1u
                  : 0u;

          // KneeBend IASA catch check (JC grab) before the jump transition.
          //
          // Decomp ordering:
          // - ftCo_KneeBend_IASA calls ftCo_Catch_CheckInput before short-hop/jump progression.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
          // - Grounded IASA owners such as Run/RunDirect fn_800CAF78 and
          //   Guard/GuardOn/GuardReflect/GuardOff ftCo_800CB024 can enter KneeBend, but that same
          //   frame is still the source motion state's input callback. The freshly-entered
          //   KneeBend must not also consume KneeBend_IASA until the next fighter proc.
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
          // On startup-complete frames, allow Anim-first Jump ordering unless an opponent currently
          // owns a Catch/CatchDash connect window (then preserve baseline IASA-before-Jump order).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
          //   ftCo_KneeBend_Anim,ftCo_KneeBend_IASA
          // }
          if ((!startup_complete || opponent_active_catch_window) &&
              !fresh_kneebend_from_prior_callback) {
            if (blaster_try_enter_ground_specialhi_from_kneebend_iasa(batch, c, idx)) {
              continue;
            }
            if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
              continue;
            }
            if (kneebend_try_enter_attack_hi4_from_iasa(batch, c, idx, buttons_pressed, stick_y)) {
              continue;
            }
          }

          // Latch short hop state (ftCo_KneeBend_Check_ShortHop).
          //
          // Source ordering:
          // - ftCo_KneeBend_Anim enters JumpF/B as soon as `cur_anim_frame >= jump_startup_time`.
          // - ftCo_KneeBend_IASA only calls ftCo_KneeBend_Check_ShortHop while the fighter is still
          //   in KneeBend after Anim. A release observed on the same frame as Anim-owned Jump entry
          //   is therefore too late to create a short hop; the latched bit must come from an earlier
          //   KneeBend IASA frame or from the replay seed.
          // - A fresh anim-end/IASA destination whose serialized action frame is still 0 has not
          //   had a source KneeBend_IASA proc yet; do not let buffered-release rows latch a short
          //   hop before the first owned KneeBend callback.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
          //   ftCo_KneeBend_Anim,ftCo_KneeBend_IASA,ftCo_KneeBend_Check_ShortHop
          // }
          if (!startup_complete && !fresh_kneebend_from_prior_callback &&
              batch->state.action_frame[idx] > 0 && !batch->state.kneebend_is_short_hop[idx]) {
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

          if (startup_complete) {
            const uint8_t is_short = batch->state.kneebend_is_short_hop[idx] ? 1 : 0;
            const uint8_t full = (uint8_t)(!is_short);
            const float jump_enter_prev_stick_y = apply_deadzone(
                stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
            const uint8_t jump_enter_post_input_tilt_timer_y =
                jump_enter_pre_input_tilt_y_after_input(c, stick_y, jump_enter_prev_stick_y);

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
            locomotion_apply_jump_enter_ground_to_air(batch, idx);
            batch->state.action_id[idx] = jump_act;
            batch->state.animation_index[idx] = (uint32_t)submotion_for_action(jump_act);
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

            // Ground-to-air momentum + jump impulse (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110)
            // Source multiplies fp->self_vel.x, not fp->gr_vel. speed_air_x_self is the visible
            // self_vel.x lane even while grounded; speed_ground_x_self is gr_vel.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110
            const float base_x =
                batch->state.speed_air_x_self[idx] * ch->ground_to_air_jump_momentum_multiplier;
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
            tilt_timer_y = jump_enter_post_input_tilt_timer_y;

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

            // Jump IASA subset on the transition frame:
            // - EscapeAir gate (ftCo_80099A58),
            // - full AttackAir gate from ftCo_AttackAir_CheckInput (A-edge or C-stick edge).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckInput
            if (escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
              continue;
            }
            if (locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
              continue;
            }
            // Jump IASA runs on the JumpF/B entered by KneeBend_Anim in the same proc. After
            // EscapeAir and AttackAir checks, ftCo_800CB870 can immediately consume a fresh
            // jump edge/tap into JumpAerial and overwrite the ground-jump velocity snapshot.
            // ftCo_Jump_Enter's x671=0xFE write is pre-input Anim ownership here; the later
            // Fighter_Spaghetti input-history pass can overwrite it before ftCo_Jump_IASA reads
            // ft_did_jump, and that same post-input value is the post-frame seed unless this branch
            // enters JumpAerial.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
            const uint8_t jump_aerial_input = ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0u ||
                                               did_tap_jump(c, stick_y, tilt_timer_y))
                                                  ? 1u
                                                  : 0u;
            if (locomotion_try_enter_jump_aerial_iasa(batch, c, ch, idx, jump_aerial_input, stick_x,
                                                      facing_dir, 1u)) {
              continue;
            }
            batch->state.tilt_timer_y[idx] = tilt_timer_y;
          }
        }

        // RunBrake -> Wait when animation ends.
        if (action_id == MSL_ACT_RUN_BRAKE) {
          const uint32_t anim = batch->state.animation_index[idx];
          if (anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
            if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                              batch->state.anim_frame_f32[idx])) {
              // RunBrake_Anim resolves through ft_8008A2BC when the motion has no frames remaining.
              // The destination Wait input callback can then run in the same fighter proc; use the
              // existing Wait_IASA locomotion tail instead of a squat-only bridge so Turn/Walk/Dash
              // ownership stays in source order.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Anim
              // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
              batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
              (void)wait_iasa_locomotion_subset_try_enter(
                  batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y, tilt_timer_x,
                  tilt_timer_y, facing_dir, action_id_start);
            }
          }
        }

        if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_FALL &&
            (action_id_start == (uint16_t)MSL_ACT_OTTOTTO ||
             action_id_start == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                        batch->state.facing[idx], batch->state.pos_x[idx])) {
          // Source applies ftCommon_8007E0E4's grounded fighter-overlap x450 displacement after
          // Anim callbacks and before Fighter_procUpdate's motion/collision owner observes the
          // Fall handoff. The common physics nudge intentionally keeps still-grounded Ottotto
          // frames clamped to the floor endpoint; once the pre-input callback has already resolved
          // Ottotto/OttottoWait into Fall, preserve the same xF8_playerNudgeVel.x before physics.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_Coll,ftCo_OttottoWait_Coll}
          const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
          const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
          if (nudge_x * facing_sign > 0.0f) {
            batch->state.pos_x[idx] += nudge_x;
          }
        }

        continue;
      }

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
            side_special_reset_ghost_ring_on_main_entry(batch, idx);
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

          if (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END &&
              anim_finished(cid, ms->specials_air_end, batch->state.anim_frame_f32[idx])) {
            // Decomp: ftFx_SpecialAirSEnd_Anim exits through ftCo_80096900(arg1=1,...), entering
            // FallSpecial and setting mv.co.fallspecial.xC.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
            enter_fall_special_via_ftco_80096900(
                batch, idx, ch != NULL ? (float)ch->illusion_landing_lag_frames : 0.0f);
            action_id = (uint16_t)MSL_ACT_FALL_SPECIAL;
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
        if (batch->state.on_ground[idx] == 0u && anim != 0xFFFFFFFFu && anim <= 0xFFFFu) {
          if (anim_finished(batch->state.char_id[idx], (uint16_t)anim,
                            batch->state.anim_frame_f32[idx])) {
            // Decomp: ftCo_AttackAir_Anim enters Fall via ftCo_Fall_Enter (KeepFastFall set).
            // This anim owner is the airborne AttackAir path; grounded AttackAir continuation is
            // owned by the floor-contact/grounded callback path and must not synthesize an
            // intermediate Fall motion-state entry before grounded IASA can enter Squat/Wait.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
            enter_fall_keep_fastfall_ftco_fall_enter(batch, idx);
            ftco_fall_enter_clamp_air_drift_x(batch, ch, idx);
            action_id = (uint16_t)MSL_ACT_FALL;
            is_attack_air = 0;
            is_air_loco = 1;
          }
        }
      }

      if (action_id == (uint16_t)MSL_ACT_PASS) {
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_PASS;
        if (anim_finished(cid, (uint16_t)MSL_SM_PASS, batch->state.anim_frame_f32[idx])) {
          // Pass_Anim exits through ftCo_Fall_Enter when the short pass-through animation ends.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Anim
          enter_fall_keep_fastfall_ftco_fall_enter(batch, idx);
          action_id = (uint16_t)MSL_ACT_FALL;
          is_air_loco = 1u;
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
        // Decomp entry check: ftCo_80099A58 (L/R press) is called from Jump/Fall-family IASA
        // owners, but not from DamageFall_IASA and not from FallSpecial.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
        // Hidden EntryEnd -> Fall control lock:
        // - EntryEnd has no IASA body, but its timer expiry enters ordinary Fall via
        //   ftCommon_8007D92C.
        // - Controlled vanilla playback shows the resulting airborne descent still suppresses the
        //   ordinary Fall aerial IASA family until landing.
        // refs/melee/src/melee/ft/ft_0C31.c::{ftCo_EntryEnd_Anim,ftCo_EntryEnd_IASA}
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
        const uint8_t allow_escape_air =
            (action_id == MSL_ACT_JUMP_F || action_id == MSL_ACT_JUMP_B ||
             action_id == MSL_ACT_JUMP_AERIAL_F || action_id == MSL_ACT_JUMP_AERIAL_B ||
             action_is_fall_like(action_id) || action_id == (uint16_t)MSL_ACT_PASS)
                ? 1
                : 0;
        const uint8_t speciallw_preempts_escapeair =
            spacie_speciallw_pressed(c, cid, buttons_pressed, stick_y);
        if (allow_escape_air && !speciallw_preempts_escapeair &&
            escape_air_try_enter_from_air_locomotion(batch, c, idx)) {
          continue;
        }

        // Aerial attack (AttackAir*) entry from eligible air locomotion states.
        //
        // Decomp ordering: common aerial IASA owners check ftCo_SpecialAir_CheckInput before
        // ftCo_AttackAir_CheckItemThrowInput. This simulator models Fox/Falco B-specials in the
        // later Shine/Blaster passes, so leave B-edge rows in the source aerial state here and let
        // those passes consume the same frame.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
        if ((buttons_pressed & (uint16_t)MSL_BUTTON_B) == 0u &&
            locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
          continue;
        }

        // Aerial jump (double jump) entry.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
        // JumpF/B IASA uses ftCo_800CB870 after Fighter_Spaghetti updates the live input history.
        // Teacher-forced seeds can carry ftCo_Jump_Enter's post-frame x671=0xFE while the next
        // source callback still observes the just-consumed held jump source at a frame-0 JumpF/B
        // boundary. Keep this reconstruction scoped to that boundary; Fall/DamageFall and
        // AttackAir still use the ordinary x671-style did_tap_jump gate.
        // refs/melee/src/melee/ft/fighter.c::{
        //   Fighter_Spaghetti_8006AD10,Fighter_procUpdate}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{
        //   ftCo_Jump_IASA,ftCo_800CB110}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
        //   ftCo_800CB870,ft_did_jump}
        const float jump_iasa_prev_stick_y = apply_deadzone(
            stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
        const uint8_t jump_source_released =
            ((batch->state.prev_input_buttons[idx] & (uint16_t)MSL_BUTTON_XY) != 0u &&
             (buttons & (uint16_t)MSL_BUTTON_XY) == 0u)
                ? 1u
                : 0u;
        const uint8_t jump_tap_crossed = (action_id_start == (uint16_t)MSL_ACT_JUMP_F ||
                                          action_id_start == (uint16_t)MSL_ACT_JUMP_B) &&
                                                 batch->state.prev_action_frame[idx] <= 0 &&
                                                 jump_source_released &&
                                                 jump_iasa_prev_stick_y < c->tap_jump_threshold &&
                                                 stick_y >= c->tap_jump_threshold
                                             ? 1u
                                             : 0u;
        const uint8_t jump_aerial_input =
            ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) ||
             did_tap_jump(c, stick_y, tilt_timer_y) || jump_tap_crossed)
                ? 1u
                : 0u;
        // FallSpecial has no JumpAerial IASA owner. It remains in the air-locomotion set so
        // physics/landing callbacks still run, but its input callback does not call
        // ftCo_800CB870.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
        const uint8_t allow_jump_aerial =
            (action_id == MSL_ACT_FALL_SPECIAL || action_id == MSL_ACT_FALL_SPECIAL_F ||
             action_id == MSL_ACT_FALL_SPECIAL_B)
                ? 0u
                : 1u;
        if (allow_jump_aerial &&
            locomotion_try_enter_jump_aerial_iasa(batch, c, ch, idx, jump_aerial_input, stick_x,
                                                  facing_dir, 1u)) {
          tilt_timer_y = 0xFEu;
          action_id = batch->state.action_id[idx];
        }

        uint8_t damagefall_x670_timer_for_iasa = batch->state.tilt_timer_x[idx];
        if (action_id == (uint16_t)MSL_ACT_DAMAGE_FALL) {
          const float prev_stick_x = apply_deadzone(
              stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
          // DamageFall_IASA is reached from Fighter_procUpdate after the input-history proc, but
          // teacher-forced seeds can begin with an already-held X stick whose current-row update
          // has advanced x670 one tick past the value vanilla's callback observes in the local
          // DamageFall handoff. Reconstruct that pre-increment value only for held-stick rows;
          // fresh X-direction entries keep the current x670=0 edge from Fighter_Spaghetti.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10,Fighter_procUpdate}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
          if (stick_x >= c->lstick_tilt_x_thresh) {
            if (prev_stick_x >= c->lstick_tilt_x_thresh) {
              if (damagefall_x670_timer_for_iasa > 0u && damagefall_x670_timer_for_iasa < 0xFEu) {
                damagefall_x670_timer_for_iasa = (uint8_t)(damagefall_x670_timer_for_iasa - 1u);
              }
            }
          } else if (stick_x <= -c->lstick_tilt_x_thresh) {
            if (prev_stick_x <= -c->lstick_tilt_x_thresh) {
              if (damagefall_x670_timer_for_iasa > 0u && damagefall_x670_timer_for_iasa < 0xFEu) {
                damagefall_x670_timer_for_iasa = (uint8_t)(damagefall_x670_timer_for_iasa - 1u);
              }
            }
          }
        }

        if (action_id == (uint16_t)MSL_ACT_DAMAGE_FALL &&
            msl_absf(stick_x) >= c->damagefall_fall_stick_x_threshold &&
            damagefall_x670_timer_for_iasa < c->damagefall_fall_tilt_max_frames) {
          // DamageFall IASA stick-fall gate:
          // - after aerial IASA checks (special/item/aircatch/jump),
          // - if ABS(lstick.x) >= p_ftCommonData->x210 and x670_timer_lstick_tilt_x < x214,
          //   enter Fall.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
          // refs/melee/src/melee/ft/types.h (fp+0x670 timer, ftCommonData x210/x214)
          enter_fall_keep_fastfall_ftco_fall_enter(batch, idx);
          continue;
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

          // Aerial attack re-entry from AttackAir DO_IASA:
          // DO_IASA calls checkItemThrowInput before the JumpAerial helper, and that helper routes
          // A/c-stick input back through ftCo_AttackAir_EnterFromMsid for ordinary Fox/Falco rows.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
          //   DO_IASA,checkItemThrowInput,ftCo_AttackAir_EnterFromMsid}
          if (locomotion_attackair_try_enter_from_air_iasa(batch, c, idx)) {
            continue;
          }

          // Aerial jump (double jump) entry.
          //
          // AttackAir DO_IASA does not call ftCo_SpecialAir_CheckInput. B-specials are not an
          // AttackAir IASA branch, so a concurrent B edge must not block the later JumpAerial
          // branch when the jump input is otherwise valid.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::DO_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
          const uint8_t jump_aerial_input = ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) ||
                                             did_tap_jump(c, stick_y, tilt_timer_y))
                                                ? 1u
                                                : 0u;
          if (locomotion_try_enter_jump_aerial_iasa(batch, c, ch, idx, jump_aerial_input, stick_x,
                                                    facing_dir, 0u)) {
            tilt_timer_y = 0xFEu;
            action_id = batch->state.action_id[idx];
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
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            } else {
              // Decomp: ground Jump_Anim enters Fall through ftCo_Fall_Enter (KeepFastFall set).
              // ftCo_Fall_Enter immediately calls ftCommon_ClampAirDrift before the destination
              // Fall Phys callback runs in the same Fighter proc. Without this clamp, terminal
              // JumpF/B rows can carry super-max air drift into Fall for one frame.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
              // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ClampAirDrift
              enter_fall_keep_fastfall_ftco_fall_enter(batch, idx);
              ftco_fall_enter_clamp_air_drift_x(batch, ch, idx);
            }
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

      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1 : 0;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1 : 0;

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      const MslSpecialMsids* ms = msl_special_msids(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t frame_start_ft80083f88_floor_loss =
          // Source grounded collision callbacks such as SquatRv_Coll are selected from the
          // frame-start grounded action and call ft_80083F88. If Phys/edge handling clears MSL's
          // transient `prev_on_ground` before mpColl, that callback still owns the Ground->Air
          // floor-loss transition through ft_80082708. Use the generated callback class only for
          // the floor-loss branch below; grounded->grounded edge/teeter publication continues to
          // consume the post-Phys `prev_on_ground` state.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
          // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class FT80083F88_GROUND_TO_AIR_COLL)
          (batch->state.frame_start_on_ground[idx] != 0u && was_ground == 0u &&
           msl_motion_state_class_has(batch->state.char_id[idx], a,
                                      MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL))
              ? 1u
              : 0u;
      if (batch->state.hitlag_started_frame[idx] != 0) {
        if (!was_ground && now_ground && action_is_attackair(a)) {
          const uint16_t land = attackair_landing_action_for_contact(batch, idx, a);
          if (land != 0u) {
            enter_landing_action_from_air(batch, ch, idx, (size_t)bi, a, land);
          }
        }
        continue;
      }

      const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
      const uint16_t floor_skip_segment = (batch->state.floor_skip_segment_id != NULL)
                                              ? batch->state.floor_skip_segment_id[idx]
                                              : 0xFFFFu;
      const uint8_t keep_attackair_transformed_platform_floor_skip =
          (floor_skip_segment != 0xFFFFu && action_is_attackair(a) && now_ground == 0u &&
           stage_collision_floor_line_has_height_platform_transform(stage_id, floor_skip_segment))
              ? 1u
              : 0u;
      const uint8_t keep_common_air_transformed_platform_floor_skip =
          (floor_skip_segment != 0xFFFFu && now_ground == 0u &&
           stage_collision_floor_line_has_height_platform_transform(stage_id, floor_skip_segment) &&
           batch->state.prev_action_id[idx] == a &&
           msl_motion_state_common_class_has(a, MSL_MS_CLASS_COMMON_AIR_COLL))
              ? 1u
              : 0u;
      const uint8_t keep_shine_platform_pass_floor_skip =
          // Fox/Falco Reflector can enter the aerial start state through
          // `ftFx_SpecialLwStart_Pass -> ftCo_8009A184`, which writes CollData.floor_skip for the
          // platform currently being passed. Preserve that source floor-skip only inside the same
          // Shine pass episode: grounded Start -> aerial Start -> sustained aerial Loop/End. This
          // keeps modelplay platform-drop Shine from immediately snapping back to the passed
          // platform without letting arbitrary replay seeds carry stale platform skips into
          // unrelated aerial specials.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
          //   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll,
          //   ftFx_SpecialAirLwEnd_Coll}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A184
          // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
          (floor_skip_segment != 0xFFFFu && now_ground == 0u &&
           stage_collision_floor_line_is_platform(stage_id, floor_skip_segment) &&
           (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START ||
            a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP ||
            a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END) &&
           (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
            batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START ||
            batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP ||
            batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END))
              ? 1u
              : 0u;
      if (batch->state.floor_skip_segment_id != NULL && floor_skip_segment != 0xFFFFu &&
          a != (uint16_t)MSL_ACT_PASS && a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
          !keep_attackair_transformed_platform_floor_skip &&
          !keep_common_air_transformed_platform_floor_skip &&
          !keep_shine_platform_pass_floor_skip) {
        // Fighter_ChangeMotionState clears CollData.floor_skip via mpClearFloorSkip. The current
        // frame has already consumed the old skip in stage collision, so clear it here for later
        // contacts once Pass has handed off to jump/aerial/fall/landing owners. SpecialAirHi is the
        // other retained owner: ftFox_SpecialHi_IsBound calls ftCo_8009A134 on platform contact,
        // which writes floor_skip without changing motion state so the launch can continue through
        // that same platform. AttackAir_Coll does not call mpUpdateFloorSkip, but FoD
        // height-transformed AttackAirN/Lw pass-through contacts can keep the same source
        // floor-skip owner live while airborne. Common-air callbacks preserve a transformed-platform
        // skip only through same-action callback continuity and the generated MSLMSO01 common-air
        // collision class. Shine preserves only the explicit platform-pass episode described above.
        // A real unrelated Fighter_ChangeMotionState clears CollData.floor_skip.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_IsBound
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
        msl_mpcoll_clear_floor_skip(batch, idx);
      }
      if (now_ground && batch->state.floor_skip_segment_id != NULL &&
          batch->state.floor_skip_segment_id[idx] != 0xFFFFu &&
          batch->state.ground_id[idx] != batch->state.floor_skip_segment_id[idx]) {
        // mpClearFloorSkip-style re-admission once another floor owns CollData.floor.index.
        // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip}
        msl_mpcoll_clear_floor_skip(batch, idx);
      }
      if (now_ground && batch->state.ledge_drop_floor_skip_segment_id != NULL &&
          batch->state.ledge_drop_floor_skip_segment_id[idx] != 0xFFFFu) {
        // Ledge-drop stale platform rejection is local to the airborne recovery from ledge. Once a
        // floor owns the fighter again, the hidden skip has been consumed.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
        // refs/melee/src/melee/mp/mpcoll.c::mpClearFloorSkip
        batch->state.ledge_drop_floor_skip_segment_id[idx] = 0xFFFFu;
      }
      if (now_ground && batch->state.cliff_ledge_floor_segment_id != NULL &&
          batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu) {
        // Grounded transfer consumes the Cliff/CollData ledge floor owner. Later grounded callbacks
        // use the normal floor.index selected by mpColl.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D7FC
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
        batch->state.cliff_ledge_floor_segment_id[idx] = 0xFFFFu;
        if (batch->state.cliff_ledge_floor_segment_seeded != NULL) {
          batch->state.cliff_ledge_floor_segment_seeded[idx] = 0u;
        }
      }
      if (!now_ground && batch->state.cliff_ledge_floor_segment_id != NULL &&
          batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
          (batch->state.ledge_cooldown[idx] == 0u ||
           !action_preserves_cliff_ledge_floor_owner(a))) {
        // Once ledge-release cooldown expires, or once a non-cliff-exit aerial state owns
        // collision, the stored cliff ledge floor is no longer the source CollData floor owner.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/mp/mpcoll.c::mpClearFloorSkip
        batch->state.cliff_ledge_floor_segment_id[idx] = 0xFFFFu;
        if (batch->state.cliff_ledge_floor_segment_seeded != NULL) {
          batch->state.cliff_ledge_floor_segment_seeded[idx] = 0u;
        }
      }
      if (now_ground && ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR) {
        // ftFx_SpecialHiHoldAir_Coll consumes ft_CheckGroundAndLedge and immediately routes
        // through ftFx_SpecialHiHoldAir_AirToGround. The AirToGround handler calls
        // ftCommon_8007D7FC, then changes to the grounded hold motion while preserving the current
        // animation frame.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        //   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialHiHoldAir_AirToGround}
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD;
        batch->state.animation_index[idx] = (uint32_t)ms->specialhi_ground_hold;
        batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
        continue;
      }
      if (!now_ground && a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) {
        specialhi_apply_collision_facing_dir(batch, ch, idx);
      }
      if (!now_ground && ft_check_ground_and_ledge_collision_contact(batch, idx)) {
        if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
          // Decomp: SpecialAirSStart_Coll calls ft_CheckGroundAndLedge, and any accepted mpColl
          // collision enters the grounded start motion through ftFx_SpecialAirSStart_AirToGround.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Coll
          side_special_air_to_ground_transition(batch, ms, ch, idx,
                                                (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START);
          continue;
        }
        if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S) {
          // Decomp: SpecialAirS_Coll uses the same ft_CheckGroundAndLedge -> AirToGround owner as
          // the start state.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Coll
          side_special_air_to_ground_transition(batch, ms, ch, idx,
                                                (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S);
          continue;
        }
      }
      if (!now_ground && try_common_air_walljump_post_collision(batch, c, ch, idx, a)) {
        continue;
      }

      if (was_ground && now_ground && a == (uint16_t)MSL_ACT_KNEE_BEND &&
          (batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_XY) == 0u &&
          did_tap_jump(c, stick_i8_to_unit(batch->state.input_main_y[idx]),
                       batch->state.tilt_timer_y[idx]) &&
          (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_OTTOTTO ||
           batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
          ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                      batch->state.facing[idx], batch->state.pos_x[idx])) {
        // Ottotto_IASA can enter KneeBend, but the new state's collision callback still owns the
        // edge floor-loss handoff. KneeBend_Coll calls ft_80083F88, which routes through
        // ft_80082708 and enters Fall when the allow-ground-to-air helper reports an edge exit.
        // Keep this scoped to the immediate Ottotto/OttottoWait -> KneeBend edge row so ordinary
        // grounded jump squat rows still remain grounded.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
        const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
        const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
        if (nudge_x * facing_sign > 0.0f) {
          batch->state.pos_x[idx] += nudge_x;
        }
        batch->state.on_ground[idx] = 0u;
        enter_fall_from_grounded_floor_loss(batch, ch, idx);
        continue;
      }

      if (!now_ground && action_is_grounded_guard_state(a)) {
        // Guard Coll callbacks are grounded owners. If a replay-seeded or continuous rollout state
        // reaches post-collision with no floor, the shield motion must not persist airborne.
        //
        // Decomp:
        // - GuardOn/Guard/GuardOff/GuardReflect Coll call ft_800845B4.
        // - GuardSetOff_Coll calls ft_800845B4, or ft_80084104 while shield SDI is allowed.
        // - Both helpers leave the shield motion on floor loss and enter an airborne state.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_GuardOn_Coll,ftCo_Guard_Coll,ftCo_GuardOff_Coll,
        //   ftCo_GuardSetOff_Coll,ftCo_GuardReflect_Coll}
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
        if (guard_floor_loss_should_missfoot(batch, idx, batch->state.stage_id[(size_t)bi])) {
          enter_missfoot_from_ground_floor_loss(batch, ch, idx);
          continue;
        }
        enter_fall_from_grounded_floor_loss(batch, ch, idx);
        continue;
      }

      if (was_ground && now_ground && a == (uint16_t)MSL_ACT_WAIT &&
          grounded_attack_submotion_from_action(batch->state.prev_action_id[idx]) != 0xFFFFFFFFu &&
          batch->state.action_frame[idx] <= 0 &&
          ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                      batch->state.facing[idx], batch->state.pos_x[idx])) {
        float ottotto_x = 0.0f;
        float ottotto_y = 0.0f;
        if (ottotto_edge_point_for_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx], &ottotto_x, &ottotto_y)) {
          const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
          const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
          if (!(nudge_x * facing_sign > 0.0f)) {
            continue;
          }
          // Grounded Attack* anim-end can install Wait before the same frame's collision callback
          // dispatch. Source then runs Wait_Coll -> ft_80084280 and admits Ottotto on Collide_Edge.
          // Keep this scoped to rows where the source xF8_playerNudgeVel.x owner has an outward
          // x450 overlap displacement at the edge. Without that provenance, already-offset
          // grounded rows enter Wait first and take the normal Wait_Coll teeter path next frame.
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Coll
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_8009A3C8
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_OTTOTTO;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_OTTOTTO;
          batch->state.pos_x[idx] = ottotto_x;
          batch->state.pos_y[idx] =
              ottotto_edge_root_y_for_entry(batch, bi, idx, batch->state.ground_id[idx], ottotto_y);
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          continue;
        }
      }

      if (was_ground && now_ground && a == (uint16_t)MSL_ACT_LANDING &&
          ft80084280_ottotto_edge_admits(batch, c, bi, p, a)) {
        const float landing_nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
        const float landing_facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
        const uint8_t landing_grounded_overlap_depth =
            (batch->state.pos_z[idx] < -1.0e-6f || batch->state.pos_z[idx] > 1.0e-6f) ? 1u : 0u;
        const uint8_t landing_source_stationary_edge =
            (batch->state.speed_ground_x_self[idx] == 0.0f &&
             batch->state.speed_air_x_self[idx] == 0.0f)
                ? 1u
                : 0u;
        if (!(landing_nudge_x * landing_facing_sign > 0.0f) &&
            landing_grounded_overlap_depth == 0u && landing_source_stationary_edge == 0u) {
          continue;
        }
        float ottotto_x = 0.0f;
        float ottotto_y = 0.0f;
        if (ottotto_edge_point_for_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx], &ottotto_x, &ottotto_y)) {
          // Landing_Coll calls ft_80084280, which in turn calls ftCo_8009A3C8 after
          // mpColl_8004B4B0 even if the floor helper kept the fighter grounded. The retained
          // same-ground slice is the source-visible non-self-motion edge case:
          // - recomputed outward xF8_playerNudgeVel.x from the current player-overlap helper,
          // - nonzero prefix-causal hidden `pos_z` proving the same ftCommon_8007DD7C/8007E0E4
          //   grounded-overlap owner was live before collision refresh, or
          // - exact zero self ground/air x velocity from the Landing_Coll source state, proving the
          //   edge bit is not from this fighter's own horizontal floor sweep.
          // Rough replay Collide_Edge alone is not precise enough to emulate every self-moving
          // ft_80084280 edge result, so moving Landing rows keep their ordinary source collision
          // result.
          // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_OTTOTTO;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_OTTOTTO;
          batch->state.pos_x[idx] = ottotto_x;
          batch->state.pos_y[idx] =
              ottotto_edge_root_y_for_entry(batch, bi, idx, batch->state.ground_id[idx], ottotto_y);
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          continue;
        }
      }

      if (was_ground && now_ground &&
          (a == (uint16_t)MSL_ACT_OTTOTTO || a == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
          ottotto_edge_matches_facing(batch, bi, batch->state.ground_id[idx],
                                      batch->state.facing[idx], batch->state.pos_x[idx])) {
        float ottotto_x = 0.0f;
        float ottotto_y = 0.0f;
        if (ottotto_edge_point_for_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx], &ottotto_x, &ottotto_y)) {
          // Ottotto_Coll/OttottoWait_Coll stay on the floor only while the facing endpoint remains
          // the source edge; the collision pass owns the endpoint anchor after any pre-collision
          // xF8 fighter-overlap displacement. Keep this limited to steady teeter states that are
          // already on their facing endpoint.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_Coll,ftCo_OttottoWait_Coll}
          batch->state.pos_x[idx] = ottotto_x;
          batch->state.pos_y[idx] =
              ottotto_edge_root_y_for_entry(batch, bi, idx, batch->state.ground_id[idx], ottotto_y);
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          continue;
        }
      }

      if (was_ground && !now_ground && action_is_catch_start_floor_loss(a)) {
        // Catch/CatchDash collision callbacks are grounded owners. On floor loss they call the
        // common floor-loss helper with fn_800D8E30, which performs catch cleanup and enters Fall.
        // Without this callback the action can stay in Catch until Anim finishes, then become
        // airborne Wait while still carrying its pre-grab ground slide.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
        //   ftCo_Catch_Coll,ftCo_CatchDash_Coll,fn_800D8E30}
        // refs/melee/src/melee/ft/ft_081B.c::ft_800841B8
        enter_fall_from_grounded_floor_loss(batch, ch, idx);
        continue;
      }

      if (!was_ground && !now_ground && action_is_ground_locomotion(a) &&
          batch->state.seed_prev_action_id[idx] == a &&
          batch->state.seed_prev_action_frame[idx] >= 0) {
        // Runtime consistency repair for externally restored/training states that have already
        // persisted as airborne grounded locomotion for at least one source snapshot. Normal source
        // collision callbacks should prevent this state; require the strictly-causal same-action
        // seed lane so ordinary public one-step seeds with omitted on_ground do not get rewritten.
        // Once proven persistent, route through the existing floor-loss helper to keep batch
        // stepping bounded.
        enter_fall_from_grounded_floor_loss(batch, ch, idx);
        continue;
      }

      if (!was_ground && now_ground) {
        if (a == (uint16_t)MSL_ACT_LANDING &&
            landing_contact_y_owner_matches_source(a, batch->state.action_frame[idx],
                                                   batch->state.prev_action_id[idx],
                                                   (uint16_t)MSL_ACT_LANDING)) {
          // Source-order guard: some post-collision callback lanes can already be in Landing before this
          // locomotion transition resolver runs. Preserve floor-contact Y for the same decomp-owned
          // Jump/SpecialAirN collision families used by the landing owner helper above.
          batch->state.pos_y[idx] = landing_root_y_from_mpcoll_contact(batch, idx, (size_t)bi);
        }

        if (a == (uint16_t)MSL_ACT_SHIELD_BREAK_FLY || a == (uint16_t)MSL_ACT_SHIELD_BREAK_FALL) {
          enter_shieldbreak_down_from_floor_contact(batch, ch, idx, a);
          continue;
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
          land = attackair_landing_action_for_contact(batch, idx, a);
        } else if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
          side_special_air_to_ground_transition(batch, ms, ch, idx,
                                                (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START);
          continue;
        } else if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S) {
          side_special_air_to_ground_transition(batch, ms, ch, idx,
                                                (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S);
          continue;
        } else if (a == (uint16_t)MSL_ACT_ESCAPE_AIR) {
          // EscapeAir: EscapeAir_Coll -> ft_80082C74(..., ftCo_80099D70) -> ftCo_LandingFallSpecial_Enter(..., x344)
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
          land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
                   a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
                   a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END) {
          // Fox/Falco AirCatchHit_Coll uses ft_80082B1C, the same velocity-gated Wait/Landing
          // split as Fall/Jump.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ftCo_AirCatchHit_Coll,ft_80082B1C}
          land = ft80082b1c_basic_landing_action(batch, c, idx, a);
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
          // Decomp: ftFx_SpecialAirSEnd_Coll enters LandingFallSpecial on ground contact.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
          land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL) {
          // Decomp: ftFx_SpecialHiFall_Coll -> ftFx_SpecialHiFall_Enter transitions to
          // SpecialHiLanding with anim_start=13 and immediate anim tick.
          // ftFx_SpecialHiFall_Enter calls ftCommon_8007D7FC before ChangeMotionState, so the
          // landing row also refreshes grounded jumps.
          // ChangeMotionState flags do not include KeepFastFall on this transition, so fall_fast is
          // cleared at landing entry.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
          //   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiFall_Enter
          // }
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          const float landing_self_vel_x = batch->state.speed_air_x_self[idx];
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_FX_SPECIAL_HI_LANDING;
          batch->state.speed_ground_x_self[idx] = landing_self_vel_x;
          batch->state.speed_air_x_self[idx] = landing_self_vel_x;
          batch->state.jumps_left[idx] = ch->max_jumps;
          batch->state.fall_fast[idx] = 0u;
          batch->state.pos_y[idx] = landing_root_y_from_mpcoll_contact(batch, idx, (size_t)bi);
          msl_anim_timebase_enter_with_policy(batch, idx, 13.0f, 1.0f,
                                              MSL_ANIM_ENTER_TICK_IMMEDIATE);
          continue;
        } else if (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) {
          // Decomp: launch collision can enter the rebound state instead of a generic landing.
          // The rebound state remains airborne on the entry row; Bound collision owns any later
          // ground/air handling.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
          //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}
          enter_specialhi_bound_from_airhi_collision(batch, ch, idx);
          continue;
        }

        // Locomotion-only fallback: fall states land into Landing/LandingFallSpecial.
        if (land == 0 && action_is_air_locomotion(a)) {
          land = ft80082b1c_basic_landing_action(batch, c, idx, a);
          if (a == MSL_ACT_FALL_SPECIAL || a == MSL_ACT_FALL_SPECIAL_F ||
              a == MSL_ACT_FALL_SPECIAL_B || a == MSL_ACT_LANDING_FALL_SPECIAL) {
            land = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
          }
        }

        // Only refresh jumps / enter a landing action when we actually take a landing transition.
        if (land != 0) {
          enter_landing_action_from_air(batch, ch, idx, (size_t)bi, a, land);
        }
      } else if ((was_ground || frame_start_ft80083f88_floor_loss) && !now_ground) {
        batch->state.fall_fast[idx] = 0;

        float ottotto_x = 0.0f;
        float ottotto_y = 0.0f;
        if (ft80084280_ottotto_edge_admits(batch, c, bi, p, a) &&
            ottotto_edge_point_for_facing(batch, bi, batch->state.ground_id[idx],
                                          batch->state.facing[idx], &ottotto_x, &ottotto_y)) {
          // Ottotto (teeter) entry on common grounded edge walk-off:
          // - ft_80084280_inline calls mpColl_8004B4B0, whose `mpColl_8004A678_Floor` fallback
          //   sets Collide_Edge only for the facing endpoint and non-hard-out stick range.
          // - ft_80084280 then lets ftCo_8009A3C8 consume that Collide_Edge before generic Fall.
          // - ftCo_8009A410 enters Ottotto and zeros self/ground velocity.
          // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
          batch->state.on_ground[idx] = 1u;
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_OTTOTTO;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_OTTOTTO;
          batch->state.pos_x[idx] = ottotto_x;
          batch->state.pos_y[idx] =
              ottotto_edge_root_y_for_entry(batch, bi, idx, batch->state.ground_id[idx], ottotto_y);
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          continue;
        }

        // Only force Ground->Air transitions for ground locomotion states for now.
        // Many non-locomotion ground states transition into specific aerial variants (e.g. FallSpecial),
        // which we do not model yet; forcing Fall here causes large action_id regressions.
        if (!action_is_ground_locomotion(a)) {
          // Decomp: grounded Illusion/Phantasm start/main lose ground through dedicated
          // GroundToAir callbacks that preserve current animation frame while entering the air
          // motion-state counterpart.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
          //   ftFx_SpecialSStart_GroundToAir,ftFx_SpecialS_GroundToAir
          // }
          if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_S_START) {
            side_special_ground_to_air_transition(batch, ms, ch, idx,
                                                  (uint16_t)MSL_ACT_FX_SPECIAL_S_START);
            continue;
          }
          if (ms != NULL && a == (uint16_t)MSL_ACT_FX_SPECIAL_S) {
            side_special_ground_to_air_transition(batch, ms, ch, idx,
                                                  (uint16_t)MSL_ACT_FX_SPECIAL_S);
            continue;
          }
          // Decomp: grounded SpecialSEnd collision falls directly into Fall when ground is lost.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Coll
          if (a == (uint16_t)MSL_ACT_FX_SPECIAL_S_END) {
            enter_fall_from_grounded_floor_loss(batch, ch, idx);
            continue;
          }
          if (action_is_grounded_specialn_ft80083f88_floor_loss(batch->state.char_id[idx], a)) {
            enter_fall_from_grounded_floor_loss(batch, ch, idx);
            continue;
          }
          // Decomp: ftFx_SpecialHiLanding_Coll enters FallSpecial when no longer grounded.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Coll
          if (a == (uint16_t)MSL_ACT_FX_SPECIAL_HI_LANDING) {
            enter_fall_special_via_ftco_80096900(
                batch, idx, ch != NULL ? (float)ch->firefox_landing_lag_frames : 0.0f);
          }
          continue;
        }

        // Ottotto->Dash first-frame carry:
        // - Ottotto_IASA can immediately route into Dash via ftCo_Dash_CheckInput.
        // - The collision owner still keeps the fighter grounded at the edge on the first
        //   Ottotto/OttottoWait -> Dash carry row instead of falling off.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
        //   ftCo_Ottotto_IASA,ftCo_Ottotto_Coll,ftCo_OttottoWait_Coll}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_CheckInput,ftCo_Dash_Enter}
        if (a == (uint16_t)MSL_ACT_DASH &&
            (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_OTTOTTO ||
             batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_OTTOTTO_WAIT) &&
            batch->state.action_frame[idx] <= 1) {
          batch->state.on_ground[idx] = 1u;
          batch->state.pos_x[idx] = batch->state.prev_pos_x[idx];
          batch->state.pos_y[idx] = batch->state.prev_pos_y[idx];
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          continue;
        }

        if (a == (uint16_t)MSL_ACT_OTTOTTO || a == (uint16_t)MSL_ACT_OTTOTTO_WAIT) {
          // Source applies ftCommon_8007E0E4's grounded fighter-overlap x450 displacement before
          // the current motion state's collision callback. The generic physics nudge keeps
          // still-grounded Ottotto frames clamped to the floor endpoint, but if this same collision
          // pass has already proven floor loss, preserve the source xF8_playerNudgeVel.x before
          // entering Fall.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
          //   ftCo_Ottotto_Coll,ftCo_OttottoWait_Coll}
          const float nudge_x = ottotto_floor_loss_player_nudge_x(batch, c, bi, p);
          const float facing_sign = batch->state.facing[idx] ? 1.0f : -1.0f;
          if (nudge_x * facing_sign > 0.0f) {
            batch->state.pos_x[idx] += nudge_x;
          }
        }

        if (a == (uint16_t)MSL_ACT_DASH &&
            batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_TURN &&
            batch->state.action_frame[idx] <= 1 && batch->state.speed_air_x_self[idx] != 0.0f) {
          const uint8_t dash_source_facing = batch->state.speed_air_x_self[idx] > 0.0f ? 1u : 0u;
          if (dash_turn_floor_loss_is_source_facing_stage_ledge(
                  batch, bi, batch->state.ground_id[idx], dash_source_facing,
                  batch->state.pos_x[idx])) {
            // Turn_IASA can route through its just-turned `x8/facing_after` lane into Dash, then
            // Dash_Coll immediately leaves the floor through ft_800844EC. Preserve the Dash
            // source-facing lane for the resulting Fall; otherwise the replay-visible Turn facing
            // can make the later ledge pass test the stage ledge behind the source trajectory.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
            //   ftCo_Turn_IASA,fn_800C9C2C}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
            batch->state.facing[idx] = dash_source_facing;
          }
        }

        // Ground locomotion -> Fall when no longer grounded.
        // Decomp refs for the common collision helpers:
        // - refs/melee/src/melee/ft/ft_081B.c:1066 (`ft_80084280`) (Wait/Walk/etc)
        // - refs/melee/src/melee/ft/ft_081B.c:1114 (`ft_800844EC`) (Dash/Run) which may enter StopWall via
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopWall.c:20 (`ftCo_8009EDA4`)
        //
        // Note: we don't yet model wall hug / StopWall, so we conservatively enter Fall here.
        enter_fall_from_grounded_floor_loss(batch, ch, idx);
      }
    }
  }
}
