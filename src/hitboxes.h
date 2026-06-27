#pragma once

#include "batch_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// Refreshes per-frame world-space hitbox centers for debug readback.
// No combat resolution uses these values yet (SIM-only / no gameplay behavior change).
void hitboxes_refresh(MslBatch* batch);

// Runtime source owner for Fighter_ChangeMotionState's ftColl_8007AFF8 clear path.
void hitboxes_clear_player_active(MslBatch* batch, int bi, int p);

#ifdef __cplusplus
}  // extern "C"
#endif
