#include "throw_flow.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "combat.h"
#include "move_tables.h"

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

static inline void throw_flow_bridge_integrate_deferred_throw_hit_position(MslBatch* batch,
                                                                           size_t owner_idx,
                                                                           size_t victim_idx) {
  if (batch == NULL) {
    return;
  }

  const uint8_t on_ground = batch->state.on_ground[victim_idx] ? 1u : 0u;
  const float vx_self =
      on_ground ? batch->state.speed_ground_x_self[victim_idx] : batch->state.speed_air_x_self[victim_idx];
  if (on_ground) {
    // Keep self_vel.x synced with grounded integration velocity, matching ftCommon_ApplyGroundMovement.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
    batch->state.speed_air_x_self[victim_idx] = vx_self;
  }

  const float vy_self = batch->state.speed_y_self[victim_idx];
  const float vx = vx_self + batch->state.speed_x_attack[victim_idx];
  const float vy = vy_self + batch->state.speed_y_attack[victim_idx];
  float owner_dx = 0.0f;
  if (owner_idx != victim_idx) {
    const uint8_t owner_on_ground = batch->state.on_ground[owner_idx] ? 1u : 0u;
    owner_dx = owner_on_ground ? batch->state.speed_ground_x_self[owner_idx]
                               : batch->state.speed_air_x_self[owner_idx];
  }

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
  // Release-position bridge (narrow x-lane ownership):
  // - ftCo_800DDDE4 resolves thrown-release world position from throw-side joints after the
  //   thrower's own motion update for the frame.
  // - This simulator defers release/hit apply to post-items; carry the thrower displacement once
  //   so deferred rows don't drop that owner-motion term on pos_x.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  batch->state.pos_x[victim_idx] += vx + owner_dx;
  batch->state.pos_y[victim_idx] += vy;
}

static inline float throw_flow_owner_self_dx(const MslBatch* batch, size_t owner_idx,
                                             size_t victim_idx) {
  if (batch == NULL || owner_idx == victim_idx) {
    return 0.0f;
  }
  const uint8_t owner_on_ground = batch->state.on_ground[owner_idx] ? 1u : 0u;
  return owner_on_ground ? batch->state.speed_ground_x_self[owner_idx]
                         : batch->state.speed_air_x_self[owner_idx];
}

static inline float throw_flow_owner_throwf_deferred_extra_share(const MslBatch* batch,
                                                                 uint8_t owner_char,
                                                                 uint16_t throw_action,
                                                                 float owner_prev_af, float owner_af,
                                                                 size_t owner_idx) {
  if (batch == NULL) {
    return 0.0f;
  }
  // Decomp/timebase-backed split for deferred ThrowF release apply:
  // - Throw release timing is owned by set_throw_flags(0) in ThrowF script events.
  // - Decomp consumes that release in ThrowF Anim before Phys integration; this sim defers
  //   throw-hit apply to post-items, so we carry a bounded owner-motion share tied to:
  //   (a) overspeed contribution from thrower anim-rate, and
  //   (b) post-release fraction within the current (prev_af -> cur_af) step.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Anim
  // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4 (case 0 set_throw_flags)
  // refs/data/moves/{fox,falco}.json moves["ftCo_SM_ThrowF"]["events"] set_throw_flags
  const float frame_speed_mul = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[owner_idx]);
  if (!(frame_speed_mul > 1.0f)) {
    return 0.0f;
  }
  const float overspeed_share = frame_speed_mul - 1.0f;
  float release_af = 0.0f;
  if (!move_tables_throw_release_frame(owner_char, throw_action, &release_af)) {
    return overspeed_share;
  }
  const float span = owner_af - owner_prev_af;
  if (!(span > 0.0f)) {
    return overspeed_share;
  }
  const float post_release = owner_af - release_af;
  if (!(post_release > 0.0f)) {
    return 0.0f;
  }
  float release_share = post_release / span;
  if (release_share < 0.0f) {
    release_share = 0.0f;
  } else if (release_share > 1.0f) {
    release_share = 1.0f;
  }
  // Weight is derived from anim-rate itself (0 at 1.0x; approaches 1 as rate increases).
  const float weight = 1.0f - (1.0f / frame_speed_mul);
  return overspeed_share + (release_share - overspeed_share) * weight;
}

