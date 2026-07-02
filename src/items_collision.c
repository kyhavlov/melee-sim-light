#include "items_internal.h"

float item_segment_segment_dist2(float p0x, float p0y, float p0z, float p1x, float p1y, float p1z,
                                 float q0x, float q0y, float q0z, float q1x, float q1y, float q1z) {
  // Closest distance between two 3D segments (projectile sweep segment vs hurt capsule segment).
  // Decomp shape for fox/falco lasers:
  // - itFoxlaser_UnkMotion1_Phys snapshots prev_pos before velocity integration.
  // - it_8029C4D4 resolves collision across (prev_pos -> cur_pos), not a point probe at cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;

  const float a = ux * ux + uy * uy + uz * uz;
  const float b = ux * vx + uy * vy + uz * vz;
  const float c = vx * vx + vy * vy + vz * vz;
  const float d = ux * wx + uy * wy + uz * wz;
  const float e = vx * wx + vy * wy + vz * wz;
  const float D = a * c - b * b;
  const float eps = 1e-8f;

  if (a < eps || c < eps) {
    // Stage-item hurtboxes such as Yoshi's Shy Guy can be point capsules (A==B). Use the shared
    // combat solver only for degenerate item/hurt segments so point-vs-segment distance is modeled
    // without perturbing ordinary non-degenerate laser/item contact ordering.
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    float d2 = 0.0f;
    combat_segment_segment_dist2(p0x, p0y, p0z, p1x, p1y, p1z, q0x, q0y, q0z, q1x, q1y, q1z, &d2,
                                 NULL, NULL);
    return d2;
  }

  float sN = 0.0f;
  float sD = D;
  float tN = 0.0f;
  float tD = D;

  if (D < eps) {
    sN = 0.0f;
    sD = 1.0f;
    tN = e;
    tD = c;
  } else {
    sN = b * e - c * d;
    tN = a * e - b * d;
    if (sN < 0.0f) {
      sN = 0.0f;
      tN = e;
      tD = c;
    } else if (sN > sD) {
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0f) {
    tN = 0.0f;
    if (-d < 0.0f) {
      sN = 0.0f;
    } else if (-d > a) {
      sN = sD;
    } else {
      sN = -d;
      sD = a;
    }
  } else if (tN > tD) {
    tN = tD;
    if ((-d + b) < 0.0f) {
      sN = 0.0f;
    } else if ((-d + b) > a) {
      sN = sD;
    } else {
      sN = -d + b;
      sD = a;
    }
  }

  const float sc = (fabsf(sN) < eps) ? 0.0f : (sN / sD);
  const float tc = (fabsf(tN) < eps) ? 0.0f : (tN / tD);

  const float dx = wx + sc * ux - tc * vx;
  const float dy = wy + sc * uy - tc * vy;
  const float dz = wz + sc * uz - tc * vz;
  return dx * dx + dy * dy + dz * dz;
}

uint8_t item_swept_sphere_capsule_intersects(const MslBatch* batch, int bi, int defender, float sx0,
                                             float sy0, float sx1, float sy1, float sr, int cap_i,
                                             uint8_t* out_hurt_height) {
  // Swept sphere segment (sx0,sy0,0)->(sx1,sy1,0) vs hurt capsule segment AB.
  // Decomp item-vs-fighter collision sweep uses prev_pos -> cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0;
  }
  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)defender) * (size_t)MSL_MAX_HURTCAPS +
      (size_t)cap_i;
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0;
  }
  float ax = batch->state.hurtcap_a_x[hi];
  float ay = batch->state.hurtcap_a_y[hi];
  const float az = batch->state.hurtcap_a_z[hi];
  const float bx = batch->state.hurtcap_b_x[hi];
  const float by = batch->state.hurtcap_b_y[hi];
  const float bz = batch->state.hurtcap_b_z[hi];
  const float cr = batch->state.hurtcap_radius[hi];
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > (rr * rr)) {
    return 0;
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1;
}

uint8_t item_laser_hitcapsule_overlaps_fighter_hitcapsule(const MslBatch* batch, size_t hb_i,
                                                          float sx0, float sy0, float sx1,
                                                          float sy1, float item_radius) {
  if (batch == NULL || !(item_radius > 0.0f)) {
    return 0u;
  }
  float hx0 = batch->state.hitbox_x[hb_i];
  float hy0 = batch->state.hitbox_y[hb_i];
  float hz0 = batch->state.hitbox_z[hb_i];
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    hx0 = batch->state.hitbox_prev_x[hb_i];
    hy0 = batch->state.hitbox_prev_y[hb_i];
    hz0 = batch->state.hitbox_prev_z[hb_i];
  }
  const float hx1 = batch->state.hitbox_x[hb_i];
  const float hy1 = batch->state.hitbox_y[hb_i];
  const float hz1 = batch->state.hitbox_z[hb_i];
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, hx0, hy0, hz0, hx1, hy1, hz1);
  const float rr = item_radius + batch->state.hitbox_radius[hb_i];
  return (uint8_t)(d2 <= rr * rr);
}

uint8_t item_laser_fighter_hitcapsule_contact_mask_precedes_shield_body(
    const MslBatch* batch, int bi, int fighter, const MslLaserParams* lp, uint8_t laser_state,
    float x0, float y0, float x, float y, float ux, float uy, float sr, float laser_prev_scale_z,
    float laser_scale_z, float item_damage) {
  if (batch == NULL || lp == NULL || fighter < 0 || fighter >= (int)batch->config.num_players) {
    return 0u;
  }
  const uint8_t zero_kb_damage_class_state =
      (laser_state == 0u) ? lp->zero_kb_damage_class : lp->state1_zero_kb_damage_class;
  if (zero_kb_damage_class_state == 0u) {
    return 0u;
  }
  // Source ordering: ftColl_8007925C builds eligible fighter HitCapsules first, then before
  // SHIELD/BODY admission it tests item HitCapsule vs fighter HitCapsule in `catch_path` and
  // continues the item loop when the clank/body-collision owner resolves. Keep this registration
  // on generated zero-KB laser states: Fox blaster shots have all-zero source KB terms and can
  // have fighter HitCapsule contact without entering the regular BODY damage-state path, while
  // Falco's nonzero-KB laser BODY rows must still apply their ordinary hit.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007AFC
  // data/items/lasers.bin::MSLLASR1 zero_kb_damage_class/state1_zero_kb_damage_class
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  uint8_t contact_mask = 0u;
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hb_i = idx_hitbox(bi, fighter, hb);
    if (!batch->state.hitbox_enabled[hb_i]) {
      continue;
    }
    const uint16_t flags = batch->state.hitbox_flags[hb_i];
    if ((flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
        (flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u) {
      continue;
    }
    if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
        batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT) {
      continue;
    }
    if (!(batch->state.hitbox_damage[hb_i] > 0.0f) ||
        batch->state.hitbox_damage[hb_i] > item_damage) {
      continue;
    }
    if (off_n == 0u) {
      if (item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, hb_i, x0, y0, x, y, sr)) {
        contact_mask |= 0x01u;
      }
      continue;
    }
    for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
      const float off_x =
          (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
      const float sx0 = x0 + (ux * off_x * laser_prev_scale_z);
      const float sy0 = y0 + (uy * off_x * laser_prev_scale_z);
      const float sx1 = x + (ux * off_x * laser_scale_z);
      const float sy1 = y + (uy * off_x * laser_scale_z);
      if (item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, hb_i, sx0, sy0, sx1, sy1, sr)) {
        if (oi < (uint8_t)MSL_MAX_HITBOXES) {
          contact_mask |= (uint8_t)(1u << oi);
        }
      }
    }
  }
  return contact_mask;
}

uint8_t item_swept_sphere_capsule_overlap_amount(const MslBatch* batch, int bi, int defender,
                                                 float sx0, float sy0, float sx1, float sy1,
                                                 float sr, int cap_i, uint8_t* out_hurt_height,
                                                 float* out_overlap_amount, uint8_t flatten_hurt_z,
                                                 float hurt_radius_mul) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0;
  }
  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)defender) * (size_t)MSL_MAX_HURTCAPS +
      (size_t)cap_i;
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0;
  }
  float ax = batch->state.hurtcap_a_x[hi];
  float ay = batch->state.hurtcap_a_y[hi];
  // The default path consumes seed-visible hurt capsule endpoints. A narrow laser BODY lane below
  // can request lbColl's flattened hurtcap Z when its source-owned filters are present.
  const float hurt_z = batch->state.pos_z[d_idx];
  const float az = flatten_hurt_z ? hurt_z : batch->state.hurtcap_a_z[hi];
  const float bx = batch->state.hurtcap_b_x[hi];
  const float by = batch->state.hurtcap_b_y[hi];
  const float bz = flatten_hurt_z ? hurt_z : batch->state.hurtcap_b_z[hi];
  float cr = batch->state.hurtcap_radius[hi];
  if (hurt_radius_mul > 0.0f) {
    cr *= hurt_radius_mul;
  }
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > (rr * rr)) {
    return 0;
  }
  if (out_overlap_amount) {
    *out_overlap_amount = rr - sqrtf(d2);
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1;
}

