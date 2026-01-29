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
#include "timers.h"
#include "reflector_bubbles.h"

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

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }

  clear_landing_transients(batch);
  cache_prev_action_ids(batch);

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

  // Decomp-shaped "ProcessHit" consume / cleanup (see combat_processhit_consume for references).
  combat_processhit_consume(batch);

  int err = 0;
  err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                    input_stride_bytes);
  if (err != 0) {
    return err;
  }

  // Match flow IASA: certain match-flow states can exit based on current-frame inputs.
  // Decomp: motion state IASA callbacks run after Anim and before Phys/Coll.
  // NOTE: match_flow_update_post_input currently includes simplified approximations (see match_flow.c).
  match_flow_update_post_input(batch);

  action_update(batch);
  physics_integrate(batch);
  // NOTE(grabbed-victim-coll):
  // Grabbed/thrown victims currently still run stage collision before their attachment callback
  // overrides position. Decomp thrown/capture Phys/Coll callbacks are empty; consider skipping
  // stage collision for grabbed victims rather than “collide then override pos”.
  stage_collision_apply(batch);
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
  combat_resolve(batch);

  return 0;
}
