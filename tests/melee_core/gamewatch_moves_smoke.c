#include "runtime/scalar.h"
#include "runtime/savestate.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftGameWatch/forward.h"
#include "ftGameWatch/ftGw_Init.h"
#include "ftGameWatch/ftGw_SpecialS.h"
#include "ftGameWatch/ftGw_SpecialHi.h"
#include "ftGameWatch/ftGw_AttackAir.h"
#include "ftGameWatch/ftGw_SpecialN.h"
#include "ftGameWatch/ftGw_SpecialLw.h"
#include "it/types.h"
#include "it/items/itfoxlaser.h"
#include "it/items/itsamusmissile.h"
#include "it/items/itgamewatchchef.h"
#include <baselib/gobj.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/objalloc.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int msl_test_setup(unsigned players, unsigned opponent)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = players;
    config.stock_count = 4;
    for (unsigned i = 0; i < players; ++i) config.players[i].char_id = 24;
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

static int msl_test_judge_history(void)
{
    unsigned seen = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
    Fighter* fp = msl_test_fighter(0);
    for (unsigned i = 0; i < 512; ++i) {
        int previous = fp->fv.gw.x222C_judgeVar1;
        int older = fp->fv.gw.x2230_judgeVar2;
        int roll = ftGw_SpecialS_GetRandomInt(msl_test_match.fighters[0]);
        MSL_TEST_CHECK(roll >= 0 && roll < 9 && roll != previous && roll != older);
        MSL_TEST_CHECK(fp->fv.gw.x2230_judgeVar2 == previous);
        seen |= 1U << roll;
    }
    MSL_TEST_CHECK(seen == 0x1ff);
    return 0;
}

static int msl_test_judge_moves(int hit, int air)
{
    unsigned seen = 0;
    for (unsigned trial = 0; trial < 128 && seen != 0x1ff; ++trial) {
        MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
        msl_test_place(0, -10, air ? 80 : 0, 1);
        msl_test_place(1, hit ? 0 : 60, air ? 80 : 0, -1);
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        for (unsigned i = 0; i < trial; ++i)
            ftGw_SpecialS_GetRandomInt(msl_test_match.fighters[0]);
        Fighter* fp = msl_test_fighter(0);
        if (air) ftGw_SpecialAirS_Enter(msl_test_match.fighters[0]);
        else ftGw_SpecialS_Enter(msl_test_match.fighters[0]);
        int roll = fp->fv.gw.x222C_judgeVar1;
        MSL_TEST_CHECK(fp->motion_id == (air ? ftGw_MS_SpecialAirS1 : ftGw_MS_SpecialS1) + roll);
        int article = 0;
        float damage = 0, self_damage = 0;
        for (unsigned frame = 0; frame < 90; ++frame) {
            MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
            article |= fp->fv.gw.x2264_judgementGObj != NULL;
            if (msl_test_fighter(1)->dmg.x1830_percent > damage) damage = msl_test_fighter(1)->dmg.x1830_percent;
            if (fp->dmg.x1830_percent > self_damage) self_damage = fp->dmg.x1830_percent;
        }
        printf("Judge number=%d hit=%d air=%d damage=%g self=%g\n", roll + 1, hit, air,
               damage, self_damage);
        MSL_TEST_CHECK(article && !fp->fv.gw.x2264_judgementGObj);
        MSL_TEST_CHECK((damage > 0) == hit);
        MSL_TEST_CHECK((self_damage > 0) == (roll == 0));
        seen |= 1U << roll;
    }
    MSL_TEST_CHECK(seen == 0x1ff);
    return 0;
}

