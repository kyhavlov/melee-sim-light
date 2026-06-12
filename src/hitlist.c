#include "hitlist.h"
#include "char_registry.h"

#include <string.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "damage_source.h"
#include "motion_state_owners.h"
#include "move_tables.h"

static inline size_t idx_fighter_hitlist(int bi, int p, int hb_id) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_id;
}

static inline uint8_t hitlist_specialhi_action(uint8_t char_id, uint16_t action_id) {
  // Ownership from the extracted MotionState row identity: only the spacie SpecialHi rows
  // carry these kinds; other characters' same-numbered actions are kind 0.
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return ((uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD <= fx_kind &&
          fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_HI_BOUND)
             ? 1u
             : 0u;
}

static inline uint8_t hitlist_grounded_attack_runtime_clear_owns_empty_hitcapsule(
    const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!msl_motion_state_class_has(batch->state.char_id[idx], action_id,
                                  MSL_MS_CLASS_GROUNDED_ATTACK)) {
    return 0u;
  }
  if (action_id == (uint16_t)MSL_ACT_ATTACK_100_LOOP) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t hitlist_attackair_create_phase_runtime_clear_owns_empty_hitcapsule(
    const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_ATTACK_AIR)) {
    return 0u;
  }
  // AttackAir multi-hit clear/create bands:
  // - ftAction_8007121C processes generated create_hitbox commands and runs ftColl_800768A0.
  // - It runs ftColl_800768A0 only when the target HitCapsule slot is disabled or changes hit_group;
  //   for the common AttackAir scripts, the data-backed signal for that empty owner is a
  //   clear_hitboxes -> create_hitbox transition. Same-slot create payloads without an intervening
  //   clear update damage/offsets while preserving victims_1.
  // - For later create bands that really are enable/group-change edges, an empty runtime-initialized
  //   HitCapsule is source-owned and must not be lazily backfilled from previous same-source BODY
  //   attribution. This preserves valid multi-hit re-hits such as Fox DAir after clear/create while
  //   keeping Falco DAir's no-clear late payload on the existing victim latch.
  // data/scripts/{fox,falco}.bin (MSLFTSC1 AttackAir create_hitbox phases)
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_CopyHitCapsule}
  const float anim_frame = batch->state.anim_frame_f32[idx];
  return move_tables_attackair_post_clear_create_hitbox_phase(batch->state.char_id[idx], action_id,
                                                              anim_frame);
}

uint8_t hitlist_rollout_dense_seed_same_object_rebind_applies(const MslBatch* batch, int bi,
                                                              int attacker, int victim) {
  if (batch == NULL || bi < 0 || attacker < 0 || victim < 0 ||
      attacker >= (int)batch->config.num_players || victim >= (int)batch->config.num_players ||
      attacker == victim) {
    return 0u;
  }
  if (batch->replay_rollout_reseeded == NULL || batch->replay_rollout_reseeded[bi] == 0u) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t v_idx = msl_idx_player(bi, victim);
  if (hitlist_victim_pointer_may_change(batch->state.stocks[v_idx],
                                        batch->state.action_id[v_idx])) {
    return 0u;
  }
  if (batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] != batch->state.action_id[a_idx]) {
    return 0u;
  }
  if (!msl_damage_source_victim_matches_attacker(batch, v_idx, a_idx, attacker)) {
    return 0u;
  }
  // Replay rollout dense seeds store Slippi-visible instance_id as a proxy for decomp's raw
  // HitVictim fighter object pointer. That object pointer survives normal motion-state changes
  // (for example Landing -> AttackLw4) while Slippi instance_id advances. Preserve the dense
  // victim only when same-source attribution proves the active attacker still owns the hidden
  // HitCapsule victim pointer; death/rebirth/object-lifetime boundaries fail closed above.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // refs/melee/src/melee/lb/types.h::HitCapsule
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (instance_id/last_hit_by export)
  return 1u;
}

static inline size_t idx_item_hitlist(int bi, int item_slot) {
  return ((size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)item_slot) * (size_t)MSL_MAX_HITBOXES;
}

