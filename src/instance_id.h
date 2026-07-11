#pragma once

#include <stddef.h>

#include "batch_internal.h"

// Fighter action-state instance_id helpers (GALE01 fp+0x2088).
//
// Decomp/asm trail:
// - Global counter semantics (unk_804D6480): refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
// - Motion-state change callsite: refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState) calls
//   ft_800895E0(fp, new_motion_state->x4_flags)
// - Instance-id writer in ft_800895E0: refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
// - Clear/reset writer: refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
// - Additional writer used by Fox/Falco SpecialN OnChangeAction: refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824

// Clear fp->x2088 (instance_id) and related internal gate state (subset of ft_800892D4).
void instance_id_reset_ft_800892D4(MslBatch* batch, size_t idx);

// Consume one plAttack_80037B08 counter value without writing fp->x2088.
// Seed-bridge helper for decomp call chains that can consume the global counter through
// unmodeled internals before the fighter's own ft_800895E0 write.
void instance_id_counter_consume_plAttack_80037B08(MslBatch* batch, size_t idx);

// Update fp->x2088 on true motion-state entry (subset of ft_800895E0 + ft_80089824 for SpecialN).
// This is wired to msl_anim_timebase_enter() (the decomp-shaped Fighter_ChangeMotionState bundle).
void instance_id_on_motion_state_change_ft_800895E0(MslBatch* batch, size_t idx);

// Unconditional fp->x2088 bump (ft_80089824), for OnChangeAction hooks (fp->x21EC) armed at
// specific callback sites (e.g. Purin's SpecialNTurn exit arming ftPr_SpecialS_8013D8B0).
// refs/melee/build/GALE01/asm (ft_80089824: plAttack_80037B08 -> sth ..., 0x2088)
void instance_id_bump_ft_80089824(MslBatch* batch, size_t idx);
