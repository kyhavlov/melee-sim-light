#pragma once

#include <stdint.h>

static inline float msl_dot3(float ax, float ay, float az, float bx, float by, float bz) {
  return ax * bx + ay * by + az * bz;
}

static inline float msl_len2_3(float x, float y, float z) { return msl_dot3(x, y, z, x, y, z); }

// Returns squared distance from point P to segment AB, and the clamped barycentric t in [0,1].
//
// Decomp reference: the exact point-to-segment distance helper is not modeled as a single symbol in
// GALE01 decomp; collision tests commonly reduce to "closest point on capsule segment" math via
// lbcollision helpers (e.g. refs/melee/src/melee/lb/lbcollision.c). We keep this as pure geometry.
static inline void combat_point_segment_dist2(float px, float py, float pz, float ax, float ay,
                                              float az, float bx, float by, float bz, float* out_d2,
                                              float* out_t) {
  // Compute closest point Q = A + t*(B-A), t clamped to [0,1].
  const float abx = bx - ax;
  const float aby = by - ay;
  const float abz = bz - az;
  const float apx = px - ax;
  const float apy = py - ay;
  const float apz = pz - az;

  const float denom = msl_len2_3(abx, aby, abz);
  float t = 0.0f;
  if (denom > 0.0f) {
    t = msl_dot3(apx, apy, apz, abx, aby, abz) / denom;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }

  const float qx = ax + t * abx;
  const float qy = ay + t * aby;
  const float qz = az + t * abz;

  const float dx = px - qx;
  const float dy = py - qy;
  const float dz = pz - qz;
  const float d2 = msl_len2_3(dx, dy, dz);

  if (out_d2) {
    *out_d2 = d2;
  }
  if (out_t) {
    *out_t = t;
  }
}

// Sphere(center,r_sphere) intersects capsule(segment AB, r_capsule) iff
// dist(point, segment) <= r_sphere + r_capsule.
static inline uint8_t combat_sphere_capsule_intersects(float sx, float sy, float sz, float r_sphere,
                                                       float ax, float ay, float az, float bx,
                                                       float by, float bz, float r_capsule,
                                                       float* out_dist2) {
  float d2 = 0.0f;
  combat_point_segment_dist2(sx, sy, sz, ax, ay, az, bx, by, bz, &d2, NULL);
  if (out_dist2) {
    *out_dist2 = d2;
  }
  const float r = r_sphere + r_capsule;
  return (uint8_t)(d2 <= r * r);
}
