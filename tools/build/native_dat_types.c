// Type roots for generating the initialization-only native DAT translation
// descriptions.  The declarations are never linked into the runtime; DWARF
// from PPC32 and x86-64 compilations supplies the source and destination
// layouts from the same decomp headers.
//
// Source: refs/melee/src/sysdolphin/baselib/archive.c::HSD_ArchiveParse and
// the public-root consumers named below.

#include "ef/types.h"
#include "ft/chara/ftCaptain/types.h"
#include "ft/chara/ftFox/types.h"
#include "ft/chara/ftDrMario/types.h"
#include "ft/chara/ftLuigi/types.h"
#include "ft/chara/ftMario/types.h"
#include "ft/chara/ftMars/types.h"
#include "ft/chara/ftDonkey/types.h"
#include "ft/chara/ftPeach/types.h"
#include "ft/chara/ftPikachu/types.h"
#include "ft/chara/ftPopo/types.h"
#include "ft/chara/ftSamus/types.h"
#include "ft/chara/ftPurin/types.h"
#include "ft/chara/ftSeak/types.h"
#include "ft/chara/ftZelda/types.h"
#include "ft/chara/ftYoshi/types.h"
#include "ft/chara/ftKoopa/types.h"
#include "ft/fighter.h"
#include "ft/types.h"
#include "gr/types.h"
#include "it/it_3F14.h"
#include "it/itCharItems.h"
#include "it/itCommonItems.h"
#include "it/items/itdosei.h"
#include "it/items/ityoshistar.h"
#include "it/items/itkoopaflame.h"
#include "it/items/itseakneedlethrown.h"
#include "it/items/types.h"
#include "lb/lbanim.h"
#include "lb/types.h"
#include "mp/types.h"
#include "pl/types.h"
#include "sc/types.h"
#include "sfx/crowdsfx.h"

#include <baselib/aobj.h>
#include <baselib/cobj.h>
#include <baselib/dobj.h>
#include <baselib/fog.h>
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/mobj.h>
#include <baselib/pobj.h>
#include <baselib/psstructs.h>
#include <baselib/robj.h>
#include <baselib/spline.h>
#include <baselib/tobj.h>
#include <baselib/wobj.h>

typedef int* MslDatIntPointer;
typedef struct MslDatItemThrowAttr {
    float velocity_mul;
    float angle;
    float smash_scale;
} MslDatItemThrowAttrs[26];
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
// PlSk.dat's ftData.x48_items is a mixed six-word table. The first four
// entries are character articles; ftSk_SpecialS_80110610 consumes entries
// four and five as pointers to Chain animation-joint pointer tables.
// refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c
typedef struct MslDatSheikArticles {
    Article* articles[4];
    HSD_Joint** chain_joint_tables[2];
} MslDatSheikArticles;
typedef Article* MslDatPeachArticles[5];
typedef Article* MslDatZeldaArticles[2];
// PlLg.dat's ftData.x48_items holds the single fireball article registered by
// ftLg_Init_OnLoad and spawned by ftLg_SpecialN via it_802C01AC.
// refs/melee/src/melee/ft/chara/ftLuigi/ftLg_Init.c
// refs/melee/src/melee/it/items/itluigifireball.c
typedef Article* MslDatLuigiArticles[1];
// PlMr/PlDr ftData.x48_items: fireball, megavitamin, cape, sheet.
// refs/melee/src/melee/ft/chara/{ftMario/ftMr_Init.c,
// ftDrMario/ftDr_Init.c}
typedef Article* MslDatMarioArticles[4];
// PlSs.dat's ftData.x48_items is a five-slot table: the bomb, charge shot,
// missile, and grapple-beam articles registered by ftSs_Init_OnLoad, then the
// throw grapple-beam accessory graph consumed by
// ftSs_Init_CreateThrowGrappleBeam (a root joint, the per-throw anim joints
// for ThrowF/B/Hi/Lw, and the shared anim/matanim joints).
// refs/melee/src/melee/ft/chara/ftSamus/ftSs_Init.c
typedef struct MslDatSamusThrowBeam {
    HSD_Joint* joint;
    HSD_AnimJoint* (*throw_anim_joints)[4];
    HSD_AnimJoint* anim_joint;
    HSD_MatAnimJoint* matanim_joint;
} MslDatSamusThrowBeam;
typedef struct MslDatSamusArticles {
    Article* articles[4];
    MslDatSamusThrowBeam* throw_beam;
} MslDatSamusArticles;
// PlPp.dat and PlNn.dat each own one three-slot table: the ice shot,
// blizzard, and belay-string articles ftPp_Init_OnLoad registers as item
// kinds 106/107/113.
// refs/melee/src/melee/ft/chara/ftPopo/ftPp_Init.c
typedef Article* MslDatIceClimberArticles[3];
// PlPk.dat's ftData.x48_items leads with the three articles
// ftPk_Init_OnLoad registers under the item kinds stored in its attribute
// block (xDC thunder 81, x14 ground jolt 89, x18 air jolt 90); the later
// slots carry presentation graphs no ported code reaches.
// refs/melee/src/melee/ft/chara/ftPikachu/ftPk_Init.c
typedef Article* MslDatPikachuArticles[3];
// ftYs_Init_OnLoad registers three articles; ftYs_SpecialN_8012CDD4 returns
// the fourth slot's joint graph for the captured fighter's egg accessory.
typedef struct MslDatYoshiArticles {
    Article* articles[3];
    HSD_Joint* capture_egg;
} MslDatYoshiArticles;
typedef Article* MslDatKoopaArticles[1];
// ItCo.dat's public x4 table owns the 43 common-item Article graphs. Peach's
// SpecialLw can reach BombHei, Dosei, and Sword even when stage items are off.
// refs/melee/src/melee/it/iteffect.c::it_802787B4
// refs/melee/src/melee/ft/chara/ftPeach/ftPe_SpecialLw.c
typedef Article* MslDatCommonItemArticles[43];
// Peach's turnip attributes end in a source-owned eight-entry inline table;
// the decomp declaration is flexible because Article.x4 is otherwise void.
// refs/melee/src/melee/it/items/itpeachturnip.c::it_802BD32C
typedef struct MslDatPeachTurnipAttrs {
    float lifetime;
    int count;
    struct {
        int odds;
        int damage;
    } faces[8];
} MslDatPeachTurnipAttrs;
// PlPr.dat's ftData.x48_items[1] points at a leading reserved word followed
// by the FtPartsDesc consumed by ftPr_Init_8013C360 for non-default hats.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_Init.c
typedef struct MslDatPurinCostumeParts {
    uint32_t reserved;
    FtPartsDesc desc;
} MslDatPurinCostumeParts;
typedef struct MslDatPurinAuxList {
    void* reserved;
    MslDatPurinCostumeParts* costume_parts;
} MslDatPurinAuxList;
typedef MslDatSpaceAnimalArticles* MslDatArticleList;
typedef uint8_t MslDatAnimBytePair[2];
// itzeldadinfire.c consumes the otherwise-void Article.x4 as twelve f32s.
// Its anonymous local declaration is the source authority for this DAT graph.
// refs/melee/src/melee/it/items/itzeldadinfire.c
typedef struct MslDatZeldaDinFireAttrs {
    float values[12];
} MslDatZeldaDinFireAttrs;

