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
extern const float msl_atanf_lookup[];
// Cosines are finite and strictly inside (-1, 1); first may alias cosines.
void msl_slerp_weights_many(const float* cosines, float weight,
                            float* first, float* second, int count);
void msl_sincosf3(const float xyz[3], float sin_out[3], float cos_out[3]);
void msl_sincosf_many(const float* angles, float* sin_out, float* cos_out,
                      int count);
#endif

#endif
