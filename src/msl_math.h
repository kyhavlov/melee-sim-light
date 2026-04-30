#pragma once

#include <stdint.h>
#include <string.h>

// Math constants used in decomp-first gameplay logic.
//
// Pi source-of-truth (GALE01):
// - refs/melee/src/MSL/math.h (M_PI)
// - used by ftFx_SpecialN_CreateBlasterShot for angle mirroring (M_PI - angle)
//   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
#define MSL_PI_F 3.14159265358979323846f
#define MSL_TAU_F 6.28318530717958647692f
#define MSL_PI_2_F 1.57079632679489661923f

static inline uint32_t msl_float_bits(float v) {
  uint32_t bits = 0u;
  memcpy(&bits, &v, sizeof(bits));
  return bits;
}

static inline float msl_float_from_bits(uint32_t bits) {
  float v = 0.0f;
  memcpy(&v, &bits, sizeof(v));
  return v;
}

static inline float msl_fnmsubs_f32(float a, float b, float c) { return c - (a * b); }

// GALE01 single-precision atan approximation used by item laser angle writes.
//
// Source path:
// - refs/melee/src/melee/lb/lbrefract.c::{atan2f,atanf}
// - refs/melee/build/GALE01/asm/melee/it/items/itfoxlaser.s::it_2725_Logic94_ShieldBounced
//
// Keep this local to gameplay code that needs the serialized game float bits. Host libc atan2f can
// differ by one ULP, which is visible through Slippi SendItemInfo's raw xDD4 metadata bytes.
static inline float msl_melee_atanf(float x) {
  static const float k_lookup[] = {
      1.0f,
      -0.3333333134651184f,
      0.1999988704919815f,
      -0.14281649887561798f,
      0.11041180044412613f,
      -0.08459755778312683f,
      0.04714243486523628f,
      6.828420162200928f,
      3.239828109741211f,
      2.0f,
      1.4464620351791382f,
      1.1715729236602783f,
      1.039566159248352f,
      7.1350000325764995e-06f,
      8.200000252145401e-07f,
      0.0f,
      6.299999881775875e-07f,
      0.0f,
      0.0f,
      0.0f,
      0.3926900029182434f,
      0.5890486240386963f,
      0.7853981256484985f,
      0.9817469716072083f,
      1.1780970096588135f,
      1.3744460344314575f,
      0.0f,
      9.081698408408556e-06f,
      2.3000000126671694e-08f,
      6.30000016599297e-08f,
      7.040000014058023e-07f,
      2.499999993688107e-07f,
      7.900000014160469e-07f,
      2.414212942123413f,
      1.4966057538986206f,
      1.0f,
      0.6681786179542542f,
      0.4142135679721832f,
      0.1989123672246933f,
      5.620000251838064e-07f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
  };

  uint32_t x_bits = msl_float_bits(x);
  const uint32_t sign_bit = x_bits & UINT32_C(0x80000000);
  x_bits &= ~UINT32_C(0x80000000);
  x = msl_float_from_bits(x_bits);

  int lookup_index = -1;
  uint8_t x_ge_ratio = 0u;
  float result = x;
  if (x >= 2.4142136573791504f) {
    x_ge_ratio = 1u;
    result = 1.0f / x;
  } else if (x > 0.4142135679721832f) {
    lookup_index = 0;
    const uint32_t exponent = x_bits & UINT32_C(0x7F800000);
    if (exponent == UINT32_C(0x3F000000)) {
      if (!((int32_t)x_bits < (int32_t)UINT32_C(0x3F08D5B9))) {
        lookup_index = 1;
      }
      if (!((int32_t)x_bits < (int32_t)UINT32_C(0x3F521801))) {
        lookup_index += 1;
      }
    } else if (exponent == UINT32_C(0x3F800000)) {
      lookup_index = 2;
      if (!((int32_t)x_bits < (int32_t)UINT32_C(0x3F9BF7EC))) {
        lookup_index = 3;
      }
      if (!((int32_t)x_bits < (int32_t)UINT32_C(0x3FEF789E))) {
        lookup_index += 1;
      }
    } else if (exponent == UINT32_C(0x40000000)) {
      lookup_index = 4;
    }

    const float* lookup_ptr = &k_lookup[lookup_index];
    const float offset_39 = lookup_ptr[39];
    const float offset_33 = lookup_ptr[33];
    result = 1.0f / (offset_33 + (x + offset_39));
    result = msl_fnmsubs_f32(result, lookup_ptr[7], offset_33) +
             msl_fnmsubs_f32(result, lookup_ptr[13], offset_39);
  }

  const float result_squared = result * result;
  float poly = (result_squared * k_lookup[6]) + k_lookup[5];
  poly = (result_squared * poly) + k_lookup[4];
  poly = (result_squared * poly) + k_lookup[3];
  poly = (result_squared * poly) + k_lookup[2];
  poly = (result_squared * poly) + k_lookup[1];
  result = ((result * result_squared) * poly) + result;

  if (lookup_index >= 0) {
    result += k_lookup[lookup_index + 27];
    result += k_lookup[lookup_index + 20];
  } else {
    result += k_lookup[26];
    result += k_lookup[19];
  }

  if (x_ge_ratio) {
    result -= MSL_PI_2_F;
    return sign_bit ? result : -result;
  }

  return msl_float_from_bits(msl_float_bits(result) | sign_bit);
}

static inline float msl_melee_atan2f(float y, float x) {
  const uint32_t x_sign = msl_float_bits(x) & UINT32_C(0x80000000);
  const uint32_t y_sign = msl_float_bits(y) & UINT32_C(0x80000000);
  if (x_sign == y_sign) {
    if (x_sign != 0u) {
      return (msl_float_bits(x) == UINT32_C(0x80000000)) ? -MSL_PI_2_F
                                                         : (msl_melee_atanf(y / x) - MSL_PI_F);
    }
    return (x != 0.0f) ? msl_melee_atanf(y / x) : MSL_PI_2_F;
  }
  if (x < 0.0f) {
    return MSL_PI_F + msl_melee_atanf(y / x);
  }
  if (x != 0.0f) {
    return msl_melee_atanf(y / x);
  }
  return msl_float_from_bits(y_sign + UINT32_C(0x3FC90FDB));
}

static inline float msl_melee_normalize_angle(float angle) {
  while (angle < 0.0f) {
    angle += MSL_TAU_F;
  }
  while (angle > MSL_TAU_F) {
    angle -= MSL_TAU_F;
  }
  return angle;
}
