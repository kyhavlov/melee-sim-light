/* the SDK's AXVPB (doldecomp/melee libs/dolphin, Matching), for the sound core (src/sound/README.md) */
#if MSL_SOUND
#include "dolphin.h" /* the sound core's: the console's bus clock as a number */
#include "decomp/ax/AXVPB.c.inc"
/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_probe_axvpb(void) { return &__AXNumVoices; }
#endif
