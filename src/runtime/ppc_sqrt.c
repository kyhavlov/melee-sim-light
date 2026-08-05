#include <platform.h>

#ifdef MSL_CORE_NATIVE

float sqrtf(float x)
{
    if (x > 0.0F) {
#if defined(__x86_64__)
        float result;
        __asm__("sqrtss %1, %0" : "=x"(result) : "x"(x));
        return result;
#elif defined(__aarch64__)
        float result;
        __asm__("fsqrt %s0, %s1" : "=w"(result) : "w"(x));
        return result;
#else
        return __builtin_sqrtf(x);
#endif
    }
    return x;
}

#endif
