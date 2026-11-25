/* The sound core's DSP (src/sound/README.md): what the AX microcode does with the command list the SDK's AXCL.c builds (__AXNextFrame),
 * one AX frame of 160 samples at 32 kHz. This is ours: the microcode is not C. It follows the SDK's own description of the parameter
 * blocks (dolphin/ax.h: AXPB) and what the CPU side reads back (__AXServiceVPB: state, currentVolume, currentAddress; the depop values
 * __AXDepopVoice adds up):
 * - the voices: the parameter blocks from the command list's PB address, following pb.next; a voice runs while state is 1
 * - samples from ARAM: DSP-ADPCM (format 0; 8-byte frames, a header byte (predictor << 4 | scale) and 14 nibbles; the decoder
 *   sample = clamp16((((nibble << scale) << 11) + a1 * yn1 + a2 * yn2 + 1024) >> 11)), PCM16 (0x0A, big-endian) and PCM8 (0x19),
 *   their addresses in nibbles, samples and bytes; at the end address a looping voice goes to its loop address (pred_scale from the
 *   loop context, yn1/yn2 too unless the voice is a stream), any other stops (state 0) and plays silence
 * - sample rate conversion: ratio 16.16 per output sample, a 4-tap polyphase filter over the last four samples (srcSelect 0; the
 *   console's coefficient tables are in the DSP's ROM: ours are a windowed sinc, one set per coefSelect), linear (1) or none (2)
 * - the volume envelope: currentVolume (1.15) times each sample, plus currentDelta every sample
 * - the mixer: L, R, S and the two aux buses' L, R, S (mixerCtrl: aux A, aux B, surround, ramps), and the ITD delay in stereo
 * - the depop: each running voice's last contribution to each bus (pb.dpop), and the studio's fades of the stopped ones (AXSPB)
 * - aux: the voices' sends into the aux input buffers, and the processed aux output (the CPU's AXFX, two frames behind) added back
 * - the output: 32-bit sums clamped to 16 bits, left and right interleaved. */
#if MSL_SOUND

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <dolphin/types.h>
#include "dolphin/ax.h"
#include "sound.h"

#define N 160

