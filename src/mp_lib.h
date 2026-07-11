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

// Source-shaped carried-floor projection. Traversal follows MapLine prev/next identity rather than
// a normalized floor-only graph, and stops at the first non-floor or inactive linked line.
// refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
uint8_t msl_mplib_project_floor(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                float y, MslMpLibFloorProjection* out);

// Return the source endpoint and outward adjacent line for the selected geometric side.
// side: -1 left, +1 right.
uint8_t msl_mplib_floor_endpoint(const MslBatch* batch, int bi, uint16_t line_id, int side,
                                 float* x_out, float* y_out, int16_t* adjacent_line_id_out);
