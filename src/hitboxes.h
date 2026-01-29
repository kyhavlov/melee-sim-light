#pragma once

#include "batch_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// Refreshes per-frame world-space hitbox centers for debug readback.
// No combat resolution uses these values yet (SIM-only / no gameplay behavior change).
void hitboxes_refresh(MslBatch* batch);

#ifdef __cplusplus
}  // extern "C"
#endif