static inline uint32_t hilo(u16 hi, u16 lo) { return ((uint32_t) hi << 16) | lo; }
static inline void set_hilo(u16* hi, u16* lo, uint32_t v) { *hi = (u16) (v >> 16); *lo = (u16) v; }
static inline int32_t clamp16(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
static inline uint8_t aram8(uint32_t a) { return a < (16u << 20) ? msl_snd_aram[a] : 0; }

/* ---- the polyphase filter: 64 phases x 4 taps per coefSelect, built once (ours) ---- */
static int16_t poly[3][64][4];
static int poly_ready;

static void make_poly(void)
{
    /* cutoffs (of the input's Nyquist) for the 8, 12 and 16 kHz sets */
    static const double cut[3] = { 0.5, 0.75, 1.0 };
    int c, p, t;
    for (c = 0; c < 3; c++) {
        for (p = 0; p < 64; p++) {
            double f = p / 64.0, w[4], sum = 0;
            for (t = 0; t < 4; t++) {
                double x = (t - 1) - f; /* taps at -1, 0, 1, 2 around the position */
                double s = x == 0 ? 1.0 : sin(M_PI * x * cut[c]) / (M_PI * x * cut[c]);
                double win = 0.5 + 0.5 * cos(M_PI * x / 2.0);
                w[t] = s * (fabs(x) < 2.0 ? win : 0.0);
                sum += w[t];
            }
            for (t = 0; t < 4; t++) poly[c][p][t] = (int16_t) lrint(w[t] / sum * 32767.0);
        }
    }
    poly_ready = 1;
}

/* ---- the accelerator: one sample from ARAM at the voice's current address, then the next address ---- */
static int16_t fetch(AXPB* pb, int* stopped)
{
    uint32_t cur = hilo(pb->addr.currentAddressHi, pb->addr.currentAddressLo);
    uint32_t end = hilo(pb->addr.endAddressHi, pb->addr.endAddressLo);
    int32_t s = 0;
    if (*stopped) return 0;
    switch (pb->addr.format) {
    case 0x00: { /* DSP-ADPCM */
        int nib, scale, idx;
        int32_t a1, a2, v;
        if ((cur & 15) == 0) {
            pb->adpcm.pred_scale = aram8(cur >> 1);
            cur += 2;
        }
        nib = aram8(cur >> 1);
        nib = (cur & 1) ? (nib & 15) : (nib >> 4);
        if (nib >= 8) nib -= 16;
        scale = pb->adpcm.pred_scale & 15;
        idx = (pb->adpcm.pred_scale >> 4) & 7;
        a1 = (int16_t) pb->adpcm.a[idx][0];
        a2 = (int16_t) pb->adpcm.a[idx][1];
        v = (((int32_t) nib << scale) << 11) + a1 * (int16_t) pb->adpcm.yn1 + a2 * (int16_t) pb->adpcm.yn2 + 1024;
        s = clamp16(v >> 11);
        pb->adpcm.yn2 = pb->adpcm.yn1;
        pb->adpcm.yn1 = (u16) (int16_t) s;
        break;
    }
    case 0x0A: /* PCM16, big-endian; gain 0x800 is 1 */
        s = (int16_t) ((aram8(cur * 2) << 8) | aram8(cur * 2 + 1));
        s = clamp16((s * (int32_t) pb->adpcm.gain) >> 11);
        break;
    case 0x19: /* PCM8 */
        s = (int8_t) aram8(cur);
        s = clamp16(((s << 8) * (int32_t) pb->adpcm.gain) >> 11);
        break;
    default:
        s = 0;
        break;
    }
    if (cur == end) {
        if (pb->addr.loopFlag) {
            cur = hilo(pb->addr.loopAddressHi, pb->addr.loopAddressLo);
            if (pb->addr.format == 0x00) {
                pb->adpcm.pred_scale = pb->adpcmLoop.loop_pred_scale;
                if (pb->type != 1) {
                    pb->adpcm.yn1 = pb->adpcmLoop.loop_yn1;
                    pb->adpcm.yn2 = pb->adpcmLoop.loop_yn2;
                }
            }
        } else {
            *stopped = 1;
            pb->state = 0;
        }
    } else {
        cur++;
    }
    set_hilo(&pb->addr.currentAddressHi, &pb->addr.currentAddressLo, cur);
    return (int16_t) s;
}

/* ---- the buses a voice's samples go to ---- */
typedef struct { int32_t* l; int32_t* r; int32_t* s; } Bus;

static int32_t main_l[N], main_r[N], main_s[N];

static inline void ramp_mix(int32_t* dst, int32_t x, u16* vol, u16* delta, int ramp, int i, int16_t* last)
{
    int32_t v = (int32_t) *vol;
    int32_t y = (x * v) >> 15;
    dst[i] += y;
    if (ramp) *vol = (u16) (v + (int16_t) *delta);
    if (last) *last = (int16_t) clamp16(y);
}

/* one voice: 160 samples through the converter, the envelope and the mixer */
static int32_t mute_l[N], mute_r[N], mute_s[N], mute_a[3 * N], mute_b[3 * N];

static void voice_into(AXPB* pb, Bus* a, Bus* b, int32_t* ml, int32_t* mr, int32_t* ms)
{
    int16_t in[N];
    int i, stopped = 0;
    uint32_t ratio = hilo(pb->src.ratioHi, pb->src.ratioLo);
    uint32_t frac = pb->src.currentAddressFrac;
    int16_t* h = (int16_t*) pb->src.last_samples;
    int ramp = (pb->mixerCtrl & 8) != 0;
    int16_t dl = 0, dr = 0, ds = 0, dal = 0, dar = 0, das = 0, dbl = 0, dbr = 0, dbs = 0;

    /* the converter: an output sample from the four last input samples at the fraction, then the fraction moves on by the ratio and
     * every whole input sample passed is fetched */
    for (i = 0; i < N; i++) {
        int32_t y;
        if (pb->srcSelect == 2) {
            y = h[3];
        } else if (pb->srcSelect == 1) {
            y = h[1] + ((((int32_t) h[2] - h[1]) * (int32_t) frac) >> 16);
        } else {
            const int16_t* c = poly[pb->coefSelect < 3 ? pb->coefSelect : 0][frac >> 10];
            y = ((int32_t) h[0] * c[0] + (int32_t) h[1] * c[1] + (int32_t) h[2] * c[2] + (int32_t) h[3] * c[3]) >> 15;
        }
        in[i] = (int16_t) clamp16(y);
        frac += pb->srcSelect == 2 ? 0x10000 : ratio;
        while (frac >= 0x10000) {
            h[0] = h[1]; h[1] = h[2]; h[2] = h[3];
            h[3] = fetch(pb, &stopped);
            frac -= 0x10000;
        }
    }
    pb->src.currentAddressFrac = (u16) frac;

    for (i = 0; i < N; i++) {
        /* the envelope */
        int32_t vol = pb->ve.currentVolume;
        int32_t x = ((int32_t) in[i] * vol) >> 15;
        int32_t xl = x, xr = x;
        pb->ve.currentVolume = (u16) (vol + pb->ve.currentDelta);
        /* ITD: the left and right ears hear the voice shiftL / shiftR samples late (the voice's own 32-sample history) */
        if (pb->itd.flag) {
            int16_t* buf = (int16_t*) (uintptr_t) hilo(pb->itd.bufferHi, pb->itd.bufferLo);
            int k;
            if (buf != NULL) {
                for (k = 31; k > 0; k--) buf[k] = buf[k - 1];
                buf[0] = (int16_t) clamp16(x);
                xl = buf[pb->itd.shiftL < 32 ? pb->itd.shiftL : 31];
                xr = buf[pb->itd.shiftR < 32 ? pb->itd.shiftR : 31];
            }
        }
        ramp_mix(ml, xl, &pb->mix.vL, &pb->mix.vDeltaL, ramp, i, &dl);
        ramp_mix(mr, xr, &pb->mix.vR, &pb->mix.vDeltaR, ramp, i, &dr);
        if (pb->mixerCtrl & 4) ramp_mix(ms, x, &pb->mix.vS, &pb->mix.vDeltaS, ramp, i, &ds);
        if ((pb->mixerCtrl & 1) && a->l) {
            ramp_mix(a->l, xl, &pb->mix.vAuxAL, &pb->mix.vDeltaAuxAL, ramp, i, &dal);
            ramp_mix(a->r, xr, &pb->mix.vAuxAR, &pb->mix.vDeltaAuxAR, ramp, i, &dar);
            if (pb->mixerCtrl & 4) ramp_mix(a->s, x, &pb->mix.vAuxAS, &pb->mix.vDeltaAuxAS, ramp, i, &das);
        }
        if ((pb->mixerCtrl & 2) && b->l) {
            ramp_mix(b->l, xl, &pb->mix.vAuxBL, &pb->mix.vDeltaAuxBL, ramp, i, &dbl);
            ramp_mix(b->r, xr, &pb->mix.vAuxBR, &pb->mix.vDeltaAuxBR, ramp, i, &dbr);
            if (pb->mixerCtrl & 4) ramp_mix(b->s, x, &pb->mix.vAuxBS, &pb->mix.vDeltaAuxBS, ramp, i, &dbs);
        }
    }
    /* the ITD shifts reach their targets at the frame's end */
    if (pb->itd.flag) {
        pb->itd.shiftL = pb->itd.targetShiftL;
        pb->itd.shiftR = pb->itd.targetShiftR;
    }
    /* the depop: the voice's last contribution to each bus, which the CPU fades out if the voice is stopped */
    pb->dpop.aL = dl; pb->dpop.aR = dr; pb->dpop.aS = ds;
    pb->dpop.aAuxAL = dal; pb->dpop.aAuxAR = dar; pb->dpop.aAuxAS = das;
    pb->dpop.aAuxBL = dbl; pb->dpop.aAuxBR = dbr; pb->dpop.aAuxBS = dbs;
}

/* the studio's fade of the stopped voices' depop sums (__AXDepopFade wrote each start as a host long into the hi/lo pair) */
static void fade(int32_t* dst, const u16* hi, const s16* delta)
{
    int32_t v;
    int i;
    memcpy(&v, hi, sizeof v);
    for (i = 0; i < N; i++) {
        dst[i] += v;
        v += *delta;
    }
}

void msl_snd_dsp_run(const u16* cl, s16* pcm)
{
    AXPB* pbs = NULL;
    AXSPB* studio = NULL;
    Bus a_in = { 0 }, b_in = { 0 };
    int32_t* a_out = NULL;
    int32_t* b_out = NULL;
    int i, done = 0;

    if (!poly_ready) make_poly();
    memset(main_l, 0, sizeof main_l);
    memset(main_r, 0, sizeof main_r);
    memset(main_s, 0, sizeof main_s);

    /* the command list (AXCL.c __AXNextFrame) */
    while (cl != NULL && !done) {
        u16 op = *cl++;
        switch (op) {
        case 0x0: studio = (AXSPB*) (uintptr_t) hilo(cl[0], cl[1]); cl += 2; break;
        case 0x7: case 0x11: cl += 2; break; /* the surround buffer */
        case 0x2: pbs = (AXPB*) (uintptr_t) hilo(cl[0], cl[1]); cl += 2; break;
        case 0x3: break; /* process the voices (below, once the aux buffers are known) */
        case 0x4:
            a_in.l = (int32_t*) (uintptr_t) hilo(cl[0], cl[1]);
            a_out = (int32_t*) (uintptr_t) hilo(cl[2], cl[3]);
            cl += 4;
            break;
        case 0x5: case 0x10:
            b_in.l = (int32_t*) (uintptr_t) hilo(cl[0], cl[1]);
            b_out = (int32_t*) (uintptr_t) hilo(cl[2], cl[3]);
            cl += 4;
            break;
        case 0xE: cl += 4; break; /* the output buffers: pcm below */
        case 0xF: default: done = 1; break;
        }
    }
    if (a_in.l) { memset(a_in.l, 0, 3 * N * 4); a_in.r = a_in.l + N; a_in.s = a_in.l + 2 * N; }
    if (b_in.l) { memset(b_in.l, 0, 3 * N * 4); b_in.r = b_in.l + N; b_in.s = b_in.l + 2 * N; }

    /* the voices (the music's, while it is off, into buses nobody hears: their state goes on as the game's) */
    {
        uint64_t music = msl_snd_music_muted ? msl_snd_music_mask() : 0;
        Bus ma = { mute_a, mute_a + N, mute_a + 2 * N }, mb = { mute_b, mute_b + N, mute_b + 2 * N };
        for (AXPB* pb = pbs; pb != NULL; pb = (AXPB*) (uintptr_t) hilo(pb->nextHi, pb->nextLo)) {
            if (pb->state != 1) continue;
            if (music & (1ull << ((pb - pbs) & 63))) {
                voice_into(pb, &ma, &mb, mute_l, mute_r, mute_s);
            } else {
                voice_into(pb, &a_in, &b_in, main_l, main_r, main_s);
            }
        }
    }

    /* the stopped voices' fades */
    if (studio != NULL) {
        fade(main_l, &studio->dpopLHi, &studio->dpopLDelta);
        fade(main_r, &studio->dpopRHi, &studio->dpopRDelta);
        fade(main_s, &studio->dpopSHi, &studio->dpopSDelta);
        if (a_in.l) {
            fade(a_in.l, &studio->dpopALHi, &studio->dpopALDelta);
            fade(a_in.r, &studio->dpopARHi, &studio->dpopARDelta);
            fade(a_in.s, &studio->dpopASHi, &studio->dpopASDelta);
        }
        if (b_in.l) {
            fade(b_in.l, &studio->dpopBLHi, &studio->dpopBLDelta);
            fade(b_in.r, &studio->dpopBRHi, &studio->dpopBRDelta);
            fade(b_in.s, &studio->dpopBSHi, &studio->dpopBSDelta);
        }
    }

    /* the aux returns */
    for (i = 0; i < N; i++) {
        if (a_out) { main_l[i] += a_out[i]; main_r[i] += a_out[N + i]; main_s[i] += a_out[2 * N + i]; }
        if (b_out) { main_l[i] += b_out[i]; main_r[i] += b_out[N + i]; main_s[i] += b_out[2 * N + i]; }
    }

    /* the output (stereo: the surround bus is not heard) */
    for (i = 0; i < N; i++) {
        pcm[2 * i] = (s16) clamp16(main_l[i]);
        pcm[2 * i + 1] = (s16) clamp16(main_r[i]);
    }
}

/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_probe_dsp(void) { return &poly_ready; }

#endif
