#include "hitlist.h"

#include <string.h>

#include "action_ids.h"
#include "batch_internal.h"

static inline uint8_t hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id) {
  // Decomp hitlists store a raw victim pointer (HitVictim.victim) and use pointer equality to
  // decide "already hit" vs "new victim":
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  //
  // This simulator does not currently have a stable pointer identity in the seed schema, so it
  // approximates "pointer changed" using Slippi-visible state:
  // - stocks==0 (dead), or
  // - in a death/respawn motion state.
  //
  // NOTE: This boundary is an approximation; it is not guaranteed to match every engine object
  // lifetime transition, but it is the intended substitute for "victim pointer changed" with the
  // current seed contract.
  if (stocks == 0) {
    return 1u;
  }
  return (action_id == (uint16_t)MSL_ACT_DEAD_DOWN || action_id == (uint16_t)MSL_ACT_DEAD_LEFT ||
          action_id == (uint16_t)MSL_ACT_DEAD_RIGHT ||
          action_id == (uint16_t)MSL_ACT_DEAD_UP_STAR || action_id == (uint16_t)MSL_ACT_REBIRTH ||
          action_id == (uint16_t)MSL_ACT_REBIRTH_WAIT)
             ? 1u
             : 0u;
}

static inline size_t idx_fighter_hitlist(int bi, int p, int hb_id) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_id;
}

static inline size_t idx_item_hitlist(int bi, int item_slot) {
  return (size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)item_slot;
}

static inline uint8_t entry_is_empty(const MslHitlistVictimEntry* e) {
  return msl_hitlist_victim_is_empty(e->kind_slot);
}

static inline void entry_clear(MslHitlistVictimEntry* e) {
  e->id32 = 0;
  e->id16 = 0;
  e->kind_slot = 0xFFu;
  e->cd = 0;
}

void hitlist_capsule_clear(MslHitlistCapsule* hit) {
  if (hit == NULL) {
    return;
  }
  // lbColl_80008440: clear both lists and reset ring indices.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    entry_clear(&hit->victims_1[i]);
  }
  hit->ring_1 = 0;
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    entry_clear(&hit->victims_2[i]);
  }
  hit->ring_2 = 0;
}

void hitlist_capsule_copy(const MslHitlistCapsule* src, MslHitlistCapsule* dst) {
  if (src == NULL || dst == NULL) {
    return;
  }
  // lbColl_CopyHitCapsule: copy victims_1, victims_2, and ring indices.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_CopyHitCapsule
  memcpy(dst, src, sizeof(*dst));
}

