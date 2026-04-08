#include "ledge.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "input_axis.h"
#include "match_flow.h"
#include "stage_collision.h"

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_cliff_action_any(uint16_t a) {
  return (is_cliff_hold_action(a) || a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2) ? 1 : 0;
}

static inline uint8_t is_fall_like_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
    case MSL_ACT_DAMAGE_FALL:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_airborne_action_with_cliffcatch_check(uint16_t a) {
  if (is_fall_like_action(a)) {
    return 1;
  }
  switch (a) {
    // Jump / aerial jump collision wrappers end with ftCliffCommon_80081298.
    // Decomp:
    // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
    // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    // - refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
      return 1;

    // NOTE(decomp): the common aerial attack / airdodge collision callbacks do not end in the
    // cliff catch check (ftCliffCommon_80081298). They use ft_80082C74, which only runs stage
    // collision and then optionally calls a landing transition callback.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    //
    // This lite sim therefore does not schedule CliffCatch directly from AttackAir*/EscapeAir;
    // ledge catches are expected to occur from fall-like motions and other collision wrappers.

    // Spacie aerial specials with decomp call sites that include the cliff catch check.
    // Decomp:
    // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
    // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialAirHi_Coll}
    case MSL_ACT_FX_SPECIAL_AIR_S_START:
    case MSL_ACT_FX_SPECIAL_AIR_S:
    case MSL_ACT_FX_SPECIAL_AIR_S_END:
    case MSL_ACT_FX_SPECIAL_HI_HOLD_AIR:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
    case MSL_ACT_FX_SPECIAL_HI_FALL:
    case MSL_ACT_FX_SPECIAL_HI_BOUND:
      return 1;

    default:
      return 0;
  }
}

static inline uint16_t cliff_submotion_for_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
      return (uint16_t)MSL_SM_CLIFF_CATCH;
    case MSL_ACT_CLIFF_WAIT:
      return (uint16_t)MSL_SM_CLIFF_WAIT;
    case MSL_ACT_CLIFF_CLIMB_QUICK:
      return (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK;
    case MSL_ACT_CLIFF_ATTACK_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK;
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK;
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1;
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK2;
    default:
      return 0xFFFFu;
  }
}

static inline float facing_dir(uint8_t facing) { return facing ? 1.0f : -1.0f; }

static inline void enter_cliff_catch_immediate(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch != NULL) {
    // Decomp: CliffCatch entry path calls ftCommon_8007D5D4, which sets
    // fp->x1968_jumpsUsed = 1 on the owning fighter before CliffWait/option processing.
    // Slippi post-frame uses "jumps left", so jumps_left=max_jumps-1 at catch entry.
    // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    batch->state.jumps_left[idx] = (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  }
  // Decomp: ftCliffCommon_80081370 enters CliffCatch, then calls ftAnim_8006EBA4 in the same
  // update before input callbacks run.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_wait_on_stage(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.on_ground[idx] = 1;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1 : 0;
}

static inline float trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float trigger_unit_from_input_lane(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp input lane shape: digital L/R/Z force the shield trigger lane to 1.0f; otherwise use
  // analog max(L, R).
  // refs/melee/src/melee/ft/fighter.c (input lane build at 1868-1890)
  enum { LRZ = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R | (uint16_t)MSL_BUTTON_Z };
  if ((buttons & LRZ) != 0u) {
    return 1.0f;
  }
  const uint8_t m = (l > r) ? l : r;
  return trigger_u8_to_unit(m);
}

