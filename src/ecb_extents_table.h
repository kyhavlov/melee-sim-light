#pragma once

#include <stdint.h>

// Init-only (may allocate): loads `data/ecb/{fox,falco}_extents.bin`.
// Must be called before any msl_ecb_extents_rel() queries.
int ecb_extents_table_init(void);

typedef struct {
  // Fighter-local joint extrema over the 6 ECB source joints (see mpColl_LoadECB_JObj joint loop):
  // - min_x/max_x correspond to left/right extrema.
  // - min_y/max_y correspond to bottom/top extrema.
  //
  // NOTE: These are extracted from fighter-local SSANIM01 v3 matrices with TransN translation removed
  // (tools/extraction/extract_fighter_anims.py). Callers should mirror X based on facing if needed.
  float min_x;
  float max_x;
  float min_y;
  float max_y;
} MslEcbExtentsRel;

// Returns fighter-local extents for a given (char_id, animation_index, action_frame).
// - `animation_index` is expected to be a submotion_id/msid (Slippi post-frame `animation_index`).
// - `action_frame` is clamped into [0, frame_count-1] for the msid.
// - If animation_index == 0xFFFFFFFF or missing entry: returns all zeros.
// - Allocation-free.
MslEcbExtentsRel msl_ecb_extents_rel(uint8_t char_id, uint32_t animation_index, int action_frame);

// Convenience helpers (allocation-free).
float msl_ecb_left_rel_x(uint8_t char_id, uint32_t animation_index, int action_frame);
float msl_ecb_right_rel_x(uint8_t char_id, uint32_t animation_index, int action_frame);
float msl_ecb_bottom_rel_y_ext(uint8_t char_id, uint32_t animation_index, int action_frame);
float msl_ecb_top_rel_y(uint8_t char_id, uint32_t animation_index, int action_frame);

