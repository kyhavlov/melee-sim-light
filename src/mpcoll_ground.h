#pragma once

#include "batch_internal.h"

// mpColl-style ground contact substrate (FD-only v1).
//
// Owns: state.on_ground, state.ground_id, and collision contact metadata fields.
//
// Decomp pointers:
// - Collision runs post-integration and is consumed by action logic:
//   refs/melee/src/melee/ft/fighter.c::Fighter_procMap (prio 6) and docs/DECOMP_PROC_ORDER.md.
// - ECB prev/current and floor persistence live in CollData:
//   refs/melee/src/melee/lb/types.h::CollData.
void mpcoll_ground_apply(MslBatch* batch);
