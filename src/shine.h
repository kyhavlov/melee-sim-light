#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Fox/Falco SpecialLw (Reflector / Shine) action logic (decomp-first).
//
// Decomp authority:
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c (Fox)
// - refs/melee/src/melee/ft/chara/ftFalco/ftFc_SpecialLw.c (Falco uses ftFx_* ids)

void shine_update_pre_physics(MslBatch* batch);
void shine_update_post_collision(MslBatch* batch);
// Installed aerial Reflector Coll callback continuation after an accepted floor contact.
uint8_t shine_air_to_ground_collision(MslBatch* batch, size_t idx);
uint8_t shine_char_supports_reflector(uint8_t char_id);
void shine_enter_ground_start_from_iasa(MslBatch* batch, size_t idx);