uint8_t item_active_hitbox_overlaps_fighter_hurtcaps(MslBatch* batch, int bi, int attacker,
                                                     int hb_id, int victim) {
  if (batch == NULL || attacker < 0 || victim < 0 || attacker >= (int)batch->config.num_players ||
      victim >= (int)batch->config.num_players || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t v_idx = msl_idx_player(bi, victim);
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (batch->state.hitbox_enabled[hb_i] == 0u || !(batch->state.hitbox_damage[hb_i] > 0.0f) ||
      batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
      batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT) {
    return 0u;
  }
  const uint16_t flags = batch->state.hitbox_flags[hb_i];
  if ((flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u) {
    return 0u;
  }
  if (batch->state.on_ground[v_idx] != 0u) {
    if ((flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
      return 0u;
    }
  } else if ((flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
    return 0u;
  }
  if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, victim,
                              batch->state.instance_id[v_idx])) {
    return 0u;
  }

  const float hx1 = batch->state.hitbox_x[hb_i];
  const float hy1 = batch->state.hitbox_y[hb_i];
  const float hz1 = batch->state.hitbox_z[hb_i];
  const float hx0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
  const float hy0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
  const float hz0 = batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
  const float hr = batch->state.hitbox_radius[hb_i];
  const uint8_t cap_n = batch->state.hurtcap_count[v_idx];
  for (uint8_t ci = 0; ci < cap_n; ci++) {
    const size_t hi = idx_hurtcap(bi, victim, (int)ci);
    if (batch->state.hurtcap_enabled[hi] == 0u) {
      continue;
    }
    const float rr = hr + batch->state.hurtcap_radius[hi];
    const float d2 = item_segment_segment_dist2(
        hx0, hy0, hz0, hx1, hy1, hz1, batch->state.hurtcap_a_x[hi], batch->state.hurtcap_a_y[hi],
        batch->state.hurtcap_a_z[hi], batch->state.hurtcap_b_x[hi], batch->state.hurtcap_b_y[hi],
        batch->state.hurtcap_b_z[hi]);
    if (d2 <= rr * rr) {
      return 1u;
    }
  }
  return 0u;
}

static inline void item_lbcoll_80006e58_closest_points(float p0x, float p0y, float p0z, float p1x,
                                                       float p1y, float p1z, float q0x, float q0y,
                                                       float q0z, float q1x, float q1y, float q1z,
                                                       float* out_px, float* out_py, float* out_pz,
                                                       float* out_qx, float* out_qy, float* out_qz,
                                                       float* out_world_dist) {
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;
  const float a = msl_dot3(ux, uy, uz, ux, uy, uz);
  const float b = msl_dot3(ux, uy, uz, vx, vy, vz);
  const float c = msl_dot3(vx, vy, vz, vx, vy, vz);
  const float d = msl_dot3(ux, uy, uz, wx, wy, wz);
  const float e = msl_dot3(vx, vy, vz, wx, wy, wz);
  const float denom = a * c - b * b;
  const float eps_hi = 1.0e-5f;
  const float eps_lo = -1.0e-5f;
  float s = 0.0f;
  float t = 0.0f;

  // This intentionally follows lbColl_80006E58's endpoint fallback order instead of the generic
  // closest-segment helper: the source routine can select a different endpoint pair, and that pair
  // feeds the local-matrix radius scalar that writes HitCapsule.coll_distance.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
  if (c < eps_hi && c > eps_lo) {
    if (!(a < eps_hi && a > eps_lo)) {
      s = -d / a;
      if (s > 1.0f) {
        s = 1.0f;
      } else if (s < 0.0f) {
        s = 0.0f;
      }
    }
  } else if (denom < eps_hi && denom > eps_lo) {
    const float mid_x = q0x + 0.5f * vx;
    const float mid_y = q0y + 0.5f * vy;
    const float mid_z = q0z + 0.5f * vz;
    const float d0 = msl_len2_3(p0x - mid_x, p0y - mid_y, p0z - mid_z);
    const float d1 = msl_len2_3(p1x - mid_x, p1y - mid_y, p1z - mid_z);
    s = (d0 < d1) ? 0.0f : 1.0f;
    const float px = (s == 0.0f) ? p0x : p1x;
    const float py = (s == 0.0f) ? p0y : p1y;
    const float pz = (s == 0.0f) ? p0z : p1z;
    const float q_to_p_x = q0x - px;
    const float q_to_p_y = q0y - py;
    const float q_to_p_z = q0z - pz;
    t = -msl_dot3(vx, vy, vz, q_to_p_x, q_to_p_y, q_to_p_z) / c;
    if (t > 1.0f) {
      t = 1.0f;
    } else if (t < 0.0f) {
      t = 0.0f;
    }
  } else {
    s = ((b * e) - (c * d)) / denom;
    t = ((a * e) - (b * d)) / denom;
    if (s > 1.0f || s < 0.0f || t > 1.0f || t < 0.0f) {
      float s_candidate = 0.0f;
      float t_candidate = 0.0f;
      float first_d2 = 0.0f;
      float second_d2 = 0.0f;
      if (s < 0.0f) {
        s_candidate = 0.0f;
        combat_point_segment_dist2(p0x, p0y, p0z, q0x, q0y, q0z, q1x, q1y, q1z, &first_d2,
                                   &t_candidate);
      } else {
        s_candidate = 1.0f;
        combat_point_segment_dist2(p1x, p1y, p1z, q0x, q0y, q0z, q1x, q1y, q1z, &first_d2,
                                   &t_candidate);
      }
      float s_candidate_2 = 0.0f;
      float t_candidate_2 = 0.0f;
      if (t < 0.0f) {
        t_candidate_2 = 0.0f;
        combat_point_segment_dist2(q0x, q0y, q0z, p0x, p0y, p0z, p1x, p1y, p1z, &second_d2,
                                   &s_candidate_2);
      } else {
        t_candidate_2 = 1.0f;
        combat_point_segment_dist2(q1x, q1y, q1z, p0x, p0y, p0z, p1x, p1y, p1z, &second_d2,
                                   &s_candidate_2);
      }
      if (first_d2 < second_d2) {
        s = s_candidate;
        t = t_candidate;
      } else {
        s = s_candidate_2;
        t = t_candidate_2;
      }
    }
  }

  const float px = p0x + s * ux;
  const float py = p0y + s * uy;
  const float pz = p0z + s * uz;
  const float qx = q0x + t * vx;
  const float qy = q0y + t * vy;
  const float qz = q0z + t * vz;
  if (out_px) {
    *out_px = px;
  }
  if (out_py) {
    *out_py = py;
  }
  if (out_pz) {
    *out_pz = pz;
  }
  if (out_qx) {
    *out_qx = qx;
  }
  if (out_qy) {
    *out_qy = qy;
  }
  if (out_qz) {
    *out_qz = qz;
  }
  if (out_world_dist) {
    const float dx = px - qx;
    const float dy = py - qy;
    const float dz = pz - qz;
    const float d2 = msl_len2_3(dx, dy, dz);
    *out_world_dist = (d2 > 0.0f) ? sqrtf(d2) : 0.0f;
  }
}

uint8_t item_body_lbcoll_matrix_radius_overlap(const MslBatch* batch, int bi, int defender,
                                               float sx0, float sy0, float sx1, float sy1, float sr,
                                               int cap_i, uint8_t* out_hurt_height,
                                               float* out_overlap_amount, uint8_t* out_evaluated,
                                               uint8_t flatten_hurt_z) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  if (out_evaluated) {
    *out_evaluated = 0u;
  }
  if (batch == NULL || !(sr > 0.0f)) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0u;
  }
  const size_t hi = idx_hurtcap(bi, defender, cap_i);
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[d_idx];
  const MslHurtCap* caps = NULL;
  uint16_t cap_count_u16 = 0u;
  if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL ||
      (uint16_t)cap_i >= cap_count_u16) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_i];

  uint16_t msid = 0u;
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  uint8_t no_submotion_guard_source_pose = 0u;
  if (anim_u32 > 0xFFFFu) {
    switch (action_id) {
      case (uint16_t)MSL_ACT_GUARD_ON:
      case (uint16_t)MSL_ACT_GUARD_REFLECT:
        // ftCo_MS_GuardOn and ftCo_MS_GuardReflect both source their no-submotion collision pose
        // from ftCo_SM_GuardOn. Slippi can serialize the post-frame animation_index as -1 while
        // ftColl_8007925C still calls lbColl_8000805C against the live hurtcaps.
        // refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_GuardOn,ftCo_MS_GuardReflect}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
        msid = (uint16_t)MSL_SM_GUARD_ON;
        no_submotion_guard_source_pose = 1u;
        break;
      case (uint16_t)MSL_ACT_GUARD:
        msid = (uint16_t)MSL_SM_GUARD;
        no_submotion_guard_source_pose = 1u;
        break;
      case (uint16_t)MSL_ACT_GUARD_SET_OFF:
        msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
        no_submotion_guard_source_pose = 1u;
        break;
      default:
        return 0u;
    }
  } else {
    msid = (uint16_t)anim_u32;
  }

  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
  // Keep fractional matrix sampling limited to the retained LandingFallSpecial flattened-Z owner.
  // Other exact item BODY users stay on the existing source-visible frame sample until their wider
  // pose/hurtcap eligibility owners are proven.
  uint16_t pose_msid = msid;
  float pose_sample_frame = (flatten_hurt_z != 0u) ? anim_frame_f32 : (float)pose_frame;
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_CATCH_DASH &&
      batch->state.frame_start_action_id[d_idx] == (uint16_t)MSL_ACT_CATCH_DASH &&
      batch->state.on_ground[d_idx] != 0u && batch->state.hitlag[d_idx] == 0u &&
      batch->state.hitstun[d_idx] == 0u) {
    if (batch->state.action_frame[d_idx] <= 1 &&
        batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_DASH) {
      const uint16_t prev_msid =
          msl_motion_state_submotion_id(char_id, batch->state.seed_prev_action_id[d_idx]);
      if (prev_msid != 0xFFFFu) {
        pose_msid = prev_msid;
        pose_sample_frame = (batch->state.seed_prev_action_frame[d_idx] >= 0)
                                ? (float)batch->state.seed_prev_action_frame[d_idx]
                                : 0.0f;
      }
    } else if (batch->state.action_frame[d_idx] <= 2 &&
               batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_CATCH_DASH &&
               batch->state.seed_prev_action_frame[d_idx] <= 1) {
      pose_msid = (uint16_t)MSL_SM_CATCH_DASH;
      pose_sample_frame = 0.0f;
    }
    // CatchDash item-BODY entry pose owner:
    // ftCo_800D8A38 enters CatchDash from Dash_IASA through ftCo_800D8C54. Fighter Anim has
    // advanced the live action_frame by the time this item collision pass runs, so the source
    // boundary is identified from frame_start_action_id + seed_prev_action_*: the first visible
    // CatchDash collision tick consumes the Dash pose, and the immediately following tick consumes
    // CatchDash frame 0. Later frames use the ordinary current CatchDash pose. Keep this local to
    // exact item BODY matrix sampling; the action/capture state itself remains CatchDash.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   ftCo_800D8A38,ftCo_800D8C54,ftCo_CatchDash_Anim}
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  }

  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, pose_msid, pose_sample_frame,
                                         cap->bone_part_id, m) != 0) {
    return 0u;
  }
  if (no_submotion_guard_source_pose != 0u &&
      batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_tilt_x4[d_idx] > 0.0f) {
    uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[d_idx];
    const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
    if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
      guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
    }
    float target_m[12];
    if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD,
                                           (float)guard_tilt_frame, cap->bone_part_id,
                                           target_m) == 0) {
      float tilt_mag = batch->state.guard_tilt_x4[d_idx];
      if (tilt_mag > 1.0f) {
        tilt_mag = 1.0f;
      }
      float guardon_blend = 1.0f;
      const MslCommonParams* c = msl_common_params();
      if (c != NULL && c->guard_x10_init_frames > 0.0f) {
        const float elapsed = c->guard_x10_init_frames - (float)batch->state.guard_x10[d_idx];
        guardon_blend = elapsed / c->guard_x10_init_frames;
        if (guardon_blend < 0.0f) {
          guardon_blend = 0.0f;
        } else if (guardon_blend > 1.0f) {
          guardon_blend = 1.0f;
        }
      }
      // GuardOn live item BODY pose:
      // ftCo_GuardOn_Anim advances mv.co.guard.x0/x10 and ftCo_80091E78 blends the live JObj
      // chain toward the selected Guard tilt target before ftColl_8007925C reaches item BODY.
      // Slippi still publishes animation_index=-1 on these snapshots, so the exact item BODY
      // lbColl owner must use the same live matrix blend as fighter BODY, then pass that matrix to
      // lbColl_8000805C's local-radius test.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      for (int k = 0; k < 12; k++) {
        const float tilted = m[k] + tilt_mag * (target_m[k] - m[k]);
        m[k] += guardon_blend * (tilted - m[k]);
      }
    }
  }
  if (out_evaluated) {
    *out_evaluated = 1u;
  }

  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
          ? chp->model_scaling
          : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }

  // lbColl_8000805C refreshes hurt capsule endpoints from the live JObj before the local-radius
  // test. Keep the exact item BODY owner internally consistent by deriving those endpoints from the
  // same collision matrix used below for the inverse-space radius measurement; falling back to the
  // seed-visible endpoints here can over-admit a capsule one frame before the source pose reaches it.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  float la_x = 0.0f, la_y = 0.0f, la_z = 0.0f;
  float lb_x = 0.0f, lb_y = 0.0f, lb_z = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &la_x, &la_y, &la_z);
  msl_mtx34_mul_point(m, cap->b_offset, &lb_x, &lb_y, &lb_z);
  la_x *= model_scale;
  la_y *= model_scale;
  la_z *= model_scale;
  lb_x *= model_scale;
  lb_y *= model_scale;
  lb_z *= model_scale;
  const float facing_dir_world = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax = batch->state.pos_x[d_idx] + facing_dir_world * la_z;
  const float ay = batch->state.pos_y[d_idx] + la_y;
  const float az = flatten_hurt_z ? batch->state.pos_z[d_idx]
                                  : batch->state.pos_z[d_idx] - facing_dir_world * la_x;
  const float bx = batch->state.pos_x[d_idx] + facing_dir_world * lb_z;
  const float by = batch->state.pos_y[d_idx] + lb_y;
  const float bz = flatten_hurt_z ? batch->state.pos_z[d_idx]
                                  : batch->state.pos_z[d_idx] - facing_dir_world * lb_x;

  float world_dist = 0.0f;
  float hit_cp_x = 0.0f;
  float hit_cp_y = 0.0f;
  float hit_cp_z = 0.0f;
  float hurt_cp_x = 0.0f;
  float hurt_cp_y = 0.0f;
  float hurt_cp_z = 0.0f;
  item_lbcoll_80006e58_closest_points(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz,
                                      &hit_cp_x, &hit_cp_y, &hit_cp_z, &hurt_cp_x, &hurt_cp_y,
                                      &hurt_cp_z, &world_dist);

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float pos_x = batch->state.pos_x[d_idx];
  const float pos_y = batch->state.pos_y[d_idx];
  const float pos_z = batch->state.pos_z[d_idx];

  const float hit_rel_x = hit_cp_x - pos_x;
  const float hit_rel_y = hit_cp_y - pos_y;
  const float hit_rel_z = hit_cp_z - pos_z;
  const float hurt_rel_x = hurt_cp_x - pos_x;
  const float hurt_rel_y = hurt_cp_y - pos_y;
  const float hurt_rel_z = hurt_cp_z - pos_z;

  const float hit_pose_x = -facing_dir * hit_rel_z;
  const float hit_pose_y = hit_rel_y;
  const float hit_pose_z = facing_dir * hit_rel_x;
  const float hurt_pose_x = -facing_dir * hurt_rel_z;
  const float hurt_pose_y = hurt_rel_y;
  const float hurt_pose_z = facing_dir * hurt_rel_x;

  float hit_local_x = 0.0f, hit_local_y = 0.0f, hit_local_z = 0.0f;
  float hurt_local_x = 0.0f, hurt_local_y = 0.0f, hurt_local_z = 0.0f;
  if (!msl_mtx34_inverse_point(m, hit_pose_x / model_scale, hit_pose_y / model_scale,
                               hit_pose_z / model_scale, &hit_local_x, &hit_local_y,
                               &hit_local_z) ||
      !msl_mtx34_inverse_point(m, hurt_pose_x / model_scale, hurt_pose_y / model_scale,
                               hurt_pose_z / model_scale, &hurt_local_x, &hurt_local_y,
                               &hurt_local_z)) {
    return 0u;
  }

  const float local_dx = hit_local_x - hurt_local_x;
  const float local_dy = hit_local_y - hurt_local_y;
  const float local_dz = hit_local_z - hurt_local_z;
  const float local_dist = sqrtf(local_dx * local_dx + local_dy * local_dy + local_dz * local_dz);
  float hurt_radius_world_equiv = cap->scale;
  if (local_dist > 1.0e-8f && world_dist > 0.0f) {
    hurt_radius_world_equiv = cap->scale * (world_dist / local_dist);
  }
  const float overlap_amount = sr + hurt_radius_world_equiv - world_dist;
  if (out_overlap_amount) {
    *out_overlap_amount = overlap_amount;
  }
  if (overlap_amount <= 0.0f) {
    return 0u;
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1u;
}

