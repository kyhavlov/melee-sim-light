#pragma once

#include "batch_internal.h"

enum {
  MSL_STAGE_PLATFORM_TRANSFORM_NONE = 0u,
  MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT = 1u,
  MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y = 2u,
  MSL_STAGE_PLATFORM_TRANSFORM_RANDALL = 3u,
};

typedef struct MslStageFloorLine {
  // Endpoints in world units, ordered so that x0 <= x1.
  float x0;
  float y0;
  float x1;
  float y1;
  // Source MapLine v0/v1 endpoint orientation. Floor collision uses normalized x0/x1 above, but
  // some mpLib helpers (notably mpLib_80051BA8_Floor for ledge grab) inspect raw v0/v1.
  float raw_x0;
  float raw_y0;
  float raw_x1;
  float raw_y1;
  // ISO-derived ledge flag for this floor segment (LINE_FLAG_LEDGE / `"segments[*].ledge"`).
  // Used by ledge-grab mask computation.
  uint8_t is_ledge;
  // ISO-derived soft platform flag (LINE_FLAG_PLATFORM / `"segments[*].platform"`). Runtime
  // fighter collision uses the full floor graph for static pass-through platform ownership, while
  // the filtered fighter graph remains available for debug/tests and non-platform-only checks.
  uint8_t is_platform;
  // MSLSTG01 active/fighter-solid policy for the current legal-stage mode. For frozen Pokemon
  // Stadium, this keeps transformation lines visible in debug/data APIs while suppressing inactive
  // transformation ground objects from fighter collision.
  // refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
  // data/stages/bin/grps.bin::MSLSTG01 flags
  uint8_t fighter_solid;
  // Generated stage-object support owner. This is not static terrain admission; gameplay consumers
  // must combine it with the matching live stage-object seed/runtime state before preserving carried
  // CollData support.
  // data/stages/bin/*.bin::MSLSTG01 flags[7:3]
  uint8_t stage_object_support_kind;
  // Generated platform transform owner for this floor line, if any. Zero means static world
  // endpoints; nonzero values correspond to MSLSTG01 platform transform record kind ids.
  // data/stages/bin/*.bin::MSLSTG01 platform_transform records
  uint8_t platform_transform_kind;
  uint8_t platform_transform_id;
  // Source floor material multiplier returned by mpLib_800569EC(MapLine.lo_flags & 0xFF).
  // Fighter_procUpdate consumes it through ft_GetGroundFrictionMultiplier for grounded knockback
  // and attacker shield pushback decay.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_800569EC,mpLib_803BF248}
  // refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
  // data/stages/bin/*.bin::MSLSTG01 segment.ground_friction_mul
  float ground_friction_mul;
  // Source MapLine flag words and MapJoint owner. These back mpLineGetFlags/mpLineGetNormal and
  // mpJointFromLine-shaped query filters.
  // refs/melee/src/melee/mp/types.h::{MapLine,MapJoint}
  // refs/melee/src/melee/mp/mplib.c::{mpLineGetFlags,mpLineGetNormal,mpJointFromLine}
  uint16_t hi_flags;
  uint16_t lo_flags;
  int16_t joint_id;
  uint8_t _pad_flags[2];
  // Connectivity hints for mpLib_8004ED5C-style endpoint extension:
  // - has_prev_link: there exists some collision segment connected to (x0,y0)
  // - has_next_link: there exists some collision segment connected to (x1,y1)
  //
  // Decomp: mpLib_8004ED5C uses mpLineGetPrev/Next only as a boolean (!= -1) to decide whether
  // to extend that endpoint by 1 unit, regardless of the neighbor line kind.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  uint8_t has_prev_link;
  uint8_t has_next_link;
  // Stable `ground_id` mapping (ISO-derived segment index).
  uint16_t segment_i;
  // Source MapLine links from refs/melee/src/melee/mp/types.h::MapLine, resolved to this
  // line's runtime endpoint orientation. These are stable ISO line ids, not graph indices.
  int16_t raw_prev_id;
  int16_t raw_next_id;
  int16_t raw_prev_alt_id;
  int16_t raw_next_alt_id;
  // MSLSTG01 preserved a non-primary endpoint link for this source line. Frozen Stadium uses these
  // alternate floor links for transformation-map adjacency that is not visible from the normalized
  // active floor chain alone.
  uint8_t has_alternate_endpoint_link;
  uint8_t _pad0[1];
  // Floor-only line graph connectivity: indices into the stage's floor line array,
  // or -1 for none.
  int16_t prev;
  int16_t next;
  // Adjacent wall graph indices at the start/end of this floor chain, or -1 for none.
  // Derived from generated MapLine endpoint geometry during stage load.
  int16_t adjacent_left_wall;
  int16_t adjacent_right_wall;
} MslStageFloorLine;

