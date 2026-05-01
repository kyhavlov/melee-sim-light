#pragma once

#include <stdint.h>

#include "api.h"
#include "hitlist_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decomp hitlist representation:
// - HitCapsule maintains per-victim hitlists (victims_1 / victims_2) with per-entry cooldowns.
// - lbColl_80008688 inserts/refreshes entries and returns whether the victim is "new".
// - lbColl_80008A5C decrements nonzero cooldowns each frame and clears the victim when it reaches 0.
// refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C

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

// HitCapsule victim list primitives (lbColl-shaped).
void hitlist_capsule_clear(MslHitlistCapsule* hit);
void hitlist_capsule_copy(const MslHitlistCapsule* src, MslHitlistCapsule* dst);

// Decrement finite cooldowns for active hitbox groups (must run before combat_resolve each frame).
void hitlist_tick(MslBatch* batch);

// Returns 1 if (attacker hitbox hb_id) is allowed to hit victim this frame, else 0.
// Decomp anchor: refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
uint8_t hitlist_allows_fighter(MslBatch* batch, int bi, int attacker, int hb_id, int victim,
                               uint16_t victim_iid);
uint8_t hitlist_allows_fighter_live_collision(MslBatch* batch, int bi, int attacker, int hb_id,
                                              int victim, uint16_t victim_iid);
// Returns 1 if the phantom/tip-log lane (victims_2) does not already contain (victim).
// Decomp anchor: refs/melee/src/melee/ft/ftcoll.c::checkTipLog
uint8_t hitlist_allows_fighter_v2(MslBatch* batch, int bi, int attacker, int hb_id, int victim,
                                  uint16_t victim_iid);

// Registers a hit on (victim) across all enabled hitboxes in the same hit_group.
// Decomp anchor (share across same x4): refs/melee/src/melee/ft/ftcoll.c::inlineB0 and ::ftColl_80076808
void hitlist_register_fighter_group(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                    int victim, uint16_t victim_iid, int type,
                                    uint8_t rehit_frames);
// Registers a phantom/tip-log victim across all enabled hitboxes in the same hit_group.
// Decomp anchor: refs/melee/src/melee/ft/ftcoll.c::{inlineB0,ftColl_80076ED8}
void hitlist_register_fighter_group_v2(MslBatch* batch, int bi, int attacker, uint8_t hit_group,
                                       int victim, uint16_t victim_iid, int type,
                                       uint8_t rehit_frames);

// Registers a fighter victim in one item hitbox capsule's victim list (victims_1).
// Decomp anchor (items): refs/melee/src/melee/it/itcoll.c::it_8026FA2C / it_8026FAC4
void hitlist_register_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                          int victim, uint16_t victim_iid, int type,
                                          uint8_t rehit_frames);

// Convenience wrapper: register a fighter victim across all item hitboxes.
void hitlist_register_item_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                   uint16_t victim_iid, int type, uint8_t rehit_frames);

// Returns 1 if (item_slot, hitbox_id) can hit (victim) this frame, else 0.
uint8_t hitlist_allows_item_hitbox_fighter(MslBatch* batch, int bi, int item_slot, int hitbox_id,
                                           int victim, uint16_t victim_iid);

// Seed bridge: initialize an active fighter hitbox's victim list from the seeded per-hitbox map,
// falling back to the legacy dense hit_group map when no per-hitbox seed is marked valid.
void hitlist_seed_init_fighter_hitbox_from_group(MslBatch* batch, int bi, int attacker, int hb_id,
                                                 uint8_t hit_group);

// Narrow seed bridge for a proven same-fighter-object dense seed lane whose Slippi-visible
// instance_id changed before the current HitCapsule create edge. Normal dense seeds fail closed on
// stale instance ids because ftColl_800768A0 create edges clear/copy concrete HitCapsule state.
void hitlist_seed_init_fighter_hitbox_from_group_allow_stale_iid(MslBatch* batch, int bi,
                                                                 int attacker, int hb_id,
                                                                 uint8_t hit_group);

// Replay-rollout bridge for dense seed maps whose Slippi instance-id proxy changed even though
// decomp's raw fighter object pointer is still the same victim object.
uint8_t hitlist_rollout_dense_seed_same_object_rebind_applies(const MslBatch* batch, int bi,
                                                              int attacker, int victim);

// Debug helper: clear victim lists for all hitboxes on (attacker).
void hitlist_debug_clear_fighter_attacker(MslBatch* batch, int bi, int attacker);

// Test-only helper: insert an item victim into `victims_1` with lbColl_80008688 semantics.
// Intended for deterministic ring insertion/eviction unit tests.
void hitlist_debug_insert_item_victims1(MslHitlistCapsule* hit, int type, uint32_t spawn_id,
                                        uint8_t rehit_frames);

#ifdef __cplusplus
}  // extern "C"
#endif
