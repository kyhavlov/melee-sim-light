#pragma once

#include "batch_internal.h"

// mpColl-style ground contact substrate for the supported legal-stage runtime.
//
// Owns: state.on_ground, state.ground_id, and collision contact metadata fields.
//
// Decomp pointers:
// - Collision runs post-integration and is consumed by action logic:
//   refs/melee/src/melee/ft/fighter.c::Fighter_procMap (prio 6).
// - ECB prev/current and floor persistence live in CollData:
//   refs/melee/src/melee/lb/types.h::CollData.
void mpcoll_ground_apply(MslBatch* batch);

// Refresh one grounded CollData.floor after a source stage displacement update that runs after the
// main map-collision floor pass.
void mpcoll_ground_refresh_grounded_root_floor_index(MslBatch* batch, int batch_index,
                                                     int player_index);

typedef struct MslMpcollFloorMaskResult {
  uint16_t ground_id;
  uint16_t candidate_ground_id;
  float corrected_pos_y;
  float corrected_pos_x;
  uint8_t candidate_published;
} MslMpcollFloorMaskResult;

// Narrow `mpColl_800477E0` floor-mask predicate for callbacks that need the floor result before
// the generic stage-collision pass mutates fighter state.
uint8_t mpcoll_800477e0_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out);

// Narrow grounded `mpColl_80048654` floor-mask predicate. Unlike 800477E0, its ECB load uses
// flags=5, which pins desired_ecb.bottom.y to the fighter root before the floor query.
uint8_t mpcoll_80048654_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out);

// CapturePulled Lw->Hi immediate callback floor-mask subset:
// `fn_800DB230` applies the capture anchor to the airborne Hi state, then calls ft_80083C00 /
// mpColl_800477E0 before generic stage collision. In this path the replay-visible root can be
// below the persisted floor while the extracted ECB bottom is above it, so use the CollData
// floor-index/root projection lane rather than the ordinary bottom sweep approximation above.
uint8_t mpcoll_800477e0_capture_root_floor_mask_probe(const MslBatch* batch, size_t idx,
                                                      MslMpcollFloorMaskResult* out);

// Throw-release `ftCo_800DDDE4` publication subset:
// after x1A70 placement, source calls `mpColl_800471F8`, whose air-collision wrapper substeps from
// CollData.last_pos to CollData.cur_pos and publishes the floor-hit substep root before damage
// entry. This helper reconstructs that root-level floor publication; it does not run generic
// per-frame map collision.
uint8_t mpcoll_800471f8_throw_release_root_floor_probe(const MslBatch* batch, size_t idx,
                                                       float source_last_x, float source_last_y,
                                                       MslMpcollFloorMaskResult* out);

// ftCo_800DC920's first release-placement branch: project the constrained fighter onto a floor
// connected to the sample owner's carried floor, subject to p_ftCommonData->x3BC.
uint8_t mpcoll_dc920_connected_floor_attempt(const MslBatch* batch, size_t constrained_idx,
                                             uint16_t sample_owner_ground_id,
                                             MslMpcollFloorMaskResult* out);
