#pragma once

#include "batch_internal.h"

// Decomp-shaped grab/capture flow glue (owner/victim motion-state synchronization).
// Called once per frame before physics.
void grab_flow_update_pre_physics(MslBatch* batch);

// Decomp-shaped catch connect transition entry point (called by fighter-vs-fighter catch collision).
// owner_p / victim_p are fighter ports in [0, num_players).
void grab_flow_on_catch_connect(MslBatch* batch, int bi, int owner_p, int victim_p);
