#pragma once

#include "batch_internal.h"
#include "common_params.h"

// Decomp-shaped grab/capture flow glue (owner/victim motion-state synchronization).
// Called once per frame before physics.
void grab_flow_update_pre_physics(MslBatch* batch);

// Decomp-shaped Catch input check subset used by grounded IASA call sites.
// Returns 1 if the fighter entered Catch on this call.
uint8_t grab_flow_try_enter_catch_from_iasa(MslBatch* batch, const MslCommonParams* c, size_t idx);

// Decomp-shaped catch connect transition entry point (called by fighter-vs-fighter catch collision).
// owner_p / victim_p are fighter ports in [0, num_players).
void grab_flow_on_catch_connect(MslBatch* batch, int bi, int owner_p, int victim_p);
