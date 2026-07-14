#include "throw_flow.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "common_params.h"
#include "combat.h"
#include "combat_internal.h"
#include "damage_terminal_owner.h"
#include "ecb_tables.h"
#include "fighter_script.h"
#include "grab_attachment.h"
#include "knockdown.h"
#include "locomotion.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_source_air.h"

#include <math.h>

static inline uint8_t throw_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0u;
  }
  // Decomp gates throw-state exit on ftAnim_IsFramesRemaining()==0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw{F,B,Hi,Lw}_Anim
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline void throw_enter_fall_via_ftco_fall_enter(MslBatch* batch, size_t idx);

static inline void enter_wait_or_fall_from_throw_end(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp Throw* Anim end calls ftCommon_8007D92C (ThrowF has an x2222_b0 branch to
  // ftCo_8009B56C that is currently out-of-scope for Fox/Falco suite offenders).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw{F,B,Hi,Lw}_Anim
  if (batch->state.on_ground[idx] != 0) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  } else {
    throw_enter_fall_via_ftco_fall_enter(batch, idx);
  }
}

static inline void throw_enter_fall_via_ftco_fall_enter(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t was_grounded = batch->state.on_ground[idx] ? 1u : 0u;
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
  if (was_grounded) {
    combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
  }
  // fn_800DD684 calls the ordinary ftCo_Fall_Enter for each detached fighter. Motion entry and
  // ClampAirDrift precede the grounded D5D4 transfer; this is not DDDE4's compatibility cleanup.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::fn_800DD684
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
}

void throw_flow_ground_loss_release(MslBatch* batch, int bi, int owner_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || owner_p < 0 ||
      owner_p >= (int)batch->config.num_players) {
    return;
  }
  const size_t oidx = msl_idx_player(bi, owner_p);
  const uint8_t victim_p = batch->state.attached_victim_port[oidx];
  if (victim_p != 0xFFu && victim_p < batch->config.num_players && victim_p != (uint8_t)owner_p) {
    const size_t vidx = msl_idx_player(bi, (int)victim_p);
    // fn_800DD684 first runs the complete DC920 release-placement owner, including constrained
    // XRotN sampling, connected-floor tolerance, and fallback map collision.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::fn_800DD684
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
    grab_attachment_dc920_release_now(batch, bi, owner_p, (int)victim_p, 0u);
    throw_enter_fall_via_ftco_fall_enter(batch, vidx);
  }
  batch->state.throw_coll_x4[oidx] = 0u;
  batch->state.throw_coll_x8[oidx] = 0u;
  throw_enter_fall_via_ftco_fall_enter(batch, oidx);
}

void throw_flow_resume_attached_victim_after_hold(MslBatch* batch, int bi, int owner_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || owner_p < 0 ||
      owner_p >= (int)batch->config.num_players) {
    return;
  }
  const size_t oidx = msl_idx_player(bi, owner_p);
  const uint8_t victim_p = batch->state.attached_victim_port[oidx];
  if (victim_p == 0xFFu || victim_p >= batch->config.num_players || victim_p == (uint8_t)owner_p) {
    return;
  }
  const size_t vidx = msl_idx_player(bi, (int)victim_p);
  batch->state.thrown_pause_active[vidx] = 0u;
  batch->state.thrown_pause_anim_timer[vidx] = 0.0f;
  batch->state.frame_speed_mul_fp_q16_16[vidx] = (int32_t)MSL_Q16_16_ONE;
  msl_anim_timebase_tick_once(batch, vidx);
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE974
}

