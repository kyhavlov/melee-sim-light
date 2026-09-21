#ifndef _MATH_PPC_H_
#define _MATH_PPC_H_

#ifdef __MWERKS__
#pragma push
#pragma cplusplus on
#endif

extern double __frsqrte(double);

#ifdef MSL_CORE_NATIVE
#include <stdint.h>
extern const int32_t msl_frsqrte_base[32];
extern const int32_t msl_frsqrte_decrement[32];
#endif

// Hosted PPC Linux supplies sqrtf through libc. The GameCube inline used a
// hardware frsqrte seed; consequential float differences remain an explicit
// validation item for the port rather than a duplicate libc declaration.

#ifdef __MWERKS__
#pragma pop
#endif

static inline float sqrtf_accurate(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x); // returns an approximation to
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 12 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 24 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 32 sig bits
        guess = 0.5 * guess * (3.0 - guess * guess * x); // extra iteration
        y = (float) (x * guess);
        return y;
    }
    return x;
}

#ifdef MSL_CORE_HOSTED
// MWCC's inline float sqrtf as emitted inside gameplay code (e.g. the
// knockback decay magnitude at GALE01 0x8006B948..0x8006B988): a Gekko
// frsqrte seed refined by exactly three double-precision Newton steps whose
// (3 - x * g * g) term is one fused fnmsub each, then x * guess and frsp.
// Hosted libc sqrtf is correctly rounded and occasionally differs by one
// ULP, which can flip knife-edge comparisons such as the decay zero clamp.
static inline float msl_gekko_sqrtf(float x)
{
    if (x > 0.0f) {
        double g = __frsqrte((double) x);
        g = (0.5 * g) * __builtin_fma(-(double) x, g * g, 3.0);
        g = (0.5 * g) * __builtin_fma(-(double) x, g * g, 3.0);
        g = (0.5 * g) * __builtin_fma(-(double) x, g * g, 3.0);
        return (float) ((double) x * g);
    }
    return x;
}
#endif

#endif
