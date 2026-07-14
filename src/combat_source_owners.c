#include "combat_internal.h"
#include "fighter_pose.h"

// Small source-owner contracts shared by damage, guard, debug instrumentation, and contact.
// Geometry selection itself lives in fighter_contact.c; this file deliberately contains no
// action/replay exception taxonomy.

uint8_t combat_is_damage_or_firefox_launch_victim_action(uint8_t char_id, uint16_t action_id) {
  return msl_damage_owner_is_damage_or_firefox_launch_action(char_id, action_id);
}

uint8_t combat_is_damage_air_action(uint16_t action_id) {
  return msl_damage_owner_is_damage_air_action(action_id);
}

uint8_t combat_is_downed_damage_contact_action(uint16_t action_id) {
  return msl_damage_owner_is_downed_damage_contact_action(action_id);
}

uint16_t combat_down_damage_action_from_source(uint16_t action_id) {
  return msl_damage_owner_down_damage_action_from_source(action_id);
}

uint32_t combat_down_damage_submotion_from_action(uint16_t action_id) {
  return msl_damage_owner_down_damage_submotion_from_action(action_id);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_strong_meteor(uint8_t hb_id, float damage,
                                                                    uint16_t angle, uint16_t kbg,
                                                                    uint16_t wsk, uint16_t bkb) {
  // data/moves/falco.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && damage == 12.0f && angle == 290u && kbg == 100u && wsk == 0u &&
                   bkb == 20u);
}

