// Stored fighter observations through charge, landing, and stock transitions.
#include "runtime/scalar.h"
#include "runtime/observation.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftDonkey/forward.h"
#include "ftDonkey/types.h"
#include "ftGameWatch/forward.h"
#include "ftGameWatch/types.h"
#include "ftKirby/forward.h"
#include "ftKirby/ftkirby.h"
#include "ftKirby/types.h"
#include "ftKoopa/forward.h"
#include "ftKoopa/types.h"
#include "ftMewtwo/forward.h"
#include "ftMewtwo/types.h"
#include "ftSamus/forward.h"
#include "ftSamus/types.h"
#include "ftSeak/forward.h"
#include "ftLuigi/forward.h"
#include "ftMario/forward.h"
#include "ftPeach/forward.h"
#include "ftPeach/types.h"
#include "it/types.h"
#include "it/items/itfoxlaser.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSL_TEST_FOX = 1, MSL_TEST_FALCO = 22 };

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;
static const MslCoreInput msl_test_b = { { { PAD_BUTTON_B } } };
static const MslCoreInput msl_test_a = { { { PAD_BUTTON_A } } };
static const MslCoreInput msl_test_jump = { { { PAD_BUTTON_X } } };
static const MslCoreInput msl_test_shield = {
    { { PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255 } }
};
static const MslCoreInput msl_test_shield_b = {
    { { PAD_TRIGGER_R | PAD_BUTTON_B, 0, 0, 0, 0, 0, 255 } }
};
static const MslCoreInput msl_test_down_b = { { { PAD_BUTTON_B, 0, -80 } } };
static const MslCoreInput msl_test_side_b = { { { PAD_BUTTON_B, 80, 0 } } };
static const MslCoreInput msl_test_left = { { { 0, -80, 0 } } };
static const MslCoreInput msl_test_right = { { { 0, 80, 0 } } };
static MslCoreObservationStored msl_test_stored;
static const MslCoreObservationStored msl_test_nothing;
static bool msl_test_present;
static unsigned msl_test_stocks = 4;
static unsigned msl_test_stage = MSL_STAGE_FINAL_DESTINATION;

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (frame %d, charge %d)\n", __func__, __LINE__, \
                #condition, msl_test_match.frame_id, msl_test_stored.charge); \
        return -1; \
    } \
} while (0)

static Fighter* msl_test_fighter(unsigned port)
{
    return msl_test_match.fighters[port]->user_data;
}

static int msl_test_frame(const MslCoreInput* input)
{
    MslCoreObservation observation;
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                                      &(MslCoreStageEvents) { 0 }) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 0, &observation) == 0);
    msl_test_stored = observation.slots[0].stored;
    msl_test_present = observation.slots[0].present;
    return 0;
}

static float msl_test_fuel(void)
{
    return msl_core_get_lef32(&msl_test_stored.character_value);
}

static int msl_test_setup(unsigned character, unsigned opponent)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = msl_test_stage;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = msl_test_stocks;
    config.players[0].char_id = character;
    config.players[1].char_id = opponent;
    MSL_TEST_CHECK((msl_test_match.memory.arena == NULL
               ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)
               : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)) == 0);
    for (unsigned i = 0; i < 150; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0 && msl_test_stored.charge == 0);
    }
    return 0;
}

// `frames` frames of `input`, the value staying at `charge` on every one.
static int msl_test_hold(const MslCoreInput* input, unsigned frames, int charge)
{
    for (unsigned i = 0; i < frames; ++i) {
        MSL_TEST_CHECK(msl_test_frame(input) == 0 && msl_test_stored.charge == charge);
    }
    return 0;
}

// Holds `input` until the value is `target`: it may only stay or go up by one.
static int msl_test_rise(const MslCoreInput* input, int target, unsigned limit)
{
    for (unsigned i = 0; msl_test_stored.charge != target; ++i) {
        int before = msl_test_stored.charge;
        MSL_TEST_CHECK(i < limit);
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
        MSL_TEST_CHECK(msl_test_stored.charge == before || msl_test_stored.charge == before + 1);
    }
    return 0;
}

// Holds `input` until port 0 is in `motion`, the value staying at `charge`.
static int msl_test_until(const MslCoreInput* input, int motion, unsigned limit, int charge)
{
    for (unsigned i = 0; (int) msl_test_fighter(0)->motion_id != motion; ++i) {
        MSL_TEST_CHECK(i < limit);
        MSL_TEST_CHECK(msl_test_frame(input) == 0 && msl_test_stored.charge == charge);
    }
    return 0;
}

