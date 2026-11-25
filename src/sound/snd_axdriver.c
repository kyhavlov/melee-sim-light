/* HSD axdriver (doldecomp/melee sysdolphin/baselib, Matching), for the sound core (src/sound/README.md) */
#if MSL_SOUND
#include "dolphin.h" /* the sound core's: the console's bus clock as a number */
#include <MSL/math_ppc.h> /* __frsqrte: the Gekko seed the retail sqrt expansion starts from */
#include "decomp/baselib/synth.h"
/* axdriver.c registers AXDriverCallback, a void(void), as the synth's master clock callback, which synth.c calls with the stream's
 * position: harmless on the console, a trap in WebAssembly. It is registered through a function of the callback's type. */
static void AXDriverCallback(void);
static void msl_snd_AXDriverCallback(int position)
{
    (void) position;
    AXDriverCallback();
}
#define HSD_SynthSFXSetDriverMasterClockCallback(callback) HSD_SynthSFXSetDriverMasterClockCallback(msl_snd_AXDriverCallback)
#define HSD_AudioSFXStartParam msl_snd_AudioSFXStartParam_game
#define HSD_AudioPStreamStartChParam msl_snd_AudioPStreamStartChParam_game
#include "decomp/baselib/axdriver.c.inc"
#undef HSD_AudioSFXStartParam
#undef HSD_AudioPStreamStartChParam
#include "sound.h"
/* (the checks) every sound the game starts, logged (core.c msl_sound_log): the sound id, volume, pan, track and channel it asks for */
int HSD_AudioSFXStartParam(int sound_id, u8 volume, u8 pan, int track, int channel)
{
    int vid = msl_snd_AudioSFXStartParam_game(sound_id, volume, pan, track, channel);
    msl_snd_log(1, sound_id, volume, pan, track, channel, vid);
    return vid;
}
/* (the checks) the music the game starts: its file (the disc's entry number), volume and track */
bool HSD_AudioPStreamStartChParam(const char* path, u8 volume, int track)
{
    msl_snd_log(2, DVDConvertPathToEntrynum(path), volume, track, 0, 0, 0);
    return msl_snd_AudioPStreamStartChParam_game(path, volume, track);
}
/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_probe_axdriver(void) { return &AXDriver_804C45A0; }
#endif
