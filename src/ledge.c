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
#include "guard_lifecycle.h"
#include "input_axis.h"
#include "locomotion.h"
#include "match_flow.h"
#include "stage_collision.h"

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_SLOW:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_SLOW:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_SLOW1:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_cliff_occupancy_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_SLOW:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_SLOW:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_SLOW1:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_cliff_action_any(uint16_t a) {
  return (is_cliff_hold_action(a) || a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW2 ||
          a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2)
             ? 1
             : 0;
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
    // MissFoot_Coll uses ft_80082F28, which runs ftCliffCommon_80081298 after the
    // ground/ledge and wall-jump checks.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082F28
    case MSL_ACT_MISS_FOOT:
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
    // Pass / platform drop collision uses the MissFoot-style common-air wrapper, which runs
    // ftCliffCommon_80081298 after airborne stage collision. Holding down still blocks the catch
    // inside ftCliffCommon_80081298, but releasing down before the ledge window must allow it.
    // Decomp:
    // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Coll
    // - refs/melee/src/melee/ft/ft_081B.c::ft_80082F28
    case MSL_ACT_PASS:
      return 1;

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
    // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll,
    //   ftFx_SpecialHiBound_Coll}
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
    case MSL_ACT_CLIFF_CLIMB_SLOW:
      return (uint16_t)MSL_SM_CLIFF_CLIMB_SLOW;
    case MSL_ACT_CLIFF_CLIMB_QUICK:
      return (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK;
    case MSL_ACT_CLIFF_ATTACK_SLOW:
      return (uint16_t)MSL_SM_CLIFF_ATTACK_SLOW;
    case MSL_ACT_CLIFF_ATTACK_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK;
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
      return (uint16_t)MSL_SM_CLIFF_ESCAPE_SLOW;
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK;
    case MSL_ACT_CLIFF_JUMP_SLOW1:
      return (uint16_t)MSL_SM_CLIFF_JUMP_SLOW1;
    case MSL_ACT_CLIFF_JUMP_SLOW2:
      return (uint16_t)MSL_SM_CLIFF_JUMP_SLOW2;
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1;
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK2;
    default:
      return 0xFFFFu;
  }
}

static inline float facing_dir(uint8_t facing) { return facing ? 1.0f : -1.0f; }

static inline void cliff_hold_phys_snap(MslBatch* batch, int bi, size_t idx, uint16_t smid);

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

static inline void enter_fall_keep_fastfall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  batch->state.on_ground[idx] = 0;
  // Decomp: CliffJump2_Anim exits through ftCo_Fall_Enter, whose ChangeMotionState call uses
  // Ft_MF_KeepFastFall before the destination Fall Phys callback can run this frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside Fighter_ChangeMotionState)
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = keep_fastfall;
}

