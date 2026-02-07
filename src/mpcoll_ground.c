#include "mpcoll_ground.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "coll_env_flags.h"
#include "match_flow.h"
#include "mpcoll_ecb_points.h"
#include "stage_collision.h"

// Decomp constants (mplib.c):
// - mpLib_8004DD90_Floor clamps small off-end X within ±0.1 before returning -1 (airborne).
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpLib_8004DD90_Floor applies a +0.0001 bias to the vertical correction to keep the point
//   infinitesimally above the floor line.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpLib_8004ED5C extends connected endpoints by 1 unit, guarded by a 0.001 distance threshold.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
// - mpCheckFloor treats floors as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckFloor
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_floor_ed5c_min_dist = 0.001f;
static const float k_floor_horiz_dy_thresh = 0.0001f;
static const float k_floor_ed5c_extend = 1.0f;

// Decomp: mpColl floor-edge helpers use +/-1 offsets from the floor endpoint when probing for
// blocking walls before setting Collide_{Left,Right}Edge.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
static const float k_floor_edge_wall_probe_x_offset = 1.0f;
static const float k_floor_edge_wall_probe_y_offset = 1.0f;

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  // Cliff / ledge hold actions use dedicated snap logic and should not be stage-grounded.
  // Decomp: ftCo_CliffCatch_Phys snaps to the cliff point each frame.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_allows_floor_edge_snap(uint16_t a) {
  // Decomp: mpColl_8004A45C_Floor (edge snap) is used by mpColl_8004B2DC (flags=2), which is
  // called by ft_800827A0. Multiple grounded motion states use ft_80084104 (which calls
  // ft_800827A0) as their collision callback, including:
  // - Down* (historically the first dominant mismatch cluster)
  // - Grounded attacks (Attack11..AttackLw4), including AttackDash and AttackS4S.
  //
  // Implementation note: we gate by action_id here as a proxy for "this motion state uses the
  // ft_80084104 collision callback chain". We intentionally do not include locomotion states
  // (Walk/Run/Dash/etc.) so walking/running off ledges still produces a ground->air transition.
  //
  // This lite sim uses the same edge-snap fallback when mpLib_8004DD90_Floor projection fails on
  // a persisted floor line, to avoid spurious ground loss at floor endpoints/seams near the FD
  // ledge.
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80084104 (calls ft_800827A0)
  // - refs/melee/src/melee/ft/ft_081B.c::ft_800827A0 (calls mpColl_8004B2DC)
  // - refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC (uses mpColl_8004A45C_Floor)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Coll
  if (a >= (uint16_t)MSL_ACT_ATTACK_11 && a <= (uint16_t)MSL_ACT_ATTACK_LW4) {
    return 1;
  }
  switch (a) {
    case MSL_ACT_DOWN_BOUND_U:
    case MSL_ACT_DOWN_WAIT_U:
    case MSL_ACT_DOWN_STAND_U:
    case MSL_ACT_DOWN_ATTACK_U:
    case MSL_ACT_DOWN_FOWARD_U:
    case MSL_ACT_DOWN_BACK_U:
    case MSL_ACT_DOWN_BOUND_D:
    case MSL_ACT_DOWN_WAIT_D:
    case MSL_ACT_DOWN_STAND_D:
    case MSL_ACT_DOWN_ATTACK_D:
    case MSL_ACT_DOWN_FOWARD_D:
    case MSL_ACT_DOWN_BACK_D:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t floor_lines_connected(const MslStageFloorGraph* g, int a, int b) {
  if (g == NULL) {
    return 0;
  }
  if (a < 0 || b < 0) {
    return 0;
  }
  if (a == b) {
    return 1;
  }
  // Decomp shape: mpLinesConnected(line_a, line_b) checks connectivity in the stage collision
  // graph. FD is a simple chain, so a bounded walk is sufficient.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor (uses mpLineGetPrev/Next traversal)
  int cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t next = g->lines[cur].next;
    if (next < 0) {
      break;
    }
    if (next == b) {
      return 1;
    }
    cur = (int)next;
  }
  cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t prev = g->lines[cur].prev;
    if (prev < 0) {
      break;
    }
    if (prev == b) {
      return 1;
    }
    cur = (int)prev;
  }
  return 0;
}

