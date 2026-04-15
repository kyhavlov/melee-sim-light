#pragma once

#include "batch_internal.h"
#include "common_params.h"

// Decomp-shaped grab/capture flow glue (owner/victim motion-state synchronization).
// Called once per frame before physics.
void grab_flow_update_pre_physics(MslBatch* batch);

// Decomp-shaped grab/capture Anim-callback ownership before current-frame input.
void grab_flow_update_anim_callbacks_pre_input(MslBatch* batch);

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
