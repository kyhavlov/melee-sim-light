#pragma once

#include "batch_internal.h"

// Update per-frame timers (hitlag/hitstun) with Melee-like semantics.
// See `src/timers.c` for decomp references and ordering notes.
void timers_update(MslBatch* batch);