static void floor_ed5c_endpoints(const MslStageFloorGraph* g, int line_idx, float* x0_out,
                                 float* y0_out, float* x1_out, float* y1_out) {
  // Decomp: mpLib_8004ED5C expands endpoints by 1 unit for connected endpoints.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  const MslStageFloorLine* l = &g->lines[line_idx];
  float x0 = l->x0;
  float y0 = l->y0;
  float x1 = l->x1;
  float y1 = l->y1;

  float dist = 0.0f;
  uint8_t have_dist = 0;
  if (l->has_prev_link) {
    const float dx = x0 - x1;
    const float dy = y0 - y1;
    dist = sqrtf(dx * dx + dy * dy);
    have_dist = 1;
    if (dist > k_floor_ed5c_min_dist) {
      x0 += (dx / dist) * k_floor_ed5c_extend;
      y0 += (dy / dist) * k_floor_ed5c_extend;
    }
  }
  if (l->has_next_link) {
    if (!have_dist) {
      const float dx = x0 - x1;
      const float dy = y0 - y1;
      dist = sqrtf(dx * dx + dy * dy);
    }
    if (dist > k_floor_ed5c_min_dist) {
      const float dx = x1 - x0;
      const float dy = y1 - y0;
      x1 += (dx / dist) * k_floor_ed5c_extend;
      y1 += (dy / dist) * k_floor_ed5c_extend;
    }
  }

  *x0_out = x0;
  *y0_out = y0;
  *x1_out = x1;
  *y1_out = y1;
}

static int floor_dd90_project(const MslStageFloorGraph* g, int line_idx, float x_in, float y_in,
                              float* y_out, float* nx_out, float* ny_out) {
  // Decomp: mpLib_8004DD90_Floor traverses prev/next (floor-only) and returns a vertical
  // correction and normal for the line under vec->x.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return -1;
  }

  int dir = 0;
  int cur = line_idx;
  float x = 0.0f;
  float x0 = 0.0f, x1 = 0.0f;
  for (;;) {
    const MslStageFloorLine* l = &g->lines[cur];
    x0 = l->x0;
    x1 = l->x1;
    x = x_in;
    if (x < x0) {
      if (dir != 1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (x - x0 < -k_floor_x_end_clamp) {
            return -1;
          }
          x = x0;
          break;
        }
        cur = prev;
        dir = -1;
        continue;
      }
      x = x0;
      break;
    }
    if (x > x1) {
      // Decomp shape: the "dir" guard is asymmetric: it sets dir=-1 when traversing prev, but
      // does not set dir=+1 when traversing next.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      if (dir != -1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (x - x1 > k_floor_x_end_clamp) {
            return -1;
          }
          x = x1;
          break;
        }
        cur = next;
        continue;
      }
      x = x1;
      break;
    }
    break;
  }

  const MslStageFloorLine* out = &g->lines[cur];
  const float y0 = out->y0;
  const float y1 = out->y1;

  if (y_out != NULL) {
    // decomp: +0.0001 bias
    *y_out = (y1 - y0) * (x - x0) / (x1 - x0) + y0 - y_in + k_floor_y_bias;
  }
  if (nx_out != NULL || ny_out != NULL) {
    float nx = -(y1 - y0);
    float ny = x1 - x0;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len > 0.0f) {
      nx /= len;
      ny /= len;
    } else {
      nx = 0.0f;
      ny = 1.0f;
    }
    if (nx_out != NULL) {
      *nx_out = nx;
    }
    if (ny_out != NULL) {
      *ny_out = ny;
    }
  }
  return cur;
}

