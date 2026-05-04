#include "grab_flow.h"

#include <assert.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "grab_attachment.h"
#include "input_axis.h"
#include "mpcoll_ground.h"
#include "move_tables.h"
#include "trigger_input.h"

static inline void capturewait_anim_callback_apply(MslBatch* batch, const MslCommonParams* c,
                                                   size_t vidx, uint8_t* out_mash_active);
static inline uint8_t capture_family_frame_start_matches_current_action(const MslBatch* batch,
                                                                        size_t idx);

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

static inline void enter_capture_wait_from_pulled(MslBatch* batch, int bi, int owner_p,
                                                  int victim_p, size_t vidx) {
  const uint16_t pulled_hitlag = batch->state.hitlag[vidx];
  const uint16_t pulled_hitstun = batch->state.hitstun[vidx];
  if (pulled_hitlag == 0u && pulled_hitstun == 0u) {
    // CapturePulled -> CaptureWait same-frame world-position ownership:
    // - CatchPull_Anim can enter CatchWait and dispatch victim fn_800DB6C8 in the same callback.
    // - The current frame's attached victim translation is still owned by fn_800DAD18 before the
    //   new CaptureWait motion continues on later callbacks/frames.
    // - Preserve that callback-owned capture delta once before swapping the victim motion state.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8,fn_800DAD18}
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);
  }
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
  //
  // CaptureWait entry ownership:
  // - the owner callback still gives the newly-entered victim its one immediate entry-frame anim
  //   advance here,
  // - any later first-steady extra victim advance is decided directly from the next frame's
  //   replay-visible owner/victim ordering and mash inputs in CatchWait/CatchAttack ownership.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
  //   ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8
  // }
  if (owner_p < victim_p && pulled_hitlag == 0u && pulled_hitstun == 0u) {
    msl_anim_timebase_tick_once(batch, vidx);
  }
}

static inline uint8_t action_is_catch_pull_state(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_CATCH_PULL ||
          action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL)
             ? 1u
             : 0u;
}

static inline uint8_t capture_pre_connect_action_is_damagefly(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_DAMAGE_FLY_HI:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

static inline void maybe_enter_capture_wait_lw_grounded_handoff(MslBatch* batch, int bi,
                                                                int owner_p, size_t oidx) {
  if (batch == NULL) {
    return;
  }
  if (batch->state.action_id[oidx] != (uint16_t)MSL_ACT_CATCH_WAIT ||
      !action_is_catch_pull_state(batch->state.prev_action_id[oidx])) {
    return;
  }
  if ((batch->state.input_buttons[oidx] & (uint16_t)MSL_BUTTON_A) != 0u ||
      batch->state.on_ground[oidx] == 0u || batch->state.hitlag_started_frame[oidx] != 0u) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int victim_p = 0; victim_p < num_players; victim_p++) {
    const size_t vidx = msl_idx_player(bi, victim_p);
    if ((int)batch->state.grab_owner_port[vidx] != owner_p ||
        batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_WAIT_HI ||
        batch->state.prev_action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_HI ||
        batch->state.hitlag_started_frame[vidx] != 0u) {
      continue;
    }

    MslMpcollFloorMaskResult floor_result = {0xFFFFu, batch->state.pos_y[vidx]};
    if (!mpcoll_800477e0_floor_mask_probe(batch, vidx, &floor_result)) {
      continue;
    }

    // Grounded CatchPull -> CatchWait handoff:
    // - fn_800DA1D8 drives victim fn_800DB6C8 into CaptureWaitHi/Lw in the owner callback.
    // - CaptureWaitHi_Coll calls ft_80083C00, which runs ft_80082578/mpColl_800477E0 and only
    //   invokes fn_800DBAC4 -> fn_800DBBF8 when CollData.env_flags has a floor-mask result.
    // - fn_800DBBF8 calls ftCommon_8007D7FC before entering CaptureWaitLw, so the same owned
    //   handoff also refreshes x1968_jumpsUsed (Slippi jumps_left=max_jumps).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   fn_800DA1D8,ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8
    // }
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_80082578}
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_800477E0
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    const float cur_anim = batch->state.anim_frame_f32[vidx];
    const float cur_rate = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[vidx]);
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_LW;
    msl_anim_timebase_enter(batch, vidx, cur_anim, cur_rate);
    batch->state.on_ground[vidx] = 1u;
    if (floor_result.ground_id != 0xFFFFu) {
      batch->state.ground_id[vidx] = floor_result.ground_id;
    }
    batch->state.pos_y[vidx] = floor_result.corrected_pos_y;
    const MslCharParams* ch = msl_char_params(batch->state.char_id[vidx]);
    if (ch != NULL) {
      batch->state.jumps_left[vidx] = ch->max_jumps;
    }
  }
}

