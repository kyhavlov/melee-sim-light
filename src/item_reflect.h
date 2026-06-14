#pragma once

#include <stdint.h>

#include "batch_internal.h"
#include "staling.h"

enum {
  MSL_ITEM_REFLECT_NO_PORT = 0xFFu,
  MSL_ITEM_REFLECT_KNOWN_NONE_PORT = 0xFEu,
};

static inline void msl_item_reflect_clear_runtime_lanes(MslBatch* batch, size_t item_idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp item defaults:
  // - reflected damage multiplier starts as identity (`item->xC6C`),
  // - no pending ReflectDesc owner snapshot is installed until ftColl_80077464 writes it.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  batch->state.item_reflect_damage_mul[item_idx] = 1.0f;
  batch->state.item_reflect_body_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_reflect_body_attack_id[item_idx] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_reflect_body_attack_instance[item_idx] = 0u;
  batch->state.item_reflect_body_damage_valid[item_idx] = 0u;
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_pending_reflect_instance_id[item_idx] = 0u;
}

static inline void msl_item_reflect_clear_seed_lanes(MslBatch* batch, size_t item_idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_reflect_transfer_seed_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_reflect_transfer_seed_iid[item_idx] = 0u;
  batch->state.item_shield_bounce_seed_valid[item_idx] = 0u;
  batch->state.item_shield_bounce_seed_vel_x[item_idx] = 0.0f;
  batch->state.item_shield_bounce_seed_vel_y[item_idx] = 0.0f;
}

static inline void msl_item_reflect_clear_all_lanes(MslBatch* batch, size_t item_idx) {
  msl_item_reflect_clear_runtime_lanes(batch, item_idx);
  msl_item_reflect_clear_seed_lanes(batch, item_idx);
}

static inline void msl_item_reflect_set_damage_mul(MslBatch* batch, size_t item_idx,
                                                   float damage_mul) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_reflect_damage_mul[item_idx] = (damage_mul > 0.0f) ? damage_mul : 1.0f;
  batch->state.item_reflect_body_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_reflect_body_attack_id[item_idx] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_reflect_body_attack_instance[item_idx] = 0u;
  batch->state.item_reflect_body_damage_valid[item_idx] = 0u;
  // Item_80269F14 rebuilds the reflected item's HitCapsule damage through it_80272460. Clear the
  // pre-reflect stale scalar so the rebuilt owner path can use the current owner's stale table
  // instead of the projectile's original spawn-time lane.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  batch->state.item_stale_damage_valid[item_idx] = 0u;
  batch->state.item_stale_damage_mul[item_idx] = 1.0f;
}

static inline float msl_item_reflect_damage_lane(const MslBatch* batch, size_t item_idx,
                                                 float base_damage) {
  if (batch == NULL || !(base_damage > 0.0f)) {
    return base_damage;
  }
  // Reflected item damage is item-owned:
  // - ftColl_80077464 snapshots ReflectDesc.x18 into `item->xC6C`,
  // - Item_80269F14 / it_80272460 consume `(u32)(hit.damage * item->xC6C + 0.99f)`.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  float mul = batch->state.item_reflect_damage_mul[item_idx];
  if (!(mul > 0.0f)) {
    mul = 1.0f;
  }
  const float tmp = base_damage * mul + 0.99f;
  uint32_t dmg_i = 0u;
  if (tmp > 0.0f) {
    dmg_i = (uint32_t)tmp;
  }
  if (dmg_i == 0u) {
    dmg_i = 1u;
  }
  return (float)dmg_i;
}

static inline uint8_t msl_item_reflect_owner_valid(const MslBatch* batch, int owner_port) {
  return (batch != NULL && owner_port >= 0 && owner_port < (int)batch->config.num_players) ? 1u
                                                                                           : 0u;
}

