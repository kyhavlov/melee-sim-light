#pragma once

#include <math.h>
#include <stdint.h>

#include "batch_internal.h"

// Deterministic animation/script timebase helpers.
//
// Decomp shape (GALE01):
// - fp->cur_anim_frame (float) is the movescript/AS timebase recorded by Slippi as `state_age`.
// - fp->frame_speed_mul (float) is the animation rate (ftAnim_SetAnimRate / ftAnim_8006F0FC).
// - Fighter_ChangeMotionState sets:
//     fp->frame_speed_mul = anim_speed;
//     fp->cur_anim_frame = anim_start - fp->frame_speed_mul;
//   but then calls ftAnim_8006E9B4, which overwrites fp->cur_anim_frame from the current AObj
//   frame (ftAnim_8006F3DC). This means post-frame `state_age` is shaped by the AObj curr_frame
//   on entry (typically anim_start for anim_start==0.0f).
//   refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
//   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
//   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006F3DC
// - Animation advance is gated during hitlag:
//   Fighter_8006A360 wraps ftAnim_8006EBA4 in `if (!fp->x2219_b5)`.
//   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
//
// We represent these as signed Q16.16 fixed-point for deterministic fractional carry on the hot path.

enum { MSL_Q16_16_ONE = 1 << 16 };

static inline int32_t msl_q16_16_clamp_i32(double x) {
  if (x >= 2147483647.0) {
    return INT32_MAX;
  }
  if (x <= -2147483648.0) {
    return INT32_MIN;
  }
  return (int32_t)x;
}

static inline int32_t msl_q16_16_from_f32(float x) {
  if (!isfinite(x)) {
    return 0;
  }
  // Round-to-nearest when quantizing float -> fixed-point so that seeded and computed rates don't
  // systematically bias downward (important for anim-end and window thresholds).
  //
  // Note: action_frame is derived separately via floor(x / 65536), matching `floor(cur_anim_frame)`.
  const double scaled = (double)x * (double)MSL_Q16_16_ONE;
  const double quantized = (scaled >= 0.0) ? floor(scaled + 0.5) : ceil(scaled - 0.5);
  return msl_q16_16_clamp_i32(quantized);
}

static inline float msl_f32_from_q16_16(int32_t x) {
  return (float)x * (1.0f / (float)MSL_Q16_16_ONE);
}

static inline int16_t msl_floor_i16_from_q16_16(int32_t x) {
  // Compute floor(x / 65536) in a way that is well-defined for negative values.
  int32_t q = 0;
  if (x >= 0) {
    q = x / (int32_t)MSL_Q16_16_ONE;
  } else {
    const int32_t neg = -x;
    q = -(int32_t)((neg + ((int32_t)MSL_Q16_16_ONE - 1)) / (int32_t)MSL_Q16_16_ONE);
  }
  if (q > (int32_t)INT16_MAX) {
    return INT16_MAX;
  }
  if (q < (int32_t)INT16_MIN) {
    return INT16_MIN;
  }
  return (int16_t)q;
}

static inline void msl_anim_timebase_recompute_derived(MslBatch* batch, size_t idx) {
  batch->state.anim_frame_f32[idx] = msl_f32_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]);
  batch->state.action_frame[idx] = msl_floor_i16_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]);
}

static inline void msl_anim_timebase_seed(MslBatch* batch, size_t idx, float cur_anim_frame_f32,
                                          float frame_speed_mul_f32) {
  if (batch == NULL) {
    return;
  }
  batch->state.anim_frame_fp_q16_16[idx] = msl_q16_16_from_f32(cur_anim_frame_f32);
  batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(frame_speed_mul_f32);
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void msl_anim_timebase_enter_raw(MslBatch* batch, size_t idx, float anim_start_f32,
                                               float anim_speed_f32) {
  // Mirror Fighter_ChangeMotionState's post-entry cur_anim_frame shape.
  //
  // Decomp:
  // - fp->frame_speed_mul = anim_speed;
  // - fp->cur_anim_frame = anim_start - fp->frame_speed_mul;
  // - ftAnim_8006E9B4(gobj) overwrites fp->cur_anim_frame from ftAnim_8006F3DC (AObj curr_frame).
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006F3DC
  if (batch == NULL) {
    return;
  }
  const int32_t start_fp = msl_q16_16_from_f32(anim_start_f32);
  const int32_t speed_fp = msl_q16_16_from_f32(anim_speed_f32);
  batch->state.frame_speed_mul_fp_q16_16[idx] = speed_fp;
  batch->state.anim_frame_fp_q16_16[idx] = start_fp;
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void msl_anim_timebase_tick_once(MslBatch* batch, size_t idx) {
  // Only call this for motion states whose *Enter* explicitly calls ftAnim_8006EBA4 immediately
  // after Fighter_ChangeMotionState (e.g. Dash / Turn).
  //
  // Decomp examples:
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter
  if (batch == NULL) {
    return;
  }
  // Decomp: Fighter_8006A360 gates ftAnim_8006EBA4 on !hitlag (fp->x2219_b5).
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (batch->state.hitlag[idx] != 0) {
    msl_anim_timebase_recompute_derived(batch, idx);
    return;
  }
  batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  msl_anim_timebase_recompute_derived(batch, idx);
}

// Forward decl: fighter attack identity update on motion-state change.
// Defined in src/attack_identity.c.
void attack_identity_on_motion_state_change_ft_800890D0(MslBatch* batch, size_t idx);

// Forward decl: fighter action-state instance_id update on motion-state change.
// Defined in src/instance_id.c.
void instance_id_on_motion_state_change_ft_800895E0(MslBatch* batch, size_t idx);

static inline void msl_anim_timebase_enter(MslBatch* batch, size_t idx, float anim_start_f32,
                                           float anim_speed_f32) {
  // Decomp: Fighter_ChangeMotionState sets the anim timebase then invokes motion-state identity
  // updates (ft_800890D0 / ft_800895E0). This helper models that full "enter motion state" bundle.
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  msl_anim_timebase_enter_raw(batch, idx, anim_start_f32, anim_speed_f32);

  attack_identity_on_motion_state_change_ft_800890D0(batch, idx);

  instance_id_on_motion_state_change_ft_800895E0(batch, idx);
}

// Pure animation timebase reset without invoking Fighter_ChangeMotionState side-effects.
//
// Used by debug/test helpers that want to manipulate the anim clock without updating identity
// (attack_id/attack_instance, instance_id).
static inline void msl_anim_timebase_restart(MslBatch* batch, size_t idx, float anim_start_f32,
                                             float anim_speed_f32) {
  msl_anim_timebase_enter_raw(batch, idx, anim_start_f32, anim_speed_f32);
}

static inline void msl_anim_timebase_set_rate(MslBatch* batch, size_t idx, float anim_rate_f32) {
  if (batch == NULL) {
    return;
  }
  batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(anim_rate_f32);
}

void anim_timebase_update_pre_input(MslBatch* batch);