static inline size_t idx_item_hitlist_hb(int bi, int item_slot, int hb_id) {
  return idx_item_hitlist(bi, item_slot) + (size_t)hb_id;
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
  // lbColl_80008688 stores HitCapsule.x40_b4 only for the same type set that refreshes an existing
  // victim entry. Fighter BODY/SHIELD/hitbox-contact inserts (types 0/1/3) write x4=0, so the
  // victim remains present until the HitCapsule is cleared/copied instead of expiring on rehit rate.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  victims[insert_idx].cd = refresh_set ? cd_set : 0u;

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
                                                  int attacker, uint8_t victim_port,
                                                  uint16_t victim_iid, size_t* out_index) {
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
    if (attacker >= 0 && attacker < (int)batch->config.num_players &&
        victims[i].id32 == MSL_HITLIST_FIGHTER_ID32_SEED_DENSE) {
      const size_t a_idx = msl_idx_player(bi, attacker);
      const size_t v_idx = msl_idx_player(bi, (int)victim_port);
      if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
          (batch->state.hitlag[v_idx] != 0u || batch->state.hitstun[v_idx] != 0u) &&
          batch->state.instance_hit_by[v_idx] != batch->state.instance_id[a_idx]) {
        // AttackAirB active-damage stale dense latch:
        // Dense group seed entries lack per-HitCapsule insertion provenance. During an active
        // damage episode, `instance_hit_by` identifies the source action instance that owns the
        // current BODY attribution; a different live AttackAirB instance must not be suppressed by
        // a stale dense proxy.
        // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
        entry_clear(&victims[i]);
        return 0u;
      }
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

uint8_t hitlist_seed_init_attackairlw_no_clear_dense_body(MslBatch* batch, int bi, int attacker,
                                                          int hb_id, int victim,
                                                          uint16_t victim_iid) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || victim < 0 || victim >= (int)MSL_MAX_PLAYERS ||
      attacker == victim) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t v_idx = msl_idx_player(bi, victim);
  const uint16_t action_id = batch->state.action_id[a_idx];
  if (action_id != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      batch->state.seed_prev_action_id[a_idx] != action_id || batch->state.hitlag[a_idx] != 0u ||
      batch->state.hitstun[a_idx] != 0u || batch->state.hitlag[v_idx] != 0u ||
      batch->state.hitstun[v_idx] != 0u ||
      !move_tables_attackair_same_group_payload_preserves_hitcapsule(
          batch->state.char_id[a_idx], action_id, batch->state.anim_frame_f32[a_idx])) {
    return 0u;
  }

  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  const size_t hb_i = idx_fighter_hitlist(bi, attacker, hb_id);
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)victim);
  if (batch->state.combat_hitlist_cd[cd_i] == 0u) {
    return 0u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid != 0u && seed_iid != victim_iid &&
      hitlist_victim_pointer_may_change(batch->state.stocks[v_idx],
                                        batch->state.action_id[v_idx])) {
    return 0u;
  }

  // Falco DAir no-clear payload seed initialization:
  // Same-slot create payloads preserve HitCapsule.victims_1 in ftAction_8007121C/ftColl_800768A0.
  // Some rollout reseed rows start after that script callback and expose only the legacy dense
  // hit_group map, not per-HitCapsule victims_1. Initialize the live BODY HitCapsule from that
  // source-owned dense seed at the BODY owner boundary, then let ordinary live hitlist state own
  // later frames. Do not feed this into the shield gate: shield acceptance has its own ShieldDesc
  // contact seed/provenance lanes.
  // data/scripts/falco.bin (MSLFTSC1 ftCo_SM_AttackAirLw no-clear create_hitbox payload)
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.id16 = (seed_iid != 0u) ? seed_iid : victim_iid;
  key.kind_slot = hitlist_fighter_key((uint8_t)victim);
  (void)hitlist_insert_victims1(&batch->state.fighter_hitlist[hb_i], (int)MSL_LBCOLL_INSERT_FT_BODY,
                                &key, 0u);
  return 1u;
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
  {
    const size_t valid_i =
        ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
        (size_t)hb_id;
    const size_t hb_base =
        (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
    const size_t hb_cd_i =
        hb_base +
        (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb_id) * (size_t)MSL_MAX_PLAYERS +
         (size_t)victim);
    const size_t a_idx = msl_idx_player(bi, attacker);
    const size_t v_idx = msl_idx_player(bi, victim);
    const uint8_t same_source_body_attribution =
        msl_damage_source_victim_matches_attacker(batch, v_idx, a_idx, attacker);
    const uint16_t seed_cd = batch->state.combat_hitlist_hb_cd[hb_cd_i];
    if (batch->state.combat_hitlist_hb_valid[valid_i] && seed_cd != 0u &&
        (batch->state.hitlag_pre_timer[a_idx] != 0u ||
         batch->state.hitlag_pre_timer[v_idx] != 0u)) {
      // Hitlag-frozen per-HitCapsule seed carry:
      // - Fighter_8006A360 skips ftAction_8007121C while hitlag is active, so an already-created
      //   HitCapsule's victims_1 list must survive through the first post-decrement hitlag-exit
      //   collision pass.
      // - A reseed can provide authoritative per-HitCapsule victims_1 state
      //   (`combat_hitlist_hb_valid/cd`), but the runtime capsule may not have been materialized if
      //   the previous rollout step stayed frozen and skipped combat. Materialize that exact lane
      //   lazily before lbColl_8000ACFC-style victim-presence testing.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      MslHitlistVictimEntry key;
      memset(&key, 0, sizeof(key));
      key.id16 = batch->state.combat_hitlist_hb_victim_iid[hb_cd_i];
      key.kind_slot = hitlist_fighter_key((uint8_t)victim);
      const uint8_t cd_set = (seed_cd == 0xFFFFu) ? 0u : (uint8_t)(seed_cd & 0xFFu);
      (void)hitlist_insert_victims1(hit, (int)MSL_LBCOLL_INSERT_FT_SHIELD, &key, cd_set);
    }
    const uint8_t runtime_empty_source_clear =
        (uint8_t)(batch->state.fighter_hitlist_init_gen[hb_i] ==
                      batch->state.hitlist_reseed_gen[bi] &&
                  (hitlist_grounded_attack_runtime_clear_owns_empty_hitcapsule(batch, a_idx) ||
                   hitlist_attackair_create_phase_runtime_clear_owns_empty_hitcapsule(batch,
                                                                                      a_idx)));
    if (seed_cd == 0u && !batch->state.combat_hitlist_hb_valid[valid_i] &&
        (batch->state.fighter_hitlist_init_gen[hb_i] != batch->state.hitlist_reseed_gen[bi] ||
         !runtime_empty_source_clear) &&
        ((batch->state.hitlag_pre_timer[a_idx] != 0u &&
          batch->state.hitlag_pre_timer[v_idx] != 0u) ||
         (batch->state.hitbox_enable_edge[hb_i] == 0u && batch->state.hitstun[v_idx] != 0u)) &&
        batch->state.hitbox_prev_enabled[hb_i] != 0u &&
        batch->state.action_id[a_idx] == batch->state.seed_prev_action_id[a_idx] &&
        same_source_body_attribution) {
      // Same-source sustained HitCapsule carry:
      // - While either fighter is frozen in hitlag, Fighter_8006A360 skips the animation/collision
      //   callback path that would clear/copy HitCapsule victims_1.
      // - Some replay seeds expose only the previous x58/x4C capsule geometry plus BODY
      //   attribution (`instance_hit_by`/`last_hit_by`), not an authoritative per-HitCapsule
      //   victim list. When both attacker and victim had hitlag at frame start, and the current
      //   HitCapsule has not already been initialized by the relevant active script event path in
      //   this runtime generation, that attribution proves the current HitCapsule already contains
      //   the victim and must suppress the first post-decrement collision pass.
      // - Authoritative per-HitCapsule empty seed lanes and runtime-initialized clear/copy results
      //   are the source owner for exact rows; they prove this slot's victims_1 list is empty and
      //   must not be backfilled from BODY attribution alone. This covers same-action grounded
      //   restarts and aerial multi-hit clear/create bands: ftAction_8007121C calls
      //   ftColl_800768A0, then lbColl_80008440/lbColl_CopyHitCapsule owns the concrete victims_1
      //   list until a later source event changes it.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      MslHitlistVictimEntry key;
      memset(&key, 0, sizeof(key));
      key.id16 = victim_iid;
      key.kind_slot = hitlist_fighter_key((uint8_t)victim);
      (void)hitlist_insert_victims1(hit, (int)MSL_LBCOLL_INSERT_FT_SHIELD, &key, 0u);
    }
  }
  size_t found = 0;
  if (hitlist_capsule_find_fighter_entry(batch, bi, hit->victims_1, (size_t)MSL_HITLIST_VICTIM_CAP,
                                         attacker, (uint8_t)victim, victim_iid, &found)) {
    return 0u;
  }
  return 1u;
}