static inline uint8_t msl_item_reflect_has_transfer_provenance(const MslBatch* batch,
                                                               size_t item_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.item_reflect_damage_mul[item_idx] != 1.0f) {
    return 1u;
  }
  if (batch->state.item_pending_reflect_owner_port[item_idx] != (uint8_t)MSL_ITEM_REFLECT_NO_PORT) {
    return 1u;
  }
  if (batch->state.item_reflect_body_owner_port[item_idx] != (uint8_t)MSL_ITEM_REFLECT_NO_PORT) {
    return 1u;
  }
  const uint8_t seed_port = batch->state.item_reflect_transfer_seed_port[item_idx];
  return (seed_port != (uint8_t)MSL_ITEM_REFLECT_NO_PORT &&
          seed_port != (uint8_t)MSL_ITEM_REFLECT_KNOWN_NONE_PORT)
             ? 1u
             : 0u;
}

static inline void msl_item_reflect_commit_owner_snapshot(MslBatch* batch, size_t item_idx,
                                                          int reflector_port) {
  if (!msl_item_reflect_owner_valid(batch, reflector_port)) {
    return;
  }
  const int bi = (int)(item_idx / (size_t)MSL_MAX_ITEMS);
  const size_t reflector_idx = msl_idx_player(bi, reflector_port);
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_pending_reflect_instance_id[item_idx] = 0u;
  batch->state.item_owner[item_idx] = (int8_t)reflector_port;
  // Slippi item.instance_id is `item->xDA8_short`; Item_80269F14 rewrites it from the reflecting
  // fighter snapshot (`fp->x2074.x2088`).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  batch->state.item_instance_id[item_idx] = batch->state.instance_id[reflector_idx];
}

static inline void msl_item_reflect_stage_pending_owner(MslBatch* batch, size_t item_idx,
                                                        int reflector_port) {
  if (!msl_item_reflect_owner_valid(batch, reflector_port)) {
    return;
  }
  const int bi = (int)(item_idx / (size_t)MSL_MAX_ITEMS);
  const size_t reflector_idx = msl_idx_player(bi, reflector_port);
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)reflector_port;
  batch->state.item_pending_reflect_instance_id[item_idx] = batch->state.instance_id[reflector_idx];
}

static inline void msl_item_reflect_commit_owner_snapshot_defer_speed(MslBatch* batch,
                                                                      size_t item_idx,
                                                                      int reflector_port) {
  if (!msl_item_reflect_owner_valid(batch, reflector_port)) {
    return;
  }
  const int bi = (int)(item_idx / (size_t)MSL_MAX_ITEMS);
  const size_t reflector_idx = msl_idx_player(bi, reflector_port);
  batch->state.item_owner[item_idx] = (int8_t)reflector_port;
  batch->state.item_instance_id[item_idx] = batch->state.instance_id[reflector_idx];
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)reflector_port;
  batch->state.item_pending_reflect_instance_id[item_idx] = batch->state.instance_id[reflector_idx];
}

static inline void msl_item_reflect_apply_direction_lane(MslBatch* batch, size_t item_idx) {
  if (batch == NULL) {
    return;
  }
  // itFoxLaser_Logic94_Reflected flips the visual facing/angle when the reflected callback runs.
  // The velocity callback may be consumed later by Item_80269F14.
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
  // refs/melee/src/melee/it/item.c::Item_80269F14
  const float reflect_vx = -batch->state.item_vel_x[item_idx];
  if (reflect_vx > 0.0f) {
    batch->state.item_direction[item_idx] = 1.0f;
  } else if (reflect_vx < 0.0f) {
    batch->state.item_direction[item_idx] = -1.0f;
  } else {
    const float cur_dir = batch->state.item_direction[item_idx];
    batch->state.item_direction[item_idx] = (cur_dir >= 0.0f) ? -1.0f : 1.0f;
  }
}

