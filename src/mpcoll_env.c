#include "mpcoll_env.h"
#include "motion_state_owners.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_ecb_pose.h"
#include "stage_collision.h"

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

static uint8_t mpcoll_ledge_obstruction_hit(uint32_t stage_id, uint32_t mpcheck_mask, float ax,
                                            float ay, float bx, float by, int16_t ledge_joint_id) {
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
          best_same_joint = (uint8_t)(l->joint_id == ledge_joint_id);
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
          best_same_joint = (uint8_t)(l->joint_id == ledge_joint_id);
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
          best_same_joint = (uint8_t)(l->joint_id == ledge_joint_id);
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

static inline uint32_t ledge_grab_flags_for_fighter(const MslBatch* batch, size_t idx,
                                                    uint32_t stage_id, float coll_prev_x,
                                                    float coll_prev_y, float coll_cur_x,
                                                    float coll_cur_y, float descent_prev_y,
                                                    float descent_cur_y, float ledge_check_dir,
                                                    uint8_t char_id, float fighter_scale_y) {
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
  MslEcbWorldPoints ecb = {0};
  // Ledge queries run inside mpColl_80046904 and consume the already-interpolated live `cd->ecb`.
  // Rebuilding another action-frame/JObj envelope here creates a second collision owner and loses
  // squeeze, lock, substep, and special-pose state already published by the callback.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044164,mpColl_800443C4}
  if (!mpcoll_state_current_ecb_points(batch, idx, &ecb, coll_cur_x, coll_cur_y, 0u)) {
    return 0u;
  }

  // Parity: scale ledge snap params and ECB side offset by fp->x34_scale.y.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  const float snap_x = ch->ledge_snap_x * fighter_scale_y;
  const float snap_y = ch->ledge_snap_y * fighter_scale_y;
  const MslCommonParams* common = msl_common_params();
  // Only ft_80081DD4 temporarily scales ledge_snap_height by p_ftCommonData->x1CC. DamageFall's
  // distinct ft_8008370C callback runs the same 473CC geometry with the fighter's full initialized
  // ledge height, so handler-family identity is too broad here; consume the extracted exact
  // wrapper selector instead.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80081DD4,ft_8008370C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Coll
  // data/motion_state/owners/*.bin::MSLMSO01 coll_wrapper_selector_kind
  const uint8_t damage_height_owner = (uint8_t)(batch->state.live_coll_wrapper_selector_kind[idx] ==
                                                (uint8_t)MSL_COLL_SELECTOR_GA_DAMAGE);
  const float snap_h =
      ch->ledge_snap_height * fighter_scale_y *
      (damage_height_owner && common != NULL ? common->damage_ledge_snap_height_mul : 1.0f);
  const float half_h = k_ledge_half_height_mul * snap_h;

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
          const uint8_t blocked_top =
              mpcoll_ledge_obstruction_hit(stage_id, /*mpcheck_mask=*/0x6u, top_x, top_y, contact_x,
                                           contact_y, ledge_line->joint_id);
          const uint8_t blocked_bot =
              mpcoll_ledge_obstruction_hit(stage_id, /*mpcheck_mask=*/0x6u, coll_cur_x,
                                           bottom_y + k_ledge_obstruction_bottom_probe_dy,
                                           contact_x, contact_y, ledge_line->joint_id);
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
          const uint8_t blocked_top =
              mpcoll_ledge_obstruction_hit(stage_id, /*mpcheck_mask=*/0xAu, top_x, top_y, contact_x,
                                           contact_y, ledge_line->joint_id);
          const uint8_t blocked_bot =
              mpcoll_ledge_obstruction_hit(stage_id, /*mpcheck_mask=*/0xAu, coll_cur_x,
                                           bottom_y + k_ledge_obstruction_bottom_probe_dy,
                                           contact_x, contact_y, ledge_line->joint_id);
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

static void update_ledge_grab_one(MslBatch* batch, int bi, int p, float coll_prev_x,
                                  float coll_prev_y, float coll_cur_x, float coll_cur_y) {
  const int num_players = (int)batch->config.num_players;
  const uint32_t stage_id = batch->state.stage_id[bi];
  const size_t idx = msl_idx_player(bi, p);

  // mpColl only writes ledge-grab bits for airborne collision passes.
  if (batch->state.on_ground[idx]) {
    return;
  }
  if (msl_action_is_thrown_victim(batch->state.action_id[idx])) {
    const uint8_t owner = batch->state.grab_owner_port[idx];
    if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
      return;
    }
  }
  // Decomp: if fp->x2064_ledgeCooldown is nonzero, fighter collision uses the mpColl variant
  // that does not attempt ledge grabs (e.g., mpColl_80047AC8 instead of mpColl_80047E14).
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083090_inline
  if (batch->state.ledge_cooldown[idx] != 0) {
    return;
  }
  // Decomp: mpColl suppresses ledge-grab checks while "on edge" (Collide_LeftEdge/RightEdge).
  // refs/melee/src/melee/mp/mpcoll.c (mpColl_80046904 ledge-grab block; `on_edge` gate)
  if (batch->state.coll_env_flags[idx] &
      ((uint32_t)MSL_COLLIDE_LEFT_EDGE | (uint32_t)MSL_COLLIDE_RIGHT_EDGE)) {
    return;
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
  if (batch->state.live_coll_wrapper_selector_kind[idx] ==
          (uint8_t)MSL_COLL_SELECTOR_AIR_LEDGE_BOTH ||
      batch->state.live_coll_wrapper_selector_kind[idx] ==
          (uint8_t)MSL_COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH) {
    ledge_check_dir = 0.0f;
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
  const float descent_prev_y = coll_prev_y;
  const float descent_cur_y = coll_cur_y;
  // Decomp: fp->x34_scale.y is initialized from Player_GetModelScale (per-character default
  // scale in most cases). Older seed corpora may still seed fighter_scale_y as 1.0 for all
  // fighters; prefer the ISO-extracted per-character default in that case.
  // refs/melee/src/melee/ft/fighter.c (Player_GetModelScale init path)
  // src/api.h::MslSeed (fighter_scale_y note)
  const float scale_y = batch->state.fighter_scale_y[idx];
  const uint32_t flags = ledge_grab_flags_for_fighter(
      batch, idx, stage_id, coll_prev_x, coll_prev_y, coll_cur_x, coll_cur_y, descent_prev_y,
      descent_cur_y, ledge_check_dir, batch->state.char_id[idx], scale_y);
  batch->state.coll_env_flags[idx] |= flags;
}

void mpcoll_env_update_ledge_grab_one(MslBatch* batch, int bi, int p, float prev_x, float prev_y,
                                      float cur_x, float cur_y) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  update_ledge_grab_one(batch, bi, p, prev_x, prev_y, cur_x, cur_y);
}

void mpcoll_env_update_ledge_grab(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.live_coll_callback_ran[idx] != 0u) {
        continue;
      }
      update_ledge_grab_one(batch, bi, p, batch->state.prev_pos_x[idx],
                            batch->state.prev_pos_y[idx], batch->state.coll_stage_cur_pos_x[idx],
                            batch->state.coll_stage_cur_pos_y[idx]);
    }
  }
}
