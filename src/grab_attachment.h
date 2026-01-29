#pragma once

#include "batch_internal.h"

// Initialize per-fighter attachment offsets on reseed.
// Must be called after `batch->state` has been populated from `MslSeed`.
void grab_attachment_reseed_init(MslBatch* batch, int batch_index);

// Decomp-shaped "accessory callback"-style update: drive captured/thrown victim position from the
// grab owner joint + per-victim offsets.
//
// Ordering contract:
// - Call after stage collision so collision does not perturb attached victims.
// - Call before hitbox/hurtbox refresh so pose-driven primitives are placed at the attached position.
void grab_attachment_update_post_collision(MslBatch* batch);
