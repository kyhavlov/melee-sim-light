#include "mp_lib.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

uint8_t msl_mplib_line_intersection_h(float* ix, float* iy, float x0, float y0, float x1, float ax,
                                      float ay, float bx, float by) {
  const float min_x = x0 < x1 ? x0 : x1;
  const float max_x = x0 < x1 ? x1 : x0;
  if ((bx < min_x && ax < min_x) || (max_x < bx && max_x < ax)) {
    return 0u;
  }
  if (x0 < x1) {
    if ((double)ay - (double)y0 < -0.0001 || (double)by - (double)y0 > 0.0001) {
      return 0u;
    }
  } else if ((double)by - (double)y0 < -0.0001 || (double)ay - (double)y0 > 0.0001) {
    return 0u;
  }
  const double dy = (double)by - (double)ay;
  const double dx = (double)bx - (double)ax;
  if (fabs(dy) < 0.0001) {
    return 0u;
  }
  double x = dx / dy * ((double)y0 - (double)ay) + (double)ax;
  if (x - (double)min_x < 0.0) {
    if (x - (double)min_x < -0.1) {
      return 0u;
    }
    x = min_x;
  } else if (x - (double)max_x > 0.0) {
    if (x - (double)max_x > 0.1) {
      return 0u;
    }
    x = max_x;
  }
  *ix = (float)x;
  *iy = y0;
  return 1u;
}

uint8_t msl_mplib_line_intersection(float* ix, float* iy, float x0, float y0, float x1, float y1,
                                    float ax, float ay, float bx, float by) {
  if (x0 <= x1) {
    if ((ax < x0 && bx < x0) || (x1 < ax && x1 < bx)) {
      return 0u;
    }
  } else if ((ax < x1 && bx < x1) || (x0 < ax && x0 < bx)) {
    return 0u;
  }
  if (y0 <= y1) {
    if ((ay < y0 && by < y0) || (y1 < ay && y1 < by)) {
      return 0u;
    }
  } else if ((ay < y1 && by < y1) || (y0 < ay && y0 < by)) {
    return 0u;
  }

  const double ah = (double)y1 - (double)y0;
  const double aw = (double)x1 - (double)x0;
  const double d0x = (double)ax - (double)x0;
  const double d0y = (double)ay - (double)y0;
  const double h0 = aw * d0y - ah * d0x;
  uint8_t b0_below = 0u;
  uint8_t b1_above = 0u;
  if (h0 < 0.0) {
    if (h0 < -0.1) {
      return 0u;
    }
    b0_below = 1u;
  }
  const double d1x = (double)bx - (double)x1;
  const double d1y = (double)by - (double)y1;
  const double h1 = aw * d1y - ah * d1x;
  if (h1 > 0.0) {
    if (h1 > 0.1) {
      return 0u;
    }
    b1_above = 1u;
  }
  if (h0 == 0.0 && h1 == 0.0) {
    return 0u;
  }
  const double det = d0x * d1y - d0y * d1x;
  if ((det < h0 && det < h1) || (det > h0 && det > h1)) {
    return 0u;
  }
  const double bw = (double)bx - (double)ax;
  const double bh = (double)by - (double)ay;
  if ((bw == 0.0 && bh == 0.0) || (b0_below && b1_above) || (h0 >= 0.0 && b1_above)) {
    return 0u;
  }
  const double area = bw * ah - bh * aw;
  if (!(fabs(area) > 0.0001)) {
    return 0u;
  }
  double t = (bw * d0y - bh * d0x) / area;
  if (t < 0.0) {
    t = 0.0;
  } else if (t > 1.0) {
    t = 1.0;
  }
  *ix = (float)(aw * t + (double)x0);
  *iy = (float)(ah * t + (double)y0);
  return 1u;
}

