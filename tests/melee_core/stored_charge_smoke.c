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

// A laser from the right, followed with `input` held until it hurts port 0.
// Returns the value on the frame the damage lands; before it the value must
// be `charge` or one more (a charge in progress).
static int msl_test_laser(const MslCoreInput* input, int charge)
{
    Fighter* fp = msl_test_fighter(0);
    float percent = fp->dmg.x1830_percent;
    Vec3 pos = { 30, 8, 0 };
    it_8029C6A4(3.14159265358979323846F, 4, msl_test_match.fighters[1], &pos,
                It_Kind_Falco_Laser);
    for (unsigned i = 0; i < 60; ++i) {
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
        if (fp->dmg.x1830_percent > percent) {
            return msl_test_charge;
        }
        MSL_TEST_CHECK(msl_test_charge == charge || msl_test_charge == charge + 1);
    }
    MSL_TEST_CHECK(!"the laser never landed");
    return -1;
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

// A hit leaves a stored count alone, and takes it when it lands during the
// wind-up (the move's own damage callback clears it).
static int msl_test_hit_during_wind_up(void)
{
    Fighter* fp;
    MSL_TEST_CHECK(msl_test_setup_in_range(FTKIND_DONKEY, MSL_TEST_FALCO) == 0);
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 1, 200) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 1) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 60) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 2) == 0);
    MSL_TEST_CHECK(msl_test_laser(&msl_test_neutral, 2) == 2);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 2) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 3, 200) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftDk_MS_SpecialNLoop);
    MSL_TEST_CHECK(msl_test_laser(&msl_test_neutral, 3) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    return 0;
}

// A count stored by one move is also lost to a hit taken during another move
// of the same fighter, where that move installs the damage callback that
// clears it. The best known case is NTSC Melee's Donkey Kong, who loses his
// Giant Punch when he is hit out of his up special: ftDk_SpecialHi.c sets
// take_dmg_cb to ftDk_Init_8010D774, which clears the count. Samus's up
// special and Sheik's chain do the same to theirs, and Mewtwo's Disable to a
// Shadow Ball that is not full. The observation shows it because it reads the
// game's variable: `stored` up to the hit, `after` from the frame it lands.
static int msl_test_hit_during_other_move(FighterKind kind, int full, const MslCoreInput* move,
                                          const MslCoreInput* held, int motion, int after)
{
    const MslCoreInput* charge = kind == FTKIND_SEAK ? &msl_test_b : &msl_test_neutral;
    const MslCoreInput* shield = kind == FTKIND_SEAK ? &msl_test_shield_b : &msl_test_shield;
    Fighter* fp;
    Vec3 pos;
    float percent;
    int stored = full ? full : 2, hit = 0;
    MSL_TEST_CHECK(msl_test_setup_in_range(kind, MSL_TEST_FALCO) == 0);
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
    if (kind == FTKIND_DONKEY && !full) {
        // The shield is taken at the top of the next swing, which counts.
        MSL_TEST_CHECK(msl_test_rise(charge, 1, 300) == 0);
        MSL_TEST_CHECK(msl_test_hold(shield, 1, 1) == 0);
        MSL_TEST_CHECK(msl_test_rise(charge, 2, 60) == 0);
    } else {
        MSL_TEST_CHECK(msl_test_rise(charge, stored, 900) == 0);
        MSL_TEST_CHECK(msl_test_hold(shield, 1, stored) == 0);
    }
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, stored) == 0);

    // Into the other move, to the state that installs the callback (for
    // Mewtwo, once Disable's spark is out); then a laser down on the head.
    percent = fp->dmg.x1830_percent;
    MSL_TEST_CHECK(msl_test_hold(move, 1, stored) == 0);
    for (unsigned i = 0; (int) fp->motion_id != motion ||
                         (kind == FTKIND_MEWTWO && fp->fv.mt.x222C_disableGObj == NULL); ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_hold(held, 1, stored) == 0);
    }
    pos = (Vec3) { fp->cur_pos.x, fp->cur_pos.y + 60, 0 };
    it_8029C6A4(-1.57079632679489661923F, 4, msl_test_match.fighters[1], &pos,
                It_Kind_Falco_Laser);
    for (unsigned i = 0; !hit; ++i) {
        int in_move = (int) fp->motion_id == motion;
        MSL_TEST_CHECK(i < 40);
        MSL_TEST_CHECK(msl_test_frame(held) == 0);
        hit = fp->dmg.x1830_percent > percent;
        // The hit lands while the other move is still going.
        MSL_TEST_CHECK(msl_test_charge == (hit ? after : stored) && in_move);
    }
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 300, after) == 0);
    return 0;
}