static inline void enter_wait_on_stage(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Cliff option anim-end owner:
  // - ftCo_CliffClimb_Anim (shared by CliffClimb/Attack/Escape) enters the grounded Wait-like
  //   destination through ftCommon_8007D92C.
  // - The same Fighter proc can then run Wait_IASA and enter GuardOn before Phys; source GuardOn
  //   Phys still sees the residual cliff-option gr_vel and applies ordinary ground friction before
  //   position integration.
  // Preserve the velocity lanes here so the destination Phys callback owns the friction/movement
  // step instead of dropping a frame of residual cliff motion at the state handoff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{
  //   ftCo_CliffClimb_Anim,ftCo_CliffClimb_Phys}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_GuardOn_Phys}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80084F3C}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.on_ground[idx] = 1;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void update_cliff_ledge_floor_owner(MslBatch* batch, int bi, size_t idx) {
  if (batch == NULL || batch->state.cliff_ledge_floor_segment_id == NULL) {
    return;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  int side = (int)batch->state.ledge_side[idx];
  if (!(side == 0 || side == 1)) {
    // Cliff source state always has a ledge side. Teacher-forced reseeds of Cliff actions can enter
    // before local side state is initialized, so reconstruct from the same source-facing convention:
    // left ledge faces right, right ledge faces left. Do not infer from position or future landing.
    // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    side = batch->state.facing[idx] ? 0 : 1;
    batch->state.ledge_side[idx] = (int8_t)side;
  }
  const MslStageFloorLine* ledge_floor = stage_collision_get_ledge_floor_line(stage_id, side);
  if (ledge_floor != NULL) {
    // Source cliff floor owner:
    // `mv.co.cliff.ledge_id` is selected by CliffCatch and remains the CollData floor owner through
    // immediate cliff exits such as CliffWait drop/release -> Fall -> JumpAerial -> EscapeAir.
    // Store the generated stage floor line, not a position-fit or outcome-fit line.
    // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    batch->state.cliff_ledge_floor_segment_id[idx] = ledge_floor->segment_i;
    if (batch->state.cliff_ledge_floor_segment_seeded != NULL) {
      batch->state.cliff_ledge_floor_segment_seeded[idx] = 0u;
    }
  }
}

static inline void mark_stale_floor_skip_for_ledge_flow(MslBatch* batch, int bi, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id != 0xFFFFu && stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    // Source has ledge identity in the cliff state and does not let a pre-catch floor index seed
    // subsequent Fall_Coll after dropping/releasing from ledge. Keep the replay-visible floor index
    // intact and carry the rejection in a hidden mpColl skip.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    if (batch->state.ledge_drop_floor_skip_segment_id != NULL) {
      batch->state.ledge_drop_floor_skip_segment_id[idx] = ground_id;
    }
  }
  update_cliff_ledge_floor_owner(batch, bi, idx);
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
  const float prev_trigger = trigger_unit_from_input_lane(
      prev_buttons, batch->state.prev_input_l[idx], batch->state.prev_input_r[idx]);
  const float cur_trigger = trigger_unit_from_input_lane(cur_buttons, batch->state.input_l[idx],
                                                         batch->state.input_r[idx]);

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
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(
      uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  batch->state.guard_release_latched_xc[idx] = 0u;
  batch->state.guard_x10[idx] = msl_guard_x10_raw_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void enter_guard_reflect_from_cliff_end(MslBatch* batch, const MslCommonParams* c,
                                                      size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Decomp Wait IASA powershield path:
  // - CliffClimb/Attack/Escape anim end calls ftCommon_8007D92C -> ft_8008A2BC -> Wait.
  // - The same Fighter proc can then run Wait_IASA; ftCo_80091A4C checks fresh L/R press plus
  //   x672 before the held-shield GuardOn branch and enters GuardReflect through ftCo_800939B4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_REFLECT;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
  batch->state.guard_reflect_timer_x14[idx] = msl_guard_reflect_timer_x14_init(c);
  batch->state.guard_reflect_timer_x18[idx] = msl_guard_reflect_timer_x18_init(c);
  batch->state.guard_reflect_origin_guardon[idx] = 0u;
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.guard_release_latched_xc[idx] = 0u;
  batch->state.guard_x10[idx] = msl_guard_x10_visible_guardon_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |=
      (uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
}

static inline void try_wait_interrupts_after_cliff_option_end(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Decomp ordering for this callback bridge:
  // - CliffClimb/Attack/Escape anim end calls ftCommon_8007D92C (grounded Wait-like destination).
  // - Wait IASA then runs in the same Fighter proc; after grounded attacks, guard
  //   (ftCo_80091A4C) precedes Jump/Dash/Squat/Turn/Walk.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  const float fd = facing_dir(batch->state.facing[idx]);

  if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(
          batch, c, idx, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y, fd)) {
    return;
  }
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons_pressed & (uint16_t)LR) != 0u &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_cliff_end(batch, c, idx);
    return;
  }
  if (held_lr_lane(batch, c, idx)) {
    enter_guard_on_from_cliff_end(batch, c, idx);
    return;
  }
  if (ch == NULL) {
    return;
  }
  (void)locomotion_wait_iasa_locomotion_subset_try_enter(
      batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y, fd,
      (uint16_t)MSL_ACT_WAIT);
}

