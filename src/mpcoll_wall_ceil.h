#pragma once

#include "batch_internal.h"

// mpColl-style wall + ceiling contact substrate (FD-only v1).
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

