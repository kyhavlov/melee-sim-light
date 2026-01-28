#pragma once

#include "batch_internal.h"

// Refresh per-fighter world-space reflector (SpecialLw) bubble geometry.
//
// This models ftColl_CreateReflectHit() + per-frame pose update for the reflector sphere
// (not powershield).
//
// Decomp trail:
// - refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_CreateReflectHit

void reflector_bubbles_refresh(MslBatch* batch);

