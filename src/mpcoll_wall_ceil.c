#include "mpcoll_wall_ceil.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "coll_env_flags.h"
#include "match_flow.h"
#include "mpcoll_ecb_points.h"
#include "stage_collision.h"

// Decomp constants / shapes:
// - mpCheckCeiling treats ceilings as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
// - mpCheck{Left,Right}Wall treats walls as vertical when |x0 - x1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
//   refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
// - mpLineIntersection{H,V} clamps small off-end travel within ±0.1 before returning false.
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionV
// - mpLib_8004E090_Ceiling applies a -0.0001 bias to keep the point infinitesimally below the ceiling.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
// - mpLib_8004E398_LeftWall / mpLib_8004E684_RightWall use ±0.1 seam clamping when traversing prev/next.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
// - mpLib_8004ED5C extends connected endpoints by 1 unit, guarded by a 0.001 distance threshold.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
static const float k_line_axis_thresh = 0.0001f;
static const float k_line_end_clamp = 0.1f;
static const float k_ceiling_y_bias = 0.0001f;
static const float k_ed5c_min_dist = 0.001f;
static const float k_ed5c_extend = 1.0f;

enum {
  MSL_WALL_NONE = 0,
  MSL_WALL_LEFT = 1,   // mplib CollLine_LeftWall
  MSL_WALL_RIGHT = 2,  // mplib CollLine_RightWall
};

static inline uint8_t point_eq_axis_eps(float ax, float ay, float bx, float by) {
  return (uint8_t)(fabsf(ax - bx) <= k_line_axis_thresh && fabsf(ay - by) <= k_line_axis_thresh);
}

static int wall_line_index_connected_to_point(const MslStageWallGraph* g, float x, float y) {
  if (g == NULL || g->lines == NULL) {
    return -1;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    const MslStageWallLine* l = &g->lines[i];
    if (point_eq_axis_eps(l->x0, l->y0, x, y) || point_eq_axis_eps(l->x1, l->y1, x, y)) {
      return (int)i;
    }
  }
  return -1;
}

static int grounded_right_wall_floor_adjacent_line_idx(uint32_t stage_id, uint16_t ground_id) {
  // Grounded right-wall checks exclude the floor-chain wall owners that are connected to the
  // current floor. Decomp resolves these through mpLib_80053394_Floor / mpLib_800536CC_Floor
  // before mpColl_80048AB0_RightWall / mpColl_800491C8_RightWall admit bottom-point wall hits.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_80053394_Floor,mpLib_800536CC_Floor}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall}
  const MslStageFloorGraph* fg = stage_collision_get_floor_graph(stage_id);
  const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
  if (fg == NULL || rwg == NULL || fg->lines == NULL || rwg->lines == NULL) {
    return -1;
  }
  int floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (floor_line_idx < 0 || (size_t)floor_line_idx >= fg->line_count) {
    return -1;
  }
  for (size_t i = 0; i < fg->line_count; i++) {
    const int16_t next = fg->lines[(size_t)floor_line_idx].next;
    if (next < 0 || (size_t)next >= fg->line_count) {
      break;
    }
    floor_line_idx = (int)next;
  }
  const MslStageFloorLine* floor = &fg->lines[(size_t)floor_line_idx];
  return wall_line_index_connected_to_point(rwg, floor->x1, floor->y1);
}

static int grounded_left_wall_floor_adjacent_line_idx(uint32_t stage_id, uint16_t ground_id) {
  // Symmetric grounded floor-chain exclusion for left-wall checks.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_80053448_Floor,mpLib_800534FC_Floor}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80049778_LeftWall,mpColl_80049EAC_LeftWall}
  const MslStageFloorGraph* fg = stage_collision_get_floor_graph(stage_id);
  const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
  if (fg == NULL || lwg == NULL || fg->lines == NULL || lwg->lines == NULL) {
    return -1;
  }
  int floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (floor_line_idx < 0 || (size_t)floor_line_idx >= fg->line_count) {
    return -1;
  }
  for (size_t i = 0; i < fg->line_count; i++) {
    const int16_t prev = fg->lines[(size_t)floor_line_idx].prev;
    if (prev < 0 || (size_t)prev >= fg->line_count) {
      break;
    }
    floor_line_idx = (int)prev;
  }
  const MslStageFloorLine* floor = &fg->lines[(size_t)floor_line_idx];
  return wall_line_index_connected_to_point(lwg, floor->x0, floor->y0);
}

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  // Cliff / ledge hold actions use dedicated snap logic and should not be stage-collided.
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

