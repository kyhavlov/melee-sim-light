// What the observation publishes of the state a fighter keeps between moves,
// with extracted data, the real scheduler and controller inputs: the stored
// charges (Giant Punch, Charge Shot, Needle Storm, Shadow Ball, Oil Panic and
// Kirby's copies), Oil Panic's damage, Fire Breath, Kirby's copied ability,
// Judge's last two numbers, the lifts, float and grapple a fighter has spent,
// and the wall jumps it has made. Every frame checks the observed values
// against what the moves have done so far, that both observation builders
// agree byte for byte, that the record sits in its player's slot from either
// viewpoint, and that the opponent has none.
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
#include "it/items/itmariofireball.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MSL_TEST_MARIO = 0, MSL_TEST_FOX = 1, MSL_TEST_FALCO = 22 };

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
static const MslCoreInput msl_test_taunt = { { { PAD_BUTTON_UP } } };
static const MslCoreInput msl_test_up_b = { { { PAD_BUTTON_B, 0, 80 } } };
static const MslCoreInput msl_test_side_b = { { { PAD_BUTTON_B, 80, 0 } } };
static const MslCoreInput msl_test_z = { { { PAD_TRIGGER_Z } } };
static const MslCoreInput msl_test_jump_a = { { { PAD_BUTTON_X | PAD_BUTTON_A } } };
static const MslCoreInput msl_test_jump_down = { { { PAD_BUTTON_X, 0, -80 } } };
static const MslCoreInput msl_test_left = { { { 0, -80, 0 } } };
static const MslCoreInput msl_test_right = { { { 0, 80, 0 } } };
// The value observed for port 0 after the last frame.
static int msl_test_charge;
// Its two gauges, and what a player who keeps nothing reads.
static float msl_test_gauge[2];
static const MslCoreObservationStored msl_test_nothing = {
    .copied_char = MSL_COPIED_NONE, .judge = { MSL_JUDGE_NONE, MSL_JUDGE_NONE }
};
// Kirby's copied ability as observed for port 0, and what it must read on
// every frame unless the scenario is following it itself.
static int msl_test_copied;
static int msl_test_want_copied = MSL_COPIED_NONE;
static int msl_test_copied_free;
// What the gauges must read on every frame while port 0 is present, unless
// the scenario is following them itself; and what a respawn leaves them at.
static float msl_test_want[2];
static float msl_test_reborn[2];
static int msl_test_gauge_free;
// Whether port 0 was present in that observation, and the stocks and stage
// of the next setup.
static int msl_test_present;
static unsigned msl_test_stocks = 4;
static unsigned msl_test_stage = 32;
// The spent bits observed for port 0 (the follower's bit apart); what they
// must read on every frame unless the scenario is following them itself; and
// what a respawn leaves.
static int msl_test_spent;
static int msl_test_want_spent;
static int msl_test_reborn_spent;
// What the new stock reads once it stands on the ground, when that differs
// from what the respawn leaves.
static int msl_test_landed_spent = -1;
static int msl_test_spent_free;
// The frames on which the follower's bit was set.
static int msl_test_follower_seen;
// The wall jumps observed, 0 on every frame unless the scenario follows them.
static int msl_test_walls;
static int msl_test_walls_free;
// Judge's last two numbers as observed, and what they must read.
static int msl_test_judge[2];
static int msl_test_want_judge[2] = { MSL_JUDGE_NONE, MSL_JUDGE_NONE };
static int msl_test_judge_free;

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (frame %d, charge %d)\n", __func__, __LINE__, \
                #condition, msl_test_match.frame_id, msl_test_charge); \
        return -1; \
    } \
} while (0)

static Fighter* msl_test_fighter(unsigned port)
{
    return msl_test_match.fighters[port]->user_data;
}

