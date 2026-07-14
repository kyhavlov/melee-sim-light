#include "items_internal.h"

uint8_t item_reflector_descriptor(const MslBatch* batch, size_t idx, float* out_x, float* out_y,
                                  float* out_z, float* out_radius, float* out_damage_mul,
                                  float* out_speed_mul, int32_t* out_max_damage) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL || out_radius == NULL ||
      out_damage_mul == NULL || out_speed_mul == NULL || out_max_damage == NULL ||
      !(batch->state.reflector_radius[idx] > 0.0f)) {
    return 0u;
  }
  *out_x = batch->state.reflector_x[idx];
  *out_y = batch->state.reflector_y[idx];
  *out_z = batch->state.reflector_z[idx];
  *out_radius = batch->state.reflector_radius[idx];
  *out_damage_mul = batch->state.reflector_damage_mul[idx];
  *out_speed_mul = batch->state.reflector_speed_mul[idx];
  *out_max_damage = batch->state.reflector_max_damage[idx];
  return 1u;
}

static inline uint8_t item_contact_targets_defender(const MslItemHitCapsulePacket* hit,
                                                    uint8_t grounded) {
  return grounded != 0u ? (uint8_t)((hit->flags & (uint32_t)MSL_ITEM_CONTACT_TARGET_GROUNDED) != 0u)
                        : (uint8_t)((hit->flags & (uint32_t)MSL_ITEM_CONTACT_TARGET_AERIAL) != 0u);
}

