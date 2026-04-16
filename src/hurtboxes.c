#include "hurtboxes.h"

#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "common_params.h"
#include "hit_status_tables.h"
#include "hurtbox_modes_tables.h"
#include "hurtcaps_tables.h"
#include "msl_math.h"
#include "mtx34.h"

enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t hurtboxes_guard_fallback_submotion(uint16_t action_id, uint16_t* out_msid) {
  if (out_msid == NULL) {
    return 0u;
  }
  // Guard-family motion-state -> submotion mapping from ftmotionstates table.
  // refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardOff,
  //                                            ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
  // Note: GuardReflect uses ftCo_SM_GuardOn in GALE01.
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
      *out_msid = (uint16_t)MSL_SM_GUARD_ON;
      return 1u;
    case MSL_ACT_GUARD:
      *out_msid = (uint16_t)MSL_SM_GUARD;
      return 1u;
    case MSL_ACT_GUARD_OFF:
      *out_msid = (uint16_t)MSL_SM_GUARD_OFF;
      return 1u;
    case MSL_ACT_GUARD_SET_OFF:
      *out_msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
      return 1u;
    case MSL_ACT_GUARD_REFLECT:
      // Decomp motion-state mapping: GuardReflect uses ftCo_SM_GuardOn as its submotion table.
      // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
      *out_msid = (uint16_t)MSL_SM_GUARD_ON;
      return 1u;
    default:
      return 0u;
  }
}

static inline uint16_t hurtboxes_timer_remaining_from_action_frame(uint16_t init_frames,
                                                                   int16_t action_frame) {
  if (init_frames == 0u) {
    return 0u;
  }
  if (action_frame <= 0) {
    return init_frames;
  }
  int rem = (int)init_frames + 1 - (int)action_frame;
  if (rem < 0) {
    rem = 0;
  }
  if (rem > 0xFFFF) {
    rem = 0xFFFF;
  }
  return (uint16_t)rem;
}

