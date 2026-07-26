#include <platform.h>

#ifdef MSL_CORE_NATIVE

#include <MSL/math_ppc.h>

// Hosted definition of the PPC-exact source sequence. The upstream owner must
// remain O0 for quaternion interpolation, but this process-wide function does
// not share that compiler constraint.
// refs/melee/src/sysdolphin/baselib/quatlib.c::sqrtf
// GALE01 0x8037E758-0x8037E790 (and every other inlined copy): MWCC compiles
// each Newton step's "3.0 - guess * guess * x" as a double-precision fnmsub,
// so the x * guess^2 product must fuse into the subtraction with a single
// rounding. The g^2 square and the 0.5 * g product stay pre-rounded fmuls.
static inline double msl_gekko_sqrt_step(double guess, double x)
{
    return (0.5 * guess) * __builtin_fma(-x, guess * guess, 3.0);
}

float sqrtf(float x)
{
    volatile float y;
    if (x > 0.0F) {
        double xd = (double) x;
        double guess = __frsqrte(xd);
        guess = msl_gekko_sqrt_step(guess, xd);
        guess = msl_gekko_sqrt_step(guess, xd);
        guess = msl_gekko_sqrt_step(guess, xd);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

#endif
