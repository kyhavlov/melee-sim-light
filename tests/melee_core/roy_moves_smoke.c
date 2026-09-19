// Roy shares Marth's special-move owners; these scenarios drive the branches
// that read FTKIND_EMBLEM (Counter's scaled reflection, the Flare Blade full
// charge authored in PlFe.dat, Blazer's special fall) with extracted data and
// the real scheduler, and check the four-Roy pool headroom.
#include "runtime/scalar.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftMars/forward.h"
#include "ftMars/ftMs_SpecialHi.h"
#include "ftMars/ftMs_SpecialLw.h"
#include "ftMars/ftMs_SpecialN.h"
#include "ftMars/ftMs_SpecialS.h"
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/objalloc.h>
#include <dolphin/pad.h>
#include <stdio.h>

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;
static unsigned msl_test_fobj_peak, msl_test_aobj_peak;

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
    size_t used = msl_test_match.memory.used;
    size_t allocations = msl_test_match.memory.allocation_count;
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                              &(MslCoreStageEvents) { 0 }) == 0);
    MSL_TEST_CHECK(msl_test_match.memory.sealed && msl_test_match.memory.used == used &&
          msl_test_match.memory.allocation_count == allocations);
    unsigned f = HSD_ObjAllocResolve(HSD_FObjGetAllocData())->used;
    unsigned a = HSD_ObjAllocResolve(HSD_AObjGetAllocData())->used;
    if (f > msl_test_fobj_peak) msl_test_fobj_peak = f;
    if (a > msl_test_aobj_peak) msl_test_aobj_peak = a;
    return 0;
}

static int msl_test_setup(unsigned players, unsigned first, unsigned opponent)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = players;
    config.stock_count = 4;
    for (unsigned i = 0; i < players; ++i) config.players[i].char_id = first;
    config.players[1].char_id = opponent;
    MSL_TEST_CHECK((msl_test_match.memory.arena == NULL
               ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)
               : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)) == 0);
    msl_test_fobj_peak = msl_test_aobj_peak = 0;
    for (unsigned i = 0; i < 150; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    return 0;
}

static void msl_test_place(unsigned port, float x, float y, float facing)
{
    Fighter* fp = msl_test_fighter(port);
    fp->cur_pos = (Vec3) { x, y, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = facing;
    if (y > 0) {
        ftCommon_8007D5D4(fp);
        ftCo_Fall_Enter(msl_test_match.fighters[port]);
    }
}

// Counter: Fox jabs into the counter window. Marth's counter returns fixed
// damage; Roy's ftMs_SpecialLw branch scales the reflected capsule by the
// incoming damage, so the two swordsmen must punish the same jab differently.
static int msl_test_counter(unsigned kind, float* fox_damage)
{
    MslCoreInput input = { 0 };
    int hit = 0;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 9, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftMs_SpecialLw_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 6; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftMs_MS_SpecialLw);
    input.p[1].buttons = PAD_BUTTON_A;
    MSL_TEST_CHECK(msl_test_step(&input) == 0);
    for (unsigned i = 0; i < 60; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        if (msl_test_fighter(0)->motion_id == ftMs_MS_SpecialLwHit) hit = 1;
    }
    *fox_damage = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "counter kind=%u hit=%d fox=%g self=%g\n", kind, hit, *fox_damage,
            msl_test_fighter(0)->dmg.x1830_percent);
    MSL_TEST_CHECK(hit);
    // Fox's jab is 4%: Roy reflects 1.5x of it, Marth deals his fixed 7%.
    MSL_TEST_CHECK(*fox_damage == (kind == 26 ? 6 : 7));
    MSL_TEST_CHECK(msl_test_fighter(0)->dmg.x1830_percent == 0);
    return 0;
}

// Flare Blade: holding the charge to its authored maximum releases on its own
// and the full-charge explosion damages Roy himself; the release also lands
// on a nearby Fox. Marth's Shield Breaker charge shares the states but never
// hurts its owner.
static int msl_test_flare_blade(unsigned kind, int full)
{
    MslCoreInput input = { 0 };
    int released = 0;
    unsigned loop_frames = 0;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 14, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftMs_SpecialN_Enter(msl_test_match.fighters[0]);
    input.p[0].buttons = PAD_BUTTON_B;
    for (unsigned i = 0; i < 400; ++i) {
        int m = msl_test_fighter(0)->motion_id;
        MSL_TEST_CHECK(msl_test_step(full ? &input : &msl_test_neutral) == 0);
        if (m == ftMs_MS_SpecialNLoop) loop_frames++;
        if (m == ftMs_MS_SpecialNEnd0 || m == ftMs_MS_SpecialNEnd1) released = 1;
        if (released && msl_test_fighter(0)->motion_id == ftCo_MS_Wait) break;
    }
    fprintf(stderr, "flare blade kind=%u full=%d loop=%u released=%d self=%g fox=%g\n", kind, full,
            loop_frames, released, msl_test_fighter(0)->dmg.x1830_percent,
            msl_test_fighter(1)->dmg.x1830_percent);
    MSL_TEST_CHECK(released);
    if (kind == 26 && full) {
        // PlFe.dat: 211 loop frames to the authored maximum, 50% dealt, 10%
        // recoil to Roy himself.
        MSL_TEST_CHECK(loop_frames == 211);
        MSL_TEST_CHECK(msl_test_fighter(1)->dmg.x1830_percent == 50);
        MSL_TEST_CHECK(msl_test_fighter(0)->dmg.x1830_percent == 10);
    } else if (kind == 26) {
        MSL_TEST_CHECK(msl_test_fighter(1)->dmg.x1830_percent == 6);
        MSL_TEST_CHECK(msl_test_fighter(0)->dmg.x1830_percent == 0);
    } else {
        // Marth's full Shield Breaker: 121 loop frames, 28%, no recoil.
        MSL_TEST_CHECK(loop_frames == 121);
        MSL_TEST_CHECK(msl_test_fighter(1)->dmg.x1830_percent == 28);
        MSL_TEST_CHECK(msl_test_fighter(0)->dmg.x1830_percent == 0);
    }
    return 0;
}