static inline void maybe_run_capture_pulled_lw_immediate_floor_callback(
    MslBatch* batch, int bi, int owner_p, int victim_p, size_t vidx,
    uint16_t victim_pre_connect_action) {
  if (batch == NULL || batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_LW ||
      batch->state.on_ground[vidx] == 0u || batch->state.hitlag_started_frame[vidx] != 0u) {
    return;
  }
  // Source-shaped boundary for the supported rollout case:
  // - the victim entered AttackHi4 earlier in the same frame before the grab callback runs, so the
  //   immediate CapturePulledLw Coll callback observes the current-frame attack/collision episode;
  // - steady AttackHi4 and ordinary Guard/Catch grounded captures keep floor contact in ft_8008403C
  //   and must not take the Lw -> Hi -> Lw root projection.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::doEnter
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkProcessGrab_8006CA5C}
  if (victim_pre_connect_action != (uint16_t)MSL_ACT_ATTACK_HI4 ||
      batch->state.prev_action_id[vidx] == victim_pre_connect_action) {
    return;
  }

  const float saved_pos_x = batch->state.pos_x[vidx];
  const float saved_pos_y = batch->state.pos_y[vidx];
  const float saved_pos_z = batch->state.pos_z[vidx];
  const uint16_t saved_action = batch->state.action_id[vidx];
  const uint32_t saved_anim = batch->state.animation_index[vidx];
  const uint8_t saved_on_ground = batch->state.on_ground[vidx];
  const uint16_t saved_ground_id = batch->state.ground_id[vidx];
  const uint8_t saved_ecb_lock = batch->state.ecb_lock_timer[vidx];
  const uint8_t saved_jumps_left = batch->state.jumps_left[vidx];
  const int8_t saved_facing_dir1 = batch->state.facing_dir1[vidx];
  const uint8_t saved_fall_fast = batch->state.fall_fast[vidx];
  const uint8_t saved_kb_smashcharge_active = batch->state.kb_smashcharge_active[vidx];
  const uint8_t saved_smash_charge_state = batch->state.smash_charge_state[vidx];
  const uint8_t saved_smash_charge_frames = batch->state.smash_charge_frames[vidx];
  const uint8_t saved_smash_charge_hold_frames_max =
      batch->state.smash_charge_hold_frames_max[vidx];
  const int32_t saved_smash_charge_saved_rate_fp_q16_16 =
      batch->state.smash_charge_saved_rate_fp_q16_16[vidx];
  const uint8_t saved_anim_defer_tick_once = batch->state.anim_defer_tick_once[vidx];
  const float saved_anim_frame_f32 = batch->state.anim_frame_f32[vidx];
  const int32_t saved_anim_frame_fp_q16_16 = batch->state.anim_frame_fp_q16_16[vidx];
  const int32_t saved_frame_speed_mul_fp_q16_16 = batch->state.frame_speed_mul_fp_q16_16[vidx];
  const int16_t saved_action_frame = batch->state.action_frame[vidx];
  const uint16_t saved_attack_id = batch->state.attack_id[vidx];
  const uint16_t saved_attack_instance = batch->state.attack_instance[vidx];
  const uint16_t saved_attack_identity_last_action_id =
      batch->state.attack_identity_last_action_id[vidx];
  const uint16_t saved_instance_id = batch->state.instance_id[vidx];
  const uint8_t saved_x2073 = batch->state.instance_id_x2073[vidx];
  const uint16_t saved_motion_entry_instance_id_override =
      batch->state.motion_entry_instance_id_override[vidx];
  const uint16_t saved_identity_last = batch->state.instance_identity_last_action_id[vidx];
  const int saved_bi = bi;
  const uint16_t saved_counter = batch->state.instance_id_counter[saved_bi];

  const float cur_anim = batch->state.anim_frame_f32[vidx];
  const float cur_rate = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[vidx]);

  // Immediate CapturePulledLw Coll callback:
  // - ftCo_CapturePulledLw_Coll calls ft_8008403C; when mpColl_8004B108 reports no floor,
  //   fn_800DB230 switches Lw -> Hi, applies fn_800DAA40 attachment X/Y/Z, and then probes
  //   ft_80083C00/mpColl_800477E0 to land back in CapturePulledLw if a floor mask is present.
  // - Keep this experiment bounded to the Lw -> Hi -> Lw case that the reconstructed floor-mask
  //   probe can prove locally; otherwise restore the original grounded Lw entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Coll,fn_800DB230,fn_800DAECC,fn_800DAEEC}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80083C00}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_800477E0}
  batch->state.on_ground[vidx] = 0u;
  batch->state.ecb_lock_timer[vidx] = 0u;
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_HI;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_HI;
  msl_anim_timebase_enter(batch, vidx, cur_anim, cur_rate);
  grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);

  MslMpcollFloorMaskResult floor_result = {0xFFFFu, batch->state.pos_y[vidx]};
  if (mpcoll_800477e0_capture_root_floor_mask_probe(batch, vidx, &floor_result)) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_LW;
    batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_LW;
    msl_anim_timebase_enter(batch, vidx, cur_anim, cur_rate);
    batch->state.on_ground[vidx] = 1u;
    if (floor_result.ground_id != 0xFFFFu) {
      batch->state.ground_id[vidx] = floor_result.ground_id;
    }
    batch->state.pos_y[vidx] = floor_result.corrected_pos_y;
    const MslCharParams* ch = msl_char_params(batch->state.char_id[vidx]);
    if (ch != NULL) {
      batch->state.jumps_left[vidx] = ch->max_jumps;
    }
    return;
  }

  batch->state.pos_x[vidx] = saved_pos_x;
  batch->state.pos_y[vidx] = saved_pos_y;
  batch->state.pos_z[vidx] = saved_pos_z;
  batch->state.action_id[vidx] = saved_action;
  batch->state.animation_index[vidx] = saved_anim;
  batch->state.on_ground[vidx] = saved_on_ground;
  batch->state.ground_id[vidx] = saved_ground_id;
  batch->state.ecb_lock_timer[vidx] = saved_ecb_lock;
  batch->state.jumps_left[vidx] = saved_jumps_left;
  batch->state.facing_dir1[vidx] = saved_facing_dir1;
  batch->state.fall_fast[vidx] = saved_fall_fast;
  batch->state.kb_smashcharge_active[vidx] = saved_kb_smashcharge_active;
  batch->state.smash_charge_state[vidx] = saved_smash_charge_state;
  batch->state.smash_charge_frames[vidx] = saved_smash_charge_frames;
  batch->state.smash_charge_hold_frames_max[vidx] = saved_smash_charge_hold_frames_max;
  batch->state.smash_charge_saved_rate_fp_q16_16[vidx] = saved_smash_charge_saved_rate_fp_q16_16;
  batch->state.anim_defer_tick_once[vidx] = saved_anim_defer_tick_once;
  batch->state.anim_frame_f32[vidx] = saved_anim_frame_f32;
  batch->state.anim_frame_fp_q16_16[vidx] = saved_anim_frame_fp_q16_16;
  batch->state.frame_speed_mul_fp_q16_16[vidx] = saved_frame_speed_mul_fp_q16_16;
  batch->state.action_frame[vidx] = saved_action_frame;
  batch->state.attack_id[vidx] = saved_attack_id;
  batch->state.attack_instance[vidx] = saved_attack_instance;
  batch->state.attack_identity_last_action_id[vidx] = saved_attack_identity_last_action_id;
  batch->state.instance_id[vidx] = saved_instance_id;
  batch->state.instance_id_x2073[vidx] = saved_x2073;
  batch->state.motion_entry_instance_id_override[vidx] = saved_motion_entry_instance_id_override;
  batch->state.instance_identity_last_action_id[vidx] = saved_identity_last;
  batch->state.instance_id_counter[saved_bi] = saved_counter;
}