static uint8_t floor_intersect_horiz(float x0, float y0, float x1, float ax, float ay, float bx,
                                     float by, float* ix_out, float* iy_out) {
  // Decomp: mpLineIntersectionH (used by mpCheckFloor on horizontal-ish floor lines).
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  if (ix_out == NULL || iy_out == NULL) {
    return 0;
  }

  // mpCheckFloor gate: only consider falling / non-rising motion segments (ay >= by in this sim's
  // y-up coordinate system).
  if (!(ay >= by)) {
    return 0;
  }

  float min_ax = 0.0f;
  float max_ax = 0.0f;
  if (x0 < x1) {
    if ((ax < x0 && bx < x0) || (x1 < ax && x1 < bx)) {
      return 0;
    }
    if ((ay - y0) < -k_floor_horiz_dy_thresh || (by - y0) > k_floor_horiz_dy_thresh) {
      return 0;
    }
    min_ax = x0;
    max_ax = x1;
  } else {
    if ((ax < x1 && bx < x1) || (x0 < ax && x0 < bx)) {
      return 0;
    }
    if ((by - y0) < -k_floor_horiz_dy_thresh || (ay - y0) > k_floor_horiz_dy_thresh) {
      return 0;
    }
    min_ax = x1;
    max_ax = x0;
  }

  const double dby = (double)by - (double)ay;
  const double dbx = (double)bx - (double)ax;
  if (fabs(dby) < (double)k_floor_horiz_dy_thresh) {
    return 0;
  }

  double new_x = dbx / dby * (double)(y0 - ay) + (double)ax;
  double dx = new_x - (double)min_ax;
  if (dx < 0.0) {
    if (dx < -(double)k_floor_x_end_clamp) {
      return 0;
    }
    new_x = (double)min_ax;
  }
  if (new_x - (double)max_ax > 0.0) {
    if (new_x - (double)max_ax > (double)k_floor_x_end_clamp) {
      return 0;
    }
    new_x = (double)max_ax;
  }

  *ix_out = (float)new_x;
  *iy_out = y0;
  return 1;
}

static uint8_t floor_intersect_segment(float x0, float y0, float x1, float y1, float ax, float ay,
                                       float bx, float by, float* ix_out, float* iy_out) {
  const float rx = bx - ax;
  const float ry = by - ay;
  const float sx = x1 - x0;
  const float sy = y1 - y0;
  const float denom = cross2(rx, ry, sx, sy);
  if (denom == 0.0f) {
    return 0;
  }
  const float qpx = x0 - ax;
  const float qpy = y0 - ay;
  const float t = cross2(qpx, qpy, sx, sy) / denom;
  const float u = cross2(qpx, qpy, rx, ry) / denom;
  if (!(t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f)) {
    return 0;
  }
  *ix_out = ax + rx * t;
  *iy_out = ay + ry * t;
  return 1;
}

static inline uint8_t floor_chain_endpoints(const MslStageFloorGraph* g, int line_idx,
                                            float* out_left_x, float* out_left_y,
                                            float* out_right_x, float* out_right_y) {
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0;
  }
  if (line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0;
  }

  int left_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t prev = g->lines[left_i].prev;
    if (prev < 0) {
      break;
    }
    if ((size_t)prev >= g->line_count) {
      break;
    }
    left_i = (int)prev;
  }
  int right_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t next = g->lines[right_i].next;
    if (next < 0) {
      break;
    }
    if ((size_t)next >= g->line_count) {
      break;
    }
    right_i = (int)next;
  }

  if (out_left_x) {
    *out_left_x = g->lines[left_i].x0;
  }
  if (out_left_y) {
    *out_left_y = g->lines[left_i].y0;
  }
  if (out_right_x) {
    *out_right_x = g->lines[right_i].x1;
  }
  if (out_right_y) {
    *out_right_y = g->lines[right_i].y1;
  }
  return 1;
}