uint8_t item_sphere_sphere_intersects_2d(float ax, float ay, float ar, float bx, float by,
                                         float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float rr = ar + br;
  return (dx * dx + dy * dy) <= (rr * rr);
}

uint8_t item_sphere_sphere_intersects_3d(float ax, float ay, float az, float ar, float bx, float by,
                                         float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

uint8_t item_prev_action_is_guard_reflect_locomotion_pose_source(uint16_t action_id) {
  // Keep this aligned with guard_lifecycle.h::msl_guard_reflect_entry_uses_guardon_pose_source:
  // the proven
  // current-pose ShieldDesc entry owner is Dash -> GuardReflect. Walk -> GuardReflect has a
  // replay-real ShieldBounced keepalive that stays on the normal shield-bubble source.
  // MSLMSO01 identifies the broad locomotion callback owners, but this same-step item contact
  // split depends on the callback-local guard-admission branch and timer state, so it remains a
  // semantic source predicate rather than a pure callback-class query.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return action_id == (uint16_t)MSL_ACT_DASH ? 1u : 0u;
}

float item_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

uint8_t item_prev_action_uses_fresh_guardreflect_shield_center(uint16_t action_id) {
  if (item_prev_action_is_guard_reflect_locomotion_pose_source(action_id)) {
    return 1u;
  }
  // Same-step Wait/Walk -> GuardReflect item shield-contact owner:
  // - Wait_IASA and Walk_IASA call ftCo_80091A4C directly; the digital powershield branch enters
  //   `ftCo_800939B4 -> ftCo_80093A50`.
  // - `ftCo_80093A50` creates ShieldDesc before ReflectDesc, so an already-live laser can resolve
  //   through Item_80269DC8 HitShield/GuardSetOff on that first no-submotion GuardReflect row.
  // - Keep this separate from the ShieldBounced normal/source predicate above; existing Dash/Walk
  //   controls show bounce ownership is narrower than the shield-center overlap owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
  return (action_id == (uint16_t)MSL_ACT_WAIT || action_id == (uint16_t)MSL_ACT_WALK_SLOW ||
          action_id == (uint16_t)MSL_ACT_WALK_MIDDLE || action_id == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1u
             : 0u;
}

uint8_t item_is_fresh_guardreflect_shield_center_source(const MslBatch* batch, size_t d_idx,
                                                        uint16_t prev_action_id) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.animation_index[d_idx] == 0xFFFFFFFFu &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
          batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
          (item_prev_action_uses_fresh_guardreflect_shield_center(prev_action_id) ||
           prev_action_id == (uint16_t)MSL_ACT_GUARD_OFF) &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_ON &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_REFLECT &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

uint8_t item_fresh_guardon_locomotion_shielddesc_owner(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL || batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.guard_on_entered_this_frame[d_idx] == 0u) {
    return 0u;
  }
  const uint16_t prev = batch->state.seed_prev_action_id[d_idx];
  // Fresh grounded-locomotion GuardOn entry ShieldDesc owner:
  // generated IASA callbacks can install a fresh ShieldDesc through ftCo_80091A4C/ftCo_80092450
  // before item shield collision consumes it. This class2 owner is deliberately separate from the
  // GuardOn frame-start x672 powershield bridge: Landing_IASA can publish item ShieldDesc while
  // follow-up GuardOn_IASA still consumes live x672.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  return msl_motion_state_common_class2_has_fast(prev,
                                                 MSL_MS_CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)
             ? 1u
             : 0u;
}

