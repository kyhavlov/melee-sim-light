#include "step.h"

#include <errno.h>

#include "action.h"
#include "combat.h"
#include "hitboxes.h"
#include "hurtboxes.h"
#include "input.h"
#include "items.h"
#include "locomotion.h"
#include "physics.h"
#include "shields.h"
#include "stage_collision.h"
#include "timers.h"

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  //
  // Decomp ordering note:
  // - refs/melee/src/melee/ft/fighter.c registers `Fighter_8006A1BC` (timer decrement) at proc prio 0,
  //   before input processing, animation advancement, and physics (`Fighter_procUpdate`).
  // We follow that by updating timers before applying inputs/advancing state.
  timers_update(batch);

  // Decomp-shaped "ProcessHit" consume / cleanup (see combat_processhit_consume for references).
  combat_processhit_consume(batch);

  int err = 0;
  err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                    input_stride_bytes);
  if (err != 0) {
    return err;
  }

  action_update(batch);
  physics_integrate(batch);
  stage_collision_apply(batch);
  locomotion_update_post_collision(batch);
  hurtboxes_refresh(batch);
  hitboxes_refresh(batch);
  shields_refresh(batch);
  combat_resolve(batch);
  items_update(batch);

  return 0;
}
