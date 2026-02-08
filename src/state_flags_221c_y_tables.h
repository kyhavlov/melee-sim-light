#pragma once

#include <stdint.h>

// Init-time loader for movescript-derived fp->x221C_u16_y timelines (opcode 52),
// keyed by (char_id, msid, frame).
//
// Data source: `data/state_flags_221c_y/<char>.bin` (MSLSF3Y1 v1).
// Optional table: missing files are tolerated so batch init remains usable without this artifact.
int state_flags_221c_y_tables_init(void);

// Returns 0 on success.
// - Missing/unloaded table or missing msid returns 1 and leaves out_flags at 0.
// - msid/frame out of range are clamped similarly to other timeline tables.
int state_flags_221c_y_get(uint8_t char_id, uint16_t msid, uint16_t frame, uint8_t* out_flags);
