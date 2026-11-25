// Pichu shares Pikachu's special-move owners; PlPc.dat authors the Pichu
// differences (its own item kinds in the attribute block, the recoil on
// every electric attack) and ftPk_SpecialHi skips the spark effects for
// FTKIND_PICHU. These scenarios run each special for both mice with
// extracted data and the real scheduler and check that the Pichu item kinds
// spawn, that only Pichu hurts himself, and the four-Pichu pool headroom.
#include "runtime/scalar.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_Fall.h"
#include "ftPikachu/forward.h"
#include "ftPikachu/ftPk_SpecialHi.h"
#include "ftPikachu/ftPk_SpecialLw.h"
#include "ftPikachu/ftPk_SpecialN.h"
#include "ftPikachu/ftPk_SpecialS.h"
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
    if (players == 4) {
        for (unsigned i = 0; i < players; ++i) config.players[i].costume_id = i;
    }
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

// Records every item kind alive during a scenario as a bitmask over the
// kinds we care about.
enum { MSL_SEEN_PK_THUNDER = 1, MSL_SEEN_PC_THUNDER = 2, MSL_SEEN_PK_JOLT = 4, MSL_SEEN_PC_JOLT = 8 };
static unsigned msl_test_item_kinds(void)
{
    unsigned seen = 0;
    for (HSD_GObj* g = HSD_GObj_Entities->items; g; g = g->next) {
        int kind = ((Item*) g->user_data)->kind;
        if (kind == It_Kind_Pikachu_Thunder) seen |= MSL_SEEN_PK_THUNDER;
        if (kind == It_Kind_Pichu_Thunder) seen |= MSL_SEEN_PC_THUNDER;
        if (kind == It_Kind_Pikachu_TJolt_Ground || kind == It_Kind_Pikachu_TJolt_Air) seen |= MSL_SEEN_PK_JOLT;
        if (kind == It_Kind_Pichu_TJolt_Ground || kind == It_Kind_Pichu_TJolt_Air) seen |= MSL_SEEN_PC_JOLT;
    }
    return seen;
}

// Thunder Jolt: the attribute block's item kind selects the mouse's own
// jolt; the jolt crawls into a standing Fox. Pichu pays the authored recoil.
static int msl_test_jolt(unsigned kind, float* self, float* fox)
{
    unsigned seen = 0;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 30, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftPk_SpecialN_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 90; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        seen |= msl_test_item_kinds();
    }
    *self = msl_test_fighter(0)->dmg.x1830_percent;
    *fox = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "jolt kind=%u seen=%#x self=%g fox=%g\n", kind, seen, *self, *fox);
    MSL_TEST_CHECK(seen == (kind == 23 ? MSL_SEEN_PC_JOLT : MSL_SEEN_PK_JOLT));
    MSL_TEST_CHECK(*fox > 0);
    return 0;
}

// Thunder: the bolt item kind is likewise authored per mouse; it falls onto
// its owner, which is free for Pikachu and costs Pichu.
static int msl_test_thunder(unsigned kind, float* self)
{
    unsigned seen = 0;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftPk_SpecialLw_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 120; ++i) {
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        seen |= msl_test_item_kinds();
    }
    *self = msl_test_fighter(0)->dmg.x1830_percent;
    fprintf(stderr, "thunder kind=%u seen=%#x self=%g motion=%d\n", kind, seen, *self,
            msl_test_fighter(0)->motion_id);
    MSL_TEST_CHECK(seen == (kind == 23 ? MSL_SEEN_PC_THUNDER : MSL_SEEN_PK_THUNDER));
    return 0;
}

// Skull Bash: hold the charge, release into a close Fox.
static int msl_test_skull_bash(unsigned kind, float* self, float* fox)
{
    MslCoreInput input = { 0 };
    int hit_state = 0;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 40, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftPk_SpecialS_Enter(msl_test_match.fighters[0]);
    input.p[0].buttons = PAD_BUTTON_B;
    for (unsigned i = 0; i < 160; ++i) {
        MSL_TEST_CHECK(msl_test_step(i < 40 ? &input : &msl_test_neutral) == 0);
        int m = msl_test_fighter(0)->motion_id;
        if (m == ftPk_MS_SpecialS0 || m == ftPk_MS_SpecialS1 || m == ftPk_MS_SpecialSEnd) hit_state = 1;
    }
    *self = msl_test_fighter(0)->dmg.x1830_percent;
    *fox = msl_test_fighter(1)->dmg.x1830_percent;
    fprintf(stderr, "skull bash kind=%u launched=%d self=%g fox=%g\n", kind, hit_state, *self, *fox);
    MSL_TEST_CHECK(hit_state);
    MSL_TEST_CHECK(*fox > 0);
    return 0;
}