static int msl_test_chef(void)
{
    unsigned seen = 0;
    MSL_TEST_CHECK(msl_test_setup(4, 24) == 0);
    for (unsigned p = 0; p < 4; ++p) msl_test_place(p, -60 + 40 * (int)p, 0, 1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        for (unsigned p = 0; p < 4; ++p) ftGw_SpecialN_Enter(msl_test_match.fighters[p]);
        for (unsigned frame = 0; frame < 65; ++frame) {
            int last[4], older[4];
            for (unsigned p = 0; p < 4; ++p) {
                last[p] = msl_test_fighter(p)->fv.gw.x2240_chefVar1;
                older[p] = msl_test_fighter(p)->fv.gw.x2244_chefVar2;
            }
            MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
            for (unsigned p = 0; p < 4; ++p) {
                int now = msl_test_fighter(p)->fv.gw.x2240_chefVar1;
                if (now != last[p]) {
                    MSL_TEST_CHECK(now >= 0 && now < 5 && now != older[p]);
                    MSL_TEST_CHECK(msl_test_fighter(p)->fv.gw.x2244_chefVar2 == last[p]);
                    seen |= 1U << now;
                }
            }
        }
    }
    MSL_TEST_CHECK(seen == 31);
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    printf("four Chef FObj=%u/%u AObj=%u/%u\n", msl_test_fobj_peak, f->used + f->free,
           msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak * 5 <= (f->used + f->free) * 4);
    MSL_TEST_CHECK(msl_test_aobj_peak * 5 <= (a->used + a->free) * 4);
    return 0;
}

