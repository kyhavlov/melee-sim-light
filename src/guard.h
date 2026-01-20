#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "common_params.h"

// Grounded shield / guard core loop (entry -> hold -> exit).
//
// Decomp references:
// - Entry check + powershield (GuardReflect) gate:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-70 (ftCo_80091A4C)
// - Shield hold drain formula + clamp-to-0:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:333-365 (ftCo_800925A4)
// - GuardOff transition on release:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509 (ftCo_80092BCC / ftCo_80092C54)
// - GuardOff -> Wait when anim ends:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:511-519 (ftCo_GuardOff_Anim)
//
// Shield recharge (when not shielding) is handled separately in guard_update_shield_recharge().
// Decomp: refs/melee/src/melee/ft/fighter.c:2803-2811 (Fighter_ProcessHit_8006D1EC).

// Per-frame shield recharge update (applies to both ground and air).
// Minimal gating: only when not in any guard motion state.
void guard_update_shield_recharge(MslBatch* batch, const MslCommonParams* c, size_t idx);

// Per-frame grounded guard update. If allow_entry is non-zero, guard entry checks are allowed.
void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx, uint8_t allow_entry);