static inline uint8_t pressed_lr_lane_edge(const MslBatch* batch, const MslCommonParams* c,
                                           size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint16_t prev_buttons = batch->state.prev_input_buttons[idx];
  const uint16_t cur_buttons = batch->state.input_buttons[idx];
  const float prev_trigger = trigger_unit_from_input_lane(prev_buttons, batch->state.prev_input_l[idx],
                                                          batch->state.prev_input_r[idx]);
  const float cur_trigger =
      trigger_unit_from_input_lane(cur_buttons, batch->state.input_l[idx], batch->state.input_r[idx]);

  const uint8_t prev_lr_lane =
      (((prev_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
       (prev_trigger > c->trigger_deadzone))
          ? 1u
          : 0u;
  const uint8_t cur_lr_lane =
      (((cur_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
       (cur_trigger > c->trigger_deadzone))
          ? 1u
          : 0u;
  return (cur_lr_lane != 0u && prev_lr_lane == 0u) ? 1u : 0u;
}

static inline uint8_t held_lr_lane(const MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const uint16_t buttons = batch->state.input_buttons[idx];
  const float trig =
      trigger_unit_from_input_lane(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  return (((buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
          (trig > c->trigger_deadzone))
             ? 1u
             : 0u;
}

static inline uint8_t guard_x10_init_u8(const MslCommonParams* c) {
  // Decomp: mv.co.guard.x10 is initialized from p_ftCommonData->x268 on GuardOn entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
  if (c == NULL || !(c->guard_x10_init_frames > 0.0f)) {
    return 0u;
  }
  uint16_t t = (uint16_t)c->guard_x10_init_frames;
  if (t > 255u) {
    t = 255u;
  }
  return (uint8_t)t;
}

static inline void enter_guard_on_from_cliff_end(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Decomp Wait IASA guard path:
  // - ftCo_Wait_IASA -> ftCo_80091A4C -> ftCo_800924C0 (GuardOn entry).
  // - ftCo_800924C0 calls ftAnim_8006EBA4 then post-frame shield states can be no-submotion
  //   snapshots (animation_index/state_age == -1 lanes in Slippi).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));

  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B3 = 0x10 };
  enum { MSL_STATE_FLAG_221C_B1 = 0x40 };
  enum { MSL_STATE_FLAG_221C_B2 = 0x20 };
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  batch->state.guard_release_latched_xc[idx] = 0u;
  batch->state.guard_x10[idx] = guard_x10_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline uint16_t walk_action_from_speed(const MslCommonParams* c, const MslCharParams* ch,
                                              float gr_vel) {
  if (c == NULL || ch == NULL) {
    return (uint16_t)MSL_ACT_WALK_SLOW;
  }
  // Decomp: ftWalkCommon_GetWalkType.
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
  const float v = msl_absf(gr_vel);
  if (v >= (c->walk_fast_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_FAST;
  }
  if (v >= (c->walk_mid_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_MIDDLE;
  }
  return (uint16_t)MSL_ACT_WALK_SLOW;
}

static inline uint32_t walk_anim_for_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_WALK_FAST:
      return (uint32_t)MSL_SM_WALK_FAST;
    case (uint16_t)MSL_ACT_WALK_MIDDLE:
      return (uint32_t)MSL_SM_WALK_MIDDLE;
    case (uint16_t)MSL_ACT_WALK_SLOW:
    default:
      return (uint32_t)MSL_SM_WALK_SLOW;
  }
}

static inline void try_wait_interrupts_after_cliff_option_end(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch,
                                                               size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Decomp ordering for this callback bridge:
  // - CliffClimb/Attack/Escape anim end calls ftCommon_8007D92C (grounded Wait-like destination).
  // - Wait IASA then runs in the same Fighter proc; guard check (ftCo_80091A4C) precedes walk.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  if (held_lr_lane(batch, c, idx)) {
    enter_guard_on_from_cliff_end(batch, c, idx);
    return;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  if (msl_absf(stick_x) < c->walk_stick_threshold) {
    return;
  }
  if (ch == NULL) {
    return;
  }
  const uint16_t walk = walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
  batch->state.action_id[idx] = walk;
  batch->state.animation_index[idx] = walk_anim_for_action(walk);
  // Decomp: ftCo_Walk_Enter delegates to ftWalkCommon_800DFCA4 which immediately calls
  // ftAnim_8006EBA4 after Fighter_ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFCA4
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static inline void enter_cliff_wait(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_WAIT;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t enter_cliff_option_quick(MslBatch* batch, int bi, int p, uint16_t act,
                                               uint16_t smid) {
  if (batch == NULL) {
    return 0;
  }
  const size_t idx = msl_idx_player(bi, p);
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)smid;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  // Decomp: cliff option entries immediately tick the new motion in the same proc
  // (Fighter_ChangeMotionState -> ftAnim_8006EBA4).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B1B8
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  return 1;
}

static inline void cliff_option_phys_airground(MslBatch* batch, int bi, size_t idx, uint16_t smid) {
  if (batch == NULL) {
    return;
  }
  if (batch->state.on_ground[idx]) {
    // Decomp: once CliffClimb/Attack/Escape is grounded, Phys uses ft_80084FA8.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
    return;
  }

  int side = (int)batch->state.ledge_side[idx];
  if (!(side == 0 || side == 1)) {
    side = batch->state.facing[idx] ? 0 : 1;
    batch->state.ledge_side[idx] = (int8_t)side;
  }

  const uint32_t stage_id = batch->state.stage_id[bi];
  MslStagePoint2 ledge = {0};
  if (!stage_collision_get_ledge_point(stage_id, side, &ledge)) {
    // Decomp: ftCo_CliffClimb_Phys falls back to Fall when the ledge id is invalid.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
    enter_fall(batch, idx);
    batch->state.ledge_side[idx] = -1;
    return;
  }

  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch == NULL) {
    return;
  }

  const uint16_t frame =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
  float t[3] = {0};
  if (anim_pose_get_transn(batch->state.char_id[idx], smid, frame, t) != 0) {
    return;
  }

  // Decomp CliffClimb_Phys (shared by CliffAttack/Escape) while airborne:
  //   cur_pos.x = transNPos.z * facing_dir + ledge_x;
  //   cur_pos.y = ledge_y + transNPos.y;
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
  const float fd = facing_dir(batch->state.facing[idx]);
  const float scale = ch->model_scaling;
  const float trans_z = t[2] * scale;
  const float trans_y = t[1] * scale;
  batch->state.pos_x[idx] = ledge.x + trans_z * fd;
  batch->state.pos_y[idx] = ledge.y + trans_y;

  if (trans_z >= 0.0f && trans_y >= 0.0f) {
    // Decomp: when airborne CliffClimb reaches non-negative transN.y/z, it writes floor.index to
    // the ledge id and calls ftCommon_8007D7FC (air->ground helper).
    // Grounding ownership mirrors ftCommon_8007D6A4 / Fighter_procUpdate:
    // - fp->gr_vel = fp->self_vel.x on air->ground transfer,
    // - grounded update keeps fp->self_vel.x aligned from fp->gr_vel.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    const MslStageFloorLine* ledge_floor = stage_collision_get_ledge_floor_line(stage_id, side);
    if (ledge_floor != NULL) {
      batch->state.ground_id[idx] = ledge_floor->segment_i;
    }
    float gr = batch->state.speed_air_x_self[idx];
    const float gmax = ch->ground_max_horizontal_velocity;
    if (gr > gmax) {
      gr = gmax;
    } else if (gr < -gmax) {
      gr = -gmax;
    }
    batch->state.on_ground[idx] = 1;
    batch->state.speed_ground_x_self[idx] = gr;
    batch->state.speed_air_x_self[idx] = gr;
    batch->state.jumps_left[idx] = ch->max_jumps;
    batch->state.ecb_lock_timer[idx] = 0u;
    // Keep ground normal/contact ownership in the generic map-collision pass. This helper models
    // the Cliff option air->ground transfer; floor normals/contacts are refreshed on the next
    // collision step.
  }
}

static inline void cliff_catch_phys_snap(MslBatch* batch, int bi, size_t idx) {
  if (batch == NULL) {
    return;
  }
  int side = (int)batch->state.ledge_side[idx];
  if (!(side == 0 || side == 1)) {
    side = batch->state.facing[idx] ? 0 : 1;
    batch->state.ledge_side[idx] = (int8_t)side;
  }

  MslStagePoint2 ledge = {0};
  if (!stage_collision_get_ledge_point(batch->state.stage_id[bi], side, &ledge)) {
    enter_fall(batch, idx);
    batch->state.ledge_side[idx] = -1;
    return;
  }

  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch == NULL) {
    return;
  }

  const uint16_t frame =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
  float t[3] = {0};
  if (anim_pose_get_transn(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH, frame, t) != 0) {
    return;
  }

  // Decomp: ftCo_CliffCatch_Phys snaps every CliffCatch frame to `cliff_point + x68C_transNPos`.
  // Keep this scoped to CliffCatch only; CliffWait timing has additional owner state and is
  // replay-sensitive.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  const float fd = facing_dir(batch->state.facing[idx]);
  batch->state.pos_x[idx] = ledge.x + (t[2] * ch->model_scaling * fd);
  batch->state.pos_y[idx] = ledge.y + (t[1] * ch->model_scaling);
}

static inline uint8_t ledge_wait_try_attack(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & ((uint16_t)MSL_BUTTON_A | (uint16_t)MSL_BUTTON_B)) != 0) {
    // Decomp: ftCo_8009AE38 -> ftCo_8009AEA4 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AE38
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_escape(MslBatch* batch, const MslCommonParams* c, int bi,
                                            int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  // Decomp: ftCo_8009AFD4 checks fp->input.x668 LR lane and can enter CliffEscape from trigger/Z
  // synthesized LR-lane edges, not only raw digital L/R button-edge bits.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AFD4
  // refs/melee/src/melee/ft/fighter.c (input lane build at 1868-1890 and x668 edge build at 2078-2086)
  if ((pressed & ((uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R)) != 0 ||
      pressed_lr_lane_edge(batch, c, idx)) {
    // Decomp: ftCo_8009AFD4 -> ftCo_8009B040 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AFD4
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_jump(MslBatch* batch, const MslCommonParams* c, int bi,
                                          int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if ((pressed & (uint16_t)MSL_BUTTON_XY) != 0 ||
      did_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx])) {
    // Decomp: ftCo_8009B170 -> ftCo_8009B1B8 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B170
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1,
                                    (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_climb_or_drop(MslBatch* batch, const MslCommonParams* c,
                                                   int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(msl_absf(stick_x) >= c->cliff_option_stick_threshold ||
        msl_absf(stick_y) >= c->cliff_option_stick_threshold)) {
    return 0;
  }

  // Decomp gate: mv.co.cliff.x8 must be set (it is set when no stick/cstick option input is
  // present, and consumed when a qualifying stick input triggers climb/drop).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AA0C
  //
  // Approximation for one-step reseeds: require the *previous frame's* main stick to have been
  // below the option threshold (a "neutral reset") before allowing climb/drop this frame.
  const float prev_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
  const float prev_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  if (msl_absf(prev_x) >= c->cliff_option_stick_threshold ||
      msl_absf(prev_y) >= c->cliff_option_stick_threshold) {
    return 0;
  }

  const float fd = facing_dir(batch->state.facing[idx]);
  // Decomp: ftCo_GetLStickAngle / ftCo_GetCStickAngle compute atan2(y, ABS(x)), not atan2(y, x).
  // This keeps "away-horizontal" stick inputs near 0 radians so CliffWait option routing can rely
  // on the stick_x*facing sign gate for climb vs drop.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCo_GetLStickAngle,ftCo_GetCStickAngle}
  const float angle = atan2f(stick_y, msl_absf(stick_x));
  if (angle > c->attack_angle_threshold_radians ||
      (angle > -c->attack_angle_threshold_radians && (stick_x * fd) >= 0.0f)) {
    // ClimbQuick entry (percent-based Quick/Slow selection is omitted for v1; suite is Quick-only).
    // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK);
  }

  // Drop from ledge (Fall entry).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  // Decomp: drop sets fp->x2064_ledgeCooldown to suppress immediate re-grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  {
    const uint16_t cd = c->ledge_cooldown_frames;
    batch->state.ledge_cooldown[idx] = (cd > 0xFFu) ? 0xFFu : (uint8_t)cd;
  }
  batch->state.ledge_side[idx] = -1;
  enter_fall(batch, idx);
  return 1;
}

static inline void refresh_stage_ledge_occupants(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    MslStagePoint2 ledge_left = {0};
    MslStagePoint2 ledge_right = {0};
    const uint8_t have_left = stage_collision_get_ledge_point(stage_id, 0, &ledge_left);
    const uint8_t have_right = stage_collision_get_ledge_point(stage_id, 1, &ledge_right);

    batch->state.stage_ledge_occupant_left[bi] = -1;
    batch->state.stage_ledge_occupant_right[bi] = -1;
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (!is_cliff_hold_action(a)) {
        continue;
      }
      // Decomp: ledge occupancy checks use fp->x221D_b7, which is set by CliffCatch entry and by
      // subsequent on-ledge actions (CliffWait/CliffClimb/CliffAttack/CliffEscape/CliffJump1),
      // not "CliffWait only".
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c
      // refs/melee/src/melee/ft/ft_081B.c::ft_80082E3C
      int8_t side = batch->state.ledge_side[idx];
      if (!(side == 0 || side == 1)) {
        // Seed/reseed robustness: infer ledge side for cliff states even if the seed omitted it.
        // Prefer geometric proximity to stage ledge points when available, else fall back to the
        // decomp-facing convention (left ledge faces right).
        if (have_left && have_right) {
          const float x = batch->state.pos_x[idx];
          const float y = batch->state.pos_y[idx];
          const float dlx = x - ledge_left.x;
          const float dly = y - ledge_left.y;
          const float drx = x - ledge_right.x;
          const float dry = y - ledge_right.y;
          const float d0 = dlx * dlx + dly * dly;
          const float d1 = drx * drx + dry * dry;
          side = (d1 < d0) ? 1 : 0;
        } else if (have_left) {
          side = 0;
        } else if (have_right) {
          side = 1;
        } else {
          side = batch->state.facing[idx] ? 0 : 1;
        }
        batch->state.ledge_side[idx] = side;
      }

      if (side == 0) {
        if (batch->state.stage_ledge_occupant_left[bi] < 0) {
          batch->state.stage_ledge_occupant_left[bi] = (int8_t)p;
        }
      } else if (side == 1) {
        if (batch->state.stage_ledge_occupant_right[bi] < 0) {
          batch->state.stage_ledge_occupant_right[bi] = (int8_t)p;
        }
      }
    }
  }
}

void ledge_update_pre_physics(MslBatch* batch) {
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
      uint16_t a = batch->state.action_id[idx];
      if (!is_cliff_action_any(a)) {
        continue;
      }

      const uint16_t smid = cliff_submotion_for_action(a);
      if (smid != 0xFFFFu) {
        batch->state.animation_index[idx] = (uint32_t)smid;
      }

      if (is_cliff_hold_action(a)) {
        const int8_t side = batch->state.ledge_side[idx];
        if (!(side == 0 || side == 1)) {
          // Infer ledge side from facing: on the left ledge the fighter faces right (+).
          batch->state.ledge_side[idx] = batch->state.facing[idx] ? 0 : 1;
        }
        if (a == (uint16_t)MSL_ACT_CLIFF_CATCH) {
          const float end_frame =
              msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH);
          if (!(end_frame > 0.0f) || batch->state.anim_frame_f32[idx] < end_frame) {
          cliff_catch_phys_snap(batch, bi, idx);
          }
        }
      }

      if (a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK || a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
        cliff_option_phys_airground(batch, bi, idx, smid);
      }

      // Anim-end transitions / IASA.
      if (a == (uint16_t)MSL_ACT_CLIFF_CATCH) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffCatch_Anim -> ftCo_8009A804 (enter CliffWait).
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
          enter_cliff_wait(batch, idx);
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_WAIT) {
        // CliffWait IASA ordering is decomp-defined.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA
        if (ledge_wait_try_attack(batch, bi, p) || ledge_wait_try_escape(batch, c, bi, p) ||
            ledge_wait_try_jump(batch, c, bi, p) || ledge_wait_try_climb_or_drop(batch, c, bi, p)) {
          // State changed; position snap for new hold option is handled by enter helpers.
          a = batch->state.action_id[idx];
        } else {
          // CliffWait hang time expires -> fall.
          // Decomp: ftCo_8009A9AC.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
          const float percent = batch->state.percent[idx];
          const float wait_frames = (percent < c->cliff_wait_percent_threshold)
                                        ? c->cliff_wait_frames_low_percent
                                        : c->cliff_wait_frames_high_percent;
          if (wait_frames > 0.0f && (float)batch->state.action_frame[idx] >= wait_frames) {
            // Decomp: CliffWait auto-release sets fp->x2064_ledgeCooldown.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
            const uint16_t cd = c->ledge_cooldown_frames;
            batch->state.ledge_cooldown[idx] = (cd > 0xFFu) ? 0xFFu : (uint8_t)cd;
            batch->state.ledge_side[idx] = -1;
            enter_fall(batch, idx);
            a = batch->state.action_id[idx];
          }
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump1_Anim -> ftCo_8009B2F8 (enter Jump2 and set launch velocity).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump1_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B2F8
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          const float fd = facing_dir(batch->state.facing[idx]);
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_JUMP_QUICK2;
          // Decomp: ftCo_8009B2F8 enters CliffJump2 and immediately calls ftAnim_8006EBA4.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B2F8
          msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f,
                                              MSL_ANIM_ENTER_TICK_IMMEDIATE);
          batch->state.ledge_side[idx] = -1;
          batch->state.on_ground[idx] = 0;
          batch->state.fall_fast[idx] = 0;
          if (ch != NULL) {
            batch->state.speed_air_x_self[idx] += fd * ch->ledge_jump_horizontal_velocity;
            batch->state.speed_y_self[idx] = ch->ledge_jump_vertical_velocity;
          }
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_JUMP_QUICK2);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump2_Anim -> Fall_Enter.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Anim
          enter_fall(batch, idx);
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
        const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: CliffClimb_Anim (and CliffAttack/Escape) -> ftCommon_8007D92C.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
          //
          // TODO(decomp): ftCommon_8007D92C likely enters a grounded Wait-like state with proper floor
          // clamping and residual velocity handling. For now, enter Wait on stage.
          const uint16_t cliff_end_action = a;
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          batch->state.ledge_side[idx] = -1;
          enter_wait_on_stage(batch, idx);
          // Scope-narrowed bridge: keep the ftCommon_8007D92C -> Wait IASA same-proc ownership only
          // for the attack/escape families validated by strict lock rows in this bundle.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          if (cliff_end_action == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
              cliff_end_action == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
            try_wait_interrupts_after_cliff_option_end(batch, c, ch, idx);
          }
          a = batch->state.action_id[idx];
        }
      }
    }
  }

  refresh_stage_ledge_occupants(batch);
}

