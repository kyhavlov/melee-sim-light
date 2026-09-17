#include "runtime/scalar.h"
#include "ft/types.h"
#include "ftMewtwo/forward.h"
#include "it/types.h"
#include "it/items/itmewtwoshadowball.h"

#include <dolphin/pad.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/objalloc.h>
#include <stdio.h>
#include <string.h>

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static unsigned msl_test_fobj_peak, msl_test_aobj_peak;

static void msl_test_sample_pools(void)
{
    unsigned f = HSD_ObjAllocResolve(HSD_FObjGetAllocData())->used;
    unsigned a = HSD_ObjAllocResolve(HSD_AObjGetAllocData())->used;
    if (f > msl_test_fobj_peak) msl_test_fobj_peak = f;
    if (a > msl_test_aobj_peak) msl_test_aobj_peak = a;
}

static int msl_test_report_pools(const char* scenario, unsigned players)
{
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    printf("%s players=%u FObj=%u/%u AObj=%u/%u\n", scenario, players,
           msl_test_fobj_peak, f->used + f->free, msl_test_aobj_peak, a->used + a->free);
    // Match article_pool_smoke's 20% spare-capacity requirement.
    if (msl_test_fobj_peak * 5 > (f->used + f->free) * 4 ||
        msl_test_aobj_peak * 5 > (a->used + a->free) * 4) {
        fprintf(stderr, "%s left less than 20%% animation storage headroom\n", scenario);
        return -1;
    }
    return 0;
}

static int msl_test_step(const MslCoreInput* input)
{
    size_t used = msl_test_match.memory.used;
    size_t allocations = msl_test_match.memory.allocation_count;
    if (msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                            &(MslCoreStageEvents) { 0 }) != 0 ||
        !msl_test_match.memory.sealed || msl_test_match.memory.used != used ||
        msl_test_match.memory.allocation_count != allocations) {
        fprintf(stderr, "Mewtwo step exceeded initialized storage\n");
        return -1;
    }
    msl_test_sample_pools();
    return 0;
}

static int msl_test_run_charge(unsigned players)
{
    MslCoreMatchConfig config = { 0 };
    MslCoreInput input = { 0 };
    unsigned player;
    unsigned frame;
    unsigned full = 0;
    unsigned fired = 0;

    msl_test_fobj_peak = msl_test_aobj_peak = 0;
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = 1;
    config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1.0f;
    config.num_players = players;
    config.stock_count = 4;
    for (player = 0; player < players; ++player) {
        config.players[player].char_id = 16;
    }
    if ((msl_test_match.memory.arena == NULL
             ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &input)
             : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &input)) != 0) {
        return -1;
    }
    for (frame = 0; frame < 480; ++frame) {
        memset(&input, 0, sizeof(input));
        for (player = 0; player < players; ++player) {
            if (frame == 150) {
                input.p[player].buttons = PAD_BUTTON_B;
            }
        }
        if (msl_test_step(&input) != 0) {
            return -1;
        }
        full = 0;
        for (player = 0; player < players; ++player) {
            Fighter* fp = msl_test_match.fighters[player]->user_data;
            ftMewtwoAttributes* attrs = fp->dat_attrs;
            if (fp->motion_id == ftMt_MS_SpecialNLoopFull &&
                fp->fv.mt.x2234_shadowBallCharge ==
                    (int) attrs->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES &&
                fp->fv.mt.x2230_shadowHeldGObj != NULL)
            {
                Item* held = fp->fv.mt.x2230_shadowHeldGObj->user_data;
                if (held->kind != It_Kind_Mewtwo_ShadowBall ||
                    held->owner != msl_test_match.fighters[player])
                {
                    fprintf(stderr, "Shadow Ball lost its fighter owner\n");
                    return -1;
                }
                ++full;
            }
        }
        if (full == players) {
            break;
        }
    }
    if (full != players) {
        fprintf(stderr, "%u/%u Mewtwo reached full charge\n", full, players);
        return -1;
    }
    for (frame = 0; frame < 60; ++frame) {
        memset(&input, 0, sizeof(input));
        if (frame == 0) {
            for (player = 0; player < players; ++player) {
                input.p[player].buttons = PAD_BUTTON_B;
            }
        }
        if (msl_test_step(&input) != 0) {
            return -1;
        }
        for (player = 0; player < players; ++player) {
            Fighter* fp = msl_test_match.fighters[player]->user_data;
            if (fp->motion_id == ftMt_MS_SpecialNEnd &&
                fp->fv.mt.x2234_shadowBallCharge == 0 &&
                fp->fv.mt.x2230_shadowHeldGObj == NULL)
            {
                fired |= 1u << player;
            }
        }
    }
    if (fired != (1u << players) - 1) {
        fprintf(stderr, "Mewtwo release coverage mask: %x\n", fired);
        return -1;
    }
    return msl_test_report_pools("charge/release", players);
}

static int msl_test_run_throw_projectiles(void)
{
    MslCoreMatchConfig config = msl_test_match.config;
    MslCoreInput input = { 0 };
    unsigned frame;
    unsigned player;
    unsigned shot;
    msl_test_fobj_peak = msl_test_aobj_peak = 0;

    if (msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &input) != 0) {
        return -1;
    }
    for (frame = 0; frame < 150; ++frame) {
        if (msl_test_step(&input) != 0) {
            return -1;
        }
    }
    // Exercise the forward-throw factory with sealed pools and read-only DATs.
    for (player = 0; player < 4; ++player) {
        Fighter* fp = msl_test_match.fighters[player]->user_data;
        ftMewtwoAttributes* attrs = fp->dat_attrs;
        Vec3 position = fp->cur_pos;
        position.y += 10.0f;
        for (shot = 0; shot < 5; ++shot) {
            Item_GObj* gobj = it_802C519C(
                msl_test_match.fighters[player], &position, It_Kind_Mewtwo_ShadowBall,
                (int) attrs->x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES,
                0.0f, fp->facing_dir);
            Item* item;
            if (gobj == NULL) {
                fprintf(stderr, "forward-throw projectile allocation failed\n");
                return -1;
            }
            msl_test_sample_pools();
            item = gobj->user_data;
            if (item->msid != 9 || item->owner != msl_test_match.fighters[player]) {
                fprintf(stderr, "forward-throw projectile state/owner mismatch\n");
                return -1;
            }
        }
    }
    for (frame = 0; frame < 100; ++frame) {
        if (msl_test_step(&input) != 0) {
            return -1;
        }
    }
    return msl_test_report_pools("forward-throw burst", 4);
}

int main(int argc, char** argv)
{
    int result;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1]) != 0) {
        return 1;
    }
    result = msl_test_run_charge(2) || msl_test_run_charge(4) || msl_test_run_throw_projectiles();
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
