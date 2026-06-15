#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mpcoll_ecb_points.h"

typedef struct MslBatch MslBatch;

#ifdef __cplusplus
extern "C" {
#endif

// Sample an ECB packet from the live collision-pose JObj matrices.
//
// Source owner:
// - mpColl_LoadECB_JObj builds the ECB from ftData_x44_t ECB joints after the action's Anim
//   callback has published any live JObj/local-SRT effects.
// - Common Fall/FallAerial/FallSpecial run ftCo_Fall_Anim_Inner before the Coll callback, so their
//   CollData ECB must consume the selected FallF/FallB submotion plus ftAnim_8006FE9C TransN blend
//   rather than linearly interpolating extracted fixed ECB extents.
//
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_LoadECB_inline}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
// refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FE9C
int msl_ecb_world_points_sample_collision_pose_f32(MslEcbWorldPoints* out, const MslBatch* batch,
                                                   size_t player_idx, uint8_t char_id,
                                                   uint16_t msid, float anim_frame,
                                                   float facing_dir, float pos_x, float pos_y,
                                                   uint8_t lock_bottom_to_zero);

#ifdef __cplusplus
}  // extern "C"
#endif
