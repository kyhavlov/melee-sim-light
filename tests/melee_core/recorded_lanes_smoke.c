// The recorded values the observation publishes beside the player rows:
// Slippi's state flags, the L-cancel status, the ground id and the last attack
// landed. Extracted data, the real scheduler and controller inputs. Every
// frame checks that both observation builders agree byte for byte from both
// viewpoints, that each record sits in its player's slot, that an absent row
// has an empty record, and that a standing fighter's ground id names the
// collision line under it. The scenarios then check each value on the frames
// where the inputs make it change.
#include "runtime/scalar.h"
#include "runtime/observation.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "mp/types.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MSL_TEST_BATTLEFIELD = 31,
    MSL_TEST_FINAL_DESTINATION = 32,
    // Slippi's attack ids.
    MSL_TEST_ATTACK_NONE = 0,
    MSL_TEST_ATTACK_JAB_1 = 2,
    MSL_TEST_ATTACK_FORWARD_SMASH = 10,
    // A fighter that has stood on nothing since it appeared.
    MSL_TEST_NO_GROUND = 0xFFFF,
};

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;
static const MslCoreInput msl_test_a = { { { PAD_BUTTON_A } } };
static const MslCoreInput msl_test_jump = { { { PAD_BUTTON_X } } };
static const MslCoreInput msl_test_down = { { { 0, 0, -80 } } };
static const MslCoreInput msl_test_smash = { { { 0, 0, 0, 80 } } };
static const MslCoreInput msl_test_shield = {
    { { PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255 } }
};
static const MslCoreInput msl_test_opponent_shield = {
    { { 0 }, { PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255 } }
};
// The observation of port 0 after the last frame, its two leader rows and
// their records, and the stocks of the next setup.
static MslCoreObservation msl_test_seen;
static const MslCoreObservationPlayer* const msl_test_row = msl_test_seen.slots;
static const MslCoreObservationRecorded* const msl_test_rec = msl_test_seen.recorded;
static const MslCoreObservationRecorded msl_test_nothing;
static unsigned msl_test_stocks = 4;

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (frame %d)\n", __func__, __LINE__, \
                #condition, msl_test_match.frame_id); \
        return -1; \
    } \
} while (0)

static Fighter* msl_test_fighter(unsigned port)
{
    return msl_test_match.fighters[port]->user_data;
}

// Whether collision line `id` is a floor passing under (x, y).
static int msl_test_stands_on(unsigned id, float x, float y)
{
    const MslMpLibState* map = &msl_test_match.source.mp_lib;
    const MapLine* line;
    const Vec2* a;
    const Vec2* b;
    if ((int) id >= map->data->line_count) {
        return 0;
    }
    line = map->lines[id].x0;
    a = &map->vertices[line->v0_idx].pos;
    b = &map->vertices[line->v1_idx].pos;
    if (a->x == b->x || x < fminf(a->x, b->x) - 0.01F || x > fmaxf(a->x, b->x) + 0.01F) {
        return 0;
    }
    return fabsf(a->y + (b->y - a->y) * (x - a->x) / (b->x - a->x) - y) < 0.01F;
}

// What holds for any row and its record on any frame.
static int msl_test_record(const MslCoreObservationPlayer* row,
                           const MslCoreObservationRecorded* recorded, int follower)
{
    const uint8_t* flags = recorded->state_flags;
    if (!row->present) {
        MSL_TEST_CHECK(memcmp(recorded, &msl_test_nothing, sizeof(*recorded)) == 0);
        return 0;
    }
    MSL_TEST_CHECK(!(flags[4] & MSL_STATE4_FOLLOWER) == !follower);
    MSL_TEST_CHECK(!(flags[4] & MSL_STATE4_INACTIVE));
    MSL_TEST_CHECK(row->hitstun == 0 || (flags[3] & MSL_STATE3_HITSTUN));
    MSL_TEST_CHECK(!(flags[1] & MSL_STATE1_HITLAG) == (row->hitlag == 0));
    MSL_TEST_CHECK(!(flags[1] & MSL_STATE1_DEFENDER_HITLAG) || (flags[1] & MSL_STATE1_HITLAG));
    MSL_TEST_CHECK(!(flags[1] & MSL_STATE1_FAST_FALL) ||
                   (!row->on_ground && row->speed_y_self < 0));
    MSL_TEST_CHECK(recorded->l_cancel == MSL_L_CANCEL_NONE ||
                   ((recorded->l_cancel == MSL_L_CANCEL_HIT ||
                     recorded->l_cancel == MSL_L_CANCEL_MISSED) &&
                    row->action_id >= ftCo_MS_LandingAirN &&
                    row->action_id <= ftCo_MS_LandingAirLw));
    MSL_TEST_CHECK(!row->on_ground ||
                   msl_test_stands_on(recorded->ground_id, row->pos_x, row->pos_y));
    MSL_TEST_CHECK(recorded->_pad0 == 0);
    return 0;
}