// Keeping an address of each root forces GCC to emit its complete reachable
// type graph even when debug-type elimination is enabled by the toolchain.
void* msl_native_dat_type_roots[] = {
    (union CmdUnion*) 0,
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
    (MslDatItemThrowAttrs*) 0,
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
    (MslDatSheikArticles*) 0,
    (MslDatPeachArticles*) 0,
    (MslDatZeldaArticles*) 0,
    (MslDatLuigiArticles*) 0,
    (MslDatMarioArticles*) 0,
    (ftMario_DatAttrs*) 0,
    (MslDatSamusArticles*) 0,
    (ftSs_DatAttrs*) 0,
    (itSamusBombAttributes*) 0,
    (itSamusChargeShot_Attributes*) 0,
    (itSamusMissileAttributes*) 0,
    (itSamusGrappleAttributes*) 0,
    (MslDatIceClimberArticles*) 0,
    (ftIceClimberAttributes*) 0,
    (itClimbersIceAttributes*) 0,
    (itClimbersBlizzardAttributes*) 0,
    (itClimbersStringAttributes*) 0,
    (ftDonkeyAttributes*) 0,
    (MslDatPikachuArticles*) 0,
    (MslDatYoshiArticles*) 0,
    (ftYoshiAttributes*) 0,
    (ftKoopaAttributes*) 0,
    (MslDatKoopaArticles*) 0,
    (itKoopaFlame_Attributes*) 0,
    (itYoshiEggThrowAttributes*) 0,
    (StarAttrs*) 0,
    (ftPikachuAttributes*) 0,
    (itPikachuthunderAttributes*) 0,
    (itPikachutJoltGroundAttributes*) 0,
    (MslDatCommonItemArticles*) 0,
    (itBombHeiAttributes*) 0,
    (itDoseiAttributes*) 0,
    (itSword_UnkArticle1*) 0,
    (MslDatPurinAuxList*) 0,
    (struct Fighter_WaitAnimData*) 0,
    (MslDatAnimBytePair*) 0,
    (ftFox_DatAttrs*) 0,
    (ftCaptain_DatAttrs*) 0,
    (MarsAttributes*) 0,
    (ftPe_DatAttrs*) 0,
    (ftPurinAttributes*) 0,
    (ftLuigiAttributes*) 0,
    (ftSeakAttributes*) 0,
    (ftZelda_DatAttrs*) 0,
    (FoxLaserAttr*) 0,
    (FoxBlasterAttr*) 0,
    (FoxIllusionAttr*) 0,
    (itSeakNeedleThrownAttributes*) 0,
    (itSeakChain_Attrs*) 0,
    (MslDatPeachTurnipAttrs*) 0,
    (itPeachToadSporeAttributes*) 0,
    (MslDatZeldaDinFireAttrs*) 0,
    (itZeldaDinFireExplodeAttributes*) 0,
    (itUnkAttributes*) 0,
    (struct Fighter_804D6518_t*) 0,
    (struct Fighter_804D651C_t*) 0,
    (struct Fighter_804D6520_t*) 0,
    (struct Fighter_804D6524_t*) 0,
    (struct Fighter_804D6528_t*) 0,
    (CrowdConfig*) 0,
    (struct Fighter_804D64FC_t*) 0,
    (struct Fighter_804D6534_t*) 0,
};
