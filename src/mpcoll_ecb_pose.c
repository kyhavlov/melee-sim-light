#include "mpcoll_ecb_pose.h"

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "ecb_pose.h"
#include "ids.h"
#include "motion_state_owners.h"
#include "msl_math.h"
#include "mtx34.h"
#include "specialhi_pose.h"

uint8_t mpcoll_non_air_common_damage_submotion(uint32_t smid) {
  switch (smid) {
    case (uint32_t)MSL_SM_DAMAGE_HI_1:
    case (uint32_t)MSL_SM_DAMAGE_HI_2:
    case (uint32_t)MSL_SM_DAMAGE_HI_3:
    case (uint32_t)MSL_SM_DAMAGE_N_1:
    case (uint32_t)MSL_SM_DAMAGE_N_2:
    case (uint32_t)MSL_SM_DAMAGE_N_3:
    case (uint32_t)MSL_SM_DAMAGE_LW_1:
    case (uint32_t)MSL_SM_DAMAGE_LW_2:
    case (uint32_t)MSL_SM_DAMAGE_LW_3:
      return 1u;
    default:
      return 0u;
  }
}

uint8_t mpcoll_ground_specialhi_uses_jobj_ecb(uint8_t char_id, uint16_t action_id) {
  const MslCharParams* chp = msl_char_params_fast(char_id);
  if (chp == NULL || !(chp->firefox_bound_angle_degrees > 0.0f)) {
    return 0u;
  }
  // Ownership from FireFox/FireBird's extracted special-hi bound-angle data. Those launch rows
  // rotate XRotN and branch through ftFox_SpecialHi_IsBound before their floor collision result.
  // Sheik/Zelda Vanish end uses a different source owner: ftSk/ftZd_SpecialAirHi_Coll calls
  // ft_CheckGroundAndLedge directly and enters LandingFallSpecial on any accepted floor/ledge.
  // Do not apply the FireFox JObj/bound-angle ECB owner to generic SpecialAirHi rows.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_RotateModel}
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC}
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI);
}

static inline uint8_t mpcoll_ground_specialhi_rotate_collision_point_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t frame_u16,
    uint16_t part_id, float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }
  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, 2u, m) != 0) {
    return 0u;
  }
  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = msl_melee_cosf(angle);
  const float s = msl_melee_sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline uint8_t mpcoll_ground_damageflyroll_xrotn_angle_from_velocity(const MslBatch* batch,
                                                                            size_t idx,
                                                                            float* out_angle) {
  if (batch == NULL || out_angle == NULL) {
    return 0u;
  }
  const float vx = batch->state.speed_air_x_self[idx] + batch->state.speed_x_attack[idx];
  const float vy = batch->state.speed_y_self[idx] + batch->state.speed_y_attack[idx];
  if (!(isfinite(vx) && isfinite(vy)) || (vx == 0.0f && vy == 0.0f)) {
    return 0u;
  }
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  *out_angle = facing_dir * atan2f(vx, vy);
  return 1u;
}

uint8_t mpcoll_ground_damageflyroll_uses_jobj_ecb(uint16_t action_id) {
  return action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL ? 1u : 0u;
}

static inline uint8_t mpcoll_ground_damageflyroll_rotate_collision_point_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t part_id,
    float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }
  float angle = 0.0f;
  if (!mpcoll_ground_damageflyroll_xrotn_angle_from_velocity(batch, idx, &angle)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_collision_matrix(batch, idx, msid, 0u, 2u, m) != 0) {
    return 0u;
  }
  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

