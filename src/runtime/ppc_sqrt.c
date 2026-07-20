#include <platform.h>

#ifdef MSL_CORE_NATIVE

#include <MSL/math_ppc.h>

// Hosted definition of the PPC-exact source sequence. The upstream owner must
// remain O0 for quaternion interpolation, but this process-wide function does
// not share that compiler constraint.
// refs/melee/src/sysdolphin/baselib/quatlib.c::sqrtf
float sqrtf(float x)
{
    volatile float y;
    if (x > 0.0F) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

#endif
