#pragma once

// Source-owned scalar constants from lb/lbcollision.c. Keep these named by the decomp symbol so BODY
// reduced geometry paths do not grow independent literal copies.
//
// refs/melee/src/melee/lb/lbcollision.c::lbColl_804D7A38
enum {
  MSL_LBCOLL_BODY_HURT_RADIUS_MUL_I = 3,
};

static inline float msl_lbcoll_body_hurt_radius_mul(void) {
  return (float)MSL_LBCOLL_BODY_HURT_RADIUS_MUL_I;
}
