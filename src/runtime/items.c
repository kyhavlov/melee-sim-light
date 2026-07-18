#include "ft/fighter.h"
#include "ft/ft_0892.h"
#include "ft/ftlib.h"
#include "it/inlines.h"
#include "it/it_26B1.h"
#include "it/it_2725.h"
#include "it/it_279C.h"
#include "it/it_3F14.h"
#include "it/items/itbombhei.h"
#include "it/items/itdosei.h"
#include "it/items/itfoxblaster.h"
#include "it/items/itfoxillusion.h"
#include "it/items/itfoxlaser.h"
#include "it/items/itheiho.h"
#include "it/items/itpeachexplode.h"
#include "it/items/itpeachparasol.h"
#include "it/items/itpeachtoad.h"
#include "it/items/itpeachtoadspore.h"
#include "it/items/itpeachturnip.h"
#include "it/items/itseakchain.h"
#include "it/items/itseakneedleheld.h"
#include "it/items/itseakneedlethrown.h"
#include "it/items/itseakvanish.h"
#include "it/items/itsword.h"
#include "it/items/itzeldadinfire.h"
#include "it/items/itzeldadinfireexplode.h"
#include "it/itzako.h"
#include "pl/plattack.h"
#include "pl/plbonuslib.h"
#include "pl/plstale.h"
#include "pl/pltrick.h"

// Source registry projection for the admitted character articles. Indices are
// ItemKind - It_Kind_Kuriboh, matching
// refs/melee/src/melee/it/item.c::Item_80267978 and the full tables in
// refs/melee/src/melee/it/it_279C.c.
struct sdata_ItemGXLink it_803F2F28[118] = {
    // Rendering is intentionally absent in the headless runtime. The source
    // values are it_8026EECC for laser/blaster and it_8029CD18 for illusion.
    [It_Kind_Fox_Laser - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Falco_Laser - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Fox_Illusion - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Falco_Phantasm - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Fox_Blaster - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Falco_Blaster - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Seak_NeedleThrow - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Seak_NeedleHeld - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Seak_Vanish - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Seak_Chain - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Peach_Explode - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Peach_Turnip - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Peach_Parasol - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Peach_Toad - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Peach_ToadSpore - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Zelda_DinFire - It_Kind_Kuriboh] = { NULL },
    [It_Kind_Zelda_DinFire_Explode - It_Kind_Kuriboh] = { NULL },
};