static int msl_test_frame(const MslCoreInput* input)
{
    MslCoreObservation direct, compared, other;
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                                       &(MslCoreStageEvents) { 0 }) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 0, &direct) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation_from_compare(&msl_test_match, 0, &compared) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 1, &other) == 0);
    msl_test_charge = direct.stored[0].charge;
    msl_test_copied = direct.stored[0].copied_char;
    msl_test_gauge[0] = direct.stored[0].gauge[0];
    msl_test_gauge[1] = direct.stored[0].gauge[1];
    msl_test_present = direct.slots[0].present;
    msl_test_spent = direct.stored[0].spent & ~MSL_SPENT_FOLLOWER_NEUTRAL_LIFT;
    msl_test_walls = direct.stored[0].wall_jumps;
    msl_test_judge[0] = direct.stored[0].judge[0];
    msl_test_judge[1] = direct.stored[0].judge[1];
    MSL_TEST_CHECK(memcmp(&direct, &compared, sizeof(direct)) == 0);
    MSL_TEST_CHECK(direct.stored[0]._pad0[0] == 0 && direct.stored[0]._pad0[1] == 0 &&
                   (direct.stored[0].spent & ~0x3F) == 0);
    if (msl_test_present) {
        // The follower's bit is her own variable while she is awake.
        const Fighter* nana = msl_test_match.follower_fighters[0] == NULL
                                  ? NULL : msl_test_match.follower_fighters[0]->user_data;
        int hers = nana != NULL && !nana->x221F_b3 && nana->fv.nn.x224C;
        MSL_TEST_CHECK(!(direct.stored[0].spent & MSL_SPENT_FOLLOWER_NEUTRAL_LIFT) == !hers);
        msl_test_follower_seen += hers;
        MSL_TEST_CHECK(msl_test_spent_free || msl_test_spent == msl_test_want_spent);
        MSL_TEST_CHECK(msl_test_walls_free || msl_test_walls == 0);
        MSL_TEST_CHECK(msl_test_judge_free || (msl_test_judge[0] == msl_test_want_judge[0] &&
                                               msl_test_judge[1] == msl_test_want_judge[1]));
    }
    if (!msl_test_present) {
        MSL_TEST_CHECK(memcmp(&direct.stored[0], &msl_test_nothing, sizeof(msl_test_nothing)) == 0);
    } else if (!msl_test_gauge_free) {
        MSL_TEST_CHECK(msl_test_gauge[0] == msl_test_want[0] &&
                       msl_test_gauge[1] == msl_test_want[1]);
    }
    MSL_TEST_CHECK(msl_test_copied_free || !msl_test_present ||
                   msl_test_copied == msl_test_want_copied);
    // Singles: each viewpoint has itself in slot 0 and the opponent in slot 1.
    MSL_TEST_CHECK(other.slots[1].source_player == 0 &&
                   memcmp(&other.stored[1], &direct.stored[0], sizeof(direct.stored[0])) == 0);
    MSL_TEST_CHECK(memcmp(&other.stored[0], &direct.stored[1], sizeof(direct.stored[0])) == 0);
    MSL_TEST_CHECK(memcmp(&direct.stored[1], &msl_test_nothing, sizeof(msl_test_nothing)) == 0 &&
                   memcmp(&direct.stored[2], &msl_test_nothing, sizeof(msl_test_nothing)) == 0 &&
                   memcmp(&direct.stored[3], &msl_test_nothing, sizeof(msl_test_nothing)) == 0);
    return 0;
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
    msl_test_gauge_free = msl_test_copied_free = 0;
    msl_test_spent_free = msl_test_walls_free = msl_test_judge_free = 0;
    msl_test_want_spent = msl_test_reborn_spent = msl_test_follower_seen = 0;
    msl_test_landed_spent = -1;
    // Mr. Game & Watch enters with the game's starting pair, 1 and 0.
    msl_test_want_judge[0] = character == FTKIND_GAMEWATCH ? 1 : MSL_JUDGE_NONE;
    msl_test_want_judge[1] = character == FTKIND_GAMEWATCH ? 0 : MSL_JUDGE_NONE;
    msl_test_want_copied = MSL_COPIED_NONE;
    msl_test_want[0] = msl_test_want[1] = msl_test_reborn[0] = msl_test_reborn[1] = 0;
    // Bowser enters with his Fire Breath full; everyone else with nothing.
    msl_test_gauge_free = character == FTKIND_KOOPA;
    for (unsigned i = 0; i < 150; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0 && msl_test_charge == 0);
    }
    msl_test_gauge_free = 0;
    msl_test_want[0] = msl_test_reborn[0] = msl_test_gauge[0];
    msl_test_want[1] = msl_test_reborn[1] = msl_test_gauge[1];
    return 0;
}

