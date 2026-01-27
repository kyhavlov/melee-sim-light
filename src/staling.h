#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Decomp: FtMoveId enum order has `FtMoveId_Default` as the second entry (value 1), and staling
// treats move_id==1 as "do not stale".
// refs/melee/src/melee/ft/forward.h::FtMoveId
// refs/melee/src/melee/ft/ft_0881.c::ft_80089118
enum { MSL_FT_MOVE_ID_DEFAULT = 1 };

// Compute the staling multiplier for `move_id` given the current fighter's stale queue.
//
// Decomp reference (GALE01): ft_80089118 / ft_80089228.
// - consults the previous 9 stale entries starting from (current_index - 1),
// - subtracts Fighter_804D6548[i] for each occurrence in position i (0..8),
// - stops early on move_id==0 ("empty") entries.
// refs/melee/src/melee/ft/ft_0881.c::ft_80089118 and ::ft_80089228
//
// Returns 1.0 if required staling tables are not loaded or if move_id is not stalable.
float staling_multiplier_for_move(const MslBatch* batch, size_t fighter_idx, uint16_t move_id);

// Update the stale queue for a successful damaging hit attributed to (fighter_idx, move_id, attack_instance).
//
// Decomp reference (GALE01): plStale_UpdateStaleMovesFromFighter / ...FromItem
// - ignores move_id==FtMoveId_Default (1),
// - ignores duplicates of the exact (move_id, attack_instance) pair already present in the table,
// - writes into current_index and then advances current_index with wrap at 9.
// refs/melee/src/melee/pl/plstale.c
void staling_queue_update(MslBatch* batch, size_t fighter_idx, uint16_t move_id,
                          uint16_t attack_instance);

// Convenience helper: map current (char_id, animation_index low16) to a FtMoveId move_id.
// Returns 0xFFFF if unknown.
uint16_t staling_move_id_from_state(const MslBatch* batch, size_t fighter_idx);
