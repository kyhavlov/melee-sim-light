#ifndef METROTRK_INTRINSICS_H
#define METROTRK_INTRINSICS_H

void __sync(void);
void __isync(void);
int __cntlzw(unsigned int);
float sqrtf__Ff(float);
#ifndef MSL_NATIVE_INLINE_FMADDS
float __fmadds(float, float, float);
#endif
float __fnmsubs(float, float, float);
float __fmsubs(float, float, float);
double __fabs(double);
float __fabsf(float);
double __frsqrte(double);

void* __memcpy(void* dst, const void* src, unsigned long n);

#endif