static int msl_test_bucket(int active, int missile)
{
    MslCoreInput input = { 0 };
    MSL_TEST_CHECK(msl_test_setup(2, missile ? 13 : 22) == 0);
    msl_test_place(0, -20, 0, 1);
    msl_test_place(1, 55, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    if (active) {
        input.p[0].buttons = PAD_BUTTON_B;
        ftGw_SpecialLw_Enter(msl_test_match.fighters[0]);
    }
    for (unsigned i = 0; i < 20; ++i) MSL_TEST_CHECK(msl_test_step(&input) == 0);
    for (unsigned shot = 0; shot < 3; ++shot) {
        Vec3 pos = { 30, 8, 0 };
        if (missile) it_802B62D0(msl_test_match.fighters[1], &pos, true, -1);
        else it_8029C6A4(3.14159265358979323846F, 4, msl_test_match.fighters[1], &pos, It_Kind_Falco_Laser);
        for (unsigned i = 0; i < 65; ++i) MSL_TEST_CHECK(msl_test_step(&input) == 0);
        if (active && !missile) MSL_TEST_CHECK(msl_test_fighter(0)->fv.gw.x2238_panicCharge == (int)shot + 1);
    }
    Fighter* fp = msl_test_fighter(0);
    printf("bucket active=%d missile=%d charge=%d stored=%d hurt=%g\n", active, missile,
           fp->fv.gw.x2238_panicCharge, fp->fv.gw.x223C_panicDamage, fp->dmg.x1830_percent);
    MSL_TEST_CHECK((fp->fv.gw.x2238_panicCharge == 3) == (active && !missile));
    MSL_TEST_CHECK((fp->dmg.x1830_percent > 0) == (!active || missile));
    if (active && !missile) {
        int damage = fp->fv.gw.x223C_panicDamage;
        ftGameWatchAttributes* attrs = fp->dat_attrs;
        int expected = (int)(damage * attrs->x78_GAMEWATCH_PANIC_DAMAGE_MUL);
        expected = (int)(expected + attrs->x74_GAMEWATCH_PANIC_DAMAGE_ADD);
        ftGw_SpecialLw_Enter(msl_test_match.fighters[0]);
        MSL_TEST_CHECK(fp->motion_id == ftGw_MS_SpecialLwShoot);
        MSL_TEST_CHECK(fp->cmd_vars[1] == expected && expected > damage);
        MSL_TEST_CHECK(fp->fv.gw.x2238_panicCharge == 0 && fp->fv.gw.x223C_panicDamage == 0);
        for (unsigned i = 0; i < 90; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(!fp->fv.gw.x2268_panicGObj);
    }
    return 0;
}

static int msl_test_four_restore(unsigned move, int interrupt)
{
    static MslCoreMatch copy;
    MSL_TEST_CHECK(msl_test_setup(4, 24) == 0);
    for (unsigned p = 0; p < 4; ++p) {
        msl_test_place(p, -60 + 40 * (int)p, move == 4 ? 100 : 0, p < 2 ? -1 : 1);
    }
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    for (unsigned p = 0; p < 4; ++p) {
        Fighter* fp = msl_test_fighter(p);
        if (move == 0) ftGw_SpecialS_Enter(msl_test_match.fighters[p]);
        if (move == 1) ftGw_SpecialN_Enter(msl_test_match.fighters[p]);
        if (move == 2) ftGw_SpecialHi_Enter(msl_test_match.fighters[p]);
        if (move == 3) {
            fp->fv.gw.x2238_panicCharge = 3;
            fp->fv.gw.x223C_panicDamage = 20;
            ftGw_SpecialLw_Enter(msl_test_match.fighters[p]);
        }
        if (move == 4) ftGw_AttackAirN_Enter(msl_test_match.fighters[p]);
        if (move == 5) {
            fp->fv.gw.x2238_panicCharge = 2;
            fp->fv.gw.x223C_panicDamage = 9;
            ftGw_SpecialLw_Enter(msl_test_match.fighters[p]);
        }
        if (move == 6) {
            Vec3 pos = { fp->cur_pos.x, 100, 0 };
            for (unsigned trajectory = 0; trajectory < 5; ++trajectory) {
                HSD_GObj* item = it_802C837C(msl_test_match.fighters[p], &pos,
                    It_Kind_GameWatch_Chef, trajectory, fp->facing_dir);
                MSL_TEST_CHECK(item != NULL);
                MSL_TEST_CHECK(((Item*)item->user_data)->xDD4_itemVar.gamewatchchef.x4 == (int)trajectory);
            }
            ftGw_SpecialHi_Enter(msl_test_match.fighters[p]);
        }
    }
    for (unsigned i = 0; i < (move == 6 ? 1U : 18U); ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    int count = 0;
    for (HSD_GObj* item = msl_test_match.gobj.entities->items; item; item = item->next) ++count;
    printf("four restore move=%u live articles=%d\n", move, count);
    MSL_TEST_CHECK(move == 5 || count >= (move == 6 ? 24 : 4));
    MSL_TEST_CHECK(msl_core_match_init(&copy, &msl_test_game_data, &msl_test_match.config, &msl_test_neutral) == 0);
    size_t size = msl_core_match_save_size(&msl_test_match), written;
    void* saved = malloc(size);
    MSL_TEST_CHECK(saved != NULL);
    MSL_TEST_CHECK(msl_core_match_save(&msl_test_match, saved, size, &written) == 0);
    MSL_TEST_CHECK(msl_core_match_copy(&copy, &msl_test_match) == 0);
    MSL_TEST_CHECK(msl_core_match_restore(&msl_test_match, saved, written) == 0);
    free(saved);
    for (unsigned i = 0; i < 140; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_core_match_step(&copy, &msl_test_neutral, copy.random_seed,
                                         &(MslCoreStageEvents) { 0 }) == 0);
        MSL_TEST_CHECK(memcmp(&msl_test_match.output, &copy.output, sizeof(copy.output)) == 0);
        MSL_TEST_CHECK(msl_test_match.random_seed == copy.random_seed);
        for (unsigned p = 0; p < 4; ++p) {
            Fighter* fp = msl_test_fighter(p);
            Fighter* other = copy.fighters[p]->user_data;
            MSL_TEST_CHECK(fp->fv.gw.x222C_judgeVar1 == other->fv.gw.x222C_judgeVar1);
            MSL_TEST_CHECK(fp->fv.gw.x2230_judgeVar2 == other->fv.gw.x2230_judgeVar2);
            MSL_TEST_CHECK(fp->fv.gw.x2240_chefVar1 == other->fv.gw.x2240_chefVar1);
            MSL_TEST_CHECK(fp->fv.gw.x2244_chefVar2 == other->fv.gw.x2244_chefVar2);
            MSL_TEST_CHECK(fp->fv.gw.x2238_panicCharge == other->fv.gw.x2238_panicCharge);
            MSL_TEST_CHECK(fp->fv.gw.x223C_panicDamage == other->fv.gw.x223C_panicDamage);
        }
        HSD_GObj* left = msl_test_match.gobj.entities->items;
        HSD_GObj* right = copy.gobj.entities->items;
        for (; left && right; left = left->next, right = right->next) {
            Item* a = left->user_data;
            Item* b = right->user_data;
            MSL_TEST_CHECK(a->kind == b->kind && memcmp(&a->pos, &b->pos, sizeof(a->pos)) == 0);
            if (a->kind == It_Kind_GameWatch_Chef)
                MSL_TEST_CHECK(a->xDD4_itemVar.gamewatchchef.x4 == b->xDD4_itemVar.gamewatchchef.x4);
        }
        MSL_TEST_CHECK(left == NULL && right == NULL);
    }
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    if (interrupt) {
        for (unsigned p = 0; p < 4; ++p) {
            ftGw_SpecialS_Enter(msl_test_match.fighters[p]);
        }
        for (unsigned i = 0; i < 18; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        for (unsigned p = 0; p < 4; ++p) {
            MSL_TEST_CHECK(msl_test_fighter(p)->fv.gw.x2264_judgementGObj != NULL);
            ftGw_Init_OnDamage(msl_test_match.fighters[p]);
            MSL_TEST_CHECK(msl_test_fighter(p)->fv.gw.x2264_judgementGObj == NULL);
        }
    }
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    printf("four move=%u peak FObj=%u/%u AObj=%u/%u\n", move,
           msl_test_fobj_peak, f->used + f->free, msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak * 5 <= (f->used + f->free) * 4);
    MSL_TEST_CHECK(msl_test_aobj_peak * 5 <= (a->used + a->free) * 4);
    msl_core_match_destroy(&copy);
    return 0;
}

static int msl_test_aerial_interrupt(unsigned move)
{
    MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
    msl_test_place(0, -30, 100, 1);
    msl_test_place(1, 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    Fighter* fp = msl_test_fighter(0);
    fp->input.lstick.x = move == 1 ? -1 : 0;
    fp->input.lstick.y = move == 2 ? 1 : 0;
    ftGw_AttackAirN_DecideAction(msl_test_match.fighters[0]);
    HSD_GObj** article = move == 0 ? &fp->fv.gw.x2258_parachuteGObj :
                         move == 1 ? &fp->fv.gw.x225C_turtleGObj :
                                     &fp->fv.gw.x2260_sparkyGObj;
    for (unsigned i = 0; i < 30 && !*article; ++i)
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(*article != NULL);
    ftGw_Init_OnDamage(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(*article == NULL);
    ftCo_Fall_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 30; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(*article == NULL);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_judge_history() || msl_test_judge_moves(0, 0) ||
                 msl_test_judge_moves(1, 0) || msl_test_judge_moves(0, 1) ||
                 msl_test_chef() || msl_test_bucket(1, 0) ||
                 msl_test_bucket(0, 0) || msl_test_bucket(1, 1) ||
                 msl_test_four_restore(0, 1) || msl_test_four_restore(1, 0) ||
                 msl_test_four_restore(2, 0) || msl_test_four_restore(3, 0) ||
                 msl_test_four_restore(4, 0) || msl_test_four_restore(5, 0) ||
                 msl_test_four_restore(6, 0) || msl_test_aerial_interrupt(0) ||
                 msl_test_aerial_interrupt(1) || msl_test_aerial_interrupt(2);
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
