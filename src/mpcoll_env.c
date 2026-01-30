#include "mpcoll_env.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "char_params.h"
#include "coll_env_flags.h"
#include "mpcoll_ecb_points.h"
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

static inline float clampf(float x, float lo, float hi) {
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline uint32_t ledge_grab_flags_for_fighter(uint32_t stage_id, float cur_x, float cur_y,
                                                    float prev_x, float prev_y, float facing_dir,
                                                    uint8_t char_id, uint32_t animation_index,
                                                    float anim_frame_f32) {
  // Decomp gate: mpColl only attempts ledge-grab checks while moving downward.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14 (cur_pos.y < prev_pos.y)
  if (!(cur_y < prev_y)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params(char_id);
  if (ch == NULL) {
    return 0u;
  }

  const MslStageFloorLine* ledge_left = stage_collision_get_ledge_floor_line(stage_id, 0);
  const MslStageFloorLine* ledge_right = stage_collision_get_ledge_floor_line(stage_id, 1);
  if (ledge_left == NULL && ledge_right == NULL) {
    return 0u;
  }

  const uint16_t ecb_frame = msl_ecb_frame_u16_from_anim_frame(anim_frame_f32);
  MslEcbWorldPoints ecb = {0};
  // Decomp: mpColl ledge-grab checks consume ECB extents (left/right x) and the ECB bottom point.
  // In this lite sim, msl_ecb_world_points_sample sources these from the ISO-extracted ECB tables
  // (data/ecb/*) and char attrs (data/characters/*).
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800443C4
  msl_ecb_world_points_sample(&ecb, char_id, animation_index, ecb_frame, facing_dir, cur_x, cur_y,
                              /*lock_bottom_to_zero=*/0u);

  const float half_h = k_ledge_half_height_mul * ch->ledge_snap_height;
  const float min_x = (prev_x < cur_x) ? prev_x : cur_x;
  const float max_x = (prev_x < cur_x) ? cur_x : prev_x;
  const float min_y = (prev_y < cur_y) ? prev_y : cur_y;
  const float max_y = (prev_y < cur_y) ? cur_y : prev_y;

  uint32_t out = 0u;

  // Left ledge grab (must be facing toward +X / into stage).
  // Decomp: mpColl_80047E14 checks left ledge when facing_dir==1 (or 0).
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
  if (ledge_left != NULL && facing_dir > 0.0f) {
    const float edge_x = ledge_left->x0;
    const float edge_y = ledge_left->y0;

    // Decomp AABB build (mpColl_80044164):
    // left = min(prev_x, cur_x)
    // right = ledge_snap_x + (max(prev_x, cur_x) + ecb.right.x)
    // bottom/top computed from (pos.y + ledge_snap_y) ± half_height.
    const float aabb_l = min_x;
    const float aabb_r = ch->ledge_snap_x + (max_x + ecb.right_rel_x);
    const float aabb_b = (min_y + ch->ledge_snap_y) - half_h;
    const float aabb_t = (max_y + ch->ledge_snap_y) + half_h;

    if (edge_x >= aabb_l && edge_x <= aabb_r && edge_y >= aabb_b && edge_y <= aabb_t) {
      // Decomp: contact.x is required to be close to the floor endpoint:
      // `cd->contact.x - edge.x < 5.0F`.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
      const float contact_x = clampf(cur_x, ledge_left->x0, ledge_left->x1);
      if ((contact_x - edge_x) < k_ledge_edge_dx_max && cur_x < edge_x && ecb.bottom_y < edge_y) {
        out |= MSL_COLLIDE_LEFT_LEDGE_GRAB;
      }
    }
  }

  // Right ledge grab (must be facing toward -X / into stage).
  // Decomp: mpColl_80047E14 checks right ledge when facing_dir==-1 (or 0).
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
  if (ledge_right != NULL && facing_dir < 0.0f) {
    const float edge_x = ledge_right->x1;
    const float edge_y = ledge_right->y1;

    // Decomp AABB build (mpColl_800443C4), with snap_x negated:
    // right = max(prev_x, cur_x)
    // left = (-ledge_snap_x) + (min(prev_x, cur_x) + ecb.left.x)
    const float aabb_r = max_x;
    const float aabb_l = (-ch->ledge_snap_x) + (min_x + ecb.left_rel_x);
    const float aabb_b = (min_y + ch->ledge_snap_y) - half_h;
    const float aabb_t = (max_y + ch->ledge_snap_y) + half_h;

    if (edge_x >= aabb_l && edge_x <= aabb_r && edge_y >= aabb_b && edge_y <= aabb_t) {
      const float contact_x = clampf(cur_x, ledge_right->x0, ledge_right->x1);
      if ((edge_x - contact_x) < k_ledge_edge_dx_max && cur_x > edge_x && ecb.bottom_y < edge_y) {
        out |= MSL_COLLIDE_RIGHT_LEDGE_GRAB;
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

      const float cur_x = batch->state.pos_x[idx];
      const float cur_y = batch->state.pos_y[idx];
      const float prev_x = batch->state.prev_pos_x[idx];
      const float prev_y = batch->state.prev_pos_y[idx];
      const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;

      const uint32_t flags = ledge_grab_flags_for_fighter(
          stage_id, cur_x, cur_y, prev_x, prev_y, fd, batch->state.char_id[idx],
          batch->state.animation_index[idx], batch->state.anim_frame_f32[idx]);
      batch->state.coll_env_flags[idx] |= flags;
    }
  }
}
