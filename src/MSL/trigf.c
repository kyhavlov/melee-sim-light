#include "math.h"

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
void msl_sincosf_many(const f32* xyz, f32* sin_out, f32* cos_out, int count)
{
    int i = 0;

#if defined(__AVX512F__)
    for (; i < count; i += 16) {
        int remaining = count - i;
        __mmask16 lanes = remaining >= 16
                             ? (__mmask16) 0xFFFF
                             : (__mmask16) ((1U << remaining) - 1U);
        __m512 x = _mm512_maskz_loadu_ps(lanes, &xyz[i]);
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
        __m512 base;
        __m512 next;
        __m512 normal_sin;
        __m512 normal_cos;
        __m512 small_sin;
        __m512 small_cos;
        __m512i quadrant;
        __mmask16 even;
        __mmask16 small;

        y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[0]), y);
        y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[1]), y);
        y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[2]), y);
        y = _mm512_fmadd_ps(x, _mm512_set1_ps(__four_over_pi_m1[3]), y);
        quadrant = _mm512_and_epi32(_mm512_slli_epi32(n, 1),
                                    _mm512_set1_epi32(6));
        base = _mm512_i32gather_ps(quadrant, __sincos_on_quadrant, 4);
        next = _mm512_i32gather_ps(_mm512_add_epi32(
                                       quadrant, _mm512_set1_epi32(1)),
                                   __sincos_on_quadrant, 4);
        small = _mm512_cmp_ps_mask(
            _mm512_abs_ps(y), _mm512_set1_ps(__epsilon), _CMP_LT_OQ);
        if (small == (__mmask16) 0xFFFF) {
            small_sin = _mm512_fmadd_ps(
                _mm512_mul_ps(next, y),
                _mm512_set1_ps(__sincos_poly[9]), base);
            small_cos = _mm512_fnmadd_ps(y, base, next);
            _mm512_mask_storeu_ps(&sin_out[i], lanes, small_sin);
            _mm512_mask_storeu_ps(&cos_out[i], lanes, small_cos);
            continue;
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

        even = _mm512_cmpeq_epi32_mask(
            _mm512_and_epi32(n, _mm512_set1_epi32(1)),
            _mm512_setzero_si512());
        normal_sin = _mm512_mask_blend_ps(
            even, _mm512_mul_ps(cos_poly, base),
            _mm512_mul_ps(_mm512_mul_ps(sin_poly, y), next));
        normal_cos = _mm512_mask_blend_ps(
            even,
            _mm512_mul_ps(_mm512_mul_ps(_mm512_sub_ps(
                                            _mm512_setzero_ps(), sin_poly),
                                        y),
                          base),
            _mm512_mul_ps(cos_poly, next));
        small_sin = _mm512_fmadd_ps(
            _mm512_mul_ps(next, y),
            _mm512_set1_ps(__sincos_poly[9]), base);
        small_cos = _mm512_fnmadd_ps(y, base, next);
        _mm512_mask_storeu_ps(
            &sin_out[i], lanes,
            _mm512_mask_blend_ps(small, normal_sin, small_sin));
        _mm512_mask_storeu_ps(
            &cos_out[i], lanes,
            _mm512_mask_blend_ps(small, normal_cos, small_cos));
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

f32 tanf(f32 x)
{
    return sin__Ff(x) / cos__Ff(x);
}
