#include "step.h"

#include <errno.h>

#include "action.h"
#include "anim_timebase.h"
#include "combat.h"
#include "hitboxes.h"
#include "hitlist.h"
#include "hurtboxes.h"
#include "blaster.h"
#include "grab_attachment.h"
#include "input.h"
#include "items.h"
#include "ledge.h"
#include "locomotion.h"
#include "match_flow.h"
#include "knockdown.h"
#include "physics.h"
#include "shields.h"
#include "shine.h"
#include "stage_collision.h"
#include "mpcoll_env.h"
#include "timers.h"
#include "reflector_bubbles.h"
#include "state_flags.h"
#include "throw_flow.h"

// NOTE: `blaster_update_post_collision` is intentionally not part of the public blaster module API
// yet; keep the forward declaration local to preserve the current include surface.
extern void blaster_update_post_collision(MslBatch* batch);

static inline void clear_landing_transients(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Slippi post-frame `l_cancel` is a 1-frame status on LandingAir* entry. Clear it each frame
      // and let landing entry code set it when applicable.
      batch->state.l_cancel[idx] = 0;
      // Clear per-frame anim-timebase transients.
      batch->state.anim_defer_tick_once[idx] = 0;
    }
  }
}

static inline void cache_prev_action_ids(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.prev_action_id[idx] = batch->state.action_id[idx];
    }
  }
}

static inline void cache_guard_reflect_timer_seed_snapshots(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.guard_reflect_timer_x14_seed[idx] = batch->state.guard_reflect_timer_x14[idx];
      batch->state.guard_reflect_timer_x18_seed[idx] = batch->state.guard_reflect_timer_x18[idx];
    }
  }
}

static inline void cache_collision_stage_prev_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp: the ledge-grab AABB builders (mpColl_80044164 / mpColl_800443C4) consume CollData.prev_pos
  // and CollData.cur_pos as managed inside the collision substep loop (mpColl_80043754).
  //
  // This sim does not yet implement mpColl substeps. Approximate the collision-stage prev/cur pair:
  // - coll_stage_prev_pos: position at start of collision stage this frame
  // - coll_stage_cur_pos: position immediately after stage_collision_apply() this frame
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_prev_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_prev_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static inline void cache_collision_stage_cur_pos(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.coll_stage_cur_pos_x[idx] = batch->state.pos_x[idx];
      batch->state.coll_stage_cur_pos_y[idx] = batch->state.pos_y[idx];
    }
  }
}

