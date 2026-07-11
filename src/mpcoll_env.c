#include "mpcoll_env.h"
#include "motion_state_owners.h"
#include "char_registry.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "msl_math.h"
#include "mtx34.h"
#include "mpcoll_ecb_points.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "throw_flow.h"

static inline void mpcoll_env_update_rot_bounds(float x, float y, float* io_min_x, float* io_max_x,
                                                float* io_min_y, float* io_max_y) {
  if (x < *io_min_x) {
    *io_min_x = x;
  }
  if (x > *io_max_x) {
    *io_max_x = x;
  }
  if (y < *io_min_y) {
    *io_min_y = y;
  }
  if (y > *io_max_y) {
    *io_max_y = y;
  }
}

static inline uint8_t mpcoll_env_specialhi_rotate_collision_point_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t frame_u16,
    uint16_t part_id, float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }
  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, 2u, m) != 0) {
    return 0u;
  }
  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline uint8_t mpcoll_env_specialhi_try_sample_jobj_ecb_points(
    MslEcbWorldPoints* out, const MslBatch* batch, size_t idx, uint8_t char_id, uint32_t anim,
    uint16_t frame_u16, float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL || !(anim <= 0xFFFFu)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim;
  const float model_scaling =
      (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[idx] * model_scaling;

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  for (uint16_t pi = 0; pi < ch->ecb_joint_count; pi++) {
    const uint16_t part_id = ch->ecb_joints[pi];
    float m[12];
    if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, part_id, m) != 0) {
      return 0u;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    msl_mtx34_mul_point(m, origin, &x, &y, &z);
    x *= model_scale;
    y *= model_scale;
    z *= model_scale;
    (void)mpcoll_env_specialhi_rotate_collision_point_xrotn(batch, idx, char_id, msid, frame_u16,
                                                            part_id, model_scale, &x, &y, &z);

    // Decomp: mpColl_LoadECB_JObj samples live JObj world points via lb_8000B1CC. For
    // Firefox/Firebird, ftFox_SpecialHi_RotateModel writes FtPart_XRotN, so ledge-grab AABBs must
    // use the same rotated collision-pose ECB already consumed by the wall/ceiling owner.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_RotateModel
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044164}
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    const float rel_x = facing_dir * z;
    const float rel_y = y;
    if (!have) {
      min_x = max_x = rel_x;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      mpcoll_env_update_rot_bounds(rel_x, rel_y, &min_x, &max_x, &min_y, &max_y);
    }
  }
  if (!have) {
    return 0u;
  }

  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(max_x - min_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * ecb_width;
    max_x = half_width;
    min_x = -half_width;
  }
  if (max_x < 2.0f) {
    max_x = 2.0f;
  }
  if (min_x > -2.0f) {
    min_x = -2.0f;
  }
  if (min_y < 0.0f) {
    min_y = 0.0f;
  }
  const float side_rel_y = ch->ecb_side_y_offset + 0.5f * (min_y + max_y);

  out->left_rel_x = min_x;
  out->right_rel_x = max_x;
  out->bottom_rel_y = min_y;
  out->top_rel_y = max_y;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + min_y;
  out->top_x = pos_x;
  out->top_y = pos_y + max_y;
  out->left_x = pos_x + min_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + max_x;
  out->right_y = pos_y + side_rel_y;
  return 1u;
}

// Decomp constants / shapes:
// - mpColl_80044164 / mpColl_800443C4 build a swept AABB using:
//   - half_height = 0.5F * ledge_snap_height
//   - a per-side horizontal extent using ledge_snap_x and ECB left/right.
//   refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
//   refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
static const float k_ledge_half_height_mul = 0.5f;
// Decomp: `contact.x - edge.x < 5.0F` (and mirrored).
// refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
// refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
static const float k_ledge_edge_dx_max = 5.0f;

// Decomp constants used by the ledge-grab obstruction checks in mpColl_80044164 / mpColl_800443C4:
// - mpLineIntersection{H,V} clamps small off-end travel within ±0.1 before returning false.
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionV
// - mpCheckCeiling treats ceilings as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
// - mpCheck{Left,Right}Wall treats walls as vertical when |x0 - x1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
//   refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
static const float k_line_axis_thresh = 0.0001f;
static const float k_line_end_clamp = 0.1f;

