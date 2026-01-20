#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"

// Grounded shield defensive options (spotdodge / rolls).
//
// Decomp references:
// - Guard IASA ordering calls spotdodge then roll:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_IASA
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA
// - Entry gates:
//   - Spotdodge: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
//     - lstick.y <= p_ftCommonData->x314 and x671_timer_lstick_tilt_y < p_ftCommonData->x318
//     - OR cstick.y <= p_ftCommonData->x314 (ftCo_800DF8E8)
//   - Roll: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009917C
//     - ABS(lstick.x) >= p_ftCommonData->x31C and x670_timer_lstick_tilt_x < p_ftCommonData->x320
//     - OR ABS(cstick.x) >= p_ftCommonData->x31C (ftCo_800DF8B0)
// - Escape motion state entry: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_800998EC
//   and refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099314
// - Escape end -> Wait: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim and
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim

// Called while shielding (GuardOn/Guard/GuardReflect) to attempt entering a grounded escape action.
// Returns 1 if an escape action was entered.
uint8_t escape_try_enter_from_guard(MslBatch* batch, const MslCommonParams* c, size_t idx);

// Per-frame grounded escape update (friction + anim-end return-to-Wait).
void escape_update_grounded(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                            size_t idx);

