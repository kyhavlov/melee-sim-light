#pragma once

// Small pose math helpers (hot-path safe; no allocations).

#include <stddef.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// Multiply a row-major 3x4 matrix by a point:
// out = (m00 m01 m02 tx)   (x)
//       (m10 m11 m12 ty) * (y)
//       (m20 m21 m22 tz)   (z)
//
// Notes:
// - This matches the SSANIM01 v5 pose matrix layout used by anim_pose_get_matrix(...).
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

static inline int msl_mtx34_inverse_point(const float m[12], float x, float y, float z,
                                          float* out_x, float* out_y, float* out_z) {
  if (m == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0;
  }
  const float a00 = m[0], a01 = m[1], a02 = m[2];
  const float a10 = m[4], a11 = m[5], a12 = m[6];
  const float a20 = m[8], a21 = m[9], a22 = m[10];
  const float tx = m[3], ty = m[7], tz = m[11];

  const float c00 = a11 * a22 - a12 * a21;
  const float c01 = a02 * a21 - a01 * a22;
  const float c02 = a01 * a12 - a02 * a11;
  const float c10 = a12 * a20 - a10 * a22;
  const float c11 = a00 * a22 - a02 * a20;
  const float c12 = a02 * a10 - a00 * a12;
  const float c20 = a10 * a21 - a11 * a20;
  const float c21 = a01 * a20 - a00 * a21;
  const float c22 = a00 * a11 - a01 * a10;
  const float det = a00 * c00 + a01 * c10 + a02 * c20;
  if (!(fabsf(det) > 1.0e-8f)) {
    return 0;
  }
  const float inv_det = 1.0f / det;
  const float rx = x - tx;
  const float ry = y - ty;
  const float rz = z - tz;
  *out_x = inv_det * (c00 * rx + c01 * ry + c02 * rz);
  *out_y = inv_det * (c10 * rx + c11 * ry + c12 * rz);
  *out_z = inv_det * (c20 * rx + c21 * ry + c22 * rz);
  return 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif
