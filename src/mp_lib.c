#include "mp_lib.h"

#include <math.h>
#include <stddef.h>

static inline void floor_normal(float x0, float y0, float x1, float y1, float* nx, float* ny) {
  float out_x = -(y1 - y0);
  float out_y = x1 - x0;
  if (out_y < 0.0f) {
    out_x = -out_x;
    out_y = -out_y;
  }
  const float len = sqrtf(out_x * out_x + out_y * out_y);
  if (len > 0.0f) {
    out_x /= len;
    out_y /= len;
  } else {
    out_x = 0.0f;
    out_y = 1.0f;
  }
  *nx = out_x;
  *ny = out_y;
}

static inline int16_t line_link_for_geometric_side(const MslStageMapLine* line, int side) {
  const uint8_t v0_is_left = (uint8_t)(line->x0 <= line->x1);
  if (side < 0) {
    return v0_is_left ? line->prev_id : line->next_id;
  }
  return v0_is_left ? line->next_id : line->prev_id;
}

uint8_t msl_mplib_project_floor(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                float y, MslMpLibFloorProjection* out) {
  if (batch == NULL || out == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageMap* map = stage_collision_get_map(stage_id);
  uint16_t line_id = start_line_id;
  if (map == NULL) {
    return 0u;
  }
  for (size_t step = 0; step <= map->line_count; step++) {
    const MslStageMapLine* source = stage_collision_map_line(stage_id, line_id);
    MslStageMapLine line = {0};
    if (source == NULL || source->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR ||
        !stage_collision_map_line_world(batch, bi, source, &line)) {
      return 0u;
    }
    const float left_x = line.x0 < line.x1 ? line.x0 : line.x1;
    const float right_x = line.x0 > line.x1 ? line.x0 : line.x1;
    if (x < left_x) {
      const int16_t next = line_link_for_geometric_side(&line, -1);
      if (next < 0) {
        return 0u;
      }
      line_id = (uint16_t)next;
      continue;
    }
    if (x > right_x) {
      const int16_t next = line_link_for_geometric_side(&line, +1);
      if (next < 0) {
        return 0u;
      }
      line_id = (uint16_t)next;
      continue;
    }
    const float dx = line.x1 - line.x0;
    const float t = dx != 0.0f ? (x - line.x0) / dx : 0.0f;
    const float floor_y = line.y0 + (line.y1 - line.y0) * t;
    out->line_id = line.segment_i;
    out->flags = line.lo_flags;
    out->correction_y = floor_y - y;
    out->contact_x = x;
    out->contact_y = floor_y;
    floor_normal(line.x0, line.y0, line.x1, line.y1, &out->normal_x, &out->normal_y);
    return 1u;
  }
  return 0u;
}

uint8_t msl_mplib_floor_endpoint(const MslBatch* batch, int bi, uint16_t line_id, int side,
                                 float* x_out, float* y_out, int16_t* adjacent_line_id_out) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || (side != -1 && side != 1)) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageMapLine* source = stage_collision_map_line(stage_id, line_id);
  MslStageMapLine line = {0};
  if (source == NULL || source->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR ||
      !stage_collision_map_line_world(batch, bi, source, &line)) {
    return 0u;
  }
  const uint8_t v0_selected = (uint8_t)(side < 0 ? line.x0 <= line.x1 : line.x0 >= line.x1);
  if (x_out != NULL) {
    *x_out = v0_selected ? line.x0 : line.x1;
  }
  if (y_out != NULL) {
    *y_out = v0_selected ? line.y0 : line.y1;
  }
  if (adjacent_line_id_out != NULL) {
    *adjacent_line_id_out = line_link_for_geometric_side(&line, side);
  }
  return 1u;
}
