#include "math.h"
#include "math_ppc.h"
#include "trigf.h"

#include <MetroTRK/intrinsics.h>

#if defined(__AVX512F__)
#include <immintrin.h>
#endif

#define __epsilon 3.45266983e-4f

#define __HI(x) (((s32*) &x)[0])

extern f32 __sincos_on_quadrant[];
extern f32 __sincos_poly[];

const f32 tmp_float[] = { 0.25f, 0.0232393741608f, 1.70555722434e-7f,
                          1.86736494323e-11f };
f32 __four_over_pi_m1[] = { 0.0f, 0.0f, 0.0f, 0.0f };

void __sinit_trigf_c(void)
{
    __four_over_pi_m1[0] = tmp_float[0];
    __four_over_pi_m1[1] = tmp_float[1];
    __four_over_pi_m1[2] = tmp_float[2];
    __four_over_pi_m1[3] = tmp_float[3];
}

SECTION_CTORS void* const __sinit_trigf_c_reference = __sinit_trigf_c;

// MWCC contracts the reduction and polynomial accumulates into scalar-single
// fused ops; preserve those rounding boundaries explicitly.
// GALE01 sinf 0x803263D4..0x80326574, cosf 0x80326240..0x803263D0.
f32 sinf(f32 x)
{
    int n;
    f32 y;
    f32 ysq;
    f32 z;

    z = (2.0f / (f32) M_PI) * x;
    n = (__HI(x) & 0x80000000) ? (int) (z - 0.5f) : (int) (z + 0.5f);

    y = __fmadds(
        __four_over_pi_m1[3], x,
        __fmadds(__four_over_pi_m1[2], x,
                 __fmadds(__four_over_pi_m1[1], x,
                          __fmadds(__four_over_pi_m1[0], x, x - n * 2))));
    n &= 3;

    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        return __fmadds(__sincos_poly[9], __sincos_on_quadrant[n + 1] * y,
                        __sincos_on_quadrant[n]);
    }

    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        z = __fmadds(
            ysq,
            __fmadds(ysq,
                     __fmadds(ysq,
                              __fmadds(__sincos_poly[0], ysq,
                                       __sincos_poly[2]),
                              __sincos_poly[4]),
                     __sincos_poly[6]),
            __sincos_poly[8]);

        return z * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = __fmadds(
                ysq,
                __fmadds(ysq,
                         __fmadds(ysq,
                                  __fmadds(__sincos_poly[1], ysq,
                                           __sincos_poly[3]),
                                  __sincos_poly[5]),
                         __sincos_poly[7]),
                __sincos_poly[9]) *
            y;
        return z * __sincos_on_quadrant[n + 1];
    }
}

f32 cosf(f32 x)
{
    int n;
    f32 y;
    f32 ysq;
    f32 z;

    z = (2.0f / (f32) M_PI) * x;
    n = (__HI(x) & 0x80000000) ? (int) (z - 0.5f) : (int) (z + 0.5f);

    y = __fmadds(
        __four_over_pi_m1[3], x,
        __fmadds(__four_over_pi_m1[2], x,
                 __fmadds(__four_over_pi_m1[1], x,
                          __fmadds(__four_over_pi_m1[0], x, x - n * 2))));
    n &= 3;
    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        return __fnmsubs(y, __sincos_on_quadrant[n],
                         __sincos_on_quadrant[n + 1]);
    }

    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        // Retail folds the source negation into a final fnmadds.
        z = -__fmadds(
                ysq,
                __fmadds(ysq,
                         __fmadds(ysq,
                                  __fmadds(__sincos_poly[1], ysq,
                                           __sincos_poly[3]),
                                  __sincos_poly[5]),
                         __sincos_poly[7]),
                __sincos_poly[9]) *
            y;
        return z * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = __fmadds(
            ysq,
            __fmadds(ysq,
                     __fmadds(ysq,
                              __fmadds(__sincos_poly[0], ysq,
                                       __sincos_poly[2]),
                              __sincos_poly[4]),
                     __sincos_poly[6]),
            __sincos_poly[8]);
        return z * __sincos_on_quadrant[n + 1];
    }
}

