#include "grab_flow.h"

#include <assert.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "combat.h"
#include "dash_iasa.h"
#include "falcon_specials.h"
#include "ftcommon_ecb.h"
#include "fighter_script.h"
#include "grab_attachment.h"
#include "ids.h"
#include "guard_lifecycle.h"
#include "hitboxes.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "mpcoll_source_air.h"
#include "mpcoll_source_ground.h"
#include "state_flags.h"
#include "trigger_input.h"

static inline void capturewait_anim_callback_apply(MslBatch* batch, const MslCommonParams* c,
                                                   int bi, int victim_p, size_t vidx,
                                                   uint8_t* out_mash_active);

static inline void catch_connect_apply_post_shield_release_recharge(MslBatch* batch,
                                                                    const MslCommonParams* c,
                                                                    size_t vidx,
                                                                    uint16_t pre_connect_action) {
  if (batch == NULL || c == NULL || msl_action_is_live_shield_family(pre_connect_action) == 0u) {
    return;
  }
  // Catch-connect runs after the frame's Guard*_Anim drain and installs CapturePulled* before the
  // late Fighter_ProcessHit pass. Once fn_800DAADC leaves the live ShieldDesc family, the shared
  // `!fp->x221A_b7` shield-health recharge gate is true for the same post-frame.
  // Use the shared GuardOn/Guard/GuardSetOff/GuardReflect family predicate: GuardSetOff is still
  // shield-owned in source (`GuardDamage` motion with live shield state) until capture entry exits
  // that family, so it must not drift from other ShieldDesc owners.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_ProcessHit_8006D1EC}
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_Guard_Anim,ftCo_GuardSetOff_Anim,ftCo_800925A4}
  msl_guard_lifecycle_apply_shield_recharge(batch, c, vidx);
}

static inline void clear_outgoing_hitboxes_after_catch_connect(MslBatch* batch, int bi, int p) {
  // Fighter_ChangeMotionState clears the complete x914 HitCapsule packet, including prior-sweep
  // geometry and group/enable state. Reuse the canonical transition helper so Catch cannot leave
  // a disabled-but-refreshable capsule behind.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AFF8
  hitboxes_clear_player_active(batch, bi, p);
}

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

static inline void enter_common_wait_or_fall_from_grab_cut_end(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: CatchCut_Anim and CaptureCut_Anim both end through ftCommon_8007D92C, which routes
  // grounded fighters through ft_8008A2BC -> Wait and airborne fighters through ftCo_Fall_Enter.
  // CaptureJump_Anim ends directly through ftCo_Fall_Enter and is handled by the caller.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchCut_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_CaptureCut_Anim
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
  if (batch->state.on_ground[idx] != 0u) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
    batch->state.ground_id[idx] = 0xFFFFu;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_fall_from_capture_jump_end(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: CaptureJump_Anim increments its timer and, on animation end, calls ftCo_Fall_Enter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureJump_Anim
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  batch->state.on_ground[idx] = 0u;
  batch->state.ground_id[idx] = 0xFFFFu;
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
  // CaptureWait (hi/lw) based on the current capture variant. No Phys callback is run here;
  // priority-4 later dispatches the newly installed CaptureWait Phys owner normally.
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
  // Decomp: CapturePulled* -> CaptureWait* transition is routed through fn_800DB6C8, which calls
  // fn_800DB790/fn_800DBAE4 (Fighter_ChangeMotionState) with no local ftAnim_8006EBA4.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DB6C8,fn_800DB790,fn_800DBAE4}
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  batch->state.capture_wait_anim_rate_timer[vidx] = 0.0f;
  // Decomp call shape for this lane is "ChangeMotionState only" (no local ftAnim_8006EBA4):
  // - fn_800DB6C8 dispatches to fn_800DB790/fn_800DBAE4,
  // - each helper calls Fighter_ChangeMotionState(..., anim_start=0.0f, anim_speed=1.0f),
  // - neither helper performs an immediate local tick after entry.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DB6C8,fn_800DB790,fn_800DBAE4}
}

static inline void run_capture_pulled_entry_collision_callback(MslBatch* batch, size_t vidx) {
  if (batch == NULL || batch->state.hitlag_started_frame[vidx] != 0u) {
    return;
  }
  const int bi = (int)(vidx / (size_t)MSL_MAX_PLAYERS);
  const int victim_p = (int)(vidx % (size_t)MSL_MAX_PLAYERS);
  motion_state_install_live_callbacks(batch, vidx);
  if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    (void)mpcoll_source_air_run_installed_callback(batch, bi, victim_p);
  } else if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW) {
    (void)mpcoll_source_ground_run_installed_callback(batch, bi, victim_p);
  }

  // fn_800DAADC invokes the newly installed CapturePulled{Hi,Lw}_Coll callback immediately after
  // entry. The generated air/ground kernels own the corresponding 477E0/4B108 CollData update and
  // paired continuation; no predecessor or replay-floor inference belongs here.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledHi_Coll,ftCo_CapturePulledLw_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_8008403C}
}