static inline void msl_item_reflect_stage_snapshot_defer_velocity(MslBatch* batch, size_t item_idx,
                                                                  int reflector_port,
                                                                  float damage_mul) {
  if (batch == NULL) {
    return;
  }
  // Reflect snapshot ownership:
  // - ftColl_80077464 writes owner/xDA8_short and multipliers to item snapshot state,
  // - Item_80269F14 consumes the snapshot in the item callback phase.
  // Runtime keeps the owner snapshot pending and applies the laser velocity callback when the item
  // callback owner runs.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  msl_item_reflect_stage_pending_owner(batch, item_idx, reflector_port);
  msl_item_reflect_apply_direction_lane(batch, item_idx);
  msl_item_reflect_set_damage_mul(batch, item_idx, damage_mul);
}

static inline uint8_t msl_item_reflect_should_flip_direction_now(const MslBatch* batch,
                                                                 size_t item_idx,
                                                                 size_t reflector_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const float vx = batch->state.item_vel_x[item_idx];
  if (!(vx > 0.0f || vx < 0.0f)) {
    return 0u;
  }
  // Visual reflect orientation is item-callback owned and can lag owner/xDA8 transfer after the
  // item has crossed the fighter origin. Keep same-frame direction flips only on the still-
  // approaching side.
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
  // refs/melee/src/melee/it/item.c::Item_80269F14
  return (((batch->state.item_pos_x[item_idx] - batch->state.pos_x[reflector_idx]) * vx) < 0.0f)
             ? 1u
             : 0u;
}

static inline void msl_item_reflect_apply_pending_laser_callback(MslBatch* batch, size_t item_idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t pending_owner = batch->state.item_pending_reflect_owner_port[item_idx];
  const uint16_t pending_iid = batch->state.item_pending_reflect_instance_id[item_idx];
  const uint8_t pending_same_owner_speed =
      (pending_owner < (uint8_t)batch->config.num_players && pending_iid != 0u &&
       batch->state.item_owner[item_idx] == (int8_t)pending_owner &&
       batch->state.item_instance_id[item_idx] == pending_iid)
          ? 1u
          : 0u;
  if (pending_owner < (uint8_t)batch->config.num_players) {
    batch->state.item_owner[item_idx] = (int8_t)pending_owner;
    if (pending_iid != 0u) {
      batch->state.item_instance_id[item_idx] = pending_iid;
    }
  }
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_pending_reflect_instance_id[item_idx] = 0u;

  const float vx = batch->state.item_vel_x[item_idx];
  if (!(vx > 0.0f || vx < 0.0f)) {
    return;
  }
  const float dir = batch->state.item_direction[item_idx];
  const uint8_t pending_reflect =
      (pending_same_owner_speed || (vx > 0.0f && dir < 0.0f) || (vx < 0.0f && dir > 0.0f)) ? 1u
                                                                                           : 0u;
  if (!pending_reflect) {
    return;
  }

  // Fox/Falco laser reflected callback:
  // - Item_80269F14 consumes the reflected callback before rebuilding HitCapsule damage,
  // - itFoxLaser_Logic94_Reflected flips facing and adds pi to angle, but does not multiply the
  //   stored laser speed by ReflectDesc.x1C. The next laser Anim callback rebuilds velocity with
  //   unchanged speed magnitude.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
  const float new_vx = -vx;
  const float new_vy = -batch->state.item_vel_y[item_idx];
  batch->state.item_vel_x[item_idx] = new_vx;
  batch->state.item_vel_y[item_idx] = new_vy;
  batch->state.item_direction[item_idx] = (new_vx >= 0.0f) ? 1.0f : -1.0f;
}

