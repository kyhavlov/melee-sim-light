#include "mp_coll.h"

#include <float.h>
#include <math.h>

#include "coll_env_flags.h"
#include "common_params.h"
#include "input_axis.h"
#include "mp_lib.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_floor_skip.h"
#include "motion_state_owners.h"
#include "stage_collision.h"
#include "state_flags.h"

static inline uint8_t joint_admitted(const MslBatch* batch, size_t idx, int16_t joint_id) {
  const int16_t skip = batch->state.mpcoll_joint_id_skip[idx];
  const int16_t only = batch->state.mpcoll_joint_id_only[idx];
  return (uint8_t)((skip < 0 || skip != joint_id) && (only < 0 || only == joint_id));
}

static void rebuild_ecb(MslMpCollFrame* frame) {
  mpcoll_ecb_world_points_from_rel(
      &frame->ecb, frame->cur_x, frame->cur_y, frame->ecb.bottom_rel_y, frame->ecb.top_rel_y,
      frame->ecb.left_rel_x, frame->ecb.right_rel_x, frame->ecb.side_rel_y, frame->ecb.frame_u16);
}

static void save_squeeze_restore(MslBatch* batch, size_t idx, MslMpCollFrame* frame) {
  if (frame->squeezed == 0u) {
    mpcoll_store_squeeze_restore_ecb_points(batch, idx, &frame->ecb);
    frame->squeezed = 1u;
  }
}

static void squeeze_horizontal(MslBatch* batch, size_t idx, MslMpCollFrame* frame,
                               float x_after_right, float x_after_left) {
  save_squeeze_restore(batch, idx, frame);
  const float half_width =
      0.5f * (x_after_left - x_after_right + frame->ecb.right_rel_x - frame->ecb.left_rel_x);
  frame->cur_x = (x_after_left + frame->ecb.right_rel_x) - half_width;
  frame->ecb.right_rel_x = half_width;
  frame->ecb.left_rel_x = -half_width;
  frame->desired_ecb.right_rel_x = half_width;
  frame->desired_ecb.left_rel_x = -half_width;
  frame->outer_stop = 0u;
  rebuild_ecb(frame);
  mpcoll_ecb_world_points_from_rel(&frame->desired_ecb, frame->cur_x, frame->cur_y,
                                   frame->desired_ecb.bottom_rel_y, frame->desired_ecb.top_rel_y,
                                   frame->desired_ecb.left_rel_x, frame->desired_ecb.right_rel_x,
                                   frame->desired_ecb.side_rel_y, frame->desired_ecb.frame_u16);
}

void msl_mpcoll_squeeze_vertical(MslBatch* batch, size_t idx, MslMpCollFrame* frame,
                                 uint8_t airborne, float y_after_ceiling, float y_after_floor) {
  save_squeeze_restore(batch, idx, frame);
  const float height =
      y_after_ceiling - y_after_floor + frame->ecb.top_rel_y - frame->ecb.bottom_rel_y;
  if (height < 3.0f) {
    const float old_height = frame->ecb.top_rel_y - frame->ecb.bottom_rel_y;
    const float new_height = frame->ecb.top_rel_y + y_after_ceiling - y_after_floor;
    frame->ecb.top_rel_y = old_height < new_height ? old_height : new_height;
    frame->ecb.bottom_rel_y = 0.0f;
    frame->cur_y = y_after_floor;
  } else if (!airborne) {
    frame->cur_y = y_after_floor;
    frame->ecb.top_rel_y = height + frame->ecb.bottom_rel_y;
  } else {
    frame->cur_y = 0.5f * (y_after_ceiling + y_after_floor);
    frame->ecb.top_rel_y = 0.5f * (frame->ecb.top_rel_y + frame->ecb.bottom_rel_y + height);
    frame->ecb.bottom_rel_y = frame->ecb.top_rel_y - height;
  }
  frame->ecb.side_rel_y = 0.5f * (frame->ecb.top_rel_y + frame->ecb.bottom_rel_y);
  frame->desired_ecb.top_rel_y = frame->ecb.top_rel_y;
  frame->desired_ecb.bottom_rel_y = frame->ecb.bottom_rel_y;
  frame->desired_ecb.side_rel_y = frame->ecb.side_rel_y;
  frame->outer_stop = 0u;
  rebuild_ecb(frame);
  mpcoll_ecb_world_points_from_rel(&frame->desired_ecb, frame->cur_x, frame->cur_y,
                                   frame->desired_ecb.bottom_rel_y, frame->desired_ecb.top_rel_y,
                                   frame->desired_ecb.left_rel_x, frame->desired_ecb.right_rel_x,
                                   frame->desired_ecb.side_rel_y, frame->desired_ecb.frame_u16);
}

static uint8_t wall_x_at_y(const MslStageMapLine* line, float y, float* x_out) {
  if (line == NULL || x_out == NULL) {
    return 0u;
  }
  const float min_y = line->y0 < line->y1 ? line->y0 : line->y1;
  const float max_y = line->y0 < line->y1 ? line->y1 : line->y0;
  if (y < min_y - 0.1f || y > max_y + 0.1f) {
    return 0u;
  }
  const float dy = line->y1 - line->y0;
  if (fabsf(dy) <= 0.0001f) {
    return 0u;
  }
  float clamped_y = y;
  if (clamped_y < min_y) {
    clamped_y = min_y;
  } else if (clamped_y > max_y) {
    clamped_y = max_y;
  }
  *x_out = line->x0 + (line->x1 - line->x0) * ((clamped_y - line->y0) / dy);
  return 1u;
}

static uint8_t carried_wall_side_contact(const MslBatch* batch, int bi, const MslMpCollFrame* frame,
                                         uint8_t kind, uint16_t carried_id) {
  if (batch == NULL || frame == NULL || carried_id == 0xFFFFu) {
    return 0u;
  }
  const MslStageMapLine* source =
      stage_collision_map_line(batch->state.stage_id[(size_t)bi], carried_id);
  MslStageMapLine line = {0};
  if (source == NULL || source->kind != kind ||
      !stage_collision_map_line_world(batch, bi, source, &line)) {
    return 0u;
  }
  const uint8_t right_wall = (uint8_t)(kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL);
  const float side_x = right_wall ? frame->ecb.left_x : frame->ecb.right_x;
  const float side_y = right_wall ? frame->ecb.left_y : frame->ecb.right_y;
  const float prev_side_x = right_wall ? frame->prev_ecb.left_x : frame->prev_ecb.right_x;
  const float prev_side_y = right_wall ? frame->prev_ecb.left_y : frame->prev_ecb.right_y;
  float wall_x = 0.0f;
  float prev_wall_x = 0.0f;
  return (uint8_t)(wall_x_at_y(&line, side_y, &wall_x) && fabsf(side_x - wall_x) <= 0.000001f &&
                   (!wall_x_at_y(&line, prev_side_y, &prev_wall_x) ||
                    fabsf(prev_side_x - prev_wall_x) > 0.000001f));
}

static float wall_ecb_edge_x_at_y(const MslMpCollFrame* frame, uint8_t right_wall, float y) {
  const float bottom_y = frame->ecb.bottom_y;
  const float side_y = right_wall ? frame->ecb.left_y : frame->ecb.right_y;
  const float top_y = frame->ecb.top_y;
  const float side_x = right_wall ? frame->ecb.left_rel_x : frame->ecb.right_rel_x;
  if (y <= side_y) {
    const float height = side_y - bottom_y;
    return height != 0.0f ? side_x * ((y - bottom_y) / height) : side_x;
  }
  const float height = side_y - top_y;
  return height != 0.0f ? side_x * ((y - top_y) / height) : side_x;
}

typedef struct MslWallEnvelope {
  float root_x;
  float contact_x;
  float contact_y;
  int32_t line_idx;
  uint16_t segment_i;
  int16_t joint_id;
  uint16_t flags;
  float normal_x;
  float normal_y;
  uint8_t have;
} MslWallEnvelope;

typedef struct MslMapKindView {
  const MslStageMap* map;
  const MslStageWallGraph* walls;
  const MslStageCeilingGraph* ceilings;
  uint32_t stage_id;
  size_t line_count;
} MslMapKindView;

