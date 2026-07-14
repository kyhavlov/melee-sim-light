#include <dolphin/mtx.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

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
    value.x = a->y * b->z - a->z * b->y;
    value.y = a->z * b->x - a->x * b->z;
    value.z = a->x * b->y - a->y * b->x;
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

void PSMTXQuat(Mtx matrix, Quaternion* q)
{
    float scale = 2.0F /
                  (q->w * q->w +
                   (q->z * q->z + (q->x * q->x + q->y * q->y)));
    float xs = q->x * scale;
    float ys = q->y * scale;
    float zs = q->z * scale;
    float wx = q->w * xs;
    float wy = q->w * ys;
    float wz = q->w * zs;
    float xx = q->x * xs;
    float xy = q->x * ys;
    float xz = q->x * zs;
    float yy = q->y * ys;
    float yz = q->y * zs;
    float zz = q->z * zs;

    matrix[0][0] = 1.0F - (yy + zz);
    matrix[0][1] = xy - wz;
    matrix[0][2] = xz + wy;
    matrix[0][3] = 0.0F;
    matrix[1][0] = xy + wz;
    matrix[1][1] = 1.0F - (xx + zz);
    matrix[1][2] = yz - wx;
    matrix[1][3] = 0.0F;
    matrix[2][0] = xz - wy;
    matrix[2][1] = yz + wx;
    matrix[2][2] = 1.0F - (xx + yy);
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
