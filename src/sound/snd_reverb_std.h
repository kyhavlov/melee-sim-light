/* AXFX reverb_std's HandleReverb, which the SDK wrote in PowerPC assembly (src/sound/decomp/axfx/reverb_std.c.inc keeps it under
 * __MWERKS__), as C that does the same operations in the same order: single-precision float, each fmadds/fnmsubs one fused
 * multiply-add (fmaf: one rounding), fctiwz a truncation to int32 that saturates. Per aux channel (left, right, surround: 160 samples
 * each, one after another in the aux buffer) a pre-delay, two comb filters, an all-pass, a low-pass, a second all-pass, and the mix
 * with the dry input, written back in place. The pre-delay pointer wraps when it reaches its line's LAST element (the assembly's
 * cmpw against line + (time - 1) * 4), so the line holds preDelayTime - 1 samples: kept as it is. Included by reverb_std.c.inc. */
#ifndef MSL_SOUND_REVERB_STD_H
#define MSL_SOUND_REVERB_STD_H

#include <math.h>
#include <stdint.h>

static inline long msl_snd_fctiwz(float f)
{
    if (f != f) return (long) INT32_MIN;
    if (f >= 2147483648.0f) return (long) INT32_MAX;
    if (f <= -2147483648.0f) return (long) INT32_MIN;
    return (long) (int32_t) f;
}

#define MSL_RV_AT(line, off) (*(float*) ((uint8_t*) (line) + (off)))

static void HandleReverb(long* sptr, struct AXFX_REVSTD_WORK* rv)
{
    const float c03 = 0.3f, c06 = 0.6f;
    const float ap = rv->allPassCoeff;
    const float damping = rv->damping;
    const float f3 = rv->level * c06;
    const float f4 = c06 - f3;
    int k, i;

    for (k = 0; k < 3; k++) {
        const float comb0 = rv->combCoef[2 * k], comb1 = rv->combCoef[2 * k + 1];
        float lp = rv->lpLastout[k];
        float* line = rv->preDelayLine[k];
        float* ptr = rv->preDelayPtr[k];
        const long time = rv->preDelayTime;
        float* last = line + (time - 1);
        struct AXFX_REVSTD_DELAYLINE* C0 = &rv->C[2 * k];
        struct AXFX_REVSTD_DELAYLINE* C1 = &rv->C[2 * k + 1];
        struct AXFX_REVSTD_DELAYLINE* A0 = &rv->AP[2 * k];
        struct AXFX_REVSTD_DELAYLINE* A1 = &rv->AP[2 * k + 1];
        long in0 = C0->inPoint, out0 = C0->outPoint, in1 = C1->inPoint, out1 = C1->outPoint;
        long ain0 = A0->inPoint, aout0 = A0->outPoint, ain1 = A1->inPoint, aout1 = A1->outPoint;
        float c0last = C0->lastOutput, c1last = C1->lastOutput, a0last = A0->lastOutput, a1last = A1->lastOutput;

        for (i = 0; i < 160; i++) {
            const float x = (float) (int32_t) sptr[i];
            float d = x, f8, f9, f14;
            if (time != 0) {
                d = *ptr;
                *ptr++ = x;
                if (ptr == last) ptr = line;
            }
            /* the two combs */
            f8 = fmaf(comb0, c0last, d);
            f9 = fmaf(comb1, c1last, d);
            MSL_RV_AT(C0->inputs, in0) = f8;
            MSL_RV_AT(C1->inputs, in1) = f9;
            f14 = MSL_RV_AT(C0->inputs, out0);
            c1last = MSL_RV_AT(C1->inputs, out1);
            in0 += 4; out0 += 4; in1 += 4; out1 += 4;
            if (in0 == C0->length) in0 = 0;
            if (out0 == C0->length) out0 = 0;
            if (in1 == C1->length) in1 = 0;
            if (out1 == C1->length) out1 = 0;
            c0last = f14;
            f14 = f14 + c1last;
            /* the first all-pass */
            f9 = fmaf(ap, a0last, f14);
            MSL_RV_AT(A0->inputs, ain0) = f9;
            f14 = -fmaf(ap, f9, -a0last);
            a0last = MSL_RV_AT(A0->inputs, aout0);
            ain0 += 4; aout0 += 4;
            if (ain0 == A0->length) ain0 = 0;
            if (aout0 == A0->length) aout0 = 0;
            /* the low-pass */
            f14 = f14 * c03;
            f14 = fmaf(damping, lp, f14);
            /* the second all-pass, and the mix with the dry input */
            f9 = fmaf(ap, a1last, f14);
            lp = f14;
            MSL_RV_AT(A1->inputs, ain1) = f9;
            f14 = -fmaf(ap, f9, -a1last);
            f8 = f4 * x;
            a1last = MSL_RV_AT(A1->inputs, aout1);
            ain1 += 4; aout1 += 4;
            f14 = fmaf(f3, f14, f8);
            if (ain1 == A1->length) ain1 = 0;
            if (aout1 == A1->length) aout1 = 0;
            sptr[i] = msl_snd_fctiwz(f14);
        }
        C0->inPoint = in0; C0->outPoint = out0; C1->inPoint = in1; C1->outPoint = out1;
        C0->lastOutput = c0last; C1->lastOutput = c1last;
        A0->inPoint = ain0; A0->outPoint = aout0; A1->inPoint = ain1; A1->outPoint = aout1;
        A0->lastOutput = a0last; A1->lastOutput = a1last;
        rv->lpLastout[k] = lp;
        rv->preDelayPtr[k] = ptr;
        sptr += 160;
    }
}

#endif