static int msl_test_frame(const MslCoreInput* input)
{
    MslCoreObservation compared, other, other_compared;
    MslCoreObservation* seen = &msl_test_seen;
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                                       &(MslCoreStageEvents) { 0 }) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 0, seen) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation_from_compare(&msl_test_match, 0, &compared) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation(&msl_test_match, 1, &other) == 0);
    MSL_TEST_CHECK(msl_core_match_write_observation_from_compare(&msl_test_match, 1,
                                                                 &other_compared) == 0);
    MSL_TEST_CHECK(memcmp(seen, &compared, sizeof(compared)) == 0);
    MSL_TEST_CHECK(memcmp(&other, &other_compared, sizeof(other)) == 0);
    // Singles: each viewpoint has itself in slot 0 and the opponent in slot 1.
    MSL_TEST_CHECK(seen->slots[0].source_player == 0 && other.slots[1].source_player == 0);
    for (unsigned k = 0; k < 2; ++k) {
        MSL_TEST_CHECK(memcmp(&other.recorded[1 - k], &seen->recorded[k],
                              sizeof(seen->recorded[k])) == 0);
        MSL_TEST_CHECK(memcmp(&other.follower_recorded[1 - k], &seen->follower_recorded[k],
                              sizeof(seen->recorded[k])) == 0);
        MSL_TEST_CHECK(msl_test_record(&seen->slots[k], &seen->recorded[k], 0) == 0);
        MSL_TEST_CHECK(msl_test_record(&seen->followers[k], &seen->follower_recorded[k], 1) == 0);
    }
    for (unsigned k = 2; k < MSL_CORE_MAX_PLAYERS; ++k) {
        MSL_TEST_CHECK(memcmp(&seen->recorded[k], &msl_test_nothing, sizeof(msl_test_nothing)) == 0);
        MSL_TEST_CHECK(memcmp(&seen->follower_recorded[k], &msl_test_nothing,
                              sizeof(msl_test_nothing)) == 0);
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
    config.stock_count = msl_test_stocks;
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
    MSL_TEST_CHECK(msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_NONE &&
                   msl_test_rec[1].last_attack_landed == MSL_TEST_ATTACK_NONE);
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

static void msl_test_place(unsigned port, float x, float facing)
{
    Fighter* fp = msl_test_fighter(port);
    fp->cur_pos = (Vec3) { x, 0, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = facing;
}

// A full hop. With `tap_down`, the stick is tapped down on the first frame
// after the fighter starts to fall. Returns the airborne frames and the
// greatest falling speed observed; the fast-fall flag must be set from the
// tap to the last airborne frame and on no other frame.
static int msl_test_hop(int tap_down, int* airborne, float* fastest)
{
    int tapped = 0;
    int ground = msl_test_rec[0].ground_id;
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
        MSL_TEST_CHECK(!(msl_test_rec[0].state_flags[1] & MSL_STATE1_FAST_FALL) ==
                       !(tapped && !msl_test_row[0].on_ground));
        // The ground id stays the last line stood on while in the air.
        MSL_TEST_CHECK(msl_test_rec[0].ground_id == ground &&
                       msl_test_rec[0].l_cancel == MSL_L_CANCEL_NONE);
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
    MSL_TEST_CHECK(msl_test_setup(MSL_TEST_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    MSL_TEST_CHECK(msl_test_hop(0, &plain, &plain_speed) == 0);
    MSL_TEST_CHECK(msl_test_hop(1, &fast, &fast_speed) == 0);
    MSL_TEST_CHECK(fast < plain && fast_speed < plain_speed);
    MSL_TEST_CHECK(fast_speed == -msl_test_fighter(0)->co_attrs.fast_fall_velocity);
    fprintf(stderr, "fast fall: %d airborne frames against %d, %.2f against %.2f\n", fast,
            plain, fast_speed, plain_speed);
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
        MSL_TEST_CHECK(*airborne < 60 && msl_test_rec[0].l_cancel == MSL_L_CANCEL_NONE);
        MSL_TEST_CHECK(msl_test_frame(*airborne == press_at ? &msl_test_shield
                                                            : &msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_LandingAirN);
    status = msl_test_rec[0].l_cancel;
    for (*lag = 0; msl_test_row[0].action_id == ftCo_MS_LandingAirN; ++*lag) {
        MSL_TEST_CHECK(*lag < 60);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_rec[0].l_cancel == MSL_L_CANCEL_NONE);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Wait);
    return status;
}

static int msl_test_l_cancel(void)
{
    int airborne, again, missed_lag, hit_lag, early_lag;
    MSL_TEST_CHECK(msl_test_setup(MSL_TEST_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    MSL_TEST_CHECK(msl_test_aerial(-1, &airborne, &missed_lag) == MSL_L_CANCEL_MISSED);
    // Three frames before the landing: inside the window.
    MSL_TEST_CHECK(msl_test_aerial(airborne - 4, &again, &hit_lag) == MSL_L_CANCEL_HIT);
    MSL_TEST_CHECK(again == airborne && hit_lag < missed_lag);
    // Twelve frames before it: too early.
    MSL_TEST_CHECK(msl_test_aerial(airborne - 13, &again, &early_lag) == MSL_L_CANCEL_MISSED);
    MSL_TEST_CHECK(again == airborne && early_lag == missed_lag);
    // The opponent did none of this.
    MSL_TEST_CHECK(msl_test_rec[1].l_cancel == MSL_L_CANCEL_NONE);
    fprintf(stderr, "l-cancel: %d frames of landing lag hit, %d missed\n", hit_lag, missed_lag);
    return 0;
}

// `input` for one frame from standing next to the opponent, then nothing
// until the opponent's damage rises. The last attack landed must be `before`
// until that frame and `attack` on it; the frame is checked as a hit.
static int msl_test_hit(const MslCoreInput* input, int before, int attack)
{
    float percent = msl_test_row[1].percent;
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200) == 0);
    for (unsigned i = 0; msl_test_row[1].action_id != ftCo_MS_Wait; ++i) {
        MSL_TEST_CHECK(i < 200);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    msl_test_place(0, -6, 1);
    msl_test_place(1, 6, -1);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 2) == 0);
    for (unsigned i = 0; msl_test_row[1].percent == percent; ++i) {
        MSL_TEST_CHECK(i < 40 && msl_test_rec[0].last_attack_landed == before);
        MSL_TEST_CHECK(msl_test_frame(i == 0 ? input : &msl_test_neutral) == 0);
        // A move clears the interrupt flag as it starts.
        MSL_TEST_CHECK(i != 0 || !(msl_test_rec[0].state_flags[0] & MSL_STATE0_ALLOW_INTERRUPT));
    }
    MSL_TEST_CHECK(msl_test_rec[0].last_attack_landed == attack &&
                   msl_test_rec[1].last_attack_landed == MSL_TEST_ATTACK_NONE);
    MSL_TEST_CHECK(msl_test_row[0].hitlag > 0 && msl_test_row[1].hitlag > 0);
    MSL_TEST_CHECK(!(msl_test_rec[0].state_flags[1] & MSL_STATE1_DEFENDER_HITLAG) &&
                   (msl_test_rec[1].state_flags[1] & MSL_STATE1_DEFENDER_HITLAG));
    MSL_TEST_CHECK(!(msl_test_rec[0].state_flags[3] & MSL_STATE3_HITSTUN) &&
                   (msl_test_rec[1].state_flags[3] & MSL_STATE3_HITSTUN));
    return 0;
}

static int msl_test_attacks(void)
{
    Fighter* fp;
    int interruptible = 0, powershield = 0, offscreen = 0;
    int ground;
    MSL_TEST_CHECK(msl_test_setup(MSL_TEST_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    fp = msl_test_fighter(0);
    ground = msl_test_rec[0].ground_id;

    // A jab, kept as the last attack through the rest of the move; the move's
    // script raises the interrupt flag before the animation ends.
    MSL_TEST_CHECK(msl_test_hit(&msl_test_a, MSL_TEST_ATTACK_NONE, MSL_TEST_ATTACK_JAB_1) == 0);
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Attack11);
    while (msl_test_row[0].action_id == ftCo_MS_Attack11) {
        int flag = msl_test_rec[0].state_flags[0] & MSL_STATE0_ALLOW_INTERRUPT;
        MSL_TEST_CHECK(flag || !interruptible);
        interruptible += flag != 0;
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_JAB_1);
    }
    MSL_TEST_CHECK(interruptible > 0 && msl_test_row[0].action_id == ftCo_MS_Wait);

    // A different attack replaces it.
    MSL_TEST_CHECK(msl_test_hit(&msl_test_smash, MSL_TEST_ATTACK_JAB_1,
                                MSL_TEST_ATTACK_FORWARD_SMASH) == 0);
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_AttackS4S);
    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 200) == 0);
    MSL_TEST_CHECK(msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_FORWARD_SMASH);

    // The opponent's shield: up while held, a powershield as it comes out.
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 60) == 0);
    for (unsigned i = 0; i < 30; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_opponent_shield) == 0);
        powershield += (msl_test_rec[1].state_flags[3] & MSL_STATE3_POWERSHIELD) != 0;
        MSL_TEST_CHECK(!(msl_test_rec[0].state_flags[2] & MSL_STATE2_SHIELD));
        MSL_TEST_CHECK(!(msl_test_rec[1].state_flags[3] & MSL_STATE3_POWERSHIELD) ||
                       (msl_test_rec[1].state_flags[2] & MSL_STATE2_SHIELD));
    }
    MSL_TEST_CHECK(powershield > 0 && powershield < 10 &&
                   msl_test_row[1].action_id == ftCo_MS_Guard &&
                   (msl_test_rec[1].state_flags[2] & MSL_STATE2_SHIELD));
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 30) == 0);
    MSL_TEST_CHECK(msl_test_row[1].action_id == ftCo_MS_Wait &&
                   !(msl_test_rec[1].state_flags[2] & MSL_STATE2_SHIELD));

    // Past the left blast zone: dead through the death animation with the
    // last attack kept, then a new fighter that has landed nothing and stood
    // on nothing until the respawn platform lets it down.
    fp->cur_pos = (Vec3) { -300, 40, 0 };
    fp->prev_pos = fp->cur_pos;
    ftCommon_8007D5D4(fp);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_neutral, 1) == 0);
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_DeadLeft);
    for (unsigned i = 0; msl_test_row[0].action_id == ftCo_MS_DeadLeft; ++i) {
        MSL_TEST_CHECK(i < 120);
        MSL_TEST_CHECK((msl_test_rec[0].state_flags[4] & MSL_STATE4_DEAD) &&
                       msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_FORWARD_SMASH &&
                       msl_test_rec[0].ground_id == ground);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Rebirth);
    for (unsigned i = 0; !msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 900);
        MSL_TEST_CHECK(!(msl_test_rec[0].state_flags[4] & MSL_STATE4_DEAD) &&
                       msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_NONE &&
                       msl_test_rec[0].ground_id == MSL_TEST_NO_GROUND);
        // The platform comes down from above the camera.
        offscreen += (msl_test_rec[0].state_flags[4] & MSL_STATE4_OFFSCREEN) != 0;
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(offscreen > 0 && msl_test_rec[0].ground_id == ground &&
                   msl_test_rec[0].last_attack_landed == MSL_TEST_ATTACK_NONE);
    // The opponent's record never showed any of it.
    MSL_TEST_CHECK(msl_test_rec[1].last_attack_landed == MSL_TEST_ATTACK_NONE &&
                   !(msl_test_rec[1].state_flags[4] & (MSL_STATE4_DEAD | MSL_STATE4_OFFSCREEN)));
    fprintf(stderr, "attacks: interruptible for %d frames of the jab, powershield for %d, "
            "off screen for %d\n", interruptible, powershield, offscreen);
    return 0;
}