static inline void enter_cliff_wait(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_WAIT;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  // Decomp: ftCo_8009A804 initializes mv.co.cliff.x8=0. The later CliffClimb/drop IASA owner
  // requires a neutral stick/c-stick callback to latch x8 before a stick option can be consumed.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  batch->state.cliff_option_stick_latch_x8[idx] = 0u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t enter_cliff_option(MslBatch* batch, int bi, int p, uint16_t act,
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
  if (act == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1 || act == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1) {
    // Decomp: ftCo_8009B1B8 enters CliffJump1, immediately ticks the new motion, then runs
    // ftCo_CliffCatch_Phys in the same proc so Jump1 stays attached to the ledge during the
    // first frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B1B8
    cliff_hold_phys_snap(batch, bi, idx, smid);
  }
  return 1;
}

static inline uint8_t enter_cliff_option_percent_split(MslBatch* batch, const MslCommonParams* c,
                                                       int bi, int p, uint16_t slow_act,
                                                       uint16_t slow_smid, uint16_t quick_act,
                                                       uint16_t quick_smid) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const size_t idx = msl_idx_player(bi, p);
  // Decomp: CliffClimb/Attack/Escape/Jump option entries select Quick below p_ftCommonData->x488
  // and Slow at or above that percent threshold.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B1B8
  const uint8_t quick = (batch->state.percent[idx] < c->cliff_wait_percent_threshold) ? 1u : 0u;
  return enter_cliff_option(batch, bi, p, quick ? quick_act : slow_act,
                            quick ? quick_smid : slow_smid);
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
    mark_stale_floor_skip_for_ledge_flow(batch, bi, idx);
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

static inline void cliff_hold_phys_snap(MslBatch* batch, int bi, size_t idx, uint16_t smid) {
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
  if (anim_pose_get_transn(batch->state.char_id[idx], smid, frame, t) != 0) {
    return;
  }

  // Decomp: ftCo_CliffCatch_Phys snaps CliffCatch/CliffWait to `cliff_point + x68C_transNPos`,
  // and ftCo_CliffWait_Phys is a direct call-through to ftCo_CliffCatch_Phys.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_Phys
  const float fd = facing_dir(batch->state.facing[idx]);
  batch->state.pos_x[idx] = ledge.x + (t[2] * ch->model_scaling * fd);
  batch->state.pos_y[idx] = ledge.y + (t[1] * ch->model_scaling);
}