static inline uint8_t hurtboxes_runtime_specialhi_pose_owner(uint8_t char_id, uint16_t action_id) {
  if (char_id != (uint8_t)MSL_CHAR_FOX && char_id != (uint8_t)MSL_CHAR_FALCO) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hurtboxes_apply_specialhi_local_xrotn(const MslBatch* batch, size_t idx,
                                                            uint8_t char_id, uint16_t msid,
                                                            uint16_t pose_frame, uint16_t part_id,
                                                            float facing_dir, float model_scale,
                                                            float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!hurtboxes_runtime_specialhi_pose_owner(char_id, action_id)) {
    return 0u;
  }
  (void)part_id;

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, pose_frame, 2u, m) != 0) {  // FtPart_XRotN
    return 0u;
  }

  const float vel_x = batch->state.speed_air_x_self[idx];
  const float vel_y = batch->state.speed_y_self[idx];
  if (!(fabsf(vel_x) > 0.0f || fabsf(vel_y) > 0.0f)) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  // Decomp: Firefox/Firebird launch writes `rotateModel = atan2f(self_vel.y, self_vel.x * facing_dir)`
  // and applies it with `ftPartSetRotX(..., 2*pi - rotateModel)` on FtPart_XRotN.
  // Victim hurtcaps bound under that subtree inherit the same runtime local rotation.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
  const float angle = (2.0f * MSL_PI_F) - atan2f(vel_y, vel_x * facing_dir);

  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline void hurtboxes_apply_colanim_action_entry(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t action = batch->state.action_id[idx];
  const uint16_t prev_action = batch->state.prev_action_id[idx];
  if (action == prev_action) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int16_t action_frame = batch->state.action_frame[idx];

  // RebirthWait -> Fall colanim ownership (x1994/x198C):
  //
  // Decomp:
  // - RebirthWait_Anim and RebirthWait_IASA call ftColl_8007B7A4(gobj, p_ftCommonData->x5D8)
  //   immediately before Fall enter.
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::{ftCo_RebirthWait_Anim,ftCo_RebirthWait_IASA}
  // - RebirthWait_Coll helper fn_800D5A30 also calls ftColl_8007B7A4(..., x5D8) before ft_8008A2BC.
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::fn_800D5A30
  //
  // Seed-bridge note:
  // - Some teacher-forced seeds can observe a direct Rebirth -> Fall snapshot without the explicit
  //   intermediate RebirthWait row. Treat prev_action=Rebirth as equivalent for this entry hook.
  if (action == (uint16_t)MSL_ACT_FALL &&
      (prev_action == (uint16_t)MSL_ACT_REBIRTH_WAIT || prev_action == (uint16_t)MSL_ACT_REBIRTH)) {
    const uint16_t rem = hurtboxes_timer_remaining_from_action_frame(
        c->colanim_rebirth_fall_x1994_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1994[idx]) {
      batch->state.colanim_timer_x1994[idx] = rem;
    }
    if (rem != 0u) {
      batch->state.colanim_hit_status_x198c[idx] =
          (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
    }
  }

  // Throw entry ownership:
  // - ftCo_800DD398 enters Throw* and calls ftColl_8007B7A4(..., x348), which sets x1994 and x198C.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  if (action == (uint16_t)MSL_ACT_THROW_F || action == (uint16_t)MSL_ACT_THROW_B ||
      action == (uint16_t)MSL_ACT_THROW_HI || action == (uint16_t)MSL_ACT_THROW_LW) {
    uint16_t rem =
        hurtboxes_timer_remaining_from_action_frame(c->colanim_throw_x1994_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1994[idx]) {
      batch->state.colanim_timer_x1994[idx] = rem;
    }
    batch->state.colanim_hit_status_x198c[idx] =
        (batch->state.colanim_timer_x1990[idx] != 0u) ? 2u : 1u;
  }

  // Cliff catch/wait invulnerability timer ownership (x49C -> x1990):
  // decomp callsite anchor: ftCo_CliffWait path uses ftColl_8007B760(..., x49C).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C
  if (action == (uint16_t)MSL_ACT_CLIFF_CATCH || action == (uint16_t)MSL_ACT_CLIFF_WAIT) {
    uint16_t rem =
        hurtboxes_timer_remaining_from_action_frame(c->colanim_cliff_x1990_frames, action_frame);
    if (rem > batch->state.colanim_timer_x1990[idx]) {
      batch->state.colanim_timer_x1990[idx] = rem;
    }
    if (rem != 0u) {
      batch->state.colanim_hit_status_x198c[idx] = 2u;
    }
  }
}

