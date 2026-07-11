#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
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

static inline float msl_ftcommon_falcon_8007d7fc_self_x(const MslBatch* batch,
                                                        const MslCharParams* ch, size_t idx) {
  float self_x = batch->state.speed_air_x_self[idx];
  const uint32_t msid_u32 = batch->state.animation_index[idx];
  if (ch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
      msid_u32 > 0xFFFFu ||
      msl_anim_uses_root_motion(batch->state.char_id[idx], (uint16_t)msid_u32) == 0u) {
    return self_x;
  }

  // x6A4_transNOffset is the callback-local delta produced by the current animation
  // interpretation. SSANIMT1 preserves the source FObj stream, including fractional rates.
  // refs/melee/src/melee/ft/ftanim.c
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
  // data/anims/<character>.tracks.bin::SSANIMT1 uses_root_motion
  float t_cur[3];
  float t_prev[3];
  const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const float rate = (float)batch->state.frame_speed_mul_fp_q16_16[idx] * (1.0f / 65536.0f);
  const float prev = (rate > 0.0f && cur > rate) ? (cur - rate) : 0.0f;
  if (anim_pose_get_transn_f32(batch->state.char_id[idx], (uint16_t)msid_u32, cur, t_cur) == 0 &&
      anim_pose_get_transn_f32(batch->state.char_id[idx], (uint16_t)msid_u32, prev, t_prev) == 0) {
    const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
    // ftAnim_8006E054 scales each sampled TransN vector into f32 storage before lbVector_Diff;
    // D6A4 applies facing to the stored delta afterward. Preserve those rounding boundaries.
    // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E054
    const float cur_scaled = t_cur[2] * ch->model_scaling;
    const float prev_scaled = t_prev[2] * ch->model_scaling;
    const float delta = cur_scaled - prev_scaled;
    self_x = delta * facing;
  }
  return self_x;
}

static inline void msl_ftcommon_8007d6a4(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const float self_x = batch->state.speed_air_x_self[idx];
  batch->state.on_ground[idx] = 1u;
  batch->state.speed_ground_x_self[idx] = self_x;
  if (ch != NULL) {
    batch->state.jumps_left[idx] = ch->max_jumps;
  }
  batch->state.walljump_used_count[idx] = 0u;
  msl_ftcommon_unlock_ecb(batch, idx);
}

static inline void msl_ftcommon_falcon_8007d7fc(MslBatch* batch, const MslCharParams* ch,
                                                size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Falcon's Raptor start owns x594_b6, so ftAnim_8006E054 uses model scaling alone. Keep that
  // source-owned TransN reconstruction narrow; generic D7FC callers need the dynamic-scale bit
  // modeled before they can safely share it.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E054
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  batch->state.speed_air_x_self[idx] = msl_ftcommon_falcon_8007d7fc_self_x(batch, ch, idx);
  msl_ftcommon_8007d6a4(batch, ch, idx);
}
