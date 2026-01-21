#pragma once

#include <stdint.h>

// Load per-character ECB bottom lookup tables from `data/ecb/*.bin`.
// This is init-only and may allocate; hot-path queries must remain allocation-free.
int ecb_table_init(void);

// Returns the fighter-local ECB bottom Y offset for a given (char_id, animation_index, action_frame).
//
// - `char_id` follows Slippi post-frame `character` (GALE01), e.g. Fox=1, Falco=22.
// - `animation_index` is expected to be a submotion_id/msid (Slippi post-frame `animation_index`).
// - `action_frame` is the integer action frame counter (`state_age` / `action_frame`).
//
// Edge cases:
// - If animation_index == 0xFFFFFFFF or missing entry: returns 0.0f.
// - Clamps action_frame to [0, frame_count-1].
float msl_ecb_bottom_rel_y(uint8_t char_id, uint32_t animation_index, int action_frame);

