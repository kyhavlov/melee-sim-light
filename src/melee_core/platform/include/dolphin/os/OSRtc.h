#ifndef _DOLPHIN_OSRTC_H_
#define _DOLPHIN_OSRTC_H_

// Hosted-width projection of
// refs/melee/extern/dolphin/include/dolphin/os/OSRtc.h. The SDK header spells
// console u32 fields as long, which widens under the x86-64 ABI.
#define OS_SOUND_MODE_MONO 0
#define OS_SOUND_MODE_STEREO 1
#define OS_VIDEO_MODE_NTSC 0
#define OS_VIDEO_MODE_MPAL 2

typedef struct SramControl {
    u8 sram[64];
    u32 offset;
    int enabled;
    int locked;
    int sync;
    void (*callback)(void);
} SramControl;

typedef struct OSSram {
    u16 checkSum;
    u16 checkSumInv;
    u32 ead0;
    u32 ead1;
    u32 counterBias;
    s8 displayOffsetH;
    u8 ntd;
    u8 language;
    u8 flags;
} OSSram;

typedef struct OSSramEx {
    u8 flashID[2][12];
    u32 wirelessKeyboardID;
    u16 wirelessPadID[4];
    u8 dvdErrorCode;
    u8 _padding0;
    u8 flashIDCheckSum[2];
    u8 _padding1[4];
} OSSramEx;

u32 OSGetSoundMode(void);
void OSSetSoundMode(u32 mode);
u32 OSGetVideoMode(void);
void OSSetVideoMode(u32 mode);
u8 OSGetLanguage(void);
void OSSetLanguage(u8 language);
u32 OSGetProgressiveMode(void);
void OSSetProgressiveMode(u32 mode);
u16 OSGetWirelessID(s32 chan);

#endif
