#pragma once

#include <stdint.h>

// Decomp: ft_800890D0 updates fp->x2068_attackID using the `move_id` field of the MotionState
// selected by the new action id (msid) during Fighter_ChangeMotionState.
// refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
// refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState call site)
//
// This module loads a compact decomp-derived mapping:
//   (char_id, action_id) -> FtMoveId (aka "move_id")
//
// Artifacts:
//   data/attack_id/move_id/{fox,falco}.bin
int attack_id_tables_init(void);

// Return FtMoveId for the fighter's current action_id. If the character is unknown or the table
// is missing/out-of-range, returns FtMoveId_Default (1).
uint16_t attack_id_move_id_from_action(uint8_t char_id, uint16_t action_id);