static inline uint8_t is_grounded_cliff_option_action(uint16_t a, uint8_t on_ground) {
  if (!on_ground) {
    return 0u;
  }
  // Decomp: grounded CliffClimb/CliffAttack/CliffEscape collision callbacks all delegate to
  // ftCo_CliffClimb_Coll, whose grounded branch is only `ft_80084104` (floor-loss handling). Wall
  // and ceiling collision stay owned by the dedicated cliff state callbacks instead.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_CliffAttack_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_CliffEscape_Coll
  switch (a) {
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t lines_connected_prev_next(size_t line_count, const int16_t* prev_next_pairs,
                                                int a, int b) {
  if (a < 0 || b < 0) {
    return 0;
  }
  if (a == b) {
    return 1;
  }
  // Bounded prev/next walk (FD-only graphs are small; keep deterministic).
  int cur = a;
  for (size_t i = 0; i < line_count; i++) {
    const int16_t next = prev_next_pairs[(size_t)cur * 2u + 1u];
    if (next < 0) {
      break;
    }
    if (next == b) {
      return 1;
    }
    cur = (int)next;
  }
  cur = a;
  for (size_t i = 0; i < line_count; i++) {
    const int16_t prev = prev_next_pairs[(size_t)cur * 2u + 0u];
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

static void ed5c_endpoints_generic(float x0, float y0, float x1, float y1, uint8_t has_prev_link,
                                   uint8_t has_next_link, float* ox0, float* oy0, float* ox1,
                                   float* oy1) {
  float ax0 = x0;
  float ay0 = y0;
  float ax1 = x1;
  float ay1 = y1;
  float dist = 0.0f;
  uint8_t have_dist = 0;
  if (has_prev_link) {
    const float dx = ax0 - ax1;
    const float dy = ay0 - ay1;
    dist = sqrtf(dx * dx + dy * dy);
    have_dist = 1;
    if (dist > k_ed5c_min_dist) {
      ax0 += (dx / dist) * k_ed5c_extend;
      ay0 += (dy / dist) * k_ed5c_extend;
    }
  }
  if (has_next_link) {
    if (!have_dist) {
      const float dx = ax0 - ax1;
      const float dy = ay0 - ay1;
      dist = sqrtf(dx * dx + dy * dy);
    }
    if (dist > k_ed5c_min_dist) {
      const float dx = ax1 - ax0;
      const float dy = ay1 - ay0;
      ax1 += (dx / dist) * k_ed5c_extend;
      ay1 += (dy / dist) * k_ed5c_extend;
    }
  }
  *ox0 = ax0;
  *oy0 = ay0;
  *ox1 = ax1;
  *oy1 = ay1;
}

static uint8_t intersect_segment(float x0, float y0, float x1, float y1, float ax, float ay,
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

static uint8_t intersect_horiz_clamped(float x0, float y0, float x1, float ax, float ay, float bx,
                                       float by, uint8_t require_rising, float* ix_out,
                                       float* iy_out) {
  // Decomp: mpLineIntersectionH has a direction gate for floor/ceiling usage:
  // - floor checks only when ay >= by (falling / non-rising)
  // - ceiling checks only when ay <= by (rising / non-falling)
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  // refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
  if (require_rising) {
    if (!(ay <= by)) {
      return 0;
    }
  } else {
    if (!(ay >= by)) {
      return 0;
    }
  }

  const float min_x = (x0 < x1) ? x0 : x1;
  const float max_x = (x0 < x1) ? x1 : x0;
  if (ay == by) {
    if (ay != y0) {
      return 0;
    }
    if (ax < min_x || ax > max_x) {
      return 0;
    }
    *ix_out = ax;
    *iy_out = y0;
    return 1;
  }
  if (require_rising) {
    if (!(ay <= y0 && by >= y0)) {
      return 0;
    }
    const float t = (y0 - ay) / (by - ay);
    float ix = ax + (bx - ax) * t;
    if (ix < min_x - k_line_end_clamp) {
      return 0;
    }
    if (ix > max_x + k_line_end_clamp) {
      return 0;
    }
    if (ix < min_x) {
      ix = min_x;
    } else if (ix > max_x) {
      ix = max_x;
    }
    *ix_out = ix;
    *iy_out = y0;
    return 1;
  }

  if (!(ay >= y0 && by <= y0)) {
    return 0;
  }
  const float t = (ay - y0) / (ay - by);
  float ix = ax + (bx - ax) * t;
  if (ix < min_x - k_line_end_clamp) {
    return 0;
  }
  if (ix > max_x + k_line_end_clamp) {
    return 0;
  }
  if (ix < min_x) {
    ix = min_x;
  } else if (ix > max_x) {
    ix = max_x;
  }
  *ix_out = ix;
  *iy_out = y0;
  return 1;
}

static uint8_t intersect_vert_clamped(float x0, float y0, float y1, float ax, float ay, float bx,
                                      float by, uint8_t require_moving_right, float* ix_out,
                                      float* iy_out) {
  // Decomp: vertical wall intersection is gated on horizontal direction for vertical-ish walls:
  // - mpCheckLeftWall only when ax <= bx
  // - mpCheckRightWall only when ax >= bx
  // refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
  // refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
  if (require_moving_right) {
    if (!(ax <= bx)) {
      return 0;
    }
  } else {
    if (!(ax >= bx)) {
      return 0;
    }
  }
  const float min_y = (y0 < y1) ? y0 : y1;
  const float max_y = (y0 < y1) ? y1 : y0;
  if (ax == bx) {
    if (ax != x0) {
      return 0;
    }
    if (ay < min_y || ay > max_y) {
      return 0;
    }
    *ix_out = x0;
    *iy_out = ay;
    return 1;
  }
  const float t = (x0 - ax) / (bx - ax);
  if (!(t >= 0.0f && t <= 1.0f)) {
    return 0;
  }
  float iy = ay + (by - ay) * t;
  if (iy < min_y - k_line_end_clamp) {
    return 0;
  }
  if (iy > max_y + k_line_end_clamp) {
    return 0;
  }
  if (iy < min_y) {
    iy = min_y;
  } else if (iy > max_y) {
    iy = max_y;
  }
  *ix_out = x0;
  *iy_out = iy;
  return 1;
}

static inline void normal_from_line(float x0, float y0, float x1, float y1, float* nx_out,
                                    float* ny_out) {
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
  *nx_out = nx;
  *ny_out = ny;
}

static int ceiling_e090_project(const MslStageCeilingGraph* g, int line_idx, float x_in, float y_in,
                                float* y_out, float* nx_out, float* ny_out) {
  // Decomp: mpLib_8004E090_Ceiling traverses prev/next for ceiling-only lines, then returns a signed
  // correction and normal for the ceiling line above vec->x.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float x = x_in;
  float x0 = 0.0f, x1 = 0.0f;
  for (;;) {
    const MslStageCeilingLine* l = &g->lines[cur];
    x0 = l->x0;
    x1 = l->x1;
    if (x_in < x1) {
      if (dir != 1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (x_in - x1 < -k_line_end_clamp) {
            return -1;
          }
          x = x1;
          break;
        }
        cur = next;
        dir = -1;
        continue;
      }
      x = x1;
    } else if (x_in > x0) {
      if (dir != -1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (x_in - x0 > k_line_end_clamp) {
            return -1;
          }
          x = x0;
          break;
        }
        cur = prev;
        dir = 1;
        continue;
      }
      x = x0;
    }
    break;
  }

  const MslStageCeilingLine* out = &g->lines[cur];
  const float y0 = out->y0;
  const float y1 = out->y1;
  if (y_out != NULL) {
    *y_out = (y1 - y0) * (x - x0) / (x1 - x0) + y0 - y_in - k_ceiling_y_bias;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static int left_wall_e398_project(const MslStageWallGraph* g, int line_idx, float x_in, float y_in,
                                  float* x_out, float* nx_out, float* ny_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float y = y_in;
  float y0 = 0.0f, y1 = 0.0f;
  for (;;) {
    const MslStageWallLine* l = &g->lines[cur];
    y0 = l->y0;
    y1 = l->y1;
    if (y_in < y0) {
      if (dir != 1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (y_in - y0 < -k_line_end_clamp) {
            return -1;
          }
          y = y0;
          break;
        }
        cur = prev;
        dir = -1;
        continue;
      }
    } else if (y_in > y1) {
      if (dir != -1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (y_in - y1 > k_line_end_clamp) {
            return -1;
          }
          y = y1;
          break;
        }
        cur = next;
        dir = 1;
        continue;
      }
    }
    break;
  }
  const MslStageWallLine* out = &g->lines[cur];
  const float x0 = out->x0;
  const float x1 = out->x1;
  if (x_out != NULL) {
    *x_out = x0 + (x1 - x0) * (y - y0) / (y1 - y0) - x_in;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static int right_wall_e684_project(const MslStageWallGraph* g, int line_idx, float x_in, float y_in,
                                   float* x_out, float* nx_out, float* ny_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float y = y_in;
  float y0 = 0.0f, y1 = 0.0f;
  for (;;) {
    const MslStageWallLine* l = &g->lines[cur];
    y0 = l->y0;
    y1 = l->y1;
    if (y_in > y0) {
      if (dir != -1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (y_in - y0 > k_line_end_clamp) {
            return -1;
          }
          y = y0;
          break;
        }
        cur = prev;
        dir = 1;
        continue;
      }
      y = y0;
    } else if (y_in < y1) {
      if (dir != 1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (y_in - y1 < -k_line_end_clamp) {
            return -1;
          }
          y = y1;
          break;
        }
        cur = next;
        dir = -1;
        continue;
      }
      y = y1;
    }
    break;
  }
  const MslStageWallLine* out = &g->lines[cur];
  const float x0 = out->x0;
  const float x1 = out->x1;
  if (x_out != NULL) {
    *x_out = x0 + (x1 - x0) * (y - y0) / (y1 - y0) - x_in;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static uint8_t ceiling_sweep_check(const MslStageCeilingGraph* g, float ax, float ay, float bx,
                                   float by, int prefer_line_idx, int* out_line_idx, float* out_ix,
                                   float* out_iy, float* out_nx, float* out_ny) {
  // refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }
  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = 0.0f, best_ny = -1.0f;

  // Build a compact prev/next pair array view (stack) for connectivity preference.
  // Deterministic: line_count is tiny (FD=3 ceiling lines).
  int16_t prev_next[128];
  if (g->line_count * 2u <= (sizeof(prev_next) / sizeof(prev_next[0]))) {
    for (size_t i = 0; i < g->line_count; i++) {
      prev_next[i * 2u + 0u] = g->lines[i].prev;
      prev_next[i * 2u + 1u] = g->lines[i].next;
    }
  }

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageCeilingLine* l = &g->lines[li];
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    ed5c_endpoints_generic(l->x0, l->y0, l->x1, l->y1, l->has_prev_link, l->has_next_link, &x0, &y0,
                           &x1, &y1);

    float ix = 0.0f, iy = 0.0f;
    uint8_t hit = 0;
    const float dy = y0 - y1;
    if (fabsf(dy) > k_line_axis_thresh) {
      hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = intersect_horiz_clamped(x0, y0, x1, ax, ay, bx, by, 1, &ix, &iy);
    }
    if (!hit) {
      continue;
    }

    const float dx = ix - ax;
    const float dy2 = iy - ay;
    const float dist2 = dx * dx + dy2 * dy2;

    int pref = 0;
    if (prefer_line_idx >= 0 && g->line_count * 2u <= (sizeof(prev_next) / sizeof(prev_next[0]))) {
      if ((int)li == prefer_line_idx) {
        pref = 2;
      } else if (lines_connected_prev_next(g->line_count, prev_next, prefer_line_idx, (int)li)) {
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
      if (fabsf(dy) <= k_line_axis_thresh) {
        best_nx = 0.0f;
        best_ny = -1.0f;
      } else {
        normal_from_line(x0, y0, x1, y1, &best_nx, &best_ny);
      }
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

static uint8_t wall_sweep_check(const MslStageWallGraph* g, uint8_t is_left_wall, float ax,
                                float ay, float bx, float by, int prefer_line_idx,
                                int* out_line_idx, float* out_ix, float* out_iy, float* out_nx,
                                float* out_ny) {
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }
  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = is_left_wall ? -1.0f : 1.0f;
  float best_ny = 0.0f;

  int16_t prev_next[256];
  if (g->line_count * 2u <= (sizeof(prev_next) / sizeof(prev_next[0]))) {
    for (size_t i = 0; i < g->line_count; i++) {
      prev_next[i * 2u + 0u] = g->lines[i].prev;
      prev_next[i * 2u + 1u] = g->lines[i].next;
    }
  }

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageWallLine* l = &g->lines[li];
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    ed5c_endpoints_generic(l->x0, l->y0, l->x1, l->y1, l->has_prev_link, l->has_next_link, &x0, &y0,
                           &x1, &y1);

    float ix = 0.0f, iy = 0.0f;
    uint8_t hit = 0;
    if (fabsf(x0 - x1) > k_line_axis_thresh) {
      hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = intersect_vert_clamped(x0, y0, y1, ax, ay, bx, by, is_left_wall, &ix, &iy);
    }
    if (!hit) {
      continue;
    }

    const float dx = ix - ax;
    const float dy = iy - ay;
    const float dist2 = dx * dx + dy * dy;

    int pref = 0;
    if (prefer_line_idx >= 0 && g->line_count * 2u <= (sizeof(prev_next) / sizeof(prev_next[0]))) {
      if ((int)li == prefer_line_idx) {
        pref = 2;
      } else if (lines_connected_prev_next(g->line_count, prev_next, prefer_line_idx, (int)li)) {
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
      if (fabsf(x0 - x1) <= k_line_axis_thresh) {
        best_nx = is_left_wall ? -1.0f : 1.0f;
        best_ny = 0.0f;
      } else {
        normal_from_line(x0, y0, x1, y1, &best_nx, &best_ny);
      }
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

// Decomp: mpColl ceiling-edge helper uses +/-1 offsets from the ceiling endpoint when probing for
// blocking walls before setting Collide_{Left,Right}Edge.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
static const float k_ceil_edge_wall_probe_x_offset = 1.0f;
static const float k_ceil_edge_wall_probe_y_offset = -1.0f;

static inline uint8_t wall_blocks_ceiling_edge_probe(const MslStageWallGraph* wg, float ax,
                                                     float ay, float bx, float by) {
  if (wg == NULL || wg->lines == NULL || wg->line_count == 0) {
    return 0;
  }
  for (size_t wi = 0; wi < wg->line_count; wi++) {
    const MslStageWallLine* w = &wg->lines[wi];
    float ix = 0.0f, iy = 0.0f;
    if (intersect_segment(w->x0, w->y0, w->x1, w->y1, ax, ay, bx, by, &ix, &iy)) {
      return 1;
    }
  }
  return 0;
}

static inline uint8_t ceiling_chain_endpoints(const MslStageCeilingGraph* g, int line_idx,
                                              float* out_left_x, float* out_left_y,
                                              float* out_right_x, float* out_right_y) {
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0;
  }
  if (line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0;
  }

  // Ceiling line orientation is right->left (x0>=x1). prev links at the right endpoint, next
  // links at the left endpoint.
  int right_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t prev = g->lines[right_i].prev;
    if (prev < 0 || (size_t)prev >= g->line_count) {
      break;
    }
    right_i = (int)prev;
  }
  int left_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t next = g->lines[left_i].next;
    if (next < 0 || (size_t)next >= g->line_count) {
      break;
    }
    left_i = (int)next;
  }

  if (out_left_x) {
    *out_left_x = g->lines[left_i].x1;
  }
  if (out_left_y) {
    *out_left_y = g->lines[left_i].y1;
  }
  if (out_right_x) {
    *out_right_x = g->lines[right_i].x0;
  }
  if (out_right_y) {
    *out_right_y = g->lines[right_i].y0;
  }
  return 1;
}

static inline void ceiling_write_edge_suppression_flags(MslBatch* batch, size_t idx,
                                                        uint32_t stage_id,
                                                        const MslStageCeilingGraph* cg,
                                                        int line_idx,
                                                        const MslEcbWorldPoints* ecb) {
  if (batch == NULL || cg == NULL || ecb == NULL) {
    return;
  }
  float left_x = 0.0f, left_y = 0.0f, right_x = 0.0f, right_y = 0.0f;
  if (!ceiling_chain_endpoints(cg, line_idx, &left_x, &left_y, &right_x, &right_y)) {
    return;
  }

  const float fighter_x = batch->state.pos_x[idx];
  if (fighter_x <= left_x) {
    // Decomp: mpColl_8004C328_Ceiling uses mpCheckLeftWall(edge+{+1,-1}, ecb_right - ecb_top).
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
    const float probe_ax = left_x + k_ceil_edge_wall_probe_x_offset;
    const float probe_ay = left_y + k_ceil_edge_wall_probe_y_offset;
    const float probe_bx = left_x + (ecb->right_rel_x /* top.x == 0 */);
    const float probe_by = left_y + (ecb->side_rel_y - ecb->top_rel_y);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    if (!wall_blocks_ceiling_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_EDGE;
      // Decomp parity: Collide_Edge is used as an aggregate “edge” marker (e.g. floor snap helper
      // mpColl_8004A678_Floor). Nothing consumes it yet in this sim, but setting it whenever we
      // set Collide_{Left,Right}Edge keeps the env flag surface closer to decomp.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  } else if (fighter_x >= right_x) {
    // Decomp: mpColl_8004C328_Ceiling uses mpCheckRightWall(edge+{-1,-1}, ecb_left - ecb_top).
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
    const float probe_ax = right_x - k_ceil_edge_wall_probe_x_offset;
    const float probe_ay = right_y + k_ceil_edge_wall_probe_y_offset;
    const float probe_bx = right_x + (ecb->left_rel_x /* top.x == 0 */);
    const float probe_by = right_y + (ecb->side_rel_y - ecb->top_rel_y);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
    if (!wall_blocks_ceiling_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_EDGE;
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  }
}

void mpcoll_wall_ceil_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    const MslStageCeilingGraph* cg = stage_collision_get_ceiling_graph(stage_id);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];

      const uint8_t prev_wall_kind = batch->state.wall_kind[idx];
      const uint16_t prev_wall_id = batch->state.wall_id[idx];
      const uint16_t prev_ceiling_id = batch->state.ceiling_id[idx];
      const uint8_t prev_had_ceiling =
          (batch->state.coll_prev_env_flags[idx] & (uint32_t)MSL_COLLIDE_CEILING_MASK) ? 1u : 0u;

      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      if (!match_flow_should_stage_collide(action_id)) {
        batch->state.wall_kind[idx] = 0;
        batch->state.ceiling_contact_x[idx] = 0.0f;
        batch->state.ceiling_contact_y[idx] = 0.0f;
        batch->state.ceiling_normal_x[idx] = 0.0f;
        batch->state.ceiling_normal_y[idx] = 0.0f;
        batch->state.wall_contact_x[idx] = 0.0f;
        batch->state.wall_contact_y[idx] = 0.0f;
        batch->state.wall_normal_x[idx] = 0.0f;
        batch->state.wall_normal_y[idx] = 0.0f;
        continue;
      }
      if (is_cliff_hold_action(action_id)) {
        batch->state.wall_kind[idx] = 0;
        batch->state.ceiling_contact_x[idx] = 0.0f;
        batch->state.ceiling_contact_y[idx] = 0.0f;
        batch->state.ceiling_normal_x[idx] = 0.0f;
        batch->state.ceiling_normal_y[idx] = 0.0f;
        batch->state.wall_contact_x[idx] = 0.0f;
        batch->state.wall_contact_y[idx] = 0.0f;
        batch->state.wall_normal_x[idx] = 0.0f;
        batch->state.wall_normal_y[idx] = 0.0f;
        continue;
      }
      if (is_grounded_cliff_option_action(action_id, batch->state.on_ground[idx])) {
        batch->state.wall_kind[idx] = 0;
        batch->state.ceiling_contact_x[idx] = 0.0f;
        batch->state.ceiling_contact_y[idx] = 0.0f;
        batch->state.ceiling_normal_x[idx] = 0.0f;
        batch->state.ceiling_normal_y[idx] = 0.0f;
        batch->state.wall_contact_x[idx] = 0.0f;
        batch->state.wall_contact_y[idx] = 0.0f;
        batch->state.wall_normal_x[idx] = 0.0f;
        batch->state.wall_normal_y[idx] = 0.0f;
        continue;
      }

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t grounded_now = batch->state.on_ground[idx] ? 1u : 0u;
      const uint8_t char_id = batch->state.char_id[idx];
      const uint32_t anim = batch->state.animation_index[idx];
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      const uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);
      const int grounded_left_floor_adj_line_idx =
          grounded_now
              ? grounded_left_wall_floor_adjacent_line_idx(stage_id, batch->state.ground_id[idx])
              : -1;
      const int grounded_right_floor_adj_line_idx =
          grounded_now
              ? grounded_right_wall_floor_adjacent_line_idx(stage_id, batch->state.ground_id[idx])
              : -1;

      const float prev_x = batch->state.prev_pos_x[idx];
      const float prev_y = batch->state.prev_pos_y[idx];
      const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;

      MslEcbWorldPoints cur_ecb = {0};
      MslEcbWorldPoints prev_ecb = {0};
      // Decomp: wall and ceiling collision consume ECB-derived points:
      // - wall: side point (left/right extent at side_y)
      // - ceiling: top point
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_Fixed
      msl_ecb_world_points_sample(&cur_ecb, char_id, anim, ecb_frame, fd, batch->state.pos_x[idx],
                                  batch->state.pos_y[idx], was_grounded);
      msl_ecb_world_points_sample(&prev_ecb, char_id, anim, ecb_frame_prev, fd, prev_x, prev_y,
                                  was_grounded);

      // Reset per-frame wall/ceiling contact outputs; ids persist when detached (mirrors floor_id behavior).
      batch->state.wall_kind[idx] = 0;
      batch->state.wall_contact_x[idx] = 0.0f;
      batch->state.wall_contact_y[idx] = 0.0f;
      batch->state.wall_normal_x[idx] = 0.0f;
      batch->state.wall_normal_y[idx] = 0.0f;
      batch->state.ceiling_contact_x[idx] = 0.0f;
      batch->state.ceiling_contact_y[idx] = 0.0f;
      batch->state.ceiling_normal_x[idx] = 0.0f;
      batch->state.ceiling_normal_y[idx] = 0.0f;

      // Left wall collision (hit by the fighter's right ECB side).
      if (lwg && lwg->lines && lwg->line_count) {
        // Decomp: mpLib_8004E398_LeftWall consumes a (x,y) point; mpColl passes ECB side points.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046224_LeftWall
        const float cur_rx = cur_ecb.right_x;
        const float cur_ry = cur_ecb.right_y;
        const float prev_rx = prev_ecb.right_x;
        const float prev_ry = prev_ecb.right_y;

        int prefer_line_idx = -1;
        if (prev_wall_kind == MSL_WALL_LEFT && prev_wall_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_left_wall_line_index(stage_id, prev_wall_id);
        }

        // Persistence: if we were already attached to a left wall last frame, project first.
        // Decomp: CollData.left_facing_wall.index persists; mpLib_8004E398_LeftWall stays attached.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
        if (!grounded_now && prev_wall_kind == MSL_WALL_LEFT && prefer_line_idx >= 0) {
          float x_corr = 0.0f;
          float nx = -1.0f, ny = 0.0f;
          const int out_line_idx =
              left_wall_e398_project(lwg, prefer_line_idx, cur_rx, cur_ry, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr > 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_LEFT;
            batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_rx + x_corr;
            batch->state.wall_contact_y[idx] = cur_ry;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
          }
        }

        float ix = 0.0f, iy = 0.0f;
        float nx = -1.0f, ny = 0.0f;
        int hit_line_idx = -1;
        if (batch->state.wall_kind[idx] == 0 &&
            wall_sweep_check(lwg, 1, prev_rx, prev_ry, cur_rx, cur_ry, prefer_line_idx,
                             &hit_line_idx, &ix, &iy, &nx, &ny)) {
          float x_corr = 0.0f;
          const int out_line_idx =
              left_wall_e398_project(lwg, hit_line_idx, cur_rx, cur_ry, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr > 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_LEFT;
            batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = ix;
            batch->state.wall_contact_y[idx] = iy;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
          }
        }

        // Airborne wall checks in-engine consider more than just the ECB side point.
        //
        // Why 3 points?
        // - mpColl computes world-space ECB points as `coll->cur_pos + coll->ecb.{left,bottom,top}` and
        //   runs sweep checks against those paths.
        // - Specifically, mpColl_80044E10_RightWall checks the ECB left-side point first (hug), then the
        //   ECB bottom point, then the ECB top point (plus additional vertical/diagonal helpers we do not
        //   yet model in this lite sim). LeftWall uses the symmetric ECB right-side point first.
        // - In this simulator, `msl_ecb_world_points_sample()` provides those same world-space points
        //   (right_x/right_y, bottom_x/bottom_y, top_x/top_y) from ISO-extracted ECB tables.
        //
        // Determinism + selection:
        // - We preserve a deterministic priority order that mirrors mpColl's primary check ordering:
        //   side-point sweep first, then bottom, then top.
        // - Each sweep's line selection is deterministic inside wall_sweep_check(): nearest (dist2),
        //   then prefer prev/adjacent line id, then ISO-derived segment_i tie-break.
        //
        // Decomp refs:
        // - refs/melee/src/melee/mp/mpcoll.c::mpColl_80044E10_RightWall (ordering over ECB points)
        // - refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall / ::mpLib_8004E398_LeftWall (projection)
        if (batch->state.wall_kind[idx] == 0) {
          const float cur_bx = cur_ecb.bottom_x;
          const float cur_by = cur_ecb.bottom_y;
          const float prev_bx = prev_ecb.bottom_x;
          const float prev_by = prev_ecb.bottom_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = -1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(lwg, 1, prev_bx, prev_by, cur_bx, cur_by, prefer_line_idx, &hit2,
                               &ix2, &iy2, &nx2, &ny2) &&
              hit2 != grounded_left_floor_adj_line_idx) {
            float x_corr = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, hit2, cur_bx, cur_by, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr > 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_LEFT;
              batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
            }
          }
        }

        if (batch->state.wall_kind[idx] == 0) {
          const float cur_tx = cur_ecb.top_x;
          const float cur_ty = cur_ecb.top_y;
          const float prev_tx = prev_ecb.top_x;
          const float prev_ty = prev_ecb.top_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = -1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(lwg, 1, prev_tx, prev_ty, cur_tx, cur_ty, prefer_line_idx, &hit2,
                               &ix2, &iy2, &nx2, &ny2)) {
            float x_corr = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, hit2, cur_tx, cur_ty, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr > 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_LEFT;
              batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK;
            }
          }
        }
      }

      // Right wall collision (hit by the fighter's left ECB side). If already touching a left wall,
      // skip the right-wall pass (FD never requires both; keep deterministic).
      if (batch->state.wall_kind[idx] == 0 && rwg && rwg->lines && rwg->line_count) {
        // Decomp: mpLib_8004E684_RightWall consumes a (x,y) point; mpColl passes ECB side points.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800454A4_RightWall
        const float cur_lx = cur_ecb.left_x;
        const float cur_ly = cur_ecb.left_y;
        const float prev_lx = prev_ecb.left_x;
        const float prev_ly = prev_ecb.left_y;

        int prefer_line_idx = -1;
        if (prev_wall_kind == MSL_WALL_RIGHT && prev_wall_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_right_wall_line_index(stage_id, prev_wall_id);
        }

        if (!grounded_now && prev_wall_kind == MSL_WALL_RIGHT && prefer_line_idx >= 0) {
          float x_corr = 0.0f;
          float nx = 1.0f, ny = 0.0f;
          const int out_line_idx =
              right_wall_e684_project(rwg, prefer_line_idx, cur_lx, cur_ly, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr < 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
            batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_lx + x_corr;
            batch->state.wall_contact_y[idx] = cur_ly;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
          }
        }

        float ix = 0.0f, iy = 0.0f;
        float nx = 1.0f, ny = 0.0f;
        int hit_line_idx = -1;
        if (batch->state.wall_kind[idx] == 0 &&
            wall_sweep_check(rwg, 0, prev_lx, prev_ly, cur_lx, cur_ly, prefer_line_idx,
                             &hit_line_idx, &ix, &iy, &nx, &ny)) {
          float x_corr = 0.0f;
          const int out_line_idx =
              right_wall_e684_project(rwg, hit_line_idx, cur_lx, cur_ly, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr < 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
            batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = ix;
            batch->state.wall_contact_y[idx] = iy;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
          }
        }

        // See the decomp notes above (LeftWall section) for why we also sweep ECB bottom/top points.
        // We keep the same deterministic priority order as mpColl_80044E10_RightWall:
        // side-point (left) first, then bottom, then top.
        if (batch->state.wall_kind[idx] == 0) {
          const float cur_bx = cur_ecb.bottom_x;
          const float cur_by = cur_ecb.bottom_y;
          const float prev_bx = prev_ecb.bottom_x;
          const float prev_by = prev_ecb.bottom_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = 1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(rwg, 0, prev_bx, prev_by, cur_bx, cur_by, prefer_line_idx, &hit2,
                               &ix2, &iy2, &nx2, &ny2) &&
              hit2 != grounded_right_floor_adj_line_idx) {
            float x_corr = 0.0f;
            const int out_line_idx =
                right_wall_e684_project(rwg, hit2, cur_bx, cur_by, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr < 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
              batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
            }
          }
        }

        if (batch->state.wall_kind[idx] == 0) {
          const float cur_tx = cur_ecb.top_x;
          const float cur_ty = cur_ecb.top_y;
          const float prev_tx = prev_ecb.top_x;
          const float prev_ty = prev_ecb.top_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = 1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(rwg, 0, prev_tx, prev_ty, cur_tx, cur_ty, prefer_line_idx, &hit2,
                               &ix2, &iy2, &nx2, &ny2)) {
            float x_corr = 0.0f;
            const int out_line_idx =
                right_wall_e684_project(rwg, hit2, cur_tx, cur_ty, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr < 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
              batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
            }
          }
        }
      }

      // Ceiling collision (hit by the fighter's top ECB point).
      if (cg && cg->lines && cg->line_count) {
        // Decomp: mpLib_8004E090_Ceiling consumes the ECB top point.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044C74_Ceiling
        const float cur_tx = cur_ecb.top_x;
        const float cur_ty = cur_ecb.top_y;
        const float prev_tx = prev_ecb.top_x;
        const float prev_ty = prev_ecb.top_y;

        int prefer_line_idx = -1;
        if (prev_ceiling_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_ceiling_line_index(stage_id, prev_ceiling_id);
        }

        // If we had a ceiling contact last frame, attempt a projection first (persistence).
        // Decomp: CollData.ceiling.index persists; mpLib_8004E090_Ceiling is used to stay attached.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
        if (prev_had_ceiling && prefer_line_idx >= 0) {
          float y_corr = 0.0f;
          float nx = 0.0f, ny = -1.0f;
          const int out_line_idx =
              ceiling_e090_project(cg, prefer_line_idx, cur_tx, cur_ty, &y_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (y_corr > 0.0f) {
              y_corr = 0.0f;
            }
            batch->state.pos_y[idx] += y_corr;
            batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
            batch->state.ceiling_normal_x[idx] = nx;
            batch->state.ceiling_normal_y[idx] = ny;
            batch->state.ceiling_contact_x[idx] = cur_tx;
            batch->state.ceiling_contact_y[idx] = cur_ty + y_corr;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
            ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx, &cur_ecb);
          }
        } else {
          float ix = 0.0f, iy = 0.0f;
          float nx = 0.0f, ny = -1.0f;
          int hit_line_idx = -1;
          if (ceiling_sweep_check(cg, prev_tx, prev_ty, cur_tx, cur_ty, prefer_line_idx,
                                  &hit_line_idx, &ix, &iy, &nx, &ny)) {
            float y_corr = 0.0f;
            const int out_line_idx =
                ceiling_e090_project(cg, hit_line_idx, cur_tx, cur_ty, &y_corr, &nx, &ny);
            if (out_line_idx >= 0) {
              if (y_corr > 0.0f) {
                y_corr = 0.0f;
              }
              batch->state.pos_y[idx] += y_corr;
              batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
              batch->state.ceiling_normal_x[idx] = nx;
              batch->state.ceiling_normal_y[idx] = ny;
              batch->state.ceiling_contact_x[idx] = ix;
              batch->state.ceiling_contact_y[idx] = iy;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
              ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx,
                                                   &cur_ecb);
            }
          }
        }
      }
    }
  }
}