// The same for Charge Shot, Needle Storm and Shadow Ball: a hit during the
// charge takes the count. Mewtwo's damage callback spares a full one.
static int msl_test_hit_during_charge(FighterKind kind)
{
    const MslCoreInput* charge = kind == FTKIND_SEAK ? &msl_test_b : &msl_test_neutral;
    MSL_TEST_CHECK(msl_test_setup_in_range(kind, MSL_TEST_FALCO) == 0);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_b) == 0);
    MSL_TEST_CHECK(msl_test_rise(charge, 2, 300) == 0);
    MSL_TEST_CHECK(msl_test_laser(charge, 2) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    if (kind == FTKIND_MEWTWO) {
        Fighter* fp = msl_test_fighter(0);
        int full = (int) ((ftMewtwoAttributes*) fp->dat_attrs)->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES;
        MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
        MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, full, 900) == 0);
        MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNLoopFull);
        MSL_TEST_CHECK(msl_test_laser(&msl_test_neutral, full) == full);
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, full) == 0);
    }
    return 0;
}

// Charge Shot counts charge steps. A shield out of the charge keeps the
// count, charging again goes on from it to the full count, where the game
// ends the charge itself, and the shot takes it.
static int msl_test_charge_shot(void)
{
    Fighter* fp;
    int full;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_SAMUS, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    full = (int) ((ftSs_DatAttrs*) fp->dat_attrs)->x18;
    MSL_TEST_CHECK(full > 4 && full <= UINT8_MAX);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSs_MS_SpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 3, 200) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSs_MS_SpecialNHold);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 3) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSs_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, full, 600) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSs_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);

    // The shot: full until it leaves the cannon, none from that frame.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, full) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftSs_MS_SpecialN, 60, full) == 0);
    for (unsigned i = 0; msl_test_charge != 0; ++i) {
        MSL_TEST_CHECK(i < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_charge == full || msl_test_charge == 0);
        MSL_TEST_CHECK(fp->motion_id == ftSs_MS_SpecialN);
    }
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // Two steps stored, then a KO.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 200) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 2) == 0);
    MSL_TEST_CHECK(msl_test_ko(2, 0) == 0);
    fprintf(stderr, "charge shot: full at %d steps\n", full);
    return 0;
}

// Needle Storm counts needles in hand, 1 from the start of the move and at
// most 6. A shield out of the charge keeps them; the throw takes them one at
// a time.
static int msl_test_needles(void)
{
    Fighter* fp;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_SEAK, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 1) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSk_MS_SpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_b, 4, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield_b, 1, 4) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSk_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_other_actions(4) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 4) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_b, 6, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 120, 6) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSk_MS_SpecialNLoop);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield_b, 1, 6) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(6) == 0);

    // The throw: release B in the charge loop.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 6) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_b, ftSk_MS_SpecialNLoop, 60, 6) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 6) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftSk_MS_SpecialNEnd);
    for (unsigned i = 0; msl_test_charge != 0; ++i) {
        int before = msl_test_charge;
        MSL_TEST_CHECK(i < 120);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_charge == before || msl_test_charge == before - 1);
        MSL_TEST_CHECK(fp->motion_id == ftSk_MS_SpecialNEnd);
    }
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // Two needles stored, then a KO.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 1) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_b, 2, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield_b, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 2) == 0);
    MSL_TEST_CHECK(msl_test_ko(2, 0) == 0);
    return 0;
}

// Shadow Ball counts charge cycles: kept through a shield out of the charge,
// continued to the full count, taken by the throw.
static int msl_test_shadow_ball(void)
{
    Fighter* fp;
    int full;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_MEWTWO, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    full = (int) ((ftMewtwoAttributes*) fp->dat_attrs)->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES;
    MSL_TEST_CHECK(full > 4 && full <= UINT8_MAX);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 3, 300) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNLoop);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 3) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, full, 900) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNLoopFull);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30, full) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, full) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, full) == 0);
    for (unsigned i = 0; msl_test_charge != 0; ++i) {
        MSL_TEST_CHECK(i < 120);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_charge == full || msl_test_charge == 0);
    }
    MSL_TEST_CHECK(fp->motion_id == ftMt_MS_SpecialNEnd);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // Two cycles stored, then a KO: Mewtwo's own death callback, left by the
    // move, takes them on the frame of the KO.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 300) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 2) == 0);
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    fprintf(stderr, "shadow ball: full at %d cycles\n", full);
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