// A short hop and a jab from standing: the value is kept through both.
static int msl_test_other_actions(int charge)
{
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, charge) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 2, charge) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 10, charge) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->ground_or_air == GA_Air);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, charge) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_a, 1, charge) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftCo_MS_Attack11);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, charge) == 0);
    return 0;
}

static void msl_test_force_ko(Fighter* fp)
{
    fp->cur_pos = (Vec3) { -300, 40, 0 };
    fp->prev_pos = fp->cur_pos;
    ftCommon_8007D5D4(fp);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
}

static int msl_test_ko(MslCoreObservationStored dead, MslCoreObservationStored reborn)
{
    Fighter* fp = msl_test_fighter(0);
    int stocks = msl_test_match.source.player.slots[fp->player_id].stocks;
    msl_test_force_ko(fp);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_DeadLeft &&
                   msl_test_match.source.player.slots[fp->player_id].stocks == stocks - 1);
    for (unsigned i = 0; fp->motion_id == ftCo_MS_DeadLeft; ++i) {
        MSL_TEST_CHECK(i < 120 && msl_test_present);
        MSL_TEST_CHECK(memcmp(&msl_test_stored, &dead, sizeof(dead)) == 0);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_Rebirth && msl_test_present);
    MSL_TEST_CHECK(memcmp(&msl_test_stored, &reborn, sizeof(reborn)) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 900, reborn.charge) == 0);
    MSL_TEST_CHECK(memcmp(&msl_test_stored, &reborn, sizeof(reborn)) == 0);
    return 0;
}

// Giant Punch counts arm swings. Three swings and a shield out of the wind-up
// keep 3; winding up again goes on from 3 to the full count, where the game
// ends the wind-up itself; the punch takes the count. A KO clears it.
static int msl_test_giant_punch(void)
{
    Fighter* fp;
    int full;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_DONKEY, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    full = ((ftDonkeyAttributes*) fp->dat_attrs)->SpecialN.x2C_MAX_ARM_SWINGS;
    MSL_TEST_CHECK(full > 4 && full <= UINT8_MAX);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 200) == 0);
    // The shield is taken at the top of the next swing, which still counts.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 3, 60) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, full, 600) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_Wait);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);

    // The punch: the count moves into the move on the frame it starts.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialNFull && fp->mv.dk.specialn.xC == full);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // One swing, punched from the wind-up.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 1, 200) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialN && fp->mv.dk.specialn.xC == 1);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // Two swings stored, then a KO.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 1, 200) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 1) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 60) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 2) == 0);
    MSL_TEST_CHECK(msl_test_ko((MslCoreObservationStored) { .charge = 2 }, msl_test_nothing) == 0);
    fprintf(stderr, "giant punch: full at %d swings\n", full);
    return 0;
}

static int msl_test_absent_after_last_stock(void)
{
    msl_test_stocks = 1;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_GAMEWATCH, MSL_TEST_FALCO) == 0);
    msl_test_stocks = 4;

    Fighter* fp = msl_test_fighter(0);
    fp->cur_pos = (Vec3) { -20, 0, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = 1;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 20, 0) == 0);

    Vec3 pos = { 30, 8, 0 };
    it_8029C6A4(3.14159265358979323846F, 4, msl_test_match.fighters[1], &pos,
                It_Kind_Falco_Laser);
    for (unsigned i = 0; msl_test_stored.charge == 0; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_down_b) == 0);
        MSL_TEST_CHECK(msl_test_stored.charge <= 1);
        MSL_TEST_CHECK(msl_core_get_le32(&msl_test_stored.oil_panic_damage) ==
                       3U * msl_test_stored.charge);
    }
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 50, 1) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 1) == 0);

    msl_test_force_ko(fp);
    int absent = 0;
    for (unsigned i = 0; i < 300; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        if (msl_test_present) {
            MSL_TEST_CHECK(msl_test_stored.charge == 1);
        } else {
            MSL_TEST_CHECK(memcmp(&msl_test_stored, &msl_test_nothing, sizeof(msl_test_stored)) == 0);
            ++absent;
        }
    }
    MSL_TEST_CHECK(absent > 100 && !msl_test_present && fp->fv.gw.x2238_panicCharge == 1 &&
                   fp->fv.gw.x223C_panicDamage > 0);
    return 0;
}