// MSLSTG01 already materializes source MapLines into no-allocation per-kind indices. Iterate that
// index and resolve each stable segment id back to the canonical MapLine instead of rescanning all
// floors, walls, and ceilings for every individual mpCheck* probe.
// data/stages/bin/*.bin::MSLSTG01 line kind/segment identity
// refs/melee/src/melee/mp/mplib.c::{mpCheckCeiling,mpCheckLeftWall,mpCheckRightWall}
static MslMapKindView map_kind_view(uint32_t stage_id, uint8_t kind) {
  MslMapKindView view = {.map = stage_collision_get_map(stage_id), .stage_id = stage_id};
  if (kind == (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL) {
    view.walls = stage_collision_get_left_wall_graph(stage_id);
    view.line_count = view.walls != NULL ? view.walls->line_count : 0u;
  } else if (kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL) {
    view.walls = stage_collision_get_right_wall_graph(stage_id);
    view.line_count = view.walls != NULL ? view.walls->line_count : 0u;
  } else if (kind == (uint8_t)MSL_STAGE_RAW_LINE_CEILING) {
    view.ceilings = stage_collision_get_ceiling_graph(stage_id);
    view.line_count = view.ceilings != NULL ? view.ceilings->line_count : 0u;
  }
  return view;
}

static const MslStageMapLine* map_kind_line(const MslMapKindView* view, size_t i) {
  if (view == NULL || view->map == NULL || i >= view->line_count) {
    return NULL;
  }
  const uint16_t segment_i =
      view->walls != NULL ? view->walls->lines[i].segment_i : view->ceilings->lines[i].segment_i;
  return stage_collision_map_line(view->stage_id, segment_i);
}

static uint8_t raw_same_kind_chain_contains(uint32_t stage_id, uint16_t start_id,
                                            uint16_t candidate_id, uint8_t kind) {
  if (start_id == candidate_id) {
    return 1u;
  }
  const MslStageMap* map = stage_collision_get_map(stage_id);
  if (map == NULL) {
    return 0u;
  }
  for (int direction = -1; direction <= 1; direction += 2) {
    uint16_t line_id = start_id;
    for (size_t n = 0; n < map->line_count; n++) {
      const MslStageMapLine* line = stage_collision_map_line(stage_id, line_id);
      if (line == NULL) {
        break;
      }
      const int16_t next = direction < 0 ? line->prev_id : line->next_id;
      if (next < 0) {
        break;
      }
      const MslStageMapLine* adjacent = stage_collision_map_line(stage_id, (uint16_t)next);
      if (adjacent == NULL || adjacent->kind != kind) {
        break;
      }
      line_id = adjacent->segment_i;
      if (line_id == candidate_id) {
        return 1u;
      }
    }
  }
  return 0u;
}

static uint8_t map_kind_view_intersects_bounds(const MslMapKindView* view, float min_x, float min_y,
                                               float max_x, float max_y) {
  if (view == NULL || view->line_count == 0u) {
    return 0u;
  }
  const float graph_min_x = view->walls != NULL ? view->walls->min_x : view->ceilings->min_x;
  const float graph_max_x = view->walls != NULL ? view->walls->max_x : view->ceilings->max_x;
  const float graph_min_y = view->walls != NULL ? view->walls->min_y : view->ceilings->min_y;
  const float graph_max_y = view->walls != NULL ? view->walls->max_y : view->ceilings->max_y;
  // mpLineIntersection{H,V} admit up to 0.1 units past an endpoint before clamping it. Preserve
  // that source tolerance in the graph-level rejection bound.
  // refs/melee/src/melee/mp/mplib.c::{mpLineIntersectionH,mpLineIntersectionV}
  return (uint8_t)(max_x >= graph_min_x - 0.1f && min_x <= graph_max_x + 0.1f &&
                   max_y >= graph_min_y - 0.1f && min_y <= graph_max_y + 0.1f);
}

static void wall_envelope_consider(const MslStageMapLine* line, int32_t line_idx,
                                   uint8_t right_wall, float candidate, float contact_x,
                                   float contact_y, MslWallEnvelope* envelope) {
  if (line == NULL || envelope == NULL ||
      (envelope->have &&
       (right_wall ? candidate <= envelope->root_x : candidate >= envelope->root_x))) {
    return;
  }
  float nx = -(line->y1 - line->y0);
  float ny = line->x1 - line->x0;
  const float length = sqrtf(nx * nx + ny * ny);
  if (length > 0.0f) {
    nx /= length;
    ny /= length;
  }
  if ((right_wall && nx < 0.0f) || (!right_wall && nx > 0.0f)) {
    nx = -nx;
    ny = -ny;
  }
  *envelope = (MslWallEnvelope){
      .root_x = candidate,
      .contact_x = contact_x,
      .contact_y = contact_y,
      .line_idx = line_idx,
      .segment_i = line->segment_i,
      .joint_id = line->joint_id,
      .flags = line->lo_flags,
      .normal_x = nx,
      .normal_y = ny,
      .have = 1u,
  };
}

static void wall_envelope_consider_line(const MslMpCollFrame* frame, const MslStageMapLine* line,
                                        int32_t line_idx, uint8_t right_wall,
                                        MslWallEnvelope* envelope) {
  const float point_y[3] = {frame->ecb.bottom_y,
                            right_wall ? frame->ecb.left_y : frame->ecb.right_y, frame->ecb.top_y};
  const float point_x[3] = {0.0f, right_wall ? frame->ecb.left_rel_x : frame->ecb.right_rel_x,
                            0.0f};
  for (int i = 0; i < 3; i++) {
    float wall_x = 0.0f;
    if (wall_x_at_y(line, point_y[i], &wall_x)) {
      wall_envelope_consider(line, line_idx, right_wall, wall_x - point_x[i], wall_x, point_y[i],
                             envelope);
    }
  }

  const float endpoint_x[2] = {line->x0, line->x1};
  const float endpoint_y[2] = {line->y0, line->y1};
  for (int i = 0; i < 2; i++) {
    if (endpoint_y[i] >= frame->ecb.bottom_y && endpoint_y[i] <= frame->ecb.top_y) {
      wall_envelope_consider(line, line_idx, right_wall,
                             endpoint_x[i] - wall_ecb_edge_x_at_y(frame, right_wall, endpoint_y[i]),
                             endpoint_x[i], endpoint_y[i], envelope);
    }
  }
}

static void wall_envelope_consider_ceiling_bridge(const MslBatch* batch, int bi,
                                                  const MslMpCollFrame* frame,
                                                  const MslStageMapLine* start, uint8_t right_wall,
                                                  MslWallEnvelope* envelope) {
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  MslStageRawLineKind bridge_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t bridge_id = 0xFFFFu;
  const uint8_t have_ceiling =
      right_wall
          ? stage_collision_raw_line_prev_non_kind(
                stage_id, start->segment_i, MSL_STAGE_RAW_LINE_RIGHT_WALL, &bridge_kind, &bridge_id)
          : stage_collision_raw_line_next_non_kind(
                stage_id, start->segment_i, MSL_STAGE_RAW_LINE_LEFT_WALL, &bridge_kind, &bridge_id);
  if (!have_ceiling || bridge_kind != MSL_STAGE_RAW_LINE_CEILING) {
    return;
  }
  MslStageRawLineKind target_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t target_id = 0xFFFFu;
  const uint8_t have_target =
      right_wall ? stage_collision_raw_line_next_non_kind(
                       stage_id, bridge_id, MSL_STAGE_RAW_LINE_CEILING, &target_kind, &target_id)
                 : stage_collision_raw_line_prev_non_kind(
                       stage_id, bridge_id, MSL_STAGE_RAW_LINE_CEILING, &target_kind, &target_id);
  const uint8_t wanted_kind =
      right_wall ? (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL : (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL;
  const MslStageMap* map = stage_collision_get_map(stage_id);
  const MslStageMapLine* target_source = have_target && target_kind == wanted_kind
                                             ? stage_collision_map_line(stage_id, target_id)
                                             : NULL;
  MslStageMapLine target = {0};
  if (map == NULL || target_source == NULL ||
      !stage_collision_map_line_world(batch, bi, target_source, &target)) {
    return;
  }
  const uint8_t start_v0_top = (uint8_t)(start->y0 >= start->y1);
  const float top_x = start_v0_top ? start->x0 : start->x1;
  const float top_y = start_v0_top ? start->y0 : start->y1;
  if (frame->ecb.top_y <= top_y) {
    return;
  }
  float nx = -(target.y1 - target.y0);
  float ny = target.x1 - target.x0;
  const float length = sqrtf(nx * nx + ny * ny);
  if (length <= 0.0f) {
    return;
  }
  nx /= length;
  ny /= length;
  if ((right_wall && nx < 0.0f) || (!right_wall && nx > 0.0f)) {
    nx = -nx;
    ny = -ny;
  }
  if (fabsf(nx) <= 0.0001f) {
    return;
  }
  const float correction =
      right_wall ? (frame->ecb.top_y - top_y) / nx * -ny + top_x - frame->ecb.top_x + 0.5f
                 : (frame->ecb.top_y - top_y) / -nx * ny + top_x - frame->ecb.top_x - 0.5f;
  wall_envelope_consider(&target, (int32_t)(target_source - map->lines), right_wall,
                         frame->cur_x + correction, top_x, top_y, envelope);
}

static uint8_t wall_commit_root_x(MslBatch* batch, int bi, const MslMpCollFrame* frame,
                                  MslStageQueryHit* hit, uint8_t right_wall, float* root_x_out) {
  if (batch == NULL || frame == NULL || hit == NULL || root_x_out == NULL || hit->line_idx < 0) {
    return 0u;
  }
  const MslStageMap* map = stage_collision_get_map(batch->state.stage_id[(size_t)bi]);
  if (map == NULL || (size_t)hit->line_idx >= map->line_count) {
    return 0u;
  }
  const MslStageMapLine* start = &map->lines[(size_t)hit->line_idx];
  // The source wall commit walks the connected same-kind line chain and all of its vertices to
  // find the limiting ECB envelope. The initial mpCheck* hit selects the chain, not a single
  // representative segment. Preserve raw MapLine order for deterministic tie-breaking.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800454A4_RightWall,
  //   mpColl_80046224_LeftWall}
  // refs/melee/src/melee/mp/mplib.c::mpLinesConnected
  MslWallEnvelope envelope = {0};
  const MslStageMapLine* chain_top_line = NULL;
  const MslStageMapLine* chain_bottom_line = NULL;
  int32_t chain_top_line_idx = -1;
  int32_t chain_bottom_line_idx = -1;
  float chain_top_x = 0.0f;
  float chain_top_y = -FLT_MAX;
  float chain_bottom_x = 0.0f;
  float chain_bottom_y = FLT_MAX;
  for (size_t i = 0; i < map->line_count; i++) {
    const MslStageMapLine* source = &map->lines[i];
    MslStageMapLine line = {0};
    if (source->kind != start->kind || source->fighter_solid == 0u ||
        !raw_same_kind_chain_contains(batch->state.stage_id[(size_t)bi], start->segment_i,
                                      source->segment_i, start->kind) ||
        !stage_collision_map_line_world(batch, bi, source, &line)) {
      continue;
    }
    wall_envelope_consider_line(frame, &line, (int32_t)i, right_wall, &envelope);
    const int top = line.y0 >= line.y1 ? 0 : 1;
    const int bottom = top ^ 1;
    const float x[2] = {line.x0, line.x1};
    const float y[2] = {line.y0, line.y1};
    if (y[top] > chain_top_y) {
      chain_top_line = source;
      chain_top_line_idx = (int32_t)i;
      chain_top_x = x[top];
      chain_top_y = y[top];
    }
    if (y[bottom] < chain_bottom_y) {
      chain_bottom_line = source;
      chain_bottom_line_idx = (int32_t)i;
      chain_bottom_x = x[bottom];
      chain_bottom_y = y[bottom];
    }
  }
  // mp{Left,Right}WallGet{Top,Bottom} walks the entire connected same-kind chain before the
  // outside-ECB endpoint case. The initial hit segment only selects that chain.
  // refs/melee/src/melee/mp/mplib.c::{mpLeftWallGetTop,mpLeftWallGetBottom,
  //   mpRightWallGetTop,mpRightWallGetBottom}
  if (chain_top_line != NULL && chain_top_y < frame->ecb.bottom_y) {
    wall_envelope_consider(chain_top_line, chain_top_line_idx, right_wall, chain_top_x, chain_top_x,
                           chain_top_y, &envelope);
  } else if (chain_bottom_line != NULL && chain_bottom_y > frame->ecb.top_y) {
    wall_envelope_consider(chain_bottom_line, chain_bottom_line_idx, right_wall, chain_bottom_x,
                           chain_bottom_x, chain_bottom_y, &envelope);
  }
  MslStageMapLine start_world = {0};
  if (stage_collision_map_line_world(batch, bi, start, &start_world)) {
    wall_envelope_consider_ceiling_bridge(batch, bi, frame, &start_world, right_wall, &envelope);
  }
  if (envelope.have) {
    *root_x_out = envelope.root_x;
    hit->line_idx = envelope.line_idx;
    hit->segment_i = envelope.segment_i;
    hit->joint_id = envelope.joint_id;
    hit->flags = envelope.flags;
    hit->x = envelope.contact_x;
    hit->y = envelope.contact_y;
    hit->normal_x = envelope.normal_x;
    hit->normal_y = envelope.normal_y;
  }
  return envelope.have;
}

static uint8_t find_line_hit(MslBatch* batch, int bi, size_t idx, uint8_t kind, float ax, float ay,
                             float bx, float by, MslStageQueryHit* out) {
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslMapKindView view = map_kind_view(stage_id, kind);
  const float min_x = ax < bx ? ax : bx;
  const float max_x = ax > bx ? ax : bx;
  const float min_y = ay < by ? ay : by;
  const float max_y = ay > by ? ay : by;
  if (view.map == NULL || out == NULL ||
      !map_kind_view_intersects_bounds(&view, min_x, min_y, max_x, max_y)) {
    return 0u;
  }
  uint8_t found = 0u;
  float best_dist2 = FLT_MAX;
  MslStageQueryHit best = {0};
  for (size_t i = 0; i < view.line_count; i++) {
    const MslStageMapLine* source = map_kind_line(&view, i);
    MslStageMapLine line = {0};
    if (source == NULL || source->fighter_solid == 0u ||
        !stage_collision_map_line_world(batch, bi, source, &line) ||
        !joint_admitted(batch, idx, line.joint_id)) {
      continue;
    }
    float ix = 0.0f;
    float iy = 0.0f;
    uint8_t hit = 0u;
    if (kind == (uint8_t)MSL_STAGE_RAW_LINE_CEILING && fabsf(line.y0 - line.y1) <= 0.0001f) {
      hit = (uint8_t)(ay <= by && msl_mplib_line_intersection_h(&ix, &iy, line.x0, line.y0, line.x1,
                                                                ax, ay, bx, by));
    } else if ((kind == (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL ||
                kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL) &&
               fabsf(line.x0 - line.x1) <= 0.0001f) {
      const uint8_t correct_direction =
          kind == (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL ? (uint8_t)(ax <= bx) : (uint8_t)(ax >= bx);
      hit = (uint8_t)(correct_direction && msl_mplib_line_intersection_v(&ix, &iy, line.x0, line.y0,
                                                                         line.y1, ax, ay, bx, by));
    } else {
      hit =
          msl_mplib_line_intersection(&ix, &iy, line.x0, line.y0, line.x1, line.y1, ax, ay, bx, by);
    }
    if (!hit) {
      continue;
    }
    const float dx = ix - ax;
    const float dy = iy - ay;
    const float dist2 = dx * dx + dy * dy;
    if (dist2 >= best_dist2) {
      continue;
    }
    float nx = -(line.y1 - line.y0);
    float ny = line.x1 - line.x0;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len > 0.0f) {
      nx /= len;
      ny /= len;
    }
    if (kind == (uint8_t)MSL_STAGE_RAW_LINE_CEILING && ny > 0.0f) {
      nx = -nx;
      ny = -ny;
    } else if (kind == (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL && nx > 0.0f) {
      nx = -nx;
      ny = -ny;
    } else if (kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL && nx < 0.0f) {
      nx = -nx;
      ny = -ny;
    }
    best_dist2 = dist2;
    best = (MslStageQueryHit){
        .kind = kind,
        .line_idx = (int32_t)(source - view.map->lines),
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

static uint8_t find_wall_quad_hit(MslBatch* batch, int bi, size_t idx, uint8_t kind, float ax,
                                  float ay, float bx, float by, float cx, float cy, float dx,
                                  float dy, MslStageQueryHit* out) {
  // Detect a wall vertex crossing the swept quadrilateral between the previous and current ECB
  // edge. Point sweeps alone miss this when both endpoints move around the vertex.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_800511A4_RightWall,mpLib_800515A0_LeftWall}
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslMapKindView view = map_kind_view(stage_id, kind);
  const float min_x = fminf(fminf(ax, bx), fminf(cx, dx));
  const float max_x = fmaxf(fmaxf(ax, bx), fmaxf(cx, dx));
  const float min_y = fminf(fminf(ay, by), fminf(cy, dy));
  const float max_y = fmaxf(fmaxf(ay, by), fmaxf(cy, dy));
  if (view.map == NULL || out == NULL ||
      !map_kind_view_intersects_bounds(&view, min_x, min_y, max_x, max_y)) {
    return 0u;
  }
  uint8_t found = 0u;
  float best_dist2 = FLT_MAX;
  MslStageQueryHit best = {0};
  for (size_t i = 0; i < view.line_count; i++) {
    const MslStageMapLine* source = map_kind_line(&view, i);
    MslStageMapLine line = {0};
    if (source == NULL || source->fighter_solid == 0u ||
        !stage_collision_map_line_world(batch, bi, source, &line) ||
        !joint_admitted(batch, idx, line.joint_id)) {
      continue;
    }
    const float endpoint_x[2] = {line.x0, line.x1};
    const float endpoint_y[2] = {line.y0, line.y1};
    for (int endpoint = 0; endpoint < 2; endpoint++) {
      float mapped_x = 0.0f;
      float mapped_y = 0.0f;
      remap_2d(&mapped_x, &mapped_y, ax, ay, bx, by, cx, cy, dx, dy, endpoint_x[endpoint],
               endpoint_y[endpoint]);
      const float vx = endpoint_x[endpoint] - mapped_x;
      const float vy = endpoint_y[endpoint] - mapped_y;
      if (vx * vx + vy * vy <= 0.001f) {
        continue;
      }
      float ix = 0.0f;
      float iy = 0.0f;
      if (!msl_mplib_line_intersection(&ix, &iy, cx, cy, dx, dy, mapped_x, mapped_y,
                                       endpoint_x[endpoint], endpoint_y[endpoint])) {
        continue;
      }
      float dist2 = (ix - endpoint_x[endpoint]) * (ix - endpoint_x[endpoint]) +
                    (iy - endpoint_y[endpoint]) * (iy - endpoint_y[endpoint]);
      if (vx * (ix - endpoint_x[endpoint]) + vy * (iy - endpoint_y[endpoint]) < 0.0f) {
        dist2 = -dist2;
      }
      if (dist2 >= best_dist2) {
        continue;
      }
      float nx = -(line.y1 - line.y0);
      float ny = line.x1 - line.x0;
      const float length = sqrtf(nx * nx + ny * ny);
      if (length > 0.0f) {
        nx /= length;
        ny /= length;
      }
      const uint8_t right_wall = (uint8_t)(kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL);
      if ((right_wall && nx < 0.0f) || (!right_wall && nx > 0.0f)) {
        nx = -nx;
        ny = -ny;
      }
      best_dist2 = dist2;
      best = (MslStageQueryHit){
          .kind = kind,
          .line_idx = (int32_t)(source - view.map->lines),
          .segment_i = line.segment_i,
          .joint_id = line.joint_id,
          .flags = line.lo_flags,
          .x = ix,
          .y = iy,
          .normal_x = nx,
          .normal_y = ny,
          .dist2 = dist2,
      };
      found = 1u;
    }
  }
  if (found) {
    *out = best;
  }
  return found;
}

static uint8_t grounded_bottom_wall_is_floor_join(const MslBatch* batch, int bi,
                                                  const MslMpCollFrame* frame,
                                                  const MslStageQueryHit* hit) {
  // Grounded wall checks exclude the wall lines terminating the carried floor from bottom-point
  // and bottom-edge admission; the side/top checks remain authoritative for real wall overlap.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,
  // mpColl_80049778_LeftWall}
  if (batch == NULL || frame == NULL || hit == NULL || !frame->grounded ||
      frame->floor_id == 0xFFFFu) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageMapLine* floor = stage_collision_map_line(stage_id, frame->floor_id);
  const int16_t floor_links[4] = {
      floor != NULL ? floor->prev_id : -1,
      floor != NULL ? floor->next_id : -1,
      floor != NULL ? floor->prev_alt_id : -1,
      floor != NULL ? floor->next_alt_id : -1,
  };
  for (size_t i = 0; floor != NULL && i < 4u; i++) {
    const int16_t linked_id = floor_links[i];
    if (linked_id < 0) {
      continue;
    }
    const MslStageMapLine* linked = stage_collision_map_line(stage_id, (uint16_t)linked_id);
    const uint8_t direct = (uint8_t)(linked_id == (int16_t)hit->segment_i);
    const uint8_t next_wall = (uint8_t)(linked != NULL && linked->kind == hit->kind &&
                                        (linked->prev_id == (int16_t)hit->segment_i ||
                                         linked->next_id == (int16_t)hit->segment_i));
    if (!direct && !next_wall) {
      continue;
    }
    // The grounded RightWall/LeftWall callbacks exclude both non-floor lines directly attached to
    // CollData.floor and the second same-side wall selected through that endpoint from bottom-point
    // and bottom-edge admission. Preserve the raw MapLine ids here: the normalized floor graph can
    // reverse an endpoint for query ordering, while source obtains these identities before running
    // either grounded wall body.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,
    // mpColl_80049778_LeftWall}
    return 1u;
  }
  MslStageRawLineKind kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t segment_i = 0xFFFFu;
  if (stage_collision_raw_line_prev_non_kind(stage_id, frame->floor_id, MSL_STAGE_RAW_LINE_FLOOR,
                                             &kind, &segment_i) &&
      kind == hit->kind && segment_i == hit->segment_i) {
    return 1u;
  }
  kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  segment_i = 0xFFFFu;
  return (uint8_t)(stage_collision_raw_line_next_non_kind(
                       stage_id, frame->floor_id, MSL_STAGE_RAW_LINE_FLOOR, &kind, &segment_i) &&
                   kind == hit->kind && segment_i == hit->segment_i);
}

static uint8_t resolve_wall(MslBatch* batch, int bi, size_t idx, MslMpCollFrame* frame,
                            uint8_t kind, uint8_t carried_hug, uint8_t admit_projected_side) {
  const uint8_t right_wall = (uint8_t)(kind == (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL);
  const uint32_t side_mask =
      right_wall ? (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK : (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
  const uint8_t current_coordinator_contact = (uint8_t)((frame->env_flags & side_mask) != 0u);
  if (frame->grounded && fabsf(frame->cur_x - frame->prev_x) <= 0.0001f &&
      batch->state.prev_action_id[idx] != batch->state.action_id[idx]) {
    // A grounded MotionState handoff with no root displacement does not acquire a new wall solely
    // from the destination animation's wider ECB. The grounded wrapper carries floor ownership
    // through the handoff; later root motion or an already-carried wall can establish contact.
    // This mirrors the live Guard -> GuardOff/other same-floor callback boundary instead of
    // treating animation-envelope growth as horizontal fighter motion.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,mpColl_80049778_LeftWall}
    return 0u;
  }
  MslStageQueryHit candidates[9] = {{0}};
  uint8_t candidate_count = 0u;
  MslStageQueryHit query = {0};
  const uint16_t carried_id = right_wall ? frame->right_wall_id : frame->left_wall_id;
  if (carried_id != 0xFFFFu) {
    const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
    const MslStageMap* map = stage_collision_get_map(stage_id);
    const MslStageMapLine* source = stage_collision_map_line(stage_id, carried_id);
    MslStageMapLine line = {0};
    if (map != NULL && source != NULL && source->kind == kind && source->fighter_solid != 0u &&
        joint_admitted(batch, idx, source->joint_id) &&
        stage_collision_map_line_world(batch, bi, source, &line)) {
      // Source CollData retains the current wall index across callbacks while the fighter remains
      // pressed into that shell. Actual separation drops the carried candidate; fresh point/edge
      // sweeps below still admit a different wall crossed during the same motion.
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800454A4_RightWall,
      // mpColl_80046224_LeftWall}
      query = (MslStageQueryHit){
          .kind = kind,
          .line_idx = (int32_t)(source - map->lines),
          .segment_i = carried_id,
          .joint_id = line.joint_id,
          .flags = line.lo_flags,
      };
      candidates[candidate_count++] = query;
    }
  }
  const uint8_t swept_side =
      right_wall
          ? find_line_hit(batch, bi, idx, kind, frame->prev_ecb.left_x, frame->prev_ecb.left_y,
                          frame->ecb.left_x, frame->ecb.left_y, &query)
          : find_line_hit(batch, bi, idx, kind, frame->prev_ecb.right_x, frame->prev_ecb.right_y,
                          frame->ecb.right_x, frame->ecb.right_y, &query);
  if (swept_side) {
    candidates[candidate_count++] = query;
  }
  const uint8_t side_hit =
      (uint8_t)(swept_side || carried_hug ||
                (admit_projected_side &&
                 carried_wall_side_contact(batch, bi, frame, kind, carried_id)));
  // Only the moving side point owns WallHug. Bottom/top motion and current ECB edges are physical
  // WallPush candidates, but DamageFly must not consume them as wall-tech contact.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044E10_RightWall,
  // mpColl_80045B74_LeftWall}
  if (find_line_hit(batch, bi, idx, kind, frame->prev_ecb.bottom_x, frame->prev_ecb.bottom_y,
                    frame->ecb.bottom_x, frame->ecb.bottom_y, &query) &&
      !grounded_bottom_wall_is_floor_join(batch, bi, frame, &query)) {
    candidates[candidate_count++] = query;
  }
  if (find_line_hit(batch, bi, idx, kind, frame->prev_ecb.top_x, frame->prev_ecb.top_y,
                    frame->ecb.top_x, frame->ecb.top_y, &query)) {
    candidates[candidate_count++] = query;
  }
  const uint8_t bottom_edge_hit =
      right_wall ? find_line_hit(batch, bi, idx, kind, frame->ecb.bottom_x, frame->ecb.bottom_y,
                                 frame->ecb.left_x, frame->ecb.left_y, &query)
                 : find_line_hit(batch, bi, idx, kind, frame->ecb.bottom_x, frame->ecb.bottom_y,
                                 frame->ecb.right_x, frame->ecb.right_y, &query);
  if (bottom_edge_hit && !grounded_bottom_wall_is_floor_join(batch, bi, frame, &query)) {
    candidates[candidate_count++] = query;
  }
  const uint8_t bottom_quad_hit =
      right_wall
          ? find_wall_quad_hit(batch, bi, idx, kind, frame->prev_ecb.bottom_x,
                               frame->prev_ecb.bottom_y, frame->prev_ecb.left_x,
                               frame->prev_ecb.left_y, frame->ecb.bottom_x, frame->ecb.bottom_y,
                               frame->ecb.left_x, frame->ecb.left_y, &query)
          : find_wall_quad_hit(batch, bi, idx, kind, frame->prev_ecb.right_x,
                               frame->prev_ecb.right_y, frame->prev_ecb.bottom_x,
                               frame->prev_ecb.bottom_y, frame->ecb.right_x, frame->ecb.right_y,
                               frame->ecb.bottom_x, frame->ecb.bottom_y, &query);
  if (bottom_quad_hit && !grounded_bottom_wall_is_floor_join(batch, bi, frame, &query)) {
    candidates[candidate_count++] = query;
  }
  const uint8_t top_edge_hit =
      right_wall ? find_line_hit(batch, bi, idx, kind, frame->ecb.top_x, frame->ecb.top_y,
                                 frame->ecb.left_x, frame->ecb.left_y, &query)
                 : find_line_hit(batch, bi, idx, kind, frame->ecb.top_x, frame->ecb.top_y,
                                 frame->ecb.right_x, frame->ecb.right_y, &query);
  if (top_edge_hit) {
    candidates[candidate_count++] = query;
  }
  const uint8_t top_quad_hit =
      right_wall
          ? find_wall_quad_hit(batch, bi, idx, kind, frame->prev_ecb.left_x, frame->prev_ecb.left_y,
                               frame->prev_ecb.top_x, frame->prev_ecb.top_y, frame->ecb.left_x,
                               frame->ecb.left_y, frame->ecb.top_x, frame->ecb.top_y, &query)
          : find_wall_quad_hit(batch, bi, idx, kind, frame->prev_ecb.top_x, frame->prev_ecb.top_y,
                               frame->prev_ecb.right_x, frame->prev_ecb.right_y, frame->ecb.top_x,
                               frame->ecb.top_y, frame->ecb.right_x, frame->ecb.right_y, &query);
  if (top_quad_hit) {
    candidates[candidate_count++] = query;
  }
  if (candidate_count == 0u) {
    return 0u;
  }
  if (frame->grounded) {
    // Every successful grounded wall check sets x34.b5 before its commit; a subsequent squeeze
    // clears it. The outer subdivision driver consumes the final value.
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
    frame->outer_stop = 1u;
  }

  MslStageQueryHit hit = candidates[0];
  float target_root_x = right_wall ? -FLT_MAX : FLT_MAX;
  uint8_t have_target = 0u;
  for (uint8_t i = 0u; i < candidate_count; i++) {
    MslStageQueryHit candidate_hit = candidates[i];
    float candidate_root_x = frame->cur_x;
    if (!wall_commit_root_x(batch, bi, frame, &candidate_hit, right_wall, &candidate_root_x) ||
        (have_target &&
         (right_wall ? candidate_root_x <= target_root_x : candidate_root_x >= target_root_x))) {
      continue;
    }
    target_root_x = candidate_root_x;
    hit = candidate_hit;
    have_target = 1u;
  }
  if (!have_target) {
    return 0u;
  }
  const float root_x_before = frame->cur_x;
  uint8_t corrected = 0u;
  if ((right_wall && frame->cur_x < target_root_x) ||
      (!right_wall && frame->cur_x > target_root_x)) {
    frame->cur_x = target_root_x;
    corrected = 1u;
  } else if (!((side_hit || current_coordinator_contact) &&
               fabsf(frame->cur_x - target_root_x) <= 0.0001f)) {
    return 0u;
  }
  rebuild_ecb(frame);
  if (corrected) {
    // WallPush is the positional-correction result. A side point already lying on its carried wall
    // publishes WallHug without manufacturing a new Push event; DamageFly's tech callback consumes
    // that distinction directly.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800454A4_RightWall,
    //   mpColl_80046224_LeftWall,mpColl_800491C8_RightWall,mpColl_80049EAC_LeftWall}
    frame->env_flags |=
        right_wall ? (uint32_t)MSL_COLLIDE_RIGHT_WALL_PUSH : (uint32_t)MSL_COLLIDE_LEFT_WALL_PUSH;
  }
  if (side_hit) {
    frame->env_flags |=
        right_wall ? (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG : (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG;
  }
  if (right_wall) {
    frame->right_wall_id = hit.segment_i;
  } else {
    frame->left_wall_id = hit.segment_i;
  }
  batch->state.wall_kind[idx] = right_wall ? 2u : 1u;
  batch->state.wall_id[idx] = hit.segment_i;
  batch->state.wall_contact_x[idx] = hit.x;
  batch->state.wall_contact_y[idx] = hit.y;
  batch->state.wall_normal_x[idx] = hit.normal_x;
  batch->state.wall_normal_y[idx] = hit.normal_y;
  batch->state.coll_wall_commit_runtime[idx] = 1u;
  const float correction_x = frame->cur_x - root_x_before;
  if (batch->state.coll_wall_probe_valid[idx] == 0u ||
      fabsf(correction_x) > fabsf(batch->state.coll_wall_probe_corr_x[idx])) {
    batch->state.coll_wall_probe_valid[idx] = 1u;
    batch->state.coll_wall_probe_side[idx] = right_wall ? 2u : 1u;
    batch->state.coll_wall_probe_commit_kind[idx] = 1u;
    batch->state.coll_wall_probe_candidate_count[idx] = candidate_count;
    batch->state.coll_wall_probe_segment_id[idx] = (int16_t)hit.segment_i;
    batch->state.coll_wall_probe_corr_x[idx] = correction_x;
  }
  return 1u;
}

uint8_t msl_mpcoll_resolve_ceiling(MslBatch* batch, int bi, size_t idx, MslMpCollFrame* frame) {
  MslStageQueryHit hit = {0};
  if (batch == NULL || frame == NULL) {
    return 0u;
  }
  // Ceiling check owns direct top-point Hug plus wall-linked Push; ceiling commit then projects
  // vertically along the carried ceiling chain or publishes its joined endpoint.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044AD8_Ceiling,mpColl_80044C74_Ceiling}
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const uint8_t direct =
      find_line_hit(batch, bi, idx, (uint8_t)MSL_STAGE_RAW_LINE_CEILING, frame->prev_ecb.top_x,
                    frame->prev_ecb.top_y, frame->ecb.top_x, frame->ecb.top_y, &hit);
  if (!direct) {
    MslStageRawLineKind linked_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    uint16_t linked_id = 0xFFFFu;
    const uint8_t linked =
        (uint8_t)((frame->left_wall_id != 0xFFFFu &&
                   stage_collision_raw_line_next_non_kind(stage_id, frame->left_wall_id,
                                                          MSL_STAGE_RAW_LINE_LEFT_WALL,
                                                          &linked_kind, &linked_id)) ||
                  (frame->right_wall_id != 0xFFFFu &&
                   stage_collision_raw_line_prev_non_kind(stage_id, frame->right_wall_id,
                                                          MSL_STAGE_RAW_LINE_RIGHT_WALL,
                                                          &linked_kind, &linked_id)));
    MslMpLibCeilingProjection linked_projection = {0};
    if (!linked || linked_kind != MSL_STAGE_RAW_LINE_CEILING ||
        !msl_mplib_project_ceiling(batch, bi, linked_id, frame->ecb.top_x, frame->ecb.top_y,
                                   &linked_projection) ||
        linked_projection.correction_y >= 0.0f) {
      return 0u;
    }
    hit = (MslStageQueryHit){
        .kind = MSL_STAGE_RAW_LINE_CEILING,
        .segment_i = linked_projection.line_id,
        .flags = linked_projection.flags,
        .x = linked_projection.contact_x,
        .y = linked_projection.contact_y,
        .normal_x = linked_projection.normal_x,
        .normal_y = linked_projection.normal_y,
    };
  }

  MslMpLibCeilingProjection projection = {0};
  if (msl_mplib_project_ceiling(batch, bi, hit.segment_i, frame->ecb.top_x, frame->ecb.top_y,
                                &projection)) {
    frame->cur_y += projection.correction_y;
    hit.segment_i = projection.line_id;
    hit.flags = projection.flags;
    hit.x = projection.contact_x;
    hit.y = projection.contact_y;
    hit.normal_x = projection.normal_x;
    hit.normal_y = projection.normal_y;
  } else {
    const MslStageMapLine* source = stage_collision_map_line(stage_id, hit.segment_i);
    MslStageMapLine line = {0};
    if (source == NULL || !stage_collision_map_line_world(batch, bi, source, &line)) {
      return 0u;
    }
    const uint8_t v0_is_left = (uint8_t)(line.x0 <= line.x1);
    const float left_x = v0_is_left ? line.x0 : line.x1;
    const float left_y = v0_is_left ? line.y0 : line.y1;
    const float right_x = v0_is_left ? line.x1 : line.x0;
    const float right_y = v0_is_left ? line.y1 : line.y0;
    const int side = frame->ecb.top_x <= left_x ? -1 : 1;
    const float edge_x = side < 0 ? left_x : right_x;
    const float edge_y = side < 0 ? left_y : right_y;
    const int16_t adjacent = side < 0 ? (v0_is_left ? line.prev_id : line.next_id)
                                      : (v0_is_left ? line.next_id : line.prev_id);
    MslStageRawLineKind adjacent_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    const uint8_t wall_join =
        (uint8_t)(adjacent >= 0 &&
                  stage_collision_raw_line_kind(stage_id, (uint16_t)adjacent, &adjacent_kind) &&
                  (side < 0 ? adjacent_kind == MSL_STAGE_RAW_LINE_RIGHT_WALL
                            : adjacent_kind == MSL_STAGE_RAW_LINE_LEFT_WALL));
    frame->cur_y = edge_y - frame->ecb.top_rel_y;
    if (wall_join) {
      frame->cur_x = edge_x;
    }
    hit.x = edge_x;
    hit.y = edge_y;
  }
  rebuild_ecb(frame);
  frame->env_flags |= (uint32_t)MSL_COLLIDE_CEILING_PUSH;
  if (direct) {
    frame->env_flags |= (uint32_t)MSL_COLLIDE_CEILING_HUG;
  }
  batch->state.ceiling_id[idx] = hit.segment_i;
  batch->state.ceiling_contact_x[idx] = hit.x;
  batch->state.ceiling_contact_y[idx] = hit.y;
  batch->state.ceiling_normal_x[idx] = hit.normal_x;
  batch->state.ceiling_normal_y[idx] = hit.normal_y;
  return 1u;
}

uint8_t msl_mpcoll_resolve_walls_ceiling(MslBatch* batch, int bi, size_t idx,
                                         MslMpCollFrame* frame) {
  if (batch == NULL || frame == NULL) {
    return 0u;
  }
  // Repeated wall passes may reuse the selected wall index for projection, but only a Hug from the
  // previous completed callback is carried as side contact. A bottom/top push selected earlier in
  // this same coordinator must not become WallHug merely because the wall loop repeats.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80045B74_LeftWall,
  //   mpColl_80044E10_RightWall,mpColl_80046904}
  const uint8_t carried_left_hug =
      (uint8_t)(frame->left_wall_id != 0xFFFFu &&
                (frame->prev_env_flags & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u);
  const uint8_t carried_right_hug =
      (uint8_t)(frame->right_wall_id != 0xFFFFu &&
                (frame->prev_env_flags & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u);
  const uint8_t entered_with_left_wall = (uint8_t)(frame->left_wall_id != 0xFFFFu);
  const uint8_t entered_with_right_wall = (uint8_t)(frame->right_wall_id != 0xFFFFu);
  // DamageFly's ordinary fixed-pose callbacks require a literal side sweep for WallHug. The
  // DamageFlyRoll callback is the exception here because its rotated JObj envelope is rebuilt
  // between the two checks; an exact side endpoint after projection is part of that live packet.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doFlyRoll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80046904}
  const uint8_t projected_side_owner =
      (uint8_t)(!msl_coll_handler_is_damage(batch->state.live_coll_handler_kind[idx]) ||
                mpcoll_ground_damageflyroll_uses_jobj_ecb(batch->state.action_id[idx]));
  uint8_t left = resolve_wall(batch, bi, idx, frame, (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL,
                              carried_left_hug, 0u);
  float x_after_left = frame->cur_x;
  uint8_t right = resolve_wall(batch, bi, idx, frame, (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL,
                               carried_right_hug, 0u);
  float x_after_right = frame->cur_x;
  // The second source pass rechecks the side sweep after the first pass projects the root. Admit
  // an exact projected endpoint only for a wall acquired by this coordinator: a wall carried into
  // the callback still needs a new side sweep (or its completed-callback Hug bit) and must not turn
  // a prior bottom/top push into a wall-tech contact on the following frame.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
  left |=
      resolve_wall(batch, bi, idx, frame, (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL, carried_left_hug,
                   (uint8_t)(projected_side_owner && left && !entered_with_left_wall));
  if (left) {
    x_after_left = frame->cur_x;
  }
  right |=
      resolve_wall(batch, bi, idx, frame, (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL, carried_right_hug,
                   (uint8_t)(projected_side_owner && right && !entered_with_right_wall));
  if (right) {
    x_after_right = frame->cur_x;
  }
  if (!left) {
    frame->env_flags &= ~(uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
  }
  if (!right) {
    frame->env_flags &= ~(uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
  }
  if (left && right) {
    // Source narrows both live and desired ECBs and reruns collision until the squeeze flags
    // converge. Preserve x64_ecb exactly once for the next interpolation restore.
    // refs/melee/src/melee/mp/mpcoll.c::{mpCollSqueezeHorizontal,mpCollInterpolateECB}
    squeeze_horizontal(batch, idx, frame, x_after_right, x_after_left);
  }
  return msl_mpcoll_resolve_ceiling(batch, bi, idx, frame);
}

static MslMpCollAirStepResult resolve_air_step_once(MslBatch* batch, int bi, size_t idx,
                                                    MslMpCollFrame* frame, uint16_t floor_skip,
                                                    uint8_t stay_airborne, uint8_t platform_pass,
                                                    uint8_t hard_floor_only) {
  MslMpCollAirStepResult result = {.floor_id = frame != NULL ? frame->floor_id : 0xFFFFu};
  if (batch == NULL || frame == NULL) {
    return result;
  }
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
  frame->grounded = 0u;
  const uint8_t ceiling_hit = msl_mpcoll_resolve_walls_ceiling(batch, bi, idx, frame);
  if ((frame->env_flags & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u) {
    result.convergence_flags |= 8u;
  }
  if ((frame->env_flags & (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK) != 0u) {
    result.convergence_flags |= 4u;
  }
  if (ceiling_hit) {
    result.convergence_flags |= 1u;
  }

  MslStageQueryHit hit = {0};
  const float floor_prev_bottom_x =
      frame->floor_prev_bottom_valid ? frame->floor_prev_bottom_x : frame->prev_ecb.bottom_x;
  const float floor_prev_bottom_y =
      frame->floor_prev_bottom_valid ? frame->floor_prev_bottom_y : frame->prev_ecb.bottom_y;
  uint8_t found = msl_mplib_sweep_floor_filtered(
      batch, bi, idx, floor_prev_bottom_x, floor_prev_bottom_y, frame->ecb.bottom_x,
      frame->ecb.bottom_y, floor_skip, hard_floor_only, &hit);
  const uint8_t root_floor_owner = (uint8_t)(batch->state.live_coll_handler_kind[idx] ==
                                             (uint8_t)MSL_COLL_HANDLER_AIR_FALL_SPECIAL);
  if (!found && root_floor_owner && frame->ecb.bottom_rel_y > 0.0f) {
    // FallSpecial uses a wrapper whose positive-bottom JObj floor commit resolves from
    // Fighter.cur_pos, not from the visible bottom joint. Preserve that root-owned landing sweep
    // together with the source near-contact admission boundary.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044838_Floor}
    MslStageQueryHit root_hit = {0};
    if (msl_mplib_sweep_floor(batch, bi, idx, frame->prev_x, frame->prev_y, frame->cur_x,
                              frame->cur_y, floor_skip, &root_hit) &&
        frame->ecb.bottom_y <= root_hit.y + 0.1f) {
      hit = root_hit;
      found = 1u;
    }
  }
  if (!found) {
    const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
    MslStageRawLineKind linked_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    uint16_t linked_floor = 0xFFFFu;
    const uint8_t linked =
        (uint8_t)(((frame->env_flags & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u &&
                   frame->left_wall_id != 0xFFFFu &&
                   stage_collision_raw_line_next_non_kind(stage_id, frame->left_wall_id,
                                                          MSL_STAGE_RAW_LINE_LEFT_WALL,
                                                          &linked_kind, &linked_floor)) ||
                  ((frame->env_flags & (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK) != 0u &&
                   frame->right_wall_id != 0xFFFFu &&
                   stage_collision_raw_line_prev_non_kind(stage_id, frame->right_wall_id,
                                                          MSL_STAGE_RAW_LINE_RIGHT_WALL,
                                                          &linked_kind, &linked_floor)));
    MslMpLibFloorProjection linked_projection = {0};
    const uint8_t linked_soft =
        (uint8_t)(linked && linked_kind == MSL_STAGE_RAW_LINE_FLOOR &&
                  (stage_collision_floor_line_is_platform(stage_id, linked_floor) ||
                   stage_collision_floor_line_has_platform_transform(stage_id, linked_floor)));
    if (linked && linked_kind == MSL_STAGE_RAW_LINE_FLOOR && !(hard_floor_only && linked_soft)) {
      if (msl_mplib_project_floor(batch, bi, linked_floor, frame->ecb.bottom_x, frame->ecb.bottom_y,
                                  &linked_projection) &&
          linked_projection.correction_y >= 0.0f &&
          floor_prev_bottom_y >= linked_projection.contact_y) {
        hit = (MslStageQueryHit){
            .kind = MSL_STAGE_RAW_LINE_FLOOR,
            .segment_i = linked_projection.line_id,
            .flags = linked_projection.flags,
            .x = linked_projection.contact_x,
            .y = linked_projection.contact_y,
            .normal_x = linked_projection.normal_x,
            .normal_y = linked_projection.normal_y,
        };
        found = 1u;
      } else {
        // A wall-held ECB can descend through the exact floor/wall joint with its bottom point a
        // few ULPs outside the floor span. Source mpColl follows the wall adjacency and admits the
        // shared endpoint; a standalone floor projection correctly rejects that outside point.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800454A4_RightWall,
        // mpColl_80046224_LeftWall,mpColl_80044628_Floor}
        float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
        int16_t la = -1, ra = -1;
        if (msl_mplib_floor_endpoint(batch, bi, linked_floor, -1, &lx, &ly, &la) &&
            msl_mplib_floor_endpoint(batch, bi, linked_floor, +1, &rx, &ry, &ra)) {
          const uint8_t use_left =
              (uint8_t)(fabsf(frame->ecb.bottom_x - lx) <= fabsf(frame->ecb.bottom_x - rx));
          const float edge_x = use_left ? lx : rx;
          const float edge_y = use_left ? ly : ry;
          if (fabsf(frame->ecb.bottom_x - edge_x) <= 0.0001f && floor_prev_bottom_y >= edge_y &&
              frame->ecb.bottom_y <= edge_y) {
            hit = (MslStageQueryHit){
                .kind = MSL_STAGE_RAW_LINE_FLOOR,
                .segment_i = linked_floor,
                .x = edge_x,
                .y = edge_y,
                .normal_y = 1.0f,
            };
            found = 1u;
          }
        }
      }
    }
  }
  if (!found) {
    return result;
  }

  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const uint8_t soft_floor =
      (uint8_t)(stage_collision_floor_line_is_platform(stage_id, hit.segment_i) ||
                stage_collision_floor_line_has_platform_transform(stage_id, hit.segment_i));
  if (platform_pass && soft_floor) {
    const MslCommonParams* common = msl_common_params();
    if (common != NULL && stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                              common->platform_air_land_stick_y_threshold) {
      msl_mpcoll_update_floor_skip(batch, idx, hit.segment_i);
      return result;
    }
  }

  result.floor_id = hit.segment_i;
  result.floor_contact = 1u;
  result.floor_contact_soft = soft_floor;
  result.convergence_flags |= 2u;
  frame->floor_id = hit.segment_i;
  frame->env_flags |= (uint32_t)(MSL_COLLIDE_FLOOR_PUSH | MSL_COLLIDE_FLOOR_HUG);
  batch->state.ground_contact_x[idx] = hit.x;
  batch->state.ground_contact_y[idx] = hit.y;
  batch->state.ground_normal_x[idx] = hit.normal_x;
  batch->state.ground_normal_y[idx] = hit.normal_y;

  MslMpLibFloorProjection projection = {0};
  const uint8_t ceiling_contact = ceiling_hit;
  // Both source floor continuations project from the fighter root when the live ECB bottom is
  // above it. mpColl_80044838_Floor receives that choice as `ignore_bottom` for ordinary landing;
  // mpColl_80044948_Floor makes the same choice internally for stay-airborne callbacks. Only the
  // ordinary floor/ceiling squeeze path suppresses ignore_bottom after a ceiling contact.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044838_Floor,mpColl_80044948_Floor,
  //   mpColl_80046904}
  const uint8_t project_from_root =
      (uint8_t)(frame->ecb.bottom_rel_y > 0.0f && (stay_airborne || !ceiling_contact));
  const float projection_y = project_from_root ? frame->cur_y : frame->ecb.bottom_y;
  if (msl_mplib_project_floor(batch, bi, hit.segment_i, frame->cur_x, projection_y, &projection)) {
    frame->floor_id = projection.line_id;
    result.floor_id = projection.line_id;
    frame->cur_y += projection.correction_y;
    rebuild_ecb(frame);
    batch->state.ground_contact_x[idx] = projection.contact_x;
    batch->state.ground_contact_y[idx] = projection.contact_y;
    batch->state.ground_normal_x[idx] = projection.normal_x;
    batch->state.ground_normal_y[idx] = projection.normal_y;
  } else {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    int16_t left_adjacent = -1;
    int16_t right_adjacent = -1;
    if (msl_mplib_floor_endpoint(batch, bi, hit.segment_i, -1, &left_x, &left_y, &left_adjacent) &&
        msl_mplib_floor_endpoint(batch, bi, hit.segment_i, +1, &right_x, &right_y,
                                 &right_adjacent)) {
      const int side = frame->cur_x < left_x ? -1 : 1;
      const float edge_x = side < 0 ? left_x : right_x;
      const float edge_y = side < 0 ? left_y : right_y;
      const int16_t adjacent = side < 0 ? left_adjacent : right_adjacent;
      uint8_t snap_x = (uint8_t)!stay_airborne;
      if (stay_airborne && adjacent >= 0) {
        MslStageRawLineKind adjacent_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
        if (stage_collision_raw_line_kind(stage_id, (uint16_t)adjacent, &adjacent_kind)) {
          snap_x = (uint8_t)(side < 0 ? adjacent_kind == MSL_STAGE_RAW_LINE_RIGHT_WALL
                                      : adjacent_kind == MSL_STAGE_RAW_LINE_LEFT_WALL);
        }
      }
      if (snap_x) {
        frame->cur_x = edge_x;
      }
      frame->cur_y = edge_y - frame->ecb.bottom_rel_y;
      rebuild_ecb(frame);
      MslMpLibFloorProjection edge_projection = {0};
      if (msl_mplib_project_floor(batch, bi, hit.segment_i, edge_x, edge_y, &edge_projection)) {
        frame->floor_id = edge_projection.line_id;
        result.floor_id = edge_projection.line_id;
        batch->state.ground_contact_x[idx] = edge_projection.contact_x;
        batch->state.ground_contact_y[idx] = edge_projection.contact_y;
        batch->state.ground_normal_x[idx] = edge_projection.normal_x;
        batch->state.ground_normal_y[idx] = edge_projection.normal_y;
      }
    }
  }
  if (!stay_airborne) {
    result.touched_floor = 1u;
    frame->outer_stop = 1u;
  }
  const float y_after_floor = frame->cur_y;
  const uint8_t ceiling_retry_hit = msl_mpcoll_resolve_ceiling(batch, bi, idx, frame);
  if (ceiling_retry_hit) {
    msl_mpcoll_squeeze_vertical(batch, idx, frame, (uint8_t)!result.touched_floor, frame->cur_y,
                                y_after_floor);
  }
  return result;
}

MslMpCollAirStepResult msl_mpcoll_resolve_air_step(MslBatch* batch, int bi, size_t idx,
                                                   MslMpCollFrame* frame, uint16_t floor_skip,
                                                   uint8_t stay_airborne, uint8_t platform_pass,
                                                   uint8_t hard_floor_only) {
  MslMpCollAirStepResult result = {.floor_id = frame != NULL ? frame->floor_id : 0xFFFFu};
  if (batch == NULL || frame == NULL) {
    return result;
  }
  uint8_t previous_squeezed = frame->squeezed;
  uint8_t previous_convergence_flags = 0u;
  uint8_t repeat = 0u;
  do {
    const MslMpCollAirStepResult pass = resolve_air_step_once(
        batch, bi, idx, frame, floor_skip, stay_airborne, platform_pass, hard_floor_only);
    if (pass.floor_contact) {
      result.floor_id = pass.floor_id;
      result.floor_contact = 1u;
      result.floor_contact_soft = pass.floor_contact_soft;
    }
    result.touched_floor |= pass.touched_floor;
    repeat = (uint8_t)(previous_squeezed != frame->squeezed ||
                       previous_convergence_flags != pass.convergence_flags);
    previous_squeezed = frame->squeezed;
    previous_convergence_flags = pass.convergence_flags;
  } while (repeat);
  // Re-enter until both x34.b6 and the current wall/floor/ceiling contact mask converge.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpCollSqueezeHorizontal,
  //   mpCollSqueezeVertical}
  result.outer_stop = frame->outer_stop;
  return result;
}

void msl_mpcoll_clear_wall_ceiling(MslBatch* batch, size_t idx) {
  batch->state.wall_kind[idx] = 0u;
  batch->state.wall_id[idx] = 0xFFFFu;
  batch->state.ceiling_id[idx] = 0xFFFFu;
  batch->state.wall_contact_x[idx] = 0.0f;
  batch->state.wall_contact_y[idx] = 0.0f;
  batch->state.wall_normal_x[idx] = 0.0f;
  batch->state.wall_normal_y[idx] = 0.0f;
  batch->state.coll_wall_commit_runtime[idx] = 0u;
}

static uint8_t static_speed(uint8_t active, float* x, float* y) {
  if (x != NULL) {
    *x = 0.0f;
  }
  if (y != NULL) {
    *y = 0.0f;
  }
  return active;
}

uint8_t mpcoll_get_speed_floor_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                      float* y) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size ||
      batch->state.ground_id[idx] == 0xFFFFu) {
    return static_speed(0u, x, y);
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  MslStageFloorLineCaps caps = {0};
  if (!stage_collision_floor_line_caps(stage_id, batch->state.ground_id[idx], &caps) ||
      caps.fighter_solid == 0u) {
    return static_speed(0u, x, y);
  }
  if (caps.platform_transform_kind == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT ||
      caps.platform_transform_kind == (uint8_t)MSL_STAGE_PLATFORM_TRANSFORM_RANDALL) {
    const MslStageFloorGraph* graph = stage_collision_get_floor_graph(stage_id);
    const int line_i = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    MslStageMovingSurfaceState surface = {0};
    if (graph == NULL || line_i < 0 || (size_t)line_i >= graph->line_count ||
        !stage_collision_floor_line_moving_surface_state(batch, bi, &graph->lines[line_i],
                                                         &surface) ||
        surface.valid == 0u || surface.source_trusted == 0u) {
      return static_speed(0u, x, y);
    }
    if (x != NULL) {
      *x = surface.velocity_x;
    }
    if (y != NULL) {
      *y = surface.velocity_y;
    }
    return 1u;
  }
  return static_speed(1u, x, y);
}

static uint8_t contact_line_static(const MslBatch* batch, int bi, uint8_t kind, uint16_t line_id,
                                   float* x, float* y) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || line_id == 0xFFFFu) {
    return static_speed(0u, x, y);
  }
  const MslStageMapLine* line =
      stage_collision_map_line(batch->state.stage_id[(size_t)bi], line_id);
  return static_speed((uint8_t)(line != NULL && line->kind == kind && line->fighter_solid != 0u), x,
                      y);
}

uint8_t mpcoll_get_speed_left_wall_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                          float* y) {
  if (batch == NULL || batch->state.wall_kind[idx] != (uint8_t)MSL_MPCOLL_WALL_KIND_LEFT) {
    return static_speed(0u, x, y);
  }
  return contact_line_static(batch, bi, (uint8_t)MSL_STAGE_RAW_LINE_LEFT_WALL,
                             batch->state.wall_id[idx], x, y);
}

uint8_t mpcoll_get_speed_right_wall_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                           float* y) {
  if (batch == NULL || batch->state.wall_kind[idx] != (uint8_t)MSL_MPCOLL_WALL_KIND_RIGHT) {
    return static_speed(0u, x, y);
  }
  return contact_line_static(batch, bi, (uint8_t)MSL_STAGE_RAW_LINE_RIGHT_WALL,
                             batch->state.wall_id[idx], x, y);
}

uint8_t mpcoll_get_speed_ceiling_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                        float* y) {
  return contact_line_static(batch, bi, (uint8_t)MSL_STAGE_RAW_LINE_CEILING,
                             batch != NULL ? batch->state.ceiling_id[idx] : 0xFFFFu, x, y);
}

uint8_t mpcoll_is_on_platform(const MslBatch* batch, int bi, size_t idx) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size ||
      batch->state.ground_id[idx] == 0xFFFFu) {
    return 0u;
  }
  const MslStageMapLine* line =
      stage_collision_map_line(batch->state.stage_id[(size_t)bi], batch->state.ground_id[idx]);
  return (uint8_t)(line != NULL && line->kind == (uint8_t)MSL_STAGE_RAW_LINE_FLOOR &&
                   line->is_platform != 0u);
}