// Decomp: mpColl_80044164 / mpColl_800443C4 probe a second "bottom" ray with a -2.0F Y offset.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
// refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
static const float k_ledge_obstruction_bottom_probe_dy = -2.0f;

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

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
  // - ceiling checks only when ay <= by (rising / non-falling)
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

static inline uint8_t stage_line_shares_endpoint(float ax0, float ay0, float ax1, float ay1,
                                                 float bx, float by) {
  return (ax0 == bx && ay0 == by) || (ax1 == bx && ay1 == by);
}

static uint8_t mpcoll_ledge_obstruction_hit(uint32_t stage_id, uint32_t mpcheck_mask, float ax,
                                            float ay, float bx, float by, float edge_x,
                                            float edge_y) {
  // Decomp: mpCheckMultiple finds the closest intersection (by dist2) among the requested kinds.
  // mpColl_80044164 / mpColl_800443C4 allow a hit when mpJointFromLine(hit) == mpJointFromLine(ledge),
  // otherwise the ledge grab is rejected.
  // refs/melee/src/melee/mp/mplib.c::mpCheckMultiple
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  float best_dist2 = FLT_MAX;
  uint8_t best_same_joint = 0;

  // Ceiling (mpCheckCeiling): only consider when rising / non-falling.
  if ((mpcheck_mask & 0x2u) && (ay <= by)) {
    const MslStageCeilingGraph* cg = stage_collision_get_ceiling_graph(stage_id);
    if (cg && cg->lines && cg->line_count) {
      for (size_t li = 0; li < cg->line_count; li++) {
        const MslStageCeilingLine* l = &cg->lines[li];
        const float x0 = l->x0;
        const float y0 = l->y0;
        const float x1 = l->x1;
        const float y1 = l->y1;
        float ix = 0.0f, iy = 0.0f;
        uint8_t hit = 0;
        if (fabsf(y0 - y1) > k_line_axis_thresh) {
          hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
        } else {
          hit = intersect_horiz_clamped(x0, y0, x1, ax, ay, bx, by, /*require_rising=*/1, &ix, &iy);
        }
        if (!hit) {
          continue;
        }
        const float dx = ix - ax;
        const float dy = iy - ay;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < best_dist2) {
          best_dist2 = dist2;
          best_same_joint = stage_line_shares_endpoint(l->x0, l->y0, l->x1, l->y1, edge_x, edge_y);
        }
      }
    }
  }

  // Left wall (mpCheckLeftWall): only consider when moving right / non-left.
  if ((mpcheck_mask & 0x4u) && (ax <= bx)) {
    const MslStageWallGraph* wg = stage_collision_get_left_wall_graph(stage_id);
    if (wg && wg->lines && wg->line_count) {
      for (size_t li = 0; li < wg->line_count; li++) {
        const MslStageWallLine* l = &wg->lines[li];
        const float x0 = l->x0;
        const float y0 = l->y0;
        const float x1 = l->x1;
        const float y1 = l->y1;
        float ix = 0.0f, iy = 0.0f;
        uint8_t hit = 0;
        if (fabsf(x0 - x1) > k_line_axis_thresh) {
          hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
        } else {
          hit = intersect_vert_clamped(x0, y0, y1, ax, ay, bx, by, /*require_moving_right=*/1, &ix,
                                       &iy);
        }
        if (!hit) {
          continue;
        }
        const float dx = ix - ax;
        const float dy = iy - ay;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < best_dist2) {
          best_dist2 = dist2;
          best_same_joint = stage_line_shares_endpoint(l->x0, l->y0, l->x1, l->y1, edge_x, edge_y);
        }
      }
    }
  }

  // Right wall (mpCheckRightWall): only consider when moving left / non-right.
  if ((mpcheck_mask & 0x8u) && (ax >= bx)) {
    const MslStageWallGraph* wg = stage_collision_get_right_wall_graph(stage_id);
    if (wg && wg->lines && wg->line_count) {
      for (size_t li = 0; li < wg->line_count; li++) {
        const MslStageWallLine* l = &wg->lines[li];
        const float x0 = l->x0;
        const float y0 = l->y0;
        const float x1 = l->x1;
        const float y1 = l->y1;
        float ix = 0.0f, iy = 0.0f;
        uint8_t hit = 0;
        if (fabsf(x0 - x1) > k_line_axis_thresh) {
          hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
        } else {
          hit = intersect_vert_clamped(x0, y0, y1, ax, ay, bx, by, /*require_moving_right=*/0, &ix,
                                       &iy);
        }
        if (!hit) {
          continue;
        }
        const float dx = ix - ax;
        const float dy = iy - ay;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < best_dist2) {
          best_dist2 = dist2;
          best_same_joint = stage_line_shares_endpoint(l->x0, l->y0, l->x1, l->y1, edge_x, edge_y);
        }
      }
    }
  }

  if (best_dist2 == FLT_MAX) {
    return 0u;
  }
  return best_same_joint ? 0u : 1u;
}
static inline uint8_t mplib_aabb_overlaps_line(float left, float bottom, float right, float top,
                                               float x0, float y0, float x1, float y1) {
  // Decomp: mpLib_80051BA8_Floor performs an AABB-vs-segment-bounds overlap check using midpoint
  // distance with a strict `<` comparison (not `<=`).
  // refs/melee/src/melee/mp/mplib.c::mpLib_80051BA8_Floor
  const float line_left = (x0 < x1) ? x0 : x1;
  const float line_right = (x0 < x1) ? x1 : x0;
  const float line_bottom = (y0 < y1) ? y0 : y1;
  const float line_top = (y0 < y1) ? y1 : y0;

  const float dist_h = fabsf((line_right + line_left) - (right + left));
  if (!(dist_h < (line_right - line_left) + (right - left))) {
    return 0u;
  }
  const float dist_v = fabsf((line_top + line_bottom) - (top + bottom));
  if (!(dist_v < (line_top - line_bottom) + (top - bottom))) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t mplib_select_ledge_floor_contact(uint32_t stage_id, uint16_t floor_skip,
                                                       int dir, float left, float bottom,
                                                       float right, float top,
                                                       const MslStageFloorLine** out_line,
                                                       float* out_contact_x, float* out_contact_y) {
  // Decomp: mpLib_80051BA8_Floor scans all ledge floor lines overlapping the query AABB, selecting
  // the smallest raw v0.x when dir>0 or largest raw v1.x when dir<0. The source helper uses raw
  // MapLine v0/v1 orientation for that selection, then clamps only out_vec.x into the query box;
  // out_vec.y stays at the raw endpoint. MSLSTG01 runtime floor endpoints are normalized to
  // x0<=x1, so this helper must explicitly consume the retained raw orientation for sloped ledges.
  // `line_id_skip` is `CollData.floor_skip`; walking/falling off a ledge floor must not immediately
  // reselect that same floor as a ledge-grab candidate.
  // refs/melee/src/melee/mp/mplib.c::mpLib_80051BA8_Floor
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044164,mpColl_800443C4}
  if (out_line) {
    *out_line = NULL;
  }
  if (out_contact_x) {
    *out_contact_x = 0.0f;
  }
  if (out_contact_y) {
    *out_contact_y = 0.0f;
  }

  // Limit each directional cliff helper to the global ledge endpoint with matching outside-stage
  // direction: dir>0 -> stage-left ledge, dir<0 -> stage-right ledge.
  const MslStageFloorLine* cand0 = stage_collision_get_ledge_floor_line(stage_id, dir > 0 ? 0 : 1);

  const MslStageFloorLine* best = NULL;
  float best_x = (dir > 0) ? INFINITY : -INFINITY;
  float best_world_x = 0.0f;
  float best_world_y = 0.0f;

  const MslStageFloorLine* cands[1] = {cand0};
  for (int i = 0; i < 1; i++) {
    const MslStageFloorLine* l = cands[i];
    if (l == NULL) {
      continue;
    }
    if (floor_skip != 0xFFFFu && l->segment_i == floor_skip) {
      continue;
    }
    if (!mplib_aabb_overlaps_line(left, bottom, right, top, l->x0, l->y0, l->x1, l->y1)) {
      continue;
    }
    if (dir > 0) {
      if (best_x > l->raw_x0) {
        best = l;
        best_x = l->raw_x0;
        best_world_x = l->x0;
        best_world_y = l->y0;
      }
    } else if (dir < 0) {
      if (best_x < l->raw_x1) {
        best = l;
        best_x = l->raw_x1;
        best_world_x = l->x1;
        best_world_y = l->y1;
      }
    }
  }

  if (best == NULL) {
    return 0u;
  }

  // Raw v0/v1 selects the source endpoint orientation, but runtime collision and fighter poses are
  // in world-scaled coordinates. Return the matching normalized world endpoint so the subsequent
  // `contact - edge` and bottom-height gates compare like units.
  // refs/melee/src/melee/mp/mplib.c::mpLib_80051BA8_Floor
  // data/stages/bin/*.bin::MSLSTG01 raw_x*/x* endpoint contract
  float cx = best_world_x;
  if (cx > right) {
    cx = right;
  } else if (cx < left) {
    cx = left;
  }

  if (out_line) {
    *out_line = best;
  }
  if (out_contact_x) {
    *out_contact_x = cx;
  }
  if (out_contact_y) {
    *out_contact_y = best_world_y;
  }
  return 1u;
}

