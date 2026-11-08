/* The game's sound functions as the sound core has them (snd_rename.h: the decomp's lbaudio_ax.c renamed msl_snd_*), for the
 * simulator's stand-ins that call them while the sound core is on (src/stubs/headless_exclusions.c).
 * The signatures are the decomp's (1f0499b melee/lb/lbaudio_ax.h). Without MSL_SOUND, SND_ROUTE does nothing: the stand-ins are what
 * they were. */
#ifndef MSL_SOUND_ROUTE_H
#define MSL_SOUND_ROUTE_H

#if MSL_SOUND
#include <stdbool.h>
#include <stdint.h>
#include <platform.h>
#include <sysdolphin/baselib/forward.h>
#include "sound.h"

int msl_snd_lbAudioAx_8002305C(int, int);
int msl_snd_lbAudioAx_80023694(void);
int msl_snd_lbAudioAx_800236B8(int);
int msl_snd_lbAudioAx_800236DC(void);
bool msl_snd_lbAudioAx_80023710(int);
int msl_snd_lbAudioAx_800237A8(enum_t sfx_id, int sfx_vol, int sfx_pan);
int msl_snd_lbAudioAx_80023870(int, int, int, int);
void msl_snd_lbAudioAx_80024030(int);
int msl_snd_lbAudioAx_80024184(int, int, int, int);
int msl_snd_lbAudioAx_80024304(int);
int msl_snd_lbAudioAx_800243F4(int);
int msl_snd_lbAudioAx_800245F4(int);
void msl_snd_lbAudioAx_80024614(int);
void msl_snd_lbAudioAx_80024B1C(int, int);
void msl_snd_lbAudioAx_80024B58(int, int);
int msl_snd_lbAudioAx_80024B94(int, int);
void msl_snd_lbAudioAx_80024C84(void);
void msl_snd_lbAudioAx_80024D50(void);
void msl_snd_lbAudioAx_80024DC4(int);
void msl_snd_lbAudioAx_80024E84(bool);
void msl_snd_lbAudioAx_80024FDC(void);
void msl_snd_lbAudioAx_80024FF4(void);
void msl_snd_lbAudioAx_8002500C(int);
void msl_snd_lbAudioAx_80025038(int);
HSD_GObj* msl_snd_lbAudioAx_800263E8(float, HSD_GObj*, int, int, int, int, int, int, int, int, int);
int msl_snd_lbAudioAx_800264E4(HSD_GObj* data);
bool msl_snd_lbAudioAx_80026510(HSD_GObj*);
bool msl_snd_lbAudioAx_800265C4(HSD_GObj*, int sfx);
void msl_snd_lbAudioAx_80026F2C(u32);
void msl_snd_lbAudioAx_8002702C(u32 flags, u64 mask);
void msl_snd_lbAudioAx_80027168(void);
void msl_snd_lbAudioAx_80027648(void);
void msl_snd_lbAudioAx_8002785C(void);

extern int msl_snd_trace;
void msl_snd_trace_call(const char* name);

/* while the sound core is on: the game's own function (counted for the checks) */
#define SND_ROUTE(call)                                                                                                            \
    do {                                                                                                                           \
        if (msl_sound_on) {                                                                                                        \
            msl_sound_count_request(__func__);                                                                                          \
            return call;                                                                                                           \
        }                                                                                                                          \
    } while (0)
#define SND_ROUTE_VOID(call)                                                                                                       \
    do {                                                                                                                           \
        if (msl_sound_on) {                                                                                                        \
            msl_sound_count_request(__func__);                                                                                          \
            call;                                                                                                                  \
            return;                                                                                                                \
        }                                                                                                                          \
    } while (0)
#else
#define SND_ROUTE(call) ((void) 0)
#define SND_ROUTE_VOID(call) ((void) 0)
#endif

#endif