uint8_t combat_shine_start_grounded_ledge_ecb_lock_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, uint16_t attacker_action) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], attacker_action);
  if (kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
      kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  const int bi = (int)(d_idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage = batch->state.stage_id[bi];
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph(stage);
  const int line = stage_collision_floor_line_index(stage, batch->state.ground_id[d_idx]);
  return graph != NULL && line >= 0 && (size_t)line < graph->line_count &&
                 graph->lines[(size_t)line].is_ledge
             ? 1u
             : 0u;
}

typedef struct MslLbCollVec3 {
  float x;
  float y;
  float z;
} MslLbCollVec3;

static inline float lbcoll_dot(MslLbCollVec3 a, MslLbCollVec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline MslLbCollVec3 lbcoll_sub(MslLbCollVec3 a, MslLbCollVec3 b) {
  return (MslLbCollVec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline MslLbCollVec3 lbcoll_lerp(MslLbCollVec3 a, MslLbCollVec3 delta, float t) {
  return (MslLbCollVec3){a.x + delta.x * t, a.y + delta.y * t, a.z + delta.z * t};
}

static float lbcoll_point_segment_dist2(MslLbCollVec3 start, MslLbCollVec3 end, MslLbCollVec3 point,
                                        float* out_param) {
  const MslLbCollVec3 delta = lbcoll_sub(end, start);
  const MslLbCollVec3 start_to_point = lbcoll_sub(start, point);
  float param = -lbcoll_dot(delta, start_to_point) / lbcoll_dot(delta, delta);
  if (param > 1.0f) {
    param = 1.0f;
  } else if (param < 0.0f) {
    param = 0.0f;
  }
  const MslLbCollVec3 closest = lbcoll_lerp(start, delta, param);
  const MslLbCollVec3 distance = lbcoll_sub(closest, point);
  *out_param = param;
  return lbcoll_dot(distance, distance);
}

static void lbcoll_80006e58_closest(MslLbCollVec3 hit_start, MslLbCollVec3 hit_end,
                                    MslLbCollVec3 hurt_start, MslLbCollVec3 hurt_end,
                                    MslLbCollVec3* out_hit, MslLbCollVec3* out_hurt) {
  const float epsilon = 1.0e-5f;
  const MslLbCollVec3 hit_delta = lbcoll_sub(hit_end, hit_start);
  const MslLbCollVec3 hurt_delta = lbcoll_sub(hurt_end, hurt_start);
  const MslLbCollVec3 start_delta = lbcoll_sub(hit_start, hurt_start);
  const float hit_len_sq = lbcoll_dot(hit_delta, hit_delta);
  const float hurt_len_sq = lbcoll_dot(hurt_delta, hurt_delta);
  const float segment_dot = lbcoll_dot(hit_delta, hurt_delta);
  const float hit_start_dot = lbcoll_dot(hit_delta, start_delta);
  const float hurt_start_dot = lbcoll_dot(hurt_delta, start_delta);
  const float denom = hit_len_sq * hurt_len_sq - segment_dot * segment_dot;
  float hit_param = 0.0f;
  float hurt_param = 0.0f;

  if (fabsf(hurt_len_sq) < epsilon) {
    if (fabsf(hit_len_sq) >= epsilon) {
      hit_param = -hit_start_dot / hit_len_sq;
      if (hit_param > 1.0f) {
        hit_param = 1.0f;
      } else if (hit_param < 0.0f) {
        hit_param = 0.0f;
      }
    }
  } else if (fabsf(denom) < epsilon) {
    // The source projects the hit endpoint nearest the hurt-segment midpoint.
    const MslLbCollVec3 hurt_mid = lbcoll_lerp(hurt_start, hurt_delta, 0.5f);
    const MslLbCollVec3 start_mid = lbcoll_sub(hit_start, hurt_mid);
    const MslLbCollVec3 end_mid = lbcoll_sub(hit_end, hurt_mid);
    const uint8_t use_start = lbcoll_dot(start_mid, start_mid) < lbcoll_dot(end_mid, end_mid);
    hit_param = use_start != 0u ? 0.0f : 1.0f;
    const MslLbCollVec3 endpoint = use_start != 0u ? hit_start : hit_end;
    (void)lbcoll_point_segment_dist2(hurt_start, hurt_end, endpoint, &hurt_param);
  } else {
    hit_param = (segment_dot * hurt_start_dot - hurt_len_sq * hit_start_dot) / denom;
    hurt_param = (hit_len_sq * hurt_start_dot - segment_dot * hit_start_dot) / denom;
    if (hit_param > 1.0f || hit_param < 0.0f || hurt_param > 1.0f || hurt_param < 0.0f) {
      float candidate_hurt = 0.0f;
      float candidate_hit = 0.0f;
      const float fixed_hit = hit_param < 0.0f ? 0.0f : 1.0f;
      const float fixed_hurt = hurt_param < 0.0f ? 0.0f : 1.0f;
      const float hit_endpoint_dist = lbcoll_point_segment_dist2(
          hurt_start, hurt_end, fixed_hit == 0.0f ? hit_start : hit_end, &candidate_hurt);
      const float hurt_endpoint_dist = lbcoll_point_segment_dist2(
          hit_start, hit_end, fixed_hurt == 0.0f ? hurt_start : hurt_end, &candidate_hit);
      if (hit_endpoint_dist < hurt_endpoint_dist) {
        hit_param = fixed_hit;
        hurt_param = candidate_hurt;
      } else {
        hit_param = candidate_hit;
        hurt_param = fixed_hurt;
      }
    }
  }
  *out_hit = lbcoll_lerp(hit_start, hit_delta, hit_param);
  *out_hurt = lbcoll_lerp(hurt_start, hurt_delta, hurt_param);
}

static uint8_t lbcoll_expanded_hit_aabb_reject(MslLbCollVec3 hit_start, MslLbCollVec3 hit_end,
                                               MslLbCollVec3 hurt_start, MslLbCollVec3 hurt_end,
                                               float radius) {
  const float hit_min[3] = {fminf(hit_start.x, hit_end.x) - radius,
                            fminf(hit_start.y, hit_end.y) - radius,
                            fminf(hit_start.z, hit_end.z) - radius};
  const float hit_max[3] = {fmaxf(hit_start.x, hit_end.x) + radius,
                            fmaxf(hit_start.y, hit_end.y) + radius,
                            fmaxf(hit_start.z, hit_end.z) + radius};
  const float hurt_a[3] = {hurt_start.x, hurt_start.y, hurt_start.z};
  const float hurt_b[3] = {hurt_end.x, hurt_end.y, hurt_end.z};
  for (uint8_t axis = 0u; axis < 3u; axis++) {
    if ((hurt_a[axis] < hit_min[axis] && hurt_b[axis] < hit_min[axis]) ||
        (hurt_a[axis] > hit_max[axis] && hurt_b[axis] > hit_max[axis])) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t combat_hurtcap_segment_overlap_lbcoll(const MslBatch* batch, int bi, int defender,
                                              int cap_id, float hit_start_x, float hit_start_y,
                                              float hit_start_z, float hit_end_x, float hit_end_y,
                                              float hit_end_z, float hit_radius,
                                              uint8_t flatten_hurt_z, float* out_overlap_amount,
                                              uint8_t* out_evaluated) {
  if (out_overlap_amount != NULL) {
    *out_overlap_amount = 0.0f;
  }
  if (out_evaluated != NULL) {
    *out_evaluated = 0u;
  }
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || defender < 0 ||
      defender >= (int)batch->config.num_players || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS ||
      !(hit_radius > 0.0f)) {
    return 0u;
  }
  const size_t ci = idx_hurtcap(bi, defender, cap_id);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (cap_id >= (int)batch->state.hurtcap_count[d_idx] || batch->state.hurtcap_enabled[ci] == 0u) {
    return 0u;
  }
  const MslLbCollVec3 hit_start = {hit_start_x, hit_start_y, hit_start_z};
  const MslLbCollVec3 hit_end = {hit_end_x, hit_end_y, hit_end_z};
  const float hurt_z = batch->state.pos_z[d_idx];
  const MslLbCollVec3 hurt_start = {batch->state.hurtcap_a_x[ci], batch->state.hurtcap_a_y[ci],
                                    flatten_hurt_z != 0u ? hurt_z : batch->state.hurtcap_a_z[ci]};
  const MslLbCollVec3 hurt_end = {batch->state.hurtcap_b_x[ci], batch->state.hurtcap_b_y[ci],
                                  flatten_hurt_z != 0u ? hurt_z : batch->state.hurtcap_b_z[ci]};
  const float world_hurt_radius = batch->state.hurtcap_radius[ci];
  if (lbcoll_expanded_hit_aabb_reject(hit_start, hit_end, hurt_start, hurt_end,
                                      hit_radius + 3.0f * world_hurt_radius) != 0u) {
    return 0u;
  }
  MslLbCollVec3 hit_closest;
  MslLbCollVec3 hurt_closest;
  lbcoll_80006e58_closest(hit_start, hit_end, hurt_start, hurt_end, &hit_closest, &hurt_closest);
  const MslLbCollVec3 closest_delta = lbcoll_sub(hit_closest, hurt_closest);
  const float distance_sq = lbcoll_dot(closest_delta, closest_delta);
  const float world_distance = sqrtf(distance_sq);
  float hurt_radius = world_hurt_radius;
  if (batch->hurtcap_matrix_valid[ci] != 0u) {
    const float scale = fighter_pose_model_scale(batch, d_idx);
    if (scale > 0.0f) {
      // hurtboxes_refresh publishes the source raw radius multiplied by the fighter scale. Recover
      // it here and measure the closest-point delta through the exact live world JObj inverse.
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80006E58,lbColl_8000805C}
      const float local_radius = hurt_radius / scale;
      const float* matrix = &batch->hurtcap_matrix[ci * 12u];
      float hit_local[3];
      float hurt_local[3];
      if (msl_mtx34_inverse_point(matrix, hit_closest.x, hit_closest.y, hit_closest.z,
                                  &hit_local[0], &hit_local[1], &hit_local[2]) != 0 &&
          msl_mtx34_inverse_point(matrix, hurt_closest.x, hurt_closest.y, hurt_closest.z,
                                  &hurt_local[0], &hurt_local[1], &hurt_local[2]) != 0) {
        const float dx = hit_local[0] - hurt_local[0];
        const float dy = hit_local[1] - hurt_local[1];
        const float dz = hit_local[2] - hurt_local[2];
        const float local_distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (local_distance > 0.0f && world_distance > 0.0f) {
          hurt_radius = local_radius * (world_distance / local_distance);
        } else {
          hurt_radius = local_radius;
        }
      }
    }
  }
  const float radius = hit_radius + hurt_radius;
  if (out_overlap_amount != NULL) {
    *out_overlap_amount = radius - world_distance;
  }
  if (out_evaluated != NULL) {
    *out_evaluated = 1u;
  }
  return distance_sq <= radius * radius ? 1u : 0u;
}

uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated) {
  (void)hx;
  (void)hy;
  (void)hz;
  (void)ax;
  (void)ay;
  (void)az;
  (void)bx;
  (void)by;
  (void)bz;
  (void)use_catch_grabbable_pose;
  if (batch == NULL || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES || attacker < 0 ||
      attacker >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t hi = idx_hitbox(bi, attacker, hb_id);
  return combat_hurtcap_segment_overlap_lbcoll(
      batch, bi, defender, cap_id, batch->state.hitbox_prev_x[hi], batch->state.hitbox_prev_y[hi],
      batch->state.hitbox_prev_z[hi], batch->state.hitbox_x[hi], batch->state.hitbox_y[hi],
      batch->state.hitbox_z[hi], hr, 0u, out_overlap_amount, out_evaluated);
}

int combat_debug_body_matrix_overlap(const MslBatch* batch, int bi, int attacker, int hb_id,
                                     int defender, int cap_id, float* out_overlap) {
  if (out_overlap == NULL || batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      defender < 0 || attacker >= (int)batch->config.num_players ||
      defender >= (int)batch->config.num_players || attacker == defender || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }
  *out_overlap = 0.0f;
  const size_t hi = idx_hitbox(bi, attacker, hb_id);
  const size_t ci = idx_hurtcap(bi, defender, cap_id);
  if (batch->state.hitbox_enabled[hi] == 0u || batch->state.hurtcap_enabled[ci] == 0u) {
    return 0;
  }
  (void)combat_body_overlap_lbColl_80006E58_matrix_radius(
      batch, bi, attacker, hb_id, defender, cap_id, batch->state.hitbox_x[hi],
      batch->state.hitbox_y[hi], batch->state.hitbox_z[hi], batch->state.hitbox_radius[hi],
      batch->state.hurtcap_a_x[ci], batch->state.hurtcap_a_y[ci], batch->state.hurtcap_a_z[ci],
      batch->state.hurtcap_b_x[ci], batch->state.hurtcap_b_y[ci], batch->state.hurtcap_b_z[ci], 0u,
      out_overlap, NULL);
  return 0;
}
