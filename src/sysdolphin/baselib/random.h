#ifndef _random_h_
#define _random_h_

#include <platform.h>

#ifdef MSL_CORE_HOSTED
typedef struct HSD_RandomContext {
    u32 value;
    u32* active;
} HSD_RandomContext;

#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_random_context() (msl_core_context_random)
#else
HSD_RandomContext* msl_core_random_context(void);
#endif

#define seed (msl_core_random_context()->value)
#define seed_ptr (msl_core_random_context()->active)
#else
extern u32 seed;
extern u32* seed_ptr;
#endif

s32 HSD_Rand(void);
f32 HSD_Randf(void);
s32 HSD_Randi(s32 max_val);
void _HSD_RandForgetMemory(void* low, void* high);

#endif