// `frames` frames of `input`, the value staying at `charge` on every one.
static int msl_test_hold(const MslCoreInput* input, unsigned frames, int charge)
{
    for (unsigned i = 0; i < frames; ++i) {
        MSL_TEST_CHECK(msl_test_frame(input) == 0 && msl_test_charge == charge);
    }
    return 0;
}

// Holds `input` until the value is `target`: it may only stay or go up by one.
static int msl_test_rise(const MslCoreInput* input, int target, unsigned limit)
{
    for (unsigned i = 0; msl_test_charge != target; ++i) {
        int before = msl_test_charge;
        MSL_TEST_CHECK(i < limit);
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
        MSL_TEST_CHECK(msl_test_charge == before || msl_test_charge == before + 1);
    }
    return 0;
}

// Holds `input` until port 0 is in `motion`, the value staying at `charge`.
static int msl_test_until(const MslCoreInput* input, int motion, unsigned limit, int charge)
{
    for (unsigned i = 0; (int) msl_test_fighter(0)->motion_id != motion; ++i) {
        MSL_TEST_CHECK(i < limit);
        MSL_TEST_CHECK(msl_test_frame(input) == 0 && msl_test_charge == charge);
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
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id ==
                   (msl_test_fighter(0)->kind == FTKIND_GAMEWATCH ? ftGw_MS_Attack11
                                                                 : ftCo_MS_Attack11));
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, charge) == 0);
    return 0;
}

// Past the left blast zone: the stock goes at once, the value stays `dead`
// through the death animation and is `reborn` from the first frame of
// Rebirth, where the game runs the fighter's OnDeath. The gauges stay as they
// were through the death animation and read msl_test_reborn from Rebirth.
static int msl_test_ko(int dead, int reborn)
{
    Fighter* fp = msl_test_fighter(0);
    int stocks = msl_test_match.source.player.slots[fp->player_id].stocks;
    fp->cur_pos = (Vec3) { -300, 40, 0 };
    fp->prev_pos = fp->cur_pos;
    ftCommon_8007D5D4(fp);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, dead) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftCo_MS_DeadLeft &&
                   msl_test_match.source.player.slots[fp->player_id].stocks == stocks - 1);
    msl_test_gauge_free = msl_test_spent_free = msl_test_judge_free = 1;
    for (unsigned i = 0; fp->motion_id != ftCo_MS_Rebirth; ++i) {
        const float* gauge = fp->motion_id == ftCo_MS_DeadLeft ? msl_test_want : msl_test_reborn;
        int first;
        MSL_TEST_CHECK(i < 120);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        gauge = fp->motion_id == ftCo_MS_DeadLeft ? msl_test_want : msl_test_reborn;
        MSL_TEST_CHECK(msl_test_charge == (fp->motion_id == ftCo_MS_DeadLeft ? dead : reborn));
        MSL_TEST_CHECK(msl_test_gauge[0] == gauge[0] && msl_test_gauge[1] == gauge[1]);
        // What was spent stays through the death animation; the respawn
        // gives back what the fighter's OnDeath clears. Judge's pair starts
        // again at 1 and 0.
        MSL_TEST_CHECK(msl_test_spent == (fp->motion_id == ftCo_MS_DeadLeft
                                              ? msl_test_want_spent : msl_test_reborn_spent));
        first = fp->kind == FTKIND_GAMEWATCH && fp->motion_id != ftCo_MS_DeadLeft;
        MSL_TEST_CHECK(msl_test_judge[0] == (first ? 1 : msl_test_want_judge[0]) &&
                       msl_test_judge[1] == (first ? 0 : msl_test_want_judge[1]));
    }
    msl_test_gauge_free = msl_test_spent_free = msl_test_judge_free = 0;
    msl_test_want_spent = msl_test_reborn_spent;
    if (fp->kind == FTKIND_GAMEWATCH) {
        msl_test_want_judge[0] = 1;
        msl_test_want_judge[1] = 0;
    }
    msl_test_want[0] = msl_test_reborn[0];
    msl_test_want[1] = msl_test_reborn[1];
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_RebirthWait, 300, reborn) == 0);
    for (unsigned i = 0; fp->motion_id != ftCo_MS_Wait; ++i) {
        MSL_TEST_CHECK(i < 600);
        msl_test_spent_free = msl_test_landed_spent >= 0;
        MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, reborn) == 0);
        msl_test_spent_free = 0;
        if (msl_test_landed_spent >= 0 && fp->ground_or_air == GA_Ground) {
            msl_test_want_spent = msl_test_landed_spent;
        }
        MSL_TEST_CHECK(msl_test_spent == msl_test_want_spent);
    }
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
    MSL_TEST_CHECK(msl_test_ko(2, 0) == 0);
    fprintf(stderr, "giant punch: full at %d swings\n", full);
    return 0;
}

