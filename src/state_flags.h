#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct MslBatch MslBatch;

// Slippi packs 5 bytes into `state_flags[..., 5]` from fighter offsets:
// (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
enum { MSL_STATE_FLAGS_BYTES = 5 };
enum { MSL_STATE_FLAGS_2218_INDEX = 0 };
enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
enum { MSL_STATE_FLAGS_221B_INDEX = 2 };
enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
enum { MSL_STATE_FLAGS_221F_INDEX = 4 };

// fp+0x2218.
// refs/melee/src/melee/ft/types.h (fp+0x2218 bitfield layout)
enum { MSL_STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80 };
enum { MSL_STATE_FLAG_2218_B1 = 0x40 };
enum { MSL_STATE_FLAG_2218_B2 = 0x20 };
enum { MSL_STATE_FLAG_2218_REFLECTING = 0x10 };
enum { MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR = 0x04 };

// fp+0x221A.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
// refs/melee/src/melee/ft/types.h (fp+0x221A bitfield layout)
enum { MSL_STATE_FLAG_221A_B7 = 0x01 };
enum { MSL_STATE_FLAG_221A_B5 = 0x04 };
enum { MSL_STATE_FLAG_221A_IS_FASTFALL = 0x08 };
enum { MSL_STATE_FLAG_221A_B3 = 0x10 };
enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };

// fp+0x221B. Bit numbering is MSB-first in the GALE01/Slippi packing
// (b0==0x80 ... b5==0x04).
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
// refs/melee/src/melee/ft/types.h (fp+0x221B bitfield layout)
enum { MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE = 0x80 };
enum { MSL_STATE_FLAG_221B_B1 = 0x40 };
enum { MSL_STATE_FLAG_221B_B5 = 0x04 };

// fp+0x221C.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
// refs/melee/src/melee/ft/types.h (fp+0x221C bitfield layout)
enum { MSL_STATE_FLAG_221C_B0 = 0x80 };
enum { MSL_STATE_FLAG_221C_B1 = 0x40 };
enum { MSL_STATE_FLAG_221C_B2 = 0x20 };
enum { MSL_STATE_FLAG_221C_B3 = 0x10 };
enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };
enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
enum { MSL_STATE_FLAG_221C_IN_DAMAGE = 0x01 };

// fp+0x221F.
// refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
// refs/melee/src/melee/ft/types.h (fp+0x221F bitfield layout)
enum { MSL_STATE_FLAG_221F_B0 = 0x80 };
enum { MSL_STATE_FLAG_221F_B1 = 0x40 };
enum { MSL_STATE_FLAG_221F_B3 = 0x10 };
enum { MSL_STATE_FLAG_221F_B4 = 0x08 };

// Internal magnifying-glass replay-local episode owner. This is not a Slippi bit; it records which
// source-visible start predicate created a local x1910 counter so carry can stay bounded.
enum { MSL_MAGNIFY_LOCAL_EPISODE_NONE = 0 };
enum { MSL_MAGNIFY_LOCAL_EPISODE_DAMAGEFLYTOP_REFLECT = 1 };
enum { MSL_MAGNIFY_LOCAL_EPISODE_DAMAGEFLY_VISIBLE_EDGE = 2 };
enum { MSL_MAGNIFY_LOCAL_EPISODE_NO_INSIDE_CARRY = 3 };

// Refresh a small subset of Slippi post-frame `state_flags` bits that are derived from
// sim-owned state each step.
//
// This helper intentionally only mutates specific bits and preserves all other bits in each
// byte, so seed passthrough remains valid for not-yet-modeled fields.
void state_flags_refresh_post_frame(MslBatch* batch);
void state_flags_refresh_post_frame_masked(MslBatch* batch, const uint8_t* mask_bytes,
                                           size_t mask_stride_bytes);
void state_flags_refresh_camera_targets_pre_physics(MslBatch* batch);

static inline uint8_t msl_state_flags_221c_hitstun_at(const uint8_t* state_flags, size_t idx) {
  if (state_flags == NULL) {
    return 0u;
  }
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  return (state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1u : 0u;
}