static inline void throw_release_source_colldata_last_pos(float* out_x, float* out_y,
                                                          const MslBatch* batch, size_t owner_idx) {
  if (out_x == NULL || out_y == NULL || batch == NULL) {
    return;
  }
  float bottom_rel_y = batch->state.coll_ecb_bottom_rel_y[owner_idx];
  float top_rel_y = batch->state.coll_ecb_top_rel_y[owner_idx];
  if (batch->state.coll_ecb_bottom_valid[owner_idx] == 0u || !isfinite(bottom_rel_y) ||
      !isfinite(top_rel_y) || !(top_rel_y > bottom_rel_y)) {
    const uint32_t owner_anim = batch->state.animation_index[owner_idx];
    // The release flag is consumed after the thrower's priority-1 Anim advance but before its
    // current-frame map callback. DDDE4 therefore reads the ECB packet published by the previous
    // map callback, not the just-advanced desired pose. A replay reseed has no serialized ECB
    // packet and initializes this lane to the clear zero envelope; reconstruct that one hidden
    // source packet from the previous collision frame instead of treating finite zeroes as live.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_80084104}
    // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_LoadECB_inline}
    const uint16_t owner_frame = msl_ecb_prev_frame_u16(
        msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[owner_idx]));
    bottom_rel_y = msl_ecb_bottom_rel_y(batch->state.char_id[owner_idx], owner_anim, owner_frame);
    top_rel_y = msl_ecb_top_rel_y(batch->state.char_id[owner_idx], owner_anim, owner_frame);
  }

  // Throw release CollData owner:
  // ftCo_800DDDE4 writes released-fighter CollData.last_pos from the thrower root plus inline3=0
  // and inline2=0.5*(thrower.coll_data.ecb.top.y + bottom.y), then calls mpColl_800471F8. MSL's
  // release callback consumes the attached-victim root; this packet feeds the next callback's
  // ft_80081DD4/mpColl sweep.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
  *out_x = batch->state.pos_x[owner_idx];
  *out_y = batch->state.pos_y[owner_idx] + 0.5f * (top_rel_y + bottom_rel_y);
}