// Fire Breath's fuel drains by one, then refills at its loaded rate.
static int msl_test_breathe(float full, float floor, float rate, unsigned breath)
{
    int moving = 0, frames = 0;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_fuel() == full);
    for (unsigned i = 0; i < breath; ++i) {
        float prev = msl_test_fuel();
        float down = prev - 1.0F < floor ? floor : prev - 1.0F;
        MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0 && msl_test_stored.charge == 0);
        MSL_TEST_CHECK(msl_test_fuel() == down || (!moving && msl_test_fuel() == prev));
        moving |= msl_test_fuel() != prev;
    }
    MSL_TEST_CHECK(moving);
    moving = 0;
    for (unsigned i = 0; msl_test_fuel() != full; ++i) {
        float prev = msl_test_fuel();
        float up = prev + rate > full ? full : prev + rate;
        float down = prev - 1.0F < floor ? floor : prev - 1.0F;
        MSL_TEST_CHECK(i < 3000);
        MSL_TEST_CHECK(msl_test_frame(moving > 20 && moving < 23 ? &msl_test_jump
                                                               : &msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_stored.charge == 0);
        MSL_TEST_CHECK(msl_test_fuel() == up ||
                       (!moving && (msl_test_fuel() == down || msl_test_fuel() == prev)));
        moving += moving || msl_test_fuel() > prev;
        frames += moving != 0;
    }
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
    return frames;
}

static int msl_test_fire_breath(void)
{
    Fighter* fp;
    ftKoopaAttributes* da;
    int frames;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KOOPA, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    da = fp->dat_attrs;
    {
        const float full = da->x10;
        const float floor = da->x14;
        const float rate = da->x8;
        MSL_TEST_CHECK(full > floor + 20 && rate > 0);
        MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
        // A short breath, then a long one that reaches its floor and stays.
        MSL_TEST_CHECK(msl_test_breathe(full, floor, rate, 40) > 0);
        MSL_TEST_CHECK(msl_test_fuel() == full && fp->motion_id == ftCo_MS_Wait);
        frames = msl_test_breathe(full, floor, rate, 400);
        MSL_TEST_CHECK(frames > 0);
        fprintf(stderr, "fire breath: fuel %g..%g +%g a frame, %d frames back to full\n",
                floor, full, rate, frames);

        // Drained to its floor, then a KO: the respawn makes it full at
        // once (ftKp_Init_OnDeath).
        for (unsigned i = 0; i < 400; ++i) {
            MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
        }
        MSL_TEST_CHECK(msl_test_fuel() == floor);
        msl_test_force_ko(fp);
        for (unsigned i = 0; fp->motion_id != ftCo_MS_Rebirth; ++i) {
            float before = msl_test_fuel();
            MSL_TEST_CHECK(i < 120);
            MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
            if (fp->motion_id == ftCo_MS_Rebirth) {
                MSL_TEST_CHECK(before < full - 1);
            } else {
                MSL_TEST_CHECK(fp->motion_id == ftCo_MS_DeadLeft && msl_test_fuel() < full);
            }
        }
        MSL_TEST_CHECK(msl_test_fuel() == full);
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 900, 0) == 0);
    }
    return 0;
}

static int msl_test_copied_fire_breath(void)
{
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KIRBY, MSL_TEST_FOX) == 0);
    const ftKb_DatAttrs* da = msl_test_fighter(0)->dat_attrs;
    ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], FTKIND_KOOPA, false);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_stored.character_byte == FTKIND_KOOPA);
    MSL_TEST_CHECK(msl_test_breathe(da->specialn_kp_max_fuel, da->specialn_kp_spew_flame_velocity,
                                   da->specialn_kp_fuel_recharge_rate, 400) > 0);
    return 0;
}

