#pragma once

#include "batch_internal.h"

// Decomp-shaped grab/capture flow glue (owner/victim motion-state synchronization).
// Called once per frame before physics.
void grab_flow_update_pre_physics(MslBatch* batch);

