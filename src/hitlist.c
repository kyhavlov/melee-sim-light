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

  // Victim identity semantics (decomp-faithful, reseed-friendly):
  //
  // Decomp hitlists store a raw victim pointer (`HitVictim.victim`) on the HitCapsule:
  // - presence in the victim list gates re-hit (lbColl_8000ACFC),
  // - insertion uses pointer equality for "already hit" vs "new victim" semantics (lbColl_80008688).
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  //
  // This simulator does not have a stable per-fighter pointer identity in the seed schema, so it
  // uses the Slippi-visible `instance_id` as a proxy. However, `instance_id` can change on motion
  // state entry (ft_800895E0), even though the victim pointer in decomp does not.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  //
  // Policy:
  // - If the stored identity differs, do NOT treat that as a "new victim" by default; keep rehit
  //   suppression and simply rebind the proxy to the current instance_id.
  // - Only clear the entry (treat as "new victim") when the victim is in a death/respawn motion
  //   state or dead by stocks. This is a heuristic approximation of "victim pointer changed" in
  //   decomp (lbColl_80008688 uses pointer equality on HitVictim.victim).
  //
  // IMPORTANT: This function is intentionally not a pure predicate. Even when returning 0
  // ("not allowed"), it may update `combat_hitlist_victim_iid` to keep the proxy identity bound to
  // the current Slippi-visible `instance_id` for this victim.
  const uint16_t stored_iid = batch->state.combat_hitlist_victim_iid[i];
  if (stored_iid != victim_iid) {
    const size_t v_idx = msl_idx_player(bi, victim);
    const uint8_t v_stocks = batch->state.stocks[v_idx];
    const uint16_t v_act = batch->state.action_id[v_idx];
    if (hitlist_victim_pointer_may_change(v_stocks, v_act)) {
      batch->state.combat_hitlist_cd[i] = (uint16_t)MSL_HITLIST_CD_EMPTY;
      batch->state.combat_hitlist_victim_iid[i] = 0;
      return 1u;
    }

    // Rebind proxy identity without clearing the suppression latch.
    batch->state.combat_hitlist_victim_iid[i] = victim_iid;
    return 0u;
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