// Oil Panic counts caught shots, full at 3, and keeps beside the count the
// damage those shots would have dealt: gauge[0]. Both are kept through other
// actions; the spill takes both and deals what ftGw_SpecialLwShoot_ReleaseOil
// makes of the damage. A KO keeps the count and clears the damage
// (ftGw_Init_OnDeath), so the next spill deals the attribute's flat amount
// alone. Returns the damage of one of the opponent's shots.
static int msl_test_oil_panic(unsigned opponent)
{
    Fighter* fp;
    ftGameWatchAttributes* da;
    int shot, spill;
    MSL_TEST_CHECK(msl_test_setup_in_range(FTKIND_GAMEWATCH, opponent) == 0);
    fp = msl_test_fighter(0);
    da = fp->dat_attrs;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 20, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftGw_MS_SpecialLw);
    shot = msl_test_catch(1);
    MSL_TEST_CHECK(shot > 0 && msl_test_catch(2) == shot && msl_test_catch(3) == shot);
    MSL_TEST_CHECK(msl_test_want[0] == 3 * shot && fp->fv.gw.x223C_panicDamage == 3 * shot);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);

    spill = (int) ((int) (msl_test_want[0] * da->x78_GAMEWATCH_PANIC_DAMAGE_MUL) +
                   da->x74_GAMEWATCH_PANIC_DAMAGE_ADD);
    msl_test_want[0] = 0;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftGw_MS_SpecialLwShoot && (int) fp->cmd_vars[1] == spill);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    fprintf(stderr, "oil panic: three shots of %d, spill %d = %d x %g + %g\n", shot, spill,
            3 * shot, da->x78_GAMEWATCH_PANIC_DAMAGE_MUL, da->x74_GAMEWATCH_PANIC_DAMAGE_ADD);

    // Filled again, then a KO: 3 and the damage through the death animation,
    // 3 and no damage from the respawn.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 20, 0) == 0);
    MSL_TEST_CHECK(msl_test_catch(1) == shot && msl_test_catch(2) == shot &&
                   msl_test_catch(3) == shot);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120, 3) == 0);
    MSL_TEST_CHECK(msl_test_ko(3, 3) == 0);
    MSL_TEST_CHECK(msl_test_charge == 3 && msl_test_gauge[0] == 0);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftGw_MS_SpecialLwShoot &&
                   (int) fp->cmd_vars[1] == (int) da->x74_GAMEWATCH_PANIC_DAMAGE_ADD);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    return shot;
}

// The same count of 3 holds different damage after Falco's lasers and after
// Mario's fireballs.
static int msl_test_oil_panic_damage(void)
{
    int lasers = msl_test_oil_panic(MSL_TEST_FALCO);
    int fireballs = lasers > 0 ? msl_test_oil_panic(MSL_TEST_MARIO) : -1;
    MSL_TEST_CHECK(lasers > 0 && fireballs > 0 && lasers != fireballs);
    return 0;
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

// Kirby wearing Bowser's hat keeps his own pair, with his own attributes; it
// goes with the hat.
static int msl_test_kirby_fire_breath(void)
{
    Fighter* fp;
    ftKb_DatAttrs* da;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KIRBY, MSL_TEST_FOX) == 0);
    fp = msl_test_fighter(0);
    da = fp->dat_attrs;
    {
        const float full[2] = { da->specialn_kp_max_fuel, da->specialn_kp_flame_scale };
        const float floor[2] = { da->specialn_kp_spew_flame_velocity,
                                 da->specialn_kp_lowest_charge_graphic_size };
        const float rate[2] = { da->specialn_kp_fuel_recharge_rate,
                                da->specialn_kp_flame_size_recharge_rate };
        int frames;
        MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
        msl_test_want[0] = full[0];
        msl_test_want[1] = full[1];
        msl_test_want_copied = FTKIND_KOOPA;
        ftKb_SpecialN_800F1BAC(msl_test_match.fighters[0], FTKIND_KOOPA, false);
        frames = msl_test_breathe(full, floor, rate, 400);
        MSL_TEST_CHECK(frames > 0);
        fprintf(stderr, "kirby's fire breath: fuel %g..%g +%g a frame, size %g..%g +%g a frame, "
                        "%d frames back to full\n",
                floor[0], full[0], rate[0], floor[1], full[1], rate[1], frames);
        // The taunt throws the hat away, and the gauges with it.
        msl_test_want[0] = msl_test_want[1] = 0;
        msl_test_want_copied = MSL_COPIED_NONE;
        MSL_TEST_CHECK(msl_test_hold(&msl_test_taunt, 1, 0) == 0);
        MSL_TEST_CHECK(fp->fv.kb.hat.kind == FTKIND_KIRBY);
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    }
    return 0;
}