uint8_t mpcoll_ground_try_sample_damageflyroll_jobj_ecb(
    MslEcbWorldPoints* out, const MslBatch* batch, size_t idx, uint8_t char_id, uint32_t anim,
    uint16_t action_id, uint16_t frame_u16, float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL || !(anim <= 0xFFFFu) ||
      !mpcoll_ground_damageflyroll_uses_jobj_ecb(action_id)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim;
  const float model_scaling =
      (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[idx] * model_scaling;

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  for (uint16_t pi = 0; pi < ch->ecb_joint_count; pi++) {
    const uint16_t part_id = ch->ecb_joints[pi];
    float m[12];
    if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, part_id, m) != 0) {
      return 0u;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    msl_mtx34_mul_point(m, origin, &x, &y, &z);
    x *= model_scale;
    y *= model_scale;
    z *= model_scale;
    (void)mpcoll_ground_damageflyroll_rotate_collision_point_xrotn(
        batch, idx, char_id, msid, part_id, model_scale, &x, &y, &z);

    // DamageFlyRoll floor collision consumes the same live XRotN JObj pose as BODY/hurtcap
    // selection: ftCo_8008DCE0 and ftCo_DamageFlyRoll_Phys call doFlyRoll before the map callback,
    // then ft_80081DD4 loads the collision ECB from those live JObjs.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    //   ftCo_8008DCE0,doFlyRoll,ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll}
    // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC}
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    const float rel_x = facing_dir * z;
    const float rel_y = y;
    if (!have) {
      min_x = max_x = rel_x;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      if (rel_x < min_x) {
        min_x = rel_x;
      }
      if (rel_x > max_x) {
        max_x = rel_x;
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
    return 0u;
  }

  if (min_y < 0.0f) {
    min_y = 0.0f;
  }
  const float side_rel_y = ch->ecb_side_y_offset + (0.5f * (min_y + max_y));

  out->left_rel_x = min_x;
  out->right_rel_x = max_x;
  out->bottom_rel_y = min_y;
  out->top_rel_y = max_y;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + min_y;
  out->top_x = pos_x;
  out->top_y = pos_y + max_y;
  out->left_x = pos_x + min_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + max_x;
  out->right_y = pos_y + side_rel_y;
  mpcoll_ecb_points_apply_jobj_horizontal_normalization(batch, idx, out);
  return 1u;
}

uint8_t mpcoll_ground_try_sample_specialhi_jobj_ecb(MslEcbWorldPoints* out, const MslBatch* batch,
                                                    size_t idx, uint8_t char_id, uint32_t anim,
                                                    uint16_t action_id, uint16_t frame_u16,
                                                    float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL || !(anim <= 0xFFFFu) ||
      !mpcoll_ground_specialhi_uses_jobj_ecb(char_id, action_id)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim;
  const float model_scaling =
      (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[idx] * model_scaling;

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  for (uint16_t pi = 0; pi < ch->ecb_joint_count; pi++) {
    const uint16_t part_id = ch->ecb_joints[pi];
    float m[12];
    if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, part_id, m) != 0) {
      return 0u;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    msl_mtx34_mul_point(m, origin, &x, &y, &z);
    x *= model_scale;
    y *= model_scale;
    z *= model_scale;
    (void)mpcoll_ground_specialhi_rotate_collision_point_xrotn(batch, idx, char_id, msid, frame_u16,
                                                               part_id, model_scale, &x, &y, &z);

    // SpecialHi floor collision uses the same live JObj ECB source as the retained wall/ledge
    // owners: mpColl_LoadECB_JObj reads collision joints after ftFox_SpecialHi_RotateModel mutates
    // FtPart_XRotN, then normalizes the ECB before mpColl_80044628_Floor consumes the bottom sweep.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    const float rel_x = facing_dir * z;
    const float rel_y = y;
    if (!have) {
      min_x = max_x = rel_x;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      if (rel_x < min_x) {
        min_x = rel_x;
      }
      if (rel_x > max_x) {
        max_x = rel_x;
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
    return 0u;
  }

  if (min_y < 0.0f) {
    min_y = 0.0f;
  }
  const float side_rel_y = ch->ecb_side_y_offset + (0.5f * (min_y + max_y));

  out->left_rel_x = min_x;
  out->right_rel_x = max_x;
  out->bottom_rel_y = min_y;
  out->top_rel_y = max_y;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + min_y;
  out->top_x = pos_x;
  out->top_y = pos_y + max_y;
  out->left_x = pos_x + min_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + max_x;
  out->right_y = pos_y + side_rel_y;
  mpcoll_ecb_points_apply_jobj_horizontal_normalization(batch, idx, out);
  return 1u;
}

// Decomp: mpColl floor-edge helpers use +/-1 offsets from the floor endpoint when probing for
// blocking walls before setting Collide_{Left,Right}Edge.
float mpcoll_pose_ecb_bottom_rel_y(uint8_t char_id, uint32_t anim, uint16_t frame_u16,
                                   uint8_t force_zero_bottom) {
  if (force_zero_bottom) {
    return 0.0f;
  }
  return msl_ecb_bottom_rel_y(char_id, anim, (int)frame_u16);
}

float mpcoll_action_pose_ecb_bottom_rel_y(uint8_t char_id, uint16_t action_id, int16_t action_frame,
                                          uint8_t force_zero_bottom) {
  if (force_zero_bottom) {
    return 0.0f;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  if (smid == 0xFFFFu) {
    return 0.0f;
  }
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame((float)action_frame);
  return msl_ecb_bottom_rel_y(char_id, smid, (int)frame);
}

static inline uint8_t mpcoll_common_fall_blended_ecb_seed_bit(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
      return 1u << 0;
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
      return 1u << 1;
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
      return 1u << 2;
    default:
      return 0u;
  }
}

uint8_t mpcoll_common_fall_blended_ecb_live_owner(const MslBatch* batch, size_t idx,
                                                  uint16_t action_id) {
  if (batch == NULL || batch->state.common_fall_blend_x4[idx] == 0.0f) {
    return 0u;
  }
  const uint8_t bit = mpcoll_common_fall_blended_ecb_seed_bit(action_id);
  if (bit == 0u) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  return (ch != NULL && (ch->common_fall_blended_ecb_seed_mask & bit) != 0u) ? 1u : 0u;
}

uint8_t mpcoll_common_fall_blended_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch,
                                              size_t idx, uint8_t char_id, uint16_t msid,
                                              uint16_t frame_u16, float facing_dir, float pos_x,
                                              float pos_y) {
  if (out == NULL || batch == NULL) {
    return 0u;
  }
  return (uint8_t)(msl_ecb_world_points_sample_collision_pose_f32(out, batch, idx, char_id, msid,
                                                                  (float)frame_u16, facing_dir,
                                                                  pos_x, pos_y, 0u) == 0);
}

uint8_t mpcoll_vanish_jobj_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch, size_t idx,
                                      uint8_t char_id, uint16_t msid, uint16_t frame_u16,
                                      float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL) {
    return 0u;
  }
  // Source `mpColl_LoadECB_inline` routes the fighter's CollData JObj ECB source through
  // `mpColl_LoadECB_JObj` before `ft_CheckGroundAndLedge -> mpColl_800473CC` floor checks. Vanish
  // does not use FireFox/FireBird's XRotN bound-angle rotation, so sample the generic collision
  // JObj pose rather than the SpecialHi rotated-JObj helper.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80081B38,ft_CheckGroundAndLedge}
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_LoadECB_inline,
  //   mpColl_800473CC}
  return (uint8_t)(msl_ecb_world_points_sample_collision_pose_f32(out, batch, idx, char_id, msid,
                                                                  (float)frame_u16, facing_dir,
                                                                  pos_x, pos_y, 0u) == 0);
}

void mpcoll_bottom_world_point_from_rel(MslEcbBottomWorldPoint* out, float pos_x, float pos_y,
                                        float rel_y, uint16_t frame_u16) {
  if (out == NULL) {
    return;
  }
  out->x = pos_x;
  out->y = pos_y + rel_y;
  out->rel_y = rel_y;
  out->frame_u16 = frame_u16;
}

void mpcoll_ecb_world_points_from_rel(MslEcbWorldPoints* out, float pos_x, float pos_y,
                                      float bottom_rel_y, float top_rel_y, float left_rel_x,
                                      float right_rel_x, float side_rel_y, uint16_t frame_u16) {
  if (out == NULL) {
    return;
  }
  out->bottom_rel_y = bottom_rel_y;
  out->top_rel_y = top_rel_y;
  out->left_rel_x = left_rel_x;
  out->right_rel_x = right_rel_x;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + bottom_rel_y;
  out->top_x = pos_x;
  out->top_y = pos_y + top_rel_y;
  out->left_x = pos_x + left_rel_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + right_rel_x;
  out->right_y = pos_y + side_rel_y;
}

void mpcoll_ecb_points_apply_jobj_horizontal_normalization(const MslBatch* batch, size_t idx,
                                                           MslEcbWorldPoints* ecb) {
  if (batch == NULL || ecb == NULL) {
    return;
  }

  // `mpColl_LoadECB_JObj` recenters a sampled horizontal span when it is narrower than
  // max(4, x12C), then clamps the final sides to at least +/-2. ft_80081B38 initializes x12C to
  // 10 * fighter scale. This is part of CollData.desired_ecb/current ecb construction, so consumers
  // that snapshot CollData after interpolation (notably ftCo_800C1E64) must observe the normalized
  // side rather than the raw extracted joint extent.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpCollInterpolateECB}
  float left_rel_x = ecb->left_rel_x;
  float right_rel_x = ecb->right_rel_x;
  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(right_rel_x - left_rel_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * ecb_width;
    left_rel_x = -half_width;
    right_rel_x = half_width;
  }
  if (right_rel_x < 2.0f) {
    right_rel_x = 2.0f;
  }
  if (left_rel_x > -2.0f) {
    left_rel_x = -2.0f;
  }

  ecb->left_rel_x = left_rel_x;
  ecb->right_rel_x = right_rel_x;
  ecb->left_x = ecb->bottom_x + left_rel_x;
  ecb->right_x = ecb->bottom_x + right_rel_x;
}

static inline uint8_t mpcoll_rel_ecb_is_finite(float bottom_rel_y, float top_rel_y,
                                               float left_rel_x, float right_rel_x,
                                               float side_rel_y) {
  return (uint8_t)(isfinite(bottom_rel_y) && isfinite(top_rel_y) && isfinite(left_rel_x) &&
                   isfinite(right_rel_x) && isfinite(side_rel_y));
}

uint8_t mpcoll_state_current_ecb_points(const MslBatch* batch, size_t idx, MslEcbWorldPoints* out,
                                        float pos_x, float pos_y, uint16_t frame_u16) {
  const float bottom_rel_y = batch->state.coll_ecb_bottom_rel_y[idx];
  const float top_rel_y = batch->state.coll_ecb_top_rel_y[idx];
  const float left_rel_x = batch->state.coll_ecb_left_rel_x[idx];
  const float right_rel_x = batch->state.coll_ecb_right_rel_x[idx];
  const float side_rel_y = batch->state.coll_ecb_side_rel_y[idx];
  if (!batch->state.coll_ecb_bottom_valid[idx] ||
      !mpcoll_rel_ecb_is_finite(bottom_rel_y, top_rel_y, left_rel_x, right_rel_x, side_rel_y)) {
    return 0u;
  }
  mpcoll_ecb_world_points_from_rel(out, pos_x, pos_y, bottom_rel_y, top_rel_y, left_rel_x,
                                   right_rel_x, side_rel_y, frame_u16);
  return 1u;
}

uint8_t mpcoll_state_squeeze_restore_ecb_points(const MslBatch* batch, size_t idx,
                                                MslEcbWorldPoints* out, float pos_x, float pos_y,
                                                uint16_t frame_u16) {
  if (batch == NULL || out == NULL || batch->state.coll_squeeze_restore_ecb_valid[idx] == 0u) {
    return 0u;
  }
  const float bottom_rel_y = batch->state.coll_squeeze_restore_ecb_bottom_rel_y[idx];
  const float top_rel_y = batch->state.coll_squeeze_restore_ecb_top_rel_y[idx];
  const float left_rel_x = batch->state.coll_squeeze_restore_ecb_left_rel_x[idx];
  const float right_rel_x = batch->state.coll_squeeze_restore_ecb_right_rel_x[idx];
  const float side_rel_y = batch->state.coll_squeeze_restore_ecb_side_rel_y[idx];
  if (!mpcoll_rel_ecb_is_finite(bottom_rel_y, top_rel_y, left_rel_x, right_rel_x, side_rel_y)) {
    return 0u;
  }
  mpcoll_ecb_world_points_from_rel(out, pos_x, pos_y, bottom_rel_y, top_rel_y, left_rel_x,
                                   right_rel_x, side_rel_y, frame_u16);
  return 1u;
}

uint8_t mpcoll_state_desired_ecb_points(const MslBatch* batch, size_t idx, MslEcbWorldPoints* out,
                                        float pos_x, float pos_y, uint16_t frame_u16) {
  if (batch == NULL || out == NULL || batch->state.coll_desired_ecb_bottom_valid[idx] == 0u) {
    return 0u;
  }
  const float bottom_rel_y = batch->state.coll_desired_ecb_bottom_rel_y[idx];
  const float top_rel_y = batch->state.coll_desired_ecb_top_rel_y[idx];
  const float left_rel_x = batch->state.coll_desired_ecb_left_rel_x[idx];
  const float right_rel_x = batch->state.coll_desired_ecb_right_rel_x[idx];
  const float side_rel_y = batch->state.coll_desired_ecb_side_rel_y[idx];
  if (!mpcoll_rel_ecb_is_finite(bottom_rel_y, top_rel_y, left_rel_x, right_rel_x, side_rel_y)) {
    return 0u;
  }
  mpcoll_ecb_world_points_from_rel(out, pos_x, pos_y, bottom_rel_y, top_rel_y, left_rel_x,
                                   right_rel_x, side_rel_y, frame_u16);
  return 1u;
}

void mpcoll_store_current_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb) {
  batch->state.coll_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_ecb_side_rel_y[idx] = ecb->side_rel_y;
}

void mpcoll_store_prev_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb) {
  batch->state.coll_prev_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_prev_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_prev_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_prev_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_prev_ecb_side_rel_y[idx] = ecb->side_rel_y;
}

void mpcoll_store_desired_ecb_points(MslBatch* batch, size_t idx, const MslEcbWorldPoints* ecb) {
  batch->state.coll_desired_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_desired_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_desired_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_desired_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_desired_ecb_side_rel_y[idx] = ecb->side_rel_y;
}
