/* The sound core's driver (src/sound/README.md): start-up for a VS match, the per-frame order, the sound objects' pool, and the PCM
 * of each game frame.
 *
 * Start-up does what the game does from boot to the match, for the sound only:
 *   lbAudioAx_8002838C (gmmain.c: ARAM, the synth and AX, the aux effects (reverb_std on A, delay on B), the three SSM banks' room),
 *   lbAudioAx_80028690 (gmmain_lib.c: the common banks and audio/us/smash2.sem),
 *   lbAudioAx_80024C84 (the scene's sound state),
 *   lbAudioAx_8002785C (gmvs.c at the match's start: the fighters' and the stage's banks, waited for),
 *   lbAudioAx_80023F28 (Stage_80225074: the stage's music, the HPS stream).
 * Each game frame: lbAudioAx_80027DF8 first (gmscene.c calls it before HSD_GObj_RunProcs), then the frame (the game's sound calls go
 * to the game's sound code), then the sound objects' procs (HSD_GObj_SetupProc(.., fn_800262A0, 23): s_link 23, after the game's
 * procs), then every AX frame (160 samples at 32 kHz, 5 ms) that starts before the game frame ends (the console's 59.94 Hz:
 * frame n ends at sample (n + 1) * 8008 / 15). Ours: where those AX frames fall (on the console the AI interrupt does not wait for
 * the game), and the disc's timing (host.c).
 *
 * The sound objects (lbAudioAx_800263E8) are HSD GObjs in the game. Here they live in this file's pool (msl_snd_GObj_Create and
 * friends, snd_lbaudio_prelude.h), so the game's GObj lists, its object heaps and its procs are the ones the simulator had. */
#if MSL_SOUND

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <platform.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/pl/player.h>

#include "snd_rename.h"
#include "snd_lbaudio_prelude.h"
#include "decomp/lb/lbaudio_ax.h"
#include "decomp/baselib/axdriver.h"
#include "dolphin/ax.h"
AXPB* __AXGetPBs(void);
#include "sound.h"

#include <stdio.h>
#define MSL_SND_TRACE(what) (msl_snd_trace ? (void) fprintf(stderr, "sound: %s\n", what) : (void) 0)
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EMSCRIPTEN_KEEPALIVE_SND EMSCRIPTEN_KEEPALIVE
#else
#define EMSCRIPTEN_KEEPALIVE_SND
#endif
int msl_snd_trace = 0;
int msl_sound_on;
struct MslCoreMatch* msl_sound_match;
int msl_snd_music_muted;
int32_t msl_snd_randi_next = -1;

/* ---- the sound objects' pool ---- */

#define POOL 64
typedef struct {
    HSD_GObj gobj;
    HSD_GObjEvent proc;
    int used;
} Obj;
static Obj objs[POOL];
static uint8_t datas[POOL][0x50];
static uint8_t data_used[POOL];
HSD_GObj* msl_snd_plink_head[1];

HSD_GObj* msl_snd_GObj_Create(u16 classifier, u8 p_link, u8 priority)
{
    int i;
    HSD_GObj** at;
    (void) p_link; (void) priority;
    for (i = 0; i < POOL; i++) {
        if (!objs[i].used) break;
    }
    if (i == POOL) return NULL;
    memset(&objs[i], 0, sizeof objs[i]);
    objs[i].used = 1;
    objs[i].gobj.classifier = classifier;
    /* (linked last: the game links a new object after those of its p_link and priority) */
    at = &msl_snd_plink_head[0];
    while (*at != NULL) {
        objs[i].gobj.prev = *at;
        at = &(*at)->next;
    }
    *at = &objs[i].gobj;
    return &objs[i].gobj;
}

void msl_snd_GObj_InitUserData(HSD_GObj* gobj, u8 kind, void (*remove_func)(void*), void* data)
{
    gobj->user_data_kind = kind;
    gobj->user_data_remove_func = remove_func;
    gobj->user_data = data;
}

HSD_GObjProc* msl_snd_GObj_SetupProc(HSD_GObj* gobj, HSD_GObjEvent func, u8 pri)
{
    (void) pri;
    ((Obj*) gobj)->proc = func;
    return NULL;
}

