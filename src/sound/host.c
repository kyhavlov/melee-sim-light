/* The sound core's console stand-ins (src/sound/README.md): what the game's sound code and the SDK's AX library ask of the GameCube
 * that is not game code - interrupts, the OS heap, ARAM and its DMA queue, the audio interface, the DVD drive and HSD's device
 * command queue (HSD_DevComRequest). All of it is ours. The rules it keeps:
 * - Nothing here touches the game's state or its heaps: the audio heap and ARAM are one block allocated when the sound core starts
 *   (msl_snd_host_start), so a module without sound is the module it was.
 * - The disc: a file is read from the module's file system (the host page writes the sound files there) the first time it is named, kept in
 *   the audio block, and every read is answered from it. A file the host has not written is noted (msl_snd_host_need) and read as
 *   empty: the host writes it and starts the sound again (src/sound/core.c).
 * - HSD_DevComRequest's requests finish in the order the device would take them (priority 0 first, each queue in order), all of them at
 *   the start of the next AX frame, or at once while the game waits for its loads (lb_800195D0). On the console that depends on the
 *   drive's seek and read times. */
#if MSL_SOUND

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/types.h>
#include <dolphin/os.h>
#include <dolphin/dvd.h>
#include <dolphin/ar.h>
#include "dolphin/ax.h"
#include "dolphin/axfx.h"
#include "decomp/ax/__ax.h"
#include "decomp/baselib/synth.h"
#include "decomp/baselib/devcom.h"
#include "sound.h"

/* ---- the audio block: ARAM and the audio heap ---- */

#define MSL_SND_ARAM_SIZE (16u << 20)
#define MSL_SND_HEAP_SIZE (12u << 20)

uint8_t* msl_snd_aram;
static uint8_t* heap_base;
static uint32_t heap_top;
static uint32_t aram_top;

/* first fit over a list of free blocks, each with an 8-byte header (size, free flag); 32-byte aligned as the console's OSAlloc */
typedef struct { uint32_t size; uint32_t free; } Blk;

/* (core.c, before the sound's static state is put back) ARAM and the audio block (the files read so far are in it), freed */
void msl_snd_host_release(void)
{
    free(msl_snd_aram);
    free(heap_base);
    msl_snd_aram = NULL;
    heap_base = NULL;
}

int msl_snd_host_start(void)
{
    if (msl_snd_aram == NULL) {
        msl_snd_aram = (uint8_t*) calloc(1, MSL_SND_ARAM_SIZE);
        heap_base = (uint8_t*) aligned_alloc(32, MSL_SND_HEAP_SIZE);
        if (msl_snd_aram == NULL || heap_base == NULL) return -1;
    }
    memset(heap_base, 0, MSL_SND_HEAP_SIZE);
    heap_top = 0;
    aram_top = 0x4000; /* ARInit keeps the first 16 KiB for the OS */
    return 0;
}

static void* heap_alloc(unsigned long size)
{
    uint32_t need = (uint32_t) ((size + 31) & ~31u) + 32, at = 0;
    while (at < heap_top) {
        Blk* b = (Blk*) (heap_base + at);
        if (b->free && b->size >= need) {
            b->free = 0;
            return heap_base + at + 32;
        }
        at += b->size;
    }
    if (heap_top + need > MSL_SND_HEAP_SIZE) {
        OSReport("sound: audio heap full (%lu)\n", size);
        return NULL;
    }
    Blk* b = (Blk*) (heap_base + heap_top);
    b->size = need;
    b->free = 0;
    heap_top += need;
    return (uint8_t*) b + 32;
}

static void heap_free(void* p)
{
    if (p == NULL) return;
    ((Blk*) ((uint8_t*) p - 32))->free = 1;
}

volatile OSHeapHandle __OSCurrHeap = 0;
void* OSAllocFromHeap(int heap, unsigned long size) { (void) heap; return heap_alloc(size); }
void OSFreeToHeap(int heap, void* ptr) { (void) heap; heap_free(ptr); }

/* ---- the CPU ---- */