// Kirby, standing next to the opponent, inhales and swallows him. The copied
// ability is none on every frame until the hat arrives and the opponent's
// character, as the observation gives it in his own slot, from that frame.
static int msl_test_swallow(void)
{
    Fighter* fp = msl_test_fighter(0);
    Fighter* opponent = msl_test_fighter(1);
    MslCoreObservation observation;
    int arrived = -1;
    fp->cur_pos = (Vec3) { 0, 0, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = 1;
    opponent->cur_pos = (Vec3) { 10, 0, 0 };
    opponent->prev_pos = opponent->cur_pos;
    opponent->facing_dir = -1;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    msl_test_copied_free = 1;
    for (unsigned i = 0; i < 300 && (arrived < 0 || fp->motion_id != ftCo_MS_Wait); ++i) {
        // B again, on a press edge inside the wait state, swallows.
        MSL_TEST_CHECK(msl_test_hold(fp->motion_id == ftKb_MS_EatWait && (i % 4) < 2
                                         ? &msl_test_b : &msl_test_neutral, 1, 0) == 0);
        MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 0, &observation) == 0);
        if (arrived < 0 && msl_test_copied != MSL_COPIED_NONE) {
            arrived = msl_test_match.frame_id;
        }
        MSL_TEST_CHECK(msl_test_copied ==
                       (arrived < 0 ? MSL_COPIED_NONE : observation.slots[1].char_id));
        MSL_TEST_CHECK((arrived >= 0) == (fp->fv.kb.hat.kind != FTKIND_KIRBY));
    }
    MSL_TEST_CHECK(arrived >= 0 && fp->motion_id == ftCo_MS_Wait);
    msl_test_copied_free = 0;
    msl_test_want_copied = msl_test_copied;
    opponent->cur_pos = (Vec3) { 60, 0, 0 };
    opponent->prev_pos = opponent->cur_pos;
    fprintf(stderr, "kirby: copied fighter %d on frame %d\n", msl_test_copied, arrived);
    return 0;
}

// A copy with nothing to store: Fox's. The ability is observed, the count and
// the gauges stay 0 through firing the copied Blaster, and a KO takes the hat
// on its frame.
static int msl_test_kirby_swallows_fox(void)
{
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KIRBY, MSL_TEST_FOX) == 0);
    MSL_TEST_CHECK(msl_test_swallow() == 0 && msl_test_copied == MSL_TEST_FOX);
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id != ftCo_MS_Wait);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    msl_test_want_copied = MSL_COPIED_NONE;
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    return 0;
}

// Kirby inhales and swallows Donkey Kong, then winds up the copied punch: the
// observed value is the copy's own count, and goes with the hat when a taunt
// throws it away.
static int msl_test_kirby_swallows_donkey_kong(void)
{
    Fighter* fp;
    int full;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_KIRBY, FTKIND_DONKEY) == 0);
    fp = msl_test_fighter(0);
    full = ((ftKb_DatAttrs*) fp->dat_attrs)->specialn_dk_swings_to_full_charge;
    MSL_TEST_CHECK(msl_test_swallow() == 0 && msl_test_copied == FTKIND_DONKEY);

    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id == ftKb_MS_DkSpecialNStart);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 2, 200) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_shield, 1, 2) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, 3, 60) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(3) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_b, 1, 3) == 0);
    MSL_TEST_CHECK(msl_test_rise(&msl_test_neutral, full, 600) == 0);
    MSL_TEST_CHECK(msl_test_other_actions(full) == 0);

    // The taunt throws the hat away on its first frame.
    MSL_TEST_CHECK(fp->fv.kb.hat.kind == FTKIND_DONKEY && fp->fv.kb.xBC == full);
    msl_test_want_copied = MSL_COPIED_NONE;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_taunt, 1, 0) == 0);
    MSL_TEST_CHECK(fp->fv.kb.hat.kind == FTKIND_KIRBY && fp->motion_id != ftCo_MS_Wait);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    fprintf(stderr, "kirby: copied giant punch full at %d swings\n", full);
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

