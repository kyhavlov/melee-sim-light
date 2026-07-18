#ifndef GALE01_08A698
#define GALE01_08A698

#include <placeholder.h>

#include "ftCommon/forward.h"

#include <dolphin/mtx.h>

typedef struct WaitStruct {
    // PlFx.dat authors these as two numeric words. The decomp's pointer union
    // is a PPC matching artifact; retaining it widens each record on native.
    // refs/melee/src/melee/ft/ftwaitanim.c::getAnimID
    int x;
    int y;
} WaitStruct;

/* 08A698 */ bool ftCo_8008A698(Fighter* fp);
/* 08A6D8 */ void ftCo_8008A6D8(Fighter_GObj* gobj, s32 anim_id);
/* 08A7A8 */ void ftCo_8008A7A8(Fighter_GObj* gobj, WaitStruct* arg1);
/* 3C54A8 */ extern char ftWaitAnim_803C54A8[];
/* 3C54C4 */ extern char ftWaitAnim_803C54C4[];

#endif
