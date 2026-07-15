#include <MSL/math.h>

#include <MetroTRK/intrinsics.h>

#include "match.h"

// Gameplay math projection of refs/melee/src/melee/lb/lbrefract.c:742-961.
// The refraction renderer is excluded, but these global libm replacements are
// called throughout fighter, item, stage, and collision source.
#define SIGN_BIT (1 << 31)
#define BITWISE(f) (*(u32*) &(f))
#define SIGNED_BITWISE(f) ((s32) BITWISE(f))
#define GET_SIGN_BIT(f) (SIGNED_BITWISE(f) & SIGN_BIT)
#define BITWISE_PI_2 0x3FC90FDB

extern float MSL_TrigF_80400770[], MSL_TrigF_80400774[];
#define NAN MSL_TrigF_80400770[0]
#define INF MSL_TrigF_80400774[0]

static inline float lbRefract_80022DF8(float x);

float __fmadds(float a, float b, float c)
{
    float result;
    __asm__("fmadds %0,%1,%2,%3" : "=f"(result) : "f"(a), "f"(b), "f"(c));
    return result;
}

// MetroWerks emits a scalar-single fnmsubs instruction for this intrinsic.
float __fnmsubs(float a, float b, float c)
{
    float result;
    __asm__("fnmsubs %0,%1,%2,%3" : "=f"(result) : "f"(a), "f"(b), "f"(c));
    return result;
}

float msl_dolphin_fnmsubs(float a, float b, float c)
{
    float result = __fnmsubs(a, b, c);

    // The tracked Slippi online captures use Dolphin's equivalent c - a*b
    // exact-zero sign, while both offline profiles retain the retail PPC
    // instruction's sign. Keep this capture/runtime capability independent of
    // BrawlOffscreenDamage: the offline mainline-Dolphin profile has that
    // patch but retains retail fnmsubs zero signs.
    if (!msl_core_uses_online_fnmsubs_zero()) {
        return result;
    }

    // A product of two binary32 values is exact in binary64, so this test
    // distinguishes true cancellation from a small nonzero result that
    // merely underflowed when rounded back to binary32.  The latter already
    // has the correct sign in the retail instruction result.
    if (result == 0.0F && (double) c == (double) a * (double) b) {
        u32 product_sign = (BITWISE(a) ^ BITWISE(b)) & SIGN_BIT;
        u32 c_sign = BITWISE(c) & SIGN_BIT;

        // The DOL sites are scalar-single fnmsubs instructions
        // (refs/melee/build/GALE01/asm/melee/ft/fighter.s at
        // 0x8006B9C8/0x8006B9E4 and 0x8006BB34/0x8006BB50). Slippi Dolphin's
        // JIT implements nmsub as the equivalent fused c - a*b expression.
        // PPC's negate-after-subtract form differs only in the sign of an
        // exact zero. Preserve a same-sign zero sum such as -0 - +0; exact
        // cancellation uses the JIT's round-to-nearest +0.
        if (c == 0.0F && c_sign != product_sign) {
            return c;
        }
        return 0.0F;
    }
    return result;
}

float __fmsubs(float a, float b, float c)
{
    float result;
    __asm__("fmsubs %0,%1,%2,%3" : "=f"(result) : "f"(a), "f"(b), "f"(c));
    return result;
}

float atan2f(float y, float x)
{
    if (GET_SIGN_BIT(x) == GET_SIGN_BIT(y)) {
        if (GET_SIGN_BIT(x) != 0) {
            return x == -0.0f ? (float) -M_PI_2 : atanf(y / x) - (float) M_PI;
        }

        return x ? atanf(y / x) : (float) M_PI_2;
    }

    if (x < 0.0f) {
        return (float) M_PI + atanf(y / x);
    }

    if (x) {
        return atanf(y / x);
    }

    *(u32*) &y = GET_SIGN_BIT(y) + BITWISE_PI_2;

    return y;
}

float acosf(float x)
{
    float result = 1.0F - x * x;
    if (result > 0) {
        float guess;
        guess = __frsqrte(result);
        guess = 0.5f * guess * (3.0f - guess * guess * result);
        guess = 0.5f * guess * (3.0f - guess * guess * result);
        guess = 0.5f * guess * (3.0f - guess * guess * result);
        result = guess;
    } else if (result) {
        result = NAN;
    } else {
        result = INF;
    }
    return (float) M_PI_2 - atanf(x * result);
}

float asinf(float x)
{
    return atanf(x * lbRefract_80022DF8(-(x * x - 1.0f)));
}

static inline float lbRefract_80022DF8(float x)
{
    if (x > 0.0f) {
        float guess;
        guess = __frsqrte(x);
        guess = 0.5f * guess * (3.0f - guess * guess * x);
        guess = 0.5f * guess * (3.0f - guess * guess * x);
        guess = 0.5f * guess * (3.0f - guess * guess * x);
        return guess;
    }

    if (x) {
        return NAN;
    }

    return INF;
}