uint8_t item_fresh_guardon_landing_shielddesc_owner(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL || batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.guard_on_entered_this_frame[d_idx] == 0u ||
      (batch->state.guard_on_entry_reflect_source_latch[d_idx] == 0u &&
       batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL)) {
    return 0u;
  }
  // Landing_IASA -> GuardOn item ShieldDesc owner:
  // `enter_guard_on` sets guard_on_entry_reflect_source_latch for Landing source callbacks; replay
  // seeds can also expose LandingFallSpecial before that latch is available. Keep this
  // item-contact-local; the broader MSLMSO01 GuardOn x672 class is deliberately separate because
  // it feeds GuardOn powershield/recharge timing rather than item ShieldDesc admission.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450,ftCo_800921DC}
  return 1u;
}

float item_guard_shield_radius_from_state(const MslBatch* batch, const MslCommonParams* common,
                                          size_t d_idx) {
  if (batch == NULL || common == NULL || !(common->start_shield_health > 0.0f)) {
    return 0.0f;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (ch == NULL || !(ch->initial_shield_size > 0.0f)) {
    return 0.0f;
  }
  float light = batch->state.lightshield_amount[d_idx];
  if (!isfinite(light)) {
    light = 0.0f;
  }
  light = item_clamp01(light);
  if (batch->state.guard_reflect_entered_this_frame[d_idx] != 0u) {
    // GuardReflect entry frame, item-collision lane: ftCo_80093A50 -> ftCo_800921DC seeds
    // fp->lightshield_amount as
    // input.x650 / (1 - x10) with NO deadzone subtraction and NO upper clamp, so a full digital
    // press yields ~1.43 and the freshly scaled ShieldDesc the item pass consumes is smaller
    // than the steady-state bubble (inlineB0 light factor 1 - 0.5*light). The same-frame
    // fighter-vs-fighter collision pass keeps the steady `state.shield_radius` its own
    // replay-real witnesses lock (MAJ rec2217 DAir entry contact); the item pass boundary is
    // witnessed by MAJ rec293 (entry-frame laser miss at gap 7.98 vs steady allowed 9.17) and
    // rec5631/7327 (entry-frame laser hits at gaps 5.4/6.7), AGN rec428 (same-step spawn
    // reflect at 6.9). GuardOn entries keep the steady radius: PTE rec2923 (analog dash-shield
    // entry) takes a trailing-beam shield hit at ~10.2 that only the steady bubble admits.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,inlineB0,ftCo_80091D58}
    const float trig =
        msl_trigger_unit_from_input(batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                    batch->state.input_r[d_idx]);
    // Divisor: the raw analog activation floor (43/255), not the x10 shield-hold deadzone
    // (0.30). Witness bracket for the entry-frame item-lane allowed distance (shield + laser
    // radius), all replay-real: MAJ rec7327 entry hit at 7.35, AGN rec428 spawn-frame reflect
    // at 6.9, MAJ rec5631 entry hit at 6.0, MAJ rec293 entry MISS at 8.43. A full digital press
    // therefore lands light = 1/(1 - 43/255) = 1.203 (r ~6.85), between the steady-state 1.0
    // (r ~8.05, admits 293's miss) and the raw ftCo_800921DC 1/(1-0.30) = 1.43 (r ~5.52,
    // rejects 7327's and AGN's hits).
    const float denom = 1.0f - (43.0f / 255.0f);
    if (trig >= 0.0f) {
      light = trig / denom;
    }
  }
  const float hp_ratio = item_clamp01(batch->state.shield_hp[d_idx] / common->start_shield_health);
  float light_scale =
      (light * (common->shield_size_lightshield_max - common->shield_size_lightshield_min)) +
      common->shield_size_lightshield_min;
  if (light_scale < 0.0f) {
    light_scale = 0.0f;
  }
  const float n1 = hp_ratio * light_scale;
  const float n2 = 1.0f - common->shield_size_min_scale;
  const float scale = (n2 * n1) + common->shield_size_min_scale;
  // Match shields_refresh()'s current collision-radius policy: ShieldDesc transform supplies the
  // live joint/model pose, while the scalar radius uses fighter scale Y and the character's
  // initial shield size.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineB0,ftCo_80091D58,ftCo_800921DC}
  return scale * ch->initial_shield_size * batch->state.fighter_scale_y[d_idx];
}

uint8_t item_guardsetoff_current_shielddesc_allows_item_contact(const MslBatch* batch, size_t d_idx,
                                                                size_t item_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    return 1u;
  }
  const uint8_t flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  const uint8_t current_collision_cmd =
      (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECTING |
                MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR);
  if ((flags_2218 & current_collision_cmd) != 0u) {
    return 1u;
  }
  if (batch->state.item_shield_bounce_seed_valid[item_idx] != 0u) {
    return 1u;
  }
  const MslItemArticleParams* needle_params =
      item_article_params_for_sheik_needle_throw_item_type(batch->state.item_type[item_idx]);
  if (needle_params != NULL && batch->state.item_state[item_idx] == 0u &&
      needle_params->needle_lifetime_frames != 0u &&
      batch->state.item_timer[item_idx] >= (float)needle_params->needle_lifetime_frames) {
    // A freshly-created thrown Needle's command-11 HitCapsule reaches ftColl_8007925C on its first
    // active callback even when the defender is in GuardSetOff with no fresh x2218 command bits.
    // Later same-volley ShieldDesc packets need explicit ShieldBounced provenance or a live command
    // lane; otherwise stale x221B shield-active state would re-enter Item_80269DC8.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::shootNeedles
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFD8C,ItemStateTable}
    // refs/melee/src/melee/it/item.c::Item_80269DC8
    return 1u;
  }
  // GuardSetOff can keep fp+0x221B_b0 shield-active visible after the accepted shield-hit packet,
  // but source does not manufacture a new item ShieldDesc collision from that stale packet alone.
  // Require either a current x2218 command/behavior lane or explicit item shield-bounce provenance;
  // otherwise old same-volley projectiles stay on their hitlist/lifecycle owner instead of
  // re-entering Item_80269DC8 from reconstructed x221B state.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240,ftCo_800932DC}
  // refs/melee/src/melee/it/item.c::{Item_80269DC8,checkHitLag}
  return 0u;
}

