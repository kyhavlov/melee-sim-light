#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "escapeair_collision_owner.h"
#include "mpcoll_ecb_points.h"

enum {
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR = 10u,
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
  MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR_ALT = 5u,
};

static inline void msl_ftcommon_lock_ecb(MslBatch* batch, size_t idx, uint8_t frames) {
  if (batch == NULL) {
    return;
  }

  // ftCommon_8007D5D4 / ftCommon_8007D60C set CollData_X130_Locked without loading a new ECB.
  // Preserve the already-published desired packet. Direct/synthetic runtime seeds can lack that
  // packet, so initialize it once from the current extracted pose before publishing lock ownership.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
  if (batch->state.coll_desired_ecb_bottom_valid[idx] == 0u) {
    const uint8_t char_id = batch->state.char_id[idx];
    const uint32_t anim = batch->state.animation_index[idx];
    const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
    const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints desired_ecb = {0};
    msl_ecb_world_points_sample(&desired_ecb, char_id, anim, frame, facing_dir,
                                batch->state.pos_x[idx], batch->state.pos_y[idx], 0u);
    batch->state.coll_desired_ecb_bottom_rel_y[idx] = desired_ecb.bottom_rel_y;
    batch->state.coll_desired_ecb_top_rel_y[idx] = desired_ecb.top_rel_y;
    batch->state.coll_desired_ecb_left_rel_x[idx] = desired_ecb.left_rel_x;
    batch->state.coll_desired_ecb_right_rel_x[idx] = desired_ecb.right_rel_x;
    batch->state.coll_desired_ecb_side_rel_y[idx] = desired_ecb.side_rel_y;
    batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
  }
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
      (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_FTCOMMON;
  batch->state.ecb_lock_timer[idx] = frames;
}

static inline void msl_ftcommon_lock_ecb_8007d5d4(MslBatch* batch, size_t idx) {
  msl_ftcommon_lock_ecb(batch, idx, MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR);
}

static inline void msl_ftcommon_lock_ecb_8007d60c(MslBatch* batch, size_t idx) {
  msl_ftcommon_lock_ecb(batch, idx, MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR_ALT);
}

static inline void msl_ftcommon_unlock_ecb(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_UnlockECB,ftCommon_8007D6A4}
  batch->state.ecb_lock_timer[idx] = 0u;
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
      (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
}
