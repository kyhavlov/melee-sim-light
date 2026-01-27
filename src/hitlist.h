#pragma once

#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decomp hitlist representation:
// - HitCapsule maintains per-victim hitlists (victims_1 / victims_2) with per-entry cooldowns.
// - lbColl_80008688 inserts/refreshes entries and returns whether the victim is "new".
// - lbColl_80008A5C decrements nonzero cooldowns each frame and clears the victim when it reaches 0.
// refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C
//
// Simulator representation (minimal, fighter victims only):
// - Dense cooldown map indexed by (attacker_slot, hit_group, victim_slot).
// - Value semantics:
//   - 0: empty (victim not in hitlist => can be hit)
//   - 1..255: finite cooldown remaining in frames
//   - 0xFFFF: indefinite latch (decomp: victim present with timer==0; cleared on hitbox-group re-enable)
enum {
  MSL_HITLIST_CD_EMPTY = 0u,
  MSL_HITLIST_CD_INDEFINITE = 0xFFFFu,
};

static inline uint8_t hitlist_hit_group_from_u16_7(uint16_t u16_7) {
  // MSLHITB1 u16_7 pack (see docs/DATA_CONTRACT.md):
  // - bits 8..10: hit_group (spawn_hitbox_0.hit_group)
  return (uint8_t)((u16_7 >> 8) & 0x7u);
}

static inline uint8_t hitlist_rehit_frames_from_u16_7(uint16_t u16_7) {
  // MSLHITB1 u16_7 pack (see docs/DATA_CONTRACT.md):
  // - low 8 bits: rehit_rate_frames (HitCapsule.x40_b4)
  return (uint8_t)(u16_7 & 0xFFu);
}

// Clears all cooldown entries for (bi, attacker, hit_group).
void hitlist_clear_group(MslBatch* batch, int bi, int attacker, uint8_t hit_group);

// Decrement finite cooldowns for active hitbox groups (must run before combat_resolve each frame).
void hitlist_tick(MslBatch* batch);

// Returns 1 if (attacker, hit_group) is allowed to hit victim this frame, else 0.
uint8_t hitlist_allows(MslBatch* batch, int bi, int attacker, uint8_t hit_group, int victim,
                       uint16_t victim_iid);

// Registers a hit on (victim) by setting the cooldown for (attacker, hit_group).
void hitlist_register(MslBatch* batch, int bi, int attacker, uint8_t hit_group, int victim,
                      uint16_t victim_iid, uint8_t rehit_frames);

#ifdef __cplusplus
}  // extern "C"
#endif