typedef struct MslStageFloorGraph {
  const MslStageFloorLine* lines;
  size_t line_count;
} MslStageFloorGraph;

typedef struct MslStageFloorLineCaps {
  uint8_t is_platform;
  uint8_t fighter_solid;
  uint8_t stage_object_support_kind;
  uint8_t platform_transform_kind;
  uint8_t platform_transform_id;
  float ground_friction_mul;
} MslStageFloorLineCaps;

typedef struct MslStageMovingSurfaceState {
  uint8_t valid;
  uint8_t active;
  uint8_t visible;
  uint8_t current_owned;
  uint8_t source_trusted;
  uint8_t reached_hidden_this_step;
  uint8_t platform_transform_kind;
  uint8_t platform_transform_id;
  uint8_t stage_object_support_kind;
  uint8_t source_phase;
  uint16_t segment_i;
  int16_t joint_id;
  int32_t source_frame;
  uint16_t source_timer;
  uint8_t source_bits;
  uint8_t _pad0[1];
  float source_target;
  float x0;
  float y0;
  float x1;
  float y1;
  float normal_x;
  float normal_y;
  float velocity_x;
  float velocity_y;
} MslStageMovingSurfaceState;

typedef struct MslStageCeilingLine {
  // Endpoints in world units, ordered so that x0 >= x1 (decomp mpLib_8004E090 assumes v0 is the
  // right endpoint and v1 is the left endpoint for ceiling lines).
  float x0;
  float y0;
  float x1;
  float y1;
  uint8_t has_prev_link;  // connected at (x0,y0)
  uint8_t has_next_link;  // connected at (x1,y1)
  uint8_t fighter_solid;
  uint8_t _pad0[1];
  uint16_t segment_i;  // ISO-derived segment index
  uint16_t hi_flags;
  uint16_t lo_flags;
  int16_t joint_id;
  int16_t raw_prev_id;
  int16_t raw_next_id;
  int16_t prev;  // neighbor whose end == our start, or -1
  int16_t next;  // neighbor whose start == our end, or -1
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
  float min_x;
  float max_x;
  float min_y;
  float max_y;
  uint8_t has_prev_link;  // connected at (x0,y0)
  uint8_t has_next_link;  // connected at (x1,y1)
  uint8_t fighter_solid;
  uint8_t _pad0[1];
  uint16_t segment_i;  // ISO-derived segment index
  uint16_t hi_flags;
  uint16_t lo_flags;
  int16_t joint_id;
  int16_t raw_prev_id;
  int16_t raw_next_id;
  int16_t prev;  // neighbor whose end == our start, or -1
  int16_t next;  // neighbor whose start == our end, or -1
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

typedef enum MslStageRawLineKind {
  MSL_STAGE_RAW_LINE_UNKNOWN = 0,
  MSL_STAGE_RAW_LINE_FLOOR = 1,
  MSL_STAGE_RAW_LINE_CEILING = 2,
  MSL_STAGE_RAW_LINE_LEFT_WALL = 3,
  MSL_STAGE_RAW_LINE_RIGHT_WALL = 4,
} MslStageRawLineKind;

typedef enum MslStageObjectSupportKind {
  MSL_STAGE_OBJECT_SUPPORT_NONE = 0,
  MSL_STAGE_OBJECT_SUPPORT_YOSHI_SHYGUY = 1,
} MslStageObjectSupportKind;

typedef struct MslStagePoint2 {
  float x;
  float y;
} MslStagePoint2;

enum {
  MSL_STAGE_QUERY_FLOOR = 1u << 0,
  MSL_STAGE_QUERY_CEILING = 1u << 1,
  MSL_STAGE_QUERY_LEFT_WALL = 1u << 2,
  MSL_STAGE_QUERY_RIGHT_WALL = 1u << 3,
};

typedef struct MslStageQueryHit {
  MslStageRawLineKind kind;
  int32_t line_idx;
  uint16_t segment_i;
  int16_t joint_id;
  uint16_t flags;
  float x;
  float y;
  float normal_x;
  float normal_y;
  float dist2;
} MslStageQueryHit;

// Load stage collision data required by stage_collision_apply.
// Must be called during initialization (before stepping); may allocate.
// Returns 0 on success.
//
// Note: stage_collision_apply() must remain allocation-free and must not do any IO or parsing.
// All stage loading allocations must stay inside stage_collision_init().
int stage_collision_init(void);

// Ensure a specific registered stage artifact is loaded. This may allocate and should be called
// only from init/tooling paths, not during frame stepping. Returns 1 when the requested stage's
// collision + match-flow role data are available.
uint8_t stage_collision_require_stage(uint32_t stage_id);

// No-allocation availability predicates for reseed/eval admission. `registered` means the stage is
// in the supported MSLSTG01 domain; `available` additionally requires its artifact to have been
// loaded during initialization.
uint8_t stage_collision_stage_registered(uint32_t stage_id);
uint8_t stage_collision_stage_available(uint32_t stage_id);

void stage_collision_apply(MslBatch* batch);

// Full floor graph view for the given stage_id. Includes soft-platform lines for debug/data
// inspection. Returns NULL if unsupported/unloaded.
const MslStageFloorGraph* stage_collision_get_floor_graph(uint32_t stage_id);

// Map a stable ISO-derived `segment_i` (ground_id) to a floor-graph line index, or -1 if unknown.
int stage_collision_floor_line_index(uint32_t stage_id, uint16_t segment_i);

// Non-platform-only floor graph view for the given stage_id. Runtime fighter platform collision
// uses the full floor graph plus Pass/floor-skip gating; this filtered view is retained for
// tests/debug callers that intentionally need static non-platform floors only.
const MslStageFloorGraph* stage_collision_get_fighter_floor_graph(uint32_t stage_id);
int stage_collision_fighter_floor_line_index(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_caps(uint32_t stage_id, uint16_t segment_i,
                                        MslStageFloorLineCaps* out);
uint8_t stage_collision_floor_line_is_platform(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_is_ledge(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_is_sloped(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_is_runtime_fighter_solid(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_stage_object_support_kind(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_is_flat_between_sloped_ledges(uint32_t stage_id,
                                                                 uint16_t segment_i);
uint8_t stage_collision_stage_has_flat_between_sloped_ledges(uint32_t stage_id);
uint8_t stage_collision_stage_has_only_static_cardinal_hard_floors(uint32_t stage_id);
uint8_t stage_collision_stage_has_alternate_floor_endpoint_links(uint32_t stage_id);
uint8_t stage_collision_stage_has_height_platform_transform(uint32_t stage_id);
uint8_t stage_collision_stage_has_deferred_static_floor_transform(uint32_t stage_id);
uint8_t stage_collision_floor_line_has_platform_transform(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_has_height_platform_transform(uint32_t stage_id,
                                                                 uint16_t segment_i);
uint8_t stage_collision_floor_line_has_static_y_platform_transform(uint32_t stage_id,
                                                                   uint16_t segment_i);
uint8_t stage_collision_floor_line_has_randall_platform_transform(uint32_t stage_id,
                                                                  uint16_t segment_i);
int stage_collision_randall_floor_line_index(uint32_t stage_id);
float stage_collision_floor_ground_friction_mul(uint32_t stage_id, uint16_t segment_i);
uint8_t stage_collision_floor_line_platform_transform_id(uint32_t stage_id, uint16_t segment_i,
                                                         uint8_t* platform_id_out);
uint8_t stage_collision_floor_line_height_platform_state_is_current_owned(const MslBatch* batch,
                                                                          int bi,
                                                                          uint16_t segment_i);
uint8_t stage_collision_floor_line_height_platform_state_is_source_trusted(const MslBatch* batch,
                                                                           int bi,
                                                                           uint16_t segment_i);
uint8_t stage_collision_fod_hidden_target_height(float* out);
// Resolve a generated platform-transform floor into one source-owned runtime surface packet.
// Static callers should continue to use `stage_collision_static_query`; this packet is the only
// moving/platform-transform admission path for fighter collision and debug inspection.
uint8_t stage_collision_floor_line_moving_surface_state(const MslBatch* batch, int bi,
                                                        const MslStageFloorLine* line,
                                                        MslStageMovingSurfaceState* out);
uint8_t stage_collision_fod_height_platform_line_y_at_x(const MslBatch* batch, int bi,
                                                        uint8_t platform_id, float x, float* y_out);
// Resolve a floor line to current world coordinates for the given batch environment. Static lines
// copy through unchanged. Dynamic FoD platform lines consume causal stage platform state.
uint8_t stage_collision_floor_line_world(const MslBatch* batch, int bi,
                                         const MslStageFloorLine* line, MslStageFloorLine* out);
// Resolve Yoshi's Story Randall to its current platform center. Returns 0 outside stages without
// extracted Randall path data.
uint8_t stage_collision_get_randall_position(const MslBatch* batch, int bi, float* x_out,
                                             float* y_out);
// Return the current-frame platform motion delta for a transformed floor line. This is runtime
// stage-object carry state, not replay-seeded future state.
uint8_t stage_collision_floor_line_motion_delta(const MslBatch* batch, int bi,
                                                const MslStageFloorLine* line, float* dx_out,
                                                float* dy_out);

// Source MapLine raw graph helpers. `segment_i` is the stable ISO line id carried in MSLSTG01.
// These mirror mplib's mpLineGetPrev/Next plus the Non* traversal families without exposing
// gameplay code to stage-specific line-id policy.
uint8_t stage_collision_raw_line_kind(uint32_t stage_id, uint16_t segment_i,
                                      MslStageRawLineKind* out_kind);
uint8_t stage_collision_raw_line_next_non_kind(uint32_t stage_id, uint16_t segment_i,
                                               MslStageRawLineKind skip_kind,
                                               MslStageRawLineKind* out_kind,
                                               uint16_t* out_segment_i);
uint8_t stage_collision_raw_line_prev_non_kind(uint32_t stage_id, uint16_t segment_i,
                                               MslStageRawLineKind skip_kind,
                                               MslStageRawLineKind* out_kind,
                                               uint16_t* out_segment_i);

// Ceiling graph view for the given stage_id. Returns NULL if unsupported/unloaded.
const MslStageCeilingGraph* stage_collision_get_ceiling_graph(uint32_t stage_id);

// Left/right wall graph views for the given stage_id. Returns NULL if unsupported/unloaded.
const MslStageWallGraph* stage_collision_get_left_wall_graph(uint32_t stage_id);
const MslStageWallGraph* stage_collision_get_right_wall_graph(uint32_t stage_id);

// Map a stable ISO-derived `segment_i` to a line index in the corresponding graph, or -1 if unknown.
int stage_collision_ceiling_line_index(uint32_t stage_id, uint16_t segment_i);
int stage_collision_left_wall_line_index(uint32_t stage_id, uint16_t segment_i);
int stage_collision_right_wall_line_index(uint32_t stage_id, uint16_t segment_i);

// Static mpLib-style query substrate for legal-stage line data. These helpers consume preloaded
// MSLSTG01 static line metadata only. Height-transformed FoD side platforms and Randall path lines
// are intentionally excluded; moving-surface runtime behavior is admitted through
// stage_collision_floor_line_moving_surface_state().
//
// `line_id_skip` is the floor skip line id for floor checks, or 0xFFFF for none.
// `joint_id_skip` / `joint_id_only` mirror mpLib's joint filters; pass -1 for disabled.
uint8_t stage_collision_static_query(uint32_t stage_id, uint32_t checks, float x0, float y0,
                                     float x1, float y1, uint16_t line_id_skip,
                                     int16_t joint_id_skip, int16_t joint_id_only,
                                     MslStageQueryHit* out);

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

// Item collision helper (lasers v1): returns 1 if the segment from (x0,y0)->(x1,y1) intersects an
// active runtime stage collision segment for the given stage_id.
//
// Decomp shape: itfoxlaser.c::itFoxlaser_UnkMotion1_Coll calls a stage collision helper
// (it_8029C4D4) and, on hit, sets lifetime=1 and restores the pre-coll position.
uint8_t stage_collision_item_line_hits_floor(uint32_t stage_id, float x0, float y0, float x1,
                                             float y1);
uint8_t stage_collision_item_line_hit_floor(uint32_t stage_id, float x0, float y0, float x1,
                                            float y1, float* hit_x_out, float* hit_y_out);

// Item fixed-ECB wall helper for stage-owned item Coll callbacks. Returns 1 when the fixed ECB's
// previous-to-current point/edge sweep hits the requested active wall graph. side: 0 = left wall,
// 1 = right wall. The caller owns source-specific response such as direction flips.
uint8_t stage_collision_item_fixed_ecb_sweep_hits_wall(uint32_t stage_id, int side,
                                                       float prev_center_x, float prev_center_y,
                                                       float center_x, float center_y,
                                                       float ecb_left, float ecb_right,
                                                       float ecb_bottom, float ecb_top);
uint8_t stage_collision_item_fixed_ecb_sweep_hits_floor(uint32_t stage_id, float prev_center_x,
                                                        float prev_center_y, float center_x,
                                                        float center_y, float ecb_left,
                                                        float ecb_right, float ecb_bottom);
