#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// mpColl-adjacent collision environment helpers (FD-only v1).
//
// This module is allocation-free and deterministic; it must remain safe to call on the per-frame hot
// path. Any stage data it uses must be preloaded by stage_collision_init().

// Compute and write per-fighter ledge-grab env flags into `batch->state.coll_env_flags`.
//
// Decomp:
// - mpColl sets Collide_{Left,Right}LedgeGrab when airborne, descending, and within the ledge
//   snap window. (refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14, mpColl_80044164, mpColl_800443C4)
// - Fighter cliff catch checks those bits in ftCliffCommon_80081298.
//   (refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298)
void mpcoll_env_update_ledge_grab(MslBatch* batch);

// Callback-local form used by source-shaped airborne map callbacks. The caller has already
// selected a ledge-enabled ft_081B wrapper and published CollData's final substep endpoints.
void mpcoll_env_update_ledge_grab_one(MslBatch* batch, int bi, int p, float prev_x, float prev_y,
                                      float cur_x, float cur_y);
