#pragma once

#include "batch_internal.h"

// Refresh per-fighter world-space shield bubble geometry.
//
// This is a non-mutating derived-geometry pass:
// - writes batch->state.{shield_{x,y,z},shield_radius}
// - does not change gameplay state (percent/hitlag/hitstun/actions/etc.)
void shields_refresh(MslBatch* batch);

// ftCo_80091BC4 owner: advance mv.co.guard.{x8,x4} from the callback-visible (pre-input)
// controller snapshot. Geometry refresh consumes this state but never advances it.
void shields_guard_anim_update_tilt(MslBatch* batch, size_t idx);
