#pragma once

#include <stdint.h>

#include "batch_internal.h"
#include "stage_collision.h"

typedef struct MslMpLibFloorProjection {
  uint16_t line_id;
  uint16_t flags;
  float correction_y;
  float contact_x;
  float contact_y;
  float normal_x;
  float normal_y;
} MslMpLibFloorProjection;

typedef MslMpLibFloorProjection MslMpLibCeilingProjection;

enum {
  MSL_MPCOLL_FLOOR_RESULT_NONE = 0u,
  MSL_MPCOLL_FLOOR_RESULT_DIRECT = 1u,
  MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY = 2u,
  MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE = 3u,
};

// Direction-dependent source intersection primitives. The asymmetric half-plane checks and
// endpoint tolerance are gameplay semantics used by mpCheckFloor/Wall/Ceiling, not numerical
// conveniences; a symmetric segment test admits contacts that retail rejects.
// refs/melee/src/melee/mp/mplib.c::{mpLineIntersection,mpLineIntersectionH,
// mpLineIntersectionV}
uint8_t msl_mplib_line_intersection(float* ix, float* iy, float x0, float y0, float x1, float y1,
                                    float ax, float ay, float bx, float by);
uint8_t msl_mplib_line_intersection_h(float* ix, float* iy, float x0, float y0, float x1, float ax,
                                      float ay, float bx, float by);
uint8_t msl_mplib_line_intersection_v(float* ix, float* iy, float x0, float y0, float y1, float ax,
                                      float ay, float bx, float by);

enum {
  MSL_MPCOLL_FLOOR_MODE_NONE = 0u,
  MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP = 1u,
  MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION = 2u,
  MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP = 3u,
  MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY = 4u,
  MSL_MPCOLL_FLOOR_MODE_4A908_RETRY = 5u,
  MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION = 6u,
  MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION = 7u,
};

// Source-shaped carried-floor projection. Traversal follows MapLine prev/next identity rather than
// a normalized floor-only graph, and stops at the first non-floor or inactive linked line.
// refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
uint8_t msl_mplib_project_floor(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                float y, MslMpLibFloorProjection* out);
uint8_t msl_mplib_project_fighter_floor(const MslBatch* batch, int bi, uint16_t start_line_id,
                                        float x, float y, MslMpLibFloorProjection* out);

// Source-shaped carried-ceiling projection. Ceiling MapLines run in the opposite endpoint
// orientation to floors and apply the retail -0.0001 separation bias.
// refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
uint8_t msl_mplib_project_ceiling(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                  float y, MslMpLibCeilingProjection* out);

// Return the source endpoint and outward adjacent line for the selected geometric side.
// side: -1 left, +1 right.
uint8_t msl_mplib_floor_endpoint(const MslBatch* batch, int bi, uint16_t line_id, int side,
                                 float* x_out, float* y_out, int16_t* adjacent_line_id_out);

// Directional mpCheckFloor-shaped sweep over the canonical MapLine table.
// refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpCheckFloorRemap,mpLib_8004ED5C}
uint8_t msl_mplib_sweep_floor(MslBatch* batch, int bi, size_t idx, float ax, float ay, float bx,
                              float by, uint16_t floor_skip, MslStageQueryHit* out);
uint8_t msl_mplib_sweep_floor_filtered(MslBatch* batch, int bi, size_t idx, float ax, float ay,
                                       float bx, float by, uint16_t floor_skip,
                                       uint8_t hard_floor_only, MslStageQueryHit* out);
