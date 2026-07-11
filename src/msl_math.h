#pragma once

#include <math.h>
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

static inline uint64_t msl_double_bits(double v) {
  uint64_t bits = 0u;
  memcpy(&bits, &v, sizeof(bits));
  return bits;
}

static inline double msl_double_from_bits(uint64_t bits) {
  double v = 0.0;
  memcpy(&v, &bits, sizeof(v));
  return v;
}

// Gekko scalar-single multiply rounds its FC operand to a 25-bit significand before the multiply.
// refs/Ishiiruka/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h::Force25Bit
// refs/Ishiiruka/Source/Core/Core/PowerPC/Interpreter/Interpreter_FloatingPoint.cpp::fmulsx
static inline double msl_ppc_force_25_bit(double value) {
  uint64_t bits = msl_double_bits(value);
  bits = (bits & UINT64_C(0xFFFFFFFFF8000000)) + (bits & UINT64_C(0x0000000008000000));
  return msl_double_from_bits(bits);
}

// Gekko `frsqrte` estimate used by Dolphin SDK's PSVECNormalize.
//
// Melee does not normalize MapLine vectors with libm sqrt/division. `mpLineGetNormal` builds the
// perpendicular and calls PSVECNormalize, whose paired-single implementation performs one
// `frsqrte` estimate and one Newton step. The estimate table below is the instruction's specified
// mantissa interpolation; keeping it here makes static and transformed stage normals bit-stable on
// non-PPC hosts.
// refs/melee/src/melee/mp/mplib.c::mpLineGetNormal
// refs/melee/build/GALE01/asm/dolphin/mtx/vec.s::PSVECNormalize
// refs/Ishiiruka/Source/Core/Common/MathUtil.cpp::ApproximateReciprocalSquareRoot
static inline double msl_ppc_frsqrte(double value) {
  static const int32_t k_base[32] = {
      0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000, 0x2fb7000, 0x2d26000, 0x2ac0000,
      0x2881000, 0x2665000, 0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000, 0x1bf4000,
      0x1a7e800, 0x17cb800, 0x1552800, 0x130c000, 0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000,
      0x09e5000, 0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800, 0x0218800, 0x0105800,
  };
  static const int32_t k_dec[32] = {
      0x7a4, 0x700, 0x670, 0x5f2, 0x584, 0x524, 0x4cc, 0x47e, 0x43a, 0x3fa, 0x3c2,
      0x38e, 0x35e, 0x332, 0x30a, 0x2e6, 0x568, 0x4f3, 0x48d, 0x435, 0x3e7, 0x3a2,
      0x365, 0x32e, 0x2fc, 0x2d0, 0x2a8, 0x283, 0x261, 0x243, 0x226, 0x20b,
  };
  const uint64_t fraction_mask = (UINT64_C(1) << 52) - UINT64_C(1);
  const uint64_t exponent_mask = UINT64_C(0x7FF) << 52;
  const uint64_t sign_mask = UINT64_C(1) << 63;
  const uint64_t implicit_bit = UINT64_C(1) << 52;

  uint64_t bits = msl_double_bits(value);
  uint64_t mantissa = bits & fraction_mask;
  const uint64_t sign = bits & sign_mask;
  int64_t exponent = (int64_t)(bits & exponent_mask);

  if (mantissa == 0u && exponent == 0) {
    return sign != 0u ? -INFINITY : INFINITY;
  }
  if ((uint64_t)exponent == exponent_mask) {
    if (mantissa == 0u) {
      return sign != 0u ? NAN : 0.0;
    }
    return 0.0 + value;
  }
  if (sign != 0u) {
    return NAN;
  }
  if (exponent == 0) {
    do {
      exponent -= (int64_t)implicit_bit;
      mantissa <<= 1;
    } while ((mantissa & implicit_bit) == 0u);
    mantissa &= fraction_mask;
    exponent += (int64_t)implicit_bit;
  }

  const uint8_t odd_exponent = (exponent & (int64_t)implicit_bit) == 0 ? 1u : 0u;
  exponent =
      ((int64_t)(UINT64_C(0x3FF) << 52) - ((exponent - (int64_t)(UINT64_C(0x3FE) << 52)) / 2)) &
      (int64_t)exponent_mask;
  const uint32_t interpolation = (uint32_t)(mantissa >> 37);
  const uint32_t table_index = interpolation / 2048u + (odd_exponent != 0u ? 16u : 0u);
  const uint64_t estimate_fraction =
      (uint64_t)(k_base[table_index] - k_dec[table_index] * (int32_t)(interpolation % 2048u)) << 26;
  return msl_double_from_bits(sign | (uint64_t)exponent | estimate_fraction);
}