uint8_t msl_mplib_line_intersection_v(float* ix, float* iy, float x0, float y0, float y1, float ax,
                                      float ay, float bx, float by) {
  const float min_y = y0 < y1 ? y0 : y1;
  const float max_y = y0 < y1 ? y1 : y0;
  if ((ay < min_y && by < min_y) || (max_y < ay && max_y < by)) {
    return 0u;
  }
  if (y0 < y1) {
    if ((double)bx - (double)x0 < -0.0001 || (double)ax - (double)x0 > 0.0001) {
      return 0u;
    }
  } else if ((double)ax - (double)x0 < -0.0001 || (double)bx - (double)x0 > 0.0001) {
    return 0u;
  }
  const double dx = (double)bx - (double)ax;
  if (fabs(dx) < 0.0001) {
    return 0u;
  }
  const double dy = (double)by - (double)ay;
  double y = dy / dx * ((double)x0 - (double)ax) + (double)ay;
  if (y - (double)min_y < 0.0) {
    if (y - (double)min_y < -0.1) {
      return 0u;
    }
    y = min_y;
  } else if (y - (double)max_y > 0.0) {
    if (y - (double)max_y > 0.1) {
      return 0u;
    }
    y = max_y;
  }
  *ix = x0;
  *iy = (float)y;
  return 1u;
}

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

static void remap_2d(float* x_out, float* y_out, float ax0, float ay0, float ax1, float ay1,
                     float bx0, float by0, float bx1, float by1, float px, float py) {
  const double dx = (double)ax1 - (double)ax0;
  const double dy = (double)ay1 - (double)ay0;
  const double dist2 = dx * dx + dy * dy;
  if (fabs(dist2) > 0.0001) {
    double t = (dy * ((double)py - (double)ay0) + dx * ((double)px - (double)ax0)) / dist2;
    if (t > 1.0) {
      t = 1.0;
    } else if (t < 0.0) {
      t = 0.0;
    }
    *x_out = (float)((double)px + (1.0 - t) * ((double)bx0 - (double)ax0) +
                     t * ((double)bx1 - (double)ax1));
    *y_out = (float)((double)py + (1.0 - t) * ((double)by0 - (double)ay0) +
                     t * ((double)by1 - (double)ay1));
  } else {
    *x_out = px + (bx0 - ax0) + (bx1 - ax0);
    *y_out = py + (by0 - ay0) + (by1 - ay0);
  }
}

static inline int16_t line_link_for_geometric_side(const MslStageMapLine* line, int side) {
  const uint8_t v0_is_left = (uint8_t)(line->x0 <= line->x1);
  if (side < 0) {
    return v0_is_left ? line->prev_id : line->next_id;
  }
  return v0_is_left ? line->next_id : line->prev_id;
}

