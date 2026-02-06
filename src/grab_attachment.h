#pragma once

#include "batch_internal.h"

// Initialize per-fighter attachment offsets on reseed.
// Must be called after `batch->state` has been populated from `MslSeed`.
void grab_attachment_reseed_init(MslBatch* batch, int batch_index);

// Recompute victim attachment offsets at Throw->Thrown entry without moving victim world position.
//
// Decomp-shaped usage:
// - Call after Throw/Thrown action + submotion are installed and msl_anim_timebase_enter() runs,
//   so offsets preserve world-space position across motion-state entry.
void grab_attachment_recompute_offsets_for_thrown_entry(MslBatch* batch, int batch_index,
                                                        int victim_p, int owner_p);

// CapturePulled*/CaptureWait*/CaptureDamage* victim Phys driver:
// - Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
// - Applies `cur_pos += (owner(x18) - victim(XRotN))`.
//
// Ordering contract:
// - Call after action update and before physics integration and stage collision, matching the
//   decomp callback ordering (Phys then integrate then Coll).
void grab_attachment_update_pre_collision(MslBatch* batch);

// Decomp-shaped "accessory callback"-style update: drive captured/thrown victim position from the
// grab owner joint + per-victim offsets.
//
// Thrown* anchor proxy contract:
// - Approximate ftCo_800DE508 by composing owner capture-anchor world + victim XRotN local.
// - Apply x1A70.z/x1A70.y-style offsets onto pos_x/pos_y after that anchor is resolved.
// - Decomp refs:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
//
// Ordering contract:
// - Call after stage collision so collision does not perturb attached victims.
// - Call before hitbox/hurtbox refresh so pose-driven primitives are placed at the attached position.
void grab_attachment_update_post_collision(MslBatch* batch);
