#pragma once

#include <stdint.h>

#include "coll_env_flags.h"

typedef struct MslMpcollEndEvents {
  uint8_t floor_callback;
  uint8_t floor_callback_arg;
  uint8_t ceiling_callback;
  uint8_t _pad0;
  uint16_t floor_segment_id;
  uint16_t ceiling_segment_id;
  float dy;
} MslMpcollEndEvents;

static inline void msl_mpcoll_end_static_events(uint16_t floor_segment_id,
                                                uint16_t ceiling_segment_id, uint32_t env_flags,
                                                uint8_t force_floor_callback, uint8_t floor_arg2,
                                                float cur_y, float last_y,
                                                MslMpcollEndEvents* out) {
  if (out == NULL) {
    return;
  }

  // Static/legal-stage finalizer boundary for `mpCollEnd`. Source also refreshes dynamic floor
  // attribute low bits and dispatches Ground callbacks; those dynamic callback effects are outside
  // Phase 2's moving-platform/Ground-object non-scope.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollEnd,mpCollEnd_inline,mpCollEnd_inline2}
  const uint8_t floor_env =
      (uint8_t)(force_floor_callback != 0u ||
                (env_flags & (MSL_COLLIDE_EDGE | MSL_COLLIDE_LEFT_EDGE | MSL_COLLIDE_RIGHT_EDGE)) !=
                    0u);
  const uint8_t ceiling_env = (uint8_t)((env_flags & MSL_COLLIDE_CEILING_MASK) != 0u);
  *out = (MslMpcollEndEvents){
      .floor_callback = (uint8_t)(floor_env && floor_segment_id != 0xFFFFu),
      .floor_callback_arg = floor_arg2 ? 1u : 2u,
      .ceiling_callback = (uint8_t)(ceiling_env && ceiling_segment_id != 0xFFFFu),
      .floor_segment_id = floor_segment_id,
      .ceiling_segment_id = ceiling_segment_id,
      .dy = cur_y - last_y,
  };
}