uint8_t hitlist_allows_fighter_live_collision(MslBatch* batch, int bi, int attacker, int hb_id,
                                              int victim, uint16_t victim_iid) {
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
  const uint8_t key = hitlist_fighter_key((uint8_t)victim);
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* e = &hit->victims_1[i];
    if (entry_is_empty(e)) {
      continue;
    }
    if (e->kind_slot != key) {
      continue;
    }
    if (e->id32 == MSL_HITLIST_FIGHTER_ID32_SEED_DENSE) {
      // Dense seed fallback is a teacher-forced compatibility surface, not concrete
      // collision-pass HitCapsule provenance. Exact per-HitCapsule seeds remain covered by
      // hitlist_allows_fighter(), and clank's replay-seed prefilter has its own narrow bridge.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
      continue;
    }
    if (e->id16 != victim_iid) {
      const size_t v_idx = msl_idx_player(bi, victim);
      if (hitlist_victim_pointer_may_change(batch->state.stocks[v_idx],
                                            batch->state.action_id[v_idx])) {
        entry_clear(e);
        return 1u;
      }
      e->id16 = victim_iid;
    }
    return 0u;
  }
  return 1u;
}

uint8_t hitlist_allows_fighter_item(MslBatch* batch, int bi, int attacker, int hb_id, int item_slot,
                                    uint32_t item_spawn_id) {
  if (batch == NULL) {
    return 0u;
  }
  if (bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES ||
      item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return 0u;
  }

  const size_t hb_i = idx_fighter_hitlist(bi, attacker, hb_id);
  MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hb_i];
  const uint8_t key = hitlist_item_key((uint8_t)item_slot);
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    const MslHitlistVictimEntry* e = &hit->victims_1[i];
    if (entry_is_empty(e) || e->kind_slot != key) {
      continue;
    }
    if (e->id32 == item_spawn_id) {
      return 0u;
    }
  }
  return 1u;
}