static inline uint8_t hitlist_type_refreshes_v1(int type) {
  // lbColl_80008688 refresh set:
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

static inline uint8_t hitlist_type_refreshes_v2(int type) {
  // lbColl_80008820 refresh set:
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

static uint8_t hitlist_insert_list(MslHitlistVictimEntry* victims, uint8_t* ring, size_t cap,
                                   int type, const MslHitlistVictimEntry* key, uint8_t cd_set,
                                   uint8_t refresh_set) {
  // Decomp shape: lbColl_80008688 / lbColl_80008820.
  //
  // Returns 1 if victim was newly inserted, 0 if already present.
  // If present and type is in the refresh set, refresh cd to cd_set.
  for (size_t i = 0; i < cap; i++) {
    if (entry_is_empty(&victims[i])) {
      continue;
    }
    if (victims[i].kind_slot != key->kind_slot) {
      continue;
    }
    const uint8_t kind = msl_hitlist_victim_kind(victims[i].kind_slot);
    if (kind == (uint8_t)MSL_HITLIST_VICTIM_KIND_ITEM) {
      if (victims[i].id32 != key->id32) {
        continue;
      }
    }
    // Fighter victim identity uses kind_slot (port) as the pointer proxy; id16 is managed by the
    // caller's boundary logic (rehit suppression should not clear on iid change unless respawning).
    if (refresh_set) {
      victims[i].cd = cd_set;
    }
    return 0u;
  }

  size_t first_empty = cap;
  for (size_t i = 0; i < cap; i++) {
    if (entry_is_empty(&victims[i])) {
      first_empty = i;
      break;
    }
  }

  size_t insert_idx = first_empty;
  if (insert_idx == cap) {
    insert_idx = (size_t)(*ring);
  }

  victims[insert_idx] = *key;
  // Decomp: on insertion, always store the per-victim cooldown from HitCapsule.x40_b4 (cd_set).
  // Refresh behavior (when already present) is type-dependent and handled above.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  victims[insert_idx].cd = cd_set;

  if (first_empty == cap) {
    uint8_t r = (uint8_t)(*ring + 1u);
    if (r >= (uint8_t)cap) {
      r = 0;
    }
    *ring = r;
  }
  (void)type;
  return 1u;
}

static uint8_t hitlist_insert_victims1(MslHitlistCapsule* hit, int type,
                                       const MslHitlistVictimEntry* key, uint8_t rehit_frames) {
  if (hit == NULL || key == NULL) {
    return 0u;
  }
  const uint8_t refresh = hitlist_type_refreshes_v1(type);
  return hitlist_insert_list(hit->victims_1, &hit->ring_1, (size_t)MSL_HITLIST_VICTIM_CAP, type,
                             key, rehit_frames, refresh);
}

static uint8_t __attribute__((unused)) hitlist_insert_victims2(MslHitlistCapsule* hit, int type,
                                                               const MslHitlistVictimEntry* key,
                                                               uint8_t rehit_frames) {
  if (hit == NULL || key == NULL) {
    return 0u;
  }
  const uint8_t refresh = hitlist_type_refreshes_v2(type);
  return hitlist_insert_list(hit->victims_2, &hit->ring_2, (size_t)MSL_HITLIST_VICTIM_CAP, type,
                             key, rehit_frames, refresh);
}

static inline uint8_t hitlist_fighter_key(uint8_t port) {
  return msl_hitlist_victim_pack((uint8_t)MSL_HITLIST_VICTIM_KIND_FIGHTER, port);
}

static inline uint8_t hitlist_item_key(uint8_t slot) {
  return msl_hitlist_victim_pack((uint8_t)MSL_HITLIST_VICTIM_KIND_ITEM, slot);
}

static uint8_t hitlist_capsule_find_fighter_entry(MslBatch* batch, int bi,
                                                  MslHitlistVictimEntry* victims, size_t cap,
                                                  uint8_t victim_port, uint16_t victim_iid,
                                                  size_t* out_index) {
  if (batch == NULL || victims == NULL || out_index == NULL) {
    return 0u;
  }
  const uint8_t key = hitlist_fighter_key(victim_port);
  for (size_t i = 0; i < cap; i++) {
    if (entry_is_empty(&victims[i])) {
      continue;
    }
    if (victims[i].kind_slot != key) {
      continue;
    }
    // Victim identity boundary handling (decomp-faithful, reseed-friendly):
    // - Decomp key is a pointer; we key by port and store instance_id as a proxy.
    // - If instance_id differs, keep suppression unless victim is dead/respawning.
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    const uint16_t stored_iid = victims[i].id16;
    if (stored_iid != victim_iid) {
      const size_t v_idx = msl_idx_player(bi, (int)victim_port);
      const uint8_t v_stocks = batch->state.stocks[v_idx];
      const uint16_t v_act = batch->state.action_id[v_idx];
      if (hitlist_victim_pointer_may_change(v_stocks, v_act)) {
        entry_clear(&victims[i]);
        return 0u;
      }
      // Rebind proxy identity without clearing the suppression latch.
      victims[i].id16 = victim_iid;
    }
    *out_index = i;
    return 1u;
  }
  return 0u;
}

uint8_t hitlist_allows_fighter(MslBatch* batch, int bi, int attacker, int hb_id, int victim,
                               uint16_t victim_iid) {
  if (batch == NULL) {
    return 0u;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return 0u;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return 0u;
  }

  const size_t hb_i = idx_fighter_hitlist(bi, attacker, hb_id);
  MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hb_i];
  size_t found = 0;
  if (hitlist_capsule_find_fighter_entry(batch, bi, hit->victims_1, (size_t)MSL_HITLIST_VICTIM_CAP,
                                         (uint8_t)victim, victim_iid, &found)) {
    return 0u;
  }
  return 1u;
}

void hitlist_register_fighter_group(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                    int victim, uint16_t victim_iid, int type,
                                    uint8_t rehit_frames) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return;
  }

  // Decomp: share insertion across all active HitCapsules with the same hit_group (x4).
  // refs/melee/src/melee/ft/ftcoll.c::inlineB0 and ::ftColl_80076808
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_fighter_key((uint8_t)victim);
  key.id16 = victim_iid;
  for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
    const size_t hb_state_i =
        ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
        (size_t)hb_id;
    if (!batch->state.hitbox_enabled[hb_state_i]) {
      continue;
    }
    const uint8_t g = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_state_i]);
    if (g != hit_group) {
      continue;
    }
    const size_t hl_i = idx_fighter_hitlist(bi, attacker, hb_id);
    MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
    (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
  }
}