void throw_flow_update_anim_callback_pre_input(MslBatch* batch, int bi, int owner_p) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (bi < 0 || bi >= batch->batch_size || owner_p < 0 || owner_p >= num_players) {
    return;
  }

  // Decomp ordering notes:
  // - `anim_timebase_update_pre_input()` runs before action_update() in step_one_frame().
  // - In decomp, movescript opcodes are processed during animation advancement, and the motion
  //   state's Anim callback consumes resulting throw flags (e.g. flip facing) before Phys/Coll.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 and Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw*_Anim
  const size_t oidx = msl_idx_player(bi, owner_p);
  if (batch->state.stocks[oidx] == 0) {
    return;
  }

  const uint16_t owner_act = batch->state.action_id[oidx];
  if (msl_action_is_thrown_victim(owner_act) && batch->state.thrown_pause_active[oidx] != 0u &&
      batch->state.thrown_pause_anim_timer[oidx] != 0.0f &&
      batch->state.thrown_pause_anim_timer[oidx] == batch->state.anim_frame_f32[oidx]) {
    batch->state.frame_speed_mul_fp_q16_16[oidx] = 0;
    batch->state.thrown_pause_anim_timer[oidx] = 0.0f;
    // ftCommon_8007E3EC also filters the held fighter's internal part-4 JObj translation. The
    // simulator's attachment owner publishes the represented fighter root directly; there is no
    // independent part-4 mutable JObj lane to update here.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE5A4
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E3EC
  }
  if (!msl_action_is_throw_owner(owner_act)) {
    batch->state.throw_coll_x4[oidx] = 0u;
    batch->state.throw_coll_x8[oidx] = 0u;
    return;
  }

  // Decomp: hitlag freezes Anim callback execution (Fighter_procUpdate does not run motion state
  // Anim/Phys/Coll callbacks while fp->x2219_b5 is set).
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // Without this guard, single-frame set_throw_flags events (notably facing flip) would be
  // re-applied every frame while anim_frame_f32 is frozen in hitlag.
  if (batch->state.hitlag_started_frame[oidx] != 0) {
    return;
  }

  const uint8_t owner_pose_facing_before_throw_flags = batch->state.facing[oidx];
  const uint8_t owner_char = batch->state.char_id[oidx];
  const float owner_af = batch->state.anim_frame_f32[oidx];
  const uint8_t throw_flags = batch->state.script_throw_flags[oidx];
  batch->state.script_throw_flags[oidx] &= (uint8_t) ~((1u << 3) | (1u << 4));

  // Facing flip: set_throw_flags(hit_idx=1) -> throw_flags_b4 -> facing_dir = -facing_dir.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4 (case 1)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724 (inline1)
  if ((throw_flags & (uint8_t)(1u << 4)) != 0u) {
    batch->state.facing[oidx] = (uint8_t)!batch->state.facing[oidx];
  }

  // Release/detach (set_throw_flags hit_idx=0):
  // - Throw Anim consumes throw_flags_b3, clears it, and calls ftCo_800DE2A8 / ftCo_800DE7C0 on
  //   the victim from the thrower's Anim callback.
  // - Apply damage immediately in this per-fighter callback rather than deferring through the item
  //   pass. The decomp global proc order runs throw Anim at fighter prio 1, before later item
  //   collision/post-hit procs; this also means later player slots can observe the release in their
  //   remaining prio-1 callbacks, while earlier slots have already missed that same-frame callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
  //   ftCo_800DD724,ftCo_800DDDE4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  uint8_t rel_hit_idx = 0u;
  float rel_anim_frame = owner_af;
  if ((throw_flags & (uint8_t)(1u << 3)) != 0u) {
    // Source owns one `victim_gobj` pointer. Replay initialization normalizes the reciprocal port
    // links once; runtime consumes that one pair instead of searching all fighters every frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    const uint8_t victim_u8 = batch->state.attached_victim_port[oidx];
    if (victim_u8 < batch->config.num_players && victim_u8 != (uint8_t)owner_p) {
      const int victim_p = (int)victim_u8;
      const size_t vidx = msl_idx_player(bi, victim_p);
      if (batch->state.stocks[vidx] == 0u ||
          batch->state.grab_owner_port[vidx] != (uint8_t)owner_p ||
          !msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
        batch->state.attached_victim_port[oidx] = 0xFFu;
        batch->state.grab_owner_port[vidx] = 0xFFu;
      } else {
        // Common release same-frame attachment ownership:
        // - ftCo_800DD724 is the shared ThrowF/B/Hi/Lw release consume path.
        // - ftCo_800DDDE4 / ftCo_800DE508 still own the attached victim world placement for the
        //   current frame before detach and damage entry.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
        const float release_sweep_root_x = batch->state.pos_x[vidx];
        const float release_sweep_root_y = batch->state.pos_y[vidx];
        float source_release_last_pos_x = release_sweep_root_x;
        float source_release_last_pos_y = release_sweep_root_y;
        throw_release_source_colldata_last_pos(&source_release_last_pos_x,
                                               &source_release_last_pos_y, batch, oidx);
        // ftCo_800DDDE4 installs this sample-owner midpoint as the input endpoint for its immediate
        // 471F8 call. It is not the endpoint of the later Damage map callback: the ft_081B wrapper
        // starts that callback from the cur_pos published by this release-local collision pass.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043670,mpCollPrev,mpColl_800471F8}
        batch->state.coll_last_pos_x[vidx] = source_release_last_pos_x;
        batch->state.coll_last_pos_y[vidx] = source_release_last_pos_y;
        grab_attachment_apply_thrown_release_anchor_now(
            batch, bi, victim_p, owner_p, rel_anim_frame, owner_pose_facing_before_throw_flags);
        const uint8_t release_was_constrained = batch->state.grab_constraint_x2226_b2[vidx];
        // DDDE4 unparents the victim and consumes x2226_b2 as part of the same release packet. The
        // attachment helper samples the represented constrained world first; clearing afterward is
        // the equivalent lifetime boundary in this non-JObj runtime.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
        batch->state.grab_constraint_x2226_b2[vidx] = 0u;
        if (isfinite(release_sweep_root_x) && isfinite(release_sweep_root_y)) {
          // Release floor-sweep root ownership:
          // - ftCo_800DD724 samples the attached victim root before detach/damage entry.
          // - The next frame's ft_80081DD4/mpColl sweep can still use that pre-release CollData.prev
          //   root during active hitlag, matching the seed-visible floor_sweep_prev_pos lane.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpCheckFloor}
          batch->state.floor_sweep_prev_pos_x[vidx] = release_sweep_root_x;
          batch->state.floor_sweep_prev_pos_y[vidx] = release_sweep_root_y;
          batch->state.floor_sweep_seed_prev_pos_x[vidx] = source_release_last_pos_x;
          batch->state.floor_sweep_seed_prev_pos_y[vidx] = source_release_last_pos_y;
          batch->state.floor_sweep_seed_prev_valid[vidx] = 1u;
          batch->state.floor_sweep_prev_source_owned[vidx] = 1u;
          batch->state.floor_sweep_prev_runtime_owned[vidx] = 1u;
        }

        // Release-local mpColl publication:
        // - ftCo_800DDDE4 writes CollData.last_pos from the selected sample owner's root + ECB
        //   midpoint, writes CollData.cur_pos from the x1A70 release vector, then immediately calls
        //   mpColl_800471F8 before ftCo_800DE7C0 applies release damage.
        // - The same callback publication must happen in free-running rollout, not only as a
        //   next-frame seed endpoint, otherwise the release hit integrates from the raw below-floor
        //   x1A70 point and can falsely downbound on the following frame.
        combat_apply_ftCommon_8007D5D4_ground_to_air(batch, vidx);
        if (release_was_constrained) {
          // Source has exactly one admission gate: live x2226_b2. Character/action identity does not
          // decide which releases run collision. Clearing the constraint also unlocks the ECB before
          // the complete 471F8 wall/ceiling/floor kernel runs.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
          msl_ftcommon_unlock_ecb(batch, vidx);
          (void)mpcoll_source_air_run_release_471f8(batch, bi, victim_p, source_release_last_pos_x,
                                                    source_release_last_pos_y);
          batch->state.grab_constraint_x2174_valid[vidx] = 0u;
        }

        // Detach immediately, then enter the throw-owned Damage state directly from the victim's
        // current Thrown/Capture state. Source does not install an intermediate Fall motion:
        // ftCo_800DDDE4 clears the links and ftCo_800DE7C0 immediately calls ftCo_8008DCE0.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
        batch->state.attached_victim_port[oidx] = 0xFFu;
        batch->state.grab_owner_port[vidx] = 0xFFu;

        MslThrowHitboxParams p = {0};
        if (fighter_script_throw_hitbox_params(batch, oidx, rel_hit_idx, &p)) {
          // If this victim is later in GObj order, its ordinary priority-1 procedure now consumes
          // the fresh Damage state naturally. An earlier victim has already completed that phase.
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
          (void)combat_apply_throw_hit(batch, bi, owner_p, victim_p, &p);
        }
      }
    }
  }

  if (batch->state.throw_coll_x4[oidx] == 0u && fighter_script_cmd_var(batch, oidx, 0u) != 0u) {
    // DD724 consumes throw flags first, then cmd_vars[0] into x4 and freezes AObj playback.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    batch->state.throw_coll_x4[oidx] = 1u;
    fighter_script_clear_cmd_var(batch, oidx, 0u);
    batch->state.special_cmd0[oidx] = 0u;
    batch->state.frame_speed_mul_fp_q16_16[oidx] = 0;
    const uint8_t victim_p = batch->state.attached_victim_port[oidx];
    if (victim_p != 0xFFu && victim_p < batch->config.num_players && victim_p != (uint8_t)owner_p) {
      const size_t vidx = msl_idx_player(bi, (int)victim_p);
      batch->state.thrown_pause_active[vidx] = 1u;
      if (batch->state.anim_frame_f32[vidx] == owner_af) {
        batch->state.frame_speed_mul_fp_q16_16[vidx] = 0;
        batch->state.thrown_pause_anim_timer[vidx] = 0.0f;
      } else {
        batch->state.thrown_pause_anim_timer[vidx] = owner_af;
      }
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE920
    }
  }

  const uint32_t owner_sm_u32 = batch->state.animation_index[oidx];
  if (owner_sm_u32 <= 0xFFFFu &&
      throw_anim_finished(owner_char, (uint16_t)owner_sm_u32, owner_af)) {
    enter_wait_or_fall_from_throw_end(batch, oidx);
  }
}