static inline void throw_flow_deferred_throwhi_owner_before_victim_hitstun_tick(MslBatch* batch,
                                                                                 size_t victim_idx) {
  if (batch == NULL) {
    return;
  }
  // Deferred ThrowHi release apply ownership bridge:
  // - In decomp, ThrowHi release/hit consume runs in the thrower's Anim callback
  //   (ftCo_ThrowHi_Anim -> ftCo_800DD724 -> ftCo_800DE7C0/ftCo_800DDDE4).
  // - Under owner-before-victim callback order, the victim can then execute Damage* callback work
  //   in the same Fighter_8006A360 pass; hitstun decrement ownership is in ftCo_8008F744.
  // - This simulator applies throw-hit in post-items, after timers_update_post_anim(), so the
  //   owner-before-victim ThrowHi lane needs one deferred hitstun tick to preserve callback order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}
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

          // Detach immediately. Defer the throw hit to post-items.
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
      // do not apply the throw hit.
      if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_FALL) {
        continue;
      }

      const uint8_t applied = combat_apply_throw_hit(batch, bi, owner_p, (int)victim_p, &p);
      if (!applied) {
        // Invincible/intangible suppression: the victim stays in FALL (already detached).
        // (No additional transition needed.)
      } else {
        // Throw-release damage-entry anim advance ownership:
        // - ftCo_8008DCE0 always performs immediate ftAnim_8006EBA4 on Damage* entry.
        // - Post-items deferred throw-hit apply in this sim needs a deferred extra tick for the
        //   Throw{Hi,Lw} lanes; applying it broadly over-advances other throw-release families.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_ThrowLw_Anim}
        //
        // TODO(narrowed_temporary): subset is intentionally limited to ThrowHi/Lw. Full parity needs
        // explicit throw-release callback/tick ownership data for ThrowF/ThrowB in this deferred
        // post-items apply architecture (instead of broad extra-tick policy).
        if ((throw_action == (uint16_t)MSL_ACT_THROW_HI ||
             throw_action == (uint16_t)MSL_ACT_THROW_LW) &&
            owner_p < (int)victim_p) {
          // Owner-before-victim callback-order bridge (deferred throw-hit apply only):
          // - In decomp, throw release/hit is consumed in the thrower's Anim callback, and victim
          //   Anim callback execution order in the same frame depends on Fighter_procUpdate order.
          // - This simulator defers throw-hit apply to post-items; only the owner-before-victim
          //   subset needs one deferred victim tick to preserve same-frame callback ownership.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_ThrowLw_Anim}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          msl_anim_timebase_defer_tick_once(batch, vidx);
          if (throw_action == (uint16_t)MSL_ACT_THROW_HI) {
            throw_flow_deferred_throwhi_owner_before_victim_hitstun_tick(batch, vidx);
          }
        }
        throw_flow_bridge_integrate_deferred_throw_hit_position(batch, oidx, vidx);
        if (throw_action == (uint16_t)MSL_ACT_THROW_F) {
          // ThrowF release-position ownership (deferred apply bridge):
          // - ftCo_800DDDE4 resolves thrown release position from thrower-side joints after thrower
          //   motion callback work for the frame.
          // - In this simulator, throw-hit apply is deferred to post-items; keep one extra owner
          //   root-motion term on ThrowF release rows where no deferred anim tick is consumed.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Anim
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          // Deferred ThrowF ownership split bridge:
          // - Carry only the owner-motion overspeed share derived from thrower anim timebase.
          const int32_t owner_prev_fp =
              batch->state.anim_frame_fp_q16_16[oidx] - batch->state.frame_speed_mul_fp_q16_16[oidx];
          const float owner_prev_af = msl_f32_from_q16_16(owner_prev_fp);
          const float owner_af = batch->state.anim_frame_f32[oidx];
          const float extra_owner_share = throw_flow_owner_throwf_deferred_extra_share(
              batch, owner_char, throw_action, owner_prev_af, owner_af, oidx);
          batch->state.pos_x[vidx] += extra_owner_share * throw_flow_owner_self_dx(batch, oidx, vidx);
        }
      }
    }
  }
}
