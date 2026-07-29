#ifndef MSL_CORE_RUNTIME_ITEM_PROJECTION_H
#define MSL_CORE_RUNTIME_ITEM_PROJECTION_H

#include <stdint.h>

enum {
    MSL_CORE_ITEM_MISC0 = 1 << 0,
    MSL_CORE_ITEM_MISC1 = 1 << 1,
    MSL_CORE_ITEM_MISC2 = 1 << 2,
    MSL_CORE_ITEM_MISC3 = 1 << 3,
    MSL_CORE_ITEM_MISC_ALL = 0xF,
};

enum {
    MSL_CORE_ITEM_KIND_MR_SATURN = 7,
    MSL_CORE_ITEM_KIND_FOX_LASER = 54,
    MSL_CORE_ITEM_KIND_FALCO_LASER = 55,
    MSL_CORE_ITEM_KIND_FOX_ILLUSION = 56,
    MSL_CORE_ITEM_KIND_FALCO_PHANTASM = 57,
    MSL_CORE_ITEM_KIND_FOX_BLASTER = 74,
    MSL_CORE_ITEM_KIND_FALCO_BLASTER = 75,
    MSL_CORE_ITEM_KIND_SHEIK_NEEDLE_THROWN = 79,
    MSL_CORE_ITEM_KIND_SHEIK_NEEDLE_HELD = 80,
    MSL_CORE_ITEM_KIND_SHEIK_VANISH = 85,
    MSL_CORE_ITEM_KIND_SHEIK_CHAIN = 97,
    MSL_CORE_ITEM_KIND_PEACH_EXPLODE = 98,
    MSL_CORE_ITEM_KIND_PEACH_TURNIP = 99,
    MSL_CORE_ITEM_KIND_PEACH_PARASOL = 103,
    MSL_CORE_ITEM_KIND_PEACH_TOAD = 104,
    MSL_CORE_ITEM_KIND_LUIGI_FIREBALL = 105,
    MSL_CORE_ITEM_KIND_MARIO_FIREBALL = 48,
    MSL_CORE_ITEM_KIND_DRMARIO_VITAMIN = 49,
    MSL_CORE_ITEM_KIND_MARIO_CAPE = 83,
    MSL_CORE_ITEM_KIND_DRMARIO_SHEET = 84,
    MSL_CORE_ITEM_KIND_PIKACHU_THUNDER = 81,
    MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_GROUND = 89,
    MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_AIR = 90,
    MSL_CORE_ITEM_KIND_SAMUS_BOMB = 93,
    MSL_CORE_ITEM_KIND_SAMUS_CHARGE_SHOT = 94,
    MSL_CORE_ITEM_KIND_SAMUS_MISSILE = 95,
    MSL_CORE_ITEM_KIND_SAMUS_GRAPPLE_BEAM = 96,
    MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE = 108,
    MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE_EXPLODE = 109,
    MSL_CORE_ITEM_KIND_PEACH_TOAD_SPORE = 111,
};