float item_guard_reflect_entry_pose_radius(const MslBatch* batch, const MslCommonParams* common,
                                           size_t d_idx) {
  // ReflectDesc collision radius: p_ftCommonData->x2A8 (powershield_reflect_size) measured in
  // the scaled shield-bone space, where the bone carries the ENTRY-frame ftCo_800921DC scale
  // (raw x650/(1 - x10) lightshield, x10 = trigger_deadzone 0.30, no upper clamp) for the live
  // GuardReflect window. Witness bracket (replay-real): MAJ rec1995 reflects at pre-move dist
  // 3.28; MAJ rec259 and rec6242 shield-hit at 6.17/5.38 - 0.75 x entry-scale ~= 4.1 with the
  // raw item HitCapsule size separates all three.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80091D58,ftCo_8009370C}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
  if (batch == NULL || common == NULL || !(common->powershield_reflect_size > 0.0f)) {
    return 0.0f;
  }
  float entry_light = 0.0f;
  const float trig = msl_trigger_unit_from_input(
      batch->state.input_buttons[d_idx], batch->state.input_l[d_idx], batch->state.input_r[d_idx]);
  const float dz_denom = 1.0f - common->trigger_deadzone;
  if (dz_denom > 0.0f && trig >= 0.0f) {
    entry_light = trig / dz_denom;
  }
  float entry_ls =
      (entry_light * (common->shield_size_lightshield_max - common->shield_size_lightshield_min)) +
      common->shield_size_lightshield_min;
  if (entry_ls < 0.0f) {
    entry_ls = 0.0f;
  }
  const float entry_hp_ratio =
      item_clamp01(batch->state.shield_hp[d_idx] / common->start_shield_health);
  const float entry_scale = ((1.0f - common->shield_size_min_scale) * (entry_hp_ratio * entry_ls)) +
                            common->shield_size_min_scale;
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  const float init =
      (ch != NULL && ch->initial_shield_size > 0.0f) ? ch->initial_shield_size : 0.0f;
  return common->powershield_reflect_size * entry_scale * init *
         batch->state.fighter_scale_y[d_idx];
}

uint8_t item_prev_action_is_guardon_spawn_frame_reflect_source(uint16_t action_id) {
  // Fresh GuardOn -> GuardReflect spawn-frame item ordering:
  // Run-family IASA can reach the digital powershield reflect owner early enough for a newly
  // spawned laser to transfer owner in the same item pass. Wait/Turn fresh GuardOn snapshots can
  // still enter GuardReflect by post-frame, but replay-real AGN/GAT controls keep the newly spawned
  // laser shooter-owned on that frame.
  // MSLMSO01 can group these actions by IASA/phys callback family, but it does not encode this
  // branch-local item pass ordering, so keep the decomp-shaped semantic list explicit.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_IASA,ftCo_RunDirect_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return (action_id == (uint16_t)MSL_ACT_DASH || action_id == (uint16_t)MSL_ACT_RUN ||
          action_id == (uint16_t)MSL_ACT_RUN_DIRECT)
             ? 1u
             : 0u;
}

uint8_t item_prev_action_is_guardon_spawn_frame_non_reflect_source(uint16_t action_id) {
  // Fresh GuardOn spawn-frame non-reflect boundary:
  // Wait/Turn -> GuardOn snapshots can enter GuardReflect by post-frame, but replay-real locks keep
  // the newly spawned laser shooter-owned for that item pass. Do not generalize this to arbitrary
  // non-run sources: AttackAir/steady-GuardOn rows can still take same-frame Item_80269DC8 shield
  // contact.
  // This is not table-expressible by MotionState callback identity alone; it is the local
  // GuardOn_IASA / Turn_Anim branch ordering before the item callback sees the newly spawned laser.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return (action_id == (uint16_t)MSL_ACT_WAIT || action_id == (uint16_t)MSL_ACT_TURN) ? 1u : 0u;
}

uint8_t laser_grounded_body_uses_lbcoll_hurt_radius(const MslBatch* batch, size_t d_idx,
                                                    uint8_t laser_state, float laser_age_frames,
                                                    uint16_t item_type) {
  // Age gate source owner:
  // - it_8029C504 creates the laser article, initializes xDD4_itemVar.foxlaser.pos to the spawn
  //   position, and starts the item motion/lifetime state.
  // - This grounded BODY lbColl hurt-radius slice applies only after that spawn/create-edge frame;
  //   live laser travel is then owned by itFoxlaser_UnkMotion1_Phys' previous-position snapshot and
  //   it_8029C4D4's previous-to-current item collision segment. Fresh laser rows stay on the
  //   spawn-frame GuardOn/create-edge owners above.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_falco_laser(item_type) == 0u && item_type_is_fox_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.on_ground[d_idx] == 0u || batch->state.shield_radius[d_idx] > 0.0f ||
      batch->state.hurtbox_state[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.char_id[d_idx] != (uint8_t)MSL_CHAR_ID_FOX ||
      (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DOWN_BACK_U &&
       batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DOWN_BACK_D)) {
    return 0u;
  }
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  if (anim_u32 > 0xFFFFu) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
  if (pose_frame == 0u) {
    return 0u;
  }
  uint8_t cur_hit_status = 0u;
  uint8_t prev_hit_status = 0u;
  if (move_tables_hit_status_at_frame(batch->state.char_id[d_idx], msid, pose_frame,
                                      &cur_hit_status) == 0u ||
      move_tables_hit_status_at_frame(batch->state.char_id[d_idx], msid,
                                      (uint16_t)(pose_frame - 1u), &prev_hit_status) == 0u ||
      cur_hit_status != 0u || prev_hit_status == 0u) {
    return 0u;
  }
  // Fox DownBack terminal item BODY lbColl owner on movescript hit-status release edges:
  // - ftColl_8007925C routes item BODY against every enabled fighter HurtCapsule through
  //   lbColl_8000805C with `ftCommon_8007F804(fp)`, `item->scl`, `fp->x34_scale.y`, and
  //   `fp->cur_pos.z`.
  // - The broad hurt-radius lane is retained only when the data-backed hit-status table says the
  //   current DownBack pose frame just released a nonzero x1988 window. Ordinary grounded
  //   vulnerable rows, shield defensive options, PassiveStand, and Falco DownBack controls stay on
  //   the exact x58/x4C matrix/local-radius owner and do not borrow this release-edge broadphase.
  // - This is an action/character data boundary, not a replay row: Fox and Falco DownBack use
  //   distinct extracted hurtcaps/animations, and the retained owner is the Fox terminal
  //   DownBack* release edge.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58,lbColl_804D7A38}
  // data/hurtcaps/{fox,falco}.bin
  // data/scripts/{fox,falco}.bin (MSLFTSC1 set_hit_status / hurt-state events)
  return 1u;
}

uint8_t laser_body_guard_family_no_submotion_exact_lbcoll_applies(const MslBatch* batch,
                                                                  size_t d_idx,
                                                                  uint8_t body_shield_adjacent) {
  if (batch == NULL || body_shield_adjacent == 0u || batch->state.hurtbox_state[d_idx] != 0u ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] <= 0xFFFFu) {
    return 0u;
  }
  switch (batch->state.action_id[d_idx]) {
    case (uint16_t)MSL_ACT_GUARD_ON:
    case (uint16_t)MSL_ACT_GUARD:
    case (uint16_t)MSL_ACT_GUARD_SET_OFF:
    case (uint16_t)MSL_ACT_GUARD_REFLECT:
      // Item BODY source owner:
      // ftColl_8007925C evaluates item BODY through lbColl_8000805C after ShieldDesc/ReflectDesc
      // branches. For no-submotion Guard-family replay snapshots, the motion-state table still
      // names the source collision submotion even when Slippi serializes animation_index=-1/-2.
      // Use that matrix path instead of the reduced replay-visible sample only inside the shield-adjacent
      // Guard-family slice.
      // refs/melee/src/melee/ft/ftmotionstates.c::{
      //   ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      return 1u;
    default:
      return 0u;
  }
}

