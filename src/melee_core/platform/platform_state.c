#include "ft/types.h"

#include <baselib/controller.h>

// Headless process-owned state normally supplied by controller.c/db.c.
HSD_PadStatus HSD_PadMasterStatus[4];
HSD_PadStatus HSD_PadGameStatus[4];
HSD_PadStatus HSD_PadCopyStatus[4];
int DbLevel;

// The original linker map exposes only the first Fox costume-cache address as
// ft_80459B28, while CostumeListsForeachCharacter indexes four contiguous
// UnkCostumeStruct entries from it. Hosted ELF must reserve the full span
// explicitly instead of relying on adjacent DOL BSS symbols.
// refs/melee/src/melee/ft/ftdata.c::CostumeListsForeachCharacter
UnkCostumeStruct ft_80459B28[4];
UnkCostumeStruct ft_80459A98[6];
UnkCostumeStruct ft_80459D18[5];
UnkCostumeStruct ft_8045A1F8[5];
UnkCostumeStruct ft_8045A0F0[5];
UnkCostumeStruct ft_8045A168[5];
UnkCostumeStruct ft_8045A420[4];
HSD_Joint* ft_8045A1E0[6];

// These match-mode queries are false for a normal local human match.
int gm_8016B0FC(void) { return 0; }
int gm_8016B41C(void) { return 0; }
int gm_801A45E8(int mode)
{
    (void) mode;
    return 0;
}
