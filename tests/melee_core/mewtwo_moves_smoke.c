#include "runtime/scalar.h"
#include "runtime/savestate.h"
#include "ft/fighter.h"
#include "ft/ft_0D31.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftCommon/ftCo_DamageFall.h"
#include "ftCommon/ftCo_CaptureCut.h"
#include "ftMewtwo/forward.h"
#include "ftMewtwo/ftMt_Init.h"
#include "ftMewtwo/ftMt_SpecialHi.h"
#include "ftMewtwo/ftMt_SpecialLw.h"
#include "ftMewtwo/ftMt_SpecialN.h"
#include "ftMewtwo/ftMt_SpecialS.h"
#include "it/types.h"
#include "it/items/itfoxlaser.h"
#include "it/items/itmewtwoshadowball.h"
#include <baselib/gobj.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/objalloc.h>
#include <dolphin/pad.h>
#include <math.h>
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
    for (unsigned i = 0; i < players; ++i) config.players[i].char_id = 16;
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

// Confusion's catch is authored in PlMt.dat and releases via cmd_vars[0].
static int msl_test_confusion_grab(int in_range, int shield, int air)
{
    MslCoreInput input = { 0 };
    float distance = in_range ? 12 : 60;
    int caught = 0;
    int thrown = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
    msl_test_place(0, -10, air ? 65 : 0, 1);
    // Fox starts above Mewtwo and falls into the aerial catch window.
    msl_test_place(1, -10 + distance, air ? 100 : 0, -1);
    if (shield) input.p[1].buttons = PAD_TRIGGER_R;
    for (unsigned i = 0; i < 2; ++i) MSL_TEST_CHECK(msl_test_step(&input) == 0);
    if (air) ftMt_SpecialAirS_Enter(msl_test_match.fighters[0]);
    else ftMt_SpecialS_Enter(msl_test_match.fighters[0]);
    for (unsigned frame = 0; frame < 100; ++frame) {
        MSL_TEST_CHECK(msl_test_step(&input) == 0);
        Fighter* mt = msl_test_fighter(0);
        Fighter* victim = msl_test_fighter(1);
        if (mt->victim_gobj == msl_test_match.fighters[1]) {
            caught = 1;
            MSL_TEST_CHECK(victim->victim_gobj == msl_test_match.fighters[0]);
        }
        if (victim->motion_id == ftCo_MS_ThrownMewtwo ||
            victim->motion_id == ftCo_MS_ThrownMewtwoAir) thrown = 1;
    }
    fprintf(stderr, "grab distance=%g shield=%d air=%d caught=%d thrown=%d damage=%g\n",
            distance, shield, air, caught, thrown, msl_test_fighter(1)->dmg.x1830_percent);
    MSL_TEST_CHECK(caught == in_range);
    MSL_TEST_CHECK(thrown == caught);
    MSL_TEST_CHECK(msl_test_fighter(0)->victim_gobj == NULL);
    MSL_TEST_CHECK(msl_test_fighter(1)->victim_gobj == NULL);
    MSL_TEST_CHECK((msl_test_fighter(1)->dmg.x1830_percent > 0) == caught);
    return 0;
}