void msl_snd_GObjFree(HSD_GObj* gobj)
{
    Obj* o = (Obj*) gobj;
    if (gobj == NULL || !o->used) return;
    if (gobj->user_data_remove_func != NULL && gobj->user_data != NULL) gobj->user_data_remove_func(gobj->user_data);
    if (gobj->prev != NULL) gobj->prev->next = gobj->next;
    else msl_snd_plink_head[0] = gobj->next;
    if (gobj->next != NULL) gobj->next->prev = gobj->prev;
    o->used = 0;
}

void* msl_snd_ObjAlloc(HSD_ObjAllocData* data)
{
    int i;
    (void) data;
    for (i = 0; i < POOL; i++) {
        if (!data_used[i]) {
            data_used[i] = 1;
            memset(datas[i], 0, sizeof datas[i]);
            return datas[i];
        }
    }
    return NULL;
}

void msl_snd_ObjFree(HSD_ObjAllocData* data, void* obj)
{
    int i;
    (void) data;
    for (i = 0; i < POOL; i++) {
        if (obj == datas[i]) data_used[i] = 0;
    }
}

/* fn_80025FAC's left/right roll: the gameplay stub drew it from the game's random stream, as it always did; this hands it on */
s32 msl_snd_Randi(s32 max_val)
{
    s32 r = msl_snd_randi_next;
    (void) max_val;
    msl_snd_randi_next = -1;
    return r < 0 ? 0 : r;
}

bool msl_snd_lang_us(void) { return true; }

/* a VS match (gmvs.h gm_8016B184: is this a 1P mode) */
bool msl_snd_gm_8016B184(void) { return false; }

/* gm_1601.c gm_801603B0: the sound and music volumes from the memory card's sound balance (gmMainLib_8015ED74), the game's default
 * 0 (the middle): gm_801601C4(0) and gm_80160244(0) are both 0x7F */
void msl_snd_gm_801603B0(void)
{
    msl_snd_lbAudioAx_80024614(0x7F);
    msl_snd_lbAudioAx_800245F4(0x7F);
}

/* stage.c Stage_8022519C: a stage's ground kind (stage_id_map, the VS stages' part: Battlefield 0x1F -> 0x24 ...) */
GrKind msl_snd_Stage_8022519C(StKind stkind)
{
    static const uint8_t map[33] = {
        0x00, 0x01, 0x0C, 0x10, 0x02, 0x04, 0x08, 0x0E, 0x0A, 0x14, 0x12, 0x03, 0x05, 0x06, 0x07, 0x09, 0x0B,
        0x0D, 0x15, 0x18, 0x19, 0x1A, 0x0F, 0x11, 0x13, 0x16, 0x16, 0x1B, 0x1C, 0x1D, 0x1E, 0x24, 0x25,
    };
    return stkind >= 0 && stkind < 33 ? map[stkind] : 0;
}

/* the stage's main track: what Ground_801C24F8 (ground.c) picks with flag 1, read from the stage's table without its writes to
 * stage_info; -2 is the music of player 1's character (lbAudioAx_8002305C, which rolls HSD_Randi(2) as the game does) */
static s32 stage_music(s32 stkind)
{
    UnkBgmStruct* table = stage_info.param->xB0;
    s32 i;
    for (i = 0; i < stage_info.param->xB4; i++) {
        if (table[i].x0 == stkind) {
            if (table[i].x4 == -2) return msl_snd_lbAudioAx_8002305C(Player_GetPlayerCharacter(0), HSD_Randi(2));
            return table[i].x4;
        }
    }
    return -1;
}

/* lbaudio_ax.c asks for the 1P mode's opponents only when gm_8016B184 says 1P mode, never in a VS match */
u8 msl_snd_gm_80169370(s32 slot)
{
    (void) slot;
    return CHKIND_MAX;
}

/* ---- the frame clock and the PCM ---- */