// The other three copies, hat given directly: the observed value is the count
// of the hat Kirby wears, kept through a shield out of the charge, continued
// to the copy's full count, and gone at a KO.
static int msl_test_kirby_copy(FighterKind kind)
{
    const MslCoreInput* charge = kind == FTKIND_SEAK ? &msl_test_b : &msl_test_neutral;
    const MslCoreInput* shield = kind == FTKIND_SEAK ? &msl_test_shield_b : &msl_test_shield;
    Fighter* fp;
    ftKb_DatAttrs* da;
    int full;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KIRBY, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    da = fp->dat_attrs;
    full = kind == FTKIND_SAMUS ? (int) da->specialn_ss_charge_time : 6;
    ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], kind, false);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_stored.character_byte == kind);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
    MSL_TEST_CHECK(msl_test_rise(charge, 3, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(shield, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);
    MSL_TEST_CHECK((kind == FTKIND_SAMUS ? fp->fv.kb.xA8 : fp->fv.kb.xB4) == 3);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_rise(charge, full, 900) == 0);
    MSL_TEST_CHECK(msl_test_hold(charge, 30, full) == 0);
    MSL_TEST_CHECK(msl_test_hold(shield, 1, full) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);
    // Kirby's death callback takes the hat, and the copy's count with it, on
    // the frame of the KO.
    MSL_TEST_CHECK(msl_test_stored.character_byte == kind);
    const MslCoreObservationStored no_copy = { .character_byte = FTKIND_KIRBY };
    MSL_TEST_CHECK(msl_test_ko(no_copy, no_copy) == 0);
    MSL_TEST_CHECK(fp->fv.kb.hat.kind == FTKIND_KIRBY);
    fprintf(stderr, "kirby: copy of fighter %d full at %d\n", (int) kind, full);
    return 0;
}

// Port 0 (and a follower) put in the air above the middle of the stage,
// falling. A fighter already in the air is only moved: nothing lands.
static void msl_test_air(float y)
{
    for (int who = 0; who < 2; ++who) {
        HSD_GObj* gobj = who ? msl_test_match.follower_fighters[0] : msl_test_match.fighters[0];
        Fighter* fp;
        if (gobj == NULL) {
            continue;
        }
        fp = gobj->user_data;
        fp->cur_pos = (Vec3) { who ? -12 : 0, y, 0 };
        fp->prev_pos = fp->cur_pos;
        if (fp->ground_or_air == GA_Ground) {
            fp->self_vel.x = fp->self_vel.y = 0;
            ftCommon_8007D5D4(fp);
            ftCo_Fall_Enter(gobj);
        }
    }
}

// One frame in the air: the fighter is moved back up before it gets near the
// floor, so that a move ends in the air.
static int msl_test_air_frame(const MslCoreInput* input)
{
    Fighter* fp = msl_test_fighter(0);
    if (fp->cur_pos.y < 40) {
        msl_test_air(fp->cur_pos.y + 80);
    }
    MSL_TEST_CHECK(msl_test_frame(input) == 0);
    MSL_TEST_CHECK(fp->ground_or_air == GA_Air);
    return 0;
}

// Neutral in the air until the fighter is in `motion`, the bits unchanged.
static int msl_test_air_until(int motion)
{
    Fighter* fp = msl_test_fighter(0);
    for (unsigned i = 0; (int) fp->motion_id != motion; ++i) {
        MSL_TEST_CHECK(i < 300);
        MSL_TEST_CHECK(msl_test_air_frame(&msl_test_neutral) == 0);
    }
    return 0;
}