static int msl_test_confusion_reflection(int active, int transition, int shadow)
{
    Item_GObj* projectile = NULL;
    int reflected = 0;
    MSL_TEST_CHECK(msl_test_setup(2, shadow ? 16 : 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 55, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    if (active) {
        ftMt_SpecialS_Enter(msl_test_match.fighters[0]);
        for (unsigned i = 0; i < 40 && !msl_test_fighter(0)->reflecting; ++i)
            MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        MSL_TEST_CHECK(msl_test_fighter(0)->reflecting);
        if (transition) {
            ftMt_SpecialS_GroundToAir(msl_test_match.fighters[0]);
            MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftMt_MS_SpecialAirS && msl_test_fighter(0)->reflecting);
            ftMt_SpecialAirS_AirToGround(msl_test_match.fighters[0]);
            MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftMt_MS_SpecialS && msl_test_fighter(0)->reflecting);
        }
    }
    Vec3 pos = { 20, 10, 0 };
    if (shadow) {
        ftMewtwoAttributes* attrs = msl_test_fighter(1)->dat_attrs;
        projectile = it_802C5000(msl_test_match.fighters[1], &pos, FtPart_RShoulderN,
                            It_Kind_Mewtwo_ShadowBall, -1);
        MSL_TEST_CHECK(projectile != NULL);
        it_802C53F0(projectile, &pos, (float) M_PI,
                    attrs->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES,
                    attrs->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES);
    } else {
        it_8029C6A4((float) M_PI, 4, msl_test_match.fighters[1], &pos, It_Kind_Fox_Laser);
        for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next) {
            if (((Item*) g->user_data)->kind == It_Kind_Fox_Laser) projectile = g;
        }
    }
    MSL_TEST_CHECK(projectile != NULL);
    for (unsigned i = 0; i < 20; ++i) {
        int live = 0;
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next) {
            if (g == projectile) {
                Item* item = g->user_data;
                live = 1;
                if (item->x40_vel.x > 0) {
                    reflected = 1;
                    MSL_TEST_CHECK(item->owner == msl_test_match.fighters[1]);
                }
            }
        }
        if (!live) break;
    }
    fprintf(stderr, "reflection active=%d transition=%d shadow=%d reflected=%d damage=%g\n",
            active, transition, shadow, reflected, msl_test_fighter(0)->dmg.x1830_percent);
    MSL_TEST_CHECK(reflected == active);
    MSL_TEST_CHECK((msl_test_fighter(0)->dmg.x1830_percent == 0) == active);
    for (unsigned i = 0; i < 60; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(!msl_test_fighter(0)->reflecting && msl_test_fighter(0)->reflect_hit_cb == NULL);
    return 0;
}

static int msl_test_disable(int facing, int air)
{
    int seen_item = 0;
    int bound = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
    msl_test_place(0, -10, air ? 50 : 0, 1);
    msl_test_place(1, 10, air ? 90 : 0, facing ? -1 : 1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    if (air) ftMt_SpecialAirLw_Enter(msl_test_match.fighters[0]);
    else ftMt_SpecialLw_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 90; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        if (msl_test_fighter(0)->fv.mt.x222C_disableGObj != NULL) seen_item = 1;
        if (msl_test_fighter(1)->motion_id == ftCo_MS_DamageBind) bound = 1;
    }
    fprintf(stderr, "disable facing=%d air=%d item=%d bound=%d damage=%g\n",
            facing, air, seen_item, bound, msl_test_fighter(1)->dmg.x1830_percent);
    MSL_TEST_CHECK(seen_item);
    MSL_TEST_CHECK(bound == (facing && !air));
    MSL_TEST_CHECK((msl_test_fighter(1)->dmg.x1830_percent > 0) == facing);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x222C_disableGObj == NULL);
    return 0;
}