BOOL OSDisableInterrupts(void) { return 0; }
BOOL OSRestoreInterrupts(BOOL level) { return level; }
void DCFlushRangeNoSync(void* addr, u32 n) { (void) addr; (void) n; }

static u32 sound_mode = 1; /* OS_SOUND_MODE_STEREO: the console's default */
u32 OSGetSoundMode(void) { return sound_mode; }
void OSSetSoundMode(u32 mode) { sound_mode = mode; }

/* ---- ARAM ---- */

u32 ARInit(u32* stack_index_addr, u32 num_entries) { (void) stack_index_addr; (void) num_entries; return 0x4000; }
void ARQInit(void) {}
u32 ARGetSize(void) { return MSL_SND_ARAM_SIZE; }
u32 ARAlloc(u32 length)
{
    u32 at = aram_top;
    aram_top += length;
    return at;
}

/* ---- the audio interface (the stream volume is the disc stream's, which the game does not play) ---- */

void msl_snd_DCFlushRange(void* addr, u32 n) { (void) addr; (void) n; }
void msl_snd_DCInvalidateRange(void* addr, u32 n) { (void) addr; (void) n; }
void AIInit(u8* stack) { (void) stack; }
void AISetDSPSampleRate(u32 rate) { (void) rate; }
void AISetStreamVolLeft(u8 vol) { (void) vol; }
void AISetStreamVolRight(u8 vol) { (void) vol; }

/* ---- the disc ---- */

#define MAX_FILES 96
typedef struct { char path[64]; uint8_t* data; uint32_t size; int tried; } File;
static File files[MAX_FILES];
static int nfiles;
static char root[256];
static char need[MAX_FILES][64];
static int nneed;

void msl_snd_host_set_root(const char* r)
{
    snprintf(root, sizeof root, "%s", r ? r : "");
}

int msl_snd_host_need_count(void) { return nneed; }
const char* msl_snd_host_need(int i) { return i >= 0 && i < nneed ? need[i] : NULL; }

static File* load(int e)
{
    File* f;
    if (e < 0 || e >= nfiles) return NULL;
    f = &files[e];
    if (!f->tried) {
        char full[340];
        FILE* fp;
        f->tried = 1;
        snprintf(full, sizeof full, "%s%s", root, f->path);
        fp = fopen(full, "rb");
        if (fp == NULL) {
            if (nneed < MAX_FILES) snprintf(need[nneed++], 64, "%s", f->path + (f->path[0] == '/'));
        } else {
            long n;
            fseek(fp, 0, SEEK_END);
            n = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            f->data = (uint8_t*) heap_alloc((unsigned long) n + 32);
            if (f->data != NULL && fread(f->data, 1, (size_t) n, fp) == (size_t) n) f->size = (uint32_t) n;
            fclose(fp);
        }
    }
    return f;
}

/* bytes [off, off + n) of file e into dst, zeros past its end. A file the host has not written reads as empty, so that the start-up
 * names every file it needs in one pass: an .ssm then has a header of no sounds (header size 0x10), anything else no bytes. */
static void file_read(int e, uint32_t off, void* dst, uint32_t n)
{
    File* f = load(e);
    uint32_t have = 0;
    if (f != NULL && f->data != NULL && off < f->size) have = f->size - off < n ? f->size - off : n;
    if (have) memcpy(dst, f->data + off, have);
    if (have < n) memset((uint8_t*) dst + have, 0, n - have);
    if (f != NULL && f->data == NULL && off == 0 && n >= 4) {
        size_t len = strlen(f->path);
        if (len > 4 && strcmp(f->path + len - 4, ".ssm") == 0) ((uint8_t*) dst)[3] = 0x10;
    }
}

