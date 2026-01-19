#include "step.h"

#include <errno.h>

#include "pass_action.h"
#include "pass_combat.h"
#include "pass_hurtboxes.h"
#include "pass_input.h"
#include "pass_items.h"
#include "pass_physics.h"
#include "pass_stage_collision.h"

int step_one_frame(
    MslBatch* batch,
    const uint8_t* prev_input_bytes,
    size_t prev_input_stride_bytes,
    const uint8_t* input_bytes,
    size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  int err = 0;
  err = pass_input_apply(
      batch,
      prev_input_bytes,
      prev_input_stride_bytes,
      input_bytes,
      input_stride_bytes);
  if (err != 0) {
    return err;
  }

  pass_action_update(batch);
  pass_physics_integrate(batch);
  pass_stage_collision(batch);
  pass_hurtboxes_refresh(batch);
  pass_combat_resolve(batch);
  pass_items_update(batch);

  return 0;
}
