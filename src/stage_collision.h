#pragma once

#include "batch_internal.h"

typedef struct MslStageFloorLine {
  // Endpoints in world units, ordered so that x0 <= x1.
  float x0;
  float y0;
  float x1;
  float y1;
  // Stable `ground_id` mapping (ISO-derived segment index).
  uint16_t segment_i;
  // Floor-only line graph connectivity (FD-only v1): indices into the stage's floor line array,
  // or -1 for none.
  int16_t prev;
  int16_t next;
} MslStageFloorLine;

typedef struct MslStageFloorGraph {
  const MslStageFloorLine* lines;
  size_t line_count;
} MslStageFloorGraph;

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

// Floor graph view for the given stage_id (FD-only v1). Returns NULL if unsupported/unloaded.
const MslStageFloorGraph* stage_collision_get_floor_graph(uint32_t stage_id);

// Map a stable ISO-derived `segment_i` (ground_id) to a floor-graph line index, or -1 if unknown.
int stage_collision_floor_line_index(uint32_t stage_id, uint16_t segment_i);

// Match-flow helpers (KO/respawn/entry). Returns 1 if stage data for the given stage_id is loaded.
uint8_t stage_collision_get_blast_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_cam_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_spawn_point(uint32_t stage_id, int port, MslStagePoint2* out);
uint8_t stage_collision_get_respawn_point(uint32_t stage_id, int port, MslStagePoint2* out);

// Ledge points (FD only v1): returns 1 if the stage has a ledge on the given side.
// side: 0 = left, 1 = right.
uint8_t stage_collision_get_ledge_point(uint32_t stage_id, int side, MslStagePoint2* out);

// Item collision helper (lasers v1): returns 1 if the segment from (x0,y0)->(x1,y1) intersects a
// stage floor segment for the given stage_id.
//
// Decomp shape: itfoxlaser.c::itFoxlaser_UnkMotion1_Coll calls a stage collision helper
// (it_8029C4D4) and, on hit, sets lifetime=1 and restores the pre-coll position.
uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1);