static inline uint8_t wall_blocks_floor_edge_probe(const MslStageWallGraph* wg, float ax, float ay,
                                                   float bx, float by) {
  // Decomp parity note: mpColl_8004A45C_Floor uses mpCheckLeftWall/mpCheckRightWall to ensure a
  // wall has not stopped the fighter before setting edge suppression bits.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
  if (wg == NULL || wg->lines == NULL || wg->line_count == 0) {
    return 0;
  }

  for (size_t wi = 0; wi < wg->line_count; wi++) {
    const MslStageWallLine* w = &wg->lines[wi];
    float ix = 0.0f, iy = 0.0f;
    if (floor_intersect_segment(w->x0, w->y0, w->x1, w->y1, ax, ay, bx, by, &ix, &iy)) {
      return 1;
    }
  }
  return 0;
}

static inline void floor_write_edge_suppression_flags(MslBatch* batch, size_t idx,
                                                      uint32_t stage_id,
                                                      const MslStageFloorGraph* fg, int line_idx,
                                                      uint8_t char_id, uint32_t anim,
                                                      uint16_t ecb_frame, uint8_t was_grounded) {
  if (batch == NULL || fg == NULL) {
    return;
  }
  if (line_idx < 0 || (size_t)line_idx >= fg->line_count) {
    return;
  }

  // Floor edge suppression (Collide_LeftEdge / Collide_RightEdge).
  //
  // Decomp: mpColl sets these bits in a floor-edge helper which snaps the fighter to the floor
  // endpoint when their position goes beyond the chain end, provided a wall check does not block.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
  //
  // Decomp: mpColl suppresses ledge-grab checks while "on edge" by testing these bits.
  // refs/melee/src/melee/mp/mpcoll.c (mpColl_80046904 ledge-grab block; `on_edge` gate)
  float left_x0 = 0.0f, left_y0 = 0.0f, right_x1 = 0.0f, right_y1 = 0.0f;
  if (!floor_chain_endpoints(fg, line_idx, &left_x0, &left_y0, &right_x1, &right_y1)) {
    return;
  }

  const float left_x = (left_x0 < right_x1) ? left_x0 : right_x1;
  const float right_x = (left_x0 < right_x1) ? right_x1 : left_x0;
  const float left_y = (left_x0 < right_x1) ? left_y0 : right_y1;
  const float right_y = (left_x0 < right_x1) ? right_y1 : left_y0;

  const float fighter_x = batch->state.pos_x[idx];
  if (fighter_x <= left_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                batch->state.pos_y[idx], was_grounded);
    const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
    const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
    const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_EDGE;
      // Decomp: mpColl_8004A678_Floor also sets Collide_Edge when snapping to floor endpoints.
      // In this sim, set Collide_Edge whenever any edge suppression bit is set as a cheap parity
      // win and to future-proof other gates.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  } else if (fighter_x >= right_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                batch->state.pos_y[idx], was_grounded);
    const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
    const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
    const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_EDGE;
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  }
}

static uint8_t floor_sweep_check(const MslStageFloorGraph* g, float ax, float ay, float bx,
                                 float by, int prefer_line_idx, int* out_line_idx, float* out_ix,
                                 float* out_iy, float* out_nx, float* out_ny) {
  // Decomp: mpCheckFloor iterates floor lines, intersects segment A->B with each, and chooses the
  // closest intersection to A (min dist^2), with stage-defined deterministic ordering on ties.
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }

  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = 0.0f, best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    floor_ed5c_endpoints(g, (int)li, &x0, &y0, &x1, &y1);

    float ix = 0.0f, iy = 0.0f;
    const float dy = y0 - y1;
    uint8_t hit = 0;
    if (fabsf(dy) > k_floor_horiz_dy_thresh) {
      hit = floor_intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = floor_intersect_horiz(x0, y0, x1, ax, ay, bx, by, &ix, &iy);
    }
    if (!hit) {
      continue;
    }

    const float dx = ix - ax;
    const float dy2 = iy - ay;
    const float dist2 = dx * dx + dy2 * dy2;

    int pref = 0;
    if (prefer_line_idx >= 0) {
      if ((int)li == prefer_line_idx) {
        pref = 2;
      } else if (floor_lines_connected(g, prefer_line_idx, (int)li)) {
        pref = 1;
      }
    }

    if (!found || (dist2 < best_dist2) || (dist2 == best_dist2 && pref > best_pref) ||
        (dist2 == best_dist2 && pref == best_pref &&
         g->lines[li].segment_i < g->lines[(size_t)best_idx].segment_i)) {
      found = 1;
      best_dist2 = dist2;
      best_idx = (int)li;
      best_pref = pref;
      best_ix = ix;
      best_iy = iy;

      // Normal matches mpCheckFloor: perpendicular to the line direction, normalized.
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      float nx = -(y1 - y0);
      float ny = x1 - x0;
      const float len = sqrtf(nx * nx + ny * ny);
      if (len > 0.0f) {
        nx /= len;
        ny /= len;
      } else {
        nx = 0.0f;
        ny = 1.0f;
      }
      best_nx = nx;
      best_ny = ny;
    }
  }

  if (!found) {
    return 0;
  }

  *out_line_idx = best_idx;
  if (out_ix) {
    *out_ix = best_ix;
  }
  if (out_iy) {
    *out_iy = best_iy;
  }
  if (out_nx) {
    *out_nx = best_nx;
  }
  if (out_ny) {
    *out_ny = best_ny;
  }
  return 1;
}

