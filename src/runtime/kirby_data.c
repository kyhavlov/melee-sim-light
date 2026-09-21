// Kirby's small-data constants. The decomp declares these as externs whose
// values live in GALE01's .sdata/.sdata2; they are read back from the
// retail DOL at the listed addresses.
// refs/melee/src/melee/ft/chara/ftKirby/{ftkirby.c,ftkirbyspecialmars.c,ftkirbyspecialfox.c}
#include <platform.h>

// 0x804D9570 / 0x804D9574: blend and speed for the Marth/Roy copy
// Fighter_ChangeMotionState calls in ftkirbyspecialmars.c.
f32 ftKb_Init_804D9570 = 0.0F;
f32 ftKb_Init_804D9574 = 1.0F;
// 0x804D3DB8 / 0x804D3DC0: facing-indexed sound ids for the Fox and Falco
// copy blaster shots (ftkirbyspecialfox.c::ftKb_SpecialNFx_800FDF30).
u32 ftKb_Init_804D3DB8[2] = { 110103, 110106 };
u32 ftKb_Init_804D3DC0[2] = { 100099, 100102 };
// 0x804D3DAC: assertion file-name payload; only reachable through __assert.
char ftKb_Init_804D3DAC[2] = "0";
