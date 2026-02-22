#pragma once

#include <math.h>
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

// Closest distance squared between segments P0->P1 and Q0->Q1.
//
// Decomp shape note:
// - lbColl_80006E58 performs a full swept-segment narrow phase between hit and hurt segments.
// - We keep this as reusable geometry scaffolding for BODY overlap activation.
// refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
static inline void combat_segment_segment_dist2(float p0x, float p0y, float p0z, float p1x,
                                                float p1y, float p1z, float q0x, float q0y,
                                                float q0z, float q1x, float q1y, float q1z,
                                                float* out_d2, float* out_s, float* out_t) {
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;

  const float a = msl_dot3(ux, uy, uz, ux, uy, uz);
  const float b = msl_dot3(ux, uy, uz, vx, vy, vz);
  const float c = msl_dot3(vx, vy, vz, vx, vy, vz);
  const float d = msl_dot3(ux, uy, uz, wx, wy, wz);
  const float e = msl_dot3(vx, vy, vz, wx, wy, wz);
  const float D = a * c - b * b;
  const float EPS = 1.0e-8f;

  float sN = 0.0f;
  float sD = D;
  float tN = 0.0f;
  float tD = D;

  if (D < EPS) {
    sN = 0.0f;
    sD = 1.0f;
    tN = e;
    tD = c;
  } else {
    sN = b * e - c * d;
    tN = a * e - b * d;
    if (sN < 0.0f) {
      sN = 0.0f;
      tN = e;
      tD = c;
    } else if (sN > sD) {
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0f) {
    tN = 0.0f;
    if (-d < 0.0f) {
      sN = 0.0f;
    } else if (-d > a) {
      sN = sD;
    } else {
      sN = -d;
      sD = a;
    }
  } else if (tN > tD) {
    tN = tD;
    if ((-d + b) < 0.0f) {
      sN = 0.0f;
    } else if ((-d + b) > a) {
      sN = sD;
    } else {
      sN = -d + b;
      sD = a;
    }
  }

  const float s = (fabsf(sN) < EPS) ? 0.0f : sN / sD;
  const float t = (fabsf(tN) < EPS) ? 0.0f : tN / tD;

  const float dx = wx + s * ux - t * vx;
  const float dy = wy + s * uy - t * vy;
  const float dz = wz + s * uz - t * vz;
  const float d2 = msl_len2_3(dx, dy, dz);

  if (out_d2) {
    *out_d2 = d2;
  }
  if (out_s) {
    *out_s = s;
  }
  if (out_t) {
    *out_t = t;
  }
}
