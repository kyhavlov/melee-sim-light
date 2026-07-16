#include <stddef.h>

#include "lb/lbaudio_ax.h"

// The renderer/audio device is excluded headlessly, but these immutable SSM
// tables are gameplay-relevant: ft_PlaySFX uses the translated common-sound
// id to decide whether to consume the shared HSD random stream for hit-sound
// pitch variation.
// refs/melee/src/melee/lb/{lbaudio_ax.c,lbaudio_ax.static.h}
// refs/melee/src/melee/ft/ft_0877.c::ft_PlaySFX
static const int msl_ssm_ranges[0x38][2] = {
    { 0, 0x20F },         { 0x2710, 0x2750 },   { 0x4E20, 0x4E25 },
    { 0x7530, 0x7546 },   { 0x9C40, 0x9C4A },   { 0xC350, 0xC356 },
    { 0xEA60, 0xEAB4 },   { 0x11170, 0x111DF }, { 0x13880, 0x138C2 },
    { 0x15F90, 0x15FF5 }, { 0x186A0, 0x1871C }, { 0x1ADB0, 0x1AE30 },
    { 0x1D4C0, 0x1D504 }, { 0x1FBD0, 0x1FCC8 }, { 0x222E0, 0x22379 },
    { 0x249F0, 0x24A3F }, { 0x27100, 0x2716C }, { 0x29810, 0x29877 },
    { 0x2BF20, 0x2BF7D }, { 0x2E630, 0x2E6B4 }, { 0x30D40, 0x30DD3 },
    { 0x33450, 0x334B5 }, { 0x35B60, 0x35BBA }, { 0x38270, 0x382D0 },
    { 0x3A980, 0x3A9E6 }, { 0x3D090, 0x3D0DE }, { 0x3F7A0, 0x3F7EA },
    { 0x41EB0, 0x41F77 }, { 0x445C0, 0x44623 }, { 0x46CD0, 0x46D17 },
    { 0x493E0, 0x4943A }, { 0x4BAF0, 0x4BB73 }, { 0x4E200, 0x4E21D },
    { 0x50910, 0x509ED }, { 0x53020, 0x53027 }, { 0x55730, 0x55749 },
    { 0x57E40, 0x57E4A }, { 0x5A550, 0x5A551 }, { 0x5CC60, 0x5CC6A },
    { 0x5F370, 0x5F37D }, { 0x61A80, 0x61A8B }, { 0x64190, 0x64194 },
    { 0x668A0, 0x668A8 }, { 0x68FB0, 0x68FB9 }, { 0x6B6C0, 0x6B6D8 },
    { 0x6DDD0, 0x6DDD6 }, { 0x704E0, 0x704E4 }, { 0x72BF0, 0x72BF8 },
    { 0x75300, 0x75301 }, { 0x77A10, 0x77A16 }, { 0x7A120, 0x7A12F },
    { 0x7C830, 0x7C865 }, { 0x7EF40, 0x7EF41 }, { 0x81650, 0x81654 },
    { 0x83D60, 0x83D60 }, { 0x83D60, 0x83D60 },
};

static const signed char msl_ssm_variant_threshold[0x38] = {
    0, 0, 0, 0, 0, 0, 4, 7, 7, 6, 6, 4, 0, 9, 1, 8, 7, 2, 1,
    7, 1, 6, 1, 1, 7, 1, 6, 2, 1, 6, 7, 6, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int lbAudioAx_800230C8(int index, int* low, int* high)
{
    if (index < 0 || index >= 0x37) {
        return 1;
    }
    if (low != NULL) {
        *low = msl_ssm_ranges[index][0];
    }
    if (high != NULL) {
        *high = msl_ssm_ranges[index][1];
    }
    return 0;
}

int lbAudioAx_80023130(int sfx_id)
{
    int index;

    if (sfx_id >= 0 && sfx_id < 0x83D60) {
        for (index = 0; index < 0x37; ++index) {
            if (msl_ssm_ranges[index][0] <= sfx_id &&
                sfx_id <= msl_ssm_ranges[index][1])
            {
                return index;
            }
        }
    }
    return 0x37;
}

int lbAudioAx_80023220(int index)
{
    if (index >= 0 && index < 0x37) {
        return msl_ssm_variant_threshold[index];
    }
    return 0;
}

int lbAudioAx_800233EC(int sfx_id)
{
    // Supported local matches keep the optional bank-33 language/remap table
    // inactive, so the source function returns its input unchanged. Common
    // hit sounds (SSM 0) never enter either remap table in any case.
    // refs/melee/src/melee/lb/lbaudio_ax.c::lbAudioAx_800233EC
    return sfx_id;
}