static inline uint8_t capturewait_grab_mash_active(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  uint16_t buttons = batch->state.input_buttons_pressed[idx];
  if ((buttons & (uint16_t)MSL_BUTTON_Z) != 0u) {
    buttons = (uint16_t)(buttons | (uint16_t)MSL_BUTTON_A);
  }
  {
    const uint16_t cur_buttons = batch->state.input_buttons[idx];
    const uint16_t prev_buttons = batch->state.prev_input_buttons[idx];
    const float cur_trig = msl_trigger_unit_from_input(cur_buttons, batch->state.input_l[idx],
                                                       batch->state.input_r[idx]);
    const float prev_trig = msl_trigger_unit_from_input(
        prev_buttons, batch->state.prev_input_l[idx], batch->state.prev_input_r[idx]);
    const uint8_t lr_now =
        (((cur_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
         cur_trig > c->trigger_deadzone)
            ? 1u
            : 0u;
    const uint8_t lr_prev =
        (((prev_buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u) ||
         prev_trig > c->trigger_deadzone)
            ? 1u
            : 0u;
    if (lr_now != 0u && lr_prev == 0u) {
      buttons = (uint16_t)(buttons | (uint16_t)MSL_BUTTON_L);
    }
  }
  uint8_t result = ((buttons & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_X |
                                          MSL_BUTTON_Y | MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
                       ? 1u
                       : 0u;
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const int8_t prev_x = batch->state.grab_mash_stick_x_sign[idx];
  const int8_t prev_y = batch->state.grab_mash_stick_y_sign[idx];
  int8_t next_x = prev_x;
  int8_t next_y = prev_y;
  // ftCommon_GrabMash consumes fp->input.x668 for AB/XY/LR and updates x1A50/x1A51 when
  // lstick.{x,y} crosses +/-x308. Mash succeeds on an x668 edge or any sign-latch change.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10_Inner1
  // data/common/ft_common_data.json: grab_mash_stick_threshold
  if (stick_x < -c->grab_mash_stick_threshold) {
    next_x = -1;
  } else if (stick_x > c->grab_mash_stick_threshold) {
    next_x = 1;
  }
  if (stick_y < -c->grab_mash_stick_threshold) {
    next_y = -1;
  } else if (stick_y > c->grab_mash_stick_threshold) {
    next_y = 1;
  }
  if (prev_x != next_x || prev_y != next_y) {
    result = 1u;
  }
  batch->state.grab_mash_stick_x_sign[idx] = next_x;
  batch->state.grab_mash_stick_y_sign[idx] = next_y;
  return result;
}

static inline float capture_grab_timer_init(const MslBatch* batch, const MslCommonParams* c,
                                            int victim_p, size_t vidx) {
  if (batch == NULL || c == NULL) {
    return 0.0f;
  }
  const float slot = (float)(victim_p + 1);
  const float handicap = (float)batch->state.handicap[vidx];
  return c->capture_grab_timer_base +
         c->capture_grab_timer_handicap_mul * (c->capture_grab_timer_handicap_base - handicap) +
         c->capture_grab_timer_slot_mul * (c->capture_grab_timer_slot_base - slot) +
         batch->state.percent[vidx] * c->capture_grab_timer_percent_mul;
}

static inline void capture_hidden_state_clear(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.capture_grab_timer[idx] = 0.0f;
  batch->state.capture_wait_counter[idx] = 0.0f;
  batch->state.capture_wait_anim_rate_timer[idx] = 0.0f;
  batch->state.capture_wait_jump_latch[idx] = 0u;
}

static inline void capture_family_tick_hidden_state(MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx, uint8_t* out_mash_active) {
  if (batch == NULL || c == NULL) {
    return;
  }
  batch->state.capture_wait_counter[idx] += 1.0f;
  batch->state.capture_grab_timer[idx] -= c->capture_wait_grab_timer_decrement;
  const uint8_t mash_active = capturewait_grab_mash_active(batch, c, idx);
  if (mash_active != 0u) {
    batch->state.capture_grab_timer[idx] -= c->capture_wait_grab_mash_damage;
  }
  if (out_mash_active != NULL) {
    *out_mash_active = mash_active;
  }
}

static inline void replace_attached_victim(MslBatch* batch, int bi, int owner_p, int victim_p) {
  const size_t oidx = msl_idx_player(bi, owner_p);
  const size_t vidx = msl_idx_player(bi, victim_p);
  // Decomp has one owner->victim_gobj pointer and one reciprocal victim->owner pointer. Replace the
  // old pair directly; searching every fighter for duplicate links invents a second ownership
  // model and makes doubles behavior depend on repair order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DA8E4,fn_800DAADC}
  const uint8_t previous_victim = batch->state.attached_victim_port[oidx];
  if (previous_victim < batch->config.num_players && previous_victim != (uint8_t)victim_p &&
      previous_victim != (uint8_t)owner_p) {
    const size_t previous_vidx = msl_idx_player(bi, (int)previous_victim);
    if (batch->state.grab_owner_port[previous_vidx] == (uint8_t)owner_p) {
      batch->state.grab_owner_port[previous_vidx] = 0xFFu;
    }
  }
  batch->state.attached_victim_port[oidx] = (uint8_t)victim_p;
  batch->state.grab_owner_port[vidx] = (uint8_t)owner_p;
}

static inline void clear_grab_linkage(MslBatch* batch, size_t oidx, size_t vidx) {
  if (batch == NULL) {
    return;
  }
  batch->state.attached_victim_port[oidx] = 0xFFu;
  batch->state.grab_owner_port[vidx] = 0xFFu;
}

static inline void enter_catch_cut_from_capture_breakout(MslBatch* batch, const MslCommonParams* c,
                                                         size_t oidx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_CUT;
  batch->state.animation_index[oidx] = (uint32_t)MSL_SM_CATCH_CUT;
  if (batch->state.on_ground[oidx] != 0u) {
    batch->state.speed_ground_x_self[oidx] =
        -(float)batch->state.facing_dir1[oidx] * c->capture_cut_escape_speed;
  } else {
    batch->state.speed_air_x_self[oidx] =
        -(float)batch->state.facing_dir1[oidx] * c->capture_jump_escape_speed_x;
    batch->state.speed_y_self[oidx] = c->capture_jump_escape_speed_y;
  }
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
}

static inline void enter_capture_cut_from_breakout(MslBatch* batch, const MslCommonParams* c,
                                                   size_t vidx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_CUT;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_CUT;
  if (batch->state.on_ground[vidx] != 0u) {
    batch->state.speed_ground_x_self[vidx] =
        -(float)batch->state.facing_dir1[vidx] * c->capture_cut_escape_speed;
  } else {
    batch->state.speed_air_x_self[vidx] =
        -(float)batch->state.facing_dir1[vidx] * c->capture_cut_escape_speed;
  }
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  capture_hidden_state_clear(batch, vidx);
}

static inline void enter_capture_jump_from_breakout(MslBatch* batch, const MslCommonParams* c,
                                                    size_t vidx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint16_t carried_ground_id = batch->state.ground_id[vidx];
  batch->state.on_ground[vidx] = 0u;
  // CaptureWait breakout enters CaptureJump through fn_800DC070. That helper calls
  // ftCommon_8007D5D4, which flips ground_or_air to Air and clears gr_vel, but does not clear
  // CollData.floor. Preserve the carried floor id for replay/API publication until the next
  // CaptureJump_Coll owner updates collision state.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DC070
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.ground_id[vidx] = carried_ground_id;
  batch->state.speed_ground_x_self[vidx] = 0.0f;
  batch->state.speed_air_x_self[vidx] =
      -(float)batch->state.facing_dir1[vidx] * c->capture_jump_escape_speed_x;
  batch->state.speed_y_self[vidx] = c->capture_jump_escape_speed_y;
  {
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[vidx]);
    if (ch != NULL && ch->max_jumps > 0u) {
      batch->state.jumps_left[vidx] = (uint8_t)(ch->max_jumps - 1u);
    }
  }
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_JUMP;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_JUMP;
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  capture_hidden_state_clear(batch, vidx);
}

static inline void capturewait_resolve_anim_breakout_owner(MslBatch* batch,
                                                           const MslCommonParams* c, size_t oidx,
                                                           size_t vidx) {
  // Fighter_8006A360 invokes CaptureWait's Anim callback before Fighter_procUpdate installs the
  // current controller sample. input_apply_pre_input_snapshot() has already published that same
  // source-visible prior sample in input_main_y. Resolve the complete paired transition here,
  // before any fighter's current-input IASA callback, just as ftCo_CaptureWaitHi_Anim does.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CaptureWaitHi_Anim,ftCo_800DA698,fn_800DC044,fn_800DC070}
  enter_catch_cut_from_capture_breakout(batch, c, oidx);
  clear_grab_linkage(batch, oidx, vidx);
  const float main_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[vidx]), c->lstick_deadzone_y);
  if (batch->state.capture_wait_jump_latch[vidx] != 0u || main_y >= c->tap_jump_threshold) {
    enter_capture_jump_from_breakout(batch, c, vidx);
  } else {
    enter_capture_cut_from_breakout(batch, c, vidx);
  }
}

static inline void capturewait_anim_callback_apply(MslBatch* batch, const MslCommonParams* c,
                                                   int bi, int victim_p, size_t vidx,
                                                   uint8_t* out_mash_active) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint16_t a = batch->state.action_id[vidx];
  if (a != (uint16_t)MSL_ACT_CAPTURE_WAIT_HI && a != (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
    return;
  }

  uint8_t mash_active = 0u;
  capture_family_tick_hidden_state(batch, c, vidx, &mash_active);
  if (out_mash_active != NULL) {
    *out_mash_active = mash_active;
  }
  if (batch->state.capture_grab_timer[vidx] <= 0.0f) {
    const uint8_t owner = batch->state.grab_owner_port[vidx];
    if (owner < batch->config.num_players && owner != (uint8_t)victim_p) {
      const size_t oidx = msl_idx_player(bi, (int)owner);
      if (batch->state.attached_victim_port[oidx] == (uint8_t)victim_p) {
        capturewait_resolve_anim_breakout_owner(batch, c, oidx, vidx);
      }
    }
    return;
  }

  // This callback runs in the engine's pre-input phase, AFTER the step's anim advance
  // consumed the current frame_speed_mul -- matching the source, where ftAnim_SetAnimRate in
  // the post-advance Anim callback first affects the NEXT frame's advance (v12 Dolphin probe,
  // AGNG rec 3208 / QGD rollout 5366-5375 windows: the arming frame advances +1, af then
  // steps +2).
  const float zero = 0.0f;
  float timer = batch->state.capture_wait_anim_rate_timer[vidx];
  if (timer != zero) {
    timer -= 1.0f;
    if (timer <= zero && mash_active == 0u) {
      batch->state.frame_speed_mul_fp_q16_16[vidx] = msl_q16_16_from_f32(1.0f);
      timer = zero;
    }
  }
  if (timer <= zero && mash_active != 0u) {
    timer = c->capture_wait_anim_rate_hold_frames;
    batch->state.frame_speed_mul_fp_q16_16[vidx] = msl_q16_16_from_f32(c->capture_wait_anim_rate);
  }
  batch->state.capture_wait_anim_rate_timer[vidx] = timer;
}

uint8_t grab_flow_on_attached_body_damage(MslBatch* batch, size_t vidx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t va = batch->state.action_id[vidx];
  if (va == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI) {
    // Decomp: a grabbed-victim hit enters CaptureDamageHi (0xE1) via ftCo_800DC284 from
    // EITHER CaptureWaitHi or CaptureDamageHi (ftCo_Damage.c::inlineF0 dispatches on msid
    // {0xE0, 0xE1} - a re-pummel RESTARTS the damage anim).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DC284
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineF0
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_DAMAGE_HI;
  } else if (va == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW || va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
    // Decomp: same shape for the Lw family via ftCo_800DC3A4 (inlineF0 msid {0xE3, 0xE4}).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DC3A4
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineF0
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_DAMAGE_LW;
  } else {
    return 0u;
  }
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  batch->state.capture_wait_anim_rate_timer[vidx] = 0.0f;
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
  batch->state.capture_wait_anim_rate_timer[vidx] = 0.0f;
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
  // Raw Slippi mapping:
  // - Fighter_Spaghetti maps Z into held_inputs LR plus x668 A before Catch_CheckInput reads
  //   those internal lanes, so the compact raw Z edge is source-equivalent to the A-edge half.
  // refs/melee/src/melee/ft/fighter.c:1868-1890
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
  // Raw Slippi mapping: held Z supplies the source held_inputs LR half for Catch_CheckInput.
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  if ((buttons & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0) {
    return 1u;
  }

  const float trig =
      msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  return (trig >= c->trigger_deadzone) ? 1u : 0u;
}

static inline uint8_t guardreflect_no_submotion_x18_blocks_guard_catch(const MslBatch* batch,
                                                                       size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // No-submotion GuardReflect catch-input boundary:
  // Slippi can serialize GuardReflect as an action-frame-negative, no-submotion snapshot while
  // `ftCo_80093BC0` still has a live x14 ReflectDesc timer to tick before IASA. The GuardReflect
  // IASA table is real, but this frozen snapshot shape does not expose a fresh Catch_CheckInput
  // A+LR consume until x14 has expired. Keep the suppression scoped to Catch_CheckInput only;
  // collision-time grabbable capsules and x18-only GuardReflect owners remain separately modeled.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
  return (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[idx] <= -2 && batch->state.animation_index[idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14_seed[idx] > 0u &&
          batch->state.guard_reflect_timer_x18[idx] > 0u)
             ? 1u
             : 0u;
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
  // - `speed_{x,y}_attack` map the source fp->x8c_kb_vel lane, not fp->x74_anim_vel. Keep
  //   residual knockback/attack velocity live across Catch/CatchDash entry; Fighter_procUpdate
  //   still integrates x8c_kb_vel after the catch motion-state helper runs.
  // refs/melee/src/melee/ft/types.h::{x74_anim_vel,x8c_kb_vel}
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = submotion;
  batch->state.catch_kind_x1a68[idx] = 1u;
  batch->state.catch_target_mask_x1a6a[idx] = 0u;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

void grab_flow_refresh_catch_contract_for_batch_index(MslBatch* batch, int batch_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(batch_index, p);
    const uint8_t char_id = batch->state.char_id[idx];
    const uint16_t action_id = batch->state.action_id[idx];
    const uint32_t classes = msl_motion_state_class3_bits(char_id, action_id);

    uint16_t kind = 0u;
    if ((classes & (uint32_t)MSL_MS_CLASS3_CATCH_KIND_1) != 0u) {
      kind = 1u;
    } else if ((classes & (uint32_t)MSL_MS_CLASS3_CATCH_KIND_2) != 0u) {
      kind = 2u;
    }

    uint16_t target_mask = 0u;
    if ((classes & (uint32_t)MSL_MS_CLASS3_CATCH_TARGET_MASK_1) != 0u) {
      target_mask = 1u;
    } else if ((classes & (uint32_t)MSL_MS_CLASS3_CATCH_TARGET_MASK_511) != 0u) {
      target_mask = 0x1FFu;
    } else if ((classes & (uint32_t)MSL_MS_CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED) != 0u) {
      const uint8_t victim_p = batch->state.attached_victim_port[idx];
      if (victim_p != 0xFFu && victim_p < (uint8_t)num_players && victim_p != (uint8_t)p) {
        const size_t vidx = msl_idx_player(batch_index, (int)victim_p);
        if (batch->state.grab_owner_port[vidx] == (uint8_t)p) {
          target_mask = 0x1FFu;
        }
      }
    }

    batch->state.catch_kind_x1a68[idx] = kind;
    batch->state.catch_target_mask_x1a6a[idx] = target_mask;
  }
}

void grab_flow_refresh_catch_contract(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    grab_flow_refresh_catch_contract_for_batch_index(batch, bi);
  }
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
  if (guardreflect_no_submotion_x18_blocks_guard_catch(batch, idx)) {
    return 0u;
  }
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
  const uint16_t pre_action = batch->state.action_id[idx];

  // Decomp (ftCo_800D8A38) for the CatchDash-enter subset:
  // - Requires held_inputs & HSD_PAD_LR and pressed-edge A (input.x668 & HSD_PAD_A).
  // - On success calls ftCo_800D8C54(..., ftCo_MS_CatchDash).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8A38
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
  //
  // NOTE(char-domain): ftCo_800D8A38 also gates through fn_800D8E94 (blocks Link/YLink
  // with an active hookshot item) and fn_800D952C (blocks Samus with an active grapple)
  // before input checks. Pass-through for every currently supported character
  // (fox/falco/marth); model these blocks when a Link/YLink/Samus port lands.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800D8E94,fn_800D952C}
  if (!catch_input_a_pressed_edge(batch, idx)) {
    return 0u;
  }
  if (!catch_input_lr_held(batch, c, idx)) {
    return 0u;
  }

  enter_catch_motion_state(batch, idx, (uint16_t)MSL_ACT_CATCH_DASH, (uint32_t)MSL_SM_CATCH_DASH);
  if (pre_action == (uint16_t)MSL_ACT_DASH) {
    // Fighter_ChangeMotionState applies the same previous-root-motion ground-speed clamp on
    // Dash_IASA -> CatchDash as other Dash_IASA destinations before CatchDash_Phys runs its
    // steady catch-friction step. This matters for Falco's high Dash velocities: source clamps to
    // co_attrs.dash_run_terminal_velocity (1.5) and then CatchDash_Phys applies x64*gr_friction.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   ftCo_800D8A38,ftCo_800D8C54,ftCo_CatchDash_Phys}
    dash_iasa_apply_root_motion_exit_gr_vel_clamp(
        batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
  }
  return 1u;
}

void grab_flow_enter_catchdash_from_attackdash_pregate(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // AttackDash IASA pre-gate consumes into CatchDash via ftCo_800D8C54(msid=0xD6).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_800D8AE0,ftCo_800D8C54}
  enter_catch_motion_state(batch, idx, (uint16_t)MSL_ACT_CATCH_DASH, (uint32_t)MSL_SM_CATCH_DASH);
  // Like the Dash_IASA -> CatchDash path, ftCo_800D8C54's Fighter_ChangeMotionState clamps gr_vel to
  // co_attrs.dash_run_terminal_velocity because the previous root-motion AttackDash motion exits into
  // the non-root-motion CatchDash. Without it the full AttackDash ground speed carries into CatchDash
  // (CatchDash_Phys then only sheds x64*gr_friction per frame), so the fighter slides far past the
  // source and accumulates rollout position drift.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D8C54,ftCo_CatchDash_Phys}
  dash_iasa_apply_root_motion_exit_gr_vel_clamp(
      batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
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
  // x98 maps to the grounded A-tilt X threshold lane in ftCommonData.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD1E4
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF7F4
  const float throw_x_thresh = c->attack_s3_stick_threshold_x;
  const uint8_t lstick_x_edge = ((stick_x_prev < throw_x_thresh && stick_x >= throw_x_thresh) ||
                                 (stick_x_prev > -throw_x_thresh && stick_x <= -throw_x_thresh))
                                    ? 1u
                                    : 0u;
  if (lstick_x_edge) {
    return (stick_x * facing_dir > 0.0f) ? (uint16_t)MSL_ACT_THROW_F : (uint16_t)MSL_ACT_THROW_B;
  }

  // Use the same x98 threshold lane for C-stick X-edge checks as L-stick in this throw selector.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD1E4
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF7F4
  const uint8_t cstick_x_edge = ((cstick_x_prev < throw_x_thresh && cstick_x >= throw_x_thresh) ||
                                 (cstick_x_prev > -throw_x_thresh && cstick_x <= -throw_x_thresh))
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

  const uint8_t victim_u8 = batch->state.attached_victim_port[oidx];
  if (victim_u8 >= batch->config.num_players || victim_u8 == (uint8_t)owner_p) {
    return 0u;
  }
  const int victim_p = (int)victim_u8;
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (batch->state.stocks[vidx] == 0u || batch->state.grab_owner_port[vidx] != (uint8_t)owner_p ||
      !msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
    return 0u;
  }

  const uint16_t thrown_action = msl_throw_victim_action_from_owner(throw_action);
  if (thrown_action == 0xFFFFu) {
    return 0u;
  }
  const uint32_t owner_sm =
      (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[oidx], throw_action);
  const uint32_t victim_sm =
      (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[vidx], thrown_action);

  float throw_anim_speed = 1.0f;
  {
    const MslCommonParams* c = msl_common_params();
    const MslCharParams* owner_ch = msl_char_params_fast(batch->state.char_id[oidx]);
    const MslCharParams* victim_ch = msl_char_params_fast(batch->state.char_id[vidx]);
    // Decomp throw-entry anim-speed ownership:
    // - ftCo_800DD4B0 computes throw_index = msid - 219, then:
    //   if (!(weight_independent_throws_mask & (1 << throw_index))) {
    //     anim_speed = 1.0f / (victim->ft_data->x0->weight * p_ftCommonData->x37C);
    //   } else {
    //     anim_speed = 1.0f;
    //   }
    // - ftCo_800DD398 passes that anim_speed into both thrower and thrown-victim motion entries.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398}
    // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x180 weight_independent_throws_mask)
    const int throw_index = msl_throw_index_from_owner_action(throw_action);
    const uint8_t weight_independent =
        (throw_index >= 0 && owner_ch != NULL)
            ? ((owner_ch->weight_independent_throws_mask & (uint8_t)(1u << throw_index)) ? 1u : 0u)
            : 0u;
    if (!weight_independent && c != NULL && victim_ch != NULL && victim_ch->weight > 0.0f &&
        c->throw_anim_speed_weight_mul > 0.0f) {
      throw_anim_speed = 1.0f / (victim_ch->weight * c->throw_anim_speed_weight_mul);
      if (!(throw_anim_speed > 0.0f)) {
        throw_anim_speed = 1.0f;
      }
    }
  }

  const int32_t throw_anim_speed_fp = msl_q16_16_from_f32(throw_anim_speed);

  batch->state.action_id[oidx] = throw_action;
  batch->state.animation_index[oidx] = owner_sm;
  batch->state.throw_coll_x4[oidx] = 0u;
  batch->state.throw_coll_x8[oidx] = 0u;
  msl_anim_timebase_enter(batch, oidx, 0.0f, throw_anim_speed);
  batch->state.frame_speed_mul_fp_q16_16[oidx] = throw_anim_speed_fp;
  batch->state.throw_anim_rate_fp_q16_16[oidx] = throw_anim_speed_fp;
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

  // ftCo_800DD398 arms the thrower's temporary collision-status window after the immediate
  // destination animation tick. This is the ordinary ftColl_8007B7A4 owner: x1994 retains the
  // longest live duration and x198C is intangible only while the independent x1990 window is
  // also live, otherwise it is invulnerable. Keeping the real timers here makes both free-running
  // throws and later ProcessHit BODY checks consume the same source state; replay reseed only has
  // to reconstruct a throw already in progress.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B7A4
  {
    const MslCommonParams* c = msl_common_params();
    if (c != NULL) {
      if (c->colanim_throw_x1994_frames > batch->state.colanim_timer_x1994[oidx]) {
        batch->state.colanim_timer_x1994[oidx] = c->colanim_throw_x1994_frames;
      }
      batch->state.colanim_hit_status_x198c[oidx] =
          batch->state.colanim_timer_x1990[oidx] != 0u ? 2u : 1u;
    }
  }

  // Throw-entry state-machine ownership and attachment handoff:
  // - CatchWait->Throw* and CaptureWait/Pulled->Thrown* transitions are owned here.
  // - Positional attachment offsets are computed by grab_attachment_recompute_offsets_for_thrown_entry
  //   with ftCo_800DE508-shaped axis/scaling semantics.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  // ftCo_800DE3FC installs the XRotN constraint before Fighter_ChangeMotionState. Preserve the
  // outgoing Capture* local translation in x2174 before replacing the victim animation.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  grab_attachment_install_xrotn_constraint(batch, vidx);
  batch->state.action_id[vidx] = thrown_action;
  batch->state.animation_index[vidx] = victim_sm;
  // ftCo_800DE3FC enters every ordinary Thrown* motion through ftCo_800DB368, which reparents the
  // victim XRotN and sets x2226_b2. ftCo_800DDDE4 later uses this exact lifetime to unparent and
  // publish the release-local CollData packet. Keep runtime entry and replay reseed on the same
  // explicit constraint owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // Decomp: Thrown entry copies victim facing from thrower before installing/accessing the
  // per-frame thrown accessory callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  batch->state.facing[vidx] = batch->state.facing[oidx];
  msl_anim_timebase_enter(batch, vidx, 0.0f, throw_anim_speed);
  batch->state.frame_speed_mul_fp_q16_16[vidx] = throw_anim_speed_fp;
  batch->state.throw_anim_rate_fp_q16_16[vidx] = throw_anim_speed_fp;
  // Safety contract matches the thrower-side guard above: this extra tick mirrors
  // ftCo_800DE3FC's immediate ftAnim_8006EBA4 and must not be generalized to arbitrary entries.
  assert(batch->state.action_id[vidx] >= (uint16_t)MSL_ACT_THROWN_F &&
         batch->state.action_id[vidx] <= (uint16_t)MSL_ACT_THROWN_LW);
  // Decomp: thrown-victim entry helper also performs an immediate ftAnim_8006EBA4 after
  // Fighter_ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  msl_anim_timebase_tick_once(batch, vidx);
  // Every Thrown direction installs the same XRotN constraint and accessory1 callback. x1A70 is
  // initialized once by Fighter_Create from the fighter's TransN/XRotN basis; the pre-entry
  // Capture world position is not an alternate low-throw offset owner.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  grab_attachment_use_static_offsets_for_thrown_entry(batch, bi, victim_p, owner_p);
  return 1u;
}

// Falcon Dive connect (owner action 353/354 with a CATCH-element script hitbox contact):
// - attacker (ftCa_SpecialLw_800E5128): -> SpecialHiCatch(355) at frame 0 with NO immediate
//   anim tick (no ftAnim_8006EBA4 there), grab descriptor disarmed (ftCommon_8007E2FC) and
//   x1A6A=511 (ungrabbable while carrying; not modeled). GROUNDED victim: the ATTACKER is
//   constrained under the victim's TransN2 (ftCo_800DB368(victim, attacker)) with x221B_b7=1
//   and accessory4 (ftCa_SpecialLw_800E550C) snapping attacker.pos to victim.pos each frame.
//   AIRBORNE victim: x221B_b7=0.
// - victim (ftCo_8009CA0C): -> CaptureCaptain(275) at frame 0 plus an immediate anim tick
//   (ftAnim_8006EBA4), victim_gobj=attacker (bidirectional link), facing=-attacker.facing,
//   x1A6A=0x1FF (un-re-grabbable). AIRBORNE victim is constrained under the attacker's
//   TransN2 (ftCo_800DB368(attacker, victim)) and accessory1=ftCo_800DB464 drives its world
//   position from that anchor plus the static x1A70 offsets each frame.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialLw_800E5128,
//   ftCa_SpecialLw_800E550C}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DB368,ftCo_800DB464}
static void grab_flow_falcon_dive_catch_connect(MslBatch* batch, int bi, int owner_p,
                                                int victim_p) {
  const size_t oidx = msl_idx_player(bi, owner_p);
  const size_t vidx = msl_idx_player(bi, victim_p);
  const uint16_t owner_instance_id_pre_connect = batch->state.instance_id[oidx];
  const uint16_t victim_pre_connect_action = batch->state.action_id[vidx];
  const uint8_t victim_on_ground = batch->state.on_ground[vidx] ? 1u : 0u;

  // Falcon Dive installs the constraint against the pre-connect fighter JObj: Falcon when the
  // victim was grounded, otherwise the victim. Capture x2174 before either destination motion is
  // installed below.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialLw_800E5128
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
  if (victim_on_ground) {
    grab_attachment_install_xrotn_constraint(batch, oidx);
  } else {
    grab_attachment_install_xrotn_constraint(batch, vidx);
  }

  // Attacker -> SpecialHiCatch (msid 309). ChangeMotionState without Ft_MF_UpdateCmd clears
  // the cmd-var latches.
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH;
  batch->state.animation_index[oidx] =
      (uint32_t)falcon_special_submotion((uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH);
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
  batch->state.special_cmd0[oidx] = 0u;
  batch->state.special_cmd1[oidx] = 0u;
  batch->state.special_cmd2[oidx] = 0u;
  batch->state.falcon_specialhi_x221b_b7[oidx] = victim_on_ground;
  batch->state.catch_kind_x1a68[oidx] = 0u;
  batch->state.catch_target_mask_x1a6a[oidx] = 0x1FFu;
  batch->state.catch_target_mask_x1a6a[vidx] = 0x1FFu;
  {
    const size_t flags_i =
        oidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    if (victim_on_ground) {
      batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221B_B7;
    } else {
      batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
    }
  }
  // Both connect owners zero EVERY attacker velocity lane (self, anim, ground, kb, shield-kb)
  // through ftCommon_8007E2FC; SpecialHiCatch_Phys is empty, so the attacker hangs with zero
  // velocity until doCatchAnim's throw entry.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialLw_800E5128
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
  batch->state.speed_air_x_self[oidx] = 0.0f;
  batch->state.speed_y_self[oidx] = 0.0f;
  batch->state.speed_ground_x_self[oidx] = 0.0f;
  batch->state.speed_x_attack[oidx] = 0.0f;
  batch->state.speed_y_attack[oidx] = 0.0f;

  // Victim -> CaptureCaptain (non-damaging catch-connect ownership lane; velocity lanes stop
  // driving the constrained victim, same hygiene as the CapturePulled entry above).
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_CAPTAIN;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_CAPTAIN;
  batch->state.facing[vidx] = batch->state.facing[oidx] ? 0u : 1u;
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, vidx);
  batch->state.speed_air_x_self[vidx] = 0.0f;
  batch->state.speed_ground_x_self[vidx] = 0.0f;
  batch->state.speed_y_self[vidx] = 0.0f;
  batch->state.speed_x_attack[vidx] = 0.0f;
  batch->state.speed_y_attack[vidx] = 0.0f;
  // Fighter_ChangeMotionState clears the connecting owner's outgoing Catch capsule before the
  // later fighter collision pass. The victim transition likewise clears its prior primitives.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  clear_outgoing_hitboxes_after_catch_connect(batch, bi, owner_p);
  clear_outgoing_hitboxes_after_catch_connect(batch, bi, victim_p);
  batch->state.hitstun[vidx] = 0u;
  batch->state.instance_hit_by[vidx] = owner_instance_id_pre_connect;
  {
    const size_t flags_i =
        vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  }
  {
    // CaptureCaptain carries its own x221B_b7 connect-time grounded flag in addition to the
    // captor's identically named bit. It controls accessory1 installation; x2226_b2 remains the
    // separate joint-constraint authority.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
    const size_t flags_i =
        vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    if (victim_on_ground) {
      batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221B_B7;
    } else {
      batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
    }
  }
  catch_connect_apply_post_shield_release_recharge(batch, msl_common_params(), vidx,
                                                   victim_pre_connect_action);

  replace_attached_victim(batch, bi, owner_p, victim_p);

  if (!victim_on_ground) {
    // Airborne hang: ftCo_800DB464 places the victim from the reparented anchor plus the
    // static x1A70 offsets — the same substrate attached Thrown* uses. Steady per-frame
    // placement runs in the priority-8 CaptureCaptain accessory owner.
    grab_attachment_use_static_offsets_for_thrown_entry(batch, bi, victim_p, owner_p);
  }
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
  const uint16_t owner_instance_id_pre_connect = batch->state.instance_id[oidx];
  const uint16_t victim_pre_connect_action = batch->state.action_id[vidx];
  if (batch->state.stocks[oidx] == 0 || batch->state.stocks[vidx] == 0) {
    return;
  }

  const uint16_t owner_act = batch->state.action_id[oidx];
  const float owner_anim_start = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[oidx]);
  // Falcon Dive command grab: the owner armed grab kind 2 at Special(Air)Hi entry
  // (ftCommon_8007E2D0), and combat's catch-selection gate only admits falcon 353/354 here.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_Enter,
  //   ftCa_SpecialAirHi_Enter}
  if (batch->state.char_id[oidx] == (uint8_t)MSL_CHAR_ID_FALCON &&
      (owner_act == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
       owner_act == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI)) {
    grab_flow_falcon_dive_catch_connect(batch, bi, owner_p, victim_p);
    return;
  }
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
  batch->state.catch_kind_x1a68[oidx] = 0u;
  batch->state.catch_target_mask_x1a6a[oidx] = 0u;
  batch->state.catch_target_mask_x1a6a[vidx] = 0x1FFu;
  // Decomp: fn_800D9CE8 installs CatchPull/CatchDashPull with anim_start=fp->cur_anim_frame
  // (preserve current catch timeline instead of restarting from frame 0).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  // Decomp ownership: fn_800D9CE8 writes 0 to fp->gr_vel (offset 0xEC) before
  // Fighter_ChangeMotionState(CatchPull/CatchDashPull), so grounded catch-connect rows should not
  // retain pre-connect ground velocity in the post-frame motion state.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  msl_anim_timebase_enter(batch, oidx, owner_anim_start, 1.0f);
  batch->state.speed_ground_x_self[oidx] = 0.0f;

  // Catch connect victim entry selects CapturePulled variant from callback-target xE0.
  // Decomp: fn_800DAADC checks xE0 on the callback target gobj and chooses:
  // - xE0 == 0 -> 0xE2 (CapturePulledLw)
  // - else    -> 0xDF (CapturePulledHi)
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA8E4
  // Sim mapping: xE0 is the live victim on_ground lane at the catch callback. Previous actions and
  // validation seed history are not source inputs to this selection.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  if (batch->state.on_ground[vidx] != 0) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_LW;
  } else {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_HI;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_HI;
  }
  // CapturePulled entry facing ownership:
  // - fn_800DA8E4 negates the owner's facing_dir and writes it into the victim before
  //   Fighter_ChangeMotionState installs CapturePulledHi/Lw.
  // - This is distinct from Thrown* entry, which copies the thrower's facing unchanged.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA8E4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  batch->state.facing[vidx] = batch->state.facing[oidx] ? 0u : 1u;
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  // Decomp: capture-pulled entry helper immediately ticks anim once via ftAnim_8006EBA4.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAA10
  assert(batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI ||
         batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW);
  msl_anim_timebase_tick_once(batch, vidx);
  // CapturePulled entry ownership: victim translation is driven by fn_800DAD18 in Phys callbacks,
  // so pre-grab self/KB velocity lanes should not remain active on the entry frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CapturePulledHi_Phys,ftCo_CapturePulledLw_Phys,fn_800DAD18}
  batch->state.speed_air_x_self[vidx] = 0.0f;
  batch->state.speed_ground_x_self[vidx] = 0.0f;
  batch->state.speed_y_self[vidx] = 0.0f;
  batch->state.speed_x_attack[vidx] = 0.0f;
  batch->state.speed_y_attack[vidx] = 0.0f;
  // Proc-order catch ownership:
  // - Fighter_UnkProcessGrab_8006CA5C (prio 0xC) runs catch acquisition before the common fighter
  //   collision pass Fighter_8006CB94 (prio 0xD).
  // - After ftColl_80078754 / fn_800DAADC installs CapturePulled*, the grabbed victim's subsequent
  //   collision pass sees the new captured motion state, not the previous Attack* hitboxes.
  // Clear the simulator's pre-refreshed outgoing hitboxes for this victim so catch-connect frames do
  // not also apply stale BODY damage in the later combat body pass.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_8006CB94}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_80078754}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAADC
  clear_outgoing_hitboxes_after_catch_connect(batch, bi, owner_p);
  clear_outgoing_hitboxes_after_catch_connect(batch, bi, victim_p);
  // Decomp ownership: catch-connect callback fn_800DAADC installs CapturePulled* and calls
  // fn_800DAA10; this transition switches to non-Damage motion-state vars, so Damage* hitstun
  // (x2340) is no longer the active lane after this transition.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAADC
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAA10
  // refs/melee/src/melee/ft/chara/ftCommon/types.h (mv.co.damage.x0 at fp+0x2340)
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (misc AS variable @ fp+0x2340)
  batch->state.hitstun[vidx] = 0u;
  {
    const MslCommonParams* c = msl_common_params();
    if (c != NULL) {
      batch->state.capture_grab_timer[vidx] = capture_grab_timer_init(batch, c, victim_p, vidx);
    } else {
      batch->state.capture_grab_timer[vidx] = 0.0f;
    }
    batch->state.capture_wait_counter[vidx] = 0.0f;
    batch->state.capture_wait_anim_rate_timer[vidx] = 0.0f;
    batch->state.capture_wait_jump_latch[vidx] = 0u;
    // Decomp: ftCommon_InitGrab clears the grab-mash sign latches at capture attach entry. The
    // CaptureWait Anim callback later owns the first sign change from this zero latch.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_InitGrab
    batch->state.grab_mash_stick_x_sign[vidx] = 0;
    batch->state.grab_mash_stick_y_sign[vidx] = 0;
  }
  // Catch-connect identity ownership:
  // - fn_800DA8E4 installs owner linkage pointers on the grabbed victim (`fp->x1A58/x1A5C`).
  // - Catch connect is a non-damaging callback lane (no Fighter_ProcessHit body write), so we
  //   carry the catcher identity from the pre-entry owner instance.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DAADC,fn_800DA8E4}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  batch->state.instance_hit_by[vidx] = owner_instance_id_pre_connect;
  // CapturePulled entry is a non-damaging catch-connect ownership lane; clear stale
  // damage-source attribution on victim entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAADC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DA8E4
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW ||
      batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    batch->state.last_hit_by[vidx] = 6u;
  }
  {
    // Catch-connect CapturePulled entry uses Fighter_ChangeMotionState after any same-frame
    // GuardReflect entry. That common entry helper clears x221C_b3, but does not clear the
    // GuardReflect timer-owned b1/b2 lanes; those remain visible on Slippi post-frame until
    // ftCo_80093BC0 expires x14/x18.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    const size_t flags_i =
        vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;
  }
  catch_connect_apply_post_shield_release_recharge(batch, msl_common_params(), vidx,
                                                   victim_pre_connect_action);
  const size_t flags_i = vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;

  replace_attached_victim(batch, bi, owner_p, victim_p);
  if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    // Decomp: fn_800DAADC calls fn_800DAC78 immediately after CapturePulledHi entry and applies
    // the owner capture-anchor minus victim XRotN delta to airborne victims in the same callback.
    // The grounded CapturePulledLw lane writes the vertical carry into fp->x2170 instead.
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    //   fn_800DAADC,fn_800DAC78}
    grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);
  }
  run_capture_pulled_entry_collision_callback(batch, vidx);
}