uint8_t hitlist_allows_item_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                    uint16_t victim_iid) {
  if (batch == NULL) {
    return 0u;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  if (item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return 0u;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return 0u;
  }

  const size_t ii = idx_item_hitlist(bi, item_slot);
  MslHitlistCapsule* hit = &batch->state.item_hitlist[ii];
  size_t found = 0;
  if (hitlist_capsule_find_fighter_entry(batch, bi, hit->victims_1, (size_t)MSL_HITLIST_VICTIM_CAP,
                                         (uint8_t)victim, victim_iid, &found)) {
    return 0u;
  }
  return 1u;
}

void hitlist_register_item_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                   uint16_t victim_iid, int type, uint8_t rehit_frames) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return;
  }

  const size_t ii = idx_item_hitlist(bi, item_slot);
  MslHitlistCapsule* hit = &batch->state.item_hitlist[ii];
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_fighter_key((uint8_t)victim);
  key.id16 = victim_iid;
  (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
}

void hitlist_seed_init_fighter_hitbox_from_group(MslBatch* batch, int bi, int attacker, int hb_id,
                                                 uint8_t hit_group) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return;
  }
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    hit_group = 0;
  }

  // Seed schema bridge:
  // - Preferred lane: per-(attacker, hitbox, victim_port), which mirrors decomp HitCapsule
  //   ownership and preserves empty lists as authoritative when valid.
  // - Fallback lane: legacy dense per-(attacker, hit_group, victim_port) map for synthetic tests
  //   and older datasets.
  //
  // We materialize a deterministic list ordering (victim port order) into victims_1 and reset ring.
  //
  // NOTE:
  // - seed does not currently carry victims_2; it is cleared here.
  // - The seed maps are seed-only inputs and are not maintained during rollouts (see src/state.h).
  // refs/melee/src/melee/lb/types.h::HitCapsule
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  const size_t hl_i = idx_fighter_hitlist(bi, attacker, hb_id);
  MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
  hitlist_capsule_clear(hit);

  const size_t a = (size_t)attacker;
  const size_t hb = (size_t)hb_id;
  const size_t valid_i = ((size_t)bi * (size_t)MSL_MAX_PLAYERS + a) * (size_t)MSL_MAX_HITBOXES + hb;
  const uint8_t use_hitbox_seed = batch->state.combat_hitlist_hb_valid[valid_i] ? 1u : 0u;

  size_t out_i = 0;
  for (int v = 0; v < (int)batch->config.num_players && out_i < (size_t)MSL_HITLIST_VICTIM_CAP;
       v++) {
    size_t i;
    if (use_hitbox_seed) {
      const size_t hb_base =
          (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
      i = hb_base + ((a * (size_t)MSL_MAX_HITBOXES + hb) * (size_t)MSL_MAX_PLAYERS + (size_t)v);
    } else {
      const size_t group_base = (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS *
                                (size_t)MSL_MAX_PLAYERS;
      const size_t g = (size_t)hit_group;
      i = group_base + ((a * (size_t)MSL_HITLIST_GROUPS + g) * (size_t)MSL_MAX_PLAYERS + (size_t)v);
    }
    const uint16_t cd_seed =
        use_hitbox_seed ? batch->state.combat_hitlist_hb_cd[i] : batch->state.combat_hitlist_cd[i];
    if (cd_seed == 0) {
      continue;
    }
    MslHitlistVictimEntry* e = &hit->victims_1[out_i++];
    e->id32 = 0;
    e->id16 = use_hitbox_seed ? batch->state.combat_hitlist_hb_victim_iid[i]
                              : batch->state.combat_hitlist_victim_iid[i];
    e->kind_slot = hitlist_fighter_key((uint8_t)v);
    // Seed semantics:
    // - 0xFFFF means "indefinite latch" (decomp: present with timer==0).
    e->cd = (cd_seed == 0xFFFFu) ? 0u : (uint8_t)(cd_seed & 0xFFu);
  }

  // Mark initialized for this reseed generation.
  const uint32_t gen = batch->state.hitlist_reseed_gen[bi];
  const size_t init_i = idx_fighter_hitlist(bi, attacker, hb_id);
  batch->state.fighter_hitlist_init_gen[init_i] = gen;
}

void hitlist_debug_clear_fighter_attacker(MslBatch* batch, int bi, int attacker) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return;
  }
  for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
    const size_t hl_i = idx_fighter_hitlist(bi, attacker, hb_id);
    hitlist_capsule_clear(&batch->state.fighter_hitlist[hl_i]);
    batch->state.fighter_hitlist_init_gen[hl_i] = batch->state.hitlist_reseed_gen[bi];
  }
}

