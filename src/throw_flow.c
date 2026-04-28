#include "throw_flow.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "common_params.h"
#include "combat.h"
#include "grab_attachment.h"
#include "knockdown.h"
#include "move_tables.h"

#include <math.h>

static inline uint8_t is_thrower_action(uint16_t a) {
  switch (a) {
    case (uint16_t)MSL_ACT_THROW_F:
    case (uint16_t)MSL_ACT_THROW_B:
    case (uint16_t)MSL_ACT_THROW_HI:
    case (uint16_t)MSL_ACT_THROW_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t throw_flow_action_is_damage_family(uint16_t action_id_u16) {
  switch (action_id_u16) {
    case MSL_ACT_DAMAGE_FALL:
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

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
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_fall_release(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: generic fall entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  //
  // Simulator note:
  // - This helper is only used as a detached-victim fallback bridge in throw-release paths.
  // - Use a pure timebase restart (no motion-identity side effects) so this bridge state does not
  //   consume an extra instance_id bump before deferred throw-hit resolution.
  batch->state.on_ground[idx] = 0;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_restart(batch, idx, 0.0f, 1.0f);
}

static inline void throw_flow_apply_deferred_throw_hit_kb_decay(MslBatch* batch,
                                                                size_t victim_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  float kb_x = batch->state.speed_x_attack[victim_idx];
  float kb_y = batch->state.speed_y_attack[victim_idx];
  if (kb_x == 0.0f && kb_y == 0.0f) {
    return;
  }

  // Deferred throw-hit scheduling bridge:
  // - In decomp, ftCo_800DD724 applies the throw hit during the thrower's Anim callback.
  // - The victim then reaches Fighter_procUpdate in the same frame, where airborne knockback velocity
  //   is decayed before position integration.
  // - This simulator defers throw-hit application until after the normal physics pass, so mirror that
  //   one Fighter_procUpdate knockback-decay step before the bridge integrates release displacement.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  const float kb_mag = sqrtf(kb_x * kb_x + kb_y * kb_y);
  const float decay = c->knockback_frame_decay;
  if (kb_mag < decay) {
    kb_x = 0.0f;
    kb_y = 0.0f;
  } else {
    const float kb_angle = atan2f(kb_y, kb_x);
    kb_x -= decay * cosf(kb_angle);
    kb_y -= decay * sinf(kb_angle);
  }
  batch->state.speed_x_attack[victim_idx] = kb_x;
  batch->state.speed_y_attack[victim_idx] = kb_y;
}

static inline void throw_flow_bridge_integrate_deferred_throw_hit_position(
    MslBatch* batch, size_t owner_idx, size_t victim_idx, uint8_t apply_damage_phys_step) {
  if (batch == NULL) {
    return;
  }

  const uint8_t on_ground = batch->state.on_ground[victim_idx] ? 1u : 0u;
  const float vx_self = on_ground ? batch->state.speed_ground_x_self[victim_idx]
                                  : batch->state.speed_air_x_self[victim_idx];
  if (on_ground) {
    // Keep self_vel.x synced with grounded integration velocity, matching ftCommon_ApplyGroundMovement.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
    batch->state.speed_air_x_self[victim_idx] = vx_self;
  }
  if (apply_damage_phys_step && !on_ground &&
      throw_flow_action_is_damage_family(batch->state.action_id[victim_idx])) {
    throw_flow_apply_deferred_throw_hit_kb_decay(batch, victim_idx);
    // Deferred throw-release damage entry happens after this simulator's normal physics pass, but in
    // decomp the release hit is consumed before the victim's Damage* Phys owner runs. Apply the
    // one-frame airborne gravity owner before integration so the current displacement observes the
    // Damage Phys self velocity.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Phys,ftCo_Damage_Phys}
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    const MslCharParams* ch = msl_char_params(batch->state.char_id[victim_idx]);
    if (ch != NULL) {
      float next_vy = batch->state.speed_y_self[victim_idx] - ch->grav;
      if (next_vy < -ch->terminal_vel) {
        next_vy = -ch->terminal_vel;
      }
      batch->state.speed_y_self[victim_idx] = next_vy;
    }
  }

  const float vy_self = batch->state.speed_y_self[victim_idx];
  const float vx = vx_self + batch->state.speed_x_attack[victim_idx];
  const float vy = vy_self + batch->state.speed_y_attack[victim_idx];
  (void)owner_idx;

  // Throw release/hit ordering bridge:
  // - In decomp, set_throw_flags(0) consume + throw-hit application (ftCo_800DE2A8/ftCo_800DE7C0)
  //   occurs in Throw Anim callback before Fighter_procUpdate Phys integration.
  // - This simulator defers throw-hit apply to post-items to preserve item-preemption ordering;
  //   apply one immediate position integration here so throw-hit velocities displace in-frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DE2A8
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // Release-position ownership:
  // - ftCo_800DD724 consumes set_throw_flags(0) in the thrower's Anim callback, and ftCo_800DDDE4
  //   samples the throw-side joint immediately there before the thrower's Phys integration.
  // - This simulator already applies the same-frame release anchor in throw_flow_update_pre_physics()
  //   before deferred throw-hit damage is applied post-items; do not add the thrower's self-velocity
  //   again here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  batch->state.pos_x[victim_idx] += vx;
  batch->state.pos_y[victim_idx] += vy;
}

static inline void throw_flow_apply_post_release_damage_callback_phase(MslBatch* batch,
                                                                       size_t victim_idx);

static inline float throw_flow_owner_self_dx(const MslBatch* batch, size_t owner_idx,
                                             size_t victim_idx) {
  if (batch == NULL || owner_idx == victim_idx) {
    return 0.0f;
  }
  const uint8_t owner_on_ground = batch->state.on_ground[owner_idx] ? 1u : 0u;
  return owner_on_ground ? batch->state.speed_ground_x_self[owner_idx]
                         : batch->state.speed_air_x_self[owner_idx];
}

void throw_flow_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Decomp ordering notes:
  // - `anim_timebase_update_pre_input()` runs before action_update() in step_one_frame().
  // - In decomp, movescript opcodes are processed during animation advancement, and the motion
  //   state's Anim callback consumes resulting throw flags (e.g. flip facing) before Phys/Coll.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 and Fighter_procUpdate
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw*_Anim
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int owner_p = 0; owner_p < num_players; owner_p++) {
      const size_t oidx = msl_idx_player(bi, owner_p);
      // Internal per-frame latch: clear by iterating over throwers each frame.
      batch->state.throw_pending_victim_port[oidx] = 0xFFu;
      batch->state.throw_pending_hit_idx[oidx] = 0xFFu;
      if (batch->state.stocks[oidx] == 0) {
        continue;
      }

      const uint16_t owner_act = batch->state.action_id[oidx];
      if (!is_thrower_action(owner_act)) {
        continue;
      }

      // Decomp: hitlag freezes Anim callback execution (Fighter_procUpdate does not run motion state
      // Anim/Phys/Coll callbacks while fp->x2219_b5 is set).
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      //
      // Without this guard, single-frame set_throw_flags events (notably facing flip) would be
      // re-applied every frame while anim_frame_f32 is frozen in hitlag.
      if (batch->state.hitlag_started_frame[oidx] != 0) {
        continue;
      }

      const uint8_t owner_pose_facing_before_throw_flags = batch->state.facing[oidx];
      const uint8_t owner_char = batch->state.char_id[oidx];
      const float owner_af = batch->state.anim_frame_f32[oidx];
      const int32_t owner_prev_fp =
          batch->state.anim_frame_fp_q16_16[oidx] - batch->state.frame_speed_mul_fp_q16_16[oidx];
      const float owner_prev_af = msl_f32_from_q16_16(owner_prev_fp);

      // Facing flip: set_throw_flags(hit_idx=1) -> throw_flags_b4 -> facing_dir = -facing_dir.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4 (case 1)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724 (inline1)
      if (move_tables_throw_should_flip_facing(owner_char, owner_act, owner_prev_af, owner_af)) {
        batch->state.facing[oidx] = (uint8_t)!batch->state.facing[oidx];
      }

      // Release/detach (set_throw_flags hit_idx=0):
      // - Detach the victim immediately (Anim-callback timing; before Phys/Coll).
      // - Defer the throw hit application until after items_update() so same-frame item hits can
      //   preempt the throw hit when item procs run earlier in the decomp schedule.
      //
      // Decomp shape:
      // - Throw Anim consumes throw_flags_b3, clears it, and calls ftCo_800DE2A8 / ftCo_800DE7C0 on
      //   the victim (release/detach) before Phys/Coll.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      uint8_t rel_hit_idx = 0xFFu;
      float rel_anim_frame = owner_af;
      const uint8_t released_prev =
          move_tables_throw_release_hit_idx(owner_char, owner_act, owner_prev_af, NULL);
      const uint8_t released_cur =
          move_tables_throw_release_hit_idx(owner_char, owner_act, owner_af, &rel_hit_idx);
      if (released_cur && !released_prev) {
        // Find the grabbed victim owned by this thrower (decomp: fp->victim_gobj is a single
        // pointer). If a malformed seed contains multiple victims with the same grab_owner_port,
        // choose the lowest port deterministically and process only that one.
        for (int victim_p = 0; victim_p < num_players; victim_p++) {
          if (victim_p == owner_p) {
            continue;
          }
          const size_t vidx = msl_idx_player(bi, victim_p);
          if (batch->state.stocks[vidx] == 0) {
            continue;
          }
          if (batch->state.grab_owner_port[vidx] != (uint8_t)owner_p) {
            continue;
          }
          if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
            // Defensive: if the victim is no longer in a grabbed-victim state, still clear the
            // attachment link so grab_attachment doesn't keep driving them next frame.
            batch->state.grab_owner_port[vidx] = 0xFFu;
            break;
          }

          // Common release same-frame attachment ownership:
          // - ftCo_800DD724 is the shared ThrowF/B/Hi/Lw release consume path.
          // - ftCo_800DDDE4 / ftCo_800DE508 still own the attached victim world placement for the
          //   current frame before detach and later damage entry.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
          grab_attachment_apply_thrown_release_anchor_now(
              batch, bi, victim_p, owner_p, rel_anim_frame, owner_pose_facing_before_throw_flags);

          // Detach immediately. Defer the throw hit to post-items.
          batch->state.attached_victim_port[oidx] = 0xFFu;
          batch->state.grab_owner_port[vidx] = 0xFFu;
          batch->state.throw_pending_victim_port[oidx] = (uint8_t)victim_p;
          batch->state.throw_pending_hit_idx[oidx] = rel_hit_idx;

          // Defensive broadening: on a detached frame, a grabbed-victim action (Thrown*/Capture*)
          // has no self/KB integration in this sim (Phys callbacks are empty in decomp), so ensure
          // we don't leave the victim in a "no physics, no attachment" freeze window if some other
          // hit suppresses the throw hit later in the frame.
          //
          // Suite expectation: for valid throw scripts, the victim is Thrown* when set_throw_flags(0)
          // fires. The broad fallback is to keep malformed seeds stable.
          enter_fall_release(batch, vidx);
          break;
        }
      }

      const uint32_t owner_sm_u32 = batch->state.animation_index[oidx];
      if (owner_sm_u32 <= 0xFFFFu &&
          throw_anim_finished(owner_char, (uint16_t)owner_sm_u32, owner_af)) {
        enter_wait_or_fall_from_throw_end(batch, oidx);
      }
    }
  }
}