uint8_t hitlist_allows_fighter_v2(MslBatch* batch, int bi, int attacker, int hb_id, int victim,
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
  if (hitlist_capsule_find_fighter_entry(batch, bi, hit->victims_2, (size_t)MSL_HITLIST_VICTIM_CAP,
                                         attacker, (uint8_t)victim, victim_iid, &found)) {
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
    // Runtime collision has now materialized this HitCapsule's live victim list. Mark it initialized
    // for the current reseed generation so the next hitbox refresh does not overwrite source-owned
    // victims_1 state with the teacher-forced seed materializer.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,inlineB0,ftColl_80076CBC}
    // refs/melee/src/melee/lb/types.h::HitCapsule
    batch->state.fighter_hitlist_init_gen[hl_i] = batch->state.hitlist_reseed_gen[bi];
    (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
  }
}

void hitlist_register_fighter_group_item(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                         int item_slot, uint32_t item_spawn_id, int type,
                                         uint8_t rehit_frames) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size || attacker < 0 ||
      attacker >= (int)batch->config.num_players || item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return;
  }

  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_item_key((uint8_t)item_slot);
  key.id32 = item_spawn_id;
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
    batch->state.fighter_hitlist_init_gen[hl_i] = batch->state.hitlist_reseed_gen[bi];
    (void)hitlist_insert_victims1(&batch->state.fighter_hitlist[hl_i], type, &key, rehit_frames);
  }
}