static int msl_test_teleport(int air, int x, int y)
{
    MslCoreInput input = { 0 };
    int travel = 0;
    int recovery = 0;
    int helpless = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    msl_test_place(0, -30, air ? 70 : 0, 1);
    msl_test_place(1, 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    input.p[0].main_x = x;
    input.p[0].main_y = y;
    if (air) ftMt_SpecialAirHiStart_Enter(msl_test_match.fighters[0]);
    else ftMt_SpecialHiStart_Enter(msl_test_match.fighters[0]);
    float start_x = msl_test_fighter(0)->cur_pos.x;
    float travel_x = start_x;
    float start_y = msl_test_fighter(0)->cur_pos.y;
    float travel_y = start_y;
    for (unsigned i = 0; i < 100; ++i) {
        MSL_TEST_CHECK(msl_test_step(&input) == 0);
        int state = msl_test_fighter(0)->motion_id;
        if (state == ftMt_MS_SpecialHiLost || state == ftMt_MS_SpecialAirHiLost) {
            travel = 1;
            MSL_TEST_CHECK(msl_test_fighter(0)->invisible);
            MSL_TEST_CHECK(msl_test_fighter(0)->x1968_jumpsUsed == msl_test_fighter(0)->co_attrs.max_jumps);
            travel_x = msl_test_fighter(0)->cur_pos.x;
            travel_y = msl_test_fighter(0)->cur_pos.y;
        }
        if (state == ftMt_MS_SpecialHi || state == ftMt_MS_SpecialAirHi) recovery = 1;
        if (state == ftCo_MS_FallSpecial) helpless = 1;
    }
    fprintf(stderr, "teleport air=%d direction=%d,%d travel=%d recovery=%d helpless=%d dx=%g\n",
            air, x, y, travel, recovery, helpless, travel_x - start_x);
    MSL_TEST_CHECK(travel && recovery);
    MSL_TEST_CHECK(!msl_test_fighter(0)->invisible);
    if (x > 0) MSL_TEST_CHECK(travel_x > start_x);
    if (x < 0) MSL_TEST_CHECK(travel_x < start_x);
    if (x == 0 && y >= 0) MSL_TEST_CHECK(travel_y > start_y);
    if (y < 0) MSL_TEST_CHECK(travel_y < start_y);
    if (air && y >= 0) MSL_TEST_CHECK(helpless);
    return 0;
}

static int msl_test_confusion_interrupt(void)
{
    MSL_TEST_CHECK(msl_test_setup(2, 1) == 0);
    msl_test_place(0, -10, 0, 1);
    msl_test_place(1, 2, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftMt_SpecialS_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 30 && !msl_test_fighter(0)->victim_gobj; ++i)
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->victim_gobj == msl_test_match.fighters[1]);
    // The source capture-cut owner must clear both sides before release.
    ftCo_CaptureCut_Enter(msl_test_match.fighters[1]);
    MSL_TEST_CHECK(msl_test_fighter(0)->victim_gobj == NULL && msl_test_fighter(1)->victim_gobj == NULL);
    for (unsigned i = 0; i < 90; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(1)->dmg.x1830_percent == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->victim_gobj == NULL && msl_test_fighter(1)->victim_gobj == NULL);
    return 0;
}

static int msl_test_confusion_air_boost(void)
{
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    msl_test_place(0, 0, 70, 1);
    ftMt_SpecialAirS_Enter(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_fighter(0)->self_vel.y > 0);
    msl_test_fighter(0)->self_vel.y = -1;
    ftMt_SpecialAirS_Enter(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_fighter(0)->self_vel.y == -1);
    ftMt_SpecialAirS_AirToGround(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(!msl_test_fighter(0)->fv.mt.x223C_isConfusionBoost);
    ftCommon_8007D5D4(msl_test_fighter(0));
    ftMt_SpecialAirS_Enter(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_fighter(0)->self_vel.y > 0);
    return 0;
}

static int msl_test_disable_interrupt(int death)
{
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    ftMt_SpecialLw_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 40 && !msl_test_fighter(0)->fv.mt.x222C_disableGObj; ++i)
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x222C_disableGObj != NULL);
    MSL_TEST_CHECK(msl_test_fighter(0)->take_dmg_cb && msl_test_fighter(0)->death2_cb);
    if (death) msl_test_fighter(0)->death2_cb(msl_test_match.fighters[0]);
    else msl_test_fighter(0)->take_dmg_cb(msl_test_match.fighters[0]);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x222C_disableGObj == NULL);
    for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next)
        MSL_TEST_CHECK(((Item*) g->user_data)->kind != It_Kind_Mewtwo_Disable);
    for (unsigned i = 0; i < 60; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x222C_disableGObj == NULL);
    return 0;
}

static int msl_test_charge_until(int full)
{
    ftMt_SpecialN_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 400; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        Fighter* fp = msl_test_fighter(0);
        int maximum = ((ftMewtwoAttributes*) fp->dat_attrs)->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES;
        if (full ? fp->motion_id == ftMt_MS_SpecialNLoopFull
                 : fp->fv.mt.x2234_shadowBallCharge >= maximum / 3) {
            MSL_TEST_CHECK(fp->fv.mt.x2230_shadowHeldGObj != NULL);
            return 0;
        }
    }
    MSL_TEST_CHECK(0 && "Shadow Ball failed to charge");
}

