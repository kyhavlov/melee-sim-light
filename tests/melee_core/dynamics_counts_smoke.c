// ftDynamics::x10 from a synthetic archive (no game data): the per-slot
// tables of chain-joint counts ftCo_8009E4A8 and ftCo_8009E7B4 hand to
// ftCo_8009CB40 come through the native-DAT translation as the counts the
// file holds, not as NULL pointers. The archive is a minimal ftDataCaptain:
// zeroed special attributes and animation tables, and an ftDynamics whose
// x10 points at three slots (two count tables and one empty slot).
// refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CB40,ftCo_8009E4A8}
#include "ft/types.h"
#include "platform/native_dat.h"
#include "runtime/context.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DATA_FTDATA = 0x0000,
    DATA_ATTRS = 0x0080,
    DATA_ANIMS = 0x0200,
    DATA_ANIM_META = 0x2000,
    DATA_DYNAMICS = 0x2280,
    DATA_TABLE = 0x22A0,
    DATA_COUNTS_A = 0x22B0,
    DATA_COUNTS_B = 0x22C0,
    DATA_SIZE = 0x22D0,
};

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

int main(void)
{
    static const uint32_t relocs[] = {
        DATA_FTDATA + 0x04, DATA_FTDATA + 0x0C, DATA_FTDATA + 0x10,
        DATA_FTDATA + 0x2C, DATA_DYNAMICS + 0x10, DATA_TABLE + 0x00,
        DATA_TABLE + 0x04,
    };
    static const char symbol[] = "ftDataCaptain";
    const uint32_t nreloc = sizeof(relocs) / sizeof(relocs[0]);
    const uint32_t file_size =
        0x20 + DATA_SIZE + nreloc * 4 + 8 + (uint32_t) sizeof(symbol);
    uint8_t* file = calloc(1, file_size);
    uint8_t* data = file + 0x20;
    uint8_t* meta = data + DATA_SIZE;
    static MslNativeDatContext native_dat;
    HSD_Archive archive;
    ftData* ft;
    uint32_t i;

    if (file == NULL) {
        return 1;
    }
    msl_core_context_native_dat = &native_dat;
    put32(file + 0x00, file_size);
    put32(file + 0x04, DATA_SIZE);
    put32(file + 0x08, nreloc);
    put32(file + 0x0C, 1);
    put32(file + 0x10, 0);
    put32(data + DATA_FTDATA + 0x04, DATA_ATTRS);
    put32(data + DATA_FTDATA + 0x0C, DATA_ANIMS);
    put32(data + DATA_FTDATA + 0x10, DATA_ANIM_META);
    put32(data + DATA_FTDATA + 0x2C, DATA_DYNAMICS);
    put32(data + DATA_DYNAMICS + 0x00, 2);
    put32(data + DATA_DYNAMICS + 0x10, DATA_TABLE);
    put32(data + DATA_TABLE + 0x00, DATA_COUNTS_A);
    put32(data + DATA_TABLE + 0x04, DATA_COUNTS_B);
    // (slot 2: no relocation, a null table)
    put32(data + DATA_COUNTS_A + 0x00, 3);
    put32(data + DATA_COUNTS_A + 0x04, 1);
    put32(data + DATA_COUNTS_B + 0x00, 0);
    put32(data + DATA_COUNTS_B + 0x04, 5);
    for (i = 0; i < nreloc; ++i) {
        put32(meta + i * 4, relocs[i]);
    }
    put32(meta + nreloc * 4 + 0, DATA_FTDATA);
    put32(meta + nreloc * 4 + 4, 0);
    memcpy(meta + nreloc * 4 + 8, symbol, sizeof(symbol));

    if (msl_native_archive_parse(&archive, file, file_size) != 0) {
        fprintf(stderr, "dynamics-counts-smoke: parse failed\n");
        return 1;
    }
    ft = msl_native_archive_get_public(&archive, symbol);
    if (ft == NULL || ft->x2C == NULL || ft->x2C->dynamicsNum != 2 ||
        ft->x2C->x10 == NULL)
    {
        fprintf(stderr, "dynamics-counts-smoke: no dynamics table\n");
        return 1;
    }
    {
        FigaTree*** x10 = ft->x2C->x10;
        static const intptr_t want[2][2] = { { 3, 1 }, { 0, 5 } };
        uint32_t s;
        for (s = 0; s < 2; ++s) {
            if (x10[s] == NULL) {
                fprintf(stderr, "dynamics-counts-smoke: slot %u is NULL\n", s);
                return 1;
            }
            for (i = 0; i < 2; ++i) {
                // (as ftCo_8009CB40 reads it: the FigaTree* slot as a count)
                if ((s32) (ssize_t) x10[s][i] != want[s][i]) {
                    fprintf(stderr,
                            "dynamics-counts-smoke: slot %u count %u is %ld, "
                            "want %ld\n",
                            s, i, (long) (ssize_t) x10[s][i], (long) want[s][i]);
                    return 1;
                }
            }
        }
        if (x10[2] != NULL) {
            fprintf(stderr, "dynamics-counts-smoke: empty slot not NULL\n");
            return 1;
        }
    }
    puts("dynamics-counts-smoke: ok");
    return 0;
}
