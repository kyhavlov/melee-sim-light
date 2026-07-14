#include "ft/types.h"

#include <baselib/controller.h>

// Headless process-owned state normally supplied by controller.c/db.c.
HSD_PadStatus HSD_PadMasterStatus[4];
HSD_PadStatus HSD_PadGameStatus[4];
HSD_PadStatus HSD_PadCopyStatus[4];
int DbLevel;

// The original costume cache is linker-owned BSS. Phase 1 only requests
// Fox's neutral costume, so the single source-declared entry is sufficient.
UnkCostumeStruct ft_80459B28;

// These match-mode queries are false for a normal local human match.
int gm_8016B0FC(void) { return 0; }
int gm_8016B41C(void) { return 0; }
int gm_801A45E8(int mode)
{
    (void) mode;
    return 0;
}

// Phase 1 has no stock/death boundary. Keep the crowd-warning test inactive.
float Stage_CalcUnkCamY(void) { return -1000000.0F; }
float Stage_CalcUnkCamYBounds(void) { return -1000000.0F; }