static inline void msl_item_reflect_apply_immediate_transfer(MslBatch* batch, size_t item_idx,
                                                             size_t reflector_idx,
                                                             int reflector_port, float damage_mul,
                                                             float speed_mul) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_misc2[item_idx] = 0u;
  batch->state.item_misc3[item_idx] = 0u;
  batch->state.item_owner[item_idx] = (int8_t)reflector_port;
  batch->state.item_instance_id[item_idx] = batch->state.instance_id[reflector_idx];
  msl_item_reflect_set_damage_mul(batch, item_idx, damage_mul);
  const uint16_t reflector_attack_id = batch->state.attack_id[reflector_idx];
  const uint16_t reflector_attack_instance = batch->state.attack_instance[reflector_idx];
  if (reflector_attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT && reflector_attack_instance != 0u) {
    uint8_t prior_stale_source_seen = 0u;
    const size_t stale_base = reflector_idx * (size_t)MSL_STALE_QUEUE_SIZE;
    for (int i = 0; i < MSL_STALE_QUEUE_SIZE; i++) {
      if (batch->state.stale_move_id[stale_base + (size_t)i] == reflector_attack_id) {
        prior_stale_source_seen = 1u;
        break;
      }
    }
    // Reflected BODY stale owner:
    // ftColl_80077464 transfers the item to the reflector, but Slippi keeps item->xD88/xD8C
    // public fields spawn-latched. Runtime carries the reflector stale owner until the next BODY
    // hit consumes it. The first reflected hit records this owner only after its public item damage;
    // later reflected hits with a prior same-move stale entry may use the hidden owner for damage.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    // refs/melee/src/melee/it/item.c::Item_80269F14
    // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    batch->state.item_reflect_body_owner_port[item_idx] = (uint8_t)reflector_port;
    batch->state.item_reflect_body_attack_id[item_idx] = reflector_attack_id;
    batch->state.item_reflect_body_attack_instance[item_idx] = reflector_attack_instance;
    batch->state.item_reflect_body_damage_valid[item_idx] = prior_stale_source_seen;
  }
  const float mul = (speed_mul > 0.0f) ? speed_mul : 1.0f;
  const float new_vx = -batch->state.item_vel_x[item_idx] * mul;
  const float new_vy = -batch->state.item_vel_y[item_idx] * mul;
  batch->state.item_vel_x[item_idx] = new_vx;
  batch->state.item_vel_y[item_idx] = new_vy;
  batch->state.item_direction[item_idx] = (new_vx >= 0.0f) ? 1.0f : -1.0f;
}

static inline void msl_item_reflect_apply_seeded_transfer(MslBatch* batch, size_t item_idx,
                                                          uint8_t seed_port, uint16_t seed_iid,
                                                          float damage_mul) {
  if (batch == NULL || seed_port >= (uint8_t)batch->config.num_players || seed_iid == 0u) {
    return;
  }
  const int bi = (int)(item_idx / (size_t)MSL_MAX_ITEMS);
  const size_t reflector_idx = msl_idx_player(bi, (int)seed_port);
  if (batch->state.item_owner[item_idx] != (int8_t)seed_port &&
      msl_item_reflect_should_flip_direction_now(batch, item_idx, reflector_idx)) {
    const float vx = batch->state.item_vel_x[item_idx];
    if (vx > 0.0f) {
      batch->state.item_direction[item_idx] = -1.0f;
    } else if (vx < 0.0f) {
      batch->state.item_direction[item_idx] = 1.0f;
    }
  }
  batch->state.item_owner[item_idx] = (int8_t)seed_port;
  batch->state.item_instance_id[item_idx] = seed_iid;
  msl_item_reflect_set_damage_mul(batch, item_idx, damage_mul);
  // Stage (do not clear) the pending reflected-callback lanes: ftColl_80077464 only writes the
  // reflect snapshot; Item_80269F14 consumes it on the item's next callback pass, where
  // itFoxLaser_Logic94_Reflected flips facing and reverses the velocity unconditionally. The
  // still-approaching direction heuristic above only times the same-frame visual facing lane; an
  // already-crossed projectile must still get the next-pass velocity reversal (MAJ rec294: the
  // powershielded laser crosses the reflector root by 0.63 before the transfer applies, and the
  // replay's item record flips velocity exactly one item pass later).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
  batch->state.item_pending_reflect_owner_port[item_idx] = (uint8_t)seed_port;
  batch->state.item_pending_reflect_instance_id[item_idx] = seed_iid;
}