#if defined(MSL_CORE_NATIVE)
#if defined(__AVX512F__)
// Packed acosf/atanf from runtime/math.c, preserving every rounded operation.
// refs/melee/src/melee/lb/lbrefract.c:742-961
static __m512 msl_acosf_unit_vector(__m512 x)
{
    const __m512 one = _mm512_set1_ps(1.0F);
    const __m512 half = _mm512_set1_ps(0.5F);
    const __m512 three = _mm512_set1_ps(3.0F);
    const __m512 pi_half = _mm512_set1_ps(1.5707963705062866F);
    const __m512i sign = _mm512_set1_epi32(0x80000000);
    const __m512i base_lo = _mm512_loadu_si512(msl_frsqrte_base);
    const __m512i base_hi = _mm512_loadu_si512(msl_frsqrte_base + 16);
    const __m512i step_lo = _mm512_loadu_si512(msl_frsqrte_decrement);
    const __m512i step_hi = _mm512_loadu_si512(msl_frsqrte_decrement + 16);

    __m512 radicand = _mm512_fnmadd_ps(x, x, one);
    __m512i bits = _mm512_castps_si512(radicand);
    __m512i exponent = _mm512_srli_epi32(bits, 23);
    __m512i selector = _mm512_xor_si512(
        _mm512_and_si512(_mm512_srli_epi32(bits, 19),
                         _mm512_set1_epi32(31)),
        _mm512_set1_epi32(16));
    __m512i fraction = _mm512_sub_epi32(
        _mm512_permutex2var_epi32(base_lo, selector, base_hi),
        _mm512_mullo_epi32(
            _mm512_permutex2var_epi32(step_lo, selector, step_hi),
            _mm512_and_si512(_mm512_srli_epi32(bits, 8),
                             _mm512_set1_epi32(2047))));
    __m512 guess;
    __m512 magnitude;
    __m512 result;
    __m512 squared;
    __m512 polynomial;
    __m512i index = _mm512_setzero_si512();
    __m512i x_sign;
    __mmask16 high;
    __mmask16 middle;
    int iteration;

    // Same rounded normal-binary32 Gekko seed as runtime/math.c.
    guess = _mm512_cvtepi32_ps(
        _mm512_add_epi32(fraction, _mm512_set1_epi32(0x4000000)));
    exponent = _mm512_slli_epi32(
        _mm512_srli_epi32(
            _mm512_sub_epi32(_mm512_set1_epi32(328), exponent), 1), 23);
    guess = _mm512_mul_ps(guess, _mm512_castsi512_ps(exponent));
    for (iteration = 0; iteration < 3; ++iteration) {
        guess = _mm512_mul_ps(
            _mm512_mul_ps(half, guess),
            _mm512_fnmadd_ps(_mm512_mul_ps(guess, guess), radicand, three));
    }
    x = _mm512_mul_ps(x, guess);
    bits = _mm512_castps_si512(x);
    x_sign = _mm512_and_si512(bits, sign);
    bits = _mm512_andnot_si512(sign, bits);
    magnitude = _mm512_castsi512_ps(bits);
    high = _mm512_cmp_ps_mask(magnitude, _mm512_set1_ps(2.4142136573791504F),
                              _CMP_GE_OQ);
    middle = _mm512_cmp_ps_mask(magnitude, _mm512_set1_ps(0.4142135679721832F),
                                _CMP_GT_OQ) & ~high;
#define MSL_ATAN_THRESHOLD(value)                                            \
    index = _mm512_mask_add_epi32(                                          \
        index, _mm512_cmp_epi32_mask(bits, _mm512_set1_epi32(value),         \
                                     _MM_CMPINT_GE),                        \
        index, _mm512_set1_epi32(1))
    MSL_ATAN_THRESHOLD(0x3F08D5B9);
    MSL_ATAN_THRESHOLD(0x3F521801);
    MSL_ATAN_THRESHOLD(0x3F9BF7EC);
    MSL_ATAN_THRESHOLD(0x3FEF789E);
#undef MSL_ATAN_THRESHOLD
    // Index zero represents the scalar lookup_index == -1 case.
    index = _mm512_maskz_add_epi32(middle, index, _mm512_set1_epi32(1));
#define MSL_ATAN_COLUMN(offset)                                             \
    _mm512_permutexvar_ps(                                                  \
        index, _mm512_maskz_loadu_ps(0x3F, &msl_atanf_lookup[(offset) - 1]))
    {
        __m512 offset_33 = MSL_ATAN_COLUMN(33);
        __m512 offset_39 = MSL_ATAN_COLUMN(39);
        __m512 denominator = _mm512_add_ps(
            offset_33, _mm512_add_ps(magnitude, offset_39));
        __m512 reciprocal;
        __m512 reduced;
        denominator = _mm512_mask_mov_ps(one, middle, denominator);
        denominator = _mm512_mask_mov_ps(denominator, high, magnitude);
        reciprocal = _mm512_div_ps(one, denominator);
        reduced = _mm512_sub_ps(
            _mm512_fnmadd_ps(reciprocal, MSL_ATAN_COLUMN(13), offset_39),
            _mm512_fmsub_ps(reciprocal, MSL_ATAN_COLUMN(7), offset_33));
        result = _mm512_mask_mov_ps(magnitude, high, reciprocal);
        result = _mm512_mask_mov_ps(result, middle, reduced);
    }
    squared = _mm512_mul_ps(result, result);
    polynomial = _mm512_set1_ps(msl_atanf_lookup[6]);
    for (iteration = 5; iteration >= 1; --iteration) {
        polynomial = _mm512_fmadd_ps(
            squared, polynomial, _mm512_set1_ps(msl_atanf_lookup[iteration]));
    }
    result = _mm512_fmadd_ps(_mm512_mul_ps(result, squared), polynomial, result);
    result = _mm512_add_ps(result, MSL_ATAN_COLUMN(27));
    result = _mm512_add_ps(result, MSL_ATAN_COLUMN(20));
#undef MSL_ATAN_COLUMN
    {
        __m512 large = _mm512_sub_ps(result, pi_half);
        large = _mm512_castsi512_ps(_mm512_xor_si512(
            _mm512_castps_si512(large), _mm512_xor_si512(x_sign, sign)));
        result = _mm512_castsi512_ps(_mm512_or_si512(
            _mm512_castps_si512(result), x_sign));
        result = _mm512_mask_mov_ps(result, high, large);
    }
    return _mm512_sub_ps(pi_half, result);
}