uint8_t laser_body_uses_exact_lbcoll_hitcapsule_sweep(const MslBatch* batch, size_t d_idx,
                                                      uint8_t laser_state, float laser_age_frames,
                                                      uint16_t item_type,
                                                      uint8_t body_shield_adjacent,
                                                      uint8_t flatten_body_hurt_z) {
  if (batch == NULL) {
    return 0u;
  }
  if (laser_state != 0u ||
      (item_type_is_falco_laser(item_type) == 0u && item_type_is_fox_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (flatten_body_hurt_z != 0u || batch->state.hurtbox_state[d_idx] != 0u) {
    return 0u;
  }
  if (body_shield_adjacent != 0u && !laser_body_guard_family_no_submotion_exact_lbcoll_applies(
                                        batch, d_idx, body_shield_adjacent)) {
    return 0u;
  }
  // Source owner for ordinary blaster BODY:
  // - it_8027137C advances item HitCapsule state by copying x4C into x58, then sampling the
  //   current JObj endpoint into x4C.
  // - ftColl_8007925C calls lbColl_8000805C on that x58->x4C segment for every fighter hurtcap.
  // - ftColl_80077C60 consumes HitCapsule.coll_distance after lbColl_80006E58 and routes
  //   `coll_distance < p_ftCommonData->x7A8` to item phantom/tip-log instead of full BODY damage.
  // This is callback/data-owned, not an action-row sweep list.
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  return 1u;
}

uint8_t laser_body_exact_lbcoll_flattens_hurt_z(const MslBatch* batch, size_t d_idx,
                                                uint8_t body_shield_adjacent,
                                                uint8_t flatten_body_hurt_z) {
  if (flatten_body_hurt_z != 0u) {
    return 1u;
  }
  if (laser_body_guard_family_no_submotion_exact_lbcoll_applies(batch, d_idx,
                                                                body_shield_adjacent) == 0u) {
    return 0u;
  }
  // lbColl_8000805C source Z lane:
  // ftColl_8007925C always passes ftCommon_8007F804(fp) and fp->cur_pos.z into item BODY
  // lbColl_8000805C. That function refreshes hurt capsule endpoints from the JObj, then flattens
  // both endpoint Z values to fp->cur_pos.z before the segment/local-radius test. Keep this
  // source lane on the exact no-submotion Guard-family item BODY owner; the reduced fallback
  // capsule path remains unchanged for unrelated states whose matrix/local-radius owner is still
  // not promoted.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 1u;
}

uint8_t laser_grounded_body_landing_fall_special_exact_z_owner(const MslBatch* batch, size_t d_idx,
                                                               float laser_age_frames) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.on_ground[d_idx] == 0u || batch->state.shield_radius[d_idx] > 0.0f ||
      batch->state.hurtbox_state[d_idx] != 0u || !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    return 0u;
  }
  // LandingFallSpecial exact item BODY owner:
  // - ftColl_8007925C routes item BODY through lbColl_8000805C with a matrix argument and
  //   fp->cur_pos.z, so the exact x58->x4C HitCapsule path must evaluate against flattened hurtcap
  //   Z instead of falling through to the old 2D/AABB miss bridge.
  // - Keep the promoted lane on the LandingFallSpecial replay-real family while the broader
  //   all-state flattened-Z owner is still blocked by missing phantom/hurtcap-order filters exposed
  //   by AttackHi3/CDO controls.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 1u;
}

uint8_t laser_tail_shallow_body_contact_rejected(const MslBatch* batch, size_t d_idx,
                                                 uint16_t item_type, uint8_t laser_state,
                                                 uint8_t hit_hb_id, float laser_radius, int cap_i,
                                                 float overlap_amount, uint16_t item_attack_id) {
  if (batch != NULL && laser_state == 0u && laser_radius > 0.0f &&
      batch->state.hurtbox_state[d_idx] == 0u && batch->state.hitlag[d_idx] == 0u &&
      batch->state.hitstun[d_idx] == 0u) {
    const MslHurtCap* caps = NULL;
    uint16_t cap_count = 0u;
    const uint8_t char_id = batch->state.char_id[d_idx];
    const uint8_t cap_is_tail =
        (hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count &&
         caps[cap_i].bone_part_id == (uint16_t)MSL_ITEM_HURTCAP_FOX_FALCO_TAIL_PART_ID)
            ? 1u
            : 0u;
    const uint16_t action_id = batch->state.action_id[d_idx];
    const uint8_t flags_2218 = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_2218_INDEX];
    const uint8_t reflect_behavior_only =
        ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) != 0u &&
         (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u)
            ? 1u
            : 0u;
    const uint8_t live_item_b1_only =
        ((flags_2218 & (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                                 MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR |
                                 MSL_STATE_FLAG_2218_REFLECTING)) ==
         (uint8_t)MSL_STATE_FLAG_2218_B1)
            ? 1u
            : 0u;
    const uint8_t cap_is_low_leg =
        (hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count && caps[cap_i].bone_part_id == (uint16_t)7u &&
         caps[cap_i].height == 0u && caps[cap_i].is_grabbable == 0u)
            ? 1u
            : 0u;
    const uint8_t fox_laser_marth_high_head_scaled_tail =
        (item_type_is_fox_laser(item_type) != 0u &&
         batch->state.char_id[d_idx] == (uint8_t)MSL_CHAR_ID_MARTH && hit_hb_id == 3u &&
         hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count && caps[cap_i].bone_part_id == (uint16_t)60u &&
         caps[cap_i].height == 2u && caps[cap_i].is_grabbable != 0u)
            ? 1u
            : 0u;
    const uint8_t falco_airborne_shallow_tail_lane =
        (item_type_is_falco_laser(item_type) != 0u && overlap_amount <= laser_radius &&
         batch->state.on_ground[d_idx] == 0u)
            ? 1u
            : 0u;
    if ((falco_airborne_shallow_tail_lane != 0u && cap_is_tail != 0u &&
         ((hit_hb_id == 0u &&
           (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
           (reflect_behavior_only != 0u || live_item_b1_only != 0u)) ||
          (hit_hb_id >= 2u && action_id >= (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
           action_id <= (uint16_t)MSL_ACT_JUMP_AERIAL_B))) ||
        (falco_airborne_shallow_tail_lane != 0u && cap_is_low_leg != 0u && hit_hb_id == 1u &&
         action_id == (uint16_t)MSL_ACT_FALL &&
         batch->state.last_attack_landed[d_idx] != item_attack_id) ||
        fox_laser_marth_high_head_scaled_tail != 0u) {
      // Source-owned shallow item BODY rejection:
      // - state0 Falco laser HitCapsules are authored in MSLLASR1 with four offsets and a radius;
      //   ftColl_8007925C tests each one against extracted hurtcaps via lbColl_8000805C.
      // - The shallow edge owner is only admitted for the extracted FtPart-18 tail hurtcap and for
      //   overlaps no deeper than the laser HitCapsule radius. Early JumpF/B additionally requires
      //   the source reflect-behavior carry or the raw fp+0x2218_b1 live-item callback lane, while
      //   JumpAerial* is limited to the trailing half of the authored laser offsets. Deeper
      //   overlaps and non-tail BODY caps continue to normal BODY damage.
      // - The same lbColl shallow-miss owner applies to the state0 hb1/Fall low-leg packet: hb1 is
      //   the only authored offset at the SDS edge whose exact x58->x4C local-radius packet misses
      //   cap11 (data/hurtcaps/{fox,falco}.json bone 7, low, non-grabbable), while adjacent hb2/hb3
      //   low-leg rows and same-attack live-shot carry rows remain BODY-eligible.
      // - Fox state0 offset-3 against Marth's high/head cap is a scaled-visual tail offset miss:
      //   the generated MSLLASR1 visual scale can place the fourth sample into cap2, while the
      //   source item BODY packet remains on the authored/local-radius lane and keeps the shot
      //   alive. Keep this on the extracted cap identity and authored offset; grounded/airborne
      //   state is not the owner for that cap2 local-radius miss.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      // data/items/lasers.bin (MSLLASR1 state0 size/offsets)
      // data/hurtcaps/{fox,falco}.bin cap12 -> FtPart 18; data/hurtcaps/marth.bin cap2 -> FtPart 60.
      return 1u;
    }
  }
  return 0u;
}

