#include "ucf.h"

#include <math.h>

static inline int8_t msl_sign_s8(int8_t x) { return (x < 0) ? (int8_t)-1 : (int8_t)1; }

MslStickI8 ucf_clamp_stick_i8(int8_t raw_x, int8_t raw_y) {
  // Mirrors HSD_PadClampCheck3 used by Melee (see refs/melee/.../baselib/controller.c).
  // With clamp_stickMin=0 and clamp_stickShift=1, only the max-radius clamp applies.
  const float fx = (float)raw_x;
  const float fy = (float)raw_y;
  const float r = sqrtf(fx * fx + fy * fy);

  if (r > 80.0f) {
    const float scale = 80.0f / r;
    raw_x = (int8_t)(fx * scale);
    raw_y = (int8_t)(fy * scale);
  }

  MslStickI8 out = {raw_x, raw_y};
  return out;
}

MslStickI8 ucf_apply_cardinals_1_0_i8(MslStickI8 clamped) {
  // Reference: refs/ucf/src/pad_buffer/pad_buffer.cpp::apply_cardinals
  // Produce 1.0 cardinals when one axis is >= 80 and the other is within [-6, 6].
  const int8_t SNAP_RANGE = 6;

  const int8_t x = clamped.x;
  const int8_t y = clamped.y;

  if ((x <= (int8_t)-80 || x >= (int8_t)80) && y >= (int8_t)-SNAP_RANGE &&
      y <= (int8_t)SNAP_RANGE) {
    clamped.x = (int8_t)(msl_sign_s8(x) * (int8_t)80);
    clamped.y = 0;
    return clamped;
  }
  if ((y <= (int8_t)-80 || y >= (int8_t)80) && x >= (int8_t)-SNAP_RANGE &&
      x <= (int8_t)SNAP_RANGE) {
    clamped.x = 0;
    clamped.y = (int8_t)(msl_sign_s8(y) * (int8_t)80);
    return clamped;
  }
  return clamped;
}

MslStickI8 ucf_process_stick_i8(int8_t raw_x, int8_t raw_y, uint8_t ucf_enabled,
                                uint8_t ucf_cardinals_1_0_enabled) {
  MslStickI8 v = {raw_x, raw_y};
  if (ucf_enabled && ucf_cardinals_1_0_enabled) {
    // Apply to raw axes; this matches how UCF uses raw PAD bytes to decide snapping,
    // and then forces the processed stick to an exact cardinal.
    v = ucf_apply_cardinals_1_0_i8(v);
  }
  return ucf_clamp_stick_i8(v.x, v.y);
}
