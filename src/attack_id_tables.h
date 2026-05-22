#pragma once

#include <stdint.h>

// Decomp: ft_800890D0 updates fp->x2068_attackID using the `move_id` field of the MotionState
// selected by the new action id (msid) during Fighter_ChangeMotionState.
// refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
// refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState call site)
//
// This module loads a compact decomp-derived mapping:
//   (char_id, action_id) -> FtMoveId (aka "move_id")
// and adjacent MotionState lanes from the same decomp table.
//
// Artifacts:
//   data/attack_id/move_id/{fox,falco}.bin
int attack_id_tables_init(void);

// Binary artifact schema version expected by this runtime.
uint32_t attack_id_tables_format_version(void);

// Return FtMoveId for the fighter's current action_id. If the character is unknown or the table
// is missing/out-of-range, returns FtMoveId_Default (1).
uint16_t attack_id_move_id_from_action(uint8_t char_id, uint16_t action_id);

// Return decomp MotionState.x4_flags (GALE01) for the fighter's current action_id.
// If the character is unknown or the table is missing/out-of-range, returns 0.
uint32_t attack_id_x4_flags_from_action(uint8_t char_id, uint16_t action_id);

// Return decomp MotionState +0x8 word (move_id in high byte plus x9 bitfields) for the fighter's
// current action_id. If the character is unknown or the table is missing/out-of-range, returns 0.
uint32_t attack_id_motion_state_word_from_action(uint8_t char_id, uint16_t action_id);
