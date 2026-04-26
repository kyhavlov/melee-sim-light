#pragma once

#include "batch_internal.h"

// mpColl-style ground contact substrate (FD-only v1).
//
// Owns: state.on_ground, state.ground_id, and collision contact metadata fields.
//
// Decomp pointers:
// - Collision runs post-integration and is consumed by action logic:
//   refs/melee/src/melee/ft/fighter.c::Fighter_procMap (prio 6) and docs/DECOMP_PROC_ORDER.md.
// - ECB prev/current and floor persistence live in CollData:
//   refs/melee/src/melee/lb/types.h::CollData.
void mpcoll_ground_apply(MslBatch* batch);

typedef struct MslMpcollFloorMaskResult {
  uint16_t ground_id;
  float corrected_pos_y;
} MslMpcollFloorMaskResult;

// Narrow `mpColl_800477E0` floor-mask predicate for callbacks that need the floor result before
// the generic stage-collision pass mutates fighter state.
uint8_t mpcoll_800477e0_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out);
