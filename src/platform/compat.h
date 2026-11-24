#ifndef MSL_CORE_PLATFORM_COMPAT_H
#define MSL_CORE_PLATFORM_COMPAT_H

// The decomp's MWCC precompiled header provides this transitively. GCC does not.
#include <stdint.h>

#define MSL_CORE_HOSTED 1

// The decomp's MSL stddef supplies this spelling transitively. Hosted libc
// does not, while several exact translation units use it directly.
typedef unsigned int usize_t;

// baselib/debug.h names these MSL callback types, but the hosted build uses
// libc's stdio rather than placing the GameCube MSL headers ahead of libc.
typedef unsigned long __file_handle;
typedef void (*__idle_proc)(void);

float msl_dolphin_fnmsubs(float a, float b, float c);
#ifdef MSL_NATIVE_INLINE_FMADDS
#define __fmadds(a, b, c) __builtin_fmaf((a), (b), (c))
#else
float __fmadds(float a, float b, float c);
#endif
float __fmsubs(float a, float b, float c);
float __fnmsubs(float a, float b, float c);

#if MSL_REPLAY_ARITHMETIC
// Slippi Jit64 can split double fmadd into rounded multiply/add according
// to its arithmetic setting. The default keeps retail's fused operation.
extern _Thread_local int msl_jit_split_fma;
static inline double msl_ppc_fma(double a, double b, double c)
{
    if (msl_jit_split_fma) {
        volatile double product = a * b;
        return product + c;
    }
    return __builtin_fma(a, b, c);
}
#else
#define msl_ppc_fma __builtin_fma
#endif

#endif
