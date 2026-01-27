#pragma once

#include <stddef.h>
#include <stdint.h>

// Init-time loader for staling-related tables (move_id mapping + stale weights).
//
// IMPORTANT: staling_tables_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.
//
// Current policy: treat missing data artifacts as non-fatal and leave the table "unloaded".
int staling_tables_init(void);

// Lookup ISO/decomp-derived `FtMoveId` (staling attribution id) for a (char_id, msid) pair.
//
// - `msid` is the GALE01 submotion id (Slippi `animation_index` low 16 bits).
// - Returns 0xFFFF if there is no mapping or if the mapping is ambiguous.
uint16_t staling_move_id_from_msid(uint8_t char_id, uint16_t msid);

// Returns a pointer to the stale-damage weight table (length 9), or NULL if not loaded.
//
// Decomp: `Fighter_804D6548[i]` for i=0..8 in ft_80089118.
// refs/melee/src/melee/ft/ft_0881.c::ft_80089118
const float* staling_weights_table(void);

