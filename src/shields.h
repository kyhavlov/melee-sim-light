#pragma once

#include "batch_internal.h"

// Refresh per-fighter world-space shield bubble geometry.
//
// This is a non-mutating derived-geometry pass:
// - writes batch->state.{shield_{x,y,z},shield_radius}
// - does not change gameplay state (percent/hitlag/hitstun/actions/etc.)
void shields_refresh(MslBatch* batch);

// Narrow pre-combat owner for no-submotion angled Guard BODY collision.
// Updates only the live Guard tilt state and shield bubble for rows that need the
// ftCo_Guard_Anim -> ftCo_80091E78 pose before BODY hurtcaps sample.
void shields_refresh_guard_tilt_body_owner(MslBatch* batch);
