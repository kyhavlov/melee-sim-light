#include "step.h"

#include <errno.h>

#include "action.h"
#include "combat.h"
#include "hurtboxes.h"
#include "input.h"
#include "items.h"
#include "physics.h"
#include "stage_collision.h"

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }

  // The exact ordering here is a major correctness lever. Keep it explicit and easy to reorder.
  int err = 0;
  err = input_apply(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                    input_stride_bytes);
  if (err != 0) {
    return err;
  }

  action_update(batch);
  physics_integrate(batch);
  stage_collision_apply(batch);
  hurtboxes_refresh(batch);
  combat_resolve(batch);
  items_update(batch);

  return 0;
}