void grab_flow_update_anim_callback_pre_input(MslBatch* batch, int bi, int player) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || player < 0 ||
      player >= (int)batch->config.num_players) {
    return;
  }
  const size_t idx = msl_idx_player(bi, player);
  if (batch->state.hitlag_started_frame[idx] != 0u) {
    return;
  }

  uint16_t action = batch->state.action_id[idx];
  if (action == (uint16_t)MSL_ACT_CATCH_CUT || action == (uint16_t)MSL_ACT_CAPTURE_CUT ||
      action == (uint16_t)MSL_ACT_CAPTURE_JUMP) {
    uint32_t msid_u32 = batch->state.animation_index[idx];
    if (msid_u32 > UINT16_MAX) {
      msid_u32 = action == (uint16_t)MSL_ACT_CATCH_CUT
                     ? (uint32_t)MSL_SM_CATCH_CUT
                     : (action == (uint16_t)MSL_ACT_CAPTURE_CUT ? (uint32_t)MSL_SM_CAPTURE_CUT
                                                                : (uint32_t)MSL_SM_CAPTURE_JUMP);
    }
    if (anim_finished(batch->state.char_id[idx], (uint16_t)msid_u32,
                      batch->state.anim_frame_f32[idx])) {
      if (action == (uint16_t)MSL_ACT_CAPTURE_JUMP) {
        enter_fall_from_capture_jump_end(batch, idx);
      } else {
        enter_common_wait_or_fall_from_grab_cut_end(batch, idx);
      }
    }
    return;
  }

  if (action == (uint16_t)MSL_ACT_CATCH || action == (uint16_t)MSL_ACT_CATCH_DASH) {
    const uint32_t msid = batch->state.animation_index[idx];
    if (msid <= UINT16_MAX && anim_finished(batch->state.char_id[idx], (uint16_t)msid,
                                            batch->state.anim_frame_f32[idx])) {
      enter_wait_from_catch_end(batch, idx);
    }
    return;
  }

  if (action == (uint16_t)MSL_ACT_CATCH_PULL || action == (uint16_t)MSL_ACT_CATCH_DASH_PULL) {
    const uint32_t msid = batch->state.animation_index[idx];
    const uint8_t finished =
        (msid <= UINT16_MAX &&
         anim_finished(batch->state.char_id[idx], (uint16_t)msid, batch->state.anim_frame_f32[idx]))
            ? 1u
            : 0u;
    if (fighter_script_take_throw_flag(batch, idx, 3u) != 0u || finished != 0u) {
      const uint8_t victim = batch->state.attached_victim_port[idx];
      enter_catch_wait_from_pull(batch, idx);
      if (victim < batch->config.num_players && victim != (uint8_t)player) {
        const size_t vidx = msl_idx_player(bi, (int)victim);
        if (batch->state.grab_owner_port[vidx] == (uint8_t)player &&
            batch->state.hitlag_started_frame[vidx] == 0u) {
          enter_capture_wait_from_pulled(batch, vidx);
        }
      }
    }
    return;
  }

  if (action == (uint16_t)MSL_ACT_CATCH_ATTACK) {
    const uint32_t msid_u32 = batch->state.animation_index[idx];
    const uint16_t msid =
        msid_u32 <= UINT16_MAX ? (uint16_t)msid_u32 : (uint16_t)MSL_SM_CATCH_ATTACK;
    if (anim_finished(batch->state.char_id[idx], msid, batch->state.anim_frame_f32[idx])) {
      enter_catch_wait_from_attack(batch, idx);
    }
    return;
  }

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  if (action == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || action == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
    capturewait_anim_callback_apply(batch, c, bi, player, idx, NULL);
    return;
  }
  if (action == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI ||
      action == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
    // ftCo_CaptureDamage*_Anim ticks the common grab timer/mash owner before testing AObj end.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   ftCo_CaptureDamageHi_Anim,ftCo_CaptureDamageLw_Anim,fn_800DB8A4}
    capture_family_tick_hidden_state(batch, c, idx, NULL);
    const uint32_t msid_u32 = batch->state.animation_index[idx];
    const uint16_t msid = msid_u32 <= UINT16_MAX ? (uint16_t)msid_u32
                                                 : (action == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI
                                                        ? (uint16_t)MSL_SM_CAPTURE_DAMAGE_HI
                                                        : (uint16_t)MSL_SM_CAPTURE_DAMAGE_LW);
    if (anim_finished(batch->state.char_id[idx], msid, batch->state.anim_frame_f32[idx])) {
      (void)enter_capture_wait_from_damage(batch, idx);
    }
  }
}