static void hitlist_capsule_tick_one(MslHitlistCapsule* hit) {
  if (hit == NULL) {
    return;
  }
  // lbColl_80008A5C: decrement nonzero cooldowns; clear victim when it reaches 0.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008A5C
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* e = &hit->victims_1[i];
    if (entry_is_empty(e)) {
      continue;
    }
    if (e->cd != 0) {
      e->cd--;
      if (e->cd == 0) {
        entry_clear(e);
      }
    }
  }
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* e = &hit->victims_2[i];
    if (entry_is_empty(e)) {
      continue;
    }
    if (e->cd != 0) {
      e->cd--;
      if (e->cd == 0) {
        entry_clear(e);
      }
    }
  }
}

void hitlist_tick(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Fighters: tick per active hitbox (decomp: per active HitCapsule).
    for (int attacker = 0; attacker < num_players; attacker++) {
      const size_t a_idx = msl_idx_player(bi, attacker);
      // Hitlag gating: freeze hitlist decrement under hitlag, consistent with fighter proc gating
      // under !fp->x2219_b5 (post-decrement hitlag gate).
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      if (batch->state.hitlag[a_idx] != 0) {
        continue;
      }
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_state_i =
            ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
            (size_t)hb_id;
        if (!batch->state.hitbox_enabled[hb_state_i]) {
          continue;
        }
        const size_t hl_i = idx_fighter_hitlist(bi, attacker, hb_id);
        hitlist_capsule_tick_one(&batch->state.fighter_hitlist[hl_i]);
      }
    }

    // Items: tick per active item hitbox each frame (decomp: it_8027146C -> lbColl_80008A5C).
    // refs/melee/src/melee/it/itcoll.c::it_8027146C
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = msl_idx_item(bi, it);
      if (!batch->state.item_exists[ii]) {
        continue;
      }
      hitlist_capsule_tick_one(&batch->state.item_hitlist[ii]);
    }
  }
}

void hitlist_debug_insert_item_victims1(MslHitlistCapsule* hit, int type, uint32_t spawn_id,
                                        uint8_t rehit_frames) {
  if (hit == NULL) {
    return;
  }
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_item_key(0);
  key.id32 = spawn_id;
  (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
}
