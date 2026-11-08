/* The SDK's <dolphin.h> for the sound core's copies of the SDK sources (src/sound/decomp/ax, axfx): the sim's own OS and type headers,
 * and the SDK's debug asserts as nothing (the release library has none). */
#ifndef MSL_SOUND_DOLPHIN_H
#define MSL_SOUND_DOLPHIN_H
#include <dolphin/types.h>
#include <dolphin/os.h>
#include <string.h>
/* the console's bus clock (os.h reads it from low memory, 0x800000F8): 162 MHz, as src/gx/sdk/msl_hw.h has it */
#undef OS_BUS_CLOCK
#define OS_BUS_CLOCK 162000000u
/* the data cache: nothing to flush or invalidate on this host (host.c) */
#define DCFlushRange msl_snd_DCFlushRange
#define DCInvalidateRange msl_snd_DCInvalidateRange
void msl_snd_DCFlushRange(void* addr, u32 n);
void msl_snd_DCInvalidateRange(void* addr, u32 n);
#ifndef ASSERTLINE
#define ASSERTLINE(line, cond) ((void) 0)
#define ASSERTMSGLINE(line, cond, msg) ((void) 0)
#define ASSERTMSG1LINE(line, cond, msg, a) ((void) 0)
#define ASSERTMSG2LINE(line, cond, msg, a, b) ((void) 0)
#endif
#endif
