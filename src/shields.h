#pragma once

#include "batch_internal.h"

// Refresh per-fighter world-space shield bubble geometry.
//
// This is a non-mutating derived-geometry pass:
// - writes batch->state.{shield_{x,y,z},shield_radius}
// - does not change gameplay state (percent/hitlag/hitstun/actions/etc.)
void shields_refresh(MslBatch* batch);