static int msl_test_shadow_cancel(void)
{
    MslCoreInput input = { 0 };
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    MSL_TEST_CHECK(msl_test_charge_until(0) == 0);
    int charge = msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge;
    input.p[0].buttons = PAD_TRIGGER_R;
    MSL_TEST_CHECK(msl_test_step(&input) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftMt_MS_SpecialNCancel);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj == NULL);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == charge);
    for (unsigned i = 0; i < 40; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == charge);
    ftMt_SpecialN_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 50; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge > charge);
    return 0;
}

static int msl_test_shadow_interrupt(int full, int death)
{
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    MSL_TEST_CHECK(msl_test_charge_until(full) == 0);
    int charge = msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge;
    if (death) {
        ftCo_800D3BC8(msl_test_match.fighters[0]);
    } else {
        ftCommon_8007DB58(msl_test_match.fighters[0]);
        ftCo_80090780(msl_test_match.fighters[0]);
    }
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj == NULL);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2238_shadowBallGObj == NULL);
    // Source Init callbacks preserve a full charge on damage, never on death.
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == (full && !death ? charge : 0));
    return 0;
}

static int msl_test_shadow_restore(void)
{
    static MslCoreMatch copy;
    void* saved;
    size_t size, written;
    MslCoreInput input = { 0 };
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    MSL_TEST_CHECK(msl_test_charge_until(0) == 0);
    MSL_TEST_CHECK(msl_core_match_init(&copy, &msl_test_game_data, &msl_test_match.config, &msl_test_neutral) == 0);
    size = msl_core_match_save_size(&msl_test_match);
    saved = malloc(size);
    MSL_TEST_CHECK(saved != NULL);
    MSL_TEST_CHECK(msl_core_match_save(&msl_test_match, saved, size, &written) == 0);
    MSL_TEST_CHECK(msl_core_match_copy(&copy, &msl_test_match) == 0);
    MSL_TEST_CHECK(msl_core_match_restore(&msl_test_match, saved, written) == 0);
    free(saved);
    Fighter* other = copy.fighters[0]->user_data;
    MSL_TEST_CHECK(other->fv.mt.x2230_shadowHeldGObj != msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj);
    MSL_TEST_CHECK(((Item*) other->fv.mt.x2230_shadowHeldGObj->user_data)->owner == copy.fighters[0]);
    for (unsigned i = 0; i < 100; ++i) {
        input.p[0].buttons = i == 20 ? PAD_BUTTON_B : 0;
        MSL_TEST_CHECK(msl_test_step(&input) == 0);
        MSL_TEST_CHECK(msl_core_match_step(&copy, &input, copy.random_seed,
                                  &(MslCoreStageEvents) { 0 }) == 0);
        MSL_TEST_CHECK(memcmp(&msl_test_match.output, &copy.output, sizeof(msl_test_match.output)) == 0);
        MSL_TEST_CHECK(msl_test_match.random_seed == copy.random_seed);
    }
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj == NULL);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == 0);
    msl_core_match_destroy(&copy);
    return 0;
}