static inline void maybe_run_capture_pulled_hi_immediate_floor_callback(
    MslBatch* batch, size_t vidx, uint16_t victim_pre_connect_action) {
  if (batch == NULL || batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_HI ||
      batch->state.on_ground[vidx] != 0u || batch->state.hitlag_started_frame[vidx] != 0u) {
    return;
  }
  if (capture_pre_connect_action_is_damagefly(victim_pre_connect_action) ||
      capture_pre_connect_action_is_damagefly(batch->state.prev_action_id[vidx]) ||
      capture_pre_connect_action_is_damagefly(batch->state.seed_prev_action_id[vidx])) {
    // DamageFly collision/ECB ownership remains with the damage family until CapturePulled entry;
    // stale floor ids on these airborne victims are not proof for the immediate capture-root floor
    // callback.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CapturePulledHi_Coll
    return;
  }

  MslMpcollFloorMaskResult floor_result = {0xFFFFu, batch->state.pos_y[vidx]};
  uint8_t floor_mask = mpcoll_800477e0_floor_mask_probe(batch, vidx, &floor_result);
  if (floor_mask == 0u &&
      ((batch->state.ground_id[vidx] != 0xFFFFu && batch->state.ecb_lock_timer[vidx] != 0u) ||
       (victim_pre_connect_action == (uint16_t)MSL_ACT_ATTACK_AIR_HI &&
        batch->state.prev_action_id[vidx] == (uint16_t)MSL_ACT_KNEE_BEND))) {
    floor_mask = mpcoll_800477e0_capture_root_floor_mask_probe(batch, vidx, &floor_result);
  }
  if (floor_mask == 0u) {
    return;
  }

  // Immediate airborne catch-connect collision callback:
  // - fn_800DAADC installs CapturePulledHi when the callback target is airborne, applies the
  //   CapturePulledHi anchor delta, then calls the victim's collision callback through fp+0x21A8.
  // - ftCo_CapturePulledHi_Coll -> ft_80083C00 -> fn_800DAECC/fn_800DAEEC lands the victim into
  //   CapturePulledLw when mpColl_800477E0 produces a floor-mask result.
  // - Same-frame airborne captures can carry a locked current CollData floor index into this
  //   callback before the capture anchor delta settles the victim; require the data-backed
  //   `ecb_lock` owner with the floor id so stale airborne floor ids from other motion owners do not
  //   force a landing. The older KneeBend -> AttackAirHi bridge remains as the one known
  //   missing-floor-index source episode until its CollData seed owner is promoted.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledHi_Coll,fn_800DAECC,fn_800DAEEC}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_80082578}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800477E0
  const float cur_anim = batch->state.anim_frame_f32[vidx];
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_LW;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_LW;
  msl_anim_timebase_enter(batch, vidx, cur_anim, 1.0f);
  batch->state.on_ground[vidx] = 1u;
  if (floor_result.ground_id != 0xFFFFu) {
    batch->state.ground_id[vidx] = floor_result.ground_id;
  }
  batch->state.pos_y[vidx] = floor_result.corrected_pos_y;
  const MslCharParams* ch = msl_char_params(batch->state.char_id[vidx]);
  if (ch != NULL) {
    batch->state.jumps_left[vidx] = ch->max_jumps;
  }
}

