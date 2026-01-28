#pragma once

#include "batch_internal.h"

// Fox/Falco SpecialLw (Reflector / Shine) action logic (decomp-first).
//
// Decomp authority:
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c (Fox)
// - refs/melee/src/melee/ft/chara/ftFalco/ftFc_SpecialLw.c (Falco uses ftFx_* ids)

void shine_update_pre_physics(MslBatch* batch);
void shine_update_post_collision(MslBatch* batch);

