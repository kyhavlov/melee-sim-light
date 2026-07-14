#pragma once

#include "batch_internal.h"

// Refresh the complete live per-fighter ReflectDesc packet and world-space sphere.
//
// Decomp trail:
// - refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_CreateReflectHit
// - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C

void reflector_bubbles_refresh(MslBatch* batch);
