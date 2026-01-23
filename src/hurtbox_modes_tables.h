#pragma once

#include <stdint.h>

// Init-time loader for movescript-derived hurt capsule modes (enabled/disabled/intangible),
// keyed by (char_id, msid, integer action_frame).
//
// Data source: `data/hurtbox_states/<char>.bin` (MSLHURM1 v1).
int hurtbox_modes_tables_init(void);

// Compute a "can be hit" mask for BODY collision checks.
// - Bit i corresponds to hurt capsule i in `data/hurtcaps/<char>.bin` order.
// - A bit value of 1 means the capsule is eligible for BODY contacts.
//
// If there is no table entry for (char_id, msid), this returns "all enabled" for the requested
// cap_count.
int hurtbox_modes_can_hit_mask(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t cap_count,
                               uint32_t* out_mask);