static inline uint8_t msl_psvec2_normalize(float x, float y, float* x_out, float* y_out) {
  if (x_out == NULL || y_out == NULL) {
    return 0u;
  }
  const float length_sq = (x * x) + (y * y);
  if (!(length_sq > 0.0f) || !isfinite(length_sq)) {
    return 0u;
  }

  const double estimate = msl_ppc_frsqrte((double)length_sq);
  // `fmuls f6,f5,f5` rounds its FC operand through Force25Bit before multiplication.
  // refs/melee/build/GALE01/asm/dolphin/mtx/vec.s::PSVECNormalize
  const float estimate_sq = (float)(estimate * msl_ppc_force_25_bit(estimate));
  const float estimate_half = (float)(estimate * 0.5);
  const float correction = fmaf(-estimate_sq, length_sq, 3.0f);
  const float inverse_length = correction * estimate_half;
  *x_out = x * inverse_length;
  *y_out = y * inverse_length;
  return 1u;
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

// GALE01 single-precision sin/cos approximations used by MSL `trigf.c`.
//
// Source:
// - refs/melee/src/MSL/trigf.c::{sinf,cosf}
// - refs/melee/src/MSL/math_data.c::{__sincos_on_quadrant,__sincos_poly}
//
// These helpers are intentionally local to source-exact gameplay paths. Host libm can differ by a
// few ULPs, and those differences accumulate during long rollout knockback/position streaks.
static inline int msl_melee_trig_quadrant(float x) {
  const float z = (2.0f / MSL_PI_F) * x;
  return (msl_float_bits(x) & UINT32_C(0x80000000)) ? (int)(z - 0.5f) : (int)(z + 0.5f);
}

static inline float msl_melee_trig_reduced(float x, int n) {
  static const float k_four_over_pi_m1[] = {
      0.25f,
      0.0232393741608f,
      1.70555722434e-7f,
      1.86736494323e-11f,
  };
  return x - (float)n * 2.0f + k_four_over_pi_m1[0] * x + k_four_over_pi_m1[1] * x +
         k_four_over_pi_m1[2] * x + k_four_over_pi_m1[3] * x;
}

static inline float msl_melee_sinf(float x) {
  static const float k_on_quadrant[] = {0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f};
  static const float k_poly[] = {
      0.0000035287617f, 0.0000003089747f, -0.0003259365f,
      -0.00003657235f,  0.015854323f,     0.0024903931f,
      -0.30842513f,     -0.08074551f,     1.0f,
      0.7853982f,
  };

  int n = msl_melee_trig_quadrant(x);
  const float y = msl_melee_trig_reduced(x, n);
  n &= 3;

  if (fabsf(y) < 3.45266983e-4f) {
    n <<= 1;
    return k_on_quadrant[n] + (k_on_quadrant[n + 1] * y * k_poly[9]);
  }

  const float ysq = y * y;
  float z;
  if (n & 1) {
    n <<= 1;
    z = (((k_poly[0] * ysq + k_poly[2]) * ysq + k_poly[4]) * ysq + k_poly[6]) * ysq + k_poly[8];
    return z * k_on_quadrant[n];
  }

  n <<= 1;
  z = ((((k_poly[1] * ysq + k_poly[3]) * ysq + k_poly[5]) * ysq + k_poly[7]) * ysq + k_poly[9]) * y;
  return z * k_on_quadrant[n + 1];
}

static inline float msl_melee_cosf(float x) {
  static const float k_on_quadrant[] = {0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f};
  static const float k_poly[] = {
      0.0000035287617f, 0.0000003089747f, -0.0003259365f,
      -0.00003657235f,  0.015854323f,     0.0024903931f,
      -0.30842513f,     -0.08074551f,     1.0f,
      0.7853982f,
  };

  int n = msl_melee_trig_quadrant(x);
  const float y = msl_melee_trig_reduced(x, n);
  n &= 3;

  if (fabsf(y) < 3.45266983e-4f) {
    n <<= 1;
    return k_on_quadrant[n + 1] - y * k_on_quadrant[n];
  }

  const float ysq = y * y;
  float z;
  if (n & 1) {
    n <<= 1;
    z = -(
        ((((k_poly[1] * ysq + k_poly[3]) * ysq + k_poly[5]) * ysq + k_poly[7]) * ysq + k_poly[9]) *
        y);
    return z * k_on_quadrant[n];
  }

  n <<= 1;
  z = (((k_poly[0] * ysq + k_poly[2]) * ysq + k_poly[4]) * ysq + k_poly[6]) * ysq + k_poly[8];
  return z * k_on_quadrant[n + 1];
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
