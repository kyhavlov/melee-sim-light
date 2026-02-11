#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Refresh a small subset of Slippi post-frame `state_flags` bits that are derived from
// sim-owned state each step.
//
// Slippi packs 5 bytes into `state_flags[..., 5]` from fighter offsets:
// (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
//
// This helper intentionally only mutates specific bits and preserves all other bits in each
// byte, so seed passthrough remains valid for not-yet-modeled fields.
void state_flags_refresh_post_frame(MslBatch* batch);

// Slippi packs fp+0x221C into state_flags[..., 3].
// x221C_b6 is the high-byte bit1 lane (mask 0x02).
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
// refs/melee/src/melee/ft/types.h (fp+0x221C bitfield layout)
enum { MSL_STATE_FLAGS_221C_BYTE_INDEX = 3 };
enum { MSL_STATE_FLAGS_221C_B6_MASK = 0x02 };

static inline uint8_t msl_state_flags_221c_b6_at(const uint8_t* state_flags, size_t idx) {
  if (state_flags == NULL) {
    return 0u;
  }
  const size_t flags_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_BYTE_INDEX;
  return (state_flags[flags_i] & (uint8_t)MSL_STATE_FLAGS_221C_B6_MASK) ? 1u : 0u;
}
