#include "runtime/scalar.h"
#include "runtime/observation.h"
#include "ft/ftcommon.h"
#include "ftCommon/forward.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    MSL_TEST_ATTACK_JAB_1 = 2,
};

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;
static const MslCoreInput msl_test_a = { { { PAD_BUTTON_A } } };
static const MslCoreInput msl_test_jump = { { { PAD_BUTTON_X } } };
static const MslCoreInput msl_test_down = { { { 0, 0, -80 } } };
static const MslCoreInput msl_test_shield = {
    { { PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255 } }
};
static const MslCoreInput msl_test_opponent_shield = {
    { { 0 }, { PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255 } }
};
static MslCoreObservation msl_test_seen;
static const MslCoreObservationPlayer* const msl_test_row = msl_test_seen.slots;

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (frame %d)\n", __func__, __LINE__, \
                #condition, msl_test_match.frame_id); \
        return -1; \
    } \
} while (0)

static int msl_test_frame(const MslCoreInput* input)
{
    MslCoreObservation live, compared;

    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                                      &(MslCoreStageEvents) { 0 }) == 0);
    for (uint8_t player = 0; player < 2; ++player) {
        MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, player, &live) == 0);
        MSL_TEST_CHECK(msl_core_match_write_observation_from_compare(&msl_test_match, player,
                                                                    &compared) == 0);
        MSL_TEST_CHECK(memcmp(&live, &compared, sizeof(live)) == 0);
        if (player == 0) {
            msl_test_seen = live;
        }
    }
    return 0;
}

static int msl_test_setup(unsigned stage, unsigned character, unsigned opponent)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = stage;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = character;
    config.players[1].char_id = opponent;
    MSL_TEST_CHECK((msl_test_match.memory.arena == NULL
               ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)
               : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)) == 0);
    for (unsigned i = 0; i < 150; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Wait &&
                   msl_test_row[1].action_id == ftCo_MS_Wait);
    MSL_TEST_CHECK(msl_test_row[0].last_attack_landed == 0 &&
                   msl_test_row[1].last_attack_landed == 0);
    return 0;
}

static int msl_test_hold(const MslCoreInput* input, unsigned frames)
{
    for (unsigned i = 0; i < frames; ++i) {
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
    }
    return 0;
}

// Holds `input` until port 0 is in `action`.
static int msl_test_until(const MslCoreInput* input, int action, unsigned limit)
{
    for (unsigned i = 0; msl_test_row[0].action_id != action; ++i) {
        MSL_TEST_CHECK(i < limit);
        MSL_TEST_CHECK(msl_test_frame(input) == 0);
    }
    return 0;
}

static int msl_test_hop(int tap_down, int* airborne, float* fastest)
{
    int tapped = 0;
    int ground = msl_test_row[0].ground_id;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120) == 0);
    *airborne = 0;
    *fastest = 0;
    for (unsigned i = 0; i < 6 || !msl_test_row[0].on_ground; ++i) {
        int tap = tap_down && !tapped && !msl_test_row[0].on_ground &&
                  msl_test_row[0].speed_y_self < 0;
        MSL_TEST_CHECK(i < 200);
        MSL_TEST_CHECK(msl_test_frame(i < 6 ? &msl_test_jump : tap ? &msl_test_down
                                                                    : &msl_test_neutral) == 0);
        tapped |= tap;
        MSL_TEST_CHECK(!(msl_test_row[0].state_flags[1] & MSL_STATE1_FAST_FALL) ==
                       !(tapped && !msl_test_row[0].on_ground));
        // The ground id stays the last line stood on while in the air.
        MSL_TEST_CHECK(msl_test_row[0].ground_id == ground &&
                       msl_test_row[0].l_cancel == MSL_L_CANCEL_NONE);
        if (!msl_test_row[0].on_ground) {
            ++*airborne;
            *fastest = fminf(*fastest, msl_test_row[0].speed_y_self);
        }
    }
    MSL_TEST_CHECK(*airborne > 20 && tapped == tap_down);
    return 0;
}