static void msl_sincosf_vector(__m512 x, __m512* sin_out, __m512* cos_out)
{
    __m512 z = _mm512_mul_ps(x, _mm512_set1_ps(2.0f / (f32) M_PI));
    __m512i x_bits = _mm512_castps_si512(x);
    __mmask16 negative =
        _mm512_movepi32_mask(_mm512_srai_epi32(x_bits, 31));
    __m512 rounded = _mm512_mask_blend_ps(
        negative, _mm512_add_ps(z, _mm512_set1_ps(0.5f)),
        _mm512_sub_ps(z, _mm512_set1_ps(0.5f)));
    __m512i n = _mm512_cvttps_epi32(rounded);
    __m512 y = _mm512_sub_ps(
        x, _mm512_cvtepi32_ps(_mm512_slli_epi32(n, 1)));
    __m512 ysq;
    __m512 sin_poly;
    __m512 cos_poly;
    __m512 sign;
    __m512 normal_sin;
    __m512 normal_cos;
    __m512 small_sin;
    __m512 small_cos;
    __mmask16 odd;
    __mmask16 small;

    y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[0]), y);
    y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[1]), y);
    y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[2]), y);
    y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[3]), y);
    odd = _mm512_test_epi32_mask(n, _mm512_set1_epi32(1));
    sign = _mm512_castsi512_ps(_mm512_slli_epi32(
        _mm512_and_epi32(n, _mm512_set1_epi32(2)), 30));
    small = _mm512_cmp_ps_mask(
        _mm512_abs_ps(y), _mm512_set1_ps(__epsilon), _CMP_LT_OQ);
    if (small == (__mmask16) 0xFFFF) {
        small_sin = _mm512_fmadd_ps(
            y, _mm512_set1_ps(__sincos_poly[9]),
            _mm512_setzero_ps());
        small_sin = _mm512_mask_mov_ps(
            small_sin, odd, _mm512_set1_ps(1.0F));
        small_cos = _mm512_mask_sub_ps(
            _mm512_set1_ps(1.0F), odd, _mm512_setzero_ps(), y);
        small_sin = _mm512_xor_ps(small_sin, sign);
        small_cos = _mm512_xor_ps(small_cos, sign);
        *sin_out = small_sin;
        *cos_out = small_cos;
        return;
    }
    ysq = _mm512_mul_ps(y, y);

    cos_poly = _mm512_fmadd_ps(
        _mm512_set1_ps(__sincos_poly[0]), ysq,
        _mm512_set1_ps(__sincos_poly[2]));
    cos_poly = _mm512_fmadd_ps(cos_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[4]));
    cos_poly = _mm512_fmadd_ps(cos_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[6]));
    cos_poly = _mm512_fmadd_ps(cos_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[8]));
    sin_poly = _mm512_fmadd_ps(
        _mm512_set1_ps(__sincos_poly[1]), ysq,
        _mm512_set1_ps(__sincos_poly[3]));
    sin_poly = _mm512_fmadd_ps(sin_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[5]));
    sin_poly = _mm512_fmadd_ps(sin_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[7]));
    sin_poly = _mm512_fmadd_ps(sin_poly, ysq,
                               _mm512_set1_ps(__sincos_poly[9]));

    normal_sin = _mm512_mul_ps(sin_poly, y);
    normal_cos = _mm512_mask_sub_ps(
        cos_poly, odd, _mm512_setzero_ps(), normal_sin);
    normal_sin = _mm512_mask_mov_ps(normal_sin, odd, cos_poly);
    small_sin = _mm512_fmadd_ps(
        y, _mm512_set1_ps(__sincos_poly[9]),
        _mm512_setzero_ps());
    small_sin = _mm512_mask_mov_ps(
        small_sin, odd, _mm512_set1_ps(1.0F));
    small_cos = _mm512_mask_sub_ps(
        _mm512_set1_ps(1.0F), odd, _mm512_setzero_ps(), y);
    normal_sin = _mm512_mask_mov_ps(normal_sin, small, small_sin);
    normal_cos = _mm512_mask_mov_ps(normal_cos, small, small_cos);
    normal_sin = _mm512_xor_ps(normal_sin, sign);
    normal_cos = _mm512_xor_ps(normal_cos, sign);
    *sin_out = normal_sin;
    *cos_out = normal_cos;
}
#endif

