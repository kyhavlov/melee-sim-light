#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Hidden CollData.floor_skip lifetime helpers.
//
// Source shape:
// - mpUpdateFloorSkip stores CollData.floor.index.
// - mpClearFloorSkip stores -1.
// Runtime stores source stage segment ids, with 0xFFFF as the simulator sentinel for -1.
// refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip}
static inline void msl_mpcoll_update_floor_skip(MslBatch* batch, size_t idx,
                                                uint16_t floor_segment_id) {
  if (batch == NULL || batch->state.floor_skip_segment_id == NULL) {
    return;
  }
  batch->state.floor_skip_segment_id[idx] = floor_segment_id;
}

static inline void msl_mpcoll_clear_floor_skip(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.floor_skip_segment_id == NULL) {
    return;
  }
  batch->state.floor_skip_segment_id[idx] = 0xFFFFu;
}
