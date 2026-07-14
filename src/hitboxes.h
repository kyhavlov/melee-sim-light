#pragma once

#include "batch_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// Publishes the persistent x58 -> x4C HitCapsule endpoints consumed by fighter/item contact.
void hitboxes_refresh(MslBatch* batch);

// Teacher-forced boundary initialization for the same live HitCapsule object. Script reseed
// reconstructs payload/state first; this publishes the authoritative seed-frame x4C endpoint so
// the next ordinary refresh advances x58 -> x4C instead of inventing a fresh-enable point.
void hitboxes_reseed_player(MslBatch* batch, size_t idx);

// Runtime source owner for Fighter_ChangeMotionState's ftColl_8007AFF8 clear path.
void hitboxes_clear_player_active(MslBatch* batch, int bi, int p);

#ifdef __cplusplus
}  // extern "C"
#endif