uint8_t laser_exact_lbcoll_body_contact_admits_candidate(
    const MslBatch* batch, size_t d_idx, const MslCommonParams* common, uint8_t hurt_height,
    float overlap_amount, float laser_prev_scale_z, float laser_scale_z, uint16_t item_type,
    uint8_t laser_state, uint8_t hit_hb_id, float laser_radius, int cap_i,
    uint16_t item_attack_id) {
  if (!(overlap_amount > 0.0f)) {
    return 0u;
  }
  if (laser_tail_shallow_body_contact_rejected(batch, d_idx, item_type, laser_state, hit_hb_id,
                                               laser_radius, cap_i, overlap_amount,
                                               item_attack_id)) {
    return 0u;
  }
  if (common != NULL && overlap_amount <= common->phantom_overlap_max_x7a8) {
    return 1u;
  }
  if (batch != NULL && item_type_is_falco_laser(item_type) != 0u && hurt_height >= (uint8_t)2u &&
      fabsf(laser_scale_z - laser_prev_scale_z) <= 1.0e-5f &&
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
      batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_TURN &&
      batch->state.action_frame[d_idx] == 1) {
    // Dash_CheckInput -> Turn first-frame high/head item-HitCapsule boundary:
    // - Dolphin lbColl probes in `reports/triage/item11_dolphin_lbcoll_dsg671/` and paired
    //   row forensics in `reports/triage/item11_dcc7573_vs_dsg671_forensics/` show vanilla rejects
    //   the stable-scale high/head candidate on the first Turn frame while lower/mid contacts and
    //   growing-scale high/head contacts remain live.
    // - This stays tied to lbColl's exported HurtHeight plus item x58/x4C scale phase; it is not a
    //   replay id or laser/Dash blanket rejection.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    return 0u;
  }
  if (batch != NULL && hurt_height < (uint8_t)2u &&
      fabsf(laser_scale_z - laser_prev_scale_z) > 1.0e-5f &&
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING &&
      ((batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_TURN &&
        batch->state.action_frame[d_idx] == 1) ||
       (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
        batch->state.action_frame[d_idx] == 1))) {
    // Landing -> Turn/Dash first-frame low/mid item-HitCapsule phase guard:
    // Dolphin primitive probe `reports/triage/item11_dolphin_lbcoll_dsg671/` shows vanilla does
    // not accept the low-cap Falco-laser BODY contact across the fresh Turn/Dash handoff after
    // Landing while the item HitCapsule scale is still changing; stable-scale DCC 7573 hits on the
    // same action handoff, so the guard is the item HitCapsule x58/x4C scale-propagation phase, not
    // the action id alone. Keep high/head exact contacts and tiny phantom overlaps live.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
    return 0u;
  }
  // Source `lbColl_8000805C` forwards the HurtHeight class (`x43_b2`) for the candidate capsule.
  // Outside the source-probed x58/x4C scale-phase boundaries above, the exact matrix/local-radius
  // owner applies to the ordinary BODY loop across heights.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077C60
  return 1u;
}

uint8_t laser_item_phantom_hitlag_suppressed_by_reflect_behavior_carry(
    const MslBatch* batch, size_t d_idx, uint8_t laser_state, float overlap_amount,
    const MslCommonParams* common) {
  if (batch == NULL || common == NULL || laser_state != 0u || !(overlap_amount > 0.0f) ||
      overlap_amount > common->phantom_overlap_max_x7a8 || batch->state.on_ground[d_idx] != 0u ||
      batch->state.hurtbox_state[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  if (action_id != (uint16_t)MSL_ACT_JUMP_F && action_id != (uint16_t)MSL_ACT_JUMP_B) {
    return 0u;
  }
  const uint8_t flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) == 0u ||
      (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) != 0u) {
    return 0u;
  }
  // Item phantom attribution-only source guard:
  // ftColl_80077C60 registers the item HitCapsule victim ring before checking the no-damage guards
  // (`x1988`, `x198C`, `x221D_b6`, and selected hurtcap state). A stale reflect-behavior carry can
  // therefore publish item source attribution for a tiny item overlap without starting
  // Fighter_ProcessHit phantom hitlag. Keep this to the early JumpF/JumpB source state: settled
  // JumpAerial* rows still run the phantom-hitlag path even when stale fp+0x2218_b5 is serialized.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077C60,ftColl_8007B868}
  // refs/melee/src/melee/it/itcoll.c::it_8026FC00
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  return 1u;
}

uint8_t laser_airborne_body_uses_flattened_hurt_z(const MslBatch* batch, size_t d_idx,
                                                  uint8_t laser_state, uint16_t item_type,
                                                  uint16_t item_attack_id) {
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_fox_laser(item_type) == 0u && item_type_is_falco_laser(item_type) == 0u)) {
    return 0u;
  }
  (void)d_idx;
  (void)item_attack_id;
  // Optional x34_scale.z collision matrix owner:
  // ftColl_8007925C passes ftCommon_8007F804(fp) to lbColl_8000805C. That helper returns
  // fp->x44_mtx only when fp->x34_scale.z != 1; only then does lbColl rewrite hurtcap endpoint Z
  // to fp->cur_pos.z before the segment/local-radius test. Airborne Fall alone does not imply this
  // hidden transform lane. Keeping the old broad Fall predicate over-applied to Marth high caps
  // (VSA:2008) and consumed a Falco laser that vanilla keeps alive.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 0u;
}

uint8_t laser_airborne_damagefall_uses_lbcoll_hurt_radius(const MslBatch* batch, size_t d_idx,
                                                          size_t o_idx, uint8_t laser_state,
                                                          float laser_age_frames,
                                                          uint16_t item_type,
                                                          uint16_t item_attack_id) {
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_fox_laser(item_type) == 0u && item_type_is_falco_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FALL ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hurtbox_state[d_idx] != 0u ||
      batch->state.shield_radius[d_idx] > 0.0f) {
    return 0u;
  }
  if (batch->state.last_attack_landed[d_idx] == item_attack_id) {
    return 0u;
  }
  // SpecialAirNLoop -> Landing blaster handoff:
  // - ftFx_SpecialN_GetBlasterAction no longer reports the loop state once Landing has entered,
  //   but the already-spawned laser still reaches the same-frame item BODY pass.
  // - Keep the promoted lbColl hurt-radius lane on this source-owned landing handoff. The
  //   adjacent in-air SpecialAirNLoop frame is replay-real no-hit and proves the full
  //   previous-to-current swept source path is too broad for this DamageFall row.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialN_GetBlasterAction}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_LANDING ||
      msl_motion_state_fx_special_kind(batch->state.char_id[o_idx],
                                       batch->state.seed_prev_action_id[o_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP) {
    return 0u;
  }
  return 1u;
}

uint8_t item_try_guard_fresh_shield_center(const MslBatch* batch, size_t d_idx,
                                           float laser_age_frames, float* out_x, float* out_y,
                                           float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  const uint16_t seed_prev_action_id = batch->state.seed_prev_action_id[d_idx];
  const uint16_t frame_start_prev_action_id = batch->state.prev_action_id[d_idx];
  const uint8_t fresh_guard_on_entry =
      (action_id == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       batch->state.guard_on_entered_this_frame[d_idx] != 0u)
          ? 1u
          : 0u;
  const uint8_t fresh_locomotion_guard_reflect_entry =
      item_is_fresh_guardreflect_shield_center_source(batch, d_idx, frame_start_prev_action_id);
  const uint8_t guard_command_bit_birth_item_pose =
      ((action_id == (uint16_t)MSL_ACT_GUARD_ON || action_id == (uint16_t)MSL_ACT_GUARD) &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       seed_prev_action_id == action_id && laser_age_frames <= 1.0f &&
       (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                 (size_t)MSL_STATE_FLAGS_2218_INDEX] &
        (uint8_t)MSL_STATE_FLAG_2218_B1) != 0u)
          ? 1u
          : 0u;
  if (!fresh_guard_on_entry && !fresh_locomotion_guard_reflect_entry &&
      !guard_command_bit_birth_item_pose) {
    return 0u;
  }

  // Fresh frozen GuardOn / locomotion GuardReflect projectile-shield bridge:
  // - ftCo_800921DC zeroes the shield-joint translate on GuardOn entry,
  // - locomotion GuardReflect entry (ftCo_80093A50) also calls ftCo_80092450 then ftCo_800921DC,
  // - ftCo_80091E78(0) preserves that live current pose on the first entry frame,
  // - later teacher-forced frozen GuardOn / GuardReflect rows are not the same owner and must keep
  //   the stable baseline shield bubble path.
  // Scope this to the grounded locomotion states that actually delegate to ftCo_80091A4C before
  // shield entry and whose pose family matches the extracted GuardOn current-pose table. GuardOff
  // can also enter GuardReflect through GuardOff_IASA -> ftCo_80093694 -> ftCo_8009388C; that path
  // keeps graphics/shield-bone pose while installing ReflectDesc. Landing IASA also calls
  // ftCo_80091A4C, but the repo only extracts Guard/GuardOn pose ownership in data/shields/*.bin;
  // applying that GuardOn current-pose table to Landing-origin GuardReflect rows creates unrelated
  // shield-contact drift.
  // Restrict this to callback-local item shield precedence only; broadening the current-pose bridge
  // to seeded frozen guard snapshots regresses replay-real shield-hit rows.
  // Steady GuardOn/Guard rows with raw fp+0x2218_b1 share the current-pose command lane only on the
  // newborn SpecialN article pass: by the next item callback, Item_80269DC8 shield contact uses the
  // normal settled Guard bubble and can enter GuardSetOff.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_{Wait,Walk,Turn,Dash,Run,RunDirect,Squat,SquatWait,SquatRv,Landing}.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800921DC,ftCo_80091E78,ftCo_800924C0,ftCo_80093694,ftCo_8009388C,ftCo_80093A50}
  // data/shields/{fox,falco}.bin: Guard / GuardOn shield center tables only (MSLSHLD1 v4)
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 ||
      tv.guard_on_xyz == NULL || tv.guard_on_frame_count == 0u) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  const float model_scaling =
      (ch != NULL && isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling
                                                                              : 1.0f;
  const float scale_y = batch->state.fighter_scale_y[d_idx] * model_scaling;
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float gx = tv.guard_on_xyz[0];
  const float gy = tv.guard_on_xyz[1];
  const float gz = tv.guard_on_xyz[2];
  const float glx = gx * scale_y;
  const float gly = gy * scale_y;
  const float glz = gz * scale_y;
  *out_x = batch->state.pos_x[d_idx] + (facing_dir * glz);
  *out_y = batch->state.pos_y[d_idx] + gly;
  *out_z = batch->state.pos_z[d_idx] + (-facing_dir * glx);
  return 1u;
}