// The lift bit can only turn on during the move. Report its peak upward speed.
static int msl_test_aerial_special(const MslCoreInput* press, int bit, int was, float* top)
{
    Fighter* fp = msl_test_fighter(0);
    int spent = was, move;
    *top = -1000;
    MSL_TEST_CHECK(fp->ground_or_air == GA_Air && fp->self_vel.y <= 0);
    MSL_TEST_CHECK(msl_test_air_frame(press) == 0);
    move = fp->motion_id;
    MSL_TEST_CHECK(move >= ftCo_MS_Count);
    for (unsigned i = 0; (int) fp->motion_id == move; ++i) {
        float before = fp->self_vel.y;
        MSL_TEST_CHECK(msl_test_stored.spent == spent ||
                       (spent == was && msl_test_stored.spent == (was | bit)));
        spent = msl_test_stored.spent;
        *top = before > *top ? before : *top;
        MSL_TEST_CHECK(i < 300);
        MSL_TEST_CHECK(msl_test_air_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_stored.spent == (was | bit));
    MSL_TEST_CHECK(msl_test_air_until(ftCo_MS_Fall) == 0);
    MSL_TEST_CHECK(msl_test_stored.spent == (was | bit));
    return 0;
}

// Falls with `input` held until the fighter stands on the ground, which must
// be in `landing`; the spent bits are `was` in the air and `after` from the
// first frame on the ground, and stay so until the fighter is standing.
static int msl_test_land(const MslCoreInput* input, int landing, int was, int after)
{
    Fighter* fp = msl_test_fighter(0);
    for (unsigned i = 0; fp->ground_or_air == GA_Air; ++i) {
        MSL_TEST_CHECK(i < 400);
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
        MSL_TEST_CHECK(msl_test_stored.spent == (fp->ground_or_air == GA_Air ? was : after));
    }
    MSL_TEST_CHECK((int) fp->motion_id == landing);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    MSL_TEST_CHECK(msl_test_stored.spent == after);
    return 0;
}

// An air attack (`attack` for a frame) kept in the air until its landing lag
// applies, then brought down to the floor: the fighter lands in the attack's
// landing state, not in the plain one.
static int msl_test_land_in_lag(const MslCoreInput* attack, int was, int after)
{
    Fighter* fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_air_frame(attack) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_AttackAirN);
    for (unsigned i = 0; !fp->cmd_vars[0]; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_air_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_AttackAirN);
    msl_test_air(1.5F);
    return msl_test_land(&msl_test_neutral, ftCo_MS_LandingAirN, was, after);
}

// A laser from above onto the airborne fighter: the hit changes nothing, and
// the fighter comes out of it falling.
static int msl_test_hit_in_air(void)
{
    Fighter* fp = msl_test_fighter(0);
    float percent = fp->dmg.x1830_percent;
    Vec3 pos = { fp->cur_pos.x, fp->cur_pos.y + 30, 0 };
    it_8029C6A4(-1.57079632679489661923F, 6, msl_test_match.fighters[1], &pos,
                It_Kind_Falco_Laser);
    for (unsigned i = 0; fp->dmg.x1830_percent == percent; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_air_frame(&msl_test_neutral) == 0);
    }
    return msl_test_air_until(ftCo_MS_Fall);
}

// A full hop from standing, to the frame the fighter stops rising.
static int msl_test_jump_to_the_top(void)
{
    Fighter* fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 6, 0) == 0);
    for (unsigned i = 0; fp->ground_or_air == GA_Ground || fp->self_vel.y > 0; ++i) {
        MSL_TEST_CHECK(i < 80);
        MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    }
    MSL_TEST_CHECK(fp->ground_or_air == GA_Air);
    return 0;
}

// A special whose aerial use lifts the fighter once. The first use in the air
// rises and sets the bit; with the bit set a second use does not rise. A hit
// leaves the bit; a plain landing clears it on its first frame. A landing in
// the landing lag of an air attack does not: the game clears these in
// ftCo_Landing_Enter only, so the fighter stands on the ground with the lift
// still spent, and a use after the next jump does not rise. A KO clears it.
// With `hat`, Kirby is given that copied ability first.
static int msl_test_special_lift(unsigned kind, unsigned hat, const MslCoreInput* press, int bit,
                                 const char* name)
{
    float first, second, again, stale;
    MSL_TEST_CHECK(msl_test_setup(kind, MSL_TEST_FALCO) == 0);
    if (hat != FTKIND_NONE) {
        ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], hat, false);
    }
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);

    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, bit, 0, &first) == 0);
    MSL_TEST_CHECK(first > 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, bit, bit, &second) == 0);
    MSL_TEST_CHECK(second <= 0);
    MSL_TEST_CHECK(msl_test_hit_in_air() == 0 && msl_test_stored.spent == bit);
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);

    // Used again, then a neutral air attack into the floor.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, bit, 0, &again) == 0);
    MSL_TEST_CHECK(again == first);
    MSL_TEST_CHECK(msl_test_land_in_lag(&msl_test_a, bit, bit) == 0);
    // Still spent after standing and a jab; the next use is from a jump.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_a, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 0) == 0);
    MSL_TEST_CHECK(msl_test_stored.spent == bit);
    MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, bit, bit, &stale) == 0);
    MSL_TEST_CHECK(stale <= 0);
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);

    // Spent once more, then a KO. The KO takes Kirby's hat, and the copied
    // move's bit with it, on its own frame.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, bit, 0, &again) == 0);
    MSL_TEST_CHECK(again == first);
    MSL_TEST_CHECK(msl_test_stored.character_byte == (hat == FTKIND_NONE ? 0 : hat));
    const MslCoreObservationStored dead = {
        .spent = hat == FTKIND_NONE ? bit : 0,
        .character_byte = hat == FTKIND_NONE ? 0 : FTKIND_KIRBY,
    };
    const MslCoreObservationStored reborn = { .character_byte = dead.character_byte };
    MSL_TEST_CHECK(msl_test_ko(dead, reborn) == 0);
    fprintf(stderr, "%s: rises at %g a frame, then %g; %g after a landing in landing lag\n", name,
            first, second, stale);
    return 0;
}

