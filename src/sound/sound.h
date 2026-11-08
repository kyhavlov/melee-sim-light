/* The sound core (src/sound/README.md): the game's own sound code (the decomp's lbaudio_ax.c, HSD synth.c and axdriver.c, the SDK's
 * AX and AXFX) in the Wasm build (MSL_SOUND=1), with our DSP (dsp.c) and console stand-ins (host.c). Off unless the host starts it; while it is
 * off every sound call is the no-op it was. */
#ifndef MSL_SOUND_H
#define MSL_SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* on after msl_sound_start succeeded: the gameplay stubs' sound calls go to the game's sound code */
extern int msl_sound_on;
/* the match the sound belongs to (src/runtime/batch.c msl_core_batch_sound_start), NULL while it is off */
struct MslCoreMatch;
extern struct MslCoreMatch* msl_sound_match;
/* off: every stub is the no-op it was (a reset, restore, copy over or destroy of the sound's match does this) */
void msl_sound_stop(void);

/* Start the sound for the bound match (after its reset, before its first frame), reading the sound files under root (the module's
 * file system). Returns 0, or the number of files it needed and did not find (msl_sound_need): the host writes them and starts the
 * match and the sound again. Negative: no memory for the sound. */
int msl_sound_start(const char* root);
int msl_sound_need_count(void);
const char* msl_sound_need(int i);

/* around each game frame (msl_core_match_step): the scene loop's per-frame sound update before the frame's procs; after the frame
 * the sound objects' procs and the AX frames that start in this game frame */
void msl_sound_frame_begin(void);
void msl_sound_frame_end(void);

/* the last frame's sound: interleaved stereo 16-bit samples at 32 kHz, frames (pairs) in msl_sound_pcm_frames */
const int16_t* msl_sound_pcm(void);
int msl_sound_pcm_frames(void);

/* music on or off (the HPS stream's channel, the game's own volume) */
void msl_sound_set_music(int on);

/* (the checks) voices running, AX frames so far, sound requests so far */
int msl_sound_info(int which);

/* gameplay stubs (src/stubs/headless_exclusions.c): the value fn_80025FAC's HSD_Randi(2) gets (the stub draws it, as before) */
extern int32_t msl_snd_randi_next;

/* inside the sound core */
extern uint8_t* msl_snd_aram;
int msl_snd_host_start(void);
void msl_snd_host_set_root(const char* root);
void msl_snd_host_service(void);
int msl_snd_host_need_count(void);
const char* msl_snd_host_need(int i);
void msl_snd_ax_frame(int16_t* pcm);
void msl_snd_dsp_run(const uint16_t* command_list, int16_t* pcm);
/* the music stream's voices (PB indexes, synth.c's stream node), heard unless muted */
uint64_t msl_snd_music_mask(void);
extern int msl_snd_music_muted;
void msl_sound_count_request(const char* name);
/* (the checks) the log of the game's sound requests: kind 1 a sound started (id, volume, pan, track, channel, voice id), 2 the music
 * (the stream's file is the last one named); each entry 8 int32: game frame, kind, six values */
void msl_snd_log(int kind, int a, int b, int c, int d, int e, int f);
const int32_t* msl_sound_log(void);
int msl_sound_log_count(void);

#ifdef __cplusplus
}
#endif

#endif
