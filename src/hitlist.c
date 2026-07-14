#include "hitlist.h"

#include <string.h>

#include "batch_internal.h"

static inline size_t fighter_capsule_index(int bi, int player, int hitbox_id) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)player) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hitbox_id;
}

static inline size_t item_capsule_index(int bi, int item_slot, int hitbox_id) {
  return ((size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)item_slot) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hitbox_id;
}

static inline uint8_t fighter_key(uint8_t port) {
  return msl_hitlist_victim_pack((uint8_t)MSL_HITLIST_VICTIM_KIND_FIGHTER, port);
}

static inline uint8_t item_key(uint8_t slot) {
  return msl_hitlist_victim_pack((uint8_t)MSL_HITLIST_VICTIM_KIND_ITEM, slot);
}

static inline uint8_t entry_is_empty(const MslHitlistVictimEntry* entry) {
  return msl_hitlist_victim_is_empty(entry->kind_slot);
}

static inline void entry_clear(MslHitlistVictimEntry* entry) {
  *entry = (MslHitlistVictimEntry){.kind_slot = 0xFFu};
}

void hitlist_capsule_clear(MslHitlistCapsule* capsule) {
  if (capsule == NULL) {
    return;
  }
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    entry_clear(&capsule->victims_1[i]);
    entry_clear(&capsule->victims_2[i]);
  }
  capsule->ring_1 = 0u;
  capsule->ring_2 = 0u;
}

void hitlist_capsule_copy(const MslHitlistCapsule* src, MslHitlistCapsule* dst) {
  if (src != NULL && dst != NULL) {
    memcpy(dst, src, sizeof(*dst));
  }
}

static inline uint8_t type_refreshes_victims_1(int type) {
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  switch (type) {
    case (int)MSL_LBCOLL_INSERT_TODO_2:
    case (int)MSL_LBCOLL_INSERT_IT_ITEM_CLANK_REFRESH:
    case (int)MSL_LBCOLL_INSERT_TODO_5:
    case (int)MSL_LBCOLL_INSERT_TODO_7:
    case (int)MSL_LBCOLL_INSERT_IT_ITEM_DAMAGE_REFRESH:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t type_refreshes_victims_2(int type) {
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820
  switch (type) {
    case (int)MSL_LBCOLL_INSERT_TODO_2:
    case (int)MSL_LBCOLL_INSERT_TODO_5:
    case (int)MSL_LBCOLL_INSERT_TODO_7:
    case (int)MSL_LBCOLL_INSERT_IT_ITEM_DAMAGE_REFRESH:
      return 1u;
    default:
      return 0u;
  }
}

static uint8_t insert_entry(MslHitlistVictimEntry* entries, uint8_t* ring, int type,
                            const MslHitlistVictimEntry* key, uint8_t rehit_frames,
                            uint8_t refresh) {
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* entry = &entries[i];
    if (entry_is_empty(entry) || entry->kind_slot != key->kind_slot) {
      continue;
    }
    if (msl_hitlist_victim_kind(entry->kind_slot) == (uint8_t)MSL_HITLIST_VICTIM_KIND_ITEM &&
        entry->id32 != key->id32) {
      continue;
    }
    if (msl_hitlist_victim_kind(entry->kind_slot) == (uint8_t)MSL_HITLIST_VICTIM_KIND_FIGHTER) {
      entry->id16 = key->id16;
    }
    if (refresh != 0u) {
      entry->cd = rehit_frames;
    }
    return 0u;
  }

  size_t insert = (size_t)MSL_HITLIST_VICTIM_CAP;
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    if (entry_is_empty(&entries[i])) {
      insert = i;
      break;
    }
  }
  if (insert == (size_t)MSL_HITLIST_VICTIM_CAP) {
    insert = (size_t)*ring;
    *ring = (uint8_t)((*ring + 1u) % (uint8_t)MSL_HITLIST_VICTIM_CAP);
  }
  entries[insert] = *key;
  entries[insert].cd = refresh != 0u ? rehit_frames : 0u;
  (void)type;
  return 1u;
}

static uint8_t insert_victims_1(MslHitlistCapsule* capsule, int type,
                                const MslHitlistVictimEntry* key, uint8_t rehit_frames) {
  return insert_entry(capsule->victims_1, &capsule->ring_1, type, key, rehit_frames,
                      type_refreshes_victims_1(type));
}