static int msl_test_shadow_air(int cancel)
{
    MslCoreInput input = { 0 };
    int released = 0;
    int recoil = 0;
    MSL_TEST_CHECK(msl_test_setup(2, 16) == 0);
    msl_test_place(0, 0, 140, 1);
    ftMt_SpecialAirN_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 100 && msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == 0; ++i)
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id == ftMt_MS_SpecialAirNLoop);
    int charge = msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge;
    MSL_TEST_CHECK(charge > 0 && msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj != NULL);
    input.p[0].buttons = cancel ? PAD_TRIGGER_R : PAD_BUTTON_B;
    MSL_TEST_CHECK(msl_test_step(&input) == 0);
    MSL_TEST_CHECK(msl_test_fighter(0)->motion_id ==
          (cancel ? ftMt_MS_SpecialAirNCancel : ftMt_MS_SpecialAirNEnd));
    for (unsigned i = 0; i < 40; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        if (msl_test_fighter(0)->self_vel.x < 0) recoil = 1;
        for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next) {
            Item* item = g->user_data;
            if (item->kind == It_Kind_Mewtwo_ShadowBall &&
                item->msid >= 1 && item->msid <= 8) {
                released = 1;
                MSL_TEST_CHECK(item->owner == msl_test_match.fighters[0]);
                MSL_TEST_CHECK(item->xDD4_itemVar.mewtwoshadowball.x18 == charge);
            }
        }
    }
    MSL_TEST_CHECK(released == !cancel && recoil == !cancel);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2230_shadowHeldGObj == NULL);
    MSL_TEST_CHECK(msl_test_fighter(0)->fv.mt.x2234_shadowBallCharge == (cancel ? charge : 0));
    return 0;
}

static int msl_test_simultaneous_specials(unsigned move)
{
    unsigned seen = 0;
    MSL_TEST_CHECK(msl_test_setup(4, 16) == 0);
    for (unsigned p = 0; p < 4; ++p)
        msl_test_place(p, -60 + 40 * (int) p, 0, p < 2 ? -1 : 1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    for (unsigned p = 0; p < 4; ++p) {
        if (move == 0) ftMt_SpecialS_Enter(msl_test_match.fighters[p]);
        if (move == 1) ftMt_SpecialLw_Enter(msl_test_match.fighters[p]);
        if (move == 2) ftMt_SpecialHiStart_Enter(msl_test_match.fighters[p]);
    }
    for (unsigned i = 0; i < 100; ++i) {
        unsigned active = 0;
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        for (unsigned p = 0; p < 4; ++p) {
            Fighter* fp = msl_test_fighter(p);
            if ((move == 0 && fp->reflecting) ||
                (move == 1 && fp->fv.mt.x222C_disableGObj) ||
                (move == 2 && fp->invisible)) active |= 1U << p;
        }
        if (active == 15) seen = active;
    }
    MSL_TEST_CHECK(seen == 15);
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    printf("four-Mewtwo move=%u FObj=%u/%u AObj=%u/%u\n", move,
           msl_test_fobj_peak, f->used + f->free, msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak < f->used + f->free && msl_test_aobj_peak < a->used + a->free);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_confusion_grab(1, 0, 0) || msl_test_confusion_grab(1, 1, 0) ||
                 msl_test_confusion_grab(0, 0, 0) || msl_test_confusion_grab(1, 0, 1) ||
                 msl_test_confusion_reflection(1, 0, 0) || msl_test_confusion_reflection(0, 0, 0) ||
                 msl_test_confusion_reflection(1, 1, 0) || msl_test_confusion_reflection(1, 0, 1) ||
                 msl_test_confusion_reflection(0, 0, 1) ||
                 msl_test_disable(1, 0) || msl_test_disable(0, 0) || msl_test_disable(1, 1) ||
                 msl_test_teleport(0, 80, 0) || msl_test_teleport(1, 80, 0) ||
                 msl_test_teleport(1, -80, 0) || msl_test_teleport(1, 0, 0) ||
                 msl_test_teleport(1, 0, 80) || msl_test_teleport(1, 0, -80) ||
                 msl_test_confusion_interrupt() || msl_test_confusion_air_boost() ||
                 msl_test_disable_interrupt(0) || msl_test_disable_interrupt(1) ||
                 msl_test_shadow_cancel() || msl_test_shadow_interrupt(0, 0) ||
                 msl_test_shadow_interrupt(1, 0) || msl_test_shadow_interrupt(0, 1) ||
                 msl_test_shadow_interrupt(1, 1) || msl_test_shadow_restore() ||
                 msl_test_shadow_air(0) || msl_test_shadow_air(1) ||
                 msl_test_simultaneous_specials(0) || msl_test_simultaneous_specials(1) ||
                 msl_test_simultaneous_specials(2);
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
