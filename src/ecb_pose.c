#include "ecb_pose.h"

#include <math.h>

#include "anim_pose.h"
#include "batch_internal.h"
#include "char_params.h"
#include "mtx34.h"

static void msl_ecb_pose_apply_jobj_load_postprocess_f32(float* left_x, float* right_x,
                                                         float* bottom_y, float* top_y,
                                                         float fighter_scale_y,
                                                         uint8_t lock_bottom_to_zero) {
  if (left_x == NULL || right_x == NULL || bottom_y == NULL || top_y == NULL) {
    return;
  }

  // Source shape for the CommonFall floor callback:
  // ftCo_Fall_Coll -> ft_800831CC -> mpColl_80047E14 calls
  // mpColl_LoadECB_inline(coll, 6), which routes JObj ECB sources through mpColl_LoadECB_JObj
  // before mpColl_80042384/mpColl_80044628_Floor consume the packet. Flags=6 includes
  // CollisionFlagAir_CanGrabLedge, so the source skips the broad +/-2 expansion block but still
  // applies the narrow-span recentering, final side clamps, vertical-span branch, and bottom clamp.
  //
  // ft_80081B38 seeds ecb_source.{x128,x12C} = 10.0F * fp->x34_scale.y.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_LoadECB_inline}
  const float ecb_source_min = 10.0f * fighter_scale_y;
  float min_span = fmaxf(4.0f, ecb_source_min);
  float span = fabsf(*right_x - *left_x);
  if (span < min_span) {
    const float half_span = 0.5f * span;
    *right_x = half_span;
    *left_x = -half_span;
  }

  min_span = fmaxf(4.0f, ecb_source_min);
  span = fabsf(*top_y - *bottom_y);
  if (span < min_span) {
    const float half_span = 0.5f * span;
    const float mid_y = 0.5f * (*top_y + *bottom_y);
    *top_y = mid_y + half_span;
    *bottom_y = mid_y - half_span;
  }

  if (*right_x < 2.0f) {
    *right_x = 2.0f;
  }
  if (*left_x > -2.0f) {
    *left_x = -2.0f;
  }

  if (lock_bottom_to_zero) {
    *bottom_y = 0.0f;
  } else if (*bottom_y < 0.0f) {
    *bottom_y = 0.0f;
  }
}

int msl_ecb_world_points_sample_collision_pose_f32(MslEcbWorldPoints* out, const MslBatch* batch,
                                                   size_t player_idx, uint8_t char_id,
                                                   uint16_t msid, float anim_frame,
                                                   float facing_dir, float pos_x, float pos_y,
                                                   uint8_t lock_bottom_to_zero) {
  if (out == NULL || batch == NULL) {
    return -1;
  }
  const MslCharParams* ch = msl_char_params_fast(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return -1;
  }

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  float matrices[6u * 12u];
  uint8_t matrix_ok[6u] = {0};
  const uint8_t restored_xrotn = batch->state.grab_constraint_x2226_b2[player_idx] == 0u &&
                                 batch->state.grab_constraint_x2174_valid[player_idx] != 0u;
  if (restored_xrotn == 0u &&
      anim_pose_get_collision_matrices_f32(batch, player_idx, msid, anim_frame, ch->ecb_joints,
                                           ch->ecb_joint_count, matrices, matrix_ok) != 0) {
    return -1;
  }
  for (uint16_t pi = 0; pi < ch->ecb_joint_count; pi++) {
    const uint16_t part_id = ch->ecb_joints[pi];
    float restored[12];
    const float* m = &matrices[(size_t)pi * 12u];
    if (restored_xrotn != 0u) {
      const float x2174[3] = {
          batch->state.grab_constraint_x2174_x[player_idx],
          batch->state.grab_constraint_x2174_y[player_idx],
          batch->state.grab_constraint_x2174_z[player_idx],
      };
      if (anim_pose_get_xrotn_restored_collision_matrix_f32(char_id, msid, anim_frame, part_id,
                                                            x2174, restored) != 0) {
        return -1;
      }
      m = restored;
    } else if (matrix_ok[pi] == 0u) {
      return -1;
    }

    // SSANIM matrices are baked with the inverse model-scale part correction; extracted fixed ECB
    // tables multiply sampled JObj coordinates by ftData model_scaling at load time. Keep the live
    // JObj path on the same unit contract so fixed and collision-pose ECB packets compare directly.
    const float model_scaling =
        (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
    const float model_scale = batch->state.fighter_scale_y[player_idx] * model_scaling;
    const float rel_x_unmirrored = m[11] * model_scale;
    const float rel_y = m[7] * model_scale;

    if (!have) {
      min_x = max_x = rel_x_unmirrored;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      if (rel_x_unmirrored < min_x) {
        min_x = rel_x_unmirrored;
      }
      if (rel_x_unmirrored > max_x) {
        max_x = rel_x_unmirrored;
      }
      if (rel_y < min_y) {
        min_y = rel_y;
      }
      if (rel_y > max_y) {
        max_y = rel_y;
      }
    }
  }
  if (!have) {
    return -1;
  }

  float left_rel_x = facing_dir * min_x;
  float right_rel_x = facing_dir * max_x;
  if (left_rel_x > right_rel_x) {
    const float tmp = left_rel_x;
    left_rel_x = right_rel_x;
    right_rel_x = tmp;
  }
  float bottom_rel_y = min_y;
  float top_rel_y = max_y;
  msl_ecb_pose_apply_jobj_load_postprocess_f32(&left_rel_x, &right_rel_x, &bottom_rel_y, &top_rel_y,
                                               batch->state.fighter_scale_y[player_idx],
                                               lock_bottom_to_zero);
  const float side_rel_y = ch->ecb_side_y_offset + (0.5f * (bottom_rel_y + top_rel_y));

  *out = (MslEcbWorldPoints){
      .bottom_x = pos_x,
      .bottom_y = pos_y + bottom_rel_y,
      .top_x = pos_x,
      .top_y = pos_y + top_rel_y,
      .left_x = pos_x + left_rel_x,
      .left_y = pos_y + side_rel_y,
      .right_x = pos_x + right_rel_x,
      .right_y = pos_y + side_rel_y,
      .left_rel_x = left_rel_x,
      .right_rel_x = right_rel_x,
      .bottom_rel_y = bottom_rel_y,
      .top_rel_y = top_rel_y,
      .side_rel_y = side_rel_y,
      .frame_u16 = msl_ecb_frame_u16_from_anim_frame(anim_frame),
  };
  return 0;
}
