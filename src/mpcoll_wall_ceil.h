#pragma once

#include "batch_internal.h"
#include "mpcoll_context.h"
#include "mpcoll_ecb_points.h"

// mpColl-style wall + ceiling contact substrate.
//
// Owns:
// - state.wall_* (wall_kind, wall_id, contact + normal)
// - state.ceiling_* (ceiling_id, contact + normal)
// - subsets of state.coll_env_flags for wall/ceiling bits
//
// Decomp pointers:
// - Query primitives:
//   - refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
//   - refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
//   - refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
// - Persistent projection helpers:
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
//   - refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
// - Contact storage:
//   - refs/melee/src/melee/lb/types.h::CollData (SurfaceData left_facing_wall/right_facing_wall/ceiling + contact)
//   - refs/melee/src/common_structs.h (Collide_* env flag bit values)
void mpcoll_wall_ceil_apply(MslBatch* batch);

// Legal-stage portions of mpCollGetSpeedFloor/Ceiling and mpColl_IsOnPlatform.
//
// Source speed helpers call mpGetSpeed(surface.index, coll->ecb.top, out), which returns the
// current-line motion delta from the previous mpLib endpoint positions. Static legal-stage lines
// have no endpoint motion, and FoD/Randall transformed floor lines consume the shared moving-surface
// packet used by fighter support/carry.
//
// Wall speed helpers below are debug-visible only while MSL still models wall contact as a singleton
// wall_kind/wall_id rather than source CollData.left_facing_wall and right_facing_wall records.
// refs/melee/src/melee/mp/mpcoll.c::{
//   mpCollGetSpeedFloor,mpCollGetSpeedLeftWall,mpCollGetSpeedRightWall,mpCollGetSpeedCeiling,
//   mpColl_IsOnPlatform}
// refs/melee/src/melee/mp/mplib.c::mpGetSpeed
uint8_t mpcoll_get_speed_floor_static(const MslBatch* batch, int batch_index, size_t idx,
                                      float* out_x, float* out_y);
uint8_t mpcoll_get_speed_left_wall_static(const MslBatch* batch, int batch_index, size_t idx,
                                          float* out_x, float* out_y);
uint8_t mpcoll_get_speed_right_wall_static(const MslBatch* batch, int batch_index, size_t idx,
                                           float* out_x, float* out_y);
uint8_t mpcoll_get_speed_ceiling_static(const MslBatch* batch, int batch_index, size_t idx,
                                        float* out_x, float* out_y);
uint8_t mpcoll_is_on_platform(const MslBatch* batch, int batch_index, size_t idx);

enum {
  MSL_MPCOLL_WALL_KIND_NONE = 0u,
  MSL_MPCOLL_WALL_KIND_LEFT = 1u,
  MSL_MPCOLL_WALL_KIND_RIGHT = 2u,
};

enum {
  MSL_MPCOLL_ORDERED_SQUEEZE_CEILING = 1u,
  MSL_MPCOLL_ORDERED_SQUEEZE_FLOOR = 2u,
  MSL_MPCOLL_ORDERED_SQUEEZE_RIGHT_WALL = 4u,
  MSL_MPCOLL_ORDERED_SQUEEZE_LEFT_WALL = 8u,
};

enum {
  MSL_MPCOLL_WALL_RESULT_NONE = 0u,
  MSL_MPCOLL_WALL_RESULT_AIR_PERSISTENCE = 1u,
  MSL_MPCOLL_WALL_RESULT_AIR_ENVELOPE = 2u,
  MSL_MPCOLL_WALL_RESULT_AIR_POINT_PROJECT = 3u,
  MSL_MPCOLL_WALL_RESULT_AIR_PERSISTED_PROJECT = 4u,
  MSL_MPCOLL_WALL_RESULT_GROUNDED_ENVELOPE = 5u,
  MSL_MPCOLL_WALL_RESULT_GROUNDED_POINT_PROJECT = 6u,
};

typedef struct MslMpcollWallResult {
  uint8_t hit;
  uint8_t side;  // 1 = left wall, 2 = right wall (mplib CollLine side)
  uint8_t hug;
  uint8_t mode;
  uint16_t segment_id;
  uint32_t env_flags;
  float dx;
  float contact_x;
  float contact_y;
  float normal_x;
  float normal_y;
} MslMpcollWallResult;

enum {
  MSL_MPCOLL_CEILING_RESULT_NONE = 0u,
  MSL_MPCOLL_CEILING_RESULT_PERSISTENCE = 1u,
  MSL_MPCOLL_CEILING_RESULT_TOP_SWEEP = 2u,
  MSL_MPCOLL_CEILING_RESULT_ADJACENT_WALL = 3u,
  MSL_MPCOLL_CEILING_RESULT_SPECIALHI_FLOOR_UNDERSIDE = 4u,
};

typedef struct MslMpcollCeilingResult {
  uint8_t hit;
  uint8_t mode;
  uint16_t segment_id;
  int line_idx;
  uint32_t env_flags;
  float dy;
  float contact_x;
  float contact_y;
  float normal_x;
  float normal_y;
} MslMpcollCeilingResult;

typedef struct MslMpcollOrderedWallCeilResult {
  uint8_t left_right_flags;  // bit 0 = left wall, bit 1 = right wall
  uint8_t squeeze_flags;     // source bits: ceiling=1, floor=2, right wall=4, left wall=8
  uint8_t squeeze_flags_all;
  uint8_t hit_ceiling;
  uint8_t hit_floor;
  uint8_t touching_floor;
  uint16_t left_wall_id;
  uint16_t right_wall_id;
  float x_after_left_wall;
  float x_after_right_wall;
  float y_after_ceiling;
  float y_after_floor;
  MslMpcollWallResult left_wall;
  MslMpcollWallResult right_wall;
  MslMpcollCeilingResult ceiling;
  MslEcbWorldPoints cur_ecb_after;
} MslMpcollOrderedWallCeilResult;

// Grounded inline2 / mpColl_8004ACE4 wall and ceiling subpass. This mutates the fighter's
// current position and wall/ceiling contact outputs in source order, before floor resolution.
// It is intentionally separate from the generic airborne wall/ceiling pass so floor collision can
// consume same-frame left/right wall results.
void mpcoll_grounded_wall_ceil_ordered_begin(const MslMpcollContext* ctx,
                                             const MslEcbWorldPoints* prev_ecb,
                                             const MslEcbWorldPoints* cur_ecb,
                                             MslMpcollOrderedWallCeilResult* out);

// Source retry after a grounded floor hit: mpColl_8004ACE4 runs the ceiling check/collide pair
// again after floor resolution. Returns 1 when the retry touched ceiling.
uint8_t mpcoll_grounded_ceiling_ordered_retry(const MslMpcollContext* ctx,
                                              const MslEcbWorldPoints* prev_ecb,
                                              MslEcbWorldPoints* cur_ecb,
                                              MslMpcollOrderedWallCeilResult* io);
