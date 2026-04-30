#pragma once

#include <stddef.h>

#include "batch_internal.h"

// Fighter attack identity (decomp-first).
//
// Mirrors GALE01 fields:
// - fp->x2068_attackID ("attack id" / move id domain)
// - fp->x206C_attack_instance (dup suppression key; value from plStale_IncrementAttackInstance)
//
// Decomp trail:
// - Reset/default: refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
// - Update on motion change: refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
// - Call site: refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState calls ft_800890D0(fp, new_motion_state->move_id))
//
// Additional same-move bump sites are modeled only where the source action callback explicitly
// calls them (e.g. Attack100Loop_Anim -> ft_800892A0 on loop restart).

void attack_identity_reset_ft_800890BC(MslBatch* batch, size_t idx);

// Call after action_id has been changed (motion state entered). This is typically invoked from
// msl_anim_timebase_enter() (the decomp-shaped Fighter_ChangeMotionState bundle).
//
// IMPORTANT: do not call this directly from gameplay logic; use msl_anim_timebase_enter() for
// motion-state changes and msl_anim_timebase_restart() for pure animation restarts.
void attack_identity_on_motion_state_change_ft_800890D0(MslBatch* batch, size_t idx);

// Source-shaped same-move restart bump:
// - ft_800892A0 calls inlineC0(fp, fp->x2068_attackID), which increments x206C only when the
//   supplied move id equals the current attack id.
// - Attack100Loop_Anim calls this when cur_anim_frame is in the first-frame restart band.
// refs/melee/src/melee/ft/ft_0881.c::ft_800892A0
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim
void attack_identity_restart_same_move_ft_800892A0(MslBatch* batch, size_t idx);