static uint8_t insert_victims_2(MslHitlistCapsule* capsule, int type,
                                const MslHitlistVictimEntry* key, uint8_t rehit_frames) {
  return insert_entry(capsule->victims_2, &capsule->ring_2, type, key, rehit_frames,
                      type_refreshes_victims_2(type));
}

static uint8_t find_fighter(MslBatch* batch, int bi, MslHitlistVictimEntry* entries, int victim,
                            uint16_t victim_iid) {
  const uint8_t key = fighter_key((uint8_t)victim);
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* entry = &entries[i];
    if (entry_is_empty(entry) || entry->kind_slot != key) {
      continue;
    }
    if (entry->id16 != victim_iid) {
      const size_t victim_idx = msl_idx_player(bi, victim);
      if (hitlist_victim_pointer_may_change(batch->state.stocks[victim_idx],
                                            batch->state.action_id[victim_idx])) {
        entry_clear(entry);
        return 0u;
      }
      // The game keys fighter victims by object pointer. A Slippi instance id may change while
      // that object remains live, so refresh the replay-visible proxy without clearing the latch.
      entry->id16 = victim_iid;
    }
    return 1u;
  }
  return 0u;
}

static uint8_t valid_fighter_query(const MslBatch* batch, int bi, int attacker, int hitbox_id,
                                   int victim) {
  return batch != NULL && bi >= 0 && bi < batch->batch_size && attacker >= 0 &&
         attacker < (int)batch->config.num_players && hitbox_id >= 0 &&
         hitbox_id < MSL_MAX_HITBOXES && victim >= 0 && victim < (int)batch->config.num_players;
}

uint8_t hitlist_allows_fighter_live_collision(MslBatch* batch, int bi, int attacker, int hitbox_id,
                                              int victim, uint16_t victim_iid) {
  if (!valid_fighter_query(batch, bi, attacker, hitbox_id, victim)) {
    return 0u;
  }
  MslHitlistCapsule* capsule =
      &batch->state.fighter_hitlist[fighter_capsule_index(bi, attacker, hitbox_id)];
  return find_fighter(batch, bi, capsule->victims_1, victim, victim_iid) == 0u ? 1u : 0u;
}

uint8_t hitlist_allows_fighter(MslBatch* batch, int bi, int attacker, int hitbox_id, int victim,
                               uint16_t victim_iid) {
  return hitlist_allows_fighter_live_collision(batch, bi, attacker, hitbox_id, victim, victim_iid);
}

uint8_t hitlist_allows_fighter_v2(MslBatch* batch, int bi, int attacker, int hitbox_id, int victim,
                                  uint16_t victim_iid) {
  if (!valid_fighter_query(batch, bi, attacker, hitbox_id, victim)) {
    return 0u;
  }
  MslHitlistCapsule* capsule =
      &batch->state.fighter_hitlist[fighter_capsule_index(bi, attacker, hitbox_id)];
  return find_fighter(batch, bi, capsule->victims_2, victim, victim_iid) == 0u ? 1u : 0u;
}

uint8_t hitlist_allows_fighter_item(MslBatch* batch, int bi, int attacker, int hitbox_id,
                                    int item_slot, uint32_t item_spawn_id) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || hitbox_id < 0 ||
      hitbox_id >= MSL_MAX_HITBOXES || item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return 0u;
  }
  const uint8_t key = item_key((uint8_t)item_slot);
  MslHitlistCapsule* capsule =
      &batch->state.fighter_hitlist[fighter_capsule_index(bi, attacker, hitbox_id)];
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    const MslHitlistVictimEntry* entry = &capsule->victims_1[i];
    if (!entry_is_empty(entry) && entry->kind_slot == key && entry->id32 == item_spawn_id) {
      return 0u;
    }
  }
  return 1u;
}

static void register_fighter(MslHitlistCapsule* capsule, int victim, uint16_t victim_iid, int type,
                             uint8_t rehit_frames, uint8_t victims_2) {
  const MslHitlistVictimEntry key = {
      .id16 = victim_iid,
      .kind_slot = fighter_key((uint8_t)victim),
  };
  if (victims_2 != 0u) {
    (void)insert_victims_2(capsule, type, &key, rehit_frames);
  } else {
    (void)insert_victims_1(capsule, type, &key, rehit_frames);
  }
}

