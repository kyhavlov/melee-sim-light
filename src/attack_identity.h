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
// NOTE: Additional bump sites like refs/melee/src/melee/ft/ft_0881.c::ft_800892A0 are not modeled
// here unless tied to an explicit, observable event in the simulator.

void attack_identity_reset_ft_800890BC(MslBatch* batch, size_t idx);

// Call after action_id has been changed (motion state entered). This is typically invoked from
// msl_anim_timebase_enter() (the decomp-shaped Fighter_ChangeMotionState bundle).
//
// IMPORTANT: do not call this directly from gameplay logic; use msl_anim_timebase_enter() for
// motion-state changes and msl_anim_timebase_restart() for pure animation restarts.
void attack_identity_on_motion_state_change_ft_800890D0(MslBatch* batch, size_t idx);