void hurtboxes_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp semantics:
  // - Hurt capsule init records are `ftHurtboxInit` (refs/melee/src/melee/ft/chara/ftCommon/types.h).
  // - ftColl_HurtboxInit assigns offsets/scale and binds `hurt->capsule.bone` to
  //   `fp->parts[hurt->capsule.bone_idx].joint` (refs/melee/src/melee/ft/ftcoll.c::ftColl_HurtboxInit).
  // - Hurt capsule endpoint world positions are computed from (bone joint matrix, offsets) via
  //   lb_8000B1CC (refs/melee/src/melee/lb/lbcollision.c::checkPos), written into HurtCapsule.a_pos/b_pos.
  //
  // We approximate that pipeline using our SSANIM01 pose sampler:
  // - anim_pose_get_matrix(char_id, msid, frame, part_id=Fighter_Part, out_3x4)
  // and then applying fighter translation (pos_x/pos_y/pos_z) in world space.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.hurtcap_count[idx] = 0;
      // Clear fixed slots for stable debug readback (and to avoid stale values when pose lookups
      // or script masks disable/skip specific capsules).
      for (int ci = 0; ci < MSL_MAX_HURTCAPS; ci++) {
        const size_t hi = idx_hurtcap(bi, p, ci);
        batch->state.hurtcap_enabled[hi] = 0;
        batch->state.hurtcap_a_x[hi] = 0.0f;
        batch->state.hurtcap_a_y[hi] = 0.0f;
        batch->state.hurtcap_a_z[hi] = 0.0f;
        batch->state.hurtcap_b_x[hi] = 0.0f;
        batch->state.hurtcap_b_y[hi] = 0.0f;
        batch->state.hurtcap_b_z[hi] = 0.0f;
        batch->state.hurtcap_radius[hi] = 0.0f;
        batch->state.hurtcap_is_grabbable[hi] = 0;
        batch->state.hurtcap_height[hi] = 0;
      }
      if (p >= num_players) {
        continue;
      }

      const uint8_t char_id = batch->state.char_id[idx];
      const uint16_t action_id = batch->state.action_id[idx];
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      const uint16_t prev_action_id = batch->state.prev_action_id[idx];
      hurtboxes_apply_colanim_action_entry(batch, idx);
      uint8_t final_hurtbox_state = batch->state.colanim_hit_status_x198c[idx];
      const uint8_t preserve_visible_downbound_colanim =
          (((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
             action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) ||
            ((prev_action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
              prev_action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
             (action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U ||
              action_id == (uint16_t)MSL_ACT_DOWN_WAIT_D ||
              action_id == (uint16_t)MSL_ACT_FALL))) &&
           batch->state.colanim_hit_status_x198c[idx] == 1u &&
           batch->state.colanim_timer_x1994[idx] != 0u)
              ? 1u
              : 0u;
      if (preserve_visible_downbound_colanim) {
        // Narrow visible-state bridge for DownBound x198C=1 rows:
        // - Combat/collision still needs the explicit x198C lane for invincible-contact gating.
        // - Slippi visible hurtbox_state can remain 0 on these timer-owned rows even while the
        //   hidden x198C/x1994 internals are active.
        // Preserve the visible compare lane and let combat read x198C directly.
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        final_hurtbox_state = batch->state.hurtbox_state[idx];
      }

      // Hurtbox-state composition:
      // - x1988 lane: movescript-derived hit status (opcode 26 / ftColl_8007B62C).
      // - x198C lane: timer/system-owned collision status (x1990/x1994 path).
      // Slippi post-frame reports x1988 when nonzero, else x198C.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
      uint8_t hit_status = 0;
      uint8_t have_hit_status_override = 0;
      if (batch->debug_hit_status_override != NULL) {
        const uint8_t ov = batch->debug_hit_status_override[idx];
        if (ov != 0xFFu) {
          hit_status = ov;
          have_hit_status_override = 1;
        }
      }

      uint16_t msid = 0u;
      if (anim_u32 > 0xFFFFu) {
        if (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            batch->state.action_frame[idx] <= (int16_t)-2 &&
            batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_ON) {
          // GuardReflect no-submotion late-phase snapshots are ordering-sensitive with shield
          // descriptor ownership (x221B_b0 via ftColl_8007B1B8). Preserve raw snapshot geometry on
          // the locomotion-entry lanes that still own shield desc, but let GuardOn_IASA powershield
          // entry (ftCo_8009388C) fall through to the GuardReflect submotion fallback because that
          // entry path already cleared shield desc and can take BODY damage on the same frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
          // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
          if (hit_status != 0) {
            final_hurtbox_state = hit_status;
          }
          batch->state.hurtbox_state[idx] = final_hurtbox_state;
          continue;
        }
        // Seed-bridge fallback (guard-family only):
        // Slippi post-frames commonly encode guard-family snapshots with animation_index=-1 while
        // decomp collision still uses the active ftCo submotion timeline. Keep the raw compare
        // field unchanged, but derive hurtcaps from the decomp motion-state table mapping.
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // refs/melee/src/melee/ft/ftmotionstates.c (Guard* motion-state rows)
        //
        // Guard (hold) no-submotion snapshots with x221B_b0 can still resolve BODY contacts when
        // shield overlap fails ("shield poke"), so keep fallback hurtcaps available when we can
        // map the current guard-family motion-state to its submotion table.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076CBC,ftColl_80076ED8}
        if (!hurtboxes_guard_fallback_submotion(action_id, &msid)) {
          if (hit_status != 0) {
            final_hurtbox_state = hit_status;
          }
          batch->state.hurtbox_state[idx] = final_hurtbox_state;
          continue;
        }
      } else {
        msid = (uint16_t)anim_u32;
      }

      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
      if ((action_id == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_F ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW) &&
          batch->state.anim_defer_tick_once[idx] != 0u && frame != 0xFFFFu) {
        // AttackAir entry hurtcaps need the post-ChangeMotionState immediate ftAnim tick.
        //
        // Decomp:
        // - ftCo_AttackAir_EnterFromMsid enters the motion state, then immediately calls
        //   ftAnim_8006EBA4 before the current frame's collision owner runs.
        // - The simulator defers that tick globally to preserve entry-pose hitbox timing, but the
        //   defender-side hurtcaps on the same frame must still sample the post-tick pose.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
        // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
        frame = (uint16_t)(frame + 1u);
      }
      if ((action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
           action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) &&
          batch->state.on_ground[idx] != 0u && batch->state.colanim_hit_status_x198c[idx] == 1u &&
          batch->state.colanim_timer_x1994[idx] != 0u) {
        // Narrow DownBound post-Anim hurtcaps pose bridge:
        // - DownBound callback ordering is Anim then Coll on the same frame.
        // - On grounded x198C=1 / x1994>0 bounce rows, pre-combat hurtcaps need the post-Anim pose
        //   to avoid a replay-false invincible BODY contact against the adjacent AttackDash frame.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Anim,ftCo_DownBound_Coll
        // }
        if (frame != 0xFFFFu) {
          frame = (uint16_t)(frame + 1u);
        }
      }
      if (!have_hit_status_override && !preserve_visible_downbound_colanim) {
        (void)hit_status_get(char_id, msid, frame, &hit_status);
      }
      if (hit_status != 0) {
        // Decomp timing: move-induced hit status (fp->x1988) is set by movescript opcode 26
        // (ftAction_80071A14 -> ftColl_8007B62C) while executing the fighter cmd script inside
        // ftAnim_8006EBA4 (ftAction_80073240), which runs at proc priority 1 before input/IASA.
        // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        //
        // Collision eligibility in decomp consults the aggregate hit status via ftColl_8007B868,
        // which combines x1988 (movescript), x198C (game-induced), and x221D_b6.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        //
        // If the sim enters a new motion state after the pre-input Anim tick (e.g. due to
        // input/IASA), the new state's movescript will not execute until the next frame's Anim
        // proc. Slippi's post-frame `hurtbox_state` prefers x1988 only when it has actually been
        // set nonzero for that frame (SendGamePostFrame.asm checks fp+0x1988 then fp+0x198C).
        //
        // Mirror that ordering by deferring the table-derived x1988 override on the entry frame
        // when we can observe that:
        // - the fighter changed action state this step (prev_action_id != action_id), and
        // - the new state's anim timebase is still at integer frame 0 (no Anim proc yet).
        //
        // NOTE(shine_entry): Some motion-state entry helpers call ftAnim_8006EBA4 immediately
        // after Fighter_ChangeMotionState, meaning the new state's cmd script (and opcode 26 hit
        // status) can run on the entry frame even though the transition happened post-Anim.
        // Shine Start (Fox/Falco SpecialLwStart / SpecialAirLwStart) is a decomp-anchored example.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
        // docs/DECOMP_PROC_ORDER.md (prio 1 vs prio 3).
        const uint16_t cur_action = batch->state.action_id[idx];
        const uint8_t is_shine_start_entry =
            (cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
             cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START)
                ? 1u
                : 0u;
        const uint8_t is_passive_tech_entry = (cur_action == (uint16_t)MSL_ACT_PASSIVE ||
                                               cur_action == (uint16_t)MSL_ACT_PASSIVE_STAND_F ||
                                               cur_action == (uint16_t)MSL_ACT_PASSIVE_STAND_B)
                                                  ? 1u
                                                  : 0u;
        // Passive / PassiveStand entry ownership:
        // - ftCo_80090184 resolves grounded tech callbacks before the post-frame snapshot.
        // - Replay-visible entry frame 0 already carries the new motion state's hurt-status table
        //   on these tech entries, so do not defer the frame-0 table override here.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_800987D0
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_800989D4
        // data/hurtbox_states/{fox,falco}.bin
        if (!(frame == 0u && batch->state.prev_action_id[idx] != cur_action &&
              !is_shine_start_entry && !is_passive_tech_entry)) {
          final_hurtbox_state = hit_status;
        }
      }
      batch->state.hurtbox_state[idx] = final_hurtbox_state;

      const MslHurtCap* caps = NULL;
      uint16_t cap_count_u16 = 0;
      if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL || cap_count_u16 == 0) {
        continue;
      }
      uint16_t cap_count = cap_count_u16;
      if (cap_count > (uint16_t)MSL_MAX_HURTCAPS) {
        cap_count = (uint16_t)MSL_MAX_HURTCAPS;
      }

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];

      // NOTE (scaling): Vanilla applies a per-fighter model scale factor (fp->x34_scale.y) to
      // hurt capsule derived quantities.
      //
      // - Radius: ftCo_800A0DA4 uses `scale = hurt->capsule.scale * fp->x34_scale.y`.
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c::ftCo_800A0DA4
      //
      // - Endpoints: In-engine capsule endpoints are computed via lb_8000B1CC against the bone's
      //   joint matrix. Since fp->x34_scale is applied at the model level, this scaling is baked
      //   into the runtime joint matrices. Our SSANIM01 pose matrices are extracted without that
      //   runtime fighter-scale, so we apply the same scalar uniformly to the pose-space endpoints
      //   before adding world translation.
      //
      // Facing parity: In-engine joint matrices are fighter-facing dependent because the fighter's
      // root part is rotated about Y by +/-90° based on `fp->facing_dir`, and lb_8000B1CC consumes
      // that runtime joint matrix when producing world endpoints.
      // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
      // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
      //
      // Our SSANIM pose matrices are extracted in a single canonical orientation without that
      // runtime facing rotation, so apply the same decomp-shaped Y rotation here (mixing X/Z).
      //
      // We intentionally use only the y component (as decomp does for collision/bounds), treating
      // it as a uniform scalar for x/y/z here.
      const float scale_y = batch->state.fighter_scale_y[idx];
      // Decomp: runtime joint matrices include ftCommon_GetModelScale(fp) (co_attrs.model_scaling)
      // in addition to fp->x34_scale.y. Our SSANIM pose matrices are extracted without those runtime
      // scalars, so apply model_scaling here alongside fighter_scale_y.
      // refs/melee/src/melee/ft/ftparts.c::ftParts_80074B8C (uses ftCommon_GetModelScale)
      const MslCharParams* chp = msl_char_params(char_id);
      const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                      ? chp->model_scaling
                                      : 1.0f;
      const float model_scale = scale_y * model_scaling;
      float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      if (action_id == (uint16_t)MSL_ACT_TURN && batch->state.turn_has_turned[idx] != 0u) {
        // Standing Turn has an internal facing owner that can lead the replay-visible facing lane.
        // ftCo_Turn_Enter records `facing_after = -fp->facing_dir`; ftCo_Turn_Anim_Inner flips
        // `fp->facing_dir` and sets `has_turned` once `frames_to_turn` has elapsed. BODY
        // collision consumes the runtime joint matrices via lb_8000B1CC, so hurtcap world space
        // must follow that internal `has_turned` orientation, not the stale visible facing byte.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
        //   ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
        // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
        facing_dir = -facing_dir;
      }
      // Fallback policy: missing pose data for a specific capsule only drops that capsule, keeping
      // the rest usable under partial animation coverage.
      //
      // Ordering policy:
      // - Preserve init-table capsule ordering/identity (slot i corresponds to `caps[i]`).
      // - Disabled/intangible capsules (movescript) and capsules with missing pose data are kept in
      //   their original slot but marked `hurtcap_enabled=0` and given radius=0.
      uint32_t can_hit_mask = 0xFFFFFFFFu;
      (void)hurtbox_modes_can_hit_mask(char_id, msid, frame, cap_count, &can_hit_mask);
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        const size_t hi = idx_hurtcap(bi, p, (int)ci);
        batch->state.hurtcap_is_grabbable[hi] = caps[ci].is_grabbable ? 1 : 0;
        batch->state.hurtcap_height[hi] = caps[ci].height;

        const uint8_t can_body_hit = (uint8_t)((can_hit_mask >> ci) & 0x1u);
        if (!can_body_hit && !caps[ci].is_grabbable) {
          continue;
        }
        float m[12];
        if (anim_pose_get_matrix(char_id, msid, frame, caps[ci].bone_part_id, m) != 0) {
          continue;
        }

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        msl_mtx34_mul_point(m, caps[ci].a_offset, &ax, &ay, &az);
        msl_mtx34_mul_point(m, caps[ci].b_offset, &bx, &by, &bz);

        ax *= model_scale;
        ay *= model_scale;
        az *= model_scale;
        bx *= model_scale;
        by *= model_scale;
        bz *= model_scale;
        (void)hurtboxes_apply_specialhi_local_xrotn(batch, idx, char_id, msid, frame,
                                                    caps[ci].bone_part_id, facing_dir, model_scale,
                                                    &ax, &ay, &az);
        (void)hurtboxes_apply_specialhi_local_xrotn(batch, idx, char_id, msid, frame,
                                                    caps[ci].bone_part_id, facing_dir, model_scale,
                                                    &bx, &by, &bz);

        // Decomp: apply root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z.
        // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
        const float ax_rot_x = facing_dir * az;
        const float ax_rot_z = -facing_dir * ax;
        const float bx_rot_x = facing_dir * bz;
        const float bx_rot_z = -facing_dir * bx;
        ax = ax_rot_x;
        az = ax_rot_z;
        bx = bx_rot_x;
        bz = bx_rot_z;

        ax += pos_x;
        ay += pos_y;
        az += pos_z;
        bx += pos_x;
        by += pos_y;
        bz += pos_z;

        // Keep BODY-hit enablement separate from catch/grab geometry:
        // - BODY selection consumes `hurtcap_enabled`, which mirrors movescript hurtbox mode.
        // - Catch selection in ftColl_80078A2C gates on `hurt_capsules[j].is_grabbable` after the
        //   fighter-wide x1988/x198C/victim-mask checks; it does not use the body-hit capsule mask.
        // Populate grabbable capsule world positions even when `can_hit_mask` disables BODY hits,
        // so grabs can still connect against shield/guard victims.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        batch->state.hurtcap_enabled[hi] = can_body_hit;
        batch->state.hurtcap_a_x[hi] = ax;
        batch->state.hurtcap_a_y[hi] = ay;
        batch->state.hurtcap_a_z[hi] = az;
        batch->state.hurtcap_b_x[hi] = bx;
        batch->state.hurtcap_b_y[hi] = by;
        batch->state.hurtcap_b_z[hi] = bz;
        batch->state.hurtcap_radius[hi] = caps[ci].scale * model_scale;
      }

      batch->state.hurtcap_count[idx] = (uint8_t)cap_count;
    }
  }
}
