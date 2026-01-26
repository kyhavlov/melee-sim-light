#pragma once

#include "batch_internal.h"

typedef struct MslStageBounds {
  float left;
  float right;
  float top;
  float bottom;
} MslStageBounds;

typedef struct MslStagePoint2 {
  float x;
  float y;
} MslStagePoint2;

// Load stage collision data required by stage_collision_apply.
// Must be called during initialization (before stepping); may allocate.
// Returns 0 on success.
//
// Note: stage_collision_apply() must remain allocation-free and must not do any IO or parsing.
// All stage loading allocations must stay inside stage_collision_init().
int stage_collision_init(void);

void stage_collision_apply(MslBatch* batch);

// Match-flow helpers (KO/respawn/entry). Returns 1 if stage data for the given stage_id is loaded.
uint8_t stage_collision_get_blast_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_cam_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_spawn_point(uint32_t stage_id, int port, MslStagePoint2* out);
uint8_t stage_collision_get_respawn_point(uint32_t stage_id, int port, MslStagePoint2* out);