void hitlist_register_fighter_group_v2(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
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
  const uint8_t key = hitlist_fighter_key((uint8_t)victim);
  for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
    const size_t hb_state_i =
        ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
        (size_t)hb_id;
    if (!batch->state.hitbox_enabled[hb_state_i]) {
      continue;
    }
    const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_state_i]);
    if (group != hit_group) {
      continue;
    }
    const size_t hl_i = idx_fighter_hitlist(bi, attacker, hb_id);
    MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
    // Runtime phantom/tip-log ownership is live HitCapsule state too; keep seed materialization
    // from clearing victims_2 on the following refresh.
    // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,ftColl_80076ED8}
    // refs/melee/src/melee/lb/types.h::HitCapsule
    batch->state.fighter_hitlist_init_gen[hl_i] = batch->state.hitlist_reseed_gen[bi];
    MslHitlistVictimEntry victim_key;
    memset(&victim_key, 0, sizeof(victim_key));
    victim_key.kind_slot = key;
    victim_key.id16 = victim_iid;
    (void)hitlist_insert_victims2(hit, type, &victim_key, rehit_frames);
  }
}

void hitlist_register_fighter_hitbox(MslBatch* batch, int bi, int attacker, int hb_id, int victim,
                                     uint16_t victim_iid, int type, uint8_t rehit_frames) {
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
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return;
  }

  const size_t hb_i = idx_fighter_hitlist(bi, attacker, hb_id);
  MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hb_i];
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_fighter_key((uint8_t)victim);
  key.id16 = victim_iid;
  batch->state.fighter_hitlist_init_gen[hb_i] = batch->state.hitlist_reseed_gen[bi];
  (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
}

uint8_t hitlist_allows_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                           int victim, uint16_t victim_iid) {
  if (batch == NULL) {
    return 0u;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  if (item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return 0u;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return 0u;
  }

  const size_t ii = idx_item_hitlist_hb(bi, item_slot, hitbox_id);
  MslHitlistCapsule* hit = &batch->state.item_hitlist[ii];
  size_t found = 0;
  if (hitlist_capsule_find_fighter_entry(batch, bi, hit->victims_1, (size_t)MSL_HITLIST_VICTIM_CAP,
                                         -1, (uint8_t)victim, victim_iid, &found)) {
    return 0u;
  }
  return 1u;
}

void hitlist_register_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                          int victim, uint16_t victim_iid, int type,
                                          uint8_t rehit_frames) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (item_slot < 0 || item_slot >= MSL_MAX_ITEMS) {
    return;
  }
  if (hitbox_id < 0 || hitbox_id >= MSL_MAX_HITBOXES) {
    return;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return;
  }

  const size_t ii = idx_item_hitlist_hb(bi, item_slot, hitbox_id);
  MslHitlistCapsule* hit = &batch->state.item_hitlist[ii];
  MslHitlistVictimEntry key;
  memset(&key, 0, sizeof(key));
  key.kind_slot = hitlist_fighter_key((uint8_t)victim);
  key.id16 = victim_iid;
  (void)hitlist_insert_victims1(hit, type, &key, rehit_frames);
}

void hitlist_register_item_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                   uint16_t victim_iid, int type, uint8_t rehit_frames) {
  for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, hb_id, victim, victim_iid, type,
                                         rehit_frames);
  }
}