// refs/melee/src/sysdolphin/baselib/quatlib.c::HSD_QuatLib_8037EF28.
// Compute the general slerp's acos, three sines and two divisions together.
void msl_slerp_weights_many(const float* cosines, float weight,
                            float* first, float* second, int count)
{
    int i = 0;
#if defined(__AVX512F__)
    __m512 p = _mm512_set1_ps(1.0F - weight);
    __m512 q = _mm512_set1_ps(weight);
    for (; i < count; i += 16) {
        int remaining = count - i;
        __mmask16 lanes = remaining >= 16 ? (__mmask16) 0xFFFF
                                         : (__mmask16) ((1U << remaining) - 1U);
        __m512 theta = msl_acosf_unit_vector(
            _mm512_maskz_loadu_ps(lanes, cosines + i));
        __m512 sinom, sp, sq, unused;
        msl_sincosf_vector(theta, &sinom, &unused);
        msl_sincosf_vector(_mm512_mul_ps(p, theta), &sp, &unused);
        msl_sincosf_vector(_mm512_mul_ps(q, theta), &sq, &unused);
        _mm512_mask_storeu_ps(first + i, lanes, _mm512_div_ps(sp, sinom));
        _mm512_mask_storeu_ps(second + i, lanes, _mm512_div_ps(sq, sinom));
    }
#else
    for (; i < count; ++i) {
        float theta = acosf(cosines[i]);
        float angles[3] = { theta, (1.0F - weight) * theta, weight * theta };
        float sine[3], cosine[3];
        msl_sincosf_many(angles, sine, cosine, 3);
        first[i] = sine[1] / sine[0];
        second[i] = sine[2] / sine[0];
    }
#endif
}

