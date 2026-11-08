/* the game's lbaudio_ax.c (doldecomp/melee 1f0499b, Matching), renamed into the sound core (snd_rename.h; src/sound/README.md) */
#if MSL_SOUND
#include "snd_rename.h"
#include "snd_lbaudio_prelude.h"
#include "decomp/lb/lbaudio_ax.c.inc"
/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_probe_lbaudio(void) { return &lbl_804D63F4; }
#endif