static int msl_test_setup_in_range(unsigned character, unsigned opponent)
{
    Fighter* fp;
    MSL_TEST_CHECK(msl_test_setup(character, opponent) == 0);
    fp = msl_test_fighter(0);
    fp->cur_pos = (Vec3) { -20, 0, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = 1;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    return 0;
}

// One of the opponent's shots from the right (Falco's laser, Mario's
// fireball), caught in the bucket: the count goes up by one and gauge[0] by
// what that shot would have dealt, on the same frame. Returns that damage.
static int msl_test_catch(int count)
{
    Vec3 pos = { 30, 8, 0 };
    float before = msl_test_want[0];
    if (msl_test_fighter(1)->kind == FTKIND_MARIO) {
        pos.x = 0;
        it_8029B6F8(msl_test_match.fighters[1], &pos, It_Kind_Mario_Fire, -1);
    } else {
        it_8029C6A4(3.14159265358979323846F, 4, msl_test_match.fighters[1], &pos,
                    It_Kind_Falco_Laser);
    }
    msl_test_gauge_free = 1;
    for (unsigned i = 0; msl_test_charge != count; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_down_b) == 0);
        MSL_TEST_CHECK(msl_test_gauge[1] == 0);
        MSL_TEST_CHECK(msl_test_charge == count ? msl_test_gauge[0] > before
                                                : msl_test_charge == count - 1 &&
                                                      msl_test_gauge[0] == before);
    }
    msl_test_gauge_free = 0;
    msl_test_want[0] = msl_test_gauge[0];
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 50, count) == 0);
    return (int) (msl_test_want[0] - before);
}

// A player without stocks is absent from the observation, and so is the
// count the game still keeps for him.
static int msl_test_absent_after_last_stock(void)
{
    Fighter* fp;
    int absent = 0;
    msl_test_stocks = 1;
    MSL_TEST_CHECK(msl_test_setup_in_range(FTKIND_GAMEWATCH, MSL_TEST_FALCO) == 0);
    msl_test_stocks = 4;
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 20, 0) == 0);
    MSL_TEST_CHECK(msl_test_catch(1) > 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 1) == 0);
    fp->cur_pos = (Vec3) { -300, 40, 0 };
    fp->prev_pos = fp->cur_pos;
    ftCommon_8007D5D4(fp);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 300; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_charge == (msl_test_present ? 1 : 0));
        absent += !msl_test_present;
    }
    MSL_TEST_CHECK(absent > 100 && !msl_test_present && fp->fv.gw.x2238_panicCharge == 1 &&
                   fp->fv.gw.x223C_panicDamage > 0);
    return 0;
}

