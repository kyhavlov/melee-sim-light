// Type roots for generating the initialization-only native DAT translation
// descriptions.  The declarations are never linked into the runtime; DWARF
// from PPC32 and x86-64 compilations supplies the source and destination
// layouts from the same decomp headers.
//
// Source: refs/melee/src/sysdolphin/baselib/archive.c::HSD_ArchiveParse and
// the public-root consumers named below.

#include "ef/types.h"
#include "ft/chara/ftFox/types.h"
#include "ft/chara/ftMars/types.h"
#include "ft/types.h"
#include "ft/fighter.h"
#include "gr/types.h"
#include "it/it_3F14.h"
#include "it/itCharItems.h"
#include "lb/lbanim.h"
#include "mp/types.h"
#include "pl/types.h"
#include "sc/types.h"
#include "sfx/crowdsfx.h"

#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/aobj.h>
#include <baselib/cobj.h>
#include <baselib/dobj.h>
#include <baselib/fog.h>
#include <baselib/mobj.h>
#include <baselib/pobj.h>
#include <baselib/psstructs.h>
#include <baselib/robj.h>
#include <baselib/spline.h>
#include <baselib/tobj.h>
#include <baselib/wobj.h>

typedef int* MslDatIntPointer;
typedef float MslDatFloat;
typedef float MslDatFloat5[5];
typedef uint8_t MslDatByte;
typedef Vec2* MslDatVec2Pointer;
typedef FighterPartsTable* MslDatFighterPartsPointer;
typedef struct Fighter_804D6540_t* MslDatFighter6540Pointer;
// Anonymous yakumono_param owners in the two imported stage translation units.
// refs/melee/src/melee/gr/{grbattle.c,grpstadium.c}
typedef struct MslDatBattlefieldParams {
    int unk0;
    int unk4;
} MslDatBattlefieldParams;
typedef struct MslDatPokemonStadiumParams {
    int x0;
    int x4;
    int x8;
    int xC;
    int x10;
    int x14;
    int x18;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t x20;
    uint32_t x24;
    uint32_t x28;
    uint32_t x2C;
    uint32_t x30;
    uint32_t x34;
    uint32_t x38;
    uint32_t x3C;
    uint32_t x40;
    uint32_t x44;
    int16_t x48;
    int16_t x4A;
    int16_t x4C;
    int16_t x4E;
    int16_t x50;
} MslDatPokemonStadiumParams;
typedef struct MslDatFountainParams {
    float x0;
    int x4;
    float values[19];
} MslDatFountainParams;
typedef struct MslDatYoshisStoryParams {
    float timer_min;
    float timer_rand;
    float spawnmany_rarity;
    float vpos[6];
} MslDatYoshisStoryParams;
typedef struct MslDatDreamLandParams {
    int16_t x0;
    int16_t x2;
    int16_t x4;
    int16_t x6;
    int x8;
    int xC;
    float values[9];
} MslDatDreamLandParams;
typedef struct MslDatStageItemEntry {
    int kind;
    Article* article;
} MslDatStageItemEntry;
// Concrete authority for GrSt.dat's otherwise-void Article.x4. The first
// source word is a relocated damage-threshold pointer, followed by six f32s.
// refs/melee/src/melee/it/items/itheiho.c
typedef struct MslDatHeihoAttrs {
    int* damage_threshold;
    float values[6];
} MslDatHeihoAttrs;
// Fox uses x48_items[0..2]; Falco shares those owners but stores Phantasm at
// x48_items[3]. Both are direct consumers in their source OnLoad callbacks.
// refs/melee/src/melee/ft/chara/{ftFox/ftFx_Init.c,ftFalco/ftFc_Init.c}
typedef Article* MslDatSpaceAnimalArticles[4];
typedef MslDatSpaceAnimalArticles* MslDatArticleList;
typedef uint8_t MslDatAnimBytePair[2];

// Keeping an address of each root forces GCC to emit its complete reachable
// type graph even when debug-type elimination is enabled by the toolchain.
void* msl_native_dat_type_roots[] = {
    (HSD_PSCmdList*) 0,
    (UnkStageDat*) 0,
    (MapCollData*) 0,
    (UnkStage6B0*) 0,
    (DynamicModelDesc*) 0,
    (HSD_Joint*) 0,
    (HSD_Spline*) 0,
    (HSD_MatAnimJoint*) 0,
    (EF_EffectDesc*) 0,
    (ftData*) 0,
    (FigaTree*) 0,
    (it_804D6D20_t*) 0,
    (ItemCommonData*) 0,
    (it_804D6D40_t*) 0,
    (Fighter_804D653C_t*) 0,
    (pl_804D6470_t*) 0,
    (ftCommonData*) 0,
    (MslDatIntPointer*) 0,
    (MslDatFloat*) 0,
    (MslDatFloat5*) 0,
    (MslDatByte*) 0,
    (MslDatVec2Pointer*) 0,
    (MslDatFighterPartsPointer*) 0,
    (MslDatFighter6540Pointer*) 0,
    (MslDatBattlefieldParams*) 0,
    (MslDatPokemonStadiumParams*) 0,
    (MslDatFountainParams*) 0,
    (MslDatYoshisStoryParams*) 0,
    (MslDatDreamLandParams*) 0,
    (MslDatStageItemEntry*) 0,
    (MslDatHeihoAttrs*) 0,
    (MslDatArticleList*) 0,
    (MslDatSpaceAnimalArticles*) 0,
    (struct Fighter_WaitAnimData*) 0,
    (MslDatAnimBytePair*) 0,
    (ftFox_DatAttrs*) 0,
    (MarsAttributes*) 0,
    (FoxLaserAttr*) 0,
    (FoxBlasterAttr*) 0,
    (FoxIllusionAttr*) 0,
    (struct Fighter_804D6518_t*) 0,
    (struct Fighter_804D651C_t*) 0,
    (struct Fighter_804D6520_t*) 0,
    (struct Fighter_804D6524_t*) 0,
    (struct Fighter_804D6528_t*) 0,
    (CrowdConfig*) 0,
    (struct Fighter_804D64FC_t*) 0,
    (struct Fighter_804D6534_t*) 0,
};
