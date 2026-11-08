/* HSD synth (doldecomp/melee sysdolphin/baselib, Matching), for the sound core (src/sound/README.md) */
#if MSL_SOUND
#include <MSL/math_ppc.h> /* sqrtf_accurate: the MSL inline the game was built with */
#include "decomp/baselib/synth.c.inc"
#include <stdint.h>
/* (sound core) the music stream's voices: the PB indexes of the stream node's voices (src/sound/sound.h) */
uint64_t msl_snd_music_mask(void)
{
    struct HSD_SynthSFXNode* node = getNode(HSD_Synth_804D7760);
    uint64_t m = 0;
    int i;
    if (node == NULL) return 0;
    for (i = 0; i < node->voice_count && i < 2; i++) {
        if (node->voice[i] != NULL) m |= 1ull << (node->voice[i]->index & 63);
    }
    return m;
}
/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_probe_synth(void) { return &hsd_SynthSFXBankNum; }
#endif