// Quick Attack: two zips (up, then forward) from the ground; Pichu skips
// the spark effects in ftPk_SpecialHiStart1_Anim and pays recoil per zip.
static int msl_test_quick_attack(unsigned kind, float* self)
{
    MslCoreInput input = { 0 };
    int second = 0, rose = 0;
    float start;
    MSL_TEST_CHECK(msl_test_setup(2, kind, 1) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    start = msl_test_fighter(0)->cur_pos.y;
    ftPk_SpecialHi_Enter(msl_test_match.fighters[0]);
    for (unsigned i = 0; i < 90; ++i) {
        int m = msl_test_fighter(0)->motion_id;
        input.p[0].main_y = (m == ftPk_MS_SpecialHiStart0 || m == ftPk_MS_SpecialAirHiStart0) ? 80 : 0;
        input.p[0].main_x = (m == ftPk_MS_SpecialHiStart1 || m == ftPk_MS_SpecialAirHiStart1) ? 80 : 0;
        MSL_TEST_CHECK(msl_test_step(&input) == 0);
        m = msl_test_fighter(0)->motion_id;
        if (m == ftPk_MS_SpecialAirHiStart1 || m == ftPk_MS_SpecialHiStart1) second = 1;
        if (msl_test_fighter(0)->cur_pos.y > start + 10) rose = 1;
    }
    *self = msl_test_fighter(0)->dmg.x1830_percent;
    fprintf(stderr, "quick attack kind=%u second=%d rose=%d self=%g\n", kind, second, rose, *self);
    MSL_TEST_CHECK(second && rose);
    return 0;
}

// Four Pichus firing jolts and thunders at once keep the sealed pools inside
// the existing reserves with the article smoke's 20% headroom rule.
static int msl_test_four_pichus(void)
{
    MSL_TEST_CHECK(msl_test_setup(4, 23, 23) == 0);
    for (unsigned p = 0; p < 4; ++p) msl_test_place(p, -60 + 40 * (int) p, 0, p < 2 ? 1 : -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    ftPk_SpecialN_Enter(msl_test_match.fighters[0]);
    ftPk_SpecialLw_Enter(msl_test_match.fighters[1]);
    ftPk_SpecialN_Enter(msl_test_match.fighters[2]);
    ftPk_SpecialLw_Enter(msl_test_match.fighters[3]);
    for (unsigned i = 0; i < 200; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    HSD_ObjAllocData* f = HSD_ObjAllocResolve(HSD_FObjGetAllocData());
    HSD_ObjAllocData* a = HSD_ObjAllocResolve(HSD_AObjGetAllocData());
    fprintf(stderr, "four-Pichu FObj=%u/%u AObj=%u/%u\n", msl_test_fobj_peak, f->used + f->free,
            msl_test_aobj_peak, a->used + a->free);
    MSL_TEST_CHECK(msl_test_fobj_peak * 5 < (f->used + f->free) * 4);
    MSL_TEST_CHECK(msl_test_aobj_peak * 5 < (a->used + a->free) * 4);
    return 0;
}

int main(int argc, char** argv)
{
    float pc_jolt_self, pc_jolt_fox, pk_jolt_self, pk_jolt_fox;
    float pc_thunder_self, pk_thunder_self, pc_bash_self, pc_bash_fox, pk_bash_self, pk_bash_fox;
    float pc_qa_self, pk_qa_self;
    if (argc != 2 || msl_core_game_data_init(&msl_test_game_data, argv[1])) return 1;
    int result = msl_test_jolt(23, &pc_jolt_self, &pc_jolt_fox) ||
                 msl_test_jolt(12, &pk_jolt_self, &pk_jolt_fox) ||
                 msl_test_thunder(23, &pc_thunder_self) || msl_test_thunder(12, &pk_thunder_self) ||
                 msl_test_skull_bash(23, &pc_bash_self, &pc_bash_fox) ||
                 msl_test_skull_bash(12, &pk_bash_self, &pk_bash_fox) ||
                 msl_test_quick_attack(23, &pc_qa_self) || msl_test_quick_attack(12, &pk_qa_self) ||
                 msl_test_four_pichus();
    if (result == 0) {
        // PlPc.dat authors Pichu's recoil: 1% per jolt, 3% when his own bolt
        // lands on him, 1% for a Skull Bash and 1% for a two-zip Quick
        // Attack. Pikachu's electric moves are free. The 40-frame Skull Bash
        // charge lands 9% from Pichu and 11% from Pikachu on the same Fox.
        if (pc_jolt_self != 1 || pc_thunder_self != 3 || pc_bash_self != 1 || pc_qa_self != 1 ||
            pk_jolt_self != 0 || pk_thunder_self != 0 || pk_bash_self != 0 || pk_qa_self != 0 ||
            pc_jolt_fox != 7 || pk_jolt_fox != 7 || pc_bash_fox != 9 || pk_bash_fox != 11) {
            fprintf(stderr, "recoil or payload mismatch\n");
            result = -1;
        }
    }
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