static int16_t pcm[2 * 160 * 4];
static int pcm_frames;
static int64_t game_frames, ax_frames;
static int requests;
#define LOG_MAX 8192
static int32_t log_entries[LOG_MAX][8];
static int log_count;
void msl_snd_log(int kind, int a, int b, int c, int d, int e, int f)
{
    if (log_count < LOG_MAX) {
        int32_t* l = log_entries[log_count++];
        l[0] = (int32_t) game_frames; l[1] = kind; l[2] = a; l[3] = b; l[4] = c; l[5] = d; l[6] = e; l[7] = f;
    }
}
const int32_t* msl_sound_log(void) { return &log_entries[0][0]; }
int msl_sound_log_count(void) { return log_count; }

int msl_sound_need_count(void) { return msl_snd_host_need_count(); }
const char* msl_sound_need(int i) { return msl_snd_host_need(i); }

/* The sound's static state (everything the objects of src/sound keep: the decomp's statics, set up once at the console's boot, and
 * ours) lies between the markers of 00_state_begin.c and zz_state_end.c, the first and last of these objects as the build links them.
 * The first start keeps a copy of its initialized part; every later start puts it back and zeroes the rest, so a start after one
 * that named missing files, or after a reset, begins from the state the first one had. */
extern int msl_snd_data_begin, msl_snd_data_end, msl_snd_bss_begin, msl_snd_bss_end;
const void* msl_snd_host_probe(void);
const void* msl_snd_probe_axvpb(void);
const void* msl_snd_probe_lbaudio(void);
const void* msl_snd_probe_axdriver(void);
const void* msl_snd_probe_synth(void);
const void* msl_snd_probe_dsp(void);
void msl_snd_host_release(void);
static uint8_t* state_copy;

static int state_inside(const void* p)
{
    const uint8_t* q = (const uint8_t*) p;
    return (q >= (const uint8_t*) &msl_snd_data_begin && q < (const uint8_t*) (&msl_snd_data_end + 1)) ||
           (q >= (const uint8_t*) &msl_snd_bss_begin && q < (const uint8_t*) (&msl_snd_bss_end + 1));
}

static int state_reset(void)
{
    uint8_t* data = (uint8_t*) &msl_snd_data_begin;
    size_t data_size = (size_t) ((uint8_t*) (&msl_snd_data_end + 1) - data);
    uint8_t* bss = (uint8_t*) &msl_snd_bss_begin;
    size_t bss_size = (size_t) ((uint8_t*) (&msl_snd_bss_end + 1) - bss);
    uint8_t* copy = state_copy;
    int trace = msl_snd_trace;
    const void* probes[] = { objs, &pcm_frames, msl_snd_host_probe(), msl_snd_probe_axvpb(), msl_snd_probe_lbaudio(),
                             msl_snd_probe_axdriver(), msl_snd_probe_synth(), msl_snd_probe_dsp() };
    size_t i;
    if ((uint8_t*) &msl_snd_data_end <= data || (uint8_t*) &msl_snd_bss_end <= bss) return -1;
    for (i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        if (!state_inside(probes[i])) return -1;
    }
    if (copy == NULL) {
        /* the first start: nothing of the sound has run yet */
        copy = (uint8_t*) malloc(data_size);
        if (copy == NULL) return -1;
        memcpy(copy, data, data_size);
        state_copy = copy;
        return 0;
    }
    msl_snd_host_release();
    memcpy(data, copy, data_size);
    memset(bss, 0, bss_size);
    state_copy = copy;
    msl_snd_trace = trace;
    return 0;
}