static int step_one_frame_core(MslBatch* batch, const uint8_t* prev_input_bytes,
                               size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                               size_t input_stride_bytes, uint8_t run_combat) {
  if (batch == NULL) {
    return EINVAL;
  }

  combat_rng_trace_begin_frame(batch);

  clear_landing_transients(batch);
  cache_prev_action_ids(batch);
  cache_guard_reflect_timer_seed_snapshots(batch);

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  //
  // Decomp ordering note:
  // - refs/melee/src/melee/ft/fighter.c registers `Fighter_8006A1BC` (timer decrement) at proc prio 0,
  //   before input processing, animation advancement, and physics (`Fighter_procUpdate`).
  // We follow that by updating timers before applying inputs/advancing state.
  timers_update(batch);

  // Match flow (Entry/Death/Rebirth) updates its own timers and can change action state before
  // animation timebase advancement.
  match_flow_update_pre_anim(batch);

  // Decomp: animation/script timebase advances at proc prio 1 (Fighter_8006A360) before input
  // (prio 3). Advance our deterministic cur_anim_frame accumulator here, after timers.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (ftAnim_8006EBA4 under !hitlag)
  anim_timebase_update_pre_input(batch);

  // Post anim-timebase match flow clamps (e.g. EntryStart animation end-frame).
  match_flow_update_post_anim(batch);

  // Post-anim timer updates (decomp prio 1 under Fighter_8006A360's non-hitlag gate):
  // - combo timer tick + victim clear (ftColl_800764DC)
  // - hitstun decrement + end effects (ftCo_8008F744 family)
  //
  // NOTE(anim_timebase_mapping):
  // `anim_timebase_update_pre_input()` in this sim only advances the deterministic `cur_anim_frame`
  // timebase (Slippi `state_age`) and applies AObj loop wrap. It does *not* run per-action Anim
  // callbacks. Those state-specific updates are modeled later in `action_update()`.
  //
  // This placement means timers_update_post_anim() observes the post-advance `action_frame`, while
  // still running before action_update(), matching the decomp ordering where ftColl_800764DC runs
  // before the per-action anim_cb within Fighter_8006A360.
  timers_update_post_anim(batch);

  // Per-action Anim-callback phase (subset) before input processing.
  // Decomp: Fighter_8006A360 (prio 1) runs anim callbacks before Fighter_procUpdate input_cb (prio 3).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  //
  // Ordering rationale vs ProcessHit consume:
  // - This phase currently owns GuardReflect x14/x18 timer tick/expire (ftCo_GuardReflect_Anim path),
  //   which is a prio-1 anim callback update.
  // - combat_processhit_consume() models the post-collision ProcessHit-style cleanup (decomp prio 14),
  //   so it should remain after prio-1 callback effects.
  action_update_anim_callbacks_pre_input(batch);

  // Decomp-shaped "ProcessHit" consume / cleanup (see combat_processhit_consume for references).
  combat_processhit_consume(batch);

  int err = 0;
  err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                    input_stride_bytes);
  if (err != 0) {
    combat_rng_trace_end_frame(batch);
    return err;
  }

  // Damage post-hitlag callback consume (subset) after input apply and before collision ownership.
  // Decomp:
  // - hitlag exit invokes `post_hitlag_cb` (Fighter_8006D10C),
  // - damage path uses ftCo_Damage_OnExitHitlag.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
  timers_consume_post_hitlag_callbacks_after_input(batch);

  // Match flow IASA: certain match-flow states can exit based on current-frame inputs.
  // Decomp: motion state IASA callbacks run after Anim and before Phys/Coll.
  // NOTE: match_flow_update_post_input currently includes simplified approximations (see match_flow.c).
  match_flow_update_post_input(batch);

  action_update(batch);
  // Fighter-driven item spawns (blaster guns + shots) are evaluated before physics integration so
  // they use the pre-physics fighter pose/position snapshot (decomp: prio1 Anim vs prio4 Update).
  items_spawn_pre_physics(batch);
  physics_integrate(batch);
  // NOTE(grabbed-victim-coll):
  // - CapturePulled*/CaptureDamage* uses a Phys position driver (fn_800DAD18) before Coll.
  // - In this simulator, the capture delta is applied in grab_attachment_update_pre_collision()
  //   (pre-collision). physics_integrate() skips self/KB integration for those victims to avoid
  //   double-moving them in a single frame.
  // - Thrown victims are still updated in a post-collision "accessory callback" style slot.
  grab_attachment_update_pre_collision(batch);
  cache_collision_stage_prev_pos(batch);
  stage_collision_apply(batch);
  cache_collision_stage_cur_pos(batch);
  // Collision environment flags (mpColl-shaped): owns Collide_LedgeGrabMask for scheduling ledge
  // catch after collision.
  mpcoll_env_update_ledge_grab(batch);
  grab_attachment_update_post_collision(batch);
  ledge_try_catch_post_collision(batch);
  knockdown_update_post_collision(batch);
  match_flow_update_post_physics(batch);
  blaster_update_post_collision(batch);
  locomotion_update_post_collision(batch);
  shine_update_post_collision(batch);
  hurtboxes_refresh(batch);
  hitboxes_refresh(batch);
  hitlist_tick(batch);
  shields_refresh(batch);
  reflector_bubbles_refresh(batch);
  items_update(batch);
  throw_flow_update_post_items(batch);
  if (run_combat) {
    combat_resolve(batch);
  }
  knockdown_update_post_combat(batch);
  // Decomp parity: some entries call ftAnim_8006EBA4 immediately after ChangeMotionState; we defer
  // to post-combat to match action_frame/state_age without perturbing pre-combat/combat geometry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
  anim_timebase_apply_deferred_tick_once_post_combat(batch);
  state_flags_refresh_post_frame(batch);

  combat_rng_trace_end_frame(batch);
  return 0;
}

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes) {
  return step_one_frame_core(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                             input_stride_bytes, 1u);
}

int step_one_frame_pre_combat(MslBatch* batch, const uint8_t* prev_input_bytes,
                              size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                              size_t input_stride_bytes) {
  return step_one_frame_core(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                             input_stride_bytes, 0u);
}
