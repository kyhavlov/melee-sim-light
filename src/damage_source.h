#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

enum { MSL_DAMAGE_SOURCE_NONE = 6 };

typedef struct MslDamageSourceEpisode {
  uint8_t has_source;
  int source_slot;
  size_t source_idx;
  uint8_t source_port0;
  uint16_t source_instance_id;
  uint16_t victim_instance_hit_by;
  uint8_t instance_matches_source;
  uint8_t x18c8_active;
  uint8_t fighter_8006cda4_pre_gate_count;
} MslDamageSourceEpisode;

static inline uint8_t msl_damage_source_port0_for_slot(const MslBatch* batch, size_t source_idx,
                                                       int source_slot) {
  // Slippi exports `fp->dmg.x18C4_source_ply` in raw controller-port domain, while runtime arrays
  // use compact local slots. All replay-facing source writes must map through `source_port0`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (batch == NULL || source_slot < 0 || source_slot >= (int)MSL_MAX_PLAYERS) {
    return (uint8_t)MSL_DAMAGE_SOURCE_NONE;
  }
  const uint8_t source_port0 = batch->state.source_port0[source_idx];
  return (source_port0 < (uint8_t)MSL_MAX_PLAYERS) ? source_port0 : (uint8_t)source_slot;
}

static inline int msl_damage_source_local_slot_from_port0(const MslBatch* batch, int bi,
                                                          int num_players, uint8_t source_port0) {
  if (batch == NULL || bi < 0 || num_players < 0) {
    return -1;
  }
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.source_port0[idx] == source_port0) {
      return p;
    }
  }
  return -1;
}

static inline int msl_damage_source_seed_local_slot_from_port0(const MslSeed* seed,
                                                               int active_players,
                                                               uint8_t source_port0) {
  if (seed == NULL || active_players < 0) {
    return -1;
  }
  for (int p = 0; p < active_players; p++) {
    if (seed->source_port0[p] == source_port0) {
      return p;
    }
  }
  return -1;
}

static inline MslDamageSourceEpisode msl_damage_source_episode_from_victim(const MslBatch* batch,
                                                                           int bi, int victim_slot,
                                                                           size_t victim_idx) {
  MslDamageSourceEpisode ep = {0};
  ep.source_slot = -1;
  ep.source_idx = (size_t)0;
  if (batch == NULL || bi < 0 || victim_slot < 0 || victim_slot >= (int)batch->config.num_players) {
    return ep;
  }

  ep.source_port0 = batch->state.last_hit_by[victim_idx];
  ep.victim_instance_hit_by = batch->state.instance_hit_by[victim_idx];
  ep.x18c8_active = (batch->state.source_clear_timer_x18c8[victim_idx] != 0u) ? 1u : 0u;
  ep.fighter_8006cda4_pre_gate_count =
      batch->state.fighter_8006cda4_pre_gate_consume_count[victim_idx];

  const int source_slot = msl_damage_source_local_slot_from_port0(
      batch, bi, (int)batch->config.num_players, ep.source_port0);
  if (source_slot < 0 || source_slot == victim_slot) {
    return ep;
  }

  ep.has_source = 1u;
  ep.source_slot = source_slot;
  ep.source_idx = msl_idx_player(bi, source_slot);
  ep.source_instance_id = batch->state.instance_id[ep.source_idx];
  ep.instance_matches_source = (ep.victim_instance_hit_by == ep.source_instance_id) ? 1u : 0u;
  return ep;
}

static inline MslDamageSourceEpisode msl_damage_source_episode_from_victim_idx(
    const MslBatch* batch, size_t victim_idx) {
  if (batch == NULL) {
    MslDamageSourceEpisode ep = {0};
    ep.source_slot = -1;
    return ep;
  }
  const int bi = (int)(victim_idx / (size_t)MSL_MAX_PLAYERS);
  const int victim_slot = (int)(victim_idx % (size_t)MSL_MAX_PLAYERS);
  return msl_damage_source_episode_from_victim(batch, bi, victim_slot, victim_idx);
}

static inline uint8_t msl_damage_source_victim_matches_attacker(const MslBatch* batch,
                                                                size_t victim_idx,
                                                                size_t attacker_idx,
                                                                int attacker_slot) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t source_port0 = msl_damage_source_port0_for_slot(batch, attacker_idx, attacker_slot);
  return (batch->state.last_hit_by[victim_idx] == source_port0 &&
          batch->state.instance_hit_by[victim_idx] == batch->state.instance_id[attacker_idx])
             ? 1u
             : 0u;
}

static inline uint8_t msl_damage_source_victim_port_matches_attacker(const MslBatch* batch,
                                                                     size_t victim_idx,
                                                                     size_t attacker_idx,
                                                                     int attacker_slot) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.last_hit_by[victim_idx] ==
          msl_damage_source_port0_for_slot(batch, attacker_idx, attacker_slot))
             ? 1u
             : 0u;
}

static inline void msl_damage_source_clear(MslBatch* batch, size_t victim_idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.last_hit_by[victim_idx] = (uint8_t)MSL_DAMAGE_SOURCE_NONE;
  batch->state.source_clear_timer_x18c8[victim_idx] = 0u;
}

static inline void msl_damage_source_write_direct(MslBatch* batch, size_t victim_idx,
                                                  uint8_t source_port0) {
  if (batch == NULL) {
    return;
  }
  batch->state.last_hit_by[victim_idx] = source_port0;
}

static inline void msl_damage_source_write_hit_ftcoll_8007861c(MslBatch* batch, size_t victim_idx,
                                                               uint8_t source_port0) {
  if (batch == NULL) {
    return;
  }
  // ftColl_8007861C (the hit-time attribution writer) sets victim->dmg.x18C8 = -1 alongside
  // x18C4_source_ply: being hit RETIRES any running source-clear countdown, and the owner then
  // persists until the next grounded x9_b1 motion entry re-arms the timer.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007861C
  // refs/melee/src/melee/ft/fighter.c (x18C8 arm inside Fighter_ChangeMotionState)
  batch->state.last_hit_by[victim_idx] = source_port0;
  batch->state.source_clear_timer_x18c8[victim_idx] = 0u;
}

static inline void msl_damage_source_commit_processhit(MslBatch* batch, size_t victim_idx,
                                                       uint8_t source_port0) {
  if (batch == NULL) {
    return;
  }
  // Fighter_ProcessHit writes the collision source owner before percent/no-KB aftermath. Grounded
  // `ftCommon_800804FC` then clears `x18C4_source_ply` and disables `x18C8`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  if (batch->state.on_ground[victim_idx] != 0u) {
    msl_damage_source_clear(batch, victim_idx);
    return;
  }
  msl_damage_source_write_hit_ftcoll_8007861c(batch, victim_idx, source_port0);
}