struct ItemLogicTable it_803F3100[118] = {
    [It_Kind_Fox_Laser - It_Kind_Kuriboh] = {
        it_803F67D0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itFoxLaser_Logic94_Clanked,
        NULL,
        NULL,
        itFoxLaser_Logic94_Reflected,
        itFoxLaser_Logic94_Clanked,
        itFoxLaser_Logic94_Absorbed,
        itFoxLaser_Logic94_ShieldBounced,
        itFoxLaser_Logic94_HitShield,
        itFoxLaser_Logic94_EvtUnk,
    },
    [It_Kind_Falco_Laser - It_Kind_Kuriboh] = {
        it_803F67D0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itFoxLaser_Logic94_Clanked,
        NULL,
        NULL,
        itFoxLaser_Logic94_Reflected,
        itFoxLaser_Logic94_Clanked,
        itFoxLaser_Logic94_Absorbed,
        itFoxLaser_Logic94_ShieldBounced,
        itFoxLaser_Logic94_HitShield,
        itFoxLaser_Logic94_EvtUnk,
    },
    [It_Kind_Fox_Illusion - It_Kind_Kuriboh] = {
        it_803F6818,
        NULL,
        itFoxIllusion_Logic14_Destroyed,
        NULL,
        NULL,
        NULL,
        itFoxIllusion_Logic14_DmgDealt,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        it_8029D948,
    },
    [It_Kind_Falco_Phantasm - It_Kind_Kuriboh] = {
        it_803F6818,
        NULL,
        itFoxIllusion_Logic14_Destroyed,
        NULL,
        NULL,
        NULL,
        itFoxIllusion_Logic14_DmgDealt,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        it_8029D948,
    },
    [It_Kind_Fox_Blaster - It_Kind_Kuriboh] = {
        it_803F6CA8,
        NULL,
        NULL,
        itFoxBlaster_Logic96_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itFoxBlaster_Logic96_EvtUnk,
    },
    [It_Kind_Falco_Blaster - It_Kind_Kuriboh] = {
        it_803F6CA8,
        NULL,
        NULL,
        itFoxBlaster_Logic96_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itFoxBlaster_Logic96_EvtUnk,
    },
    [It_Kind_Seak_NeedleThrow - It_Kind_Kuriboh] = {
        it_803F6F50,
        NULL,
        itSeakNeedleThrown_Logic109_Destroyed,
        NULL,
        NULL,
        NULL,
        it_2725_Logic109_DmgDealt,
        it_2725_Logic109_DmgReceived,
        NULL,
        it_2725_Logic109_Reflected,
        it_2725_Logic109_Clanked,
        NULL,
        it_2725_Logic109_ShieldBounced,
        it_2725_Logic109_HitShield,
        itSeakNeedleThrown_Logic109_EvtUnk,
    },
    [It_Kind_Seak_NeedleHeld - It_Kind_Kuriboh] = {
        it_803F70A8,
        NULL,
        NULL,
        itSeakNeedleHeld_Logic110_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itSeakNeedleHeld_Logic110_EvtUnk,
    },
    [It_Kind_Seak_Vanish - It_Kind_Kuriboh] = {
        it_803F70B8,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itSeakVanish_Logic42_DmgDealt,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        it_802B1DCC,
    },
    [It_Kind_Seak_Chain - It_Kind_Kuriboh] = {
        it_803F7438,
        NULL,
        NULL,
        it_2725_Logic54_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itSeakChain_Logic54_EvtUnk,
    },
    [It_Kind_Peach_Explode - It_Kind_Kuriboh] = {
        it_803F7488,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itPeachExplode_Logic55_DmgDealt,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itPeachExplode_Logic55_EvtUnk,
    },
    [It_Kind_Peach_Turnip - It_Kind_Kuriboh] = {
        it_803F74A8,
        NULL,
        itPeachTurnip_Logic56_Destroyed,
        itPeachTurnip_Logic56_PickedUp,
        itPeachTurnip_Logic56_Dropped,
        itPeachTurnip_Logic56_Thrown,
        itPeachTurnip_Logic56_DmgDealt,
        NULL,
        NULL,
        itPeachTurnip_Logic56_Reflected,
        itPeachTurnip_Logic56_Clanked,
        NULL,
        itPeachTurnip_Logic56_ShieldBounced,
        itPeachTurnip_Logic56_HitShield,
        itPeachTurnip_Logic56_EvtUnk,
    },
    [It_Kind_Peach_Parasol - It_Kind_Kuriboh] = {
        it_803F74F8,
        NULL,
        itPeachParasol_Logic60_Destroyed,
        itPeachParasol_Logic60_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itPeachParasol_Logic60_EvtUnk,
    },
    [It_Kind_Peach_Toad - It_Kind_Kuriboh] = {
        it_803F7528,
        NULL,
        itPeachToad_Logic91_Destroyed,
        itPeachToad_Logic91_PickedUp,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itPeachToad_Logic91_EvtUnk,
    },
    [It_Kind_Peach_ToadSpore - It_Kind_Kuriboh] = {
        it_803F7548,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itPeachToadSpore_Logic92_DmgDealt,
        NULL,
        NULL,
        itPeachToadSpore_Logic68_Reflected,
        itPeachToadSpore_Logic68_Clanked,
        itPeachToadSpore_Logic68_Absorbed,
        itPeachToadSpore_Logic68_ShieldBounced,
        itPeachToadSpore_Logic68_HitShield,
        itPeachToadSpore_Logic92_EvtUnk,
    },
    [It_Kind_Zelda_DinFire - It_Kind_Kuriboh] = {
        ItemStateTable_ZeldaDinFire,
        NULL,
        itZeldaDinFire_Logic65_Destroyed,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itZeldaDinFire_Logic65_Reflected,
        itZeldaDinFire_Logic65_Clanked,
        itZeldaDinFire_Logic65_Absorbed,
        NULL,
        NULL,
        itZeldaDinFire_Logic65_EvtUnk,
    },
    [It_Kind_Zelda_DinFire_Explode - It_Kind_Kuriboh] = {
        it_803F7740,
        NULL,
        itZeldaDinFireExplode_Logic66_Destroyed,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        itZeldaDinFireExplode_Logic66_Clanked,
        itZeldaDinFireExplode_Logic66_Absorbed,
        itZeldaDinFireExplode_Logic66_ShieldBounced,
        itZeldaDinFireExplode_Logic66_HitShield,
        itZeldaDinFireExplode_Logic66_EvtUnk,
    },
};

// The shared item-link constructor used by Sheik's chain copies this zero
// vector from DOL rodata. The decomp exposes the symbol but not its
// definition. data/raw/main.dol::0x803B8650
// (refs/melee/config/GALE01/symbols.txt)
Vec3 it_803B8650;

