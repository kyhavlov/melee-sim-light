#include "hitlist.h"

#include <string.h>

#include "batch_internal.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t idx_cd(int bi, int attacker, uint8_t hit_group, int victim) {
  const size_t a = (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
  const size_t g = (size_t)hit_group;
  const size_t v = (size_t)victim;
  return (a * (size_t)MSL_HITLIST_GROUPS + g) * (size_t)MSL_MAX_PLAYERS + v;
}

void hitlist_clear_group(MslBatch* batch, int bi, int attacker, uint8_t hit_group) {
  if (batch == NULL) {
    return;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return;
  }
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return;
  }

  for (int victim = 0; victim < MSL_MAX_PLAYERS; victim++) {
    const size_t i = idx_cd(bi, attacker, hit_group, victim);
    batch->state.combat_hitlist_cd[i] = (uint16_t)MSL_HITLIST_CD_EMPTY;
    batch->state.combat_hitlist_victim_iid[i] = 0;
  }
}

void hitlist_tick(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int attacker = 0; attacker < num_players; attacker++) {
      // Hitlag gating: in GALE01, fighter collision processing is gated under !hitlag, so hitlist
      // countdown decrement should also be frozen under hitlag (future-proof for finite rehit timers).
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (ftColl_800764DC under !fp->x2219_b5)
      const size_t p_i = (size_t)bi * (size_t)num_players + (size_t)attacker;
      if (batch->state.hitlag[p_i] != 0) {
        continue;
      }

      uint8_t group_active[MSL_HITLIST_GROUPS];
      memset(group_active, 0, sizeof(group_active));

      // Mark which hit_groups are active for this attacker this frame.
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        const uint8_t g = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        if (g < (uint8_t)MSL_HITLIST_GROUPS) {
          group_active[g] = 1;
        }
      }

      for (uint8_t g = 0; g < (uint8_t)MSL_HITLIST_GROUPS; g++) {
        if (!group_active[g]) {
          continue;
        }
        for (int victim = 0; victim < num_players; victim++) {
          const size_t i = idx_cd(bi, attacker, g, victim);
          const uint16_t cd = batch->state.combat_hitlist_cd[i];
          if (cd == (uint16_t)MSL_HITLIST_CD_EMPTY || cd == (uint16_t)MSL_HITLIST_CD_INDEFINITE) {
            continue;
          }
          const uint16_t cd2 = (uint16_t)(cd - 1u);
          batch->state.combat_hitlist_cd[i] = cd2;
          if (cd2 == (uint16_t)MSL_HITLIST_CD_EMPTY) {
            batch->state.combat_hitlist_victim_iid[i] = 0;
          }
        }
      }
    }
  }
}

uint8_t hitlist_allows(MslBatch* batch, int bi, int attacker, uint8_t hit_group, int victim,
                       uint16_t victim_iid) {
  if (batch == NULL) {
    return 0;
  }
  if (bi < 0 || bi >= batch->batch_size) {
    return 0;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return 0;
  }
  if (victim < 0 || victim >= (int)batch->config.num_players) {
    return 0;
  }
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    hit_group = 0;
  }
  const size_t i = idx_cd(bi, attacker, hit_group, victim);
  const uint16_t cd = batch->state.combat_hitlist_cd[i];
  if (cd == (uint16_t)MSL_HITLIST_CD_EMPTY) {
    return 1u;
  }

  // Decomp stores a victim pointer (`HitVictim.victim`). On death/respawn, the new fighter instance
  // pointer should not match the stale entry, so the victim is treated as "new" and can be hit.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  if (batch->state.combat_hitlist_victim_iid[i] != victim_iid) {
    batch->state.combat_hitlist_cd[i] = (uint16_t)MSL_HITLIST_CD_EMPTY;
    batch->state.combat_hitlist_victim_iid[i] = 0;
    return 1u;
  }

  return 0u;
}

void hitlist_register(MslBatch* batch, int bi, int attacker, uint8_t hit_group, int victim,
                      uint16_t victim_iid, uint8_t rehit_frames) {
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
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    hit_group = 0;
  }

  const uint16_t cd =
      (rehit_frames == 0) ? (uint16_t)MSL_HITLIST_CD_INDEFINITE : (uint16_t)rehit_frames;
  const size_t i = idx_cd(bi, attacker, hit_group, victim);
  batch->state.combat_hitlist_cd[i] = cd;
  batch->state.combat_hitlist_victim_iid[i] = victim_iid;
}
