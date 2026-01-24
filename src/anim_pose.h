#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init-time loader for SSANIM01 v3 animation matrix blobs:
// - data/anims/fox.bin (char_id=1)
// - data/anims/falco.bin (char_id=22)
//
// Layout source pointers:
// - tools/extraction/extract_ecb_bottom.py (header + payload walk)
// - tools/extraction/extract_ecb_extents.py (matrix record layout)
// - tools/extraction/extract_fighter_anims.py (writer: SSANIM01 v3 + per-frame TransN tail)
int anim_pose_init(void);

// Test/debug helper: reset global pose tables so a subsequent anim_pose_init() reloads from the
// current MSL_DATA_DIR. This exists only for fast synthetic tests; it must not be used while any
// live batches are depending on pose data.
void anim_pose_reset_for_tests(void);

// Hot-path matrix sampler.
// Reads the 3x4 matrix for (char_id, msid, frame, part_id) into out_3x4 in row-major order:
//   (m00 m01 m02 tx, m10 m11 m12 ty, m20 m21 m22 tz)
//
// Returns 0 on success; nonzero on missing/invalid inputs.
int anim_pose_get_matrix(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t part_id,
                         float out_3x4[12]);

#ifdef __cplusplus
}  // extern "C"
#endif
