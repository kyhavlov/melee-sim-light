#ifndef MSL_TRIGF_H
#define MSL_TRIGF_H

#include <platform.h>

float acosf(float);
float asinf(float);
float atan2f(float y, float x);
float atanf(float);
float cosf(float);
float sinf(float);
float tanf(float);

#if defined(MSL_CORE_NATIVE)
void msl_sincosf3(const float xyz[3], float sin_out[3], float cos_out[3]);
#endif

#endif
