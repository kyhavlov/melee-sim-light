#pragma once

#include "batch_internal.h"

// mpColl-style ground contact substrate (FD-only v1).
//
// Owns: state.on_ground, state.ground_id, and collision contact metadata fields.
//
// Decomp pointers:
// - Collision runs post-integration and is consumed by action logic:
//   refs/melee/src/melee/ft/fighter.c::Fighter_procMap (prio 6).
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

// CapturePulled Lw->Hi immediate callback floor-mask subset:
// `fn_800DB230` applies the capture anchor to the airborne Hi state, then calls ft_80083C00 /
// mpColl_800477E0 before generic stage collision. In this path the replay-visible root can be
// below the persisted floor while the extracted ECB bottom is above it, so use the CollData
// floor-index/root projection lane rather than the ordinary bottom sweep approximation above.
uint8_t mpcoll_800477e0_capture_root_floor_mask_probe(const MslBatch* batch, size_t idx,
                                                      MslMpcollFloorMaskResult* out);