#define SILVER_RATIO_1_CONJUGATE lbRefract3_804D7DD4

#define BITWISE_INF 0x7F800000 /* = +Infinity */
#define BITWISE_0_5 0x3F000000 /* = 0.5f */
#define BITWISE_1_0 0x3F800000 /* = 1.0f */
#define BITWISE_2_0 0x40000000 /* = 2.0f */

#define BITWISE_THRESHOLD_0 0x3F08D5B9 /* = 0.534511148929596f */
#define BITWISE_THRESHOLD_1 0x3F521801 /* = 0.8206787705421448f */
#define BITWISE_THRESHOLD_2 0x3F9BF7EC /* = 1.218503475189209f */
#define BITWISE_THRESHOLD_3 0x3FEF789E /* = 1.870868444442749f */

static const float atanf_lookup[] = {
    1.0,
    -0.3333333134651184,
    0.1999988704919815,
    -0.14281649887561798,
    0.11041180044412613,
    -0.08459755778312683,
    0.04714243486523628,
    6.828420162200928,
    3.239828109741211,
    2.0,
    1.4464620351791382,
    1.1715729236602783,
    1.039566159248352,
    7.1350000325764995e-06,
    8.200000252145401e-07,
    0.0,
    6.299999881775875e-07,
    0.0,
    0.0,
    0.0,
    0.3926900029182434,
    0.5890486240386963,
    0.7853981256484985,
    0.9817469716072083,
    1.1780970096588135,
    1.3744460344314575,
    0.0,
    9.081698408408556e-06,
    2.3000000126671694e-08,
    6.30000016599297e-08,
    7.040000014058023e-07,
    2.499999993688107e-07,
    7.900000014160469e-07,
    2.414212942123413,
    1.4966057538986206,
    1.0,
    0.6681786179542542,
    0.4142135679721832,
    0.1989123672246933,
    5.620000251838064e-07,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
};

float atanf(float x)
{
    float const silver_ratio = 2.4142136573791504f;
    float const silver_ratio_conjugate = 0.4142135679721832f;

    float result;
    const float* lookup_ptr;
    s32 lookup_index = -1;
    bool x_ge_ratio = false;
    s32 sign_bit_x = BITWISE(x) & SIGN_BIT;

    BITWISE(x) &= ~SIGN_BIT;

    if (x >= silver_ratio) {
        x_ge_ratio = true;
        result = 1.0f / x;
    } else if (silver_ratio_conjugate < x) {
        lookup_index = 0;
        switch (BITWISE(x) & BITWISE_INF) {
        case BITWISE_0_5: {
            if (!(SIGNED_BITWISE(x) < BITWISE_THRESHOLD_0)) {
                lookup_index = 1;
            }

            if (!(SIGNED_BITWISE(x) < BITWISE_THRESHOLD_1)) {
                lookup_index += 1;
            }

            break;
        }
        case BITWISE_1_0: {
            lookup_index = 2;
            if (!(SIGNED_BITWISE(x) < BITWISE_THRESHOLD_2)) {
                lookup_index = 3;
            }

            if (!(SIGNED_BITWISE(x) < BITWISE_THRESHOLD_3)) {
                lookup_index += 1;
            }

            break;
        }
        case BITWISE_2_0: {
            lookup_index = 4;
            break;
        }
        }
        {
            float offset_39;
            float offset_33;
            lookup_ptr = &atanf_lookup[lookup_index];
            offset_39 = lookup_ptr[39];
            offset_33 = lookup_ptr[33];

            result = 1.0f / (offset_33 + (x + offset_39));
            result = __fnmsubs(result, lookup_ptr[7], offset_33) +
                     __fnmsubs(result, lookup_ptr[13], offset_39);
        }
    } else {
        result = x;
    }

    {
        float result_squared = result * result;
        lookup_ptr = &atanf_lookup[lookup_index];

        // clang-format off
        result = result *
            result_squared * (
                result_squared * (
                    result_squared * (
                        result_squared * (
                            result_squared * (
                                result_squared * (
                                    atanf_lookup[6]
                                ) + atanf_lookup[5]
                            ) + atanf_lookup[4]
                        ) + atanf_lookup[3]
                    ) + atanf_lookup[2]
                ) + atanf_lookup[1]
            ) + result;
        // clang-format on

        result += lookup_ptr[27];
        result += lookup_ptr[20];
    }

    if (x_ge_ratio) {
        result -= (float) M_PI_2;
        return sign_bit_x ? result : -result;
    }

    BITWISE(result) |= sign_bit_x;
    return result;
}