void msl_sincosf_many(const f32* xyz, f32* sin_out, f32* cos_out, int count)
{
    int i = 0;

#if defined(__AVX512F__)
    for (; i < count; i += 16) {
        int remaining = count - i;
        __mmask16 lanes = remaining >= 16 ? (__mmask16) 0xFFFF
                                         : (__mmask16) ((1U << remaining) - 1U);
        __m512 sine, cosine;
        msl_sincosf_vector(_mm512_maskz_loadu_ps(lanes, xyz + i), &sine, &cosine);
        _mm512_mask_storeu_ps(sin_out + i, lanes, sine);
        _mm512_mask_storeu_ps(cos_out + i, lanes, cosine);
    }
#endif

    for (; i < count; i++) {
        int n;
        f32 x = xyz[i];
        f32 y;
        f32 ysq;
        f32 sin_z;
        f32 cos_z;

        sin_z = (2.0f / (f32) M_PI) * x;
        n = (__HI(x) & 0x80000000) ? (int) (sin_z - 0.5f)
                                   : (int) (sin_z + 0.5f);

        // Match the fused sinf/cosf rounding boundaries above.
        y = __fmadds(
            __four_over_pi_m1[3], x,
            __fmadds(__four_over_pi_m1[2], x,
                     __fmadds(__four_over_pi_m1[1], x,
                              __fmadds(__four_over_pi_m1[0], x,
                                       x - n * 2))));
        n &= 3;

        if (__builtin_fabsf(y) < __epsilon) {
            n <<= 1;
            sin_out[i] = __fmadds(__sincos_poly[9],
                                  __sincos_on_quadrant[n + 1] * y,
                                  __sincos_on_quadrant[n]);
            cos_out[i] = __fnmsubs(y, __sincos_on_quadrant[n],
                                   __sincos_on_quadrant[n + 1]);
            continue;
        } else {
            f32 cos_poly;
            f32 sin_poly;
            ysq = y * y;
            cos_poly = __fmadds(
                ysq,
                __fmadds(ysq,
                         __fmadds(ysq,
                                  __fmadds(__sincos_poly[0], ysq,
                                           __sincos_poly[2]),
                                  __sincos_poly[4]),
                         __sincos_poly[6]),
                __sincos_poly[8]);
            sin_poly = __fmadds(
                ysq,
                __fmadds(ysq,
                         __fmadds(ysq,
                                  __fmadds(__sincos_poly[1], ysq,
                                           __sincos_poly[3]),
                                  __sincos_poly[5]),
                         __sincos_poly[7]),
                __sincos_poly[9]);
            if (n & 1) {
                n <<= 1;
                sin_z = cos_poly;
                cos_z = -sin_poly * y;
                sin_out[i] = sin_z * __sincos_on_quadrant[n];
                cos_out[i] = cos_z * __sincos_on_quadrant[n];
            } else {
                n <<= 1;
                sin_z = sin_poly * y;
                cos_z = cos_poly;
                sin_out[i] = sin_z * __sincos_on_quadrant[n + 1];
                cos_out[i] = cos_z * __sincos_on_quadrant[n + 1];
            }
        }
    }
}

void msl_sincosf3(const f32 xyz[3], f32 sin_out[3], f32 cos_out[3])
{
    msl_sincosf_many(xyz, sin_out, cos_out, 3);
}
#endif

#pragma dont_inline on

f32 sin__Ff(f32 x)
{
    return sinf(x);
}

f32 cos__Ff(f32 x)
{
    return cosf(x);
}

#pragma dont_inline reset

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
static inline void msl_tanf_sincos(f32 x, f32* sin_out, f32* cos_out)
{
    int n;
    f32 y;
    f32 ysq;
    f32 sin_z;
    f32 cos_z;

    sin_z = (2.0f / (f32) M_PI) * x;
    n = (__HI(x) & 0x80000000) ? (int) (sin_z - 0.5f)
                               : (int) (sin_z + 0.5f);
    y = __fmadds(
        __four_over_pi_m1[3], x,
        __fmadds(__four_over_pi_m1[2], x,
                 __fmadds(__four_over_pi_m1[1], x,
                          __fmadds(__four_over_pi_m1[0], x, x - n * 2))));
    n &= 3;

    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        *sin_out = __fmadds(__sincos_poly[9],
                            __sincos_on_quadrant[n + 1] * y,
                            __sincos_on_quadrant[n]);
        *cos_out = __fnmsubs(y, __sincos_on_quadrant[n],
                             __sincos_on_quadrant[n + 1]);
        return;
    }

    ysq = y * y;
    cos_z = __fmadds(
        ysq,
        __fmadds(ysq,
                 __fmadds(ysq,
                          __fmadds(__sincos_poly[0], ysq,
                                   __sincos_poly[2]),
                          __sincos_poly[4]),
                 __sincos_poly[6]),
        __sincos_poly[8]);
    sin_z = __fmadds(
        ysq,
        __fmadds(ysq,
                 __fmadds(ysq,
                          __fmadds(__sincos_poly[1], ysq,
                                   __sincos_poly[3]),
                          __sincos_poly[5]),
                 __sincos_poly[7]),
        __sincos_poly[9]);
    if (n & 1) {
        n <<= 1;
        *sin_out = cos_z * __sincos_on_quadrant[n];
        *cos_out = (-sin_z * y) * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        *sin_out = (sin_z * y) * __sincos_on_quadrant[n + 1];
        *cos_out = cos_z * __sincos_on_quadrant[n + 1];
    }
}
#endif

f32 tanf(f32 x)
{
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_WASM)
    f32 sin_x;
    f32 cos_x;

    msl_tanf_sincos(x, &sin_x, &cos_x);
    return sin_x / cos_x;
#else
    return sin__Ff(x) / cos__Ff(x);
#endif
}