// The other item classes are outside the admitted items-off domain. Their
// registries remain present so the source item constructor retains its exact
// class dispatch without keeping unrelated item implementations alive.
// Source registry projection for common items reachable through Peach's
// SpecialLw pull table. The three gameplay entries are copied directly from
// refs/melee/src/melee/it/it_3F14.c::it_803F14C4; rendering stays headless.
struct sdata_ItemGXLink it_803F1418[43];
struct ItemLogicTable it_803F14C4[43] = {
    [It_Kind_BombHei] = {
        it_803F54D8,
        itBombhei_Logic6_Spawned,
        NULL,
        itBombhei_Logic6_PickedUp,
        it_3F14_Logic6_Dropped,
        it_3F14_Logic6_Thrown,
        it_3F14_Logic6_DmgDealt,
        it_3F14_Logic6_DmgReceived,
        it_3F14_Logic6_EnteredAir,
        itBombhei_Logic6_Reflected,
        itBombhei_Logic6_Clanked,
        NULL,
        it_3F14_Logic6_ShieldBounced,
        it_3F14_Logic6_HitShield,
        itBombhei_Logic6_EvtUnk,
    },
    [It_Kind_Dosei] = {
        it_803F55D0,
        itDosei_Logic7_Spawned,
        NULL,
        itDosei_Logic7_PickedUp,
        itDosei_Logic7_Dropped,
        itDosei_Logic7_Thrown,
        itDosei_Logic7_DmgDealt,
        itDosei_Logic7_DmgReceived,
        itDosei_Logic7_EnteredAir,
        itDosei_Logic7_Reflected,
        itDosei_Logic7_Clanked,
        NULL,
        itDosei_Logic7_ShieldBounced,
        itDosei_Logic7_HitShield,
        itDosei_Logic7_EvtUnk,
    },
    [It_Kind_Sword] = {
        it_803F5800,
        itSword_Logic12_Spawned,
        NULL,
        itSword_Logic12_PickedUp,
        itSword_Logic12_Dropped,
        itSword_Logic12_Thrown,
        itSword_Logic12_DmgDealt,
        NULL,
        itSword_Logic12_EnteredAir,
        itSword_Logic12_Reflected,
        itSword_Logic12_Clanked,
        NULL,
        itSword_Logic12_ShieldBounced,
        itSword_Logic12_HitShield,
        itSword_Logic12_EvtUnk,
    },
};
struct sdata_ItemGXLink it_803F2310[47];
struct ItemLogicTable it_803F23CC[47];
struct sdata_ItemGXLink it_803F4CA8[30];
struct ItemLogicTable it_803F4D20[30] = {
    [It_Kind_Heiho - It_Kind_Old_Kuri] = {
        it_803F83F0,
        it_802D8688,
        it_2725_Logic9_Destroyed,
        NULL,
        NULL,
        NULL,
        NULL,
        it_802D8EC8,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        it_802D9A0C,
    },
};
ItemKind it_803F2ED0[22];

char it_803F1ED8[] = "ItCo.dat";
char it_803F1EE4[] = "ItCo.usd";
char it_803F1EF0[] = "itPublicData";

#ifndef MSL_CORE_HOSTED
Article* it_804A0F60[30];
DamageLogEntry it_804A0E70[15];
ItemPickTable it_804A0E60;
ItemPickTable it_804A0E50;
RandomItemSpawner it_804A0E30;

it_804D6D40_t* it_804D6D40;
static Article* msl_character_articles[118];
Article** it_804D6D38 = msl_character_articles;
Article** it_804D6D30;
ItemCommonData* it_804D6D28;
Article** it_804D6D24;
it_804D6D20_t* it_804D6D20;
u8 it_804D6D1C[4];
u32 it_804D6D18;
u32 it_804D6D14;
u32 it_804D6D10;
s32 it_804D6D0C;
s32 it_804D6D08;
Fighter_804D653C_t* it_804D6D04;
s8 it_804D6D00;
#endif

// Exact reached source slice from it_279C.c. This records attack ownership and
// fighter/item interaction metadata; it is gameplay bookkeeping even in a
// headless port.
void it_8027B070(Item_GObj* item_gobj, Fighter_GObj* owner_gobj)
{
    Item* item = GET_ITEM(item_gobj);
    Fighter* fighter = GET_FIGHTER(owner_gobj);
    item->xD88_attackID = fighter->x2068_attackID;
    item->xD8C_attack_instance = fighter->x206C_attack_instance;
    item->xD90 = fighter->x2070;
    item->xD94 = fighter->x2074.x2074_vec;
    item->xD9C = fighter->x2074.x207C;
    item->xDA4_word = fighter->x2074.x2084;
    item->xDA8_short = fighter->x2074.x2088;
}