MslItemFighterContactKind item_hitcapsule_select_fighter_contact(MslBatch* batch, int bi,
                                                                 int item_slot, int defender,
                                                                 const MslItemHitCapsulePacket* hit,
                                                                 MslItemFighterContact* out) {
  if (batch == NULL || hit == NULL || out == NULL || bi < 0 || bi >= batch->batch_size ||
      item_slot < 0 || item_slot >= MSL_MAX_ITEMS || defender < 0 ||
      defender >= (int)batch->config.num_players || !(hit->radius > 0.0f) ||
      (hit->flags & (uint32_t)MSL_ITEM_CONTACT_BODY_ENABLED) == 0u) {
    return MSL_ITEM_FIGHTER_CONTACT_NONE;
  }
  memset(out, 0, sizeof(*out));
  out->fighter_hitbox = -1;
  out->hurtcap = -1;

  const size_t ii = msl_idx_item(bi, item_slot);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t defender_iid = batch->state.instance_id[d_idx];
  if (batch->state.item_exists[ii] == 0u ||
      msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx]) != 0u ||
      item_contact_targets_defender(hit, batch->state.on_ground[d_idx]) == 0u ||
      ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_FACING_FILTER) != 0u &&
       batch->state.facing[d_idx] == (uint8_t)(batch->state.item_direction[ii] > 0.0f)) ||
      hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, (int)hit->hitbox_id, defender,
                                         defender_iid) == 0u) {
    return MSL_ITEM_FIGHTER_CONTACT_NONE;
  }

  // Fighter priority-13 item traversal is descriptor ordered. Only an accepted overlap consumes
  // the later owners; a live descriptor miss falls through.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  if ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_REFLECTABLE) != 0u &&
      item_reflector_descriptor(batch, d_idx, &out->descriptor_x, &out->descriptor_y,
                                &out->descriptor_z, &out->descriptor_radius,
                                &out->reflect_damage_mul, &out->reflect_speed_mul,
                                &out->reflect_max_damage) != 0u &&
      item_swept_sphere_sphere_intersects_3d(hit->x0, hit->y0, hit->z0, hit->x1, hit->y1, hit->z1,
                                             hit->radius, out->descriptor_x, out->descriptor_y,
                                             out->descriptor_z, out->descriptor_radius) != 0u) {
    out->kind = MSL_ITEM_FIGHTER_CONTACT_REFLECT;
    return out->kind;
  }

  // AbsorbDesc follows ReflectDesc in source. None of the supported fighter roster publishes the
  // x2218_b6 AbsorbDesc owner (Marth Counter is a ShieldDesc), so the extracted absorbable bit is
  // intentionally retained in the packet without inventing an unsupported descriptor.

  if ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_CLANK) != 0u) {
    for (int fighter_hb = 0; fighter_hb < MSL_MAX_HITBOXES; fighter_hb++) {
      const size_t hi = idx_hitbox(bi, defender, fighter_hb);
      const uint16_t flags = batch->state.hitbox_flags[hi];
      if (batch->state.hitbox_enabled[hi] == 0u || !msl_hitbox_x42_b5_enabled(flags) ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
          batch->state.hitbox_element[hi] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          batch->state.hitbox_element[hi] == (uint8_t)MSL_HIT_ELEMENT_INERT) {
        continue;
      }
      if (hit->item_grounded != 0u) {
        if ((flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
          continue;
        }
      } else if ((flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
        continue;
      }
      if (hitlist_allows_fighter_item(batch, bi, defender, fighter_hb, item_slot,
                                      batch->state.item_spawn_id[ii]) == 0u ||
          item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, hi, hit->x0, hit->y0, hit->x1,
                                                            hit->y1, hit->radius) == 0u) {
        continue;
      }
      out->fighter_hitbox = (int8_t)fighter_hb;
      out->kind = MSL_ITEM_FIGHTER_CONTACT_CLANK;
      return out->kind;
    }
  }

  if ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_SHIELDABLE) != 0u &&
      marth_counter_shielddesc_world(batch, d_idx, &out->descriptor_x, &out->descriptor_y,
                                     &out->descriptor_z, &out->descriptor_radius) != 0u &&
      item_swept_sphere_sphere_intersects_3d(hit->x0, hit->y0, hit->z0, hit->x1, hit->y1, hit->z1,
                                             hit->radius, out->descriptor_x, out->descriptor_y,
                                             out->descriptor_z, out->descriptor_radius) != 0u) {
    // Counter is a ShieldDesc with x221B_b1 set, not a BODY interception. Keep it distinct from
    // common Guard because Fighter_ProcessHit dispatches shield_hit_cb instead of GuardSetOff.
    // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_Anim
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077688}
    out->kind = MSL_ITEM_FIGHTER_CONTACT_COUNTER;
    return out->kind;
  }

  const size_t flags_221b_i =
      d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  if ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_SHIELDABLE) != 0u &&
      (batch->state.state_flags[flags_221b_i] & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) !=
          0u &&
      batch->state.shield_radius[d_idx] > 0.0f) {
    out->descriptor_x = batch->state.shield_x[d_idx];
    out->descriptor_y = batch->state.shield_y[d_idx];
    out->descriptor_z = batch->state.shield_z[d_idx];
    out->descriptor_radius = batch->state.shield_radius[d_idx];
    if (item_swept_sphere_sphere_intersects_3d(hit->x0, hit->y0, hit->z0, hit->x1, hit->y1, hit->z1,
                                               hit->radius, out->descriptor_x, out->descriptor_y,
                                               out->descriptor_z, out->descriptor_radius) != 0u) {
      out->kind = MSL_ITEM_FIGHTER_CONTACT_SHIELD;
      return out->kind;
    }
  }

  // x1988/x198C status 2 rejects BODY only; descriptors above remain live. Status 1 and disabled
  // individual HurtCapsules still register contact and suppress only Fighter_ProcessHit.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077C60,ftColl_8007925C}
  if (batch->state.hurtbox_state[d_idx] == 2u) {
    return MSL_ITEM_FIGHTER_CONTACT_NONE;
  }
  for (uint8_t cap = 0u; cap < batch->state.hurtcap_count[d_idx]; cap++) {
    const size_t cap_i = idx_hurtcap(bi, defender, cap);
    if ((hit->flags & (uint32_t)MSL_ITEM_CONTACT_GRABBABLE_ONLY) != 0u &&
        batch->state.hurtcap_is_grabbable[cap_i] == 0u) {
      continue;
    }
    uint8_t evaluated = 0u;
    float overlap = 0.0f;
    uint8_t height = 0u;
    if (item_body_lbcoll_matrix_radius_overlap(batch, bi, defender, hit->x0, hit->y0, hit->x1,
                                               hit->y1, hit->radius, (int)cap, &height, &overlap,
                                               &evaluated, 0u) == 0u) {
      continue;
    }
    out->hurtcap = (int8_t)cap;
    out->hurt_height = height > 2u ? 1u : height;
    out->hurt_status = batch->state.script_hurtcap_state[cap_i];
    out->body_overlap = overlap;
    out->body_exact_evaluated = evaluated;
    out->kind = MSL_ITEM_FIGHTER_CONTACT_BODY;
    return out->kind;
  }
  return MSL_ITEM_FIGHTER_CONTACT_NONE;
}