static inline uint8_t capturewait_grab_mash_active(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  const uint16_t buttons = batch->state.input_buttons[idx];
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
  // ftCommon_GrabMash updates x1A50/x1A51 when lstick.{x,y} crosses +/-x308, and mash succeeds on
  // any sign-latch change even without AB/XY/LR holds.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
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

static inline uint8_t capturewait_grab_mash_active_peek(const MslBatch* batch,
                                                        const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  const uint16_t buttons = batch->state.input_buttons[idx];
  uint8_t result = ((buttons & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_X |
                                          MSL_BUTTON_Y | MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
                       ? 1u
                       : 0u;
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  int8_t next_x = batch->state.grab_mash_stick_x_sign[idx];
  int8_t next_y = batch->state.grab_mash_stick_y_sign[idx];
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
  if (batch->state.grab_mash_stick_x_sign[idx] != next_x ||
      batch->state.grab_mash_stick_y_sign[idx] != next_y) {
    result = 1u;
  }
  return result;
}

static inline uint8_t capturewait_first_steady_extra_tick_active_post_input(
    const MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  const uint16_t buttons =
      (uint16_t)(batch->state.input_buttons[idx] | batch->state.prev_input_buttons[idx]);
  uint8_t result = ((buttons & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_X |
                                          MSL_BUTTON_Y | MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
                       ? 1u
                       : 0u;
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  int8_t next_x = batch->state.grab_mash_stick_x_sign[idx];
  int8_t next_y = batch->state.grab_mash_stick_y_sign[idx];
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
  if (batch->state.grab_mash_stick_x_sign[idx] != next_x ||
      batch->state.grab_mash_stick_y_sign[idx] != next_y) {
    result = 1u;
  }
  return result;
}

static inline uint8_t capturewait_is_first_steady_row(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t va = batch->state.action_id[idx];
  if (va != (uint16_t)MSL_ACT_CAPTURE_WAIT_HI && va != (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
    return 0u;
  }
  return (batch->state.prev_action_id[idx] == va && batch->state.prev_action_frame[idx] == 1 &&
          batch->state.seed_prev_action_id[idx] == va &&
          batch->state.seed_prev_action_frame[idx] == 0)
             ? 1u
             : 0u;
}

static inline uint8_t capturewait_should_apply_first_steady_extra_tick(const MslBatch* batch,
                                                                       const MslCommonParams* c,
                                                                       int owner_p, int victim_p,
                                                                       size_t oidx, size_t vidx) {
  if (batch == NULL || c == NULL || victim_p < 0) {
    return 0u;
  }
  const uint16_t held_or_carried =
      (uint16_t)(batch->state.input_buttons[vidx] | batch->state.prev_input_buttons[vidx]);
  const uint16_t victim_action = batch->state.action_id[vidx];
  const uint16_t victim_seed_prev = batch->state.seed_prev_action_id[vidx];
  const uint8_t held_buttons_active =
      ((held_or_carried & (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_X | MSL_BUTTON_Y |
                                     MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
          ? 1u
          : 0u;
  const uint8_t mash_active =
      (held_buttons_active != 0u ||
       capturewait_first_steady_extra_tick_active_post_input(batch, c, vidx) != 0u)
          ? 1u
          : 0u;

  if (victim_p > owner_p) {
    const uint8_t owner_earlier_callback_shape =
        ((batch->state.prev_action_id[oidx] == (uint16_t)MSL_ACT_CATCH_WAIT &&
          batch->state.prev_action_frame[oidx] == 0) ||
         (batch->state.prev_action_id[oidx] == (uint16_t)MSL_ACT_CATCH_ATTACK &&
          batch->state.prev_action_frame[oidx] == 0 &&
          batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_ATTACK &&
          batch->state.action_frame[oidx] == 1))
            ? 1u
            : 0u;
    return (owner_earlier_callback_shape != 0u &&
            action_is_catch_pull_state(batch->state.seed_prev_action_id[oidx]) != 0u &&
            (victim_action == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW ||
             victim_action == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI) &&
            batch->state.prev_action_id[vidx] == victim_action &&
            batch->state.prev_action_frame[vidx] == 1 &&
            ((victim_action == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW &&
              ((victim_seed_prev == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
                batch->state.seed_prev_action_id[oidx] == (uint16_t)MSL_ACT_CATCH_PULL) ||
               victim_seed_prev == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI ||
               victim_seed_prev == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI)) ||
             (victim_action == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI &&
              victim_seed_prev == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI)) &&
            mash_active != 0u)
               ? 1u
               : 0u;
  }
  return (capturewait_is_first_steady_row(batch, vidx) != 0u && mash_active != 0u) ? 1u : 0u;
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
  batch->state.capture_breakout_pending[idx] = 0u;
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
  batch->state.on_ground[vidx] = 0u;
  batch->state.ground_id[vidx] = 0xFFFFu;
  batch->state.speed_ground_x_self[vidx] = 0.0f;
  batch->state.speed_air_x_self[vidx] =
      -(float)batch->state.facing_dir1[vidx] * c->capture_jump_escape_speed_x;
  batch->state.speed_y_self[vidx] = c->capture_jump_escape_speed_y;
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_JUMP;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_JUMP;
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
  capture_hidden_state_clear(batch, vidx);
}

static inline void capturewait_anim_callback_apply(MslBatch* batch, const MslCommonParams* c,
                                                   size_t vidx, uint8_t* out_mash_active) {
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
    // Decomp adjacency owner:
    // - CaptureWaitHi_Anim decrements grab_timer and reaches the breakout gate here.
    // - Shared CatchWait ownership still decides pummel / throw / breakout adjacency later in the
    //   same frame's IASA path, so keep only the explicit timer/jump-latch owner state here and
    //   resolve the actual CatchCut/CaptureCut/CaptureJump transition in CatchWait pre-physics.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   ftCo_CaptureWaitHi_Anim,ftCo_CatchWait_IASA
    // }
    return;
  }

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

static inline uint8_t capture_family_frame_start_matches_current_action(const MslBatch* batch,
                                                                        size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.seed_prev_action_id[idx] == batch->state.action_id[idx]) ? 1u : 0u;
}

static inline uint8_t capture_family_should_run_anim_callback_pre_input(const MslBatch* batch,
                                                                        size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // CapturePulled*/Damage* -> CaptureWait* can be entered from the grab owner's callback after
  // the victim's prio-1 Anim callback phase has already passed for that game frame. The first
  // replay-visible CaptureWait snapshot (usually af=1 with seed_prev still Pulled/Damage) must not
  // immediately run CaptureWait_Anim again; wait until frame-start state was already CaptureWait.
  //
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkProcessGrab_8006CA5C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CaptureWaitHi_Anim,ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
  return capture_family_frame_start_matches_current_action(batch, idx);
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
  batch->state.capture_wait_anim_rate_timer[vidx] = 0.0f;
  batch->state.capture_breakout_pending[vidx] = 0u;
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
  batch->state.capture_breakout_pending[vidx] = 0u;
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

void grab_flow_enter_catchdash_from_attackdash_pregate(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // AttackDash IASA pre-gate consumes into CatchDash via ftCo_800D8C54(msid=0xD6).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_800D8AE0,ftCo_800D8C54}
  enter_catch_motion_state(batch, idx, (uint16_t)MSL_ACT_CATCH_DASH, (uint32_t)MSL_SM_CATCH_DASH);
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

static inline int throw_index_from_action(uint16_t throw_action) {
  switch (throw_action) {
    case (uint16_t)MSL_ACT_THROW_F:
      return 0;
    case (uint16_t)MSL_ACT_THROW_B:
      return 1;
    case (uint16_t)MSL_ACT_THROW_HI:
      return 2;
    case (uint16_t)MSL_ACT_THROW_LW:
      return 3;
    default:
      return -1;
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
  const float victim_entry_pos_y = batch->state.pos_y[vidx];
  float throw_anim_speed = 1.0f;
  {
    const MslCommonParams* c = msl_common_params();
    const MslCharParams* owner_ch = msl_char_params(batch->state.char_id[oidx]);
    const MslCharParams* victim_ch = msl_char_params(batch->state.char_id[vidx]);
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
    const int throw_index = throw_index_from_action(throw_action);
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

  // Throw-entry state-machine ownership and attachment handoff:
  // - CatchWait->Throw* and CaptureWait/Pulled->Thrown* transitions are owned here.
  // - Positional attachment offsets are computed by grab_attachment_recompute_offsets_for_thrown_entry
  //   with ftCo_800DE508-shaped axis/scaling semantics.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  batch->state.action_id[vidx] = thrown_action;
  batch->state.animation_index[vidx] = victim_sm;
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
  if (throw_action == (uint16_t)MSL_ACT_THROW_LW && thrown_action == (uint16_t)MSL_ACT_THROWN_LW) {
    // ThrowLw/ThrownLw entry split (proven subset):
    // - ftCo_800DB368 reparents victim FtPart_XRotN under the thrower's FtPart_TransN2 before the
    //   thrown accessory callback takes over.
    // - On the immediate low-throw handoff, the forward residual collapses onto the new attached
    //   joint while the vertical residual remains carried from the pre-entry world.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    const float scale_y = batch->state.fighter_scale_y[vidx];
    grab_attachment_query_thrown_anchor_world(&ax, &ay, &az, batch, bi, victim_p, owner_p);
    (void)ax;
    (void)az;
    if (scale_y > 0.0f) {
      batch->state.grab_offset_y[vidx] = (victim_entry_pos_y - ay) / scale_y;
    }
    batch->state.grab_offset_z[vidx] = 0.0f;
  } else {
    // Thrown entry from CatchWait/CaptureWait owns an immediate attachment position through the
    // ftCo_800DE3FC -> ftCo_800DE508 path. Use the victim's static x1A70 analog instead of
    // preserving the pre-entry capture world position as an offset; otherwise rollout keeps the
    // old CaptureWait/CaptureDamage position for the first attached Thrown* frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
    // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
    grab_attachment_use_static_offsets_for_thrown_entry(batch, bi, victim_p, owner_p);
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
  }
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
  const uint16_t owner_instance_id_pre_connect = batch->state.instance_id[oidx];
  const uint16_t victim_pre_connect_action = batch->state.action_id[vidx];
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
  // Sim mapping: xE0 is represented by victim on_ground. DamageFly-family victims can carry a
  // transient grounded collision result during damage collision ownership; source catch entry still
  // treats the pre-connect airborne DamageFly owner as CapturePulledHi.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
  if (batch->state.on_ground[vidx] != 0 &&
      !capture_pre_connect_action_is_damagefly(victim_pre_connect_action) &&
      !capture_pre_connect_action_is_damagefly(batch->state.prev_action_id[vidx]) &&
      !capture_pre_connect_action_is_damagefly(batch->state.seed_prev_action_id[vidx])) {
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
    batch->state.capture_breakout_pending[vidx] = 0u;
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
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  const size_t flags_i = vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;

  // Decomp has a single victim_gobj pointer per owner; keep exactly one attached victim link.
  for (int p = 0; p < num_players; p++) {
    const size_t pidx = msl_idx_player(bi, p);
    if (p != victim_p && (int)batch->state.grab_owner_port[pidx] == owner_p) {
      batch->state.grab_owner_port[pidx] = 0xFFu;
    }
  }
  batch->state.attached_victim_port[oidx] = (uint8_t)victim_p;
  batch->state.grab_owner_port[vidx] = (uint8_t)owner_p;
  if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    // Decomp: fn_800DAADC calls fn_800DAC78 immediately after CapturePulledHi entry and applies
    // the owner capture-anchor minus victim XRotN delta to airborne victims in the same callback.
    // The grounded CapturePulledLw lane writes the vertical carry into fp->x2170 instead.
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    //   fn_800DAADC,fn_800DAC78}
    grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);
    maybe_run_capture_pulled_hi_immediate_floor_callback(batch, vidx, victim_pre_connect_action);
  } else if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW) {
    maybe_run_capture_pulled_lw_immediate_floor_callback(batch, bi, owner_p, victim_p, vidx,
                                                         victim_pre_connect_action);
  }
}

void grab_flow_update_anim_callbacks_pre_input(MslBatch* batch) {
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
          for (int victim_p = 0; victim_p < num_players; victim_p++) {
            const size_t vidx = msl_idx_player(bi, victim_p);
            if (batch->state.hitlag_started_frame[vidx] != 0) {
              continue;
            }
            if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
              continue;
            }
            enter_capture_wait_from_pulled(batch, bi, owner_p, victim_p, vidx);
          }
          oa = batch->state.action_id[oidx];
        }
      }

      if (oa == (uint16_t)MSL_ACT_CATCH_ATTACK) {
        if (move_tables_catchattack_grabbed_hit_active(batch->state.char_id[oidx],
                                                       batch->state.anim_frame_f32[oidx])) {
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
          owner_catch_attack_ended = 1u;
        }
      }

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
          if (victim_p > owner_p) {
            continue;
          }
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

      if (owner_catch_attack_ended) {
        enter_catch_wait_from_attack(batch, oidx);
        oa = batch->state.action_id[oidx];
        for (int victim_p = owner_p + 1; victim_p < num_players; victim_p++) {
          const size_t vidx = msl_idx_player(bi, victim_p);
          if (batch->state.hitlag_started_frame[vidx] != 0) {
            continue;
          }
          if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
            continue;
          }
          const uint16_t va = batch->state.action_id[vidx];
          if (va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI ||
              va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
            (void)enter_capture_wait_from_damage(batch, vidx);
          }
        }
      }

      if (c == NULL) {
        continue;
      }
      for (int victim_p = 0; victim_p < num_players; victim_p++) {
        const size_t vidx = msl_idx_player(bi, victim_p);
        if (batch->state.hitlag_started_frame[vidx] != 0) {
          continue;
        }
        if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
          continue;
        }
        const uint16_t va = batch->state.action_id[vidx];
        if (va == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || va == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) {
          if (capture_family_should_run_anim_callback_pre_input(batch, vidx) == 0u) {
            continue;
          }
          capturewait_anim_callback_apply(batch, c, vidx, NULL);
        } else if (va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_HI ||
                   va == (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW) {
          if (capture_family_should_run_anim_callback_pre_input(batch, vidx) == 0u) {
            continue;
          }
          capture_family_tick_hidden_state(batch, c, vidx, NULL);
        }
      }
    }
  }
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

      if (oa == (uint16_t)MSL_ACT_WAIT && c != NULL) {
        // Decomp: Wait IASA runs ftCo_Catch_CheckInput before the grounded locomotion checks.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_Catch_CheckInput
        if (grab_flow_try_enter_catch_from_iasa(batch, c, oidx)) {
          continue;
        }
      }

      if (c == NULL) {
        continue;
      }

      const uint8_t owner_grab_core_iasa =
          (oa == (uint16_t)MSL_ACT_CATCH_WAIT || oa == (uint16_t)MSL_ACT_CATCH_ATTACK) ? 1u : 0u;
      if (owner_grab_core_iasa == 0u) {
        continue;
      }

      int breakout_victim_p = -1;
      size_t breakout_vidx = 0u;
      for (int victim_p = 0; victim_p < num_players; victim_p++) {
        const size_t vidx = msl_idx_player(bi, victim_p);
        if ((int)batch->state.grab_owner_port[vidx] != owner_p ||
            batch->state.hitlag_started_frame[vidx] != 0u) {
          continue;
        }
        const uint16_t va = batch->state.action_id[vidx];
        if (capturewait_should_apply_first_steady_extra_tick(batch, c, owner_p, victim_p, oidx,
                                                             vidx) != 0u) {
          const int32_t saved_rate = batch->state.frame_speed_mul_fp_q16_16[vidx];
          batch->state.frame_speed_mul_fp_q16_16[vidx] = MSL_Q16_16_ONE;
          msl_anim_timebase_tick_once(batch, vidx);
          batch->state.frame_speed_mul_fp_q16_16[vidx] = saved_rate;
          capturewait_anim_callback_apply(batch, c, vidx, NULL);
        }
        if ((va == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || va == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) &&
            batch->state.capture_wait_counter[vidx] < c->capture_wait_jump_latch_window_frames &&
            (batch->state.input_buttons_pressed[vidx] & (uint16_t)MSL_BUTTON_XY) != 0u) {
          // Decomp: CaptureWait*_IASA calls fn_800DC014, which gates the jump latch on
          // fp->input.x668 & HSD_PAD_XY, not held_inputs.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DC014
          batch->state.capture_wait_jump_latch[vidx] = 1u;
        }
        if ((va == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || va == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) &&
            batch->state.capture_breakout_pending[vidx] != 0u) {
          breakout_victim_p = victim_p;
          breakout_vidx = vidx;
        }
      }

      if (oa != (uint16_t)MSL_ACT_CATCH_WAIT) {
        continue;
      }

      // CatchWait IASA ordering: pummel check before throw check.
      // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchWait_IASA
      if (catch_input_a_pressed_edge(batch, oidx)) {
        enter_catch_attack_from_wait(batch, oidx);
        continue;
      }

      const uint16_t throw_action = catch_wait_throw_action_from_inputs(batch, c, oidx);
      if (throw_action != 0u) {
        (void)enter_throw_from_wait(batch, bi, owner_p, oidx, throw_action);
        continue;
      }

      maybe_enter_capture_wait_lw_grounded_handoff(batch, bi, owner_p, oidx);

      if (breakout_victim_p >= 0) {
        enter_catch_cut_from_capture_breakout(batch, c, oidx);
        clear_grab_linkage(batch, oidx, breakout_vidx);
        if (batch->state.capture_wait_jump_latch[breakout_vidx] != 0u ||
            apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[breakout_vidx]),
                           c->lstick_deadzone_y) >= c->tap_jump_threshold) {
          enter_capture_jump_from_breakout(batch, c, breakout_vidx);
        } else {
          enter_capture_cut_from_breakout(batch, c, breakout_vidx);
        }
        batch->state.capture_breakout_pending[breakout_vidx] = 0u;
      }
    }
  }
}