int msl_sound_start(const char* root)
{
    s32 bgm = -1;
    u32 keep;
    if (state_reset() != 0) return -1;
    msl_sound_on = 0;
    msl_sound_match = NULL;
    log_count = 0;
    if (msl_snd_host_start() != 0) return -1;
    msl_snd_host_set_root(root);
    MSL_SND_TRACE("boot");
    msl_snd_lbAudioAx_8002838C();
    MSL_SND_TRACE("common banks");
    msl_snd_lbAudioAx_80028690();
    MSL_SND_TRACE("scene");
    msl_snd_lbAudioAx_80024C84();
    MSL_SND_TRACE("match banks");
    msl_snd_lbAudioAx_8002785C();
    MSL_SND_TRACE("music");
    /* the stage's music (Stage_80225074 -> Ground_801C24F8 with the VS flags rolls HSD_Randi for an alternate track before the match:
     * not known from a replay, so the main track, as flag 1 picks it; the random stream is put back) */
    keep = *seed_ptr;
    bgm = stage_music(Stage_80225194());
    *seed_ptr = keep;
    if (bgm >= 0) msl_snd_lbAudioAx_80023F28(bgm);
    msl_snd_host_service();
    game_frames = ax_frames = 0;
    pcm_frames = 0;
    requests = 0;
    if (msl_snd_host_need_count() != 0) return msl_snd_host_need_count();
    msl_sound_on = 1;
    return 0;
}

void msl_sound_stop(void)
{
    msl_sound_on = 0;
    msl_sound_match = NULL;
}

void msl_sound_frame_begin(void)
{
    if (!msl_sound_on) return;
    msl_snd_lbAudioAx_80027DF8();
}

void msl_sound_frame_end(void)
{
    HSD_GObj* g;
    if (!msl_sound_on) return;
    /* the sound objects' procs */
    for (g = msl_snd_plink_head[0]; g != NULL;) {
        HSD_GObj* next = g->next;
        Obj* o = (Obj*) g;
        if (o->proc != NULL) o->proc(g);
        g = next;
    }
    game_frames++;
    pcm_frames = 0;
    while (ax_frames * 2400 < game_frames * 8008 && pcm_frames + 160 <= (int) (sizeof pcm / 4)) {
        msl_snd_host_service();
        msl_snd_ax_frame(pcm + 2 * pcm_frames);
        pcm_frames += 160;
        ax_frames++;
    }
}

const int16_t* msl_sound_pcm(void) { return pcm; }
int msl_sound_pcm_frames(void) { return msl_sound_on ? pcm_frames : 0; }

/* music off: the music stream's voices are played as before (their state is the game's) but not heard (dsp.c) */
void msl_sound_set_music(int on) { msl_snd_music_muted = !on; }

int msl_sound_info(int which)
{
    switch (which) {
    case 0: return AXDriver_8038E5D4();
    case 1: return (int) ax_frames;
    case 2: return requests;
    case 3: { uint64_t m = msl_snd_music_mask(); int n = 0; while (m) { n += (int) (m & 1); m >>= 1; } return n; }
    case 4: { AXPB* pb = __AXGetPBs(); int n = 0, i; for (i = 0; i < 64; i++) n += pb[i].state == 1; return n; }
    case 5: case 6: case 7: case 8: case 9: case 10: {
        /* (the checks) the first music voice's DSP block: loop flag, current, end and loop address, state, ratio */
        uint64_t m = msl_snd_music_mask(); int i = 0; AXPB* pb;
        if (!m) return -1;
        while (!(m & 1)) { m >>= 1; i++; }
        pb = &__AXGetPBs()[i];
        switch (which) {
        case 5: return pb->addr.loopFlag;
        case 6: return (int) (((uint32_t) pb->addr.currentAddressHi << 16) | pb->addr.currentAddressLo);
        case 7: return (int) (((uint32_t) pb->addr.endAddressHi << 16) | pb->addr.endAddressLo);
        case 8: return (int) (((uint32_t) pb->addr.loopAddressHi << 16) | pb->addr.loopAddressLo);
        case 9: return pb->state;
        default: return (int) (((uint32_t) pb->src.ratioHi << 16) | pb->src.ratioLo);
        }
    }
    default: return 0;
    }
}

void msl_snd_trace_call(const char* name);
void msl_sound_count_request(const char* name) { requests++; if (msl_snd_trace > 1) msl_snd_trace_call(name); }
void msl_snd_trace_call(const char* name) { fprintf(stderr, "sound: frame %d %s\n", (int) game_frames, name); }
/* (the checks) 0 quiet, 1 start-up, 2 every call */
EMSCRIPTEN_KEEPALIVE_SND void msl_sound_trace(int level) { msl_snd_trace = level; }

#endif
