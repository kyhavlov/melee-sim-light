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
    MSL_ITEM_KIND_YOSHI_EGG_THROW = 86,
    MSL_ITEM_KIND_YOSHI_EGG_LAY = 87,
    MSL_ITEM_KIND_YOSHI_STAR = 88,
    MSL_CORE_ITEM_KIND_SHEIK_CHAIN = 97,
    MSL_CORE_ITEM_KIND_PEACH_EXPLODE = 98,
    MSL_CORE_ITEM_KIND_PEACH_TURNIP = 99,
    MSL_CORE_ITEM_KIND_PEACH_PARASOL = 103,
    MSL_CORE_ITEM_KIND_PEACH_TOAD = 104,
    MSL_CORE_ITEM_KIND_LUIGI_FIREBALL = 105,
    MSL_CORE_ITEM_KIND_ICECLIMBER_ICE = 106,
    MSL_CORE_ITEM_KIND_ICECLIMBER_BLIZZARD = 107,
    MSL_CORE_ITEM_KIND_ICECLIMBER_GUM_STRINGS = 113,
    MSL_CORE_ITEM_KIND_MARIO_FIREBALL = 48,
    MSL_CORE_ITEM_KIND_DRMARIO_VITAMIN = 49,
    MSL_CORE_ITEM_KIND_MARIO_CAPE = 83,
    MSL_CORE_ITEM_KIND_DRMARIO_SHEET = 84,
    MSL_CORE_ITEM_KIND_PIKACHU_THUNDER = 81,
    MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_GROUND = 89,
    MSL_CORE_ITEM_KIND_PIKACHU_TJOLT_AIR = 90,
    MSL_CORE_ITEM_KIND_NESS_PKFIRE = 66,
    MSL_CORE_ITEM_KIND_NESS_PKFIRE_PILLAR = 67,
    MSL_CORE_ITEM_KIND_NESS_PKFLASH = 68,
    MSL_CORE_ITEM_KIND_NESS_PKTHUNDER = 69,
    MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL1 = 70,
    MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL2 = 71,
    MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL3 = 72,
    MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL4 = 73,
    MSL_CORE_ITEM_KIND_NESS_PKFLASH_EXPLODE = 78,
    MSL_CORE_ITEM_KIND_NESS_BAT = 101,
    MSL_CORE_ITEM_KIND_NESS_YOYO = 102,
    MSL_CORE_ITEM_KIND_LINK_BOMB = 58,
    MSL_CORE_ITEM_KIND_CLINK_BOMB = 59,
    MSL_CORE_ITEM_KIND_LINK_BOOMERANG = 60,
    MSL_CORE_ITEM_KIND_CLINK_BOOMERANG = 61,
    MSL_CORE_ITEM_KIND_LINK_HOOKSHOT = 62,
    MSL_CORE_ITEM_KIND_CLINK_HOOKSHOT = 63,
    MSL_CORE_ITEM_KIND_LINK_ARROW = 64,
    MSL_CORE_ITEM_KIND_CLINK_ARROW = 65,
    MSL_CORE_ITEM_KIND_LINK_BOW = 76,
    MSL_CORE_ITEM_KIND_CLINK_BOW = 77,
    MSL_CORE_ITEM_KIND_CLINK_MILK = 123,
    MSL_CORE_ITEM_KIND_SAMUS_BOMB = 93,
    MSL_CORE_ITEM_KIND_SAMUS_CHARGE_SHOT = 94,
    MSL_CORE_ITEM_KIND_SAMUS_MISSILE = 95,
    MSL_CORE_ITEM_KIND_SAMUS_GRAPPLE_BEAM = 96,
    MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE = 108,
    MSL_CORE_ITEM_KIND_ZELDA_DIN_FIRE_EXPLODE = 109,
    MSL_CORE_ITEM_KIND_PEACH_TOAD_SPORE = 111,
    MSL_ITEM_KIND_GAMEWATCH_GREENHOUSE = 114,
    MSL_ITEM_KIND_GAMEWATCH_MANHOLE = 115,
    MSL_ITEM_KIND_GAMEWATCH_FIRE = 116,
    MSL_ITEM_KIND_GAMEWATCH_PARACHUTE = 117,
    MSL_ITEM_KIND_GAMEWATCH_TURTLE = 118,
    MSL_ITEM_KIND_GAMEWATCH_BREATH = 119,
    MSL_ITEM_KIND_GAMEWATCH_JUDGE = 120,
    MSL_ITEM_KIND_GAMEWATCH_PANIC = 121,
    MSL_ITEM_KIND_GAMEWATCH_CHEF = 122,
    MSL_ITEM_KIND_GAMEWATCH_RESCUE = 124,
    MSL_ITEM_KIND_MEWTWO_DISABLE = 110,
    MSL_ITEM_KIND_MEWTWO_SHADOW_BALL = 112,
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
    case MSL_ITEM_KIND_GAMEWATCH_GREENHOUSE:
    case MSL_ITEM_KIND_GAMEWATCH_MANHOLE:
    case MSL_ITEM_KIND_GAMEWATCH_FIRE:
    case MSL_ITEM_KIND_GAMEWATCH_PARACHUTE:
    case MSL_ITEM_KIND_GAMEWATCH_TURTLE:
    case MSL_ITEM_KIND_GAMEWATCH_BREATH:
    case MSL_ITEM_KIND_GAMEWATCH_JUDGE:
    case MSL_ITEM_KIND_GAMEWATCH_PANIC:
    case MSL_ITEM_KIND_GAMEWATCH_RESCUE:
        // it_8027CE64 owns the leading attribute pointer; Rescue adds an
        // owner pointer. Remaining sampled lanes are outside these payloads.
        return 0;
    case MSL_ITEM_KIND_GAMEWATCH_CHEF:
        // x0 is the shared attribute pointer; x4 is the trajectory index.
        return MSL_CORE_ITEM_MISC1;
    case MSL_ITEM_KIND_MEWTWO_DISABLE:
        // itMDisable_ItemVars contains only the owner pointer. The remaining
        // Slippi samples are outside that source payload.
        return 0;
    case MSL_ITEM_KIND_MEWTWO_SHADOW_BALL:
        // it_802C519C initializes x0 only for the forward-throw shot (state 9).
        // The held ball's launch angle x4.x is first written at release by
        // it_802C53F0; x14 and x18 track its mode and charge.
        // refs/melee/src/melee/it/items/itmewtwoshadowball.c
        if (state == 9) {
            return MSL_CORE_ITEM_MISC_ALL;
        }
        if (state == 0) {
            return MSL_CORE_ITEM_MISC2 | MSL_CORE_ITEM_MISC3;
        }
        return MSL_CORE_ITEM_MISC1 | MSL_CORE_ITEM_MISC2 |
               MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_MR_SATURN:
        // Every xDE4 write in itdosei.c is `= ip->pos`, a copy of the
        // directly compared item position lanes, and the writing Anim
        // callbacks skip during item hitlag, so a state entered mid-hitlag
        // samples stale bytes or pool residue (the held/thrown states never
        // write it at all). The y/z samples carry no independent gameplay
        // signal in any state.
        // refs/melee/src/melee/it/items/itdosei.c
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
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
    case MSL_ITEM_KIND_YOSHI_EGG_THROW:
    case MSL_ITEM_KIND_YOSHI_STAR:
        // These articles own no xDD4 payload.
        // refs/melee/src/melee/it/items/{ityoshieggthrow.c,ityoshistar.c}
        return 0;
    case MSL_ITEM_KIND_YOSHI_EGG_LAY:
        // Only xDD4 is assigned by it_802F2F34.
        // refs/melee/src/melee/it/itCommonItems.h::itYoshiEggLay_ItemVars
        return MSL_CORE_ITEM_MISC0;
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
    case MSL_CORE_ITEM_KIND_ICECLIMBER_ICE:
        // x0 is the spawner GObj pointer written at spawn (it_802C1590), so
        // xDD7 samples allocator identity. x4 is the live scale, written at
        // spawn from x60_scale and decayed by every bounce
        // (itClimbersice_UnkMotion0_Anim), so xDDB is gameplay-owned for the
        // article's whole life. The declared struct ends at xC, leaving
        // 0x17/0x1B past-member residue.
        // refs/melee/src/melee/it/itCharItems.h::itClimbersIce_ItemVars
        // refs/melee/src/melee/it/items/itclimbersice.c::it_802C1590
        return MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_ICECLIMBER_BLIZZARD:
        // x0 is the scale written at spawn (itClimbersBlizzard_Spawn), so
        // xDD7 is gameplay-owned for the article's whole life. The only
        // other member is the flag0 bit in xDD8's leading byte, so offset 7
        // samples an unwritten byte and 0x17/0x1B fall past the declared
        // members.
        // refs/melee/src/melee/it/itCharItems.h::itClimbersBlizzard_ItemVars
        // refs/melee/src/melee/it/items/itclimbersblizzard.c
        //   ::itClimbersBlizzard_Spawn
        return MSL_CORE_ITEM_MISC0;
    case MSL_CORE_ITEM_KIND_ICECLIMBER_GUM_STRINGS:
        // x0's only owner write is the constant 2.25f (it_802C3864) whose
        // sampled low byte is 0x00; the spawn constructor (it_802C27D4)
        // leaves it unwritten, so no live export ever samples a varying
        // owner-written value -- the Link-bomb direction-sign argument.
        // x4/x8 are the string ItemLink chain, xC the owner GObj, x14 the
        // tail joint, and the struct ends there, so every sampled lane is
        // pointer identity, residue, or a constant.
        // refs/melee/src/melee/it/itCharItems.h::itClimbersString_ItemVars
        // refs/melee/src/melee/it/items/itclimbersstring.c
        //   ::{it_802C27D4,it_802C3864}
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
    case MSL_CORE_ITEM_KIND_NESS_PKFIRE:
    case MSL_CORE_ITEM_KIND_NESS_PKFIRE_PILLAR:
        // Neither PK Fire article declares an item-variable payload; every
        // sampled misc lane is fixed-pool residue.
        // refs/melee/src/melee/it/items/{itnesspkfire.c,itnesspkfirepillar.c}
        return 0;
    case MSL_CORE_ITEM_KIND_NESS_PKFLASH:
        // Only xDD8 (the charge scale) is written by ported code. No owner
        // writes the leading xDD4 word at all -- neither the constructor
        // it_802AAA80 nor any motion callback -- so its sampled byte is
        // fixed-pool residue, and the struct ends at xDE4, leaving
        // 0x17/0x1B past-member residue too.
        // refs/melee/src/melee/it/itPKFlash.h::itPKFlush_ItemVars
        // refs/melee/src/melee/it/items/itnesspkflash.c
        return MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_NESS_PKFLASH_EXPLODE:
        // xDD4/xDD8 are the explosion timers; xDDC is the owner pointer and
        // nothing is declared past it.
        // refs/melee/src/melee/it/itPKFlash.h::itPKFlushExplode_ItemVars
        return MSL_CORE_ITEM_MISC0 | MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_NESS_PKTHUNDER:
        // The ball leads with the six trail GObj pointers (xDD4[0..5]), so
        // offsets 3/7/0x17 are all pointer bytes; 0x1B samples the first
        // recorded trail position's x.
        // refs/melee/src/melee/it/itPKThunder.h::itPKThunder_ItemVars
        return MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL1:
    case MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL2:
    case MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL3:
    case MSL_CORE_ITEM_KIND_NESS_PKTHUNDER_TRAIL4:
        // x0 is the ball Item_GObj*; x4 is the trail's gameplay index and
        // the struct ends at x8, so 0x17/0x1B are past-member residue.
        // refs/melee/src/melee/it/itCharItems.h::itNesspkthundertrail_ItemVars
        return MSL_CORE_ITEM_MISC1;
    case MSL_CORE_ITEM_KIND_NESS_BAT:
        // The bat declares only its owner GObj pointer.
        // refs/melee/src/melee/it/itCharItems.h::itNessbat_ItemVars
        return 0;
    case MSL_CORE_ITEM_KIND_NESS_YOYO:
        // x0 is the smash action id, written at spawn. x4's only owner,
        // it_802BFEC4, writes it on the despawn transition, so no live
        // export ever samples an owner-written value there: like the PK
        // Flash leading word, the sampled byte is fixed-pool residue for
        // the article's whole exported life (the swing read in it_802BF800
        // consumes the same residue retail does; its gameplay effect, if
        // any, lands in directly compared lanes). x8/xC are the string
        // ItemLink chain, x10 the owner GObj, x14 padding and x18 the
        // string joint.
        // refs/melee/src/melee/it/itCharItems.h::itNessYoyo_ItemVars
        // refs/melee/src/melee/it/items/itnessyoyo.c::it_802BFEC4
        return MSL_CORE_ITEM_MISC0;
    case MSL_CORE_ITEM_KIND_LINK_BOMB:
    case MSL_CORE_ITEM_KIND_CLINK_BOMB:
        // x0's flag bits are written but its trailing x1/x2/x3 bytes never
        // are, so xDD7 is unwritten residue. xDDB samples the last byte of
        // the x4 direction-sign float, which is unwritten pool residue for
        // the whole held phase (it_8029F18C first writes it on the thrown
        // transition) and a constant 0x00 afterwards (the byte of +/-1.0f),
        // so no live export ever samples an owner-written varying value.
        // The struct ends at x14, so xDEB/xDEF are past-member residue.
        // refs/melee/src/melee/it/itCharItems.h::itLinkBomb_ItemVars
        // refs/melee/src/melee/it/items/itlinkbomb.c::it_8029F18C
        return 0;
    case MSL_CORE_ITEM_KIND_LINK_BOOMERANG:
    case MSL_CORE_ITEM_KIND_CLINK_BOOMERANG:
        // No owner writes the leading xDD4 word; xDD8 (turn state), xDE8
        // (turn timer), and xDEC (catch state) are constructor- and
        // motion-written scalars.
        // refs/melee/src/melee/it/itCharItems.h::itLinkBoomerang_ItemVars
        // refs/melee/src/melee/it/items/itlinkboomerang.c
        return MSL_CORE_ITEM_MISC1 | MSL_CORE_ITEM_MISC2 |
               MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_LINK_HOOKSHOT:
    case MSL_CORE_ITEM_KIND_CLINK_HOOKSHOT:
        // x0/x4 are ItemLink pointers (host-widened), x14's trailing bytes
        // are padding, and the x18 float past the sampled window is the
        // first owner-written scalar.
        // refs/melee/src/melee/it/itCharItems.h::itLinkHookshot_ItemVars
        // refs/melee/src/melee/it/items/itlinkhookshot.c
        return 0;
    case MSL_CORE_ITEM_KIND_LINK_ARROW:
    case MSL_CORE_ITEM_KIND_CLINK_ARROW:
        // The leading x0..x14 floats are never written by any owner; the
        // first written member is the x18 spawn-position vector whose x
        // lane xDEF samples.
        // refs/melee/src/melee/it/itCharItems.h::itLinkArrow_ItemVars
        // refs/melee/src/melee/it/items/itlinkarrow.c
        return MSL_CORE_ITEM_MISC3;
    case MSL_CORE_ITEM_KIND_LINK_BOW:
    case MSL_CORE_ITEM_KIND_CLINK_BOW:
        // x0 is the charge frame scalar written at draw start; x4 is the
        // owner arrow GObj pointer and the struct ends there.
        // refs/melee/src/melee/it/itCharItems.h::itLinkBow_ItemVars
        // refs/melee/src/melee/it/items/itlinkbow.c
        return MSL_CORE_ITEM_MISC0;
    case MSL_CORE_ITEM_KIND_CLINK_MILK:
        // The milk bottle declares only its owner Fighter_GObj pointer.
        // refs/melee/src/melee/it/itCharItems.h::itCLinkMilk_ItemVars
        return 0;
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
