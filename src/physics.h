#pragma once

#include "batch_internal.h"

void physics_integrate(MslBatch* batch);
void physics_apply_attackdash_downbound_overlap_nudge_post_collision(MslBatch* batch);
uint8_t physics_apply_attackdash_entry_phys_now(MslBatch* batch, size_t idx, float facing_dir);