// Mario's and Dr. Mario's tornado and Luigi's cyclone rise while B is tapped,
// once: the game sets the flag part-way through an aerial use, and with it
// set the taps do nothing. Mario's comes back on a plain landing, like the
// other lifts. Luigi's is not among the flags a landing clears, and his
// OnDeath does not clear it either: it stays through landings and a KO until
// a cyclone touches the ground, which one started on the ground does at once.
static int msl_test_tornado(unsigned kind, const char* name)
{
    const int bit = MSL_SPENT_DOWN_LIFT;
    const int luigi = kind == FTKIND_LUIGI;
    const int grounded = luigi ? ftLg_MS_SpecialLw : ftMr_MS_SpecialLw;
    Fighter* fp;
    float first, second, later = 0, back;
    int rose, taps;
    MSL_TEST_CHECK(msl_test_setup(kind, MSL_TEST_FALCO) == 0);
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);

    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_down_b, &msl_test_b, bit, 0, &first,
                                           &taps) == 0);
    MSL_TEST_CHECK(taps > 3 && first > 1);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_down_b, &msl_test_b, bit, bit, &second,
                                           &rose) == 0);
    MSL_TEST_CHECK(rose == 0 && second < first);
    MSL_TEST_CHECK(msl_test_hit_in_air() == 0);
    // A plain landing: Mario's is back, Luigi's is not.
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, luigi ? bit : 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30, 0) == 0);

    if (luigi) {
        // From a jump, tapping: nothing.
        MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
        MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_down_b, &msl_test_b, bit, bit, &later,
                                               &rose) == 0);
        MSL_TEST_CHECK(rose == 0 && later == second);
        MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, bit) == 0);
        // A KO: still spent on the new stock.
        msl_test_reborn_spent = bit;
        MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
        MSL_TEST_CHECK(msl_test_spent == bit && fp->fv.lg.x222C_cycloneCharge);
        // A cyclone started on the ground: the game enters it through the
        // aerial state, which touches the floor at once, so the lift is back
        // on its first frame.
        msl_test_want_spent = 0;
        MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 1, 0) == 0);
        MSL_TEST_CHECK((int) fp->motion_id == grounded && !fp->fv.lg.x222C_cycloneCharge);
        MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    }
    // Spent again for what follows.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_down_b, &msl_test_b, bit, 0, &back,
                                           &rose) == 0);
    MSL_TEST_CHECK(back == first && rose == taps);

    // An aerial one that comes down onto the floor: back from the frame it
    // touches, in the move's grounded state.
    msl_test_air(6);
    msl_test_spent_free = 1;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_down_b, 1, 0) == 0);
    MSL_TEST_CHECK((int) fp->motion_id == grounded + 1 && msl_test_spent == bit);
    for (unsigned i = 0; fp->ground_or_air == GA_Air; ++i) {
        MSL_TEST_CHECK(i < 100);
        MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
        MSL_TEST_CHECK(msl_test_spent == (fp->ground_or_air == GA_Air ? bit : 0));
    }
    MSL_TEST_CHECK((int) fp->motion_id == grounded);
    msl_test_spent_free = 0;
    msl_test_want_spent = 0;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);

    // And it rises again.
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_down_b, &msl_test_b, bit, 0, &back,
                                           &rose) == 0);
    MSL_TEST_CHECK(back == first && rose == taps);
    if (!luigi) {
        MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    }
    fprintf(stderr, "%s: tapped, speeds up on %d frames to %g a frame; spent, on none (%g)\n",
            name, taps, first, second);
    return 0;
}

