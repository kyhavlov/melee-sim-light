#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct MslBatch MslBatch;

#ifdef __cplusplus
extern "C" {
#endif

// Init-time loader for SSANIM01 v5 animation matrix blobs:
// - data/anims/fox.bin (char_id=1)
// - data/anims/falco.bin (char_id=22)
//
// Layout source pointers:
// - tools/extraction/extract_ecb_bottom.py (header + payload walk)
// - tools/extraction/extract_ecb_extents.py (matrix record layout)
// - tools/extraction/extract_fighter_anims.py (writer: SSANIM01 v5 + per-frame TransN tail)
int anim_pose_init(void);

// Semantic version for generated fighter animation data. This is surfaced through the top-level
// data manifest because source-donor cross-bakes can change required pose rows without changing
// the byte layout of SSANIM01 itself.
uint32_t anim_pose_data_schema_version(void);

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
void anim_pose_update_dynamic_state_player(MslBatch* batch, size_t player_idx);

// Teacher-forced/fresh-lane initialization of the same persistent DynamicsDesc state. This is
// called at the reseed boundary, before the next frame's animation and collision callbacks; normal
// rollout advances the initialized state only from the priority-16 dynamics phase above.
void anim_pose_reseed_dynamic_state(MslBatch* batch, size_t player_idx);

// Apply ftCo_8009E7B4's per-motion dynamic/AObj ownership before collision reads the live JObj.
void anim_pose_sync_dynamic_ownership(MslBatch* batch, size_t player_idx);

// Persistent live-JObj owner used only by source callbacks that mutate/preserve local SRT across
// frames. Ordinary animation sampling stays descriptor-backed and clears this sparse materialized
// tree. Guard applies ftCo_80091E78's two source blends in place.
void anim_pose_live_discard(MslBatch* batch, size_t player_idx);
int anim_pose_live_materialize(MslBatch* batch, size_t player_idx, uint16_t msid, float anim_frame);
int anim_pose_live_guard_apply(MslBatch* batch, size_t player_idx, float target_weight);

// Pure sampler for ftCo_80091E78's settled target tree (ftData.x20 blended with Guard[x8] by
// guard.x4). Validation preprocessing uses this for the preceding ShieldDesc contact owner when no
// live batch tree is available; GuardOn's recurrent current-tree blend remains a separate hidden
// state owner.
int anim_pose_get_guard_target_matrix_f32(uint8_t char_id, float guard_frame, float guard_weight,
                                          uint16_t part_id, float out_3x4[12]);

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

// Release-local collision-pose sampler for ftCo_800DDDE4. The current Thrown* descendants remain
// animated, but FtPart_XRotN itself has its constraint removed, Euler rotation cleared, and the
// local translation saved by ftCo_800DB368 restored before mpColl_LoadECB_JObj samples the ECB.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
int anim_pose_get_xrotn_restored_collision_matrix_f32(uint8_t char_id, uint16_t msid,
                                                      float anim_frame, uint16_t part_id,
                                                      const float xrotn_restore_pos[3],
                                                      float out_3x4[12]);

// Float-frame local translation accessor used by constraint entry/reseed to preserve fp->x2174.
int anim_pose_get_local_translation_f32(uint8_t char_id, uint16_t msid, float anim_frame,
                                        uint16_t part_id, float out_xyz[3]);

// Evaluate a part's animated local translation only when that part owns an active extracted AObj
// in the requested motion. This is the source predicate used by ftAnim_8006F368 before callbacks
// such as ftCo_800DB500 preserve mutable local JObj state.
// refs/melee/src/melee/ft/ftanim.c::ftAnim_8006F368
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB500
int anim_pose_get_animated_local_translation_f32(uint8_t char_id, uint16_t msid, float anim_frame,
                                                 uint16_t part_id, float out_xyz[3]);

// Common Fall/FallAerial/FallSpecial live local-SRT blend matrix sampler.
//
// Source owner:
// - ftCo_Fall_Anim_Inner chooses neutral/F/B submotions from air drift and updates
//   mv.co.{fall,fallaerial,fallspecial}.x4.
// - ftCo_800CC988 / ftCo_Fall_Anim_Inner call ftAnim_8006FE9C(fp, FtPart_TransN, ...), which keeps
//   pre-TransN ancestors on the active selected submotion and blends eligible TransN descendants'
//   local SRT with lb_8000C490 before collision refresh samples the requested part matrix.
// Returns 0 and writes out_3x4 only when the current fighter state owns a nonzero source blend.
int anim_pose_get_common_fall_blend_collision_matrix_f32(const MslBatch* batch, size_t player_idx,
                                                         uint16_t msid, float anim_frame,
                                                         uint16_t part_id, float out_3x4[12]);

// Debug/test helper for the CommonFall local-SRT blend path. This samples the same matrix builder
// used by anim_pose_get_common_fall_blend_collision_matrix_f32 without requiring a live batch
// state, so tests can lock source-owned endpoints such as x4 == 1.0 matching the target
// submotion matrix.
int anim_pose_debug_common_fall_blend_matrix(uint8_t char_id, uint16_t neutral_msid,
                                             uint16_t target_msid, float anim_frame,
                                             uint16_t part_id, float weight, float out_3x4[12]);

// Debug/test helper for the float-frame collision matrix path without requiring a live batch.
// This is intentionally not a gameplay API; it exists to lock SSANIMT1/FObj replay-data behavior.
int anim_pose_debug_collision_matrix_f32(uint8_t char_id, uint16_t msid, float anim_frame,
                                         uint16_t part_id, float out_3x4[12]);

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

// Float-frame TransN/root translation sampler.
//
// Uses the extracted SSANIMT1 FObj track stream for the live AObj/JObj local SRT owner. Falls back
// to linear interpolation of the SSANIM01 v5 TransN tail when track data is unavailable.
//
// Decomp owner path:
// - refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
// - refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
// - refs/melee/src/melee/ft/ft_081B.c::ft_80085030
int anim_pose_get_transn_f32(uint8_t char_id, uint16_t msid, float anim_frame, float out_xyz[3]);

#ifdef __cplusplus
}  // extern "C"
#endif