static void register_fighter_group_impl(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                        int victim, uint16_t victim_iid, int type,
                                        uint8_t rehit_frames, uint8_t victims_2) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || victim < 0 ||
      victim >= (int)batch->config.num_players) {
    return;
  }
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hi = fighter_capsule_index(bi, attacker, hb);
    if (batch->state.hitbox_enabled[hi] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hi]) != hit_group) {
      continue;
    }
    register_fighter(&batch->state.fighter_hitlist[hi], victim, victim_iid, type, rehit_frames,
                     victims_2);
    batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
  }
}

void hitlist_register_fighter_group(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                    int victim, uint16_t victim_iid, int type,
                                    uint8_t rehit_frames) {
  register_fighter_group_impl(batch, bi, attacker, hit_group, victim, victim_iid, type,
                              rehit_frames, 0u);
}

void hitlist_register_fighter_group_v2(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                       int victim, uint16_t victim_iid, int type,
                                       uint8_t rehit_frames) {
  register_fighter_group_impl(batch, bi, attacker, hit_group, victim, victim_iid, type,
                              rehit_frames, 1u);
}

void hitlist_register_fighter_hitbox(MslBatch* batch, int bi, int attacker, int hitbox_id,
                                     int victim, uint16_t victim_iid, int type,
                                     uint8_t rehit_frames) {
  if (!valid_fighter_query(batch, bi, attacker, hitbox_id, victim)) {
    return;
  }
  const size_t hi = fighter_capsule_index(bi, attacker, hitbox_id);
  register_fighter(&batch->state.fighter_hitlist[hi], victim, victim_iid, type, rehit_frames, 0u);
  batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
}

void hitlist_register_fighter_hitbox_v2(MslBatch* batch, int bi, int attacker, int hitbox_id,
                                        int victim, uint16_t victim_iid, int type,
                                        uint8_t rehit_frames) {
  if (!valid_fighter_query(batch, bi, attacker, hitbox_id, victim)) {
    return;
  }
  const size_t hi = fighter_capsule_index(bi, attacker, hitbox_id);
  register_fighter(&batch->state.fighter_hitlist[hi], victim, victim_iid, type, rehit_frames, 1u);
  batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
}

void hitlist_register_fighter_group_item(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                         int item_slot, uint32_t item_spawn_id, int type,
                                         uint8_t rehit_frames) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return;
  }
  const MslHitlistVictimEntry key = {
      .id32 = item_spawn_id,
      .kind_slot = item_key((uint8_t)item_slot),
  };
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hi = fighter_capsule_index(bi, attacker, hb);
    if (batch->state.hitbox_enabled[hi] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hi]) != hit_group) {
      continue;
    }
    (void)insert_victims_1(&batch->state.fighter_hitlist[hi], type, &key, rehit_frames);
    batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
  }
}

uint8_t hitlist_allows_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                           int victim, uint16_t victim_iid) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || item_slot < 0 ||
      item_slot >= MSL_MAX_ITEMS || hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES || victim < 0 ||
      victim >= (int)batch->config.num_players) {
    return 0u;
  }
  MslHitlistCapsule* capsule =
      &batch->state.item_hitlist[item_capsule_index(bi, item_slot, hitbox_id)];
  return find_fighter(batch, bi, capsule->victims_1, victim, victim_iid) == 0u ? 1u : 0u;
}

void hitlist_register_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                          int victim, uint16_t victim_iid, int type,
                                          uint8_t rehit_frames) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || item_slot < 0 ||
      item_slot >= MSL_MAX_ITEMS || hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES || victim < 0 ||
      victim >= (int)batch->config.num_players) {
    return;
  }
  register_fighter(&batch->state.item_hitlist[item_capsule_index(bi, item_slot, hitbox_id)], victim,
                   victim_iid, type, rehit_frames, 0u);
}

void hitlist_register_item_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                   uint16_t victim_iid, int type, uint8_t rehit_frames) {
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, hb, victim, victim_iid, type,
                                         rehit_frames);
  }
}

