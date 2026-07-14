#pragma once

#include "batch_internal.h"

// Update per-frame timers (hitlag/hitstun) with Melee-like semantics.
// See `src/timers.c` for decomp references and ordering notes.
void timers_update(MslBatch* batch);

// Consume prio-0 post-hitlag callbacks before current-frame input processing.
// Current modeled subset:
// - `ftCo_Damage_OnExitHitlag` ASDI/DI/LSI ownership
void timers_consume_post_hitlag_callbacks_pre_input(MslBatch* batch);

// Post-anim per-frame timer updates (Fighter_8006A360 under the non-hitlag gate):
// - Combo timer tick + victim clear (ftColl_800764DC)
// - Hitstun decrement + end effects (ftCo_8008F744 family)
void timers_update_post_anim(MslBatch* batch);
void timers_update_post_anim_fighter(MslBatch* batch, int bi, int p);

// Consume decomp-shaped damage hitlag callbacks that depend on current-frame inputs.
// Current modeled subset:
// - `ftCo_Damage_OnEveryHitlag` SDI stick displacement
void timers_consume_post_hitlag_callbacks_after_input(MslBatch* batch);

// Post-frame Fighter_procUpdate magnifying-glass/offscreen damage counter (`fp->dmg.x1910`).
void timers_update_magnify_damage_post_frame(MslBatch* batch);