void throw_flow_update_post_items(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Apply the throw hit after items_update() (but latch the release/detach in pre-physics):
  //
  // Decomp scheduling evidence (GALE01):
  // - Item GObj spawn registers multiple per-frame procs including prio 0 and prio 1.
  //   refs/melee/src/melee/it/item.c::Item_8026862C (HSD_GObjProc_8038FD54(..., prio=0/1/...))
  // - Fighter timer decrement is prio 0, and animation advancement is prio 1; motion-state callbacks
  //   (Anim/Phys/Coll) are scheduled later under Fighter_procUpdate.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC (prio 0)
  //   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (prio 1)
  //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // In this light sim, items_update() runs later in step_one_frame() than the fighter throw Anim
  // callback. Deferring the throw hit here is a deterministic approximation to allow same-frame item
  // hits to preempt the throw hit in cases where decomp ordering applies item collision earlier.
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int owner_p = 0; owner_p < num_players; owner_p++) {
      const size_t oidx = msl_idx_player(bi, owner_p);
      if (batch->state.stocks[oidx] == 0) {
        continue;
      }

      const uint8_t victim_p = batch->state.throw_pending_victim_port[oidx];
      const uint8_t hit_idx = batch->state.throw_pending_hit_idx[oidx];
      if (victim_p == 0xFFu || (int)victim_p >= num_players || (int)victim_p == owner_p) {
        continue;
      }
      if (hit_idx == 0xFFu) {
        continue;
      }

      const uint8_t owner_char = batch->state.char_id[oidx];
      uint16_t throw_action = batch->state.action_id[oidx];
      if (!is_thrower_action(throw_action)) {
        // Fallback for deferred throw-hit lookup:
        // - Why needed: in decomp, Throw Anim handles release/throw-script timing and can then exit
        //   the throw state on the same frame (`ftCo_Throw*_Anim` does `ftCo_800DD724` then
        //   `ftCommon_8007D92C` on anim end).
        //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw{F,B,Hi,Lw}_Anim
        // - This simulator defers throw-hit application to post-items so same-frame item hits can
        //   preempt throw hits; scheduling anchor is item procs before fighter motion callbacks in
        //   decomp (`Item_8026862C` proc registration vs fighter proc chain).
        //   refs/melee/src/melee/it/item.c::Item_8026862C
        //   refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_procUpdate}
        // - Safety/determinism: only recover from frame-start action_id when it is strictly one of
        //   Throw{F,B,Hi,Lw}. If the thrower was interrupted/canceled into a non-throw state,
        //   fallback is disabled and no throw-hit is applied.
        const uint16_t prev_act = batch->state.prev_action_id[oidx];
        if (!is_thrower_action(prev_act)) {
          continue;
        }
        throw_action = prev_act;
      }

      MslThrowHitboxParams p = {0};
      if (!move_tables_throw_hitbox_params(owner_char, throw_action, hit_idx, &p)) {
        continue;
      }

      const size_t vidx = msl_idx_player(bi, (int)victim_p);
      if (batch->state.stocks[vidx] == 0) {
        continue;
      }

      // If the victim transitioned out of the release state earlier in the frame (e.g. item hit),
      // do not apply the deferred throw hit.
      if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_FALL) {
        continue;
      }
      const uint8_t applied = combat_apply_throw_hit(batch, bi, owner_p, (int)victim_p, &p);
      if (!applied) {
        // Invincible/intangible suppression: the victim stays in FALL (already detached).
        // (No additional transition needed.)
      } else {
        // Throw-release post-damage callback phase:
        // - ftCo_800DD724 consumes release/hit in the thrower's Anim callback.
        // - If owner callback order precedes the victim and the victim enters Damage* this frame,
        //   decomp still gives the victim its same-frame Damage callback work after entry.
        // - This simulator applies throw-hit in post-items; restore the equivalent post-release
        //   Damage callback phase here instead of keeping ThrowHi-specific bridge logic.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_ThrowLw_Anim,ftCo_800DD724}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008F744}
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
        const uint8_t run_post_release_damage_callback =
            (owner_p < (int)victim_p &&
             throw_flow_action_is_damage_family(batch->state.action_id[vidx]))
                ? 1u
                : 0u;
        const uint8_t run_post_release_damage_phys =
            throw_flow_action_is_damage_family(batch->state.action_id[vidx]) ? 1u : 0u;
        if (run_post_release_damage_callback) {
          msl_anim_timebase_defer_tick_once(batch, vidx);
          throw_flow_apply_post_release_damage_callback_phase(batch, vidx);
        }
        throw_flow_bridge_integrate_deferred_throw_hit_position(batch, oidx, vidx,
                                                                run_post_release_damage_phys);
        if (throw_action == (uint16_t)MSL_ACT_THROW_LW &&
            throw_flow_action_is_damage_family(batch->state.action_id[vidx])) {
          knockdown_try_throw_release_damage_floor_contact(batch, (size_t)bi, vidx,
                                                           (uint16_t)MSL_ACT_DAMAGE_FLY_TOP);
        }
        if (throw_action == (uint16_t)MSL_ACT_THROW_F) {
          // ThrowF release-position ownership:
          // - same-frame owner-anchor placement is already bridged at release consume time
          //   (ftCo_800DE508-style world placement before detach).
          // - Keep the older deferred extra-share term disabled here to avoid double-counting owner
          //   motion on the same release frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
        }
      }
    }
  }
}
static inline void throw_flow_apply_post_release_damage_callback_phase(MslBatch* batch,
                                                                       size_t victim_idx) {
  if (batch == NULL) {
    return;
  }
  // Shared post-release Damage callback phase:
  // - Damage entry from throw release goes through ftCo_8008DCE0, which immediately advances the
  //   new Damage* motion once via ftAnim_8006EBA4.
  // - Under owner-before-victim callback order, the victim's Damage callback work (including the
  //   hitstun decrement in ftCo_8008F744 when not in hitlag) still occurs later in the same
  //   Fighter_8006A360 pass.
  // - This simulator applies release damage in post-items, after timers_update_post_anim(); restore
  //   only the callback-owned work that would have happened after entry on the same frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  if (batch->state.hitlag[victim_idx] != 0u) {
    return;
  }
  const uint16_t hs = batch->state.hitstun[victim_idx];
  if (hs > 0u) {
    batch->state.hitstun[victim_idx] = (uint16_t)(hs - 1u);
  }
}
