#pragma once

#include "batch_internal.h"

// Packet-1 exact live-handler dispatch for common grounded Coll callbacks.
void mpcoll_source_ground_apply(MslBatch* batch);

// Run one freshly installed grounded Coll callback in source order. Used by source callbacks that
// change MotionState and immediately invoke the destination `coll_cb` before the global map pass.
uint8_t mpcoll_source_ground_run_installed_callback(MslBatch* batch, int bi, int p);
