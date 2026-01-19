#pragma once

#include "batch_internal.h"

// Load stage collision data required by stage_collision_apply.
// Must be called during initialization (before stepping); may allocate.
// Returns 0 on success.
//
// Note: stage_collision_apply() must remain allocation-free and must not do any IO or parsing.
// All stage loading allocations must stay inside stage_collision_init().
int stage_collision_init(void);

void stage_collision_apply(MslBatch* batch);
