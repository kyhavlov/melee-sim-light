#pragma once

// Small pose math helpers (hot-path safe; no allocations).

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Multiply a row-major 3x4 matrix by a point:
// out = (m00 m01 m02 tx)   (x)
//       (m10 m11 m12 ty) * (y)
//       (m20 m21 m22 tz)   (z)
//
// Notes:
// - This matches the SSANIM01 v3 pose matrix layout used by anim_pose_get_matrix(...).
// - Keep this inlined: hitbox/hurtcap refresh is on the per-frame hot path.
static inline void msl_mtx34_mul_point(const float m[12], const float v[3], float* out_x,
                                       float* out_y, float* out_z) {
  const float x = v[0];
  const float y = v[1];
  const float z = v[2];
  *out_x = m[0] * x + m[1] * y + m[2] * z + m[3];
  *out_y = m[4] * x + m[5] * y + m[6] * z + m[7];
  *out_z = m[8] * x + m[9] * y + m[10] * z + m[11];
}

#ifdef __cplusplus
}  // extern "C"
#endif
