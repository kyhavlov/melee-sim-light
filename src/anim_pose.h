#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct MslBatch MslBatch;

#ifdef __cplusplus
extern "C" {
#endif

// Init-time loader for SSANIM01 v4 animation matrix blobs:
// - data/anims/fox.bin (char_id=1)
// - data/anims/falco.bin (char_id=22)
//
// Layout source pointers:
// - tools/extraction/extract_ecb_bottom.py (header + payload walk)
// - tools/extraction/extract_ecb_extents.py (matrix record layout)
// - tools/extraction/extract_fighter_anims.py (writer: SSANIM01 v4 + per-frame TransN tail)
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

// Reads the local JObj translation for (char_id, msid, frame, part_id) into out_xyz.
// This is intentionally distinct from anim_pose_get_matrix(), whose translation is already
// parent-composed. Attachment code uses this for decomp fields such as fp->x1A70 that are stored
// from local/base JObj offsets rather than final lb_8000B1CC world positions.
int anim_pose_get_local_translation(uint8_t char_id, uint16_t msid, uint16_t frame,
                                    uint16_t part_id, float out_xyz[3]);

// Collision-pose matrix sampler.
//
// This wraps SSANIM01 with the live fighter dynamics owner for dynamic JObj chains that feed
// BODY hit/hurt primitives before ftColl_80078C70/lbColl_8000805C. It must remain data/decomp
// backed; unsupported dynamic states fall back to the base SSANIM matrix rather than widening
// collision geometry.
//
// Decomp owner path:
// - refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CF84,ftCo_8009DD94,ftCo_8009E318}
// - refs/melee/src/melee/lb/lb_00F9.c::{lb_8000FD48,lb_80011710,lb_8001044C}
// - refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
void anim_pose_update_dynamic_state(MslBatch* batch);

int anim_pose_get_collision_matrix(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                   uint16_t frame, uint16_t part_id, float out_3x4[12]);

// Float-frame collision-pose matrix sampler.
//
// This uses the extracted SSANIMT1 FObj track streams to evaluate the same AObj/JObj local SRT
// owner used by HSD before lb_8000B1CC samples hit/hurt primitive endpoints. It falls back to the
// integer SSANIM01 matrix when track data is unavailable.
//
// Decomp owner path:
// - refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
// - refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
// - refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjSetupMatrix
// - refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
int anim_pose_get_collision_matrix_f32(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                       float anim_frame, uint16_t part_id, float out_3x4[12]);

// Bulk variant for hot primitive refresh paths that need several collision matrices for the same
// fighter pose. `out_mats_12` is `count * 12` floats; `out_ok[i]` is set to 1 when row i was
// populated and 0 otherwise.
int anim_pose_get_collision_matrices_f32(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                         float anim_frame, const uint16_t* part_ids, uint16_t count,
                                         float* out_mats_12, uint8_t* out_ok);

// Hot-path TransN sampler.
// Reads the per-frame TransN/root translation tail for (char_id, msid, frame) into out_xyz:
//   (x, y, z)
//
// Decomp consumer example (uses y/z components as offsets/vel targets):
// - refs/melee/src/melee/ft/ft_081B.c::ft_80085030 (fp->x6A4_transNOffset.{y,z})
//
// Returns 0 on success; nonzero on missing/invalid inputs.
int anim_pose_get_transn(uint8_t char_id, uint16_t msid, uint16_t frame, float out_xyz[3]);

#ifdef __cplusplus
}  // extern "C"
#endif