void hitlist_seed_init_fighter_hitbox_from_group(MslBatch* batch, int bi, int attacker,
                                                 int hitbox_id, uint8_t hit_group) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || hitbox_id < 0 ||
      hitbox_id >= MSL_MAX_HITBOXES) {
    return;
  }
  const size_t hi = fighter_capsule_index(bi, attacker, hitbox_id);
  MslHitlistCapsule* capsule = &batch->state.fighter_hitlist[hi];
  hitlist_capsule_clear(capsule);

  const size_t valid_i = hi;
  const uint8_t exact = batch->state.combat_hitlist_hb_valid[valid_i] != 0u ? 1u : 0u;
  const size_t hb_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  for (int victim = 0; victim < (int)batch->config.num_players; victim++) {
    const size_t seed_i =
        exact != 0u
            ? hb_base + (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hitbox_id) *
                             (size_t)MSL_MAX_PLAYERS +
                         (size_t)victim)
            : group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                                (size_t)MSL_MAX_PLAYERS +
                            (size_t)victim);
    const uint16_t cd = exact != 0u ? batch->state.combat_hitlist_hb_cd[seed_i]
                                    : batch->state.combat_hitlist_cd[seed_i];
    if (cd == 0u) {
      continue;
    }
    const uint16_t stored_iid = exact != 0u ? batch->state.combat_hitlist_hb_victim_iid[seed_i]
                                            : batch->state.combat_hitlist_victim_iid[seed_i];
    const MslHitlistVictimEntry entry = {
        .id16 =
            stored_iid != 0u ? stored_iid : batch->state.instance_id[msl_idx_player(bi, victim)],
        .kind_slot = fighter_key((uint8_t)victim),
        .cd = cd == UINT16_MAX ? 0u : (uint8_t)cd,
    };
    size_t slot = 0u;
    while (slot < (size_t)MSL_HITLIST_VICTIM_CAP && !entry_is_empty(&capsule->victims_1[slot])) {
      slot++;
    }
    if (slot < (size_t)MSL_HITLIST_VICTIM_CAP) {
      capsule->victims_1[slot] = entry;
    }
  }
  batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
}

static void tick_capsule(MslHitlistCapsule* capsule) {
  for (size_t list = 0; list < 2u; list++) {
    MslHitlistVictimEntry* entries = list == 0u ? capsule->victims_1 : capsule->victims_2;
    for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
      if (!entry_is_empty(&entries[i]) && entries[i].cd != 0u && --entries[i].cd == 0u) {
        entry_clear(&entries[i]);
      }
    }
  }
}

void hitlist_tick(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int attacker = 0; attacker < (int)batch->config.num_players; attacker++) {
      if (batch->state.hitlag[msl_idx_player(bi, attacker)] != 0u) {
        continue;
      }
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t hi = fighter_capsule_index(bi, attacker, hb);
        if (batch->state.hitbox_enabled[hi] != 0u) {
          tick_capsule(&batch->state.fighter_hitlist[hi]);
        }
      }
    }
    for (int item = 0; item < MSL_MAX_ITEMS; item++) {
      if (batch->state.item_exists[msl_idx_item(bi, item)] == 0u) {
        continue;
      }
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        tick_capsule(&batch->state.item_hitlist[item_capsule_index(bi, item, hb)]);
      }
    }
  }
}

void hitlist_debug_clear_fighter_attacker(MslBatch* batch, int bi, int attacker) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= MSL_MAX_PLAYERS) {
    return;
  }
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hi = fighter_capsule_index(bi, attacker, hb);
    hitlist_capsule_clear(&batch->state.fighter_hitlist[hi]);
    batch->state.fighter_hitlist_init_gen[hi] = batch->state.hitlist_reseed_gen[bi];
  }
}

void hitlist_debug_insert_item_victims1(MslHitlistCapsule* capsule, int type, uint32_t spawn_id,
                                        uint8_t rehit_frames) {
  if (capsule == NULL) {
    return;
  }
  const MslHitlistVictimEntry key = {
      .id32 = spawn_id,
      .kind_slot = item_key(0u),
  };
  (void)insert_victims_1(capsule, type, &key, rehit_frames);
}