// Blazer: aerial up-B rises, then Roy is helpless until landing.
static int msl_test_blazer(void)
{
    int rose = 0, helpless = 0;
    float start;
    MSL_TEST_CHECK(msl_test_setup(2, 26, 26) == 0);
    msl_test_place(0, 0, 60, 1);
    msl_test_place(1, 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    start = msl_test_fighter(0)->cur_pos.y;
    ftMs_SpecialAirHi_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 120; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        if (msl_test_fighter(0)->cur_pos.y > start + 10) rose = 1;
        if (msl_test_fighter(0)->motion_id == ftCo_MS_FallSpecial) helpless = 1;
        if (msl_test_fighter(0)->motion_id == ftCo_MS_Wait) break;
    }
    fprintf(stderr, "blazer rose=%d helpless=%d\n", rose, helpless);
    MSL_TEST_CHECK(rose && helpless);
    return 0;
}

// Double-Edge Dance: repeated side-B advances the chain at least to its
// second stage and the swings land on a close Fox.
static int msl_test_dance(void)
{
    MslCoreInput input = { 0 };
    int deepest = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 26, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 10, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftMs_SpecialS_Enter(msl_test_match.fighters[0]);
    input.p[0].buttons = PAD_BUTTON_B;
    input.p[0].main_x = 80;
    for (unsigned i = 0; i < 120; ++i) {
        int m = msl_test_fighter(0)->motion_id;
        MSL_TEST_CHECK(msl_test_step(i % 8 < 3 ? &input : &msl_test_neutral) == 0);
        if (m >= ftMs_MS_SpecialS1 && m <= ftMs_MS_SpecialS4Lw && m - ftMs_MS_SpecialS1 > deepest)
            deepest = m - ftMs_MS_SpecialS1;
    }
    fprintf(stderr, "dance deepest=%d fox=%g\n", deepest, msl_test_fighter(1)->dmg.x1830_percent);
    MSL_TEST_CHECK(deepest >= ftMs_MS_SpecialS2Hi - ftMs_MS_SpecialS1);
    MSL_TEST_CHECK(msl_test_fighter(1)->dmg.x1830_percent > 0);
    return 0;
}

// Four Roys charging and countering at once keep the sealed pools inside the
// existing reserves with the article smoke's 20% headroom rule.
static int msl_test_four_roys(void)
{
    MSL_TEST_CHECK(msl_test_setup(4, 26, 26) == 0);
    for (unsigned p = 0; p < 4; ++p) msl_test_place(p, -60 + 40 * (int) p, 0, p < 2 ? 1 : -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftMs_SpecialN_Enter(msl_test_match.fighters[0]);
    ftMs_SpecialLw_Enter(msl_test_match.fighters[1]);
    ftMs_SpecialS_Enter(msl_test_match.fighters[2]);
    ftMs_SpecialHi_Enter(msl_test_match.fighters[3]);
    for (unsigned i = 0; i < 200; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    fprintf(stderr, "four-Roy FObj=%u/%u AObj=%u/%u\n", msl_test_fobj_peak, f->used + f->free,
            msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak * 5 < (f->used + f->free) * 4);
    MSL_TEST_CHECK(msl_test_aobj_peak * 5 < (a->used + a->free) * 4);
    return 0;
}

int main(int argc, char** argv)
{
    float roy_counter, marth_counter;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_counter(26, &roy_counter) || msl_test_counter(18, &marth_counter) ||
                 msl_test_flare_blade(26, 1) || msl_test_flare_blade(26, 0) ||
                 msl_test_flare_blade(18, 1) || msl_test_blazer() || msl_test_dance() ||
                 msl_test_four_roys();
    if (result == 0 && roy_counter == marth_counter) {
        fprintf(stderr, "Roy and Marth counters returned identical damage\n");
        result = -1;
    }
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