void mpcoll_ground_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
    if (g == NULL || g->lines == NULL || g->line_count == 0) {
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];

      // Decomp: Fighter_procUpdate and Fighter_procMap collision blocks are gated out during hitlag.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (the `if (!fp->x2219_b5)` block)
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      // Match-flow actions use dedicated (or NULL) collision callbacks in decomp; this lite sim
      // skips the generic stage collision pass until those paths are implemented.
      // refs: src/match_flow.c::match_flow_should_stage_collide
      if (!match_flow_should_stage_collide(action_id)) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      // Cliff / ledge hold actions use their own snap logic and should not be stage-grounded.
      if (is_cliff_hold_action(action_id)) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;

      // Decomp: Fighter_procMap decrements fp->ecb_lock before calling coll_cb/map callbacks and
      // clears CollData_X130_Locked when the countdown reaches 0.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      //
      // This simulator stores only the countdown (`ecb_lock_timer`) and uses it as the lock gate.
      // Grounded snapshots should not carry a stale lock.
      uint8_t ecb_lock_timer = batch->state.ecb_lock_timer[idx];
      if (batch->state.on_ground[idx]) {
        ecb_lock_timer = 0;
      } else if (ecb_lock_timer > 0u) {
        ecb_lock_timer = (uint8_t)(ecb_lock_timer - 1u);
      }
      batch->state.ecb_lock_timer[idx] = ecb_lock_timer;
      const uint8_t ecb_lock_active = (ecb_lock_timer > 0u) ? 1u : 0u;

      // Decomp shape: ECB is loaded each collision step and prev_ecb is a one-step lag:
      // mpCollInterpolateECB assigns prev_ecb = ecb before updating.
      // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
      const uint8_t char_id = batch->state.char_id[idx];
      const uint32_t anim = batch->state.animation_index[idx];
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);

      // Decomp: some stage collision entrypoints load ECB with flags where `flags & 1` forces
      // desired_ecb.bottom.y = 0.0 (relative to cur_pos). This stabilizes grounded contact against
      // pose-driven ECB changes.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj (flags & 1)
      MslEcbBottomWorldPoint cur_bot = {0};
      MslEcbBottomWorldPoint prev_bot = {0};

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      const float prev_x = batch->state.prev_pos_x[idx];
      const float prev_y = batch->state.prev_pos_y[idx];

      // ECB bottom point for floor collision.
      // Decomp: mpLib_8004DD90_Floor and mpCheckFloor consume the ECB bottom point.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      uint8_t lock_bottom_to_zero = was_grounded;
      uint8_t lock_bottom_to_prev_frame = 0u;
      if (!lock_bottom_to_zero && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) {
        // EscapeAir collision callback path:
        // - ftCommon_8007D5D4 sets fp->ecb_lock=10 and CollData_X130_Locked on ground->air takeoff.
        // - mpColl_LoadECB_inline preserves desired_ecb.bottom while CollData_X130_Locked is set.
        // - EscapeAir_Coll routes grounded contact into LandingFallSpecial.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        lock_bottom_to_zero = 1u;
      } else if (!lock_bottom_to_zero && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 batch->state.action_frame[idx] >= 0 && batch->state.action_frame[idx] <= 10 &&
                 batch->state.speed_y_self[idx] <= 0.0f) {
        // Decomp shape: mpColl tracks both `ecb` and `prev_ecb`, and floor checks can resolve from a
        // swept bottom segment while EscapeAir is descending. This lite sim does not carry mpColl's
        // full interpolation state, so use the previous ECB frame as a deterministic approximation.
        // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
        lock_bottom_to_prev_frame = 1u;
      }
      const uint16_t ecb_frame_cur = lock_bottom_to_prev_frame ? ecb_frame_prev : ecb_frame;
      msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame_cur, x, y,
                                        lock_bottom_to_zero);
      msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev, prev_x, prev_y,
                                        lock_bottom_to_zero);

      const float cur_bottom_x = cur_bot.x;
      const float cur_bottom_y = cur_bot.y;
      const float prev_bottom_x = prev_bot.x;
      const float prev_bottom_y = prev_bot.y;

      // Collision env flags (subset) for Parity Project #2 (ledge grab mask parity).
      // Decomp: CollData carries env_flags and prev_env_flags across frames.
      // refs/melee/src/melee/lb/types.h::CollData
      batch->state.coll_prev_env_flags[idx] = batch->state.coll_env_flags[idx];
      batch->state.coll_env_flags[idx] = 0;

      // Contact persistence:
      // - CollData carries floor.index across frames and uses the line graph to traverse seams.
      // - Grounded resolution uses a per-line projection (mpLib_8004DD90_Floor) rather than
      //   reselecting from scratch each frame.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C (example of floor.index persistence)
      uint8_t on_ground = 0;
      uint16_t ground_id = batch->state.ground_id[idx];

      float floor_nx = 0.0f;
      float floor_ny = 1.0f;
      float contact_x = cur_bottom_x;
      float contact_y = 0.0f;

      int prefer_line_idx = -1;
      if (ground_id != 0xFFFFu) {
        prefer_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
      }

      if (was_grounded && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                                    &y_corr, &floor_nx, &floor_ny);
        if (out_line_idx >= 0) {
          // mpLib_8004DD90_Floor returns a signed correction; for stable grounded frames we only
          // need to resolve penetration. If we are already above the floor due to upstream
          // approximation drift, avoid snapping down in the collision substrate.
          if (y_corr < 0.0f) {
            y_corr = 0.0f;
          }
          batch->state.pos_y[idx] += y_corr;
          on_ground = 1;
          ground_id = g->lines[(size_t)out_line_idx].segment_i;
          contact_y = (cur_bottom_y + y_corr);
        } else {
          // Decomp shape:
          // - Grounded collision uses mpLib_8004DD90_Floor to project onto the current floor line,
          //   but on failure it can still (a) snap to the current floor edge (mpColl_8004A45C_Floor,
          //   used by mpColl_8004B2DC) and/or (b) detect a floor hit via the swept segment test
          //   (mpCheckFloor-style) before concluding the fighter is airborne.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          //
          // This matters for downed rolls near the FD ledge: the integrated position can move
          // past the floor endpoint, but the motion segment still intersects the floor line and
          // the engine clamps the contact to the intersection point before leaving ground.
          uint8_t snapped_edge = 0;
          const MslStageFloorLine* l = &g->lines[(size_t)prefer_line_idx];
          const float left_x = l->x0;
          const float left_y = l->y0;
          const float right_x = l->x1;
          const float right_y = l->y1;

          // mpColl_8004A45C_Floor: when the fighter passes beyond the current floor endpoint,
          // mpColl can snap the position to the edge point (if not blocked by a wall probe) while
          // keeping the fighter grounded for this collision result.
          if (action_allows_floor_edge_snap(action_id)) {
            if (cur_bottom_x <= left_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, left_x, left_y,
                                          was_grounded);
              const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
              const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
              const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = floor_dd90_project(g, prefer_line_idx, left_x, left_y, NULL,
                                                       &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (left_x - cur_bottom_x);
                batch->state.pos_y[idx] = left_y;
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = left_x;
                contact_y = left_y;
                snapped_edge = 1;
              }
            } else if (cur_bottom_x >= right_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, right_x, right_y,
                                          was_grounded);
              const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
              const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
              const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = floor_dd90_project(g, prefer_line_idx, right_x, right_y, NULL,
                                                       &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (right_x - cur_bottom_x);
                batch->state.pos_y[idx] = right_y;
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = right_x;
                contact_y = right_y;
                snapped_edge = 1;
              }
            }
          }

          if (!snapped_edge) {
            // Decomp: mpCheckFloor's horizontal intersection helper is gated on non-rising segments
            // (ay >= by), so equality must be allowed (horizontal motion with vy==0 can still sweep).
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor (the `if (ay >= by && mpLineIntersectionH(...))` gate)
            const uint8_t can_sweep = (uint8_t)(cur_bottom_y <= prev_bottom_y);
            int hit_line_idx = -1;
            float ix = 0.0f, iy = 0.0f;
            if (can_sweep &&
                floor_sweep_check(g, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                  prefer_line_idx, &hit_line_idx, &ix, &iy, &floor_nx, &floor_ny)) {
              // Decomp: desired_ecb.bottom.x is always 0.0, so clamping the ECB bottom contact X
              // corresponds to clamping the fighter position X.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              batch->state.pos_x[idx] += (ix - cur_bottom_x);

              float y_corr2 = 0.0f;
              const int out_line_idx2 =
                  floor_dd90_project(g, hit_line_idx, ix, cur_bottom_y, &y_corr2, NULL, NULL);
              if (out_line_idx2 >= 0) {
                batch->state.pos_y[idx] += y_corr2;
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = ix;
                contact_y = iy;
              } else {
                // Sweep saw a floor segment, but projection failed (unexpected). Preserve the sweep
                // contact point deterministically and apply the decomp-shaped +0.0001 floor bias.
                // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              }
            } else {
              // Decomp parity: mpColl_8004A45C_Floor can still set Collide_{Left,Right}Edge while
              // the floor collision pass does not report "touched_floor" (airborne), and the
              // ledge-grab block uses these bits as the `on_edge` suppression gate.
              floor_write_edge_suppression_flags(batch, idx, stage_id, g, prefer_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded);
            }
          }
        }
      } else {
        int hit_line_idx = -1;
        float ix = 0.0f, iy = 0.0f;
        const uint8_t escapeair_locked =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ? 1u : 0u;
        uint8_t deep_lock_penetration = 0u;
        if (escapeair_locked) {
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          deep_lock_penetration = (bottom_rel0 > 0.0f && cur_bottom_y <= -bottom_rel0) ? 1u : 0u;
        }
        if (!on_ground && escapeair_locked && deep_lock_penetration && prefer_line_idx >= 0) {
          // Decomp shape: while CollData_X130_Locked is active, EscapeAir collision still resolves
          // against the persisted floor.index line via mpLib_8004DD90_Floor-style projection.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          //
          // This projection allows deterministic floor contact even when the locked ECB bottom is
          // already below the floor line (no crossing sweep this frame).
          //
          // Gate to deep penetration only:
          // - The lock path forces desired_ecb.bottom.y=0 relative to the fighter root.
          // - Use the ISO-extracted EscapeAir frame-0 bottom extent as a deterministic depth
          //   threshold so normal lock-active airborne descent (still above this depth) remains in
          //   air until the regular sweep/projection path lands.
          // refs/data/ecb/*_bottom.bin via msl_ecb_bottom_rel_y()
          float y_corr = 0.0f;
          const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                                      &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr > 0.0f) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = cur_bottom_x;
            contact_y = cur_bottom_y + y_corr;
          }
        }
        if (!on_ground &&
            floor_sweep_check(g, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y,
                              prefer_line_idx, &hit_line_idx, &ix, &iy, &floor_nx, &floor_ny)) {
          // EscapeAir lock semantics:
          // - keep shallow crossings on ledge floor segments airborne while lock is active;
          // - allow deep-penetration fallback above to ground deterministically.
          //
          // Decomp shape: floor-edge collision bits and ledge suppression are handled separately via
          // mpColl edge helpers, so touching a ledge floor segment is not always equivalent to
          // immediate grounded resolution in EscapeAir lock windows.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          const uint8_t suppress_locked_ledge_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               g->lines[(size_t)hit_line_idx].is_ledge)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_vertical_af3_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               !g->lines[(size_t)hit_line_idx].is_ledge &&
               // Decomp-shaped interpolation gap: in the early EscapeAir lock window, vertical-only
               // sweep crossings can report a floor hit one frame earlier than replay references.
               // Keep this specific case airborne and let subsequent collision frames resolve.
               batch->state.action_frame[idx] == 3 &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          if (suppress_locked_ledge_land || suppress_locked_vertical_af3_land) {
            floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id, anim,
                                               ecb_frame, was_grounded);
          } else {
            float y_corr = 0.0f;
            const int out_line_idx =
                floor_dd90_project(g, hit_line_idx, cur_bottom_x, cur_bottom_y, &y_corr, NULL, NULL);
            if (out_line_idx >= 0) {
              batch->state.pos_y[idx] += y_corr;
              on_ground = 1;
              ground_id = g->lines[(size_t)out_line_idx].segment_i;
              contact_x = ix;
              contact_y = iy;
            } else {
              // Sweep saw a floor segment, but projection failed (often an off-end / edge case).
              // Propagate edge suppression bits so mpColl-shaped ledge-grab checks can apply the
              // `on_edge` gate deterministically.
              floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded);
            }
          }
        } else if (prefer_line_idx >= 0 && batch->state.speed_y_self[idx] == 0.0f &&
                   batch->state.hitlag[idx] == 0 && batch->state.hitstun[idx] == 0) {
          // Decomp: mpLib_8004DD90_Floor can resolve a resting contact even when no crossing sweep is
          // reported (e.g. vy==0 and the ECB bottom is already on the surface).
          // Gate this to "already on the surface" to avoid snapping to the floor from far below.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x,
                                                      cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 &&
              fabsf(y_corr - k_floor_y_bias) <= (float)k_floor_horiz_dy_thresh) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = cur_bottom_x;
            contact_y = cur_bottom_y + y_corr;
          }
        }
      }

      batch->state.on_ground[idx] = on_ground;
      if (on_ground) {
        // Decomp: floor collision sets Collide_FloorPush (+ sometimes FloorHug).
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046F78
        batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;

        floor_write_edge_suppression_flags(batch, idx, stage_id, g,
                                           stage_collision_floor_line_index(stage_id, ground_id),
                                           char_id, anim, ecb_frame, was_grounded);

        batch->state.ground_id[idx] = ground_id;
        batch->state.ground_normal_x[idx] = floor_nx;
        batch->state.ground_normal_y[idx] = floor_ny;
        batch->state.ground_contact_x[idx] = contact_x;
        batch->state.ground_contact_y[idx] = contact_y;

        // Slippi post-frames: on_ground can become true while `speed_y_self` remains negative for
        // one frame on landing, then resets to 0 on the subsequent grounded frame.
        if (was_grounded) {
          batch->state.speed_y_self[idx] = 0.0f;
        }
      } else {
        // Keep ground_id stable while airborne (CollData floor.index persists while in air).
        // refs/melee/src/melee/lb/types.h::CollData
        batch->state.ground_normal_x[idx] = 0.0f;
        batch->state.ground_normal_y[idx] = 1.0f;
        batch->state.ground_contact_x[idx] = 0.0f;
        batch->state.ground_contact_y[idx] = 0.0f;
      }
    }
  }
}