static int msl_test_judge_history(void)
{
    const MslCoreObservationStored starting = { .character_byte = 1 };
    MSL_TEST_CHECK(msl_test_setup(FTKIND_GAMEWATCH, MSL_TEST_FOX) == 0);
    Fighter* fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_stored.character_byte == starting.character_byte);
    for (int aerial = 0; aerial < 2; ++aerial) {
        const uint8_t previous = msl_test_stored.character_byte;
        if (aerial) {
            msl_test_air(60);
            MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 2, 0) == 0);
        }
        MSL_TEST_CHECK(msl_test_hold(&msl_test_side_b, 1, 0) == 0);
        const uint8_t history = msl_test_stored.character_byte;
        const int hammer = history & 15;
        MSL_TEST_CHECK(hammer < 9 && hammer != (previous & 15) && hammer != (previous >> 4));
        MSL_TEST_CHECK((history >> 4) == (previous & 15));
        MSL_TEST_CHECK((int) fp->motion_id ==
                       (aerial ? ftGw_MS_SpecialAirS1 : ftGw_MS_SpecialS1) + hammer);
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 400, 0) == 0);
        MSL_TEST_CHECK(msl_test_stored.character_byte == history && msl_test_stored.spent == 0);
    }
    return msl_test_ko(msl_test_stored, starting);
}

static int msl_test_wall_jumps(void)
{
    static const MslCoreInput drop = { { { 0, 0, -80 } } };
    static const MslCoreInput back_air_right = { { { 0, 80, 0, 80, 0 } } };
    static const MslCoreInput up_air_right = { { { 0, 80, 0, 0, 80 } } };
    static const MslCoreInput jump_right = { { { PAD_BUTTON_X, 80, 0 } } };
    static const struct {
        const MslCoreInput* input;
        unsigned frames;
        int wall_jumps;
    } moves[] = {
        // Drop from the left ledge, drift into the wall, then flick away.
        { &drop, 1, 0 },
        { &msl_test_neutral, 1, 0 },
        { &msl_test_right, 9, 0 },
        { &msl_test_left, 4, 1 },
        // Back air after the first jump, then drift back into the wall.
        { &msl_test_right, 1, 1 },
        { &back_air_right, 1, 1 },
        { &msl_test_right, 44, 1 },
        { &msl_test_neutral, 1, 1 },
        { &msl_test_left, 4, 2 },
        // Up air after the second jump.
        { &msl_test_right, 1, 2 },
        { &up_air_right, 1, 2 },
        { &msl_test_right, 43, 2 },
        { &msl_test_neutral, 1, 2 },
        { &msl_test_left, 1, 3 },
        { &msl_test_right, 5, 3 },
    };
    float speed[3] = { 0 };

    msl_test_stage = MSL_STAGE_YOSHIS_STORY;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_CAPTAIN, MSL_TEST_FALCO) == 0);
    msl_test_stage = MSL_STAGE_FINAL_DESTINATION;
    const Fighter* fp = msl_test_fighter(0);

    // Short hop backward from the side platform and drift into the ledge.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 5, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_left, 18, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_right, 24, 0) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_CliffWait, 15, 0) == 0);

    for (unsigned i = 0; i < sizeof(moves) / sizeof(moves[0]); ++i) {
        const int count = moves[i].wall_jumps;
        for (unsigned frame = 0; frame < moves[i].frames; ++frame) {
            const int previous_count = msl_test_stored.wall_jumps;
            MSL_TEST_CHECK(msl_test_frame(moves[i].input) == 0);
            MSL_TEST_CHECK(fp->ground_or_air == GA_Air && fp->motion_id != ftCo_MS_CliffCatch);
            MSL_TEST_CHECK(msl_test_stored.wall_jumps == count);
            MSL_TEST_CHECK(previous_count == count || fp->motion_id == ftCo_MS_PassiveWallJump);
            if (count > 0 && fp->self_vel.y > speed[count - 1]) {
                speed[count - 1] = fp->self_vel.y;
            }
        }
    }
    MSL_TEST_CHECK(speed[0] > speed[1] && speed[1] > speed[2] && speed[2] > 0);

    // Double jump back to the stage. The landing clears the counter.
    MSL_TEST_CHECK(msl_test_frame(&jump_right) == 0 && msl_test_stored.wall_jumps == 3);
    for (unsigned i = 0; fp->ground_or_air == GA_Air; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_right) == 0);
        MSL_TEST_CHECK(msl_test_stored.wall_jumps == (fp->ground_or_air == GA_Air ? 3 : 0));
    }
    MSL_TEST_CHECK(msl_test_match.source.player.slots[0].stocks == msl_test_stocks);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 60, 0) == 0);
    fprintf(stderr, "falcon: three wall jumps on Yoshi's rising at %g, %g and %g a frame\n",
            speed[0], speed[1], speed[2]);
    return 0;
}

