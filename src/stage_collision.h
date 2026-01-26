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

// Item collision helper (lasers v1): returns 1 if the segment from (x0,y0)->(x1,y1) intersects a
// stage floor segment for the given stage_id.
//
// Decomp shape: itfoxlaser.c::itFoxlaser_UnkMotion1_Coll calls a stage collision helper
// (it_8029C4D4) and, on hit, sets lifetime=1 and restores the pre-coll position.
uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1);
