#pragma once

#include <stdint.h>

// Init-time loader for per-animation end frames (FigaTree.frames) for a subset of submotions.
//
// Source of truth: `data/anims/<character>.tracks.bin` (ISO-derived).
// Extractor: `tools/extraction/extract_fighter_anims.py` (decomp-first).
//
// IMPORTANT: anim_table_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

int anim_table_init(void);

// Return end_frame for the given character + submotion id (anim_id).
// Returns 0.0f if unknown/unloaded.
float msl_anim_end_frame(uint8_t char_id, uint16_t submotion_id);
