// The model shake of a charging smash, with extracted data, the real
// scheduler and controller inputs. PlCo.dat's smash-charge table
// (Fighter_804D6528) is a list of offsets and its length; a charging fighter
// keeps the length in smash_attrs.x212D and steps smash_attrs.x212C through
// the list once a frame, and ftCo_800DEEE8 gives the entry the model is
// shifted by. Two ways into a charge are driven: the common one
// (ftCo_800DF0D0, a forward smash) and Ness's yo-yo (up and down smash,
// ftNs_AttackHi4_YoyoApplySmash), which sets the charge up itself.
// refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEEE8,ftCo_800DEF38,
//   ftCo_800DF0D0}
// refs/melee/src/melee/ft/chara/ftNess/ftNs_AttackHi4.c::
//   ftNs_AttackHi4_YoyoApplySmash
#include "runtime/scalar.h"
#include "ft/fighter.h"
#include "ft/ft_0DF0.h"
#include "ft/types.h"
#include "ftNess/forward.h"
#include <dolphin/pad.h>
#include <stdio.h>

enum { MSL_TEST_FOX = 1, MSL_TEST_NESS = 8, MSL_TEST_STICK = 80 };

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
        return -1; \
    } \
} while (0)

static Fighter* msl_test_fighter(unsigned port)
{
    return msl_test_match.fighters[port]->user_data;
}

static int msl_test_step(const MslCoreInput* input)
{
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                              &(MslCoreStageEvents) { 0 }) == 0);
    return 0;
}

static int msl_test_setup(unsigned kind)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = kind;
    config.players[1].char_id = MSL_TEST_FOX;
    MSL_TEST_CHECK((msl_test_match.memory.arena == NULL
               ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)
               : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)) == 0);
    for (unsigned i = 0; i < 150; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    return 0;
}

// Smash with the stick and A, keep A held, and follow the charge: on every
// charging frame the length is the table's, the index is the one after the
// frame before's (back to 0 past the last entry), and the shift is that
// entry's. The charge has to go once round the table at least.
static int msl_test_charge(const char* name, unsigned kind, int stick_x, int stick_y, int charge_state)
{
    const struct Fighter_804D6528_t* table = Fighter_804D6528;
    MslCoreInput smash = { 0 }, hold = { 0 };
    unsigned charging = 0, wraps = 0, seen = 0;
    int last = -1;

    MSL_TEST_CHECK(table != NULL && table->x0 != NULL && table->x4 > 1 && table->x4 < 256);
    MSL_TEST_CHECK(msl_test_setup(kind) == 0);
    smash.p[0].buttons = hold.p[0].buttons = PAD_BUTTON_A;
    smash.p[0].main_x = stick_x;
    smash.p[0].main_y = stick_y;
    MSL_TEST_CHECK(msl_test_step(&smash) == 0);
    for (unsigned frame = 0; frame < 120; ++frame) {
        Fighter* fp = msl_test_fighter(0);
        SmashAttr* attr = &fp->smash_attrs;
        Vec2 shift = { 0 };

        MSL_TEST_CHECK(msl_test_step(&hold) == 0);
        if (attr->state != charge_state) {
            MSL_TEST_CHECK(ftCo_800DEEE8(fp, &shift) == NULL || attr->state == SmashState_Charging ||
                  attr->state == 4);
            last = -1;
            continue;
        }
        ++charging;
        if (attr->x212D != table->x4) {
            fprintf(stderr, "%s: charging with a table length of %u, the table has %d\n", name,
                    attr->x212D, table->x4);
            return -1;
        }
        MSL_TEST_CHECK(attr->x212C < table->x4);
        if (last >= 0) {
            MSL_TEST_CHECK(attr->x212C == (last + 1) % table->x4);
            wraps += attr->x212C == 0;
        }
        last = attr->x212C;
        seen |= 1u << attr->x212C;
        MSL_TEST_CHECK(ftCo_800DEEE8(fp, &shift) == &shift);
        MSL_TEST_CHECK(shift.x == table->x0[attr->x212C].x && shift.y == table->x0[attr->x212C].y);
    }
    fprintf(stderr, "%s: %u charging frames, table length %d, %u times round, entries seen %#x\n",
            name, charging, table->x4, wraps, seen);
    MSL_TEST_CHECK(charging >= (unsigned) table->x4 * 2 && wraps >= 2);
    MSL_TEST_CHECK(seen == (1u << table->x4) - 1);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_charge("Fox forward smash", MSL_TEST_FOX, MSL_TEST_STICK, 0,
                                 SmashState_Charging) ||
                 msl_test_charge("Ness up smash", MSL_TEST_NESS, 0, MSL_TEST_STICK, 4) ||
                 msl_test_charge("Ness down smash", MSL_TEST_NESS, 0, -MSL_TEST_STICK, 4);
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