static int msl_test_fast_fall(void)
{
    int plain, fast;
    float plain_speed, fast_speed;
    MSL_TEST_CHECK(msl_test_setup(MSL_STAGE_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    MSL_TEST_CHECK(msl_test_hop(0, &plain, &plain_speed) == 0);
    MSL_TEST_CHECK(msl_test_hop(1, &fast, &fast_speed) == 0);
    MSL_TEST_CHECK(fast < plain && fast_speed < plain_speed);
    return 0;
}

// A short hop and a neutral air that lands during the attack, with the shield
// button pressed on airborne frame `press_at` of the attack (-1: never).
// Returns the L-cancel status of the landing frame, the airborne frames and
// the frames of landing lag; the status must be zero on every other frame.
static int msl_test_aerial(int press_at, int* airborne, int* lag)
{
    int status;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 2) == 0);
    for (unsigned i = 0; msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 10);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_hold(&msl_test_a, 1) == 0);
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_AttackAirN);
    for (*airborne = 0; !msl_test_row[0].on_ground; ++*airborne) {
        MSL_TEST_CHECK(*airborne < 60 && msl_test_row[0].l_cancel == MSL_L_CANCEL_NONE);
        MSL_TEST_CHECK(msl_test_frame(*airborne == press_at ? &msl_test_shield
                                                            : &msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_LandingAirN);
    status = msl_test_row[0].l_cancel;
    for (*lag = 0; msl_test_row[0].action_id == ftCo_MS_LandingAirN; ++*lag) {
        MSL_TEST_CHECK(*lag < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_row[0].l_cancel == MSL_L_CANCEL_NONE);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Wait);
    return status;
}

static int msl_test_l_cancel(void)
{
    int airborne, again, missed_lag, hit_lag, early_lag;
    MSL_TEST_CHECK(msl_test_setup(MSL_STAGE_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    MSL_TEST_CHECK(msl_test_aerial(-1, &airborne, &missed_lag) == MSL_L_CANCEL_MISSED);
    // Three frames before the landing: inside the window.
    MSL_TEST_CHECK(msl_test_aerial(airborne - 4, &again, &hit_lag) == MSL_L_CANCEL_HIT);
    MSL_TEST_CHECK(again == airborne && hit_lag < missed_lag);
    // Twelve frames before it: too early.
    MSL_TEST_CHECK(msl_test_aerial(airborne - 13, &again, &early_lag) == MSL_L_CANCEL_MISSED);
    MSL_TEST_CHECK(again == airborne && early_lag == missed_lag);
    // The opponent did none of this.
    MSL_TEST_CHECK(msl_test_row[1].l_cancel == MSL_L_CANCEL_NONE);
    return 0;
}

static int msl_test_attack(void)
{
    MslCoreInput walk = { 0 };
    int interruptible = 0;
    const MslCoreObservationPlayer* fox = &msl_test_row[0];
    const MslCoreObservationPlayer* opponent = &msl_test_row[1];

    MSL_TEST_CHECK(msl_test_setup(MSL_STAGE_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    walk.p[0].main_x = fox->pos_x < opponent->pos_x ? 40 : -40;
    for (unsigned i = 0; fabsf(fox->pos_x - opponent->pos_x) > 9; ++i) {
        MSL_TEST_CHECK(i < 180);
        MSL_TEST_CHECK(msl_test_frame(&walk) == 0);
    }
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 30) == 0);
    MSL_TEST_CHECK(msl_test_frame(&msl_test_a) == 0);
    MSL_TEST_CHECK(fox->action_id == ftCo_MS_Attack11);
    for (unsigned i = 0; opponent->percent == 0; ++i) {
        MSL_TEST_CHECK(i < 10 && fox->last_attack_landed == 0);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(fox->last_attack_landed == MSL_TEST_ATTACK_JAB_1);
    MSL_TEST_CHECK(opponent->last_attack_landed == 0);
    MSL_TEST_CHECK(fox->state_flags[1] & MSL_STATE1_HITLAG);
    MSL_TEST_CHECK(opponent->state_flags[1] & MSL_STATE1_DEFENDER_HITLAG);
    MSL_TEST_CHECK(opponent->state_flags[3] & MSL_STATE3_HITSTUN);
    for (unsigned i = 0; fox->action_id == ftCo_MS_Attack11; ++i) {
        MSL_TEST_CHECK(i < 60);
        interruptible |= fox->state_flags[0] & MSL_STATE0_ALLOW_INTERRUPT;
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(fox->last_attack_landed == MSL_TEST_ATTACK_JAB_1);
    }
    MSL_TEST_CHECK(interruptible);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 60) == 0);

    MSL_TEST_CHECK(msl_test_frame(&msl_test_opponent_shield) == 0);
    MSL_TEST_CHECK(opponent->state_flags[3] & MSL_STATE3_POWERSHIELD);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_opponent_shield, 11) == 0);
    MSL_TEST_CHECK(opponent->state_flags[2] & MSL_STATE2_SHIELD);
    MSL_TEST_CHECK(!(fox->state_flags[2] & MSL_STATE2_SHIELD));
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30) == 0);
    MSL_TEST_CHECK(!(opponent->state_flags[2] & MSL_STATE2_SHIELD));
    return 0;
}

