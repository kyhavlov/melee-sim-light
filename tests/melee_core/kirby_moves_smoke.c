// Kirby's own specials and the copy pipeline with extracted data and the
// real scheduler: Final Cutter's beam item, the hammer, stone, inhaling a
// fighter, spitting it out, swallowing it for its hat, and firing the
// copied neutral special through the Kirby item kinds. Also the four-Kirby
// pool headroom.
#include "runtime/scalar.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftKirby/forward.h"
#include "ftKirby/ftkirby.h"
#include "ftKirby/ftkirbyspecialhi.h"
#include "it/types.h"
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

static int msl_test_item_seen(int kind)
{
    for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next)
        if (((Item*) g->user_data)->kind == kind) return 1;
    return 0;
}

// Final Cutter: rise, fall, and the beam item that crawls along the ground.
static int msl_test_final_cutter(float* fox)
{
    int beam = 0, top = 0;
    float peak = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 4, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 12, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftKb_SpecialHi_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 120; ++i) {
        int m;
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        m = msl_test_fighter(0)->motion_id;
        if (m == ftKb_MS_SpecialHi2 || m == ftKb_MS_SpecialHi3 || m == ftKb_MS_SpecialHi4) top = 1;
        if (msl_test_fighter(0)->cur_pos.y > peak) peak = msl_test_fighter(0)->cur_pos.y;
        beam |= msl_test_item_seen(It_Kind_Kirby_CBeam);
    }
    *fox = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "final cutter top=%d peak=%g beam=%d fox=%g\n", top, peak, beam, *fox);
    // PlKb.dat: the rise tops out near 45 units and the beam lands 15.28%.
    MSL_TEST_CHECK(beam && peak > 40);
    MSL_TEST_CHECK(*fox > 15.27f && *fox < 15.29f);
    return 0;
}

// Hammer and Stone on a close Fox.
static int msl_test_hammer_and_stone(float* hammer, float* stone)
{
    MSL_TEST_CHECK(msl_test_setup(2, 4, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 9, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftKb_SpecialS_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 80; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    *hammer = msl_test_fighter(1)->dmg.x1830_percent;
    MSL_TEST_CHECK(msl_test_setup(2, 4, 1) == 0);
    msl_test_place(0, 4, 40, 1);
    msl_test_place(1, 4, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftKb_SpecialAirLw_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 120; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    *stone = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "hammer fox=%g stone fox=%g\n", *hammer, *stone);
    // Hammer 16% on the close Fox, Stone 18%.
    MSL_TEST_CHECK(*hammer == 16 && *stone == 18);
    return 0;
}

// Inhale the opponent, then either spit (A) or swallow (B) for the hat, and
// fire the copied neutral special. Returns the hat kind and the item kind seen.
static int msl_test_copy(unsigned opponent, int swallow, int copy_item, float* opp_damage,
                         int* hat_kind)
{
    MslCoreInput input = { 0 };
    int captured = 0, item = 0, fired = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 4, opponent) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 10, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftKb_SpecialN_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 90; ++i) {
        int m = msl_test_fighter(0)->motion_id;
        if (m == ftKb_MS_SpecialNCapture0 || m == ftKb_MS_SpecialNCapture1 || m == ftKb_MS_EatWait ||
            m == ftKb_MS_Eat)
            captured = 1;
        // The wait state's IASA reads press edges: B or stick-down swallows
        // (SpecialNDrink), A spits (SpecialNSpit). Pulse the button so an
        // edge lands inside EatWait rather than during the capture states.
        input.p[0].buttons =
            (m == ftKb_MS_EatWait && (i % 4) < 2) ? (swallow ? PAD_BUTTON_B : PAD_BUTTON_A) : 0;
        MSL_TEST_CHECK(msl_test_step(&input) == 0);
    }
    *hat_kind = (int) msl_test_fighter(0)->fv.kb.hat.kind;
    if (swallow) {
        msl_test_place(0, 0, 0, 1);
        msl_test_place(1, 30, 0, -1);
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        ftKb_SpecialN_Enter(msl_test_match.fighters[0]);
        input.p[0].buttons = PAD_BUTTON_B;
        for (unsigned i = 0; i < 90; ++i) {
            MSL_TEST_CHECK(msl_test_step(i < 20 ? &input : &msl_test_neutral) == 0);
            item |= msl_test_item_seen(copy_item);
            if (msl_test_fighter(0)->motion_id >= ftKb_MS_MrSpecialN) fired = 1;
        }
    }
    *opp_damage = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "copy opp=%u swallow=%d captured=%d hat=%d fired=%d item=%d opp_dmg=%g\n",
            opponent, swallow, captured, *hat_kind, fired, item, *opp_damage);
    MSL_TEST_CHECK(captured);
    if (swallow) {
        // The hat takes the swallowed fighter's kind and the copied special
        // spawns its Kirby item kind.
        MSL_TEST_CHECK(*hat_kind == (int) opponent && fired && item);
    } else {
        // Spit: the hat stays empty (FTKIND_KIRBY) and the spat Fox takes 10%.
        MSL_TEST_CHECK(*hat_kind == 4 && *opp_damage == 10);
    }
    return 0;
}

static int msl_test_four_kirbys(void)
{
    MSL_TEST_CHECK(msl_test_setup(4, 4, 4) == 0);
    for (unsigned p = 0; p < 4; ++p) msl_test_place(p, -60 + 40 * (int) p, 0, p < 2 ? 1 : -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftKb_SpecialHi_Enter(msl_test_match.fighters[0]);
    ftKb_SpecialS_Enter(msl_test_match.fighters[1]);
    ftKb_SpecialN_Enter(msl_test_match.fighters[2]);
    ftKb_SpecialHi_Enter(msl_test_match.fighters[3]);
    for (unsigned i = 0; i < 200; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    fprintf(stderr, "four-Kirby FObj=%u/%u AObj=%u/%u\n", msl_test_fobj_peak, f->used + f->free,
            msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak * 5 < (f->used + f->free) * 4);
    MSL_TEST_CHECK(msl_test_aobj_peak * 5 < (a->used + a->free) * 4);
    return 0;
}

int main(int argc, char** argv)
{
    float cutter_fox, hammer_fox, stone_fox, spit_fox, fox_copy, bowser_copy;
    int hat_spit, hat_fox, hat_bowser;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_final_cutter(&cutter_fox) ||
                 msl_test_hammer_and_stone(&hammer_fox, &stone_fox) ||
                 msl_test_copy(1, 0, 0, &spit_fox, &hat_spit) ||
                 msl_test_copy(1, 1, It_Kind_Kirby_FoxLaser, &fox_copy, &hat_fox) ||
                 msl_test_copy(5, 1, It_Kind_Kirby_KoopaFlame, &bowser_copy, &hat_bowser) ||
                 msl_test_four_kirbys();
    if (result == 0) {
        // Copied Fox blaster: 11% on the Fox; copied Bowser fire breath
        // spawned from the live mouth bone (parts mask anchor 12): 20.8%
        // over the burst.
        if (fox_copy != 11 || !(bowser_copy > 20.79f && bowser_copy < 20.81f)) {
            fprintf(stderr, "copy payload mismatch\n");
            result = -1;
        }
    }
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