void it_8027B0C4(Item_GObj* item_gobj, SpawnItem* spawn)
{
    union Struct2070 attack;

    if (ftLib_80086960(spawn->x0_parent_gobj)) {
        if (ftLib_80086960(spawn->x4_parent_gobj2)) {
            it_8027B070(item_gobj, spawn->x4_parent_gobj2);
        } else {
            Item* owner = spawn->x4_parent_gobj2->user_data;
            Item* item = GET_ITEM(item_gobj);
            item->xD88_attackID = owner->xD88_attackID;
            item->xD8C_attack_instance = owner->xD8C_attack_instance;
            item->xD90 = owner->xD90;
            item->xD94 = owner->xD94;
            item->xD9C = owner->xD9C;
            item->xDA4_word = owner->xDA4_word;
            item->xDA8_short = owner->xDA8_short;
        }
    } else {
        Item* item = GET_ITEM(item_gobj);
        attack.x2070_int = 0;
        item->xD88_attackID = 1;
        item->xD8C_attack_instance = 0;
        item->xD90 = attack;
        ft_80089768(&item->xD94);
        item->xDA8_short = 0;
    }
}

void it_8027B1F4(Item_GObj* item_gobj)
{
    Item* item = GET_ITEM(item_gobj);
    union Struct2070 attack;

    attack.x2070_int = 0;
    item->xD88_attackID = 1;
    item->xD8C_attack_instance = 0;
    item->xD90 = attack;
    item->xDA8_short = 0;
    if (ftLib_80086960(item->owner)) {
        struct Struct2074* source = ft_800898A8((Fighter_GObj*) item->owner);
        item->xD94 = source->x2074_vec;
        item->xD9C = source->x207C;
        item->xDA4_word = source->x2084;
        return;
    }
    ft_80089768(&item->xD94);
}

void it_8027B288(Item_GObj* item_gobj, volatile u32 arg1)
{
    union Struct2070 attack;
    Item* item = item_gobj->user_data;
    attack.x2070_int = arg1;
    if (attack.x2073 == 0 || attack.x2073 != item->xD90.x2073) {
        item->xDA8_short = plAttack_80037B08();
    }
    item->xD90 = attack;
    if (ftLib_80086960(item->owner)) {
        struct Struct2074* source = ft_800898A8(item->owner);
        item->xD94 = source->x2074_vec;
        item->xD9C = source->x207C;
        item->xDA4_word = source->x2084;
        return;
    }
    ft_80089768(&item->xD94);
}

void it_8027B330(Item_GObj* item_gobj, u32 arg1)
{
    Item* item = GET_ITEM(item_gobj);
    if (arg1 == 1 || arg1 != item->xD88_attackID) {
        item->xD88_attackID = arg1;
        item->xD8C_attack_instance = plStale_IncrementAttackInstance();
    }
}

void it_8027B378(Fighter_GObj* fighter_gobj, Item_GObj* item_gobj, f32 damage)
{
    if (it_8026B6C8(item_gobj)) {
        u8 attack = ft_80089884(fighter_gobj)->x2073;
        s32 slot = ftLib_800874BC(fighter_gobj);
        u16 player = ftLib_80086BE0(fighter_gobj);
        pl_8003EB30(damage, player, slot, 6, 0, attack);
    }
}

void it_8027B408(Item_GObj* owner_gobj, Item_GObj* target_gobj, f32 damage)
{
    if (it_8026B6C8(target_gobj)) {
        Item* owner = GET_ITEM(owner_gobj);
        if (ftLib_80086960(owner->owner)) {
            s32 slot = ftLib_800874BC(owner->owner);
            u16 player = ftLib_80086BE0(owner->owner);
            pl_8003EB30(damage, player, slot, 6, 0, owner->xD90.x2073);
        }
    }
}

void it_8027B4A4(Fighter_GObj* fighter_gobj, Item_GObj* item_gobj)
{
    if (it_8026B6C8(item_gobj)) {
        void* attack = (void*) ft_800898A8(fighter_gobj);
        pl_800384DC(fighter_gobj, ft_80089884(fighter_gobj)->x2073, attack);
    }
}

void it_8027B508(Item_GObj* owner_gobj, Item_GObj* target_gobj)
{
    if (it_8026B6C8(target_gobj)) {
        Item* owner = GET_ITEM(owner_gobj);
        if (ftLib_80086960(owner->owner)) {
            pl_800384DC(owner->owner, owner->xD90.x2073, &owner->xD94);
        }
    }
}

void it_8027B564(Item_GObj* item_gobj)
{
    Item* item = GET_ITEM(item_gobj);
    if (ftLib_80086960(item->owner)) {
        union Struct2070 attack = item->xD90;
        pl_80037DF4(item->owner, &attack);
    }
}
