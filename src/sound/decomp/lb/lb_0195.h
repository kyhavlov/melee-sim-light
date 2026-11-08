/* lb_0195.h: the wait callback the game passes to its sound loads (lb_800195D0); the sound core's own (src/sound/host.c) completes the
 * disc queue while the game waits (the console's DVD interrupt) */
#ifndef MSL_SOUND_LB_0195_H
#define MSL_SOUND_LB_0195_H
void lb_800195D0(void);
#endif