static inline uint32_t ledge_grab_flags_for_fighter(
    const MslBatch* batch, size_t idx, uint16_t action_id, uint32_t stage_id, float coll_prev_x,
    float coll_prev_y, float coll_cur_x, float coll_cur_y, float descent_prev_y,
    float descent_cur_y, float ledge_check_dir, float model_facing_dir, uint8_t char_id,
    uint32_t animation_index, float anim_frame_f32, float fighter_scale_y) {
  // Decomp gate: mpColl only attempts ledge-grab checks while moving downward.
  // Decomp: uses CollData.prev_pos.y / cur_pos.y as managed inside the collision substep loop
  // (mpColl_80043754), not previous-frame y.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904 (coll->cur_pos.y < coll->prev_pos.y)
  if (!(descent_cur_y < descent_prev_y)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(char_id);
  if (ch == NULL) {
    return 0u;
  }
  const uint16_t desired_frame = msl_ecb_frame_u16_from_anim_frame(anim_frame_f32);
  const uint16_t ecb_frame = desired_frame;
  MslEcbWorldPoints ecb = {0};
  // Decomp: mpColl ledge-grab checks consume ECB extents (left/right x) and the ECB bottom point.
  // In this lite sim, msl_ecb_world_points_sample sources these from the ISO-extracted ECB tables
  // (data/ecb/*) and char attrs (data/characters/*).
  //
  // Decomp: the ledge AABB builders consume `cd->ecb` (interpolated ECB), not `cd->desired_ecb`.
  //
  // MSL materializes this ledge-grab `cd->ecb` sample from the same ISO ECB tables and action-frame
  // timebase used by CollData floor/wall/ceiling callbacks. The sampled state is bounded to
  // mpColl_80044164/mpColl_800443C4 ledge AABB construction; full per-substep ECB history is owned
  // by a later moving/substep collision pass.
  // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  // mpCollSetFacingDir writes the requested ledge-check direction into CollData, but
  // mpColl_LoadECB_JObj samples the fighter's already-faced JObj hierarchy. CLIFFCATCH_BOTH
  // therefore changes only which ledge helpers run; it does not mirror or symmetrize the ECB.
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollSetFacingDir,mpColl_LoadECB_JObj}
  msl_ecb_world_points_sample(&ecb, char_id, animation_index, ecb_frame, model_facing_dir,
                              coll_cur_x, coll_cur_y,
                              /*lock_bottom_to_zero=*/0u);
  // The ledge AABB gates consume cd->ecb.bottom.x, but mpCollInterpolateECB runs with
  // time = 1/(steps-step), i.e. time=1.0 on the final (usually only) substep - cd->ecb
  // snaps to desired_ecb, whose bottom point is centered (bottom.x = 0 relative to
  // cur_pos). Sampling the instantaneous POSED bottom-X table here was over-fit: marth's
  // JumpAerial flip swings the posed bottom +/-4.7 units across a single frame and pushed
  // the bottom past the ledge-side gate, costing real ledge catches (the JumpAerialF ->
  // CliffCatch rollout cluster, freq 22).
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044164,mpColl_800443C4}
  ecb.bottom_x = coll_cur_x;
  const uint8_t env_fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id);
  if (env_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI ||
      env_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL) {
    (void)mpcoll_env_specialhi_try_sample_jobj_ecb_points(&ecb, batch, idx, char_id,
                                                          animation_index, ecb_frame,
                                                          model_facing_dir, coll_cur_x, coll_cur_y);
  }

  // Data-contract guardrail: if ECB extents/bottom tables do not contain an entry for this
  // (char_id, msid), msl_ecb_world_points_sample returns all-zeros. Do not attempt ledge-grab AABB
  // generation in that case; the decomp always has valid ECB data, and clamping a missing sample
  // to ±2 (mpColl_LoadECB_JObj parity) would create false-positive ledge grabs.
  // data/ecb/*_extents.bin + data/ecb/*_bottom.bin (see ecb_tables.c::msl_ecb_extents_rel)
  if (ecb.left_rel_x == 0.0f && ecb.right_rel_x == 0.0f && ecb.bottom_rel_y == 0.0f &&
      ecb.top_rel_y == 0.0f) {
    return 0u;
  }

  // Parity: scale ledge snap params and ECB side offset by fp->x34_scale.y.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  const float snap_x = ch->ledge_snap_x * fighter_scale_y;
  const float snap_y = ch->ledge_snap_y * fighter_scale_y;
  const float snap_h = ch->ledge_snap_height * fighter_scale_y;
  const float half_h = k_ledge_half_height_mul * snap_h;

  // Parity: ECB side-point offset is scaled by fp->x34_scale.y (JObj ECB source path).
  // Note: ledge-grab AABB does not consume side point Y, but keep the sampled ECB self-consistent.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  const float side_off_unscaled = ch->ecb_side_y_offset;
  const float side_off_scaled = side_off_unscaled * fighter_scale_y;
  const float side_off_delta = side_off_scaled - side_off_unscaled;
  if (side_off_delta != 0.0f) {
    ecb.side_rel_y += side_off_delta;
    ecb.left_y += side_off_delta;
    ecb.right_y += side_off_delta;
  }

  // Decomp parity: mpColl_LoadECB_JObj postprocesses left/right extents for the JObj ECB source.
  // The ledge-grab path calls mpColl_LoadECB_inline(coll, 6) (flags=6), so:
  // - no ±2 expansion (flags&0b100 is set),
  // - symmetrize left/right around 0 when width is "small" (ABS(dx) < max(4, x12C)),
  // - clamp to at least ±2 when not forced to ±1 (flags&0b1000 is clear).
  //
  // x12C is set in fighter init for JObj ECB source:
  // - ft_80081B38 sets coll->ecb_source.x12C = 10.0F * fp->x34_scale.y
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  {
    const float phi_f1 = (4.0f > (10.0f * fighter_scale_y)) ? 4.0f : (10.0f * fighter_scale_y);
    const float phi_f2 = fabsf(ecb.right_rel_x - ecb.left_rel_x);
    if (phi_f2 < phi_f1) {
      const float half_w = 0.5f * phi_f2;
      ecb.right_rel_x = half_w;
      ecb.left_rel_x = -half_w;
    }

    // Clamp to at least ±2.0F.
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj (right_x < +2 ? +2 : right_x, etc.)
    if (ecb.right_rel_x < 2.0f) {
      ecb.right_rel_x = 2.0f;
    }
    if (ecb.left_rel_x > -2.0f) {
      ecb.left_rel_x = -2.0f;
    }
    ecb.left_x = coll_cur_x + ecb.left_rel_x;
    ecb.right_x = coll_cur_x + ecb.right_rel_x;
  }

  const float min_x = (coll_prev_x < coll_cur_x) ? coll_prev_x : coll_cur_x;
  const float max_x = (coll_prev_x < coll_cur_x) ? coll_cur_x : coll_prev_x;
  const float min_y = (coll_prev_y < coll_cur_y) ? coll_prev_y : coll_cur_y;
  const float max_y = (coll_prev_y < coll_cur_y) ? coll_cur_y : coll_prev_y;

  uint32_t out = 0u;

  // Left ledge grab (must be facing toward +X / into stage).
  // Decomp: mpColl_80047E14 checks left ledge when facing_dir==1 (or 0).
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
  if (ledge_check_dir >= 0.0f) {
    // Decomp AABB build (mpColl_80044164):
    // left = min(prev_x, cur_x)
    // right = ledge_snap_x + (max(prev_x, cur_x) + ecb.right.x)
    // bottom/top computed from (pos.y + ledge_snap_y) ± half_height.
    const float aabb_l = min_x;
    const float aabb_r = snap_x + (max_x + ecb.right_rel_x);
    const float aabb_b = (min_y + snap_y) - half_h;
    const float aabb_t = (max_y + snap_y) + half_h;

    const MslStageFloorLine* ledge_line = NULL;
    float contact_x = 0.0f, contact_y = 0.0f;
    const uint16_t floor_skip = (batch->state.floor_skip_segment_id != NULL)
                                    ? batch->state.floor_skip_segment_id[idx]
                                    : 0xFFFFu;
    if (mplib_select_ledge_floor_contact(stage_id, floor_skip, /*dir=*/+1, aabb_l, aabb_b, aabb_r,
                                         aabb_t, &ledge_line, &contact_x, &contact_y)) {
      const float edge_x = ledge_line->x0;
      const float edge_y = ledge_line->y0;
      // Decomp: contact.x is required to be close to the floor endpoint:
      // `cd->contact.x - edge.x < 5.0F`.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
      // Decomp: `cd->cur_pos.x + cd->ecb.bottom.x < edge.x` (the posed bottom point).
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
      if ((contact_x - edge_x) < k_ledge_edge_dx_max && ecb.bottom_x < edge_x &&
          ecb.bottom_y < edge_y) {
        // Decomp: mpColl_80044164 includes additional obstruction checks using mpCheckMultiple
        // (checks=6) from ECB top and a bottom-2 probe to cd->contact.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
        // refs/melee/src/melee/mp/mplib.c::mpCheckMultiple
        const float bottom_y = ecb.bottom_y;
        // Decomp fast-accept gate:
        // - mpColl_80044164 accepts immediately when `cur_pos.y + ecb.bottom.y > contact.y`.
        // - Obstruction mpCheckMultiple probes are only evaluated on the opposite branch.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
        uint8_t clear_path = (bottom_y > contact_y) ? 1u : 0u;
        if (!clear_path) {
          const float top_x = coll_cur_x;  // ecb.top.x == 0
          const float top_y = ecb.top_y;
          const uint8_t blocked_top = mpcoll_ledge_obstruction_hit(
              stage_id, /*mpcheck_mask=*/0x6u, top_x, top_y, contact_x, contact_y, edge_x, edge_y);
          const uint8_t blocked_bot = mpcoll_ledge_obstruction_hit(
              stage_id, /*mpcheck_mask=*/0x6u, coll_cur_x,
              bottom_y + k_ledge_obstruction_bottom_probe_dy, contact_x, contact_y, edge_x, edge_y);
          clear_path = (uint8_t)(!(blocked_top || blocked_bot));
        }
        if (clear_path) {
          out |= MSL_COLLIDE_LEFT_LEDGE_GRAB;
        }
      }
    }
  }

  // Right ledge grab (must be facing toward -X / into stage).
  // Decomp: mpColl_80047E14 checks right ledge when facing_dir==-1 (or 0).
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
  if (ledge_check_dir <= 0.0f) {
    // Decomp AABB build (mpColl_800443C4), with snap_x negated:
    // right = max(prev_x, cur_x)
    // left = (-ledge_snap_x) + (min(prev_x, cur_x) + ecb.left.x)
    const float aabb_r = max_x;
    const float aabb_l = (-snap_x) + (min_x + ecb.left_rel_x);
    const float aabb_b = (min_y + snap_y) - half_h;
    const float aabb_t = (max_y + snap_y) + half_h;

    const MslStageFloorLine* ledge_line = NULL;
    float contact_x = 0.0f, contact_y = 0.0f;
    const uint16_t floor_skip = (batch->state.floor_skip_segment_id != NULL)
                                    ? batch->state.floor_skip_segment_id[idx]
                                    : 0xFFFFu;
    if (mplib_select_ledge_floor_contact(stage_id, floor_skip, /*dir=*/-1, aabb_l, aabb_b, aabb_r,
                                         aabb_t, &ledge_line, &contact_x, &contact_y)) {
      const float edge_x = ledge_line->x1;
      const float edge_y = ledge_line->y1;
      // Decomp: `edge.x - cd->contact.x < 5.0F`.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
      // Decomp: `cd->cur_pos.x + cd->ecb.bottom.x > edge.x` (the posed bottom point).
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
      if ((edge_x - contact_x) < k_ledge_edge_dx_max && ecb.bottom_x > edge_x &&
          ecb.bottom_y < edge_y) {
        // Decomp: mpColl_800443C4 includes additional obstruction checks using mpCheckMultiple
        // (checks=10) from ECB top and a bottom-2 probe to cd->contact.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
        // refs/melee/src/melee/mp/mplib.c::mpCheckMultiple
        const float bottom_y = ecb.bottom_y;
        // Decomp fast-accept gate mirrored from mpColl_80044164:
        // - mpColl_800443C4 accepts immediately when `cur_pos.y + ecb.bottom.y > contact.y`.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
        uint8_t clear_path = (bottom_y > contact_y) ? 1u : 0u;
        if (!clear_path) {
          const float top_x = coll_cur_x;  // ecb.top.x == 0
          const float top_y = ecb.top_y;
          const uint8_t blocked_top = mpcoll_ledge_obstruction_hit(
              stage_id, /*mpcheck_mask=*/0xAu, top_x, top_y, contact_x, contact_y, edge_x, edge_y);
          const uint8_t blocked_bot = mpcoll_ledge_obstruction_hit(
              stage_id, /*mpcheck_mask=*/0xAu, coll_cur_x,
              bottom_y + k_ledge_obstruction_bottom_probe_dy, contact_x, contact_y, edge_x, edge_y);
          clear_path = (uint8_t)(!(blocked_top || blocked_bot));
        }
        if (clear_path) {
          out |= MSL_COLLIDE_RIGHT_LEDGE_GRAB;
        }
      }
    }
  }

  return out;
}

