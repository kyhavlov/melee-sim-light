/* The sound core's host side (src/sound/README.md): what the game's sound code reads from the disc is big-endian, and a few of its
 * reads and writes take a pair of u16 fields (an AX address's hi and lo halves) as one u32. On the console both are the same thing;
 * on this little-endian host they are not, so the decomp files (src/sound/decomp) call these at the places listed in LEDGER.md. */
#ifndef MSL_SOUND_HOST_H
#define MSL_SOUND_HOST_H

#include <stddef.h>
#include <stdint.h>

static inline uint32_t msl_snd_bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
}

static inline uint16_t msl_snd_bswap16(uint16_t x) { return (uint16_t) ((x >> 8) | (x << 8)); }

/* n big-endian u32 words in place */
static inline void msl_snd_be32(void* p, size_t n)
{
    uint32_t* w = (uint32_t*) p;
    for (size_t i = 0; i < n; i++) w[i] = msl_snd_bswap32(w[i]);
}

/* n big-endian u16 fields in place */
static inline void msl_snd_be16(void* p, size_t n)
{
    uint16_t* h = (uint16_t*) p;
    for (size_t i = 0; i < n; i++) h[i] = msl_snd_bswap16(h[i]);
}

#define MSL_SND_BE32(p, n) msl_snd_be32((void*) (p), (size_t) (n))

/* an AX address's two u16 halves (hi first, as the PPC stores a u32) read, written or added to as one u32 */
static inline uint32_t msl_snd_hilo_get(const void* p)
{
    const uint16_t* h = (const uint16_t*) p;
    return ((uint32_t) h[0] << 16) | h[1];
}
static inline void msl_snd_hilo_set(void* p, uint32_t v)
{
    uint16_t* h = (uint16_t*) p;
    h[0] = (uint16_t) (v >> 16);
    h[1] = (uint16_t) v;
}
#define MSL_SND_HILO_GET(p) msl_snd_hilo_get((const void*) (p))
#define MSL_SND_HILO_SET(p, v) msl_snd_hilo_set((void*) (p), (uint32_t) (v))
/* the u32 that, stored, holds these two u16 fields in this order (the PPC's 0xAAAABBBB written as one word) */
#define MSL_SND_U16PAIR(first, second) ((uint32_t) (uint16_t) (first) | ((uint32_t) (uint16_t) (second) << 16))
#define MSL_SND_HILO_ADD(p, v) msl_snd_hilo_set((void*) (p), msl_snd_hilo_get((const void*) (p)) + (uint32_t) (v))

/* An .ssm's sound entries as HSD_SynthSFXSampleLoadCallback reads them (from file offset 0x10): per sound a u32 voice count n and a u32
 * sample rate, then n blocks of 64 bytes of u16 fields (AXPBADDR, AXPBADPCM, AXPBADPCMLOOP, pad). */
static inline void msl_snd_be_ssm_entries(void* p, uint32_t count)
{
    uint8_t* b = (uint8_t*) p;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t* w = (uint32_t*) b;
        w[0] = msl_snd_bswap32(w[0]);
        w[1] = msl_snd_bswap32(w[1]);
        msl_snd_be16(b + 8, (size_t) w[0] * 32);
        b += 8 + (size_t) w[0] * 64;
    }
}

/* An .hps header as HSD_SynthPStreamHeaderCallback reads it (0x80 bytes): u32 words 0-3 (the tag, sample rate, channel count), then per
 * channel 14 words of u16 fields (AXPBADDR, AXPBADPCM) */
static inline void msl_snd_be_hps_header(void* p)
{
    msl_snd_be32(p, 4);
    msl_snd_be16((uint8_t*) p + 16, (0x80 - 16) / 2);
}

/* An .hps block header ("hako", 0x20 bytes): u32 words 0-2 (length, end, next block), then per channel an AXPBADPCMLOOP and pad (u16) */
static inline void msl_snd_be_hako_header(void* p)
{
    msl_snd_be32(p, 3);
    msl_snd_be16((uint8_t*) p + 12, (0x20 - 12) / 2);
}

#endif
