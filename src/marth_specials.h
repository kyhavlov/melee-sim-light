#pragma once

#include <stdint.h>

#include "api.h"

// Marth (ftMs_*) character specials: Shield Breaker, Dancing Blade, Dolphin Slash, Counter.
// Decomp-first port of refs/melee/src/melee/ft/chara/ftMars/ftMs_Special{N,S,Hi,Lw}.c.
// Movescripts own hitboxes/timing/windows (data/moves + data/scripts via move_tables);
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