static int msl_test_follower_record(void)
{
    MSL_TEST_CHECK(msl_test_setup(FTKIND_POPO, MSL_TEST_FOX) == 0);
    Fighter* popo = msl_test_fighter(0);
    Fighter* nana = msl_test_match.follower_fighters[0]->user_data;
    MSL_TEST_CHECK(!nana->x221F_b3);
    popo->x2224_b1 = true;
    nana->fv.nn.x224C = true;
    nana->x1969_walljumpUsed = 2;
    const MslCoreObservationStored leader = { .spent = MSL_SPENT_AIR_ITEM_CATCH };
    const MslCoreObservationStored follower = {
        .spent = MSL_SPENT_NEUTRAL_LIFT,
        .wall_jumps = 2,
    };
    for (unsigned sleeping = 0; sleeping < 2; ++sleeping) {
        nana->x221F_b3 = sleeping;
        for (unsigned viewpoint = 0; viewpoint < 2; ++viewpoint) {
            MslCoreObservation direct, compared;
            MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, viewpoint,
                                                           &direct) == 0);
            MSL_TEST_CHECK(msl_core_match_write_observation_from_compare(&msl_test_match,
                                                                        viewpoint, &compared) == 0);
            MSL_TEST_CHECK(memcmp(&direct, &compared, sizeof(direct)) == 0);
            MSL_TEST_CHECK(direct.slots[viewpoint].source_player == 0);
            MSL_TEST_CHECK(direct.followers[viewpoint].present == !sleeping);
            for (unsigned slot = 0; slot < MSL_MAX_PLAYERS; ++slot) {
                const MslCoreObservationStored* player_expected =
                    slot == viewpoint ? &leader : &msl_test_nothing;
                const MslCoreObservationStored* follower_expected =
                    slot == viewpoint && !sleeping ? &follower : &msl_test_nothing;
                MSL_TEST_CHECK(memcmp(&direct.slots[slot].stored, player_expected,
                                      sizeof(*player_expected)) == 0);
                MSL_TEST_CHECK(memcmp(&direct.followers[slot].stored, follower_expected,
                                      sizeof(*follower_expected)) == 0);
            }
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    // Exercise each field and the absent-fighter records.
    result = msl_test_giant_punch() || msl_test_fire_breath() || msl_test_copied_fire_breath() ||
             msl_test_kirby_copy(FTKIND_SAMUS) || msl_test_kirby_copy(FTKIND_SEAK) ||
             msl_test_special_lift(FTKIND_MARIO, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "mario's cape") ||
             msl_test_special_lift(FTKIND_KIRBY, FTKIND_PEACH, &msl_test_b,
                                   MSL_SPENT_NEUTRAL_LIFT, "kirby's copied Toad") ||
             msl_test_special_lift(FTKIND_KIRBY, FTKIND_POPO, &msl_test_b,
                                   MSL_SPENT_NEUTRAL_LIFT, "kirby's copied Ice Shot") ||
             msl_test_wall_jumps() || msl_test_judge_history() ||
             msl_test_absent_after_last_stock() || msl_test_follower_record();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