static void hitlist_seed_init_fighter_hitbox_from_group_impl(MslBatch* batch, int bi, int attacker,
                                                             int hb_id, uint8_t hit_group,
                                                             uint8_t allow_stale_iid_rebind) {
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
  const uint8_t is_replay_rollout =
      (batch->replay_rollout_reseeded != NULL && batch->replay_rollout_reseeded[bi] != 0u) ? 1u
                                                                                           : 0u;
  const uint8_t exact_replay_reseed =
      (is_replay_rollout && batch->replay_rollout_seed_frame_id != NULL &&
       batch->state.frame_id[bi] == batch->replay_rollout_seed_frame_id[bi])
          ? 1u
          : 0u;
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
    const uint16_t stored_iid = use_hitbox_seed ? batch->state.combat_hitlist_hb_victim_iid[i]
                                                : batch->state.combat_hitlist_victim_iid[i];
    const size_t v_idx = msl_idx_player(bi, v);
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (!use_hitbox_seed &&
        hitlist_specialhi_action(batch->state.char_id[a_idx], batch->state.action_id[a_idx])) {
      // Dense group seeds are a compatibility surface for replay-derived HitCapsule victims_1,
      // not per-HitCapsule authority. SpecialHi charge/launch spans can carry coarse dense
      // entries across inactive gaps, so materialize them only when replay-visible state proves
      // the victim is still owned by an accepted hit from this attacker: the stored victim instance
      // is current, BODY attribution names the attacker instance, and last_hit_by names the
      // attacker source port. This spans post-hitlag DownBound/knockdown aftermath where the
      // HitVictim pointer still suppresses repeats after hitstun has cleared, but rejects stale
      // same-victim dense entries from unrelated source ownership. Authoritative per-hitbox seeds
      // stay exact.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      if (stored_iid == 0u || stored_iid != batch->state.instance_id[v_idx]) {
        continue;
      }
      if (!msl_damage_source_victim_matches_attacker(batch, v_idx, a_idx, attacker)) {
        continue;
      }
    } else if (is_replay_rollout && !exact_replay_reseed && !use_hitbox_seed) {
      // Dense group seeds are a compatibility surface for replay-derived HitCapsule victims_1.
      // Once a replay rollout advances past its seed frame, the filter below prevents stale dense
      // seeds from becoming a free-running runtime bridge without same-source proof.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      if (stored_iid != 0u && stored_iid != batch->state.instance_id[v_idx]) {
        // Dense group seeds carry a Slippi-visible instance_id proxy for decomp's raw victim
        // pointer, not the raw pointer itself. On a normal HitCapsule create edge,
        // ftColl_800768A0 clears/copies concrete victims_1 state; a stale dense seed usually fails
        // closed instead of suppressing a new hit after the victim entered a new motion state.
        //
        // Retained stale-iid rebinds are explicit source-proven bridges where either the caller or
        // the rollout same-source lane below proves the source phase still owns the same hidden
        // victim pointer.
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
        const uint8_t rollout_same_object_rebind =
            (is_replay_rollout && !use_hitbox_seed &&
             hitlist_rollout_dense_seed_same_object_rebind_applies(batch, bi, attacker, v))
                ? 1u
                : 0u;
        if (!rollout_same_object_rebind &&
            (!allow_stale_iid_rebind ||
             hitlist_victim_pointer_may_change(batch->state.stocks[v_idx],
                                               batch->state.action_id[v_idx]))) {
          continue;
        }
      }
    }
    MslHitlistVictimEntry* e = &hit->victims_1[out_i++];
    e->id32 = use_hitbox_seed ? 0u : MSL_HITLIST_FIGHTER_ID32_SEED_DENSE;
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

void hitlist_seed_init_fighter_hitbox_from_group(MslBatch* batch, int bi, int attacker, int hb_id,
                                                 uint8_t hit_group) {
  hitlist_seed_init_fighter_hitbox_from_group_impl(batch, bi, attacker, hb_id, hit_group, 0u);
}

void hitlist_seed_init_fighter_hitbox_from_group_allow_stale_iid(MslBatch* batch, int bi,
                                                                 int attacker, int hb_id,
                                                                 uint8_t hit_group) {
  hitlist_seed_init_fighter_hitbox_from_group_impl(batch, bi, attacker, hb_id, hit_group, 1u);
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
      const size_t base = idx_item_hitlist(bi, it);
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        hitlist_capsule_tick_one(&batch->state.item_hitlist[base + (size_t)hb_id]);
      }
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
