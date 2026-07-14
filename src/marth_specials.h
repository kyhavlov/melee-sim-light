#pragma once

#include <stdint.h>

#include "api.h"

// Marth (ftMs_*) character specials: Shield Breaker, Dancing Blade, Dolphin Slash, Counter.
// Decomp-first port of refs/melee/src/melee/ft/chara/ftMars/ftMs_Special{N,S,Hi,Lw}.c.
// The live fighter-script cursor owns movescript hitboxes, timing, and command state;
// this module carries only state, counters, transitions, physics, and combat hooks.

// True for the marth char-special action id range (341..372). Callers must gate on
// char_id == MSL_CHAR_ID_MARTH: the numeric range is shared with other characters' specials.
static inline uint8_t marth_action_is_special(uint16_t action_id) {
  return (uint8_t)(action_id >= 341u && action_id <= 372u);
}

// Marth special submotion ids are a fixed 1:1 offset from the action ids
// (ftMs_MS_SpecialNStart=341 <-> anim bank msid 295 ... ftMs_MS_SpecialAirLwHit=372 <-> 326).
// refs/melee/src/melee/ft/chara/ftMars/forward.h
static inline uint16_t marth_special_submotion(uint16_t action_id) {
  return (uint16_t)(295u + (action_id - 341u));
}

// Entry dispatch (B-press routing) + per-action Anim/IASA/transition logic. Runs in the
// action phase alongside shine/blaster updates.
void marth_specials_update_pre_physics(MslBatch* batch);

// Per-player physics for marth special actions. Returns 1 when this module owned the player's
// self-velocity update this frame (the generic physics path must then skip its own).
uint8_t marth_specials_phys(MslBatch* batch, size_t idx);

// Collision-callback ground/air variant swaps (preserve the current animation frame).
// Return 1 when the player's action was swapped; callers own the grounding/floor-loss bundle.
// refs/melee/src/melee/ft/chara/ftMars/ftMs_Special{N,S,Lw}.c (Coll handlers)
uint8_t marth_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);
uint8_t marth_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);

// Counter owns a live ShieldDesc installed by ftColl_8007B1B8. Contact systems query the same
// posed descriptor for fighter and item HitCapsules; the callback consumer below owns the shared
// x19A4 -> SpecialLwHit transition.
// refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{ftMs_SpecialLw_Anim,
//   ftMs_SpecialLw_80139140}
// refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80078C70,ftColl_8007925C}
uint8_t marth_counter_shielddesc_world(const MslBatch* batch, size_t defender_idx, float* out_x,
                                       float* out_y, float* out_z, float* out_radius);
uint8_t marth_counter_try_fighter_contact(MslBatch* batch, int batch_index, int attacker,
                                          int defender, int hitbox_id);
uint8_t marth_counter_apply_item_contact(MslBatch* batch, int batch_index, int attacker,
                                         int defender, uint16_t item_attack_id,
                                         uint16_t item_attack_instance, float damage,
                                         float item_pos_x);
