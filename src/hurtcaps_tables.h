#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MslHurtCap {
  uint16_t bone_part_id; // Fighter_Part id (GALE01); used as anim_pose part_id.
  uint8_t height;        // HurtHeight (decomp: refs/melee/src/melee/lb/types.h)
  uint8_t is_grabbable;  // 0/1
  float a_offset[3];
  float b_offset[3];
  float scale; // capsule radius (decomp: HurtCapsule.scale; refs/melee/src/melee/lb/types.h)
} MslHurtCap;

// Init-time loader for ISO-derived fighter hurt capsule init tables:
// - data/hurtcaps/fox.bin (char_id=1)
// - data/hurtcaps/falco.bin (char_id=22)
int hurtcaps_tables_init(void);

// Hot-path getter for the per-character init capsule records (stable iteration order).
// Returns 0 on success; nonzero on missing/invalid inputs.
int hurtcaps_get(uint8_t char_id, const MslHurtCap** out_caps, uint16_t* out_count);

#ifdef __cplusplus
}  // extern "C"
#endif