static uint8_t project_floor(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                             float y, uint8_t require_fighter_solid, MslMpLibFloorProjection* out) {
  if (batch == NULL || out == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageMap* map = stage_collision_get_map(stage_id);
  uint16_t line_id = start_line_id;
  int direction = 0;
  if (map == NULL) {
    return 0u;
  }
  for (size_t step = 0; step <= map->line_count; step++) {
    const MslStageMapLine* source = stage_collision_map_line(stage_id, line_id);
    MslStageMapLine line = {0};
    if (source == NULL || source->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR ||
        (require_fighter_solid && source->fighter_solid == 0u) ||
        !stage_collision_map_line_world(batch, bi, source, &line)) {
      return 0u;
    }
    float projected_x = x;
    if (x < line.x0) {
      const int16_t next = direction != 1 ? line.prev_id : -1;
      const MslStageMapLine* linked =
          next >= 0 ? stage_collision_map_line(stage_id, (uint16_t)next) : NULL;
      if (linked == NULL || linked->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR) {
        if (direction != 1 && x - line.x0 < -0.1f) {
          return 0u;
        }
        projected_x = line.x0;
      } else {
        line_id = (uint16_t)next;
        direction = -1;
        continue;
      }
    } else if (x > line.x1) {
      const int16_t next = direction != -1 ? line.next_id : -1;
      const MslStageMapLine* linked =
          next >= 0 ? stage_collision_map_line(stage_id, (uint16_t)next) : NULL;
      if (linked == NULL || linked->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR) {
        if (direction != -1 && x - line.x1 > 0.1f) {
          return 0u;
        }
        projected_x = line.x1;
      } else {
        line_id = (uint16_t)next;
        direction = 1;
        continue;
      }
    }
    const float dx = line.x1 - line.x0;
    const float t = dx != 0.0f ? (projected_x - line.x0) / dx : 0.0f;
    const float floor_y = line.y0 + (line.y1 - line.y0) * t;
    out->line_id = line.segment_i;
    out->flags = line.lo_flags;
    out->correction_y = floor_y - y + 0.0001f;
    out->contact_x = projected_x;
    out->contact_y = floor_y;
    floor_normal(line.x0, line.y0, line.x1, line.y1, &out->normal_x, &out->normal_y);
    return 1u;
  }
  return 0u;
}

uint8_t msl_mplib_project_floor(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                float y, MslMpLibFloorProjection* out) {
  return project_floor(batch, bi, start_line_id, x, y, 0u, out);
}

uint8_t msl_mplib_project_fighter_floor(const MslBatch* batch, int bi, uint16_t start_line_id,
                                        float x, float y, MslMpLibFloorProjection* out) {
  if (batch == NULL || out == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* graph = stage_collision_get_fighter_floor_graph(stage_id);
  int line_i = stage_collision_fighter_floor_line_index(stage_id, start_line_id);
  int direction = 0;
  if (graph == NULL || line_i < 0 || (size_t)line_i >= graph->line_count) {
    return 0u;
  }
  for (size_t step = 0; step <= graph->line_count; step++) {
    const MslStageFloorLine* source = &graph->lines[(size_t)line_i];
    MslStageFloorLine line = {0};
    if (source->fighter_solid == 0u ||
        !stage_collision_floor_line_world(batch, bi, source, &line)) {
      return 0u;
    }
    float projected_x = x;
    if (x < line.x0) {
      const int16_t next = direction != 1 ? source->prev : -1;
      if (next >= 0 && (size_t)next < graph->line_count) {
        line_i = next;
        direction = -1;
        continue;
      }
      if (direction != 1 && x - line.x0 < -0.1f) {
        return 0u;
      }
      projected_x = line.x0;
    } else if (x > line.x1) {
      const int16_t next = direction != -1 ? source->next : -1;
      if (next >= 0 && (size_t)next < graph->line_count) {
        line_i = next;
        direction = 1;
        continue;
      }
      if (direction != -1 && x - line.x1 > 0.1f) {
        return 0u;
      }
      projected_x = line.x1;
    }
    const float dx = line.x1 - line.x0;
    const float t = dx != 0.0f ? (projected_x - line.x0) / dx : 0.0f;
    const float floor_y = line.y0 + (line.y1 - line.y0) * t;
    out->line_id = line.segment_i;
    out->flags = line.lo_flags;
    out->correction_y = floor_y - y + 0.0001f;
    out->contact_x = projected_x;
    out->contact_y = floor_y;
    floor_normal(line.x0, line.y0, line.x1, line.y1, &out->normal_x, &out->normal_y);
    return 1u;
  }
  // Frozen Stadium's active fighter-floor policy can connect a lip through a generated alternate
  // endpoint where the raw MapLine chain points into an inactive transformation object.
  // `mpColl_800488F4` consumes the active collision graph selected by Ground, so this fallback must
  // traverse MSLSTG01's fighter-floor graph rather than calling the raw projector a second time.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800488F4
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/stages/bin/*.bin::MSLSTG01 fighter_solid + floor endpoint links
  return 0u;
}

uint8_t msl_mplib_project_ceiling(const MslBatch* batch, int bi, uint16_t start_line_id, float x,
                                  float y, MslMpLibCeilingProjection* out) {
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
    if (source == NULL || source->kind != (uint8_t)MSL_STAGE_RAW_LINE_CEILING ||
        !stage_collision_map_line_world(batch, bi, source, &line)) {
      return 0u;
    }
    const float left_x = line.x0 < line.x1 ? line.x0 : line.x1;
    const float right_x = line.x0 > line.x1 ? line.x0 : line.x1;
    float projected_x = x;
    if (x < left_x) {
      const int16_t next = line_link_for_geometric_side(&line, -1);
      const MslStageMapLine* linked =
          next >= 0 ? stage_collision_map_line(stage_id, (uint16_t)next) : NULL;
      if (linked == NULL || linked->kind != (uint8_t)MSL_STAGE_RAW_LINE_CEILING) {
        if (x - left_x < -0.1f) {
          return 0u;
        }
        projected_x = left_x;
      } else {
        line_id = (uint16_t)next;
        continue;
      }
    } else if (x > right_x) {
      const int16_t next = line_link_for_geometric_side(&line, +1);
      const MslStageMapLine* linked =
          next >= 0 ? stage_collision_map_line(stage_id, (uint16_t)next) : NULL;
      if (linked == NULL || linked->kind != (uint8_t)MSL_STAGE_RAW_LINE_CEILING) {
        if (x - right_x > 0.1f) {
          return 0u;
        }
        projected_x = right_x;
      } else {
        line_id = (uint16_t)next;
        continue;
      }
    }
    const float dx = line.x1 - line.x0;
    const float t = dx != 0.0f ? (projected_x - line.x0) / dx : 0.0f;
    const float ceiling_y = line.y0 + (line.y1 - line.y0) * t;
    out->line_id = line.segment_i;
    out->flags = line.lo_flags;
    out->correction_y = ceiling_y - y - 0.0001f;
    out->contact_x = projected_x;
    out->contact_y = ceiling_y;
    floor_normal(line.x0, line.y0, line.x1, line.y1, &out->normal_x, &out->normal_y);
    if (out->normal_y > 0.0f) {
      out->normal_x = -out->normal_x;
      out->normal_y = -out->normal_y;
    }
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

uint8_t msl_mplib_sweep_floor_filtered(MslBatch* batch, int bi, size_t idx, float ax, float ay,
                                       float bx, float by, uint16_t floor_skip,
                                       uint8_t hard_floor_only, MslStageQueryHit* out) {
  if (batch == NULL || out == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageMap* map = stage_collision_get_map(stage_id);
  const MslStageFloorGraph* floors = stage_collision_get_floor_graph(stage_id);
  if (map == NULL || floors == NULL) {
    return 0u;
  }
  const int16_t skip_joint = batch->state.mpcoll_joint_id_skip[idx];
  const int16_t only_joint = batch->state.mpcoll_joint_id_only[idx];
  uint8_t found = 0u;
  float best_dist2 = FLT_MAX;
  MslStageQueryHit best = {0};
  // Iterate the canonical source MapLine table in stable source order. Typed floor graphs remain
  // compatibility/dynamic-transform indices; they are not a second query topology owner.
  // data/stages/bin/*.bin::MSLSTG01 line kind/segment identity
  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpCheckFloorRemap}
  for (size_t i = 0; i < map->line_count; i++) {
    const MslStageMapLine* source = &map->lines[i];
    MslStageMapLine line = {0};
    // FoD height platforms are runtime Ground/JObj owners. A fallback pose with no current-owned
    // stage state is useful for display/debug, but it is not a selectable mpLib fighter floor.
    // data/stages/bin/griz.bin::MSLSTG01 platform_transform
    // refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
    // mpCheckFloor/mpCheckFloorRemap reject line_id_skip before testing line flags; floor_skip is
    // not restricted to soft platforms even though mpUpdateFloorSkip's common caller is Pass.
    // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpCheckFloorRemap}
    if (source->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR || source->fighter_solid == 0u ||
        source->segment_i == floor_skip ||
        (hard_floor_only &&
         (stage_collision_floor_line_is_platform(stage_id, source->segment_i) ||
          stage_collision_floor_line_has_platform_transform(stage_id, source->segment_i))) ||
        (stage_collision_floor_line_has_height_platform_transform(batch->state.stage_id[(size_t)bi],
                                                                  source->segment_i) &&
         !stage_collision_floor_line_height_platform_state_is_current_owned(batch, bi,
                                                                            source->segment_i)) ||
        !stage_collision_map_line_world(batch, bi, source, &line) ||
        (skip_joint >= 0 && skip_joint == line.joint_id) ||
        (only_joint >= 0 && only_joint != line.joint_id)) {
      continue;
    }
    float x0 = line.x0;
    float y0 = line.y0;
    float x1 = line.x1;
    float y1 = line.y1;
    float ix = 0.0f;
    float iy = 0.0f;
    float remap_ax = ax;
    float remap_ay = ay;
    if (batch->state.coll_geometry_generation[idx] !=
            batch->state.stage_collision_geometry_generation[(size_t)bi] &&
        (source->platform_transform_kind == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT ||
         source->platform_transform_kind == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL)) {
      float motion_x = 0.0f;
      float motion_y = 0.0f;
      const int floor_i = stage_collision_floor_line_index(stage_id, source->segment_i);
      if (floor_i >= 0 && (size_t)floor_i < floors->line_count &&
          stage_collision_floor_line_motion_delta(batch, bi, &floors->lines[(size_t)floor_i],
                                                  &motion_x, &motion_y)) {
        remap_2d(&remap_ax, &remap_ay, x0 - motion_x, y0 - motion_y, x1 - motion_x, y1 - motion_y,
                 x0, y0, x1, y1, ax, ay);
      }
    }
    const uint8_t hit =
        fabsf(y0 - y1) > 0.0001f
            ? msl_mplib_line_intersection(&ix, &iy, x0, y0, x1, y1, remap_ax, remap_ay, bx, by)
            : (uint8_t)(remap_ay >= by && msl_mplib_line_intersection_h(
                                              &ix, &iy, x0, y0, x1, remap_ax, remap_ay, bx, by));
    if (!hit) {
      continue;
    }
    const float sweep_dx = bx - remap_ax;
    const float sweep_dy = by - remap_ay;
    const float old_dx = ix - ax;
    const float old_dy = iy - ay;
    float dist2 = old_dx * old_dx + old_dy * old_dy;
    if (sweep_dx * old_dx + sweep_dy * old_dy < 0.0f) {
      dist2 = -dist2;
    }
    if (dist2 >= best_dist2) {
      continue;
    }
    float nx = -(y1 - y0);
    float ny = x1 - x0;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len > 0.0f) {
      nx /= len;
      ny /= len;
    }
    if (ny < 0.0f) {
      nx = -nx;
      ny = -ny;
    }
    best_dist2 = dist2;
    best = (MslStageQueryHit){
        .kind = (uint8_t)MSL_STAGE_RAW_LINE_FLOOR,
        .line_idx = (int32_t)i,
        .segment_i = line.segment_i,
        .joint_id = line.joint_id,
        .flags = line.lo_flags,
        .x = ix,
        .y = iy,
        .normal_x = nx,
        .normal_y = ny,
    };
    found = 1u;
  }
  if (found) {
    *out = best;
  }
  return found;
}

uint8_t msl_mplib_sweep_floor(MslBatch* batch, int bi, size_t idx, float ax, float ay, float bx,
                              float by, uint16_t floor_skip, MslStageQueryHit* out) {
  return msl_mplib_sweep_floor_filtered(batch, bi, idx, ax, ay, bx, by, floor_skip, 0u, out);
}