s32 DVDConvertPathToEntrynum(const char* path)
{
    int i;
    for (i = 0; i < nfiles; i++) {
        if (strcmp(files[i].path, path) == 0) return i;
    }
    if (nfiles == MAX_FILES || strlen(path) >= sizeof files[0].path) return -1;
    snprintf(files[nfiles].path, sizeof files[0].path, "%s", path);
    files[nfiles].data = NULL;
    files[nfiles].size = 0;
    files[nfiles].tried = 0;
    /* (read now: a file the game names is a file it reads, and the host learns of a missing one before the match plays) */
    nfiles++;
    load(nfiles - 1);
    return nfiles - 1;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* info)
{
    File* f = load(entrynum);
    if (f == NULL) return FALSE;
    memset(info, 0, sizeof *info);
    info->startAddr = (u32) entrynum;
    info->length = f->size;
    return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* info, void* addr, s32 length, s32 offset, DVDCallback cb, s32 prio)
{
    (void) prio;
    file_read((int) info->startAddr, (uint32_t) offset, addr, (uint32_t) length);
    if (cb != NULL) cb(length, info);
    return TRUE;
}

BOOL DVDClose(DVDFileInfo* info) { (void) info; return TRUE; }

/* ---- HSD's device command queue (sysdolphin/baselib/devcom.c's requests; types: 0x2x from the disc, 0x0x from main memory;
 *      destination type & 7: 1 main memory, 2 a relay buffer handed to the callback, 3 ARAM) ---- */

typedef struct { int file; uintptr_t src, dest; size_t size; int type, pri, req; HSD_DevComCallback cb; uintptr_t args; int live; uint32_t frame; } Req;
#define MAX_REQS 64
static Req reqs[MAX_REQS];
static int dc_base = 0, seq = 0;
/* AX frames so far (msl_snd_ax_frame): a request is answered at the earliest in the AX frame after the one it was made in */
static uint32_t ax_now;
static uint8_t relay[0x2000] __attribute__((aligned(32)));

int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size, int type, int pri, HSD_DevComCallback cb, uintptr_t args)
{
    int i;
    for (i = 0; i < MAX_REQS; i++) {
        if (!reqs[i].live) break;
    }
    if (i == MAX_REQS) {
        OSReport("sound: device queue full\n");
        return -1;
    }
    pri = (type & 0x38) == 0x20 ? pri : 3;
    reqs[i] = (Req) { file, src, dest, size, type, pri, dc_base + pri, cb, args, ++seq, ax_now };
    dc_base += 4;
    return reqs[i].req;
}

int HSD_DevComCancelEx(int dcReq, u32 flags, HSD_DevComCallback cb, uintptr_t args)
{
    int i;
    (void) flags; (void) cb; (void) args;
    for (i = 0; i < MAX_REQS; i++) {
        if (reqs[i].live && reqs[i].req == dcReq) {
            reqs[i].live = 0;
            return 1;
        }
    }
    return 0;
}

extern int msl_snd_trace;
static void finish(Req* r)
{
    int dest = r->type & 7, from_disc = (r->type & 0x38) == 0x20;
    void* buf = NULL;
    if (msl_snd_trace > 1)
        fprintf(stderr, "sound: ax %u answer type %x file %s off %x size %x\n", (unsigned) ax_now, r->type,
                 r->file >= 0 && r->file < nfiles ? files[r->file].path : "-", (unsigned) r->src, (unsigned) r->size);
    r->live = 0;
    if (dest == 1) {
        if (from_disc) file_read(r->file, (uint32_t) r->src, (void*) r->dest, (uint32_t) r->size);
        else memmove((void*) r->dest, (void*) r->src, r->size);
    } else if (dest == 2) {
        uint32_t n = r->size < sizeof relay ? (uint32_t) r->size : (uint32_t) sizeof relay;
        file_read(r->file, (uint32_t) r->src, relay, n);
        buf = relay;
    } else if (dest == 3) {
        if (r->dest + r->size <= MSL_SND_ARAM_SIZE) {
            if (from_disc) file_read(r->file, (uint32_t) r->src, msl_snd_aram + r->dest, (uint32_t) r->size);
            /* (main memory -> ARAM: the only one is HSD_SynthInit's 0x500 bytes from address 0, the stream's end silence: zeros) */
            else memset(msl_snd_aram + r->dest, 0, r->size);
        }
    }
    /* (synth.c hands HSD_SynthPStreamFirstHakoDataCallback, a void(void), as a device callback: called as what it is, since a
     *  call through the wrong type traps in WebAssembly) */
    if ((void*) r->cb == (void*) HSD_SynthPStreamFirstHakoDataCallback) HSD_SynthPStreamFirstHakoDataCallback();
    else if (r->cb != NULL) r->cb(r->req, r->args, buf, false);
}