// Fire Breath's two gauges, for Bowser or for Kirby wearing his hat: full at
// rest, each down by exactly 1 on every frame of breath to its floor, held
// there while the breath goes on, and back up by its own rate on every frame
// once the move is over, to full. full, floor and rate are the loaded
// attributes. Returns the frames the recovery took.
static int msl_test_breathe(const float full[2], const float floor[2], const float rate[2],
                            unsigned breath)
{
    float prev[2];
    int moving = 0, frames = 0;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_gauge[0] == full[0] && msl_test_gauge[1] == full[1]);
    msl_test_gauge_free = 1;
    for (unsigned i = 0; i < breath; ++i) {
        int changed = 0;
        prev[0] = msl_test_gauge[0];
        prev[1] = msl_test_gauge[1];
        MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0 && msl_test_charge == 0);
        for (int g = 0; g < 2; ++g) {
            float down = prev[g] - 1.0F < floor[g] ? floor[g] : prev[g] - 1.0F;
            MSL_TEST_CHECK(msl_test_gauge[g] == down || (!moving && msl_test_gauge[g] == prev[g]));
            changed |= msl_test_gauge[g] != prev[g];
        }
        // Both start on the same frame and neither pauses before its floor.
        MSL_TEST_CHECK(!changed || (msl_test_gauge[0] != full[0] && msl_test_gauge[1] != full[1]));
        moving |= changed;
    }
    MSL_TEST_CHECK(moving);
    moving = 0;
    for (unsigned i = 0; msl_test_gauge[0] != full[0] || msl_test_gauge[1] != full[1]; ++i) {
        int changed = 0;
        MSL_TEST_CHECK(i < 3000);
        prev[0] = msl_test_gauge[0];
        prev[1] = msl_test_gauge[1];
        // A short hop on the way: the recovery does not wait for it.
        MSL_TEST_CHECK(msl_test_frame(moving > 20 && moving < 23 ? &msl_test_jump
                                                               : &msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_charge == 0);
        for (int g = 0; g < 2; ++g) {
            float up = prev[g] + rate[g] > full[g] ? full[g] : prev[g] + rate[g];
            float down = prev[g] - 1.0F < floor[g] ? floor[g] : prev[g] - 1.0F;
            // The breath goes on draining until it stops and the move's
            // ending holds the value; after the move, only up.
            MSL_TEST_CHECK(msl_test_gauge[g] == up ||
                           (!moving && (msl_test_gauge[g] == down || msl_test_gauge[g] == prev[g])));
            changed |= msl_test_gauge[g] > prev[g];
        }
        moving += moving || changed;
        frames += moving != 0;
    }
    msl_test_gauge_free = 0;
    msl_test_want[0] = msl_test_reborn[0] = full[0];
    msl_test_want[1] = msl_test_reborn[1] = full[1];
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
        const float full[2] = { da->x10, da->x18 };
        const float floor[2] = { da->x14, da->x1C };
        const float rate[2] = { da->x8, da->xC };
        MSL_TEST_CHECK(full[0] > floor[0] + 20 && full[1] > floor[1] + 20 && rate[0] > 0 &&
                       rate[1] > 0);
        MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
        // A short breath, then a long one that reaches both floors and stays.
        MSL_TEST_CHECK(msl_test_breathe(full, floor, rate, 40) > 0);
        MSL_TEST_CHECK(msl_test_gauge[0] == full[0] && fp->motion_id == ftCo_MS_Wait);
        frames = msl_test_breathe(full, floor, rate, 400);
        MSL_TEST_CHECK(frames > 0);
        fprintf(stderr, "fire breath: fuel %g..%g +%g a frame, size %g..%g +%g a frame, "
                        "%d frames back to full\n",
                floor[0], full[0], rate[0], floor[1], full[1], rate[1], frames);

        // Drained to the floors, then a KO: the respawn makes both full at
        // once (ftKp_Init_OnDeath).
        msl_test_gauge_free = 1;
        for (unsigned i = 0; i < 400; ++i) {
            MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
        }
        MSL_TEST_CHECK(msl_test_gauge[0] == floor[0] && msl_test_gauge[1] == floor[1]);
        fp->cur_pos = (Vec3) { -300, 40, 0 };
        fp->prev_pos = fp->cur_pos;
        ftCommon_8007D5D4(fp);
        ftCo_Fall_Enter(msl_test_match.fighters[0]);
        for (unsigned i = 0; fp->motion_id != ftCo_MS_Rebirth; ++i) {
            float before = msl_test_gauge[0];
            MSL_TEST_CHECK(i < 120);
            MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
            if (fp->motion_id == ftCo_MS_Rebirth) {
                MSL_TEST_CHECK(before < full[0] - 1);
            } else {
                MSL_TEST_CHECK(fp->motion_id == ftCo_MS_DeadLeft && msl_test_gauge[0] < full[0]);
            }
        }
        MSL_TEST_CHECK(msl_test_gauge[0] == full[0] && msl_test_gauge[1] == full[1]);
        msl_test_gauge_free = 0;
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 900, 0) == 0);
    }
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
    full = kind == FTKIND_SAMUS ? (int) da->specialn_ss_charge_time :
           kind == FTKIND_MEWTWO ? (int) da->specialn_mt_charge_time : 6;
    msl_test_want_copied = kind;
    ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], kind, false);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
    MSL_TEST_CHECK(msl_test_rise(charge, 3, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(shield, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);
    MSL_TEST_CHECK((kind == FTKIND_SAMUS ? fp->fv.kb.xA8 :
                    kind == FTKIND_MEWTWO ? fp->fv.kb.x9C : fp->fv.kb.xB4) == 3);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_rise(charge, full, 900) == 0);
    MSL_TEST_CHECK(msl_test_hold(charge, 30, full) == 0);
    MSL_TEST_CHECK(msl_test_hold(shield, 1, full) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);
    // Kirby's death callback takes the hat, and the copy's count with it, on
    // the frame of the KO.
    msl_test_want_copied = MSL_COPIED_NONE;
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
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

// One aerial use of a special: `press` for a frame, then `tap` on every other
// frame, to the end of the move's motion state. On every frame the spent bits
// are `was` or `was` with `bit`, never back; `bit` is set by the end. *top is
// the highest upward speed of the fighter's own during the move, and *rose the
// frames on which that speed went up.
static int msl_test_aerial_special(const MslCoreInput* press, const MslCoreInput* tap, int bit,
                                   int was, float* top, int* rose)
{
    Fighter* fp = msl_test_fighter(0);
    int spent = was, move;
    *top = -1000;
    *rose = 0;
    MSL_TEST_CHECK(fp->ground_or_air == GA_Air && fp->self_vel.y <= 0);
    msl_test_spent_free = 1;
    MSL_TEST_CHECK(msl_test_air_frame(press) == 0);
    move = fp->motion_id;
    MSL_TEST_CHECK(move >= ftCo_MS_Count);
    for (unsigned i = 0; (int) fp->motion_id == move; ++i) {
        float before = fp->self_vel.y;
        MSL_TEST_CHECK(msl_test_spent == spent || (spent == was && msl_test_spent == (was | bit)));
        spent = msl_test_spent;
        *top = before > *top ? before : *top;
        MSL_TEST_CHECK(i < 300);
        MSL_TEST_CHECK(msl_test_air_frame(i % 2 ? &msl_test_neutral : tap) == 0);
        *rose += (int) fp->motion_id == move && fp->self_vel.y > before;
    }
    MSL_TEST_CHECK(msl_test_spent == (was | bit));
    msl_test_spent_free = 0;
    msl_test_want_spent = was | bit;
    return msl_test_air_until(ftCo_MS_Fall);
}

// Falls with `input` held until the fighter stands on the ground, which must
// be in `landing`; the spent bits are `was` in the air and `after` from the
// first frame on the ground, and stay so until the fighter is standing.
static int msl_test_land(const MslCoreInput* input, int landing, int was, int after)
{
    Fighter* fp = msl_test_fighter(0);
    for (unsigned i = 0; fp->ground_or_air == GA_Air; ++i) {
        MSL_TEST_CHECK(i < 400);
        msl_test_spent_free = 1;
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
        msl_test_spent_free = 0;
        MSL_TEST_CHECK(msl_test_spent == (fp->ground_or_air == GA_Air ? was : after));
    }
    MSL_TEST_CHECK((int) fp->motion_id == landing);
    msl_test_want_spent = after;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
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
    int rose;
    MSL_TEST_CHECK(msl_test_setup(kind, MSL_TEST_FALCO) == 0);
    if (hat != FTKIND_NONE) {
        msl_test_want_copied = hat;
        ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], hat, false);
    }
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);

    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, &msl_test_neutral, bit, 0, &first, &rose) == 0);
    MSL_TEST_CHECK(first > 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, &msl_test_neutral, bit, bit, &second, &rose) == 0);
    MSL_TEST_CHECK(second <= 0);
    MSL_TEST_CHECK(msl_test_hit_in_air() == 0);
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);

    // Used again, then a neutral air attack into the floor.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, &msl_test_neutral, bit, 0, &again, &rose) == 0);
    MSL_TEST_CHECK(again == first);
    MSL_TEST_CHECK(msl_test_land_in_lag(&msl_test_a, bit, bit) == 0);
    // Still spent after standing and a jab; the next use is from a jump.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_a, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 0) == 0);
    MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, &msl_test_neutral, bit, bit, &stale, &rose) == 0);
    MSL_TEST_CHECK(stale <= 0);
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);

    // Spent once more, then a KO. The KO takes Kirby's hat, and the copied
    // move's bit with it, on its own frame.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(press, &msl_test_neutral, bit, 0, &again, &rose) == 0);
    MSL_TEST_CHECK(again == first);
    msl_test_want_copied = MSL_COPIED_NONE;
    msl_test_want_spent = hat != FTKIND_NONE ? 0 : bit;
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    fprintf(stderr, "%s: rises at %g a frame, then %g; %g after a landing in landing lag\n", name,
            first, second, stale);
    return 0;
}

