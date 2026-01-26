#pragma once

#include "batch_internal.h"

// Cliff / ledge action-state updates (FD v1).
//
// Decomp entry points:
// - Catch check: refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
// - Enter CliffCatch: refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
// - CliffWait IASA ordering: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA
//
// Ordering contract:
// - ledge_update_pre_physics() runs after inputs are applied and before physics integration.
// - ledge_try_catch_post_collision() runs after stage collision (grounding) and before post-physics
//   motion transitions.

void ledge_update_pre_physics(MslBatch* batch);
void ledge_try_catch_post_collision(MslBatch* batch);
