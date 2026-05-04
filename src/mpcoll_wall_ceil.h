#pragma once

#include "batch_internal.h"
#include "mpcoll_ecb_points.h"

// mpColl-style wall + ceiling contact substrate.
//
// Owns:
// - state.wall_* (wall_kind, wall_id, contact + normal)
// - state.ceiling_* (ceiling_id, contact + normal)
// - subsets of state.coll_env_flags for wall/ceiling bits
//
// Decomp pointers:
// - Query primitives:
//   - refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
//   - refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
//   - refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
// - Persistent projection helpers:
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
// - Contact storage:
//   - refs/melee/src/melee/lb/types.h::CollData (SurfaceData left_facing_wall/right_facing_wall/ceiling + contact)
//   - refs/melee/src/common_structs.h (Collide_* env flag bit values)
void mpcoll_wall_ceil_apply(MslBatch* batch);

enum {
  MSL_MPCOLL_ORDERED_SQUEEZE_CEILING = 1u,
  MSL_MPCOLL_ORDERED_SQUEEZE_FLOOR = 2u,
  MSL_MPCOLL_ORDERED_SQUEEZE_RIGHT_WALL = 4u,
  MSL_MPCOLL_ORDERED_SQUEEZE_LEFT_WALL = 8u,
};

typedef struct MslMpcollOrderedWallCeilResult {
  uint8_t left_right_flags;  // bit 0 = left wall, bit 1 = right wall
  uint8_t squeeze_flags;     // source bits: ceiling=1, floor=2, right wall=4, left wall=8
  uint8_t squeeze_flags_all;
  uint8_t hit_ceiling;
  uint8_t hit_floor;
  uint8_t touching_floor;
  uint16_t left_wall_id;
  uint16_t right_wall_id;
  float x_after_left_wall;
  float x_after_right_wall;
  float y_after_ceiling;
  float y_after_floor;
  MslEcbWorldPoints cur_ecb_after;
} MslMpcollOrderedWallCeilResult;

// Grounded inline2 / mpColl_8004ACE4 wall and ceiling subpass. This mutates the fighter's
// current position and wall/ceiling contact outputs in source order, before floor resolution.
// It is intentionally separate from the generic airborne wall/ceiling pass so floor collision can
// consume same-frame left/right wall results.
void mpcoll_grounded_wall_ceil_ordered_begin(MslBatch* batch, size_t idx,
                                             const MslEcbWorldPoints* prev_ecb,
                                             const MslEcbWorldPoints* cur_ecb,
                                             MslMpcollOrderedWallCeilResult* out);

// Source retry after a grounded floor hit: mpColl_8004ACE4 runs the ceiling check/collide pair
// again after floor resolution. Returns 1 when the retry touched ceiling.
uint8_t mpcoll_grounded_ceiling_ordered_retry(MslBatch* batch, size_t idx,
                                              const MslEcbWorldPoints* prev_ecb,
                                              MslEcbWorldPoints* cur_ecb,
                                              MslMpcollOrderedWallCeilResult* io);
