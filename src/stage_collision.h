#pragma once

#include "batch_internal.h"

typedef struct MslStageFloorLine {
  // Endpoints in world units, ordered so that x0 <= x1.
  float x0;
  float y0;
  float x1;
  float y1;
  // ISO-derived ledge flag for this floor segment (LINE_FLAG_LEDGE / `"segments[*].ledge"`).
  // Used by ledge-grab mask computation.
  uint8_t is_ledge;
  // ISO-derived soft platform flag (LINE_FLAG_PLATFORM / `"segments[*].platform"`). Current
  // platform mechanics are first-pass: platforms are present in the floor graph for from-above
  // landing/floor collision, while drop-through/pass-through ownership remains a separate mpColl
  // owner.
  uint8_t is_platform;
  // Connectivity hints for mpLib_8004ED5C-style endpoint extension:
  // - has_prev_link: there exists some collision segment connected to (x0,y0)
  // - has_next_link: there exists some collision segment connected to (x1,y1)
  //
  // Decomp: mpLib_8004ED5C uses mpLineGetPrev/Next only as a boolean (!= -1) to decide whether
  // to extend that endpoint by 1 unit, regardless of the neighbor line kind.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  uint8_t has_prev_link;
  uint8_t has_next_link;
  uint8_t _pad0[2];
  // Stable `ground_id` mapping (ISO-derived segment index).
  uint16_t segment_i;
  // Floor-only line graph connectivity: indices into the stage's floor line array,
  // or -1 for none.
  int16_t prev;
  int16_t next;
} MslStageFloorLine;

typedef struct MslStageFloorGraph {
  const MslStageFloorLine* lines;
  size_t line_count;
} MslStageFloorGraph;

typedef struct MslStageCeilingLine {
  // Endpoints in world units, ordered so that x0 >= x1 (decomp mpLib_8004E090 assumes v0 is the
  // right endpoint and v1 is the left endpoint for ceiling lines).
  float x0;
  float y0;
  float x1;
  float y1;
  uint8_t has_prev_link;  // connected at (x0,y0)
  uint8_t has_next_link;  // connected at (x1,y1)
  uint8_t _pad0[2];
  uint16_t segment_i;  // ISO-derived segment index
  int16_t prev;        // neighbor whose end == our start, or -1
  int16_t next;        // neighbor whose start == our end, or -1
} MslStageCeilingLine;

typedef struct MslStageCeilingGraph {
  const MslStageCeilingLine* lines;
  size_t line_count;
  float min_x;
  float max_x;
  float min_y;
  float max_y;
} MslStageCeilingGraph;

typedef struct MslStageWallLine {
  // Endpoints in world units. Orientation is kind-dependent to mirror decomp:
  // - left_wall: y0 <= y1
  // - right_wall: y0 >= y1
  float x0;
  float y0;
  float x1;
  float y1;
  uint8_t has_prev_link;  // connected at (x0,y0)
  uint8_t has_next_link;  // connected at (x1,y1)
  uint8_t _pad0[2];
  uint16_t segment_i;  // ISO-derived segment index
  int16_t prev;        // neighbor whose end == our start, or -1
  int16_t next;        // neighbor whose start == our end, or -1
} MslStageWallLine;

typedef struct MslStageWallGraph {
  const MslStageWallLine* lines;
  size_t line_count;
  float min_x;
  float max_x;
  float min_y;
  float max_y;
} MslStageWallGraph;

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

// Floor graph view for the given stage_id. Returns NULL if unsupported/unloaded.
const MslStageFloorGraph* stage_collision_get_floor_graph(uint32_t stage_id);

// Map a stable ISO-derived `segment_i` (ground_id) to a floor-graph line index, or -1 if unknown.
int stage_collision_floor_line_index(uint32_t stage_id, uint16_t segment_i);

// Ceiling graph view for the given stage_id. Returns NULL if unsupported/unloaded.
const MslStageCeilingGraph* stage_collision_get_ceiling_graph(uint32_t stage_id);

// Left/right wall graph views for the given stage_id. Returns NULL if unsupported/unloaded.
const MslStageWallGraph* stage_collision_get_left_wall_graph(uint32_t stage_id);
const MslStageWallGraph* stage_collision_get_right_wall_graph(uint32_t stage_id);

// Map a stable ISO-derived `segment_i` to a line index in the corresponding graph, or -1 if unknown.
int stage_collision_ceiling_line_index(uint32_t stage_id, uint16_t segment_i);
int stage_collision_left_wall_line_index(uint32_t stage_id, uint16_t segment_i);
int stage_collision_right_wall_line_index(uint32_t stage_id, uint16_t segment_i);

// Match-flow helpers (KO/respawn/entry). Returns 1 if stage data for the given stage_id is loaded.
uint8_t stage_collision_get_blast_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_cam_bounds_world(uint32_t stage_id, MslStageBounds* out);
uint8_t stage_collision_get_spawn_point(uint32_t stage_id, int port, MslStagePoint2* out);
uint8_t stage_collision_get_respawn_point(uint32_t stage_id, int port, MslStagePoint2* out);

// Ledge points: returns 1 if the stage has a ledge on the given side.
// side: 0 = left, 1 = right.
uint8_t stage_collision_get_ledge_point(uint32_t stage_id, int side, MslStagePoint2* out);

// Ledge floor line: returns the floor segment that carries the exterior ledge point for
// the given side, or NULL if unavailable.
const MslStageFloorLine* stage_collision_get_ledge_floor_line(uint32_t stage_id, int side);

// Item collision helper (lasers v1): returns 1 if the segment from (x0,y0)->(x1,y1) intersects a
// stage collision segment for the given stage_id.
//
// Decomp shape: itfoxlaser.c::itFoxlaser_UnkMotion1_Coll calls a stage collision helper
// (it_8029C4D4) and, on hit, sets lifetime=1 and restores the pre-coll position.
uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1);
