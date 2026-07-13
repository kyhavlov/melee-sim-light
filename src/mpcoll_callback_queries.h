#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

typedef struct MslMpcollFloorMaskResult {
  uint16_t ground_id;
  uint16_t candidate_ground_id;
  float corrected_pos_y;
  float corrected_pos_x;
  uint8_t candidate_published;
} MslMpcollFloorMaskResult;

// Exact callback-local queries whose source owners need a floor result outside the ordinary
// Fighter_procMap pass. refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_800477E0,
// mpColl_80048654}; refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
uint8_t msl_mpcoll_query_477e0_floor_mask(const MslBatch* batch, size_t idx,
                                          MslMpcollFloorMaskResult* out);
uint8_t msl_mpcoll_query_48654_floor_mask(const MslBatch* batch, size_t idx,
                                          MslMpcollFloorMaskResult* out);
uint8_t msl_mpcoll_query_capture_root_floor_mask(const MslBatch* batch, size_t idx,
                                                 MslMpcollFloorMaskResult* out);
uint8_t msl_mpcoll_query_capturecut_connected_floor(const MslBatch* batch, size_t constrained_idx,
                                                    uint16_t sample_owner_ground_id,
                                                    MslMpcollFloorMaskResult* out);

// Republish a carried grounded floor after a source stage displacement performed after map
// collision. refs/melee/src/melee/mp/mpcoll.c::mpColl_8004DD90_Floor
void mpcoll_ground_refresh_grounded_root_floor_index(MslBatch* batch, int bi, int p);