// Battlefield starts the Ice Climbers on the left platform and Fox on the
// right one. Popo drops through to the main stage and jumps back up; Nana
// follows him. Each surface has its own id, and the id changes on the landing
// frame, not before.
static int msl_test_platform(void)
{
    const MslCoreObservationPlayer* nana = &msl_test_seen.followers[0];
    int platform, main_stage, right_platform;
    MSL_TEST_CHECK(msl_test_setup(MSL_STAGE_BATTLEFIELD, FTKIND_POPO, FTKIND_FOX) == 0);
    platform = msl_test_row[0].ground_id;
    right_platform = msl_test_row[1].ground_id;
    MSL_TEST_CHECK(msl_test_row[0].pos_y > 20 && msl_test_row[1].pos_y == msl_test_row[0].pos_y);
    MSL_TEST_CHECK(platform != right_platform && nana->present && nana->ground_id == platform);
    MSL_TEST_CHECK(nana->state_flags[4] & MSL_STATE4_FOLLOWER);
    MSL_TEST_CHECK(!(msl_test_row[0].state_flags[4] & MSL_STATE4_FOLLOWER));
    MSL_TEST_CHECK(!msl_test_seen.followers[1].present);
    MSL_TEST_CHECK(msl_test_seen.followers[1].state_flags[4] == 0);

    for (unsigned i = 0; msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 60 && msl_test_row[0].ground_id == platform);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_down) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Pass);
    for (unsigned i = 0; !msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 120 && msl_test_row[0].ground_id == platform);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    main_stage = msl_test_row[0].ground_id;
    MSL_TEST_CHECK(fabsf(msl_test_row[0].pos_y) < 1 && main_stage != platform &&
                   main_stage != right_platform);
    // Nana comes down after him.
    for (unsigned i = 0; !nana->on_ground || nana->ground_id != main_stage; ++i) {
        MSL_TEST_CHECK(i < 300 && nana->present);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].ground_id == main_stage);

    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 10) == 0);
    for (unsigned i = 0; !msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 200 && msl_test_row[0].ground_id == main_stage);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].ground_id == platform && msl_test_row[0].pos_y > 20);
    for (unsigned i = 0; !nana->on_ground || nana->ground_id != platform; ++i) {
        MSL_TEST_CHECK(i < 300 && nana->present);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    // Fox never moved.
    MSL_TEST_CHECK(msl_test_row[1].ground_id == right_platform);
    return 0;
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    result = msl_test_fast_fall() || msl_test_l_cancel() || msl_test_attack() ||
             msl_test_platform();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