// The Ice Climbers: Nana keeps her own Ice Shot flag, published as the
// follower's bit of her leader's record.
static int msl_test_ice_climbers(void)
{
    MSL_TEST_CHECK(msl_test_special_lift(FTKIND_POPO, FTKIND_NONE, &msl_test_b,
                                         MSL_SPENT_NEUTRAL_LIFT, "ice shot") == 0);
    MSL_TEST_CHECK(msl_test_match.follower_fighters[0] != NULL && msl_test_follower_seen > 0);
    fprintf(stderr, "ice shot: the follower's bit on %d frames\n", msl_test_follower_seen);
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

// Holds `input` from standing until the float starts, on which frame it is
// spent and gauge[0] is `left`.
static int msl_test_start_float(const MslCoreInput* input, float left)
{
    Fighter* fp = msl_test_fighter(0);
    for (unsigned i = 0; fp->motion_id != ftPe_MS_Float; ++i) {
        MSL_TEST_CHECK(i < 120);
        msl_test_spent_free = msl_test_gauge_free = 1;
        MSL_TEST_CHECK(msl_test_hold(input, 1, 0) == 0);
        msl_test_spent_free = 0;
        if (fp->motion_id != ftPe_MS_Float) {
            MSL_TEST_CHECK(msl_test_spent == 0 && msl_test_gauge[0] == 0);
        }
    }
    MSL_TEST_CHECK(msl_test_spent == MSL_SPENT_FLOAT && msl_test_gauge[0] == left &&
                   msl_test_gauge[1] == 0);
    msl_test_want_spent = MSL_SPENT_FLOAT;
    return 0;
}

// Peach's float. Spent from the frame the float starts until she is on the
// ground again, where any change of action gives it back, a landing in an
// air attack's lag included. gauge[0] is the float's frames left: the loaded
// attribute as it starts, down by 1 on every frame of
// the float and of an attack done out of it, and 0 whenever no float is going
// on, though the game leaves the rest in its variable when a float is let go
// early.
static int msl_test_peach_float(void)
{
    const int bit = MSL_SPENT_FLOAT;
    Fighter* fp;
    float full;
    int frames = 0, attack = 0;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_PEACH, MSL_TEST_FALCO) == 0);
    fp = msl_test_fighter(0);
    full = ((ftPe_DatAttrs*) fp->dat_attrs)->xC;
    MSL_TEST_CHECK(full > 60);
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);

    // Jump held: the float starts as the jump stops rising.
    MSL_TEST_CHECK(msl_test_start_float(&msl_test_jump, full) == 0);
    for (unsigned i = 0; i < 20; ++i) {
        float before = msl_test_gauge[0];
        MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 1, 0) == 0);
        MSL_TEST_CHECK(fp->motion_id == ftPe_MS_Float && msl_test_gauge[0] == before - 1);
    }
    // An attack out of the float, and back into the float: one count.
    for (unsigned i = 0; i == 0 || fp->motion_id != ftPe_MS_Float; ++i) {
        float before = msl_test_gauge[0];
        MSL_TEST_CHECK(i < 120);
        MSL_TEST_CHECK(msl_test_hold(i == 0 ? &msl_test_jump_a : &msl_test_jump, 1, 0) == 0);
        MSL_TEST_CHECK(msl_test_gauge[0] == before - 1 && msl_test_gauge[1] == 0);
        MSL_TEST_CHECK(i > 0 || fp->motion_id == ftPe_MS_FloatAttackAirN);
        attack += fp->motion_id == ftPe_MS_FloatAttackAirN;
    }
    MSL_TEST_CHECK(attack > 5 && msl_test_gauge[0] > 10);
    // Let go: no float is going on, and the game's variable keeps the rest.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id != ftPe_MS_Float && msl_test_gauge[0] == 0 &&
                   fp->fv.pe.x4 > 10);
    msl_test_gauge_free = 0;
    // Jump held again all the way down: no second float.
    MSL_TEST_CHECK(msl_test_land(&msl_test_jump, ftCo_MS_Landing, bit, 0) == 0);
    MSL_TEST_CHECK(fp->fv.pe.x4 > 10);

    // A whole float: it ends by itself when the count reaches 0.
    MSL_TEST_CHECK(msl_test_start_float(&msl_test_jump, full) == 0);
    for (frames = 1; fp->motion_id == ftPe_MS_Float; ++frames) {
        float before = msl_test_gauge[0];
        MSL_TEST_CHECK(frames < 1000);
        MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 1, 0) == 0);
        MSL_TEST_CHECK(msl_test_gauge[0] == (fp->motion_id == ftPe_MS_Float ? before - 1 : 0));
    }
    msl_test_gauge_free = 0;
    MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);

    // Floated, let go, then an ordinary air attack into the floor: she lands
    // in the attack's landing lag and has the float back on that frame.
    MSL_TEST_CHECK(msl_test_start_float(&msl_test_jump, full) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    msl_test_gauge_free = 0;
    for (unsigned i = 0; i < 20; ++i) {
        MSL_TEST_CHECK(msl_test_air_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_land_in_lag(&msl_test_a, bit, 0) == 0);

    // Spent, then a KO.
    MSL_TEST_CHECK(msl_test_start_float(&msl_test_jump, full) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    msl_test_gauge_free = 0;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    fprintf(stderr, "float: %g frames loaded, a whole one ends after %d, %d frames of the first in an attack\n", full,
            frames - 1, attack);
    return 0;
}

// The aerial grapple of Link, Young Link and Samus: once until the fighter is
// on the ground again. A grapple that catches nothing ends in special fall;
// out of that by a hit, the same input is an air attack, not a grapple. Like
// the float it is back on any change of action on the ground: a plain
// landing, the special fall's landing, an air attack's landing lag.
static int msl_test_tether(unsigned kind, const char* name)
{
    const int bit = MSL_SPENT_TETHER;
    Fighter* fp;
    MSL_TEST_CHECK(msl_test_setup(kind, MSL_TEST_FALCO) == 0);
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_other_actions(0) == 0);
    for (int pass = 0; pass < 3; ++pass) {
        msl_test_air(120);
        MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
        msl_test_want_spent = bit;
        MSL_TEST_CHECK(msl_test_hold(&msl_test_z, 1, 0) == 0);
        MSL_TEST_CHECK(fp->motion_id >= ftCo_MS_Count);
        MSL_TEST_CHECK(msl_test_air_until(ftCo_MS_FallSpecial) == 0);
        if (pass == 0) {
            MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_LandingFallSpecial, bit,
                                         0) == 0);
            continue;
        }
        MSL_TEST_CHECK(msl_test_hit_in_air() == 0);
        if (pass == 1) {
            MSL_TEST_CHECK(msl_test_air_frame(&msl_test_z) == 0);
            MSL_TEST_CHECK(fp->motion_id == ftCo_MS_AttackAirN);
            MSL_TEST_CHECK(msl_test_air_until(ftCo_MS_Fall) == 0);
            MSL_TEST_CHECK(msl_test_land(&msl_test_neutral, ftCo_MS_Landing, bit, 0) == 0);
        } else {
            MSL_TEST_CHECK(msl_test_land_in_lag(&msl_test_z, bit, 0) == 0);
        }
    }
    msl_test_air(120);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 3, 0) == 0);
    msl_test_want_spent = bit;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_z, 1, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id >= ftCo_MS_Count);
    // A KO does not give it back: the new stock has none until it has come
    // down from the respawn platform and stands on the ground.
    msl_test_reborn_spent = bit;
    msl_test_landed_spent = 0;
    MSL_TEST_CHECK(msl_test_ko(0, 0) == 0);
    fprintf(stderr, "%s: one aerial grapple an airtime\n", name);
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

