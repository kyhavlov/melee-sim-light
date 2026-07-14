#pragma once

#include "batch_internal.h"
#include "common_params.h"

// Run one live fighter's grab/capture IASA callback in priority-3 fighter-list order.
void grab_flow_update_iasa(MslBatch* batch, int batch_index, int player);

// Run one live fighter's grab/capture Anim callback in priority-1 fighter-list order.
void grab_flow_update_anim_callback_pre_input(MslBatch* batch, int batch_index, int player);

// Accepted attached BODY damage enters/restarts CaptureDamage from Fighter_ProcessHit, not from
// HitCapsule creation. Returns 1 when the live capture motion was replaced.
uint8_t grab_flow_on_attached_body_damage(MslBatch* batch, size_t victim_idx);

// Refresh the explicit ftCommon_8007E2D0/ftCommon_8007E2F4 catch descriptor lanes from generated
// MotionState owner classes. Called after action transitions and before catch collision selection.
void grab_flow_refresh_catch_contract(MslBatch* batch);
void grab_flow_refresh_catch_contract_for_batch_index(MslBatch* batch, int batch_index);

// Decomp-shaped Catch input check subset used by grounded IASA call sites.
// Returns 1 if the fighter entered Catch on this call.
uint8_t grab_flow_try_enter_catch_from_iasa(MslBatch* batch, const MslCommonParams* c, size_t idx);

// Decomp-shaped CatchDash input check subset used by grounded dash/run IASA call sites.
// Returns 1 if the fighter entered CatchDash on this call.
uint8_t grab_flow_try_enter_catchdash_from_iasa(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx);

// AttackDash pre-gate entry helper (ftCo_800D8AE0 -> ftCo_800D8C54 with CatchDash msid).
// refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_800D8AE0,ftCo_800D8C54}
void grab_flow_enter_catchdash_from_attackdash_pregate(MslBatch* batch, size_t idx);

// Decomp-shaped catch connect transition entry point (called by fighter-vs-fighter catch collision).
// owner_p / victim_p are fighter ports in [0, num_players).
void grab_flow_on_catch_connect(MslBatch* batch, int bi, int owner_p, int victim_p);
