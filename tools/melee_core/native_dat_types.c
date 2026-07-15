// Type roots for generating the initialization-only native DAT translation
// descriptions.  The declarations are never linked into the runtime; DWARF
// from PPC32 and x86-64 compilations supplies the source and destination
// layouts from the same decomp headers.
//
// Source: refs/melee/src/sysdolphin/baselib/archive.c::HSD_ArchiveParse and
// the public-root consumers named below.

#include "ef/types.h"
#include "ft/chara/ftFox/types.h"
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
typedef ftFox_DatAttrs* MslDatFoxAttrsPointer;
typedef Article* MslDatFoxArticles[3];
typedef MslDatFoxArticles* MslDatArticleList;
// ftData_Table_Unk0[FTKIND_FOX].count in the hosted source registry.
// refs/melee/src/melee/ft/ftdata.c
typedef struct Fighter_WaitAnimData MslDatFoxWaitAnimArray[327];
typedef MslDatFoxWaitAnimArray* MslDatFoxWaitAnimList;
typedef uint8_t MslDatFoxAnimByteArray[327][2];
typedef MslDatFoxAnimByteArray* MslDatFoxAnimByteList;

// Keeping an address of each root forces GCC to emit its complete reachable
// type graph even when debug-type elimination is enabled by the toolchain.
void* msl_native_dat_type_roots[] = {
    (HSD_PSCmdList*) 0,
    (UnkStageDat*) 0,
    (MapCollData*) 0,
    (UnkStage6B0*) 0,
    (DynamicModelDesc*) 0,
    (HSD_Joint*) 0,
    (HSD_MatAnimJoint*) 0,
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
    (MslDatFoxAttrsPointer*) 0,
    (MslDatArticleList*) 0,
    (MslDatFoxWaitAnimList*) 0,
    (MslDatFoxAnimByteList*) 0,
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
