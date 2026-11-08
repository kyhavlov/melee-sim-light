/* The first of the sound core's objects (the build lists src/sound in name order and links it in that order): the start of its
 * static state, so that a second start of the sound puts all of it back as it was before the first (core.c msl_snd_state_reset).
 * The decomp's sound code keeps its state in statics that the console sets up once, at boot. */
#if MSL_SOUND
int msl_snd_data_begin = 1;
int msl_snd_bss_begin;
#endif