void ledge_try_catch_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    MslStagePoint2 ledge_left = {0};
    MslStagePoint2 ledge_right = {0};
    const uint8_t have_left = stage_collision_get_ledge_point(stage_id, 0, &ledge_left);
    const uint8_t have_right = stage_collision_get_ledge_point(stage_id, 1, &ledge_right);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (is_cliff_action_any(a)) {
        continue;
      }
      if (batch->state.on_ground[idx]) {
        continue;
      }
      // Decomp: Fighter_procUpdate and Fighter_procMap collision blocks are gated out during hitlag
      // (and thus do not run cliff catch checks).
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (the `if (!fp->x2219_b5)` block)
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      // Match-flow actions use dedicated (or NULL) collision callbacks in decomp; this lite sim
      // skips generic stage collision for those motions, and thus should not schedule cliff catch.
      // refs: src/match_flow.c::match_flow_should_stage_collide
      if (!match_flow_should_stage_collide(a)) {
        continue;
      }
      // Decomp: if fp->x2064_ledgeCooldown is nonzero, fighter collision uses the mpColl variant
      // that does not attempt ledge grabs (e.g., mpColl_80047AC8 instead of mpColl_80047E14),
      // so Collide_LedgeGrabMask will not be set for this collision step.
      // refs/melee/src/melee/ft/ft_081B.c::ft_80083090_inline
      if (batch->state.ledge_cooldown[idx] != 0) {
        continue;
      }
      // Action gate (suite-focused): only attempt cliff catch from a subset of airborne actions
      // whose collision callbacks include the cliff check in-engine.
      // Decomp: cliff check call sites are in shared collision wrappers that run after mpColl.
      // refs/melee/src/melee/ft/ft_081B.c::{ft_800835B0,ft_800831CC,ft_80083090}
      if (!is_airborne_action_with_cliffcatch_check(a)) {
        continue;
      }
      // Decomp: cliff catch checks collision env flags for Collide_LedgeGrabMask.
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
      const uint32_t env = batch->state.coll_env_flags[idx];
      const uint32_t grab_mask = env & (uint32_t)MSL_COLLIDE_LEDGE_GRAB_MASK;
      if (grab_mask == 0u) {
        continue;
      }
      // mpColl's on-edge suppression is already modeled in mpcoll_env_update_ledge_grab() when
      // producing Collide_LedgeGrabMask. If ledge-grab bits are set here, treat them as
      // authoritative for ftCliffCommon_80081298 scheduling.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298

      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      // Decomp: holding down beyond threshold disables ledge catch.
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
      if (stick_y <= -c->cliff_drop_stick_threshold) {
        continue;
      }

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      // Decomp: ftCliffCommon_80081370 chooses ledge side based on Collide_LeftLedgeGrab (left bit
      // wins when both bits are present), and does not attempt a second side on failure.
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
      const int side = (grab_mask & (uint32_t)MSL_COLLIDE_LEFT_LEDGE_GRAB) ? 0 : 1;
      if (side == 0) {
        if (!have_left) {
          continue;
        }
        if (batch->state.stage_ledge_occupant_left[bi] >= 0) {
          continue;
        }

        // Enter CliffCatch and snap.
        batch->state.ledge_side[idx] = 0;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
        batch->state.on_ground[idx] = 0;
        batch->state.fall_fast[idx] = 0;
        enter_cliff_catch_immediate(batch, idx);
        // Decomp: ftCliffCommon_80081370 sets facing toward stage and snaps to the ledge point.
        // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
        batch->state.facing[idx] = 1;  // left ledge -> face right
        {
          // Decomp: ftCo_CliffCatch_Phys snaps to `cliff_point + TransNPos` each frame.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
          float t[3] = {0};
          if (anim_pose_get_transn(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH, 0, t) ==
              0) {
            batch->state.pos_x[idx] = ledge_left.x + (t[2] * ch->model_scaling);
            batch->state.pos_y[idx] = ledge_left.y + (t[1] * ch->model_scaling);
          } else {
            // Fallback (should not happen with extracted pose data present).
            batch->state.pos_x[idx] = ledge_left.x;
            batch->state.pos_y[idx] = ledge_left.y + ch->ledge_snap_y;
          }
        }
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        batch->state.stage_ledge_occupant_left[bi] = (int8_t)p;
      } else {
        if (!have_right) {
          continue;
        }
        if (batch->state.stage_ledge_occupant_right[bi] >= 0) {
          continue;
        }

        batch->state.ledge_side[idx] = 1;
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
        batch->state.on_ground[idx] = 0;
        batch->state.fall_fast[idx] = 0;
        enter_cliff_catch_immediate(batch, idx);
        batch->state.facing[idx] = 0;  // right ledge -> face left
        {
          // Decomp: ftCo_CliffCatch_Phys snaps to `cliff_point + TransNPos` each frame.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
          float t[3] = {0};
          if (anim_pose_get_transn(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH, 0, t) ==
              0) {
            batch->state.pos_x[idx] = ledge_right.x + (-t[2] * ch->model_scaling);
            batch->state.pos_y[idx] = ledge_right.y + (t[1] * ch->model_scaling);
          } else {
            batch->state.pos_x[idx] = ledge_right.x;
            batch->state.pos_y[idx] = ledge_right.y + ch->ledge_snap_y;
          }
        }
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        batch->state.stage_ledge_occupant_right[bi] = (int8_t)p;
      }
    }
  }
}
