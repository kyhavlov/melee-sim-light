#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "char_params.h"
#include "common_params.h"

// Action/state transitions + per-action callbacks (non-physics).
// Called once per frame in the scheduler, before physics.
void action_update(MslBatch* batch);

// Pre-input Anim-callback phase (decomp-shaped prio 1 callbacks that do not depend on current-frame
// input edge processing). Runs after anim timebase/timers pre-input updates and before input_apply().
void action_update_anim_callbacks_pre_input(MslBatch* batch);

// ----------------
// Guard / shielding
// ----------------
//
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
void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx,
                           uint8_t allow_entry);

// ---------------------------
// Shield defensive options
// ---------------------------
//
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

// ---------
// EscapeAir
// ---------
//
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