/* Finish queued requests in the device's order: those made before this AX frame (all = every one, and those their callbacks make: the
 * game waiting for its loads). A request is never answered in the AX frame it was made in: on the console a read takes milliseconds,
 * so the parameter blocks are synced between a request and the next one its answer makes (the music stream sets a voice's whole
 * address block (AXSetVoiceAddr) with one answer and its addresses with the next: synced in one AX frame, the loop flag would be lost:
 * __AXServiceVPB copies the whole block only when no single address changed). */
static void service(int all)
{
    for (;;) {
        Req* best = NULL;
        int i;
        for (i = 0; i < MAX_REQS; i++) {
            Req* r = &reqs[i];
            if (!r->live || (!all && r->frame >= ax_now)) continue;
            if (best == NULL || r->pri < best->pri || (r->pri == best->pri && r->live < best->live)) best = r;
        }
        if (best == NULL) return;
        finish(best);
    }
}

void msl_snd_host_service(void) { service(0); }

/* the wait the game passes to its loads (HSD_SynthSFXWaitForLoadCompletion, AXDriver_8038DA70): the device works meanwhile */
void lb_800195D0(void) { service(1); }

/* ---- AX: AXInit and the frame AXOut runs on the console's AI interrupt (SDK ax/AX.c, ax/AXOut.c), with the DSP model (dsp.c) ---- */

static s16 out_buffer[2][320] __attribute__((aligned(32)));
static long out_sbuffer[160] __attribute__((aligned(32)));
static u32 out_frame;
static void (*user_callback)(void);

void AXRegisterCallback(void (*callback)()) { user_callback = callback; }

void AXInit(void)
{
    __AXAllocInit();
    __AXVPBInit();
    __AXSPBInit();
    __AXAuxInit();
    __AXClInit();
    out_frame = 0;
    memset(out_buffer, 0, sizeof out_buffer);
    memset(out_sbuffer, 0, sizeof out_sbuffer);
    __AXNextFrame(out_sbuffer, &out_buffer[1][0]);
    user_callback = NULL;
}

/* one AX frame (__AXOutNewFrame's order): the parameter blocks synced, the command list built last frame run by the DSP (ours), the
 * dropped voices' callbacks, the aux effects, the game's callback (HSD_SynthCallback: the .sem macros, envelopes, the music stream),
 * and the next frame's command list. 160 stereo samples go to pcm. */
void msl_snd_ax_frame(s16* pcm)
{
    u32 cl;
    ax_now++;
    __AXSyncPBs(0);
    __AXPrintStudio();
    cl = __AXGetCommandListAddress();
    msl_snd_dsp_run((const u16*) (uintptr_t) cl, pcm);
    __AXServiceCallbackStack();
    __AXProcessAux();
    if (user_callback != NULL) user_callback();
    __AXNextFrame(out_sbuffer, &out_buffer[out_frame][0]);
    out_frame ^= 1;
}

/* ---- the AX profile, unused by the game ---- */
AXPROFILE* __AXGetCurrentProfile(void) { return NULL; }

/* ---- the two AXFX effects the game never selects (it sets up reverb_std and delay: lbAudioAx_8002838C) ---- */
int AXFXReverbHiInit(struct AXFX_REVERBHI* r) { (void) r; return 0; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* r) { (void) r; return 1; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBHI* r) { (void) b; (void) r; }
int AXFXChorusInit(struct AXFX_CHORUS* c) { (void) c; return 0; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c) { (void) c; return 1; }
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_CHORUS* c) { (void) b; (void) c; }

/* (core.c: the bounds of the sound's static state hold this file's) */
const void* msl_snd_host_probe(void) { return &nfiles; }

#endif