// The last stock: the row goes absent after the death animation and its
// record with it.
static int msl_test_absent_after_last_stock(void)
{
    Fighter* fp;
    int absent = 0;
    msl_test_stocks = 1;
    MSL_TEST_CHECK(msl_test_setup(MSL_TEST_FINAL_DESTINATION, FTKIND_FOX, FTKIND_FOX) == 0);
    msl_test_stocks = 4;
    fp = msl_test_fighter(0);
    fp->cur_pos = (Vec3) { -300, 40, 0 };
    fp->prev_pos = fp->cur_pos;
    ftCommon_8007D5D4(fp);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 300; ++i) {
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(absent == 0 || !msl_test_row[0].present);
        absent += !msl_test_row[0].present;
    }
    MSL_TEST_CHECK(absent > 100 && msl_test_row[1].present);
    return 0;
}

// Battlefield starts the Ice Climbers on the left platform and Fox on the
// right one. Popo drops through to the main stage and jumps back up; Nana
// follows him. Each surface has its own id, and the id changes on the landing
// frame, not before.
static int msl_test_platform(void)
{
    const MslCoreObservationPlayer* nana = &msl_test_seen.followers[0];
    const MslCoreObservationRecorded* hers = &msl_test_seen.follower_recorded[0];
    int platform, main_stage, right_platform;
    MSL_TEST_CHECK(msl_test_setup(MSL_TEST_BATTLEFIELD, FTKIND_POPO, FTKIND_FOX) == 0);
    platform = msl_test_rec[0].ground_id;
    right_platform = msl_test_rec[1].ground_id;
    MSL_TEST_CHECK(msl_test_row[0].pos_y > 20 && msl_test_row[1].pos_y == msl_test_row[0].pos_y);
    MSL_TEST_CHECK(platform != right_platform && nana->present && hers->ground_id == platform);

    for (unsigned i = 0; msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 60 && msl_test_rec[0].ground_id == platform);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_down) == 0);
    }
    MSL_TEST_CHECK(msl_test_row[0].action_id == ftCo_MS_Pass);
    for (unsigned i = 0; !msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 120 && msl_test_rec[0].ground_id == platform);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    main_stage = msl_test_rec[0].ground_id;
    MSL_TEST_CHECK(fabsf(msl_test_row[0].pos_y) < 1 && main_stage != platform &&
                   main_stage != right_platform);
    // Nana comes down after him.
    for (unsigned i = 0; !nana->on_ground || hers->ground_id != main_stage; ++i) {
        MSL_TEST_CHECK(i < 300 && nana->present);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_rec[0].ground_id == main_stage);

    MSL_TEST_CHECK(msl_test_until(&msl_test_neutral, ftCo_MS_Wait, 120) == 0);
    MSL_TEST_CHECK(msl_test_hold(&msl_test_jump, 10) == 0);
    for (unsigned i = 0; !msl_test_row[0].on_ground; ++i) {
        MSL_TEST_CHECK(i < 200 && msl_test_rec[0].ground_id == main_stage);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    MSL_TEST_CHECK(msl_test_rec[0].ground_id == platform && msl_test_row[0].pos_y > 20);
    for (unsigned i = 0; !nana->on_ground || hers->ground_id != platform; ++i) {
        MSL_TEST_CHECK(i < 300 && nana->present);
        MSL_TEST_CHECK(msl_test_frame(&msl_test_neutral) == 0);
    }
    // Fox never moved.
    MSL_TEST_CHECK(msl_test_rec[1].ground_id == right_platform);
    fprintf(stderr, "ground ids: left platform %d, right platform %d, main stage %d\n",
            platform, right_platform, main_stage);
    return 0;
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    result = msl_test_fast_fall() || msl_test_l_cancel() || msl_test_attacks() ||
             msl_test_absent_after_last_stock() || msl_test_platform();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
