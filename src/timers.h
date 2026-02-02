#pragma once

#include "batch_internal.h"

// Update per-frame timers (hitlag/hitstun) with Melee-like semantics.
// See `src/timers.c` for decomp references and ordering notes.
void timers_update(MslBatch* batch);

// Post-anim per-frame timer updates (Fighter_8006A360 under the non-hitlag gate):
// - Combo timer tick + victim clear (ftColl_800764DC)
// - Hitstun decrement + end effects (ftCo_8008F744 family)
void timers_update_post_anim(MslBatch* batch);