// A ledge grab and the climb back onto the stage give a spent lift back no
// more than a hit does: Marth, on Yoshi's Story, uses his side special in the
// air, takes the left ledge, climbs up and stands with the lift still spent;
// the next aerial use does not rise.
static int msl_test_ledge_keeps_lift(void)
{
    const int bit = MSL_SPENT_SIDE_LIFT;
    Fighter* fp;
    float first, stale;
    int rose;
    msl_test_stage = 8;
    MSL_TEST_CHECK(msl_test_setup(FTKIND_MARS, MSL_TEST_FALCO) == 0);
    msl_test_stage = 32;
    fp = msl_test_fighter(0);
    MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
    fp->cur_pos = (Vec3) { -100, 70, 0 };
    fp->prev_pos = fp->cur_pos;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_side_b, &msl_test_neutral, bit, 0, &first,
                                           &rose) == 0);
    MSL_TEST_CHECK(first > 0);
    // Out to the left of the stage, then down beside the ledge.
    fp->cur_pos = (Vec3) { -100, 60, 0 };
    fp->prev_pos = fp->cur_pos;
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1, 0) == 0);
    fp->cur_pos = (Vec3) { -61, -20, 0 };
    fp->prev_pos = fp->cur_pos;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_CliffWait, 60, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 10, 0) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_right, 2, 0) == 0);
    MSL_TEST_CHECK(fp->motion_id != ftCo_MS_CliffWait);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200, 0) == 0);
    MSL_TEST_CHECK(fp->ground_or_air == GA_Ground && fp->cur_pos.y > -10 && msl_test_spent == bit);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 10, 0) == 0);
    MSL_TEST_CHECK(msl_test_jump_to_the_top() == 0);
    MSL_TEST_CHECK(msl_test_aerial_special(&msl_test_side_b, &msl_test_neutral, bit, bit, &stale,
                                           &rose) == 0);
    MSL_TEST_CHECK(stale <= 0);
    fprintf(stderr, "ledge: marth's side special rises at %g a frame, then %g after a ledge grab "
                    "and the climb\n", first, stale);
    return 0;
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    result = msl_test_giant_punch() || msl_test_hit_during_wind_up() ||
             msl_test_hit_during_charge(FTKIND_SAMUS) ||
             msl_test_hit_during_charge(FTKIND_SEAK) ||
             msl_test_hit_during_charge(FTKIND_MEWTWO) ||
             msl_test_hit_during_other_move(FTKIND_DONKEY, 0, &msl_test_up_b, &msl_test_neutral,
                                            ftDk_MS_SpecialHi, 0) ||
             msl_test_hit_during_other_move(FTKIND_DONKEY, 10, &msl_test_up_b, &msl_test_neutral,
                                            ftDk_MS_SpecialHi, 0) ||
             msl_test_hit_during_other_move(FTKIND_SAMUS, 0, &msl_test_up_b, &msl_test_neutral,
                                            ftSs_MS_SpecialHi, 0) ||
             msl_test_hit_during_other_move(FTKIND_SAMUS, 7, &msl_test_up_b, &msl_test_neutral,
                                            ftSs_MS_SpecialHi, 0) ||
             msl_test_hit_during_other_move(FTKIND_SEAK, 0, &msl_test_side_b, &msl_test_b,
                                            ftSk_MS_SpecialS, 0) ||
             msl_test_hit_during_other_move(FTKIND_MEWTWO, 0, &msl_test_down_b, &msl_test_neutral,
                                            ftMt_MS_SpecialLw, 0) ||
             msl_test_hit_during_other_move(FTKIND_MEWTWO, 7, &msl_test_down_b, &msl_test_neutral,
                                            ftMt_MS_SpecialLw, 7) ||
             msl_test_charge_shot() || msl_test_needles() ||
             msl_test_shadow_ball() || msl_test_oil_panic_damage() ||
             msl_test_absent_after_last_stock() ||
             msl_test_fire_breath() || msl_test_kirby_fire_breath() ||
             msl_test_kirby_swallows_fox() || msl_test_kirby_swallows_donkey_kong() || msl_test_kirby_copy(FTKIND_SAMUS) ||
             msl_test_kirby_copy(FTKIND_MEWTWO) || msl_test_kirby_copy(FTKIND_SEAK) ||
             msl_test_special_lift(FTKIND_MARIO, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "mario's cape") ||
             msl_test_special_lift(FTKIND_DRMARIO, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "dr. mario's sheet") ||
             msl_test_special_lift(FTKIND_MARS, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "marth's side special") ||
             msl_test_special_lift(FTKIND_EMBLEM, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "roy's side special") ||
             msl_test_special_lift(FTKIND_MEWTWO, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "confusion") ||
             msl_test_special_lift(FTKIND_PEACH, FTKIND_NONE, &msl_test_b,
                                   MSL_SPENT_NEUTRAL_LIFT, "toad") ||
             msl_test_special_lift(FTKIND_KIRBY, FTKIND_NONE, &msl_test_side_b,
                                   MSL_SPENT_SIDE_LIFT, "kirby's hammer") ||
             msl_test_special_lift(FTKIND_KIRBY, FTKIND_PEACH, &msl_test_b,
                                   MSL_SPENT_NEUTRAL_LIFT, "kirby's copied toad") ||
             msl_test_special_lift(FTKIND_KIRBY, FTKIND_POPO, &msl_test_b,
                                   MSL_SPENT_NEUTRAL_LIFT, "kirby's copied ice shot") ||
             msl_test_ice_climbers() ||
             msl_test_tornado(FTKIND_MARIO, "mario's tornado") ||
             msl_test_tornado(FTKIND_DRMARIO, "dr. mario's tornado") ||
             msl_test_tornado(FTKIND_LUIGI, "luigi's cyclone") ||
             msl_test_judge_history() || msl_test_peach_float() ||
             msl_test_tether(FTKIND_LINK, "link") || msl_test_tether(FTKIND_CLINK, "young link") ||
             msl_test_tether(FTKIND_SAMUS, "samus") ||
             msl_test_wall_jumps(MSL_TEST_FOX, "fox") || msl_test_wall_jumps(FTKIND_MARIO, "mario") ||
             msl_test_ledge_keeps_lift();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