void mpcoll_env_update_ledge_grab(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // mpColl only writes ledge-grab bits for airborne collision passes.
      if (batch->state.on_ground[idx]) {
        continue;
      }
      if (msl_action_is_thrown_victim(batch->state.action_id[idx])) {
        const uint8_t owner = batch->state.grab_owner_port[idx];
        if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
          // Attached Thrown* rows do not run mpColl ledge-grab checks in decomp because their Coll
          // callbacks are empty while the attachment callback owns position.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
          //   ftCo_ThrownF_Coll,ftCo_ThrownB_Coll,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Coll
          // }
          continue;
        }
      }
      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        continue;
      }
      // Decomp: if fp->x2064_ledgeCooldown is nonzero, fighter collision uses the mpColl variant
      // that does not attempt ledge grabs (e.g., mpColl_80047AC8 instead of mpColl_80047E14).
      // refs/melee/src/melee/ft/ft_081B.c::ft_80083090_inline
      if (batch->state.ledge_cooldown[idx] != 0) {
        continue;
      }
      // Decomp: mpColl suppresses ledge-grab checks while "on edge" (Collide_LeftEdge/RightEdge).
      // refs/melee/src/melee/mp/mpcoll.c (mpColl_80046904 ledge-grab block; `on_edge` gate)
      if (batch->state.coll_env_flags[idx] &
          ((uint32_t)MSL_COLLIDE_LEFT_EDGE | (uint32_t)MSL_COLLIDE_RIGHT_EDGE)) {
        continue;
      }

      // Decomp: mpColl_80044164 / mpColl_800443C4 consume CollData.prev_pos and CollData.cur_pos
      // as managed inside mpColl_80043754's collision substep loop. Those are "previous substep
      // within collision" snapshots, not previous-frame snapshots.
      //
      // MSL materializes the collision-stage prev/cur pair used by the static ledge AABB sweep:
      // - coll_stage_prev_pos: position immediately before stage_collision_apply() this frame
      // - coll_stage_cur_pos: position immediately after stage_collision_apply() this frame
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
      // Note: mpcoll_env_update_ledge_grab is scheduled post-collision so ledge_try_catch can see
      // the final (post-collision) state, but the swept AABB inputs match the collision-stage sweep.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
      const float model_facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
      float ledge_check_dir = model_facing_dir;
      // Source callbacks that pass CLIFFCATCH_BOTH (0) into ft_CheckGroundAndLedge make
      // mpColl_80046904 test both ledge sides instead of only the fighter's current facing. The
      // generated owner is callback-argument identity; ECB sampling still uses model_facing_dir.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
      //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::doAirColl
      // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
      if (msl_motion_state_class2_has(batch->state.char_id[idx], batch->state.action_id[idx],
                                      MSL_MS_CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL)) {
        ledge_check_dir = 0.0f;
      }
      // Puff Coll direction modes: air Sing passes CLIFFCATCH_BOTH (0) into
      // ft_CheckGroundAndLedge, so both ledge sides are live regardless of facing; Rollout
      // AirChargeRelease passes the roll direction (mv x34.x) into ft_8008239C, not facing.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialHi.c::ftPr_SpecialAirHi_Coll
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
      if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_PUFF) {
        const uint16_t pr_a = batch->state.action_id[idx];
        if (pr_a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_L ||
            pr_a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_R) {
          ledge_check_dir = 0.0f;
        } else if (pr_a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE) {
          ledge_check_dir = (batch->state.puff_rollout_dir[idx] < 0) ? -1.0f : 1.0f;
        }
      }
      // Decomp: mpColl_80046904 runs inside the mpColl_80043754 substep loop, which updates:
      // - cd->prev_pos (previous substep position)
      // - cd->cur_pos  (current substep position after collision correction)
      // and checks `cd->cur_pos.y < cd->prev_pos.y` before attempting ledge grabs.
      //
      // MSL materializes the mpColl prev/cur pair for the static ledge check from within-frame
      // pre/post integration positions:
      // - prev_pos_*: position at start of physics_integrate() this frame (pre-integration)
      // - coll_stage_cur_pos_*: position immediately after stage_collision_apply() this frame
      // Note: prev_pos_* is written in physics_integrate(), so it is not a previous-frame snapshot.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
      const float coll_prev_x = batch->state.prev_pos_x[idx];
      const float coll_prev_y = batch->state.prev_pos_y[idx];
      const float coll_cur_x = batch->state.coll_stage_cur_pos_x[idx];
      const float coll_cur_y = batch->state.coll_stage_cur_pos_y[idx];
      const float descent_prev_y = coll_prev_y;
      const float descent_cur_y = coll_cur_y;
      // Decomp: fp->x34_scale.y is initialized from Player_GetModelScale (per-character default
      // scale in most cases). Older seed corpora may still seed fighter_scale_y as 1.0 for all
      // fighters; prefer the ISO-extracted per-character default in that case.
      // refs/melee/src/melee/ft/fighter.c (Player_GetModelScale init path)
      // src/api.h::MslSeed (fighter_scale_y note)
      const float scale_y = batch->state.fighter_scale_y[idx];
      const uint32_t flags = ledge_grab_flags_for_fighter(
          batch, idx, batch->state.action_id[idx], stage_id, coll_prev_x, coll_prev_y, coll_cur_x,
          coll_cur_y, descent_prev_y, descent_cur_y, ledge_check_dir, model_facing_dir,
          batch->state.char_id[idx], batch->state.animation_index[idx],
          batch->state.anim_frame_f32[idx], scale_y);
      batch->state.coll_env_flags[idx] |= flags;
    }
  }
}