static inline uint8_t ledge_wait_try_attack(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & ((uint16_t)MSL_BUTTON_A | (uint16_t)MSL_BUTTON_B | (uint16_t)MSL_BUTTON_Z)) != 0) {
    // Decomp: ftCo_8009AE38 -> ftCo_8009AEA4 chooses Quick vs Slow based on percent threshold.
    // Fighter input synthesis maps Z into held HSD_PAD_A before x668 edge construction, so Z-edge
    // cliff rows route to CliffAttack before the LR-lane CliffEscape check.
    // refs/melee/src/melee/ft/fighter.c (held_inputs Z maps to HSD_PAD_LR | HSD_PAD_A)
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AE38
    return enter_cliff_option_percent_split(
        batch, msl_common_params(), bi, p, (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW,
        (uint16_t)MSL_SM_CLIFF_ATTACK_SLOW, (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK,
        (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_escape(MslBatch* batch, const MslCommonParams* c, int bi,
                                            int p) {
  const size_t idx = msl_idx_player(bi, p);
  // Decomp: ftCo_8009AFD4 checks fp->input.x668 LR lane and can enter CliffEscape from trigger/Z
  // synthesized LR-lane edges, not only raw digital L/R button-edge bits.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AFD4
  // refs/melee/src/melee/ft/fighter.c (input lane build at 1868-1890 and x668 edge build at 2078-2086)
  if (pressed_lr_lane_edge(batch, c, idx)) {
    // Decomp: ftCo_8009AFD4 -> ftCo_8009B040 chooses Quick vs Slow based on percent threshold.
    // The callback reads input.x668's synthesized LR lane edge; do not also admit raw L/R button
    // edges when the analog trigger lane was already active on the previous frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AFD4
    // refs/melee/src/melee/ft/fighter.c (input x668 edge build)
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
    return enter_cliff_option_percent_split(
        batch, c, bi, p, (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW, (uint16_t)MSL_SM_CLIFF_ESCAPE_SLOW,
        (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK, (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK);
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
    return enter_cliff_option_percent_split(
        batch, c, bi, p, (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1, (uint16_t)MSL_SM_CLIFF_JUMP_SLOW1,
        (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1, (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1);
  }
  return 0;
}

static inline uint8_t ledge_wait_has_stick_option_input(MslBatch* batch, const MslCommonParams* c,
                                                        size_t idx) {
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_x = stick_i8_to_unit(batch->state.input_c_x[idx]);
  const float cstick_y = stick_i8_to_unit(batch->state.input_c_y[idx]);
  return (msl_absf(stick_x) >= c->cliff_option_stick_threshold ||
          msl_absf(stick_y) >= c->cliff_option_stick_threshold ||
          msl_absf(cstick_x) >= c->cliff_option_stick_threshold ||
          msl_absf(cstick_y) >= c->cliff_option_stick_threshold)
             ? 1u
             : 0u;
}

static inline void ledge_wait_latch_stick_option_ready_if_neutral(MslBatch* batch,
                                                                  const MslCommonParams* c,
                                                                  size_t idx) {
  if (!ledge_wait_has_stick_option_input(batch, c, idx)) {
    // Decomp: ftCo_8009AA0C sets mv.co.cliff.x8 when no stick/c-stick option is present.
    // This can happen on the same proc where CliffCatch_Anim enters CliffWait; x8 still blocks
    // immediate climb/drop that frame, but the neutral callback prepares the next frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AA0C
    batch->state.cliff_option_stick_latch_x8[idx] = 1u;
  }
}

static inline uint8_t ledge_wait_try_climb_or_drop(MslBatch* batch, const MslCommonParams* c,
                                                   int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_x = stick_i8_to_unit(batch->state.input_c_x[idx]);
  const float cstick_y = stick_i8_to_unit(batch->state.input_c_y[idx]);
  const uint8_t main_option = (uint8_t)(msl_absf(stick_x) >= c->cliff_option_stick_threshold ||
                                        msl_absf(stick_y) >= c->cliff_option_stick_threshold);
  const uint8_t cstick_option = (uint8_t)(msl_absf(cstick_x) >= c->cliff_option_stick_threshold ||
                                          msl_absf(cstick_y) >= c->cliff_option_stick_threshold);
  if (!main_option && !cstick_option) {
    ledge_wait_latch_stick_option_ready_if_neutral(batch, c, idx);
    return 0;
  }

  // Decomp gate: mv.co.cliff.x8 must have been latched by a previous neutral CliffWait IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  if (batch->state.cliff_option_stick_latch_x8[idx] == 0u) {
    return 0;
  }

  const float option_x = main_option ? stick_x : cstick_x;
  const float option_y = main_option ? stick_y : cstick_y;
  const float fd = facing_dir(batch->state.facing[idx]);
  // Decomp: ftCo_GetLStickAngle / ftCo_GetCStickAngle compute atan2(y, ABS(x)), not atan2(y, x).
  // This keeps "away-horizontal" stick inputs near 0 radians so CliffWait option routing can rely
  // on the stick_x*facing sign gate for climb vs drop.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCo_GetLStickAngle,ftCo_GetCStickAngle}
  const float angle = atan2f(option_y, msl_absf(option_x));
  if (angle > c->attack_angle_threshold_radians ||
      (angle > -c->attack_angle_threshold_radians && (option_x * fd) >= 0.0f)) {
    if (!main_option) {
      // Decomp: c-stick reaches ftCo_8009AAFC with arg1=false, so it may release/drop from ledge
      // but cannot start CliffClimb even when the angle falls in the climb branch.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AA0C
      return 0;
    }
    // ClimbQuick entry (percent-based Quick/Slow selection is omitted for v1; suite is Quick-only).
    // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
    return enter_cliff_option_percent_split(
        batch, c, bi, p, (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW, (uint16_t)MSL_SM_CLIFF_CLIMB_SLOW,
        (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK, (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK);
  }

  // Drop from ledge (Fall entry).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  // Decomp: drop sets fp->x2064_ledgeCooldown to suppress immediate re-grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  {
    const uint16_t cd = c->ledge_cooldown_frames;
    batch->state.ledge_cooldown[idx] = (cd > 0xFFu) ? 0xFFu : (uint8_t)cd;
  }
  mark_stale_floor_skip_for_ledge_flow(batch, bi, idx);
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
      if (!is_cliff_occupancy_action(a)) {
        continue;
      }
      // Decomp: ledge occupancy checks use fp->x221D_b7, which is set by CliffCatch entry and by
      // subsequent on-ledge actions (CliffWait/CliffClimb/CliffAttack/CliffEscape/CliffJump1),
      // not "CliffWait only". Slow and quick ledge options both occupy the ledge; Jump2 no longer
      // uses the attach snap and is not an occupancy action.
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
        update_cliff_ledge_floor_owner(batch, bi, idx);
        if (a == (uint16_t)MSL_ACT_CLIFF_CATCH) {
          const float end_frame =
              msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH);
          if (!(end_frame > 0.0f) || batch->state.anim_frame_f32[idx] < end_frame) {
            cliff_hold_phys_snap(batch, bi, idx, (uint16_t)MSL_SM_CLIFF_CATCH);
          }
        } else if (a == (uint16_t)MSL_ACT_CLIFF_WAIT) {
          // Decomp: CliffWait_Phys is a direct call-through to ftCo_CliffCatch_Phys and therefore
          // keeps the fighter snapped to ledge_point + TransNPos on every CliffWait frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_Phys
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
          cliff_hold_phys_snap(batch, bi, idx, (uint16_t)MSL_SM_CLIFF_WAIT);
        } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1 ||
                   a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1) {
          const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
          if (!(end_frame > 0.0f) || batch->state.anim_frame_f32[idx] < end_frame) {
            // Decomp: CliffJump1_Phys is also a direct call-through to ftCo_CliffCatch_Phys while
            // the fighter remains in Jump1. Do not snap on the terminal Jump1 row because Anim
            // hands off to Jump2 first, and Jump2 no longer uses the attach snap.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::{
            //   ftCo_CliffJump1_Phys,ftCo_CliffJump1_Anim,ftCo_8009B2F8}
            // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
            cliff_hold_phys_snap(batch, bi, idx, smid);
          }
        }
      }

      if (a == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW || a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW || a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW || a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
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
          // Decomp ordering:
          // - ftCo_CliffCatch_Anim enters CliffWait through ftCo_8009A804 during the same fighter
          //   proc.
          // - Fighter_procUpdate then runs the new state's Phys callback, and ftCo_CliffWait_Phys
          //   is a direct call-through to ftCo_CliffCatch_Phys.
          // Keep the handoff scoped to the anim-end transition row only.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::{
          //   ftCo_8009A804,ftCo_CliffWait_Phys}
          cliff_hold_phys_snap(batch, bi, idx, (uint16_t)MSL_SM_CLIFF_WAIT);
          // Fighter_procUpdate runs Anim before IASA; when CliffCatch_Anim enters CliffWait, the
          // new CliffWait IASA options can be consumed in that same proc.
          // Keep climb/drop out of this same-proc slice because ftCo_8009A804 initializes
          // mv.co.cliff.x8=0, and ftCo_8009AAFC requires x8 before climb/drop can consume stick.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
          if (ledge_wait_try_attack(batch, bi, p) || ledge_wait_try_escape(batch, c, bi, p) ||
              ledge_wait_try_jump(batch, c, bi, p)) {
            a = batch->state.action_id[idx];
          } else {
            ledge_wait_latch_stick_option_ready_if_neutral(batch, c, idx);
          }
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
            mark_stale_floor_skip_for_ledge_flow(batch, bi, idx);
            batch->state.ledge_side[idx] = -1;
            enter_fall(batch, idx);
            a = batch->state.action_id[idx];
          }
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1 ||
                 a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1) {
        const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump1_Anim -> ftCo_8009B2F8 (enter Jump2 and set launch velocity).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump1_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B2F8
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          const float fd = facing_dir(batch->state.facing[idx]);
          if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW2;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_JUMP_SLOW2;
          } else {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_JUMP_QUICK2;
          }
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
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW2 ||
                 a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2) {
        const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump2_Anim -> Fall_Enter.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Anim
          batch->state.prev_action_id[idx] = a;
          enter_fall_keep_fastfall(batch, idx);
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW ||
                 a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW ||
                 a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW ||
                 a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
        const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: CliffClimb_Anim (and CliffAttack/Escape) -> ftCommon_8007D92C.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
          //
          // Decomp: CliffClimb_Anim / CliffAttack_Anim / CliffEscape_Anim end through
          // ftCommon_8007D92C. Supported RL 1.0 ledge options land on stage into Wait and can run
          // Wait IASA in the same proc; the detailed floor-clamp portion is collision-owned and is
          // covered by the ledge/platform collision locks.
          const uint16_t cliff_end_action = a;
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          batch->state.ledge_side[idx] = -1;
          enter_wait_on_stage(batch, idx);
          // Source policy: keep the ftCommon_8007D92C -> Wait IASA same-proc ownership for the
          // CliffClimb/Attack/Escape option families validated by strict lock rows.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
          if (cliff_end_action == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW ||
              cliff_end_action == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
              cliff_end_action == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW ||
              cliff_end_action == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
              cliff_end_action == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW ||
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
      // producing Collide_LedgeGrabMask. The decomp gate checks Collide_LeftEdge/RightEdge before
      // writing the ledge-grab bits; it does not reject a later CliffCatch solely because the
      // aggregate Collide_Edge bit is also present. If ledge-grab bits are set here, treat them as
      // authoritative for ftCliffCommon_80081298 scheduling, including SpecialHiFall.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298

      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      // Decomp: holding down beyond threshold disables ledge catch.
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
      if (stick_y <= -c->cliff_drop_stick_threshold) {
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

        batch->state.ledge_side[idx] = 0;
        update_cliff_ledge_floor_owner(batch, bi, idx);
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
        batch->state.on_ground[idx] = 0;
        batch->state.fall_fast[idx] = 0;
        batch->state.facing[idx] = 1;  // left ledge -> face right
        // Decomp: ftCliffCommon_80081370 sets facing toward stage, enters CliffCatch, immediately
        // ticks the new motion via ftAnim_8006EBA4, then ftCo_CliffCatch_Phys snaps using the live
        // post-entry TransN frame rather than the raw frame-0 pose.
        // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
        // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
        enter_cliff_catch_immediate(batch, idx);
        cliff_hold_phys_snap(batch, bi, idx, (uint16_t)MSL_SM_CLIFF_CATCH);
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
        update_cliff_ledge_floor_owner(batch, bi, idx);
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
        batch->state.on_ground[idx] = 0;
        batch->state.fall_fast[idx] = 0;
        batch->state.facing[idx] = 0;  // right ledge -> face left
        // Decomp: same CliffCatch entry/tick/phys ordering as the left ledge branch above.
        // refs/melee/src/melee/ft/ftcliffcommon.c::{ftCliffCommon_80081370,ftCo_CliffCatch_Phys}
        enter_cliff_catch_immediate(batch, idx);
        cliff_hold_phys_snap(batch, bi, idx, (uint16_t)MSL_SM_CLIFF_CATCH);
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