// SendItemInfo samples four fixed bytes from every article's xDD4 union even
// when that article owns a pointer, padding, or no storage at the sampled
// offset. Keep replay-forensic output raw, but exclude those source-proven
// non-gameplay lanes from deterministic production projections and output
// fingerprints. Numeric kinds are the stable replay protocol values from
// refs/melee/src/melee/it/forward.h.
// refs/slippi-ssbm-asm/Recording/SendItemInfo.s
// refs/melee/src/melee/it/{itCommonItems.h,itCharItems.h}
static inline uint8_t msl_core_item_gameplay_misc_mask(uint16_t kind,
                                                       uint8_t state)
{
    switch (kind) {
    case MSL_CORE_ITEM_KIND_MR_SATURN:
        // itDosei held/thrown states do not uniformly own xDE4.
        // refs/melee/src/melee/it/items/itdosei.c
        if (state == 1 || state == 4 || state == 5) {
            return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
        }
        break;
    case MSL_CORE_ITEM_KIND_FOX_BLASTER:
    case MSL_CORE_ITEM_KIND_FALCO_BLASTER:
        // xDE4[1..2] are presentation effect-object pointers.
        // refs/melee/src/melee/it/items/itfoxblaster.c
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_FOX_LASER:
    case MSL_CORE_ITEM_KIND_FALCO_LASER:
        // itFoxLaser_ItemVars ends at xDEC; xDEF is unowned residue.
        // refs/melee/src/melee/it/itCharItems.h::itFoxLaser_ItemVars
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1 |
               MSL_CORE_ITEM_MISC2;
    case MSL_CORE_ITEM_KIND_FOX_ILLUSION:
    case MSL_CORE_ITEM_KIND_FALCO_PHANTASM:
        // The sampled lanes are model pointers or unowned residue.
        // refs/melee/src/melee/it/items/itfoxillusion.c
        return 0;
    case MSL_CORE_ITEM_KIND_SHEIK_NEEDLE_HELD:
        // The declared payload contains only its fighter-owner pointer.
        // refs/melee/src/melee/it/items/itseakneedleheld.c
        return 0;
    case MSL_CORE_ITEM_KIND_SHEIK_NEEDLE_THROWN:
        // xDD4/xDD8 are not initialized; xDE4 is gameplay history.
        // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_802AFD8C
        return MSL_CORE_ITEM_MISC2 | MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_SHEIK_VANISH:
        // This article declares no item-variable payload.
        // refs/melee/src/melee/it/items/itseakvanish.c
        return 0;
    case MSL_CORE_ITEM_KIND_SHEIK_CHAIN:
        // The first samples are ItemLink pointers; x14/x18 are scalars.
        // refs/melee/src/melee/it/itCharItems.h::itSeakChain_ItemVars
        return MSL_CORE_ITEM_MISC2 | MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_PEACH_EXPLODE:
    case MSL_CORE_ITEM_KIND_PEACH_PARASOL:
    case MSL_CORE_ITEM_KIND_PEACH_TOAD:
    case MSL_CORE_ITEM_KIND_PEACH_TOAD_SPORE:
        // These articles declare no item-variable payload.
        // refs/melee/src/melee/it/items/{itpeachexplode.c,
        //   itpeachparasol.c,itpeachtoad.c,itpeachtoadspore.c}
        return 0;
    case MSL_CORE_ITEM_KIND_PEACH_TURNIP:
        // Only xDDB is uniformly gameplay-owned for all turnip variants.
        // refs/melee/src/melee/it/items/itpeachturnip.c::it_802BD4AC
        return MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_LUIGI_FIREBALL:
        // The fireball declares no item-variable payload; every sampled
        // lane is fixed-pool residue.
        // refs/melee/src/melee/it/items/itluigifireball.c
        return 0;
    case MSL_CORE_ITEM_KIND_MARIO_FIREBALL:
    case MSL_CORE_ITEM_KIND_DRMARIO_VITAMIN:
    case MSL_CORE_ITEM_KIND_MARIO_CAPE:
    case MSL_CORE_ITEM_KIND_DRMARIO_SHEET:
        // The Mario-family articles declare no Slippi-visible item
        // variables; sampled misc lanes are fixed-pool residue.
        // refs/melee/src/melee/it/items/{itmariofireball.c,
        // itdrmariopill.c,itmariocape.c}
        return 0;
    case MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_GROUND:
        // xDD7 samples the crawl angle (constructor-written via
        // it_802B3554) and xDEB/xDEF the constructor's spawn position;
        // xDDB is a byte of the owner pointer.
        // refs/melee/src/melee/it/itCharItems.h::itPikachutJoltGround_ItemVars
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC2 |
               MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_AIR:
        // xDD4/xDD8 are owner/sibling GObj pointers; xDE8 is the launch
        // velocity vector written by it_802B3F88 on every air entry.
        // refs/melee/src/melee/it/itCharItems.h::itPikachutJoltAir_ItemVars
        return MSL_CORE_ITEM_MISC2 | MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_SAMUS_BOMB:
        // The morph-launch bool at xDD4 is first written by the explosion
        // event, not the constructor, so xDD7 carries fixed-pool residue for
        // most of the bomb's life; xDDB is a byte of the owner pointer.
        // refs/melee/src/melee/it/items/itsamusbomb.c
        return 0;
    case MSL_CORE_ITEM_KIND_SAMUS_CHARGE_SHOT:
        // xDD4 is declared padding. xDD8 (launch angle) and xDEC (charge
        // level) are first written when the shot fires -- state 0 is the
        // held charge, whose lanes still carry fixed-pool residue. xDE8
        // (launch state) is initialized by the constructor.
        // refs/melee/src/melee/it/items/itsamuschargeshot.c::it_802B55C8
        if (state == 0) {
            return MSL_CORE_ITEM_MISC2;
        }
        return MSL_CORE_ITEM_MISC1 | MSL_CORE_ITEM_MISC2 |
               MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_SAMUS_MISSILE:
        // xDD7 samples is_smash_missile and xDDB the owner move-id scalar;
        // xDEB/xDEF are bytes of GObj pointers.
        // refs/melee/src/melee/it/itCharItems.h::itSamusMissile_ItemVars
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_SAMUS_GRAPPLE_BEAM:
        // The declared payload is link/object pointers and three bytes that
        // end before the xDEB/xDEF samples.
        // refs/melee/src/melee/it/itCharItems.h::itSamusGrapple_ItemVars
        return 0;
    case MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE:
        // Its first source word is explicitly uninitialized padding.
        // refs/melee/src/melee/it/items/itzeldadinfire.c::it_802C1590
        return MSL_CORE_ITEM_MISC1 | MSL_CORE_ITEM_MISC2 |
               MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE_EXPLODE:
        // The declared payload ends before the xDEB/xDEF samples.
        // refs/melee/src/melee/it/itCharItems.h::itZeldaDinFireExplode_ItemVars
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
    }
    return MSL_CORE_ITEM_MISC_ALL;
}

#endif