uint8_t item_hitcapsule_apply_fighter_hitbox_contact(MslBatch* batch, int bi, int item_slot,
                                                     int defender,
                                                     const MslItemHitCapsulePacket* hit,
                                                     const MslItemFighterContact* contact) {
  if (batch == NULL || hit == NULL || contact == NULL || bi < 0 || bi >= batch->batch_size ||
      item_slot < 0 || item_slot >= MSL_MAX_ITEMS || defender < 0 ||
      defender >= (int)batch->config.num_players ||
      contact->kind != MSL_ITEM_FIGHTER_CONTACT_CLANK || contact->fighter_hitbox < 0 ||
      contact->fighter_hitbox >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  const size_t f_idx = msl_idx_player(bi, defender);
  const int fighter_hb = contact->fighter_hitbox;
  const size_t hi = idx_hitbox(bi, defender, fighter_hb);
  const int item_damage = combat_get_env_dmg(hit->damage);
  const int fighter_damage = combat_hitbox_collision_env_damage(batch, f_idx, hi);
  const int threshold = common->clank_damage_diff_threshold;
  uint8_t result = 0u;

  // ftColl_80077970 resolves both sides independently. A close-damage contact can therefore
  // install the fighter's deal-hitlag packet and the item's clank/damage packet together; a
  // sufficiently stronger side installs only the opposite packet. Keep that common decision out
  // of article callbacks so every extracted x40_b0 item HitCapsule uses the same victims lifetime.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077970,inlineItemA0,inlineItemA1}
  if (fighter_damage - threshold < item_damage) {
    const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hi]);
    const uint8_t rehit = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hi]);
    combat_apply_deal_hitlag_hitbox_damage(batch, f_idx, hi);
    hitlist_register_fighter_group_item(batch, bi, defender, group, item_slot,
                                        batch->state.item_spawn_id[ii],
                                        (int)MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, rehit);
    result |= (uint8_t)MSL_ITEM_HITBOX_CONTACT_FIGHTER_RECEIVED;
  }
  if (item_damage - threshold < fighter_damage) {
    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, (int)hit->hitbox_id, defender,
                                         batch->state.instance_id[f_idx],
                                         (int)MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, 0);
    result |= (uint8_t)MSL_ITEM_HITBOX_CONTACT_ITEM_RECEIVED;
  }
  return result;
}

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

uint8_t item_body_lbcoll_matrix_radius_overlap(const MslBatch* batch, int bi, int defender,
                                               float sx0, float sy0, float sx1, float sy1, float sr,
                                               int cap_i, uint8_t* out_hurt_height,
                                               float* out_overlap_amount, uint8_t* out_evaluated,
                                               uint8_t flatten_hurt_z) {
  const uint8_t overlap = combat_hurtcap_segment_overlap_lbcoll(
      batch, bi, defender, cap_i, sx0, sy0, 0.0f, sx1, sy1, 0.0f, sr, flatten_hurt_z,
      out_overlap_amount, out_evaluated);
  if (overlap != 0u && out_hurt_height != NULL) {
    *out_hurt_height = batch->state.hurtcap_height[idx_hurtcap(bi, defender, cap_i)];
  }
  return overlap;
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
