#include <dolphin/mtx.h>

#include <MSL/trigf.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__FMA__)
#include <immintrin.h>
#endif

static uint64_t double_bits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double double_from_bits(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// Gekko frsqrte's specified estimate table. This is the instruction used by
// Dolphin SDK PSVECNormalize before its one Newton step; a host sqrt/divide is
// observably different even for a perfectly horizontal stage line.
// refs/melee/extern/dolphin/src/dolphin/mtx/vec.c::PSVECNormalize
// refs/Ishiiruka/Source/Core/Common/MathUtil.cpp::ApproximateReciprocalSquareRoot
static double ppc_frsqrte(double value)
{
    static const int32_t base[32] = {
        0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000,
        0x2fb7000, 0x2d26000, 0x2ac0000, 0x2881000, 0x2665000,
        0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000,
        0x1bf4000, 0x1a7e800, 0x17cb800, 0x1552800, 0x130c000,
        0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000, 0x09e5000,
        0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800,
        0x0218800, 0x0105800,
    };
    static const int32_t decrement[32] = {
        0x7a4, 0x700, 0x670, 0x5f2, 0x584, 0x524, 0x4cc, 0x47e,
        0x43a, 0x3fa, 0x3c2, 0x38e, 0x35e, 0x332, 0x30a, 0x2e6,
        0x568, 0x4f3, 0x48d, 0x435, 0x3e7, 0x3a2, 0x365, 0x32e,
        0x2fc, 0x2d0, 0x2a8, 0x283, 0x261, 0x243, 0x226, 0x20b,
    };
    const uint64_t fraction_mask = (UINT64_C(1) << 52) - UINT64_C(1);
    const uint64_t exponent_mask = UINT64_C(0x7FF) << 52;
    const uint64_t sign_mask = UINT64_C(1) << 63;
    const uint64_t implicit_bit = UINT64_C(1) << 52;
    uint64_t bits = double_bits(value);
    uint64_t mantissa = bits & fraction_mask;
    uint64_t sign = bits & sign_mask;
    int64_t exponent = (int64_t) (bits & exponent_mask);
    uint32_t interpolation;
    uint32_t index;
    uint64_t fraction;
    int odd_exponent;

    if (mantissa == 0 && exponent == 0) {
        return sign ? -INFINITY : INFINITY;
    }
    if ((uint64_t) exponent == exponent_mask) {
        if (mantissa == 0) {
            return sign ? NAN : 0.0;
        }
        return 0.0 + value;
    }
    if (sign) {
        return NAN;
    }
    if (exponent == 0) {
        do {
            exponent -= (int64_t) implicit_bit;
            mantissa <<= 1;
        } while ((mantissa & implicit_bit) == 0);
        mantissa &= fraction_mask;
        exponent += (int64_t) implicit_bit;
    }

    odd_exponent = (exponent & (int64_t) implicit_bit) == 0;
    exponent =
        ((int64_t) (UINT64_C(0x3FF) << 52) -
         ((exponent - (int64_t) (UINT64_C(0x3FE) << 52)) / 2)) &
        (int64_t) exponent_mask;
    interpolation = (uint32_t) (mantissa >> 37);
    index = interpolation / 2048U + (odd_exponent ? 16U : 0U);
    fraction =
        (uint64_t) (base[index] -
                    decrement[index] * (int32_t) (interpolation % 2048U))
        << 26;
    return double_from_bits(sign | (uint64_t) exponent | fraction);
}

// Gekko fres estimate table. PSMTXInverse performs one Newton step from this
// instruction result before scaling the adjugate matrix.
// refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::PSMTXInverse
static double ppc_fres(double value)
{
    static const uint32_t base[32] = {
        0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200,
        0x579000, 0x520800, 0x4cc800, 0x47ca00, 0x430800, 0x3e8000,
        0x3a2c00, 0x360800, 0x321400, 0x2e4a00, 0x2aa800, 0x272c00,
        0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
        0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000,
        0x041c00, 0x020c00,
    };
    static const uint16_t decrement[32] = {
        0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0,
        0x27f, 0x261, 0x245, 0x22a, 0x212, 0x1fb, 0x1e5, 0x1d1,
        0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e, 0x15b, 0x15b,
        0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106,
    };
    const uint64_t fraction_mask = (UINT64_C(1) << 52) - UINT64_C(1);
    const uint64_t exponent_mask = UINT64_C(0x7ff) << 52;
    const uint64_t sign_mask = UINT64_C(1) << 63;
    uint64_t bits = double_bits(value);
    uint64_t mantissa = bits & fraction_mask;
    uint64_t exponent = bits & exponent_mask;
    uint64_t sign = bits & sign_mask;
    uint32_t interpolation;
    uint32_t index;
    uint64_t fraction;

    if (mantissa == 0 && exponent == 0) {
        return double_from_bits(sign | exponent_mask);
    }
    if (exponent == exponent_mask) {
        if (mantissa == 0) {
            return double_from_bits(sign);
        }
        return 0.0 + value;
    }
    if (exponent < (UINT64_C(895) << 52)) {
        return double_from_bits(sign | UINT64_C(0x7fefffffffffffff));
    }
    if (exponent >= (UINT64_C(1149) << 52)) {
        return double_from_bits(sign);
    }
    exponent = (UINT64_C(0x7fd) << 52) - exponent;
    interpolation = (uint32_t) (mantissa >> 37);
    index = interpolation / 1024U;
    fraction = base[index] -
               (decrement[index] * (interpolation % 1024U) + 1U) / 2U;
    return double_from_bits(sign | exponent | (fraction << 29));
}

// Gekko scalar-single multiply rounds its FC operand to a 25-bit significand.
// refs/Ishiiruka/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h
static double ppc_force_25_bit(double value)
{
    uint64_t bits = double_bits(value);
    bits = (bits & UINT64_C(0xFFFFFFFFF8000000)) +
           (bits & UINT64_C(0x0000000008000000));
    return double_from_bits(bits);
}

// The Dolphin SDK implements these entry points with paired-single assembly.
// PPC Linux cannot assemble the GameCube-specific paired-single mnemonics, so
// hosted C preserves the source formulas and operation ordering where the SDK
// publishes a C equivalent.
void PSVECNormalize(Vec* src, Vec* unit)
{
    float xx = src->x * src->x;
    float yy = src->y * src->y;
    float mag = fmaf(src->z, src->z, xx) + yy;
    double estimate = ppc_frsqrte((double) mag);
    float estimate_sq = (float) (estimate * ppc_force_25_bit(estimate));
    float estimate_half = (float) (estimate * 0.5);
    float correction = fmaf(-estimate_sq, mag, 3.0F);
    float inv = correction * estimate_half;
    unit->x = src->x * inv;
    unit->y = src->y * inv;
    unit->z = src->z * inv;
}

void PSVECAdd(Vec* a, Vec* b, Vec* out)
{
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
}

void PSVECSubtract(Vec* a, Vec* b, Vec* out)
{
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    out->z = a->z - b->z;
}

void PSVECScale(Vec* src, Vec* out, float scale)
{
    out->x = src->x * scale;
    out->y = src->y * scale;
    out->z = src->z * scale;
}

float PSVECDotProduct(Vec* a, Vec* b)
{
    return a->z * b->z + (a->x * b->x + a->y * b->y);
}

void PSVECCrossProduct(Vec* a, Vec* b, Vec* out)
{
    Vec value;
    float az_bx = b->x * a->z;
    float az_by = b->y * a->z;
    float ax_by = b->y * a->x;

    // The SDK paired-single routine rounds the ps_mul terms above, then uses
    // ps_msub for the other product and subtraction. Preserve which side of
    // each determinant owns the fused operation; reassociating it moves the
    // gameplay camera's screen-edge publication.
    // refs/melee/extern/dolphin/src/dolphin/mtx/vec.c::
    //     PSVECCrossProduct
    value.x = fmaf(a->y, b->z, -az_by);
    value.y = -fmaf(a->x, b->z, -az_bx);
    value.z = -fmaf(a->y, b->x, -ax_by);
    *out = value;
}

float PSVECMag(Vec* value)
{
    return sqrtf(value->z * value->z +
                 (value->x * value->x + value->y * value->y));
}

void PSMTXIdentity(Mtx matrix)
{
    static const Mtx identity = {
        { 1.0F, 0.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F, 0.0F },
    };
    memcpy(matrix, identity, sizeof(Mtx));
}

void PSMTXCopy(Mtx src, Mtx dst)
{
    if (src != dst) {
        memcpy(dst, src, sizeof(Mtx));
    }
}

void PSMTXTranspose(Mtx src, Mtx dst)
{
    Mtx temporary;
    float (*result)[4] = src == dst ? temporary : dst;

    result[0][0] = src[0][0];
    result[0][1] = src[1][0];
    result[0][2] = src[2][0];
    result[0][3] = 0.0F;
    result[1][0] = src[0][1];
    result[1][1] = src[1][1];
    result[1][2] = src[2][1];
    result[1][3] = 0.0F;
    result[2][0] = src[0][2];
    result[2][1] = src[1][2];
    result[2][2] = src[2][2];
    result[2][3] = 0.0F;
    if (result == temporary) {
        memcpy(dst, temporary, sizeof(Mtx));
    }
}

void PSMTXConcat(Mtx a, Mtx b, Mtx out)
{
    Mtx temporary;
    float (*result)[4] = out == a || out == b ? temporary : out;
    int row;
    int column;

    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::PSMTXConcat
    // accumulates each output lane with ps_mul/ps_madd in row order. Bone
    // world matrices feed gameplay collision and article anchors, so use the
    // paired-single contraction boundaries rather than C_MTXConcat's scalar
    // expression.
    for (row = 0; row != 3; row++) {
        for (column = 0; column != 4; column++) {
            float value = a[row][0] * b[0][column];
            value = fmaf(b[1][column], a[row][1], value);
            value = fmaf(b[2][column], a[row][2], value);
            if (column == 3) {
                value = fmaf(1.0F, a[row][3], value);
            }
            result[row][column] = value;
        }
    }
    if (result == temporary) {
        memcpy(out, temporary, sizeof(Mtx));
    }
}

static inline __attribute__((always_inline)) void
mtx_srt_concat_trig(Mtx out, Mtx parent, Vec3* scale, Vec3* translate,
                    Vec3* parent_scale, const float sin_xyz[3],
                    const float cos_xyz[3])
{
    float scale_x2 = scale->x;
    float scale_x1 = scale->x;
    float scale_x = scale->x;
    float scale_y2 = scale->y;
    float scale_y1 = scale->y;
    float scale_y = scale->y;
    float scale_z2 = scale->z;
    float scale_z1 = scale->z;
    float scale_z = scale->z;
#if !defined(__FMA__)
    float local[3][4];
#endif
    int row;
#if !defined(__FMA__)
    int column;
#endif

    // Exact hosted fusion of HSD_MtxSRT followed by the paired-single
    // PSMTXConcat. The local values retain the source rounding boundaries,
    // but are consumed directly instead of being published and reloaded as a
    // general aliasable matrix.
    // refs/melee/src/sysdolphin/baselib/mtx.c::HSD_MtxSRT
    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::PSMTXConcat
    if (parent_scale != NULL &&
        (parent_scale->x != 1.0F || parent_scale->y != 1.0F ||
         parent_scale->z != 1.0F)) {
        float reciprocal_x = 1.0F / parent_scale->x;
        float reciprocal_y = 1.0F / parent_scale->y;
        float reciprocal_z = 1.0F / parent_scale->z;

        scale_y2 *= parent_scale->y * reciprocal_x;
        scale_z2 *= parent_scale->z * reciprocal_x;
        scale_x1 *= parent_scale->x * reciprocal_y;
        scale_z1 *= parent_scale->z * reciprocal_y;
        scale_x *= parent_scale->x * reciprocal_z;
        scale_y *= parent_scale->y * reciprocal_z;
    }

    {
        float local00 = cos_xyz[2] * (scale_x2 * cos_xyz[1]);
        float local10 = sin_xyz[2] * (scale_x1 * cos_xyz[1]);
        float local20 = -scale_x * sin_xyz[1];
        float local01 = scale_y2 *
                        fmaf(cos_xyz[2], sin_xyz[0] * sin_xyz[1],
                             -(cos_xyz[0] * sin_xyz[2]));
        float local11 =
            scale_y1 * fmaf(sin_xyz[2], sin_xyz[0] * sin_xyz[1],
                            cos_xyz[0] * cos_xyz[2]);
        float local21 = cos_xyz[1] * (scale_y * sin_xyz[0]);
        float local02 =
            scale_z2 * fmaf(cos_xyz[2], cos_xyz[0] * sin_xyz[1],
                            sin_xyz[0] * sin_xyz[2]);
        float local12 = scale_z1 *
                        fmaf(sin_xyz[2], cos_xyz[0] * sin_xyz[1],
                             -(sin_xyz[0] * cos_xyz[2]));
        float local22 = cos_xyz[1] * (scale_z * cos_xyz[0]);

#if defined(__FMA__)
        __m128 local0 =
            _mm_set_ps(translate->x, local02, local01, local00);
        __m128 local1 =
            _mm_set_ps(translate->y, local12, local11, local10);
        __m128 local2 =
            _mm_set_ps(translate->z, local22, local21, local20);

        for (row = 0; row != 3; row++) {
            __m128 world =
                _mm_mul_ps(_mm_set1_ps(parent[row][0]), local0);
            float translate;

            world = _mm_fmadd_ps(_mm_set1_ps(parent[row][1]), local1,
                                 world);
            world = _mm_fmadd_ps(_mm_set1_ps(parent[row][2]), local2,
                                 world);
            translate = fmaf(1.0F, parent[row][3],
                             _mm_cvtss_f32(_mm_shuffle_ps(
                                 world, world, _MM_SHUFFLE(3, 3, 3, 3))));
            world = _mm_insert_ps(world, _mm_set_ss(translate), 0x30);
            _mm_storeu_ps(out[row], world);
        }
#else
        local[0][0] = local00;
        local[1][0] = local10;
        local[2][0] = local20;
        local[0][1] = local01;
        local[1][1] = local11;
        local[2][1] = local21;
        local[0][2] = local02;
        local[1][2] = local12;
        local[2][2] = local22;
        local[0][3] = translate->x;
        local[1][3] = translate->y;
        local[2][3] = translate->z;

        for (row = 0; row != 3; row++) {
            for (column = 0; column != 4; column++) {
                float value = parent[row][0] * local[0][column];
                value = fmaf(local[1][column], parent[row][1], value);
                value = fmaf(local[2][column], parent[row][2], value);
                if (column == 3) {
                    value = fmaf(1.0F, parent[row][3], value);
                }
                out[row][column] = value;
            }
        }
#endif
    }
}

void HSD_MtxSRTConcat(Mtx out, Mtx parent, Vec3* scale, Vec3* rotate,
                      Vec3* translate, Vec3* parent_scale)
{
    float sin_xyz[3];
    float cos_xyz[3];

    msl_sincosf3(&rotate->x, sin_xyz, cos_xyz);
    mtx_srt_concat_trig(out, parent, scale, translate, parent_scale, sin_xyz,
                        cos_xyz);
}

void HSD_MtxSRTConcatTrig(Mtx out, Mtx parent, Vec3* scale, Vec3* translate,
                          Vec3* parent_scale, const float sin_xyz[3],
                          const float cos_xyz[3])
{
    mtx_srt_concat_trig(out, parent, scale, translate, parent_scale, sin_xyz,
                        cos_xyz);
}

void PSMTXMultVec(Mtx44 matrix, Vec* src, Vec* dst)
{
    Vec value;
    int row;

    // refs/melee/extern/dolphin/src/dolphin/mtx/mtxvec.c::PSMTXMultVec
    // uses ps_mul, two ps_madd lanes, then ps_sum0.  Preserve those
    // single-precision rounding and contraction boundaries: the SDK's C
    // expression has a different association and is observably a few ULPs
    // away for article spawn anchors.
    for (row = 0; row != 3; row++) {
        float xy0 = matrix[row][0] * src->x;
        float xy1 = matrix[row][1] * src->y;
        float zx = fmaf(matrix[row][2], src->z, xy0);
        float ty = fmaf(matrix[row][3], 1.0F, xy1);
        (&value.x)[row] = zx + ty;
    }
    *dst = value;
}

void PSMTXMultVecSR(Mtx44 matrix, Vec* src, Vec* dst)
{
    Vec value;
    value.x = matrix[0][2] * src->z +
              (matrix[0][0] * src->x + matrix[0][1] * src->y);
    value.y = matrix[1][2] * src->z +
              (matrix[1][0] * src->x + matrix[1][1] * src->y);
    value.z = matrix[2][2] * src->z +
              (matrix[2][0] * src->x + matrix[2][1] * src->y);
    *dst = value;
}

u32 C_MTXInverse(Mtx src, Mtx inv)
{
    Mtx temporary;
    float (*out)[4] = src == inv ? temporary : inv;
    float determinant;

    // Direct SDK C owner. The retail draw path calls the paired-single
    // implementation; keeping the portable source formula here gives the
    // hosted headless camera an explicit inverse until that operation needs
    // a narrower paired-single exactness treatment.
    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::C_MTXInverse
    determinant = ((((src[2][1] * (src[0][2] * src[1][0])) +
                     ((src[2][2] * (src[0][0] * src[1][1])) +
                      (src[2][0] * (src[0][1] * src[1][2])))) -
                    (src[0][2] * (src[2][0] * src[1][1]))) -
                   (src[2][2] * (src[1][0] * src[0][1]))) -
                  (src[1][2] * (src[0][0] * src[2][1]));
    if (determinant == 0.0F) {
        return 0;
    }
    determinant = 1.0F / determinant;
    out[0][0] = determinant *
                ((src[1][1] * src[2][2]) - (src[2][1] * src[1][2]));
    out[0][1] = -determinant *
                ((src[0][1] * src[2][2]) - (src[2][1] * src[0][2]));
    out[0][2] = determinant *
                ((src[0][1] * src[1][2]) - (src[1][1] * src[0][2]));
    out[1][0] = -determinant *
                ((src[1][0] * src[2][2]) - (src[2][0] * src[1][2]));
    out[1][1] = determinant *
                ((src[0][0] * src[2][2]) - (src[2][0] * src[0][2]));
    out[1][2] = -determinant *
                ((src[0][0] * src[1][2]) - (src[1][0] * src[0][2]));
    out[2][0] = determinant *
                ((src[1][0] * src[2][1]) - (src[2][0] * src[1][1]));
    out[2][1] = -determinant *
                ((src[0][0] * src[2][1]) - (src[2][0] * src[0][1]));
    out[2][2] = determinant *
                ((src[0][0] * src[1][1]) - (src[1][0] * src[0][1]));
    out[0][3] = ((-out[0][0] * src[0][3]) -
                 (out[0][1] * src[1][3])) -
                (out[0][2] * src[2][3]);
    out[1][3] = ((-out[1][0] * src[0][3]) -
                 (out[1][1] * src[1][3])) -
                (out[1][2] * src[2][3]);
    out[2][3] = ((-out[2][0] * src[0][3]) -
                 (out[2][1] * src[1][3])) -
                (out[2][2] * src[2][3]);
    if (out == temporary) {
        memcpy(inv, temporary, sizeof(temporary));
    }
    return 1;
}

u32 PSMTXInverse(Mtx src, Mtx inv)
{
    Mtx temporary;
    float (*out)[4] = src == inv ? temporary : inv;
    float f11_0;
    float f11_1;
    float f13_0;
    float f13_1;
    float f12_0;
    float f12_1;
    float f10;
    float f9;
    float f8;
    float determinant;
    float estimate;
    float estimate_twice;
    float estimate_squared;
    float reciprocal;
    float translation;

    // Scalar expansion of the SDK paired-single instruction stream. The
    // intermediate names retain the source FPRs so multiply/subtract and
    // multiply/add contraction boundaries remain visible and auditable.
    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::PSMTXInverse
    f11_0 = fmaf(src[0][1], src[1][2],
                 -(src[1][1] * src[0][2]));
    f11_1 = fmaf(src[0][2], src[1][0],
                 -(src[1][2] * src[0][0]));
    f13_0 = fmaf(src[1][1], src[2][2],
                 -(src[2][1] * src[1][2]));
    f13_1 = fmaf(src[1][2], src[2][0],
                 -(src[2][2] * src[1][0]));
    f12_0 = fmaf(src[2][1], src[0][2],
                 -(src[0][1] * src[2][2]));
    f12_1 = fmaf(src[2][2], src[0][0],
                 -(src[0][2] * src[2][0]));
    f10 = fmaf(src[1][0], src[2][1],
               -(src[1][1] * src[2][0]));
    f9 = fmaf(src[0][1], src[2][0],
              -(src[0][0] * src[2][1]));
    f8 = fmaf(src[0][0], src[1][1],
              -(src[0][1] * src[1][0]));
    determinant = src[0][0] * f13_0;
    determinant = fmaf(src[1][0], f12_0, determinant);
    determinant = fmaf(src[2][0], f11_0, determinant);
    if (determinant == 0.0F) {
        return 0;
    }

    estimate = (float) ppc_fres((double) determinant);
    estimate_twice = estimate + estimate;
    estimate_squared = estimate * estimate;
    reciprocal = fmaf(-determinant, estimate_squared, estimate_twice);
    f13_0 *= reciprocal;
    f13_1 *= reciprocal;
    f12_0 *= reciprocal;
    f12_1 *= reciprocal;
    f11_0 *= reciprocal;
    f11_1 *= reciprocal;
    f10 *= reciprocal;
    f9 *= reciprocal;
    f8 *= reciprocal;

    out[0][0] = f13_0;
    out[0][1] = f12_0;
    out[0][2] = f11_0;
    translation = f13_0 * src[0][3];
    translation = fmaf(f12_0, src[1][3], translation);
    out[0][3] = -fmaf(f11_0, src[2][3], translation);
    out[1][0] = f13_1;
    out[1][1] = f12_1;
    out[1][2] = f11_1;
    translation = f13_1 * src[0][3];
    translation = fmaf(f12_1, src[1][3], translation);
    out[1][3] = -fmaf(f11_1, src[2][3], translation);
    out[2][0] = f10;
    out[2][1] = f9;
    out[2][2] = f8;
    translation = f10 * src[0][3];
    translation = fmaf(f9, src[1][3], translation);
    out[2][3] = -fmaf(f8, src[2][3], translation);

    if (out == temporary) {
        memcpy(inv, temporary, sizeof(temporary));
    }
    return 1;
}

void PSMTXScale(Mtx matrix, float x, float y, float z)
{
    PSMTXIdentity(matrix);
    matrix[0][0] = x;
    matrix[1][1] = y;
    matrix[2][2] = z;
}

void PSMTXTrans(Mtx matrix, float x, float y, float z)
{
    PSMTXIdentity(matrix);
    matrix[0][3] = x;
    matrix[1][3] = y;
    matrix[2][3] = z;
}

void PSMTXRotAxisRad(Mtx matrix, Vec* axis, float radians)
{
    Vec normal;
    float sine = sinf(radians);
    float cosine = cosf(radians);
    float one_minus_cosine = 1.0F - cosine;
    float x;
    float y;
    float z;

    PSVECNormalize(axis, &normal);
    x = normal.x;
    y = normal.y;
    z = normal.z;
    matrix[0][0] = cosine + one_minus_cosine * x * x;
    matrix[0][1] = y * (one_minus_cosine * x) - sine * z;
    matrix[0][2] = z * (one_minus_cosine * x) + sine * y;
    matrix[0][3] = 0.0F;
    matrix[1][0] = y * (one_minus_cosine * x) + sine * z;
    matrix[1][1] = cosine + one_minus_cosine * y * y;
    matrix[1][2] = z * (one_minus_cosine * y) - sine * x;
    matrix[1][3] = 0.0F;
    matrix[2][0] = z * (one_minus_cosine * x) - sine * y;
    matrix[2][1] = z * (one_minus_cosine * y) + sine * x;
    matrix[2][2] = cosine + one_minus_cosine * z * z;
    matrix[2][3] = 0.0F;
}

void MTXRotRad(Mtx matrix, char axis, float radians)
{
    float sine = sinf(radians);
    float cosine = cosf(radians);

    // Scalar spelling of the SDK's paired-single MTXRotTrig owner. Ground IK
    // reaches the z-axis case; retaining all three source cases keeps this
    // platform primitive complete.
    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::{
    //   MTXRotRad,PSMTXRotTrig}
    axis |= 0x20;
    PSMTXIdentity(matrix);
    switch (axis) {
    case 'x':
        matrix[1][1] = cosine;
        matrix[1][2] = -sine;
        matrix[2][1] = sine;
        matrix[2][2] = cosine;
        break;
    case 'y':
        matrix[0][0] = cosine;
        matrix[0][2] = sine;
        matrix[2][0] = -sine;
        matrix[2][2] = cosine;
        break;
    case 'z':
        matrix[0][0] = cosine;
        matrix[0][1] = -sine;
        matrix[1][0] = sine;
        matrix[1][1] = cosine;
        break;
    }
}

void PSMTXQuat(Mtx matrix, Quaternion* q)
{
    // refs/melee/extern/dolphin/src/dolphin/mtx/mtx.c::PSMTXQuat is
    // paired-single assembly: the norm reciprocal is a fres estimate plus one
    // Newton step, each diagonal folds its squared sum through a single
    // ps_nmsub round (m[1][1] with the sum itself fused, m[0][0] and m[2][2]
    // from pre-rounded squares), and every off-diagonal fuses its first
    // product. Dynamic-bone rotations re-enter gameplay through the
    // mtx->euler conversion, so keep those exact boundaries.
    float x = q->x;
    float y = q->y;
    float z = q->z;
    float w = q->w;
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float zz_xx = fmaf(z, z, xx);
    float ww_yy = fmaf(w, w, yy);
    float norm = zz_xx + ww_yy;
    float estimate = (float) ppc_fres((double) norm);
    float scale = estimate * fmaf(-norm, estimate, 2.0F);
    float zw = z * w;
    float yw = y * w;
    float xw = x * w;
    float xy_zw = fmaf(x, y, zw);
    float xy_mzw = fmaf(x, y, -zw);
    float xz_yw = fmaf(x, z, yw);
    float yz_xw = fmaf(y, z, xw);
    float xz_myw = fmaf(-2.0F, yw, xz_yw);
    float yz_mxw = fmaf(-2.0F, xw, yz_xw);
    float xx_yy = xx + yy;
    float zz_yy = zz + yy;

    scale = scale * 2.0F;
    matrix[0][0] = fmaf(-zz_yy, scale, 1.0F);
    matrix[0][1] = xy_mzw * scale;
    matrix[0][2] = xz_yw * scale;
    matrix[0][3] = 0.0F;
    matrix[1][0] = xy_zw * scale;
    matrix[1][1] = fmaf(-zz_xx, scale, 1.0F);
    matrix[1][2] = yz_mxw * scale;
    matrix[1][3] = 0.0F;
    matrix[2][0] = xz_myw * scale;
    matrix[2][1] = yz_xw * scale;
    matrix[2][2] = fmaf(-xx_yy, scale, 1.0F);
    matrix[2][3] = 0.0F;
}

double __fabs(double value)
{
    return fabs(value);
}

float __fabsf(float value)
{
    return fabsf(value);
}

float sqrtf__Ff(float value)
{
    return sqrtf(value);
}

double __frsqrte(double value)
{
    return ppc_frsqrte(value);
}