void grab_flow_update_iasa(MslBatch* batch, int bi, int player) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || player < 0 ||
      player >= (int)batch->config.num_players) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const size_t idx = msl_idx_player(bi, player);
  if (batch->state.hitlag_started_frame[idx] != 0u) {
    return;
  }

  const uint16_t action = batch->state.action_id[idx];
  if (action == (uint16_t)MSL_ACT_WAIT) {
    // Decomp: Wait IASA runs ftCo_Catch_CheckInput before the grounded locomotion checks.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_Catch_CheckInput
    (void)grab_flow_try_enter_catch_from_iasa(batch, c, idx);
    return;
  }

  if (action == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || action == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
    const uint8_t owner = batch->state.grab_owner_port[idx];
    if (owner >= batch->config.num_players || owner == (uint8_t)player) {
      return;
    }
    const size_t oidx = msl_idx_player(bi, (int)owner);
    if (batch->state.attached_victim_port[oidx] != (uint8_t)player) {
      return;
    }
    if (batch->state.capture_wait_counter[idx] < c->capture_wait_jump_latch_window_frames &&
        (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_XY) != 0u) {
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DC014
      batch->state.capture_wait_jump_latch[idx] = 1u;
    }
    return;
  }

  if (action != (uint16_t)MSL_ACT_CATCH_WAIT) {
    return;
  }

  // CatchWait IASA ordering once no earlier CaptureWait breakout has replaced CatchWait:
  // pummel check before throw check.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchWait_IASA
  if (catch_input_a_pressed_edge(batch, idx)) {
    enter_catch_attack_from_wait(batch, idx);
    return;
  }

  const uint16_t throw_action = catch_wait_throw_action_from_inputs(batch, c, idx);
  if (throw_action != 0u) {
    (void)enter_throw_from_wait(batch, bi, player, idx, throw_action);
  }
}
