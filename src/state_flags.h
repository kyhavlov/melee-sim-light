#pragma once

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
