#pragma once

#include <stdint.h>

// Init-time loader for movescript-derived hit status timelines (opcode 26),
// keyed by (char_id, msid, integer action_frame).
//
// Data source: `data/hit_status/<char>.bin` (MSLHSTA1 v1).
int hit_status_tables_init(void);

// Lookup hit status (u8) for (char_id, msid, action_frame).
//
// Fallback policy:
// - If there is no table entry for (char_id, msid), this returns status=0 ("normal").
// - If frame >= frame_count in file, clamps to the last frame.
int hit_status_get(uint8_t char_id, uint16_t msid, uint16_t frame, uint8_t* out_status);

