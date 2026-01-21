#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "common_params.h"

// EscapeAir (airdodge) entry + per-frame updates.
//
// Decomp references:
// - Entry check: ftCo_80099A58
// - Enter: ftCo_80099A9C (deadzone, force, timer)
// - Angle helper: ftCommon_8007D9D4 (atan2f(y, x))
// - Phys decay: ftCo_EscapeAir_Phys uses p_ftCommonData->escapeair_decay
// - Anim end -> FallSpecial: ftCo_EscapeAir_Anim calls ftCo_80096900(..., x340, x344)
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c and ftcommon.c

// Attempt to enter EscapeAir (airdodge). Returns 1 if entered.
//
// Contract: only call from eligible airborne locomotion states; this helper only checks L/R press.
uint8_t escape_air_try_enter_from_air_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                 size_t idx);

// Per-frame EscapeAir update: apply velocity decay and handle anim-end -> FallSpecial transition.
void escape_air_update(MslBatch* batch, const MslCommonParams* c, size_t idx);

