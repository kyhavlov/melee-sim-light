#pragma once

#include <math.h>
#include <stdint.h>

// Decomp shape:
// - Movescripts and cmd-script timers are driven by fp->cur_anim_frame (float), advanced by
//   fp->frame_speed_mul and affected by hitlag.
//   refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
//
// Sim policy (suite-neutral, for debug geometry / future combat enablement):
// - When seeded anim_frame_f32 is negative or non-finite (NaN/inf), treat it as 0.0f for any
//   move-script table sampling and pose frame indexing.
//   This matches the existing "negative => consult frame 0" fallback used elsewhere, and keeps
//   behavior deterministic under partial/missing replay fields.
static inline float msl_anim_frame_sanitize_f32(float anim_frame_f32) {
  if (!isfinite(anim_frame_f32) || anim_frame_f32 < 0.0f) {
    return 0.0f;
  }
  return anim_frame_f32;
}

static inline uint16_t msl_anim_frame_floor_u16(float anim_frame_f32_sanitized) {
  if (!(anim_frame_f32_sanitized >= 0.0f)) {
    return 0;
  }
  if (anim_frame_f32_sanitized >= 65535.0f) {
    return 0xFFFFu;
  }
  return (uint16_t)floorf(anim_frame_f32_sanitized);
}