// Mr. Game & Watch's Judge: the game leaves the last two numbers out of the
// next roll. judge[0] is the hammer the move shows (the motion state is the
// first hammer's plus the number), judge[1] the one before; every new number
// differs from both. The match starts, and a new stock starts, with 1 and 0,
// so the first hammer is never a 1 or a 2. Every fourth roll is in the air:
// the game has a once-per-airtime variable for an aerial Judge's lift, which
// no hammer's script ever sets, so there is no bit for it.
static int msl_test_judge_history(void)
{
    Fighter* fp;
    int seen[9] = { 0 }, aerial_seen[9] = { 0 }, kinds = 0, aerial_kinds = 0;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_GAMEWATCH, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    for (int stock = 0; stock < 3; ++stock) {
        MSL_TEST_CHECK(msl_test_judge[0] == 1 && msl_test_judge[1] == 0);
        for (int roll = 0; roll < 60; ++roll) {
            int last = msl_test_judge[0], before = msl_test_judge[1], now;
            int aerial = roll % 4 == 3;
            if (aerial) {
                msl_test_air(60);
                MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 2, 0) == 0);
            }
            msl_test_judge_free = 1;
            MSL_TEST_CHECK(msl_test_hold(&msl_test_side_b, 1, 0) == 0);
            now = msl_test_judge[0];
            MSL_TEST_CHECK(now >= 0 && now < 9 && now != last && now != before &&
                           msl_test_judge[1] == last);
            MSL_TEST_CHECK((int) fp->motion_id ==
                           (aerial ? ftGw_MS_SpecialAirS1 : ftGw_MS_SpecialS1) + now);
            msl_test_want_judge[0] = now;
            msl_test_want_judge[1] = last;
            msl_test_judge_free = 0;
            kinds += !seen[now]++;
            aerial_kinds += aerial && !aerial_seen[now]++;
            for (unsigned i = 0; fp->motion_id != ftCo_MS_Wait; ++i) {
                MSL_TEST_CHECK(i < 400);
                MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
                MSL_TEST_CHECK(fp->fv.gw.x2234 == 0);
            }
            MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 2, 0) == 0);
        }
        MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
        MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    }
    MSL_TEST_CHECK(kinds == 9 && aerial_kinds == 9);
    fprintf(stderr, "judge: 180 rolls, none repeating either of the two before it; hammers 1 to 9 "
                    "came up %d %d %d %d %d %d %d %d %d times\n",
            seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7], seen[8]);
    return 0;
}

