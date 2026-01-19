#include "msl_step.h"

#include <errno.h>

#include "msl_pass_action.h"
#include "msl_pass_combat.h"
#include "msl_pass_hurtboxes.h"
#include "msl_pass_input.h"
#include "msl_pass_items.h"
#include "msl_pass_physics.h"
#include "msl_pass_stage_collision.h"

int msl_step_one_frame_v0(
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
  err = msl_pass_input_apply_v0(
      batch,
      prev_input_bytes,
      prev_input_stride_bytes,
      input_bytes,
      input_stride_bytes);
  if (err != 0) {
    return err;
  }

  msl_pass_action_update_v0(batch);
  msl_pass_physics_integrate_v0(batch);
  msl_pass_stage_collision_v0(batch);
  msl_pass_hurtboxes_refresh_v0(batch);
  msl_pass_combat_resolve_v0(batch);
  msl_pass_items_update_v0(batch);

  return 0;
}

