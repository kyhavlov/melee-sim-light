#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Fighter HitCapsule -> ShieldDesc narrowphase.
//
// ftColl_80078C70 supplies one live x914 capsule and the defender's live ShieldDesc. Source
// lbColl_80007BCC refreshes the ShieldDesc center, then delegates to lbColl_80006E58 with the
// HitCapsule x58->x4C sweep and the zero-length ShieldDesc segment. shields_refresh() has already
// published that descriptor's world center and its bone-scale-adjusted radius, so the remaining
// collision is exactly a swept-sphere/point distance test. No action, replay, or seed identity is
// part of this geometric owner.
// refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
static inline uint8_t msl_shielddesc_fighter_overlap_ftcoll_80007bcc(const MslBatch* batch, int bi,
                                                                     int attacker, int defender,
                                                                     int hb_id,
                                                                     float* out_overlap_margin) {
  if (out_overlap_margin != NULL) {
    *out_overlap_margin = 0.0f;
  }
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 || defender < 0 ||
      attacker >= (int)batch->config.num_players || defender >= (int)batch->config.num_players ||
      attacker == defender || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }

  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->state.hitbox_enabled[hi] == 0u || !(batch->state.hitbox_radius[hi] > 0.0f) ||
      !(batch->state.shield_radius[d_idx] > 0.0f)) {
    return 0u;
  }

  const float ax = batch->state.hitbox_prev_x[hi];
  const float ay = batch->state.hitbox_prev_y[hi];
  const float az = batch->state.hitbox_prev_z[hi];
  const float bx = batch->state.hitbox_x[hi];
  const float by = batch->state.hitbox_y[hi];
  const float bz = batch->state.hitbox_z[hi];
  const float px = batch->state.shield_x[d_idx];
  const float py = batch->state.shield_y[d_idx];
  const float pz = batch->state.shield_z[d_idx];

  const float dx = bx - ax;
  const float dy = by - ay;
  const float dz = bz - az;
  const float length_sq = dx * dx + dy * dy + dz * dz;
  float t = 0.0f;
  if (length_sq > 0.0f) {
    t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / length_sq;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }

  const float qx = ax + t * dx;
  const float qy = ay + t * dy;
  const float qz = az + t * dz;
  const float ex = px - qx;
  const float ey = py - qy;
  const float ez = pz - qz;
  const float distance_sq = ex * ex + ey * ey + ez * ez;
  const float radius = batch->state.hitbox_radius[hi] + batch->state.shield_radius[d_idx];
  if (out_overlap_margin != NULL) {
    *out_overlap_margin = radius - sqrtf(distance_sq);
  }
  return distance_sq <= radius * radius ? 1u : 0u;
}