// Wall jumps on Yoshi's Story, against the wall under the left edge: the
// count goes up by one with each, every jump after the first rises less
// (the game multiplies by a base to the power of the count), and standing on
// the ground clears it.
static int msl_test_wall_jumps(unsigned kind, const char* name)
{
    Fighter* fp;
    float speed[3] = { 0 };
    int jumps = 0, hug = 0;
    msl_test_stage = 8;
    MSL_TEST_CHECK(msl_test_setup(kind, MSL_TEST_FALCO) == 0);
    msl_test_stage = 32;
    fp = msl_test_fighter(0);
    // Off the starting platform with a jump, then next to the wall.
    MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
    fp->cur_pos = (Vec3) { -100, 60, 0 };
    fp->prev_pos = fp->cur_pos;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    fp->cur_pos = (Vec3) { -100, -40, 0 };
    fp->prev_pos = fp->cur_pos;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    msl_test_walls_free = 1;
    for (unsigned i = 0; jumps < 3; ++i) {
        int jumping = fp->motion_id == ftCo_MS_PassiveWallJump;
        int walled = (fp->coll_data.env_flags & (Collide_LeftWallHug | Collide_RightWallHug)) != 0;
        int before = msl_test_walls;
        MSL_TEST_CHECK(i < 600 && fp->cur_pos.y > -80 && fp->ground_or_air == GA_Air);
        if (jumping && fp->cur_pos.y < -55) {
            // Kept up while the jump plays out, well away from the wall.
            fp->cur_pos.y += 30;
            fp->prev_pos = fp->cur_pos;
        } else if (!jumping && fp->cur_pos.x < -66) {
            // Next to the wall, below the reach of the ledge.
            fp->cur_pos = (Vec3) { -62, -40, 0 };
            fp->prev_pos = fp->cur_pos;
            fp->self_vel.x = fp->self_vel.y = 0;
        }
        hug = walled && !jumping ? hug + 1 : 0;
        // Into the wall, then away from it once it is touched.
        MSL_TEST_CHECK(msl_test_frame(jumping ? &msl_test_neutral
                                              : hug >= 2 ? &msl_test_left : &msl_test_right) == 0);
        MSL_TEST_CHECK(msl_test_walls == before || msl_test_walls == before + 1);
        MSL_TEST_CHECK(msl_test_walls == fp->x1969_walljumpUsed && msl_test_walls <= jumps + 1);
        if (fp->motion_id == ftCo_MS_PassiveWallJump && fp->self_vel.y > speed[msl_test_walls - 1]) {
            speed[msl_test_walls - 1] = fp->self_vel.y;
        }
        jumps = fp->motion_id == ftCo_MS_PassiveWallJump ? jumps : msl_test_walls;
    }
    MSL_TEST_CHECK(msl_test_walls == 3 && speed[0] > 0 && speed[1] > 0 && speed[2] > 0);
    MSL_TEST_CHECK(speed[1] < speed[0] && speed[2] < speed[1]);
    // Back onto the stage: none from the first frame on the ground.
    fp->cur_pos = (Vec3) { 0, 30, 0 };
    fp->prev_pos = fp->cur_pos;
    for (unsigned i = 0; fp->ground_or_air == GA_Air; ++i) {
        MSL_TEST_CHECK(i < 200);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_walls == (fp->ground_or_air == GA_Air ? 3 : 0));
    }
    msl_test_walls_free = 0;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    fprintf(stderr, "%s: three wall jumps rising at %g, %g and %g a frame\n", name, speed[0],
            speed[1], speed[2]);
    return 0;
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    // One scenario per observed field: charge, gauge, copied_char, spent,
    // wall_jumps and judge, and a player with no stocks left reads empty.
    result = msl_test_giant_punch() || msl_test_fire_breath() ||
             msl_test_kirby_copy(FTKIND_SAMUS) ||
             msl_test_special_lift(FTKIND_MARIO, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "mario's cape") ||
             msl_test_wall_jumps(MSL_TEST_FOX, "fox") || msl_test_judge_history() ||
             msl_test_absent_after_last_stock();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
