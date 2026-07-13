#pragma once

#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "ids.h"
#include "msl_math.h"
#include "motion_state_owners.h"

static inline uint8_t msl_specialhi_rotate_model_action(uint8_t char_id, uint16_t action_id) {
  // Firefox/Firebird model-rotation ownership from the extracted MotionState row identity;
  // the 341..372 range stays kind 0 for other characters (see char_registry.h).
  switch (msl_motion_state_fx_special_kind_fast(char_id, action_id)) {
    case MSL_FX_KIND_SPECIAL_HI:
    case MSL_FX_KIND_SPECIAL_AIR_HI:
      // `mv.fx.SpecialHi.rotateModel` is written and applied to FtPart_XRotN only by the launch
      // states. Landing/Fall/Bound enter through Fighter_ChangeMotionState with flags=0 and load
      // their own unrotated animation pose; none of those callbacks reapplies RotateModel.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
      //   ftFox_SpecialHi_RotateModel,ftFx_SpecialHiFall_AirToGround,
      //   ftFx_SpecialHiLanding_GroundToAir,ftFx_SpecialHiBound_Enter}
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t msl_specialhi_rotate_model_from_velocity(float vel_x, float vel_y,
                                                               uint8_t facing, float* out) {
  if (out == NULL || !(isfinite(vel_x) && isfinite(vel_y)) ||
      !(fabsf(vel_x) > 0.0f || fabsf(vel_y) > 0.0f)) {
    return 0u;
  }
  const float facing_dir = facing ? 1.0f : -1.0f;
  *out = atan2f(vel_y, vel_x * facing_dir);
  return 1u;
}

static inline void msl_specialhi_rotate_model_set(MslBatch* batch, size_t idx, float rotate_model) {
  if (batch == NULL) {
    return;
  }
  if (!isfinite(rotate_model)) {
    batch->state.specialhi_rotate_model_valid[idx] = 0u;
    batch->state.specialhi_rotate_model[idx] = 0.0f;
    return;
  }
  batch->state.specialhi_rotate_model[idx] = rotate_model;
  batch->state.specialhi_rotate_model_valid[idx] = 1u;
}

static inline void msl_specialhi_rotate_model_set_from_velocity(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_from_velocity(batch->state.speed_air_x_self[idx],
                                                batch->state.speed_y_self[idx],
                                                batch->state.facing[idx], &rotate_model)) {
    batch->state.specialhi_rotate_model_valid[idx] = 0u;
    batch->state.specialhi_rotate_model[idx] = 0.0f;
    return;
  }
  msl_specialhi_rotate_model_set(batch, idx, rotate_model);
}

static inline uint8_t msl_specialhi_rotate_model_get(const MslBatch* batch, size_t idx,
                                                     float* out) {
  if (batch == NULL || out == NULL || batch->state.specialhi_rotate_model_valid[idx] == 0u) {
    return 0u;
  }
  const float rotate_model = batch->state.specialhi_rotate_model[idx];
  if (!isfinite(rotate_model)) {
    return 0u;
  }
  *out = rotate_model;
  return 1u;
}

static inline uint8_t msl_specialhi_rotate_model_get_or_velocity(const MslBatch* batch, size_t idx,
                                                                 float* out) {
  if (msl_specialhi_rotate_model_get(batch, idx, out)) {
    return 1u;
  }
  if (batch == NULL) {
    return 0u;
  }
  return msl_specialhi_rotate_model_from_velocity(batch->state.speed_air_x_self[idx],
                                                  batch->state.speed_y_self[idx],
                                                  batch->state.facing[idx], out);
}

static inline float msl_specialhi_xrotn_angle_from_rotate_model(float rotate_model) {
  return (2.0f * MSL_PI_F) - rotate_model;
}