uint8_t item_try_guardon_carried_behavior_shield_center(const MslBatch* batch, size_t d_idx,
                                                        float* out_x, float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const uint8_t flags_2218 = batch->state.state_flags_2218_frame_start[d_idx];
  const uint8_t behavior_carry =
      ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) != 0u &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B2) == 0u &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u)
          ? 1u
          : 0u;
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.animation_index[d_idx] != UINT32_MAX || batch->state.action_frame[d_idx] >= 0 ||
      !behavior_carry) {
    return 0u;
  }

  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 || tv.xyz == NULL ||
      tv.frame_count == 0u) {
    return 0u;
  }

  // Item-vs-fighter GuardOn carried behavior lane:
  // `ftColl_8007925C` checks item ShieldDesc before item BODY, but carried GuardOn laser rows with
  // raw fp+0x2218 reflect behavior and B2 clear keep the item callback on the behavior-byte sample
  // boundary instead of reusing the fighter-vs-fighter sustained GuardOn ShieldDesc publication.
  // Reconstruct the ordinary Guard tilt center from the extracted ShieldDesc table for this item
  // branch only; fighter-vs-fighter ShieldDesc still consumes the current GuardOn x10 pose.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
  // data/shields/<char>.bin::MSLSHLD1 Guard tilt owner
  uint16_t neutral = tv.neutral_frame;
  if (neutral >= tv.frame_count) {
    neutral = 0u;
  }
  uint16_t f = batch->state.guard_tilt_x8[d_idx];
  if (f >= tv.frame_count) {
    f = (uint16_t)(tv.frame_count - 1u);
  }
  float mag = batch->state.guard_tilt_x4[d_idx];
  if (mag < 0.0f) {
    mag = 0.0f;
  }
  if (mag > 1.0f) {
    mag = 1.0f;
  }
  const size_t n_i = (size_t)neutral * 3u;
  const size_t f_i = (size_t)f * 3u;
  const float dx = tv.xyz[n_i + 0u] + mag * (tv.xyz[f_i + 0u] - tv.xyz[n_i + 0u]);
  const float dy = tv.xyz[n_i + 1u] + mag * (tv.xyz[f_i + 1u] - tv.xyz[n_i + 1u]);
  const float dz = tv.xyz[n_i + 2u] + mag * (tv.xyz[f_i + 2u] - tv.xyz[n_i + 2u]);
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float lx = dx * scale_y;
  const float ly = dy * scale_y;
  const float lz = dz * scale_y;
  *out_x = batch->state.pos_x[d_idx] + (facing_dir * lz);
  *out_y = batch->state.pos_y[d_idx] + ly;
  *out_z = batch->state.pos_z[d_idx] + (-facing_dir * lx);
  return 1u;
}

uint8_t item_swept_sphere_sphere_intersects_3d(float ax0, float ay0, float az0, float ax1,
                                               float ay1, float az1, float ar, float bx, float by,
                                               float bz, float br) {
  // Segment-point closest distance for swept item sphere vs shield sphere center.
  const float vx = ax1 - ax0;
  const float vy = ay1 - ay0;
  const float vz = az1 - az0;
  const float wx = bx - ax0;
  const float wy = by - ay0;
  const float wz = bz - az0;
  const float vv = vx * vx + vy * vy + vz * vz;
  float t = 0.0f;
  if (vv > 0.0f) {
    t = (wx * vx + wy * vy + wz * vz) / vv;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float cx = ax0 + t * vx;
  const float cy = ay0 + t * vy;
  const float cz = az0 + t * vz;
  return item_sphere_sphere_intersects_3d(cx, cy, cz, ar, bx, by, bz, br);
}

void item_guard_reflect_apply_recharge(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  float hp = batch->state.shield_hp[d_idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[d_idx] = hp;
  }
}

void item_guard_reflect_restore_anim_drain(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  // GuardReflect item-BODY handoff:
  // - GuardReflect_Anim runs ftCo_80093BC0 then GuardOn_Anim before IASA / item collision,
  // - item collision can consume the live GuardReflect/Escape owner into Damage after that
  //   shield-state callback phase,
  // - replay post-frame shield HP does not retain the same-frame shield-hold drain from the
  //   consumed shield state. If Fighter_ProcessHit owns a recharge tick, that recharge has already
  //   been applied inside combat_apply_item_hit().
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_IASA
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // The caller snapshots the GuardReflect/no-submotion predicate before combat_apply_item_hit(),
  // because a successful BODY hit immediately changes the fighter into Damage*. Do not re-read the
  // action predicate here after the owner has already been consumed.
  const float light = batch->state.lightshield_amount[d_idx];
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;
  float hp = batch->state.shield_hp[d_idx] + drain;
  if (hp > c->start_shield_health) {
    hp = c->start_shield_health;
  }
  batch->state.shield_hp[d_idx] = hp;
}

uint8_t item_guard_reflect_body_hit_consumes_shield_state(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  // Only restore consumed shield-state drain when GuardReflect was already the frame-start owner.
  // GuardOn -> GuardReflect -> BODY in one item pass keeps the GuardOn_Anim drain before
  // Fighter_ProcessHit recharge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint8_t frozen_guard_reflect =
      (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[d_idx] < 0 &&
       batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       (batch->state.guard_reflect_timer_x14[d_idx] != 0u ||
        batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u))
          ? 1u
          : 0u;
  const uint8_t guard_reflect_iasa_escape =
      ((action_id == (uint16_t)MSL_ACT_ESCAPE_F || action_id == (uint16_t)MSL_ACT_ESCAPE_B ||
        action_id == (uint16_t)MSL_ACT_ESCAPE_N) &&
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u &&
       batch->state.action_frame[d_idx] <= 1)
          ? 1u
          : 0u;
  return (frozen_guard_reflect || guard_reflect_iasa_escape) ? 1u : 0u;
}

uint8_t item_guardon_reflect_body_hit_undoes_action_recharge(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // The action-level recharge path admits fresh GuardOn -> GuardReflect rows because
  // ftCo_8009388C clears the shield descriptor. If item BODY contact immediately consumes that
  // same hidden GuardReflect owner, Fighter_ProcessHit owns the visible recharge instead; remove
  // the pre-item recharge after the accepted BODY hit.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
          batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX)
             ? 1u
             : 0u;
}

void item_guardon_reflect_undo_action_recharge(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  float hp = batch->state.shield_hp[d_idx] - c->shield_recharge_per_frame;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;
}

uint8_t item_guardreflect_active_timer_shield_contact_needs_drain(const MslBatch* batch,
                                                                  size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.animation_index[d_idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14[d_idx] > 0u &&
          (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
           batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON))
             ? 1u
             : 0u;
}

uint8_t item_guardreflect_origin_x14_expired_this_callback(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // GuardOn-origin GuardReflect expiry:
  // - `ftCo_8009388C` enters GuardReflect from an already-shielding guard state with ShieldDesc
  //   cleared and only ReflectDesc live.
  // - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` decrements x14; when the frame-start seed was the
  //   final x14 tick, ftCo_80093BC0 recreates ShieldDesc before item/fighter collision.
  // - This matches combat.c's final-x14 ShieldDesc owner and prevents the item path from treating
  //   the same callback phase as frozen keepalive.
  // - Keep rows with raw fp+0x2218_b1 set on the existing live-article lane; replay-real
  //   final-x14 controls with that command bit do not hand off to Item_80269DC8 here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
  // refs/melee/src/melee/ft/types.h (fp+0x2218_b1)
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.animation_index[d_idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
          batch->state.guard_reflect_timer_x14[d_idx] == 0u &&
          batch->state.guard_reflect_origin_guardon[d_idx] != 0u &&
          (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                    (size_t)MSL_STATE_FLAGS_2218_INDEX] &
           (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u)
             ? 1u
             : 0u;
}

void item_guardreflect_apply_contact_drain(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  // Active-x14 GuardReflect no-contact rows leave the shield descriptor cleared and recharge at
  // action level. Once Item_80269DC8 owns a shield contact, the GuardOn_Anim shield drain is
  // visible before shield-hit depletion/bounce resolution.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800925A4}
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  const float light = batch->state.lightshield_amount[d_idx];
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  float hp = batch->state.shield_hp[d_idx] - (c->shield_hold_drain_mul * drain_factor);
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;
}
